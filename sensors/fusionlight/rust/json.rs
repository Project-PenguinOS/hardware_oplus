/*
 * SPDX-FileCopyrightText: The LineageOS Project
 * SPDX-License-Identifier: Apache-2.0
 */

//! The jsoncpp reading rules the FusionLight profiles were written against, on serde_json values.
//! Lookups on the wrong type give null instead of aborting like jsoncpp's asserts did.

use serde_json::Value;

static NULL: Value = Value::Null;

/// `value[name]`: the member, or null when `value` is not an object or has no such member.
pub fn member<'a>(value: &'a Value, name: &str) -> &'a Value {
    value.as_object().and_then(|o| o.get(name)).unwrap_or(&NULL)
}

pub fn is_member(value: &Value, name: &str) -> bool {
    value.as_object().is_some_and(|o| o.contains_key(name))
}

/// The elements of an array, or the member values of an object, as jsoncpp iterates them.
pub fn elements(value: &Value) -> Vec<&Value> {
    match value {
        Value::Array(a) => a.iter().collect(),
        Value::Object(o) => o.values().collect(),
        _ => vec![],
    }
}

/// jsoncpp size(): elements of an array or object, 0 otherwise.
pub fn size(value: &Value) -> usize {
    match value {
        Value::Array(a) => a.len(),
        Value::Object(o) => o.len(),
        _ => 0,
    }
}

/// jsoncpp asBool(): numbers are true when non-zero; strings, which jsoncpp aborted on, are false.
pub fn as_bool(value: &Value) -> bool {
    match value {
        Value::Bool(b) => *b,
        Value::Number(n) => n.as_f64().is_some_and(|v| v != 0.0),
        _ => false,
    }
}

/// jsoncpp isInt() then asInt(): integers, and integral reals, within the int32 range.
fn as_int(value: &Value) -> Option<i32> {
    let Value::Number(n) = value else { return None };
    if let Some(v) = n.as_i64() {
        return i32::try_from(v).ok();
    }
    if let Some(v) = n.as_u64() {
        return i32::try_from(v).ok();
    }
    let v = n.as_f64()?;
    (v.fract() == 0.0 && v >= f64::from(i32::MIN) && v <= f64::from(i32::MAX)).then_some(v as i32)
}

/// android::base::ParseInt: strtoll with base 0 (0x hex, 0 octal), whole string, int32 range.
pub fn parse_int(text: &str) -> Option<i32> {
    let (negative, digits) = match text.as_bytes().first()? {
        b'-' => (true, &text[1..]),
        b'+' => (false, &text[1..]),
        _ => (false, text),
    };
    let (radix, digits) = if let Some(hex) = digits.strip_prefix("0x").or_else(|| digits.strip_prefix("0X")) {
        (16, hex)
    } else if digits.len() > 1 && digits.starts_with('0') {
        (8, &digits[1..])
    } else {
        (10, digits)
    };
    if digits.is_empty() || !digits.chars().all(|c| c.is_digit(radix)) {
        return None;
    }
    let magnitude = i64::from_str_radix(digits, radix).ok()?;
    i32::try_from(if negative { -magnitude } else { magnitude }).ok()
}

/// ReadInt: an int, or a string holding one after trimming.
pub fn read_int(value: &Value) -> Option<i32> {
    match value {
        Value::String(s) => parse_int(s.trim()),
        _ => as_int(value),
    }
}

/// ReadNumber: a finite number, or a string holding one after trimming.
pub fn read_number(value: &Value) -> Option<f64> {
    let number = match value {
        Value::String(s) => s.trim().parse::<f64>().ok()?,
        Value::Number(n) => n.as_f64()?,
        _ => return None,
    };
    number.is_finite().then_some(number)
}

/// ReadRequiredInt: `object[name]` as an int, when `object` is an object that has it.
pub fn read_required_int(object: &Value, name: &str) -> Option<i32> {
    if !object.is_object() || !is_member(object, name) {
        return None;
    }
    read_int(member(object, name))
}

#[cfg(test)]
mod tests {
    use super::*;
    use serde_json::json;

    #[test]
    fn ints_like_jsoncpp() {
        assert_eq!(read_int(&json!(7)), Some(7));
        assert_eq!(read_int(&json!(7.0)), Some(7));
        assert_eq!(read_int(&json!(7.5)), None);
        assert_eq!(read_int(&json!(" 0x1F ")), Some(31));
        assert_eq!(read_int(&json!("010")), Some(8));
        assert_eq!(read_int(&json!("-3")), Some(-3));
        assert_eq!(read_int(&json!("3a")), None);
        assert_eq!(read_int(&json!(4294967296u64)), None);
        assert_eq!(read_int(&json!(true)), None);
    }

    #[test]
    fn numbers_like_jsoncpp() {
        assert_eq!(read_number(&json!(" 1.5 ")), Some(1.5));
        assert_eq!(read_number(&json!(2)), Some(2.0));
        assert_eq!(read_number(&json!("inf")), None);
        assert_eq!(read_number(&json!(null)), None);
        assert_eq!(read_number(&json!(true)), None);
    }

    #[test]
    fn numbers_round_like_strtod() {
        // Inputs the default serde_json float parser read one ulp away from strtod.
        for text in [
            "16006.777224800475", "36584.367584216096", "36350.312303214756", "14585.551217544275",
            "57.56586296187565", "481.2120600998082", "3762.124428364747", "1524.1724145980227",
            "1838.3807536658787", "3819.67713057116", "2752.598708397492", "1872.3979252454794",
            "1400.252616518665", "2804.249259683675", "561.0908415388126", "1927.0041257035186",
            "0.065", "0.3", "1e-7", "4294967296",
        ] {
            let value: Value = serde_json::from_str(text).unwrap();
            assert_eq!(read_number(&value), text.parse::<f64>().ok(), "{text}");
        }
    }

    #[test]
    fn wrong_types_read_as_null() {
        let v = json!([1, 2]);
        assert!(member(&v, "a").is_null());
        assert!(!is_member(&json!(3), "a"));
        assert!(!as_bool(&json!("true")));
        assert!(as_bool(&json!(1)));
        assert_eq!(size(&json!(null)), 0);
    }
}
