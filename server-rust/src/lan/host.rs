use std::{
    collections::BTreeSet,
    env, fs,
    fs::OpenOptions,
    io::{self, Write},
    net::{IpAddr, Ipv4Addr},
    path::Path,
    time::Duration,
};

use anyhow::{Context, Result, ensure};
use base64::{Engine, engine::general_purpose::STANDARD};
use if_addrs::{IfAddr, Ifv4Addr, get_if_addrs};
use rcgen::{
    CertificateParams, DistinguishedName, DnType, ExtendedKeyUsagePurpose, KeyPair, KeyUsagePurpose,
};
use ring::digest::{SHA256, digest};
use serde::Serialize;
use time::{Duration as CertDuration, OffsetDateTime};
use tokio::net::UdpSocket;

use super::attachments::hex;

const DISCOVERY_PORT: u16 = 38888;

fn local_ipv4() -> io::Result<Vec<Ifv4Addr>> {
    Ok(get_if_addrs()?
        .into_iter()
        .filter(|interface| interface.is_oper_up())
        .filter_map(|interface| match interface.addr {
            IfAddr::V4(address)
                if !address.ip.is_loopback()
                    && !address.ip.is_unspecified()
                    && !(address.ip.octets()[0] == 198
                        && matches!(address.ip.octets()[1], 18 | 19)) =>
            {
                Some(address)
            }
            _ => None,
        })
        .collect())
}

pub(super) fn ensure_certificate(cert: &Path, key: &Path) -> Result<()> {
    let has_cert = cert.try_exists().context("检查证书文件")?;
    let has_key = key.try_exists().context("检查私钥文件")?;
    ensure!(has_cert == has_key, "证书和私钥必须同时存在或同时缺失");
    if has_cert {
        return Ok(());
    }

    let mut addresses = BTreeSet::from([Ipv4Addr::LOCALHOST]);
    addresses.extend(local_ipv4()?.into_iter().map(|address| address.ip));
    let mut names = vec!["localhost".to_owned()];
    names.extend(addresses.iter().map(ToString::to_string));
    let mut params = CertificateParams::new(names)?;
    let now = OffsetDateTime::now_utc();
    params.not_before = now - CertDuration::minutes(1);
    params.not_after = now + CertDuration::days(825);
    params.distinguished_name = DistinguishedName::new();
    params
        .distinguished_name
        .push(DnType::CommonName, "LAN Chat Local Host");
    params.key_usages = vec![KeyUsagePurpose::DigitalSignature];
    params.extended_key_usages = vec![ExtendedKeyUsagePurpose::ServerAuth];
    let key_pair = KeyPair::generate()?;
    let certificate = params.self_signed(&key_pair)?;

    create_pem(key, &key_pair.serialize_pem()).context("写入房主 TLS 私钥")?;
    if let Err(error) = create_pem(cert, &certificate.pem()) {
        fs::remove_file(key).context("清理证书写入失败时创建的私钥")?;
        return Err(error).context("写入房主 TLS 证书");
    }
    Ok(())
}

fn create_pem(path: &Path, contents: &str) -> io::Result<()> {
    if let Some(parent) = path
        .parent()
        .filter(|parent| !parent.as_os_str().is_empty())
    {
        fs::create_dir_all(parent)?;
    }
    let mut options = OpenOptions::new();
    options.write(true).create_new(true);
    #[cfg(unix)]
    {
        use std::os::unix::fs::OpenOptionsExt;
        options.mode(0o600);
    }
    let mut file = options.open(path)?;
    if let Err(error) = file
        .write_all(contents.as_bytes())
        .and_then(|()| file.sync_all())
    {
        drop(file);
        fs::remove_file(path)?;
        return Err(error);
    }
    Ok(())
}

#[derive(Serialize)]
#[serde(rename_all = "camelCase")]
struct Announcement<'a> {
    service: &'static str,
    version: u8,
    host_name: &'a str,
    port: u16,
    certificate_base64: String,
    fingerprint_sha256: String,
}

pub(super) async fn discover(cert: &Path, port: u16, name: &str) -> Result<()> {
    let pem = fs::read(cert).context("读取发现广播证书")?;
    ensure!(
        (1..=16 * 1024).contains(&pem.len()),
        "发现广播证书须在 1–16384 字节内"
    );
    let hostname = env::var("COMPUTERNAME")
        .or_else(|_| env::var("HOSTNAME"))
        .unwrap_or_else(|_| "LAN Chat Host".into());
    let name = name.trim();
    let payload = serde_json::to_vec(&Announcement {
        service: "lan-chat",
        version: 1,
        host_name: if name.is_empty() { &hostname } else { name },
        port,
        certificate_base64: STANDARD.encode(&pem),
        fingerprint_sha256: hex(digest(&SHA256, &pem).as_ref()),
    })?;
    let socket = UdpSocket::bind((Ipv4Addr::UNSPECIFIED, 0)).await?;
    socket.set_broadcast(true)?;
    let mut tick = tokio::time::interval(Duration::from_secs(1));
    loop {
        tick.tick().await;
        let addresses: BTreeSet<_> = local_ipv4()?
            .into_iter()
            .filter(|address| !address.ip.is_link_local() && address.prefixlen < 32)
            .filter_map(|address| address.broadcast)
            .collect();
        for address in addresses {
            if let Err(error) = socket
                .send_to(&payload, (IpAddr::V4(address), DISCOVERY_PORT))
                .await
            {
                eprintln!("发现广播发送至 {address} 失败: {error}");
            }
        }
    }
}
