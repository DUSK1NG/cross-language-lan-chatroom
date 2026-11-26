use std::time::Duration;

use anyhow::{Context, Result, bail, ensure};
use serde_json::{Value, json};
use tauri::Url;

#[tauri::command]
pub(crate) async fn astrbot_settings(
    endpoint: String,
    token: String,
    resource: String,
    operation: String,
    id: String,
    fields: Value,
) -> Result<Value, String> {
    tauri::async_runtime::spawn_blocking(move || {
        settings(&endpoint, &token, &resource, &operation, &id, &fields)
            .map_err(|error| format!("{error:#}"))
    })
    .await
    .map_err(|_| "AstrBot 请求执行失败".to_owned())?
}

fn settings(
    endpoint: &str,
    token: &str,
    resource: &str,
    operation: &str,
    id: &str,
    fields: &Value,
) -> Result<Value> {
    let base = Url::parse(endpoint).map_err(|_| anyhow::anyhow!("AstrBot 地址格式无效"))?;
    ensure!(
        base.scheme() == "https"
            && base.host().is_some()
            && base.path() == "/"
            && base.username().is_empty()
            && base.password().is_none()
            && base.query().is_none()
            && base.fragment().is_none()
            && !endpoint.contains('\\')
            && !endpoint.chars().any(char::is_whitespace),
        "AstrBot 地址须为 HTTPS 根地址"
    );
    ensure!(
        !token.is_empty()
            && token.len() <= 1024
            && token.bytes().all(|byte| byte.is_ascii_graphic()),
        "请填写 AstrBot OpenAPI Key"
    );
    let (route, id_field, data_field, id_key) = match resource {
        "source" => ("provider-sources", "source_id", "provider_source", "id"),
        "provider" => ("providers", "provider_id", "provider", "id"),
        "persona" => ("personas", "persona_id", "", "persona_id"),
        _ => bail!("AstrBot 配置类型无效"),
    };
    let agent: ureq::Agent = ureq::Agent::config_builder()
        .timeout_global(Some(Duration::from_secs(12)))
        .max_redirects(0)
        .http_status_as_error(false)
        .build()
        .into();
    let collection = base.join(&format!("api/v1/{route}"))?;
    let list = || -> Result<Vec<String>> {
        let response = exchange(&agent, &collection, token, "GET", &Value::Null)?;
        let data = match resource {
            "source" => &response["provider_sources"],
            "provider" => &response["providers"],
            _ => &response,
        };
        data.as_array()
            .context("AstrBot 配置列表格式无效")?
            .iter()
            .map(|record| Ok(field(record, id_key)?.to_owned()))
            .collect()
    };
    if operation == "list" {
        return Ok(json!(list()?));
    }
    ensure!(
        !id.is_empty() && id == id.trim() && id.len() <= 256 && !id.chars().any(char::is_control),
        "请填写配置编号，首尾不能有空格"
    );
    let by_id = base.join(&format!("api/v1/{route}/by-id"))?;
    let read = || -> Result<Value> {
        let mut url = by_id.clone();
        url.query_pairs_mut().append_pair(id_field, id);
        let mut data = exchange(&agent, &url, token, "GET", &Value::Null)?;
        if !data_field.is_empty() {
            data = data
                .as_object_mut()
                .and_then(|fields| fields.remove(data_field))
                .with_context(|| format!("AstrBot 响应缺少 {data_field}"))?;
        }
        ensure!(field(&data, id_key)? == id, "AstrBot 返回了不同的配置编号");
        Ok(data)
    };
    if operation == "read" {
        let data = read()?;
        return Ok(match resource {
            "source" => json!({"api_base": field(&data, "api_base")?}),
            "provider" => json!({"model": field(&data, "model")?}),
            _ => {
                let error = if data["custom_error_message"].is_null() {
                    ""
                } else {
                    field(&data, "custom_error_message")?
                };
                json!({"system_prompt": field(&data, "system_prompt")?, "custom_error_message": error})
            }
        });
    }

    let create = operation == "create";
    ensure!(create || operation == "save", "AstrBot 配置操作无效");
    if create {
        ensure!(
            !list()?.iter().any(|existing| existing == id),
            "配置编号已存在，请读取后编辑"
        );
    }
    let body = match resource {
        "source" => {
            let api_base = field(fields, "api_base")?;
            let api_key = field(fields, "api_key")?;
            let url = Url::parse(api_base).map_err(|_| anyhow::anyhow!("模型 API 地址无效"))?;
            ensure!(
                matches!(url.scheme(), "http" | "https")
                    && url.host().is_some()
                    && url.username().is_empty()
                    && url.password().is_none(),
                "模型 API 地址须为 HTTP(S) 地址"
            );
            let mut config = if create {
                json!({"id": id, "provider": "openai", "type": "openai_chat_completion",
                    "provider_type": "chat_completion", "enable": true, "key": [],
                    "timeout": 120, "proxy": "", "custom_headers": {}})
            } else {
                read()?
            };
            let old_key = &config["key"];
            let multiple_keys = old_key.is_array();
            let empty_key =
                old_key.as_array().is_some_and(Vec::is_empty) || old_key.as_str() == Some("");
            ensure!(
                old_key.is_array() || old_key.is_string(),
                "此模型源没有受支持的 API Key 字段"
            );
            ensure!(
                create || config["api_base"] == api_base || empty_key || !api_key.is_empty(),
                "修改模型 API 地址时，请同时填写新地址使用的 API Key"
            );
            config["api_base"] = json!(api_base);
            if !api_key.is_empty() {
                config["key"] = if multiple_keys {
                    json!([api_key])
                } else {
                    json!(api_key)
                };
            }
            json!({"source_id": id, "config": config})
        }
        "provider" => {
            let model = field(fields, "model")?;
            ensure!(!model.trim().is_empty(), "请填写模型名称");
            let mut config = if create {
                let source = field(fields, "source_id")?;
                ensure!(!source.is_empty(), "新建模型前请选择模型源");
                json!({"id": id, "provider_source_id": source, "enable": true})
            } else {
                read()?
            };
            config["model"] = json!(model);
            if create {
                json!({"config": config})
            } else {
                json!({"provider_id": id, "config": config})
            }
        }
        _ => {
            let prompt = field(fields, "system_prompt")?;
            ensure!(!prompt.trim().is_empty(), "请填写系统提示词");
            json!({"persona_id": id, "system_prompt": prompt,
                "custom_error_message": field(fields, "custom_error_message")?})
        }
    };
    if create && resource != "source" {
        exchange(&agent, &collection, token, "POST", &body)?;
    } else {
        exchange(&agent, &by_id, token, "PUT", &body)?;
    }
    Ok(Value::Null)
}

fn field<'a>(data: &'a Value, name: &str) -> Result<&'a str> {
    data[name]
        .as_str()
        .with_context(|| format!("AstrBot 配置字段 {name} 应为文本"))
}

fn exchange(
    agent: &ureq::Agent,
    url: &Url,
    token: &str,
    method: &str,
    body: &Value,
) -> Result<Value> {
    let authorization = format!("Bearer {token}");
    let mut response = match method {
        "GET" => agent
            .get(url.as_str())
            .header("Authorization", &authorization)
            .call(),
        "POST" => agent
            .post(url.as_str())
            .header("Authorization", &authorization)
            .send_json(body),
        _ => agent
            .put(url.as_str())
            .header("Authorization", &authorization)
            .send_json(body),
    }
    .map_err(|_| anyhow::anyhow!("无法连接 AstrBot，请检查地址和 HTTPS 证书"))?;
    let status = response.status();
    ensure!(
        status.is_success(),
        "AstrBot 请求失败（HTTP {}）",
        status.as_u16()
    );
    let mut data: Value = response
        .body_mut()
        .with_config()
        .limit(2 * 1024 * 1024)
        .read_json()
        .map_err(|_| anyhow::anyhow!("AstrBot 返回了无效的配置响应"))?;
    ensure!(data["status"] == "ok", "AstrBot 拒绝了配置操作");
    Ok(data["data"].take())
}
