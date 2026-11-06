#
# SPDX-FileCopyrightText: The LineageOS Project
# SPDX-License-Identifier: Apache-2.0
#

from __future__ import annotations

import base64
import binascii
import hashlib
import json
import os
import struct
import xml.etree.ElementTree as ET
from contextlib import suppress
from os import path
from typing import Any, Dict, List, NamedTuple, Optional, Tuple

from extract_utils.file import File, FileArgs, FileList
from extract_utils.fixups_blob import BlobFixupCtx, blob_fixup
from extract_utils.module import ExtractUtilsModule, GeneratedProprietaryFile
from extract_utils.postprocess import PostprocessCtx
from extract_utils.utils import run_cmd_bytes

THERMAL_ENGINE_CONFIG = 'vendor/etc/thermal-engine.conf'
THERMAL_CONTROL_CONFIG = (
    'odm/etc/temperature_profile/sys_thermal_control_config.xml'
)
HORAE_CONFIG_DIR = 'etc/horae'
HORAE_CONFIG = f'system_ext/{HORAE_CONFIG_DIR}/horae.conf'
HORAE_TARGET_CONFIG = 'odm/etc/horae/horae_target.conf'

HORAE_KEY = '00010aae69ca90b74e9ffa45e316d752'
THERMAL_CONTROL_KEY = hashlib.sha256(b'thermal').hexdigest()[:16].encode()

# Size of the threshold array in thermal-engine-v2 monitor settings.
MAX_THRESHOLDS = 12
INT_MAX = 2**31 - 1
NO_LIMIT = INT_MAX
SAMPLING_MS = 1000


class StatusModel(NamedTuple):
    up: List[int]
    down: List[int]
    status: List[int]


class ControlConfig(NamedTuple):
    cpu_levels: Dict[int, List[int]]
    gpu_levels: Dict[int, int]
    # tempGear -> (cpu level, gpu level)
    gears: Dict[int, Tuple[int, int]]
    clusters: int
    # Higher CPU limits stock allows for touch and launch boosts
    cpu_boost_levels: Dict[int, List[int]]


class Level(NamedTuple):
    temp: int
    clr: int
    limits: List[int]
    # CPU limits for when the user is not interacting, if stricter than limits
    idle: Optional[List[int]] = None


def get_member(value: Any, *keys: str) -> Any:
    for key in keys:
        if not isinstance(value, dict):
            return None
        value = value.get(key)
    return value


def parse_int(value: Optional[str], default: int = -1) -> int:
    try:
        return int(value)  # type: ignore[arg-type]
    except (TypeError, ValueError):
        return default


def read_horae_config(horae_config_path: str) -> Dict[str, Any]:
    with open(horae_config_path, 'rb') as f:
        data = f.read()

    if not data or len(data) % 16:
        raise ValueError(f'Failed to read {horae_config_path}')

    plain = run_cmd_bytes(
        [
            'openssl',
            'enc',
            '-d',
            '-aes-128-cbc',
            '-K',
            HORAE_KEY,
            '-iv',
            '0' * 32,
            '-nopad',
        ],
        data=data,
    )

    # Plaintext is <u32 LE length><JSON><junk>.
    (length,) = struct.unpack_from('<I', plain)
    if length > len(plain) - 4:
        raise ValueError(f'Failed to decrypt {horae_config_path}')

    root = json.loads(plain[4 : 4 + length])
    if not isinstance(root, dict):
        raise ValueError(f'Failed to parse {horae_config_path}')

    return root


def select_project(root: Dict[str, Any], project: Optional[str]):
    if project and isinstance(root.get(project), dict):
        return root[project]

    for name in sorted(root):
        if (
            isinstance(root[name], dict)
            and 'shell_temp_fitting_model' in root[name]
        ):
            return root[name]

    return root


def get_shell_sensors(project: Dict[str, Any]) -> List[str]:
    models = (
        get_member(project, 'shell_temp_fitting_model', 'default', 'temp_model')
        or {}
    )

    # Negative index (board) doesn't feed a shell zone, <name>_N are extra hot
    # spots of <name>.
    return sorted(
        {
            f'shell_{name.split("_")[0]}'
            for name, model in models.items()
            if int(get_member(model, 'index') or 0) >= 0
        }
    )


def parse_status_model(value: Any) -> Optional[StatusModel]:
    if not isinstance(value, dict):
        return None

    model = StatusModel(
        [int(e) for e in value.get('temp_threshold', [])],
        [int(e) for e in value.get('temp_threshold_down', [])],
        [int(e) for e in value.get('thermal_status', [])],
    )

    if (
        not model.status
        or len(model.up) != len(model.status)
        or len(model.down) != len(model.status)
    ):
        return None

    return model


def decrypt_control_config(data: bytes) -> bytes:
    # base64(<u8 IV length><IV><AES-GCM ciphertext><16 byte tag>)
    data = base64.b64decode(b''.join(data.split()), validate=True)
    iv_len = data[0]
    if iv_len != 12:
        raise ValueError(f'Unsupported IV length {iv_len}')

    iv = data[1 : 1 + iv_len]
    ciphertext = data[1 + iv_len : -16]

    # GCM with a 96-bit IV is CTR starting at <IV><u32 BE 2>, the tag is left
    # unchecked.
    return run_cmd_bytes(
        [
            'openssl',
            'enc',
            '-d',
            '-aes-128-ctr',
            '-K',
            THERMAL_CONTROL_KEY.hex(),
            '-iv',
            (iv + struct.pack('>I', 2)).hex(),
        ],
        data=ciphertext,
    )


def read_control_config(control_config_path: str) -> ControlConfig:
    root = ET.parse(control_config_path).getroot()
    if root.tag != 'sys_thermal_control_list':
        raise ValueError(f'Failed to parse {control_config_path}')

    config = ControlConfig({}, {}, {}, 0, {})
    clusters = 0

    for feature in root.iterfind('feature'):
        for c in feature.iterfind('config'):
            for level in c.iterfind('level'):
                index = parse_int(level.get('index'))

                if c.get('name') == 'thermal_cpulevel':
                    freqs = [
                        parse_int(freq)
                        for freq in (level.get('maxCpuFreq') or '').split(',')
                    ]
                    clusters = max(clusters, len(freqs))
                    config.cpu_levels[index] = freqs
                    boost = [
                        parse_int(freq)
                        for freq in (level.get('boostCeiling') or '').split(',')
                    ]
                    if len(boost) == len(freqs):
                        config.cpu_boost_levels[index] = boost
                elif c.get('name') == 'thermal_gpulevel':
                    config.gpu_levels[index] = parse_int(
                        level.get('maxGpuFreq')
                    )

    policy = root.find('thermalPolicyConfigItem/globalPolicy/globalPolicy')
    if policy is None:
        policy = root.find('thermalPolicyConfigItem/default/common_config')

    for gear in policy.iterfind('gear_config') if policy is not None else []:
        config.gears[parse_int(gear.get('tempGear'))] = (
            parse_int(gear.get('cpu')),
            parse_int(gear.get('gpu')),
        )

    if not config.gears:
        raise ValueError(
            f'No global or default thermal policy in {control_config_path}'
        )

    return config._replace(clusters=clusters)


def build_levels(
    control: ControlConfig,
    model: StatusModel,
    boost_ceiling_below: Optional[int] = None,
) -> List[Level]:
    """
    With boost_ceiling_below, CPU limits are the stock boost ceilings, the
    limits stock allows during touch and launch boosts. Levels from
    boost_ceiling_below (C) up also carry the stock limits as idle limits, for
    the power HAL to apply while the user is not interacting.
    """
    levels: List[Level] = []
    cpu_limits = [NO_LIMIT] * control.clusters
    cpu_idle = [NO_LIMIT] * control.clusters
    gpu_limit = NO_LIMIT

    for up, down, status in zip(model.up, model.down, model.status):
        if up < 35 or status not in control.gears:
            continue

        cpu_level, gpu_level = control.gears[status]

        freqs = control.cpu_levels.get(cpu_level, [])
        boost = control.cpu_boost_levels.get(cpu_level, freqs)
        if boost_ceiling_below is None:
            boost = freqs

        for c, (freq, ceiling) in enumerate(zip(freqs, boost)):
            if freq >= 0:
                cpu_idle[c] = freq
                cpu_limits[c] = max(ceiling, freq)

        if control.gpu_levels.get(gpu_level, -1) >= 0:
            gpu_limit = control.gpu_levels[gpu_level]

        limits = cpu_limits + [gpu_limit]
        idle = None
        if (
            boost_ceiling_below is not None
            and up >= boost_ceiling_below
            and cpu_idle != cpu_limits
        ):
            idle = list(cpu_idle)
            # The power HAL finds the idle limits from the CPU limits in
            # effect, so a hotter level with the same CPU limits as a cooler
            # one keeps that level's idle limits.
            for level in levels:
                if level.limits[: control.clusters] == cpu_limits:
                    idle = level.idle
                    break

        if levels:
            redundant = levels[-1].limits == limits and levels[-1].idle == idle
        else:
            redundant = all(limit == NO_LIMIT for limit in limits)

        if not redundant:
            levels.append(Level(up * 1000, down * 1000, limits, idle))

    return levels


def format_values(key: str, values: List[Any]) -> str:
    return ' '.join([key, *map(str, values)])


def format_section(name: str, *lines: str) -> str:
    return f'[{name}]\n' + ''.join(f'{line}\n' for line in lines) + '\n'


def generate_thermal_engine_config(
    control_config_path: str,
    horae_config_path: str,
    horae_target_config_path: str,
    project_name: Optional[str] = None,
    boost_ceiling_below: Optional[int] = None,
) -> str:
    control = read_control_config(control_config_path)
    horae = read_horae_config(horae_target_config_path)

    project = select_project(horae, project_name)
    sensors = get_shell_sensors(project)
    if not sensors:
        raise ValueError(
            f'No shell temp fitting models in {horae_target_config_path}'
        )

    model = (
        parse_status_model(project.get('thermal_status_model'))
        or parse_status_model(horae.get('thermal_status_model'))
        or parse_status_model(
            read_horae_config(horae_config_path).get('thermal_status_model')
        )
    )
    if model is None:
        raise ValueError(f'No thermal_status_model in {horae_config_path}')

    levels = build_levels(control, model, boost_ceiling_below)
    if not levels:
        raise ValueError(f'No thermal levels in {control_config_path}')

    devices = [f'cpu-cluster{c}' for c in range(control.clusters)] + ['gpu']
    actions = '+'.join(devices)

    config = ''
    if any(level.idle for level in levels):
        # Read by the power HAL: while the user is not interacting it applies
        # the idle CPU limits of the CPU limits thermal-engine has in effect.
        config += '# Interactive CPU limits: <limits> <idle limits>\n'
        for level in levels:
            if level.idle:
                config += format_values(
                    '# idle_limits',
                    [
                        '+'.join(map(str, level.limits[: control.clusters])),
                        '+'.join(map(str, level.idle)),
                    ],
                )
                config += '\n'
        config += '\n'

    for sensor in sensors:
        # Shell zones have no trip notifications. Keep a virtual sensor active
        # so its timed wait polls the underlying temperature. A descending gate
        # also stays active when the engine's cached trip temperature is
        # INT_MIN.
        polled_sensor = f'{sensor}-poll'
        config += format_section(
            polled_sensor,
            'algo_type virtual',
            f'trip_sensor {sensor}',
            f'thresholds {INT_MAX - 1}',
            f'thresholds_clr {INT_MAX}',
            'descending',
            f'sensors {sensor}',
            'weights 1',
            f'sampling {SAMPLING_MS}',
        )

        # Levels past MAX_THRESHOLDS go to extra monitors, thermal-engine
        # applies the lowest requested limit per device.
        for i, first in enumerate(range(0, len(levels), MAX_THRESHOLDS)):
            chunk = levels[first : first + MAX_THRESHOLDS]
            config += format_section(
                f'{sensor}-{i}',
                'algo_type monitor',
                f'sensor {polled_sensor}',
                f'sampling {SAMPLING_MS}',
                format_values('thresholds', [level.temp for level in chunk]),
                format_values('thresholds_clr', [level.clr for level in chunk]),
                format_values('actions', [actions] * len(chunk)),
                format_values(
                    'action_info',
                    ['+'.join(map(str, level.limits)) for level in chunk],
                ),
            )

    return config


def find_extracted_file(module: ExtractUtilsModule, file_dst: str) -> str:
    for proprietary_file in module.proprietary_files:
        for file in proprietary_file.file_list.all_files:
            if file.dst != file_dst:
                continue

            file_path = path.join(
                module.proprietary_file_vendor_path(proprietary_file),
                file.dst,
            )
            if path.isfile(file_path):
                return file_path

    raise ValueError(f'{file_dst} has to be extracted by {module.device}')


def add_generated_thermal_engine_config(
    module: ExtractUtilsModule,
    project_name: Optional[str] = None,
    horae_target_config: str = HORAE_TARGET_CONFIG,
    boost_ceiling_below: Optional[int] = None,
) -> GeneratedProprietaryFile:
    """
    Generate /vendor/etc/thermal-engine.conf out of the stock
    sys_thermal_control_config.xml gear CPU/GPU limits and horae shell
    sensors, tripping on horae's thermal_status_model thresholds.

    The stock config is replaced by a blob fixup, its inputs are extracted
    for it and removed afterwards. An encrypted sys_thermal_control_config.xml
    is decrypted by a blob fixup too. horae_target_config is the extracted
    destination path of the target Horae config and has to be extracted by
    the module beforehand. project_name selects its entry, the first one
    with shell sensors is used if it's missing.

    With boost_ceiling_below set, thermal-engine limits the CPUs to the stock
    boost ceilings, like stock does during touch and launch boosts. From that
    temperature (C) on, the stock limits are listed as idle limits for the
    power HAL to apply while the user is not interacting.
    """

    def fix_file_list(file_list: FileList):
        file_list.get_file(HORAE_CONFIG).set_arg(FileArgs.EXTRACT_ONLY, True)
        file_list.add_from_lines(
            [
                f'{THERMAL_CONTROL_CONFIG};EXTRACT_ONLY',
                THERMAL_ENGINE_CONFIG,
            ]
        )

    def generate_fn(
        ctx: BlobFixupCtx,
        file: File,
        file_path: str,
        *args: Any,
        **kwargs: Any,
    ):
        config = generate_thermal_engine_config(
            find_extracted_file(module, THERMAL_CONTROL_CONFIG),
            find_extracted_file(module, HORAE_CONFIG),
            find_extracted_file(module, horae_target_config),
            project_name,
            boost_ceiling_below,
        ).strip()

        with open(file_path, 'w', encoding='utf-8') as f:
            f.write(config)
            f.write('\n')

    def decrypt_fn(
        ctx: BlobFixupCtx,
        file: File,
        file_path: str,
        *args: Any,
        **kwargs: Any,
    ):
        with open(file_path, 'rb') as f:
            data = f.read()

        # Like stock, try decrypting first and keep the file as is otherwise.
        with suppress(binascii.Error):
            data = decrypt_control_config(data)
            with open(file_path, 'wb') as f:
                f.write(data)

    # Run any device fixups on top of the generated and decrypted files
    for file_dst, fn in [
        (THERMAL_CONTROL_CONFIG, decrypt_fn),
        (THERMAL_ENGINE_CONFIG, generate_fn),
    ]:
        fixup = blob_fixup().call(fn, need_tmp_dir=False)
        if file_dst in module.blob_fixups:
            fixup.merge(module.blob_fixups[file_dst])
        module.blob_fixups[file_dst] = fixup

    # Only the generated list partition gets extracted for it, odm and vendor
    # are covered by the module already.
    proprietary_file = module.add_generated_proprietary_file(
        'proprietary-files-thermal-engine.txt',
        partition='system_ext',
        rel_path=HORAE_CONFIG_DIR,
        regex=r'^horae\.conf$',
        fix_file_list=fix_file_list,
    )

    def remove_inputs_fn(ctx: PostprocessCtx):
        vendor_path = module.proprietary_file_vendor_path(proprietary_file)

        for file in proprietary_file.file_list.all_files:
            if FileArgs.EXTRACT_ONLY not in file.args:
                continue

            file_path = path.join(vendor_path, file.dst)
            with suppress(FileNotFoundError):
                os.remove(file_path)

            # Stops at the first non-empty parent
            with suppress(OSError):
                os.removedirs(path.dirname(file_path))

    module.add_postprocess_fn(remove_inputs_fn)

    return proprietary_file
