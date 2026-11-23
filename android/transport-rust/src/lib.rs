use jni::JNIEnv;
use jni::errors::Error as JniError;
use jni::objects::{JByteArray, JClass};
use jni::sys::{jint, jlong, jstring};
use std::ptr;

const MAX_FRAME_BYTES: u32 = 64 * 1024;
const CHUNK_BYTES: i64 = 40 * 1024;
const MAX_FILE_BYTES: i64 = 32 * 1024 * 1024;

fn frame_length(value: u32) -> Result<u32, String> {
    if (1..=MAX_FRAME_BYTES).contains(&value) {
        Ok(value)
    } else {
        Err(format!("消息帧长度须在 1–65536 字节，实际为 {value}"))
    }
}

fn decode_payload(payload: &[u8]) -> Result<&str, String> {
    std::str::from_utf8(payload)
        .map_err(|error| format!("服务器消息 UTF-8 无效，字节位置 {}", error.valid_up_to()))
}

fn chunk_length(size: i64, index: i32) -> Result<i32, String> {
    if size == 0 && index == 0 {
        return Ok(0);
    }

    let offset = i64::from(index) * CHUNK_BYTES;
    if !(1..=MAX_FILE_BYTES).contains(&size) || offset < 0 || offset >= size {
        return Err(format!("附件分块超出范围：{index}，文件大小 {size}"));
    }

    Ok((size - offset).min(CHUNK_BYTES) as i32)
}

fn java_result<T>(env: &mut JNIEnv, result: Result<T, String>, exception_value: T) -> T {
    match result {
        Ok(value) => value,
        Err(detail) => {
            if !matches!(env.exception_check(), Ok(true)) {
                match env.throw_new("java/io/IOException", detail) {
                    Ok(()) | Err(JniError::JavaException) => {}
                    Err(error) => env.fatal_error(error.to_string()),
                }
            }
            exception_value
        }
    }
}

#[unsafe(no_mangle)]
pub extern "system" fn Java_dev_dusk1ng_lanchat_TransportCodec_readFrameLength(
    mut env: JNIEnv,
    _class: JClass,
    header: JByteArray,
) -> jint {
    let mut bytes = [0; 4];
    let result = env
        .get_byte_array_region(&header, 0, &mut bytes)
        .map_err(|error| error.to_string())
        .and_then(|()| frame_length(u32::from_be_bytes(bytes.map(|byte| byte as u8))))
        .map(|length| length as jint);
    java_result(&mut env, result, 0)
}

#[unsafe(no_mangle)]
pub extern "system" fn Java_dev_dusk1ng_lanchat_TransportCodec_writeFrameLength(
    mut env: JNIEnv,
    _class: JClass,
    length: jint,
    header: JByteArray,
) {
    let result = frame_length(length as u32).and_then(|length| {
        let bytes = length.to_be_bytes().map(|byte| byte as i8);
        env.set_byte_array_region(&header, 0, &bytes)
            .map_err(|error| error.to_string())
    });
    java_result(&mut env, result, ());
}

#[unsafe(no_mangle)]
pub extern "system" fn Java_dev_dusk1ng_lanchat_TransportCodec_decodePayload(
    mut env: JNIEnv,
    _class: JClass,
    payload: JByteArray,
) -> jstring {
    let result = env
        .convert_byte_array(&payload)
        .map_err(|error| error.to_string())
        .and_then(|bytes| {
            let text = decode_payload(&bytes)?;
            env.new_string(text)
                .map(|value| value.into_raw())
                .map_err(|error| error.to_string())
        });
    java_result(&mut env, result, ptr::null_mut())
}

#[unsafe(no_mangle)]
pub extern "system" fn Java_dev_dusk1ng_lanchat_TransportCodec_chunkLength(
    mut env: JNIEnv,
    _class: JClass,
    size: jlong,
    index: jint,
) -> jint {
    java_result(&mut env, chunk_length(size, index), 0)
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn checks_wire_frame_limits() {
        assert_eq!(frame_length(65536).unwrap().to_be_bytes(), [0, 1, 0, 0]);
        assert_eq!(frame_length(1).unwrap().to_be_bytes(), [0, 0, 0, 1]);
        assert!(frame_length(0).is_err());
        assert!(frame_length(65537).is_err());
    }

    #[test]
    fn preserves_valid_utf8_and_rejects_invalid_bytes() {
        assert_eq!(decode_payload("聊天 🐱".as_bytes()).unwrap(), "聊天 🐱");
        assert!(decode_payload(&[0xc0, 0xaf]).is_err());
    }

    #[test]
    fn checks_attachment_chunks() {
        assert_eq!(chunk_length(0, 0).unwrap(), 0);
        assert_eq!(chunk_length(40963, 0).unwrap(), 40960);
        assert_eq!(chunk_length(40963, 1).unwrap(), 3);
        assert!(chunk_length(40963, 2).is_err());
        assert!(chunk_length(40963, -1).is_err());
        assert!(chunk_length(33554433, 0).is_err());
    }
}
