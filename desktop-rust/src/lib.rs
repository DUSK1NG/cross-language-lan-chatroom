use std::{
    fs::File,
    io::{self, BufReader},
    path::Path,
    time::Duration,
};

use anyhow::{Context, Result, ensure};
use tokio::{net::TcpStream, time::timeout};
use tokio_native_tls::{TlsConnector, TlsStream};

pub async fn connect(
    host: &str,
    port: u16,
    certificate_name: &str,
    certificate_file: Option<&Path>,
) -> Result<TlsStream<TcpStream>> {
    let mut settings = native_tls::TlsConnector::builder();
    settings.min_protocol_version(Some(native_tls::Protocol::Tlsv12));
    if let Some(path) = certificate_file {
        let mut file = BufReader::new(File::open(path).context("读取公开证书")?);
        let certificates = rustls_pemfile::certs(&mut file).collect::<io::Result<Vec<_>>>()?;
        ensure!(
            !certificates.is_empty(),
            "公开证书应为 PEM 格式的 X.509 证书"
        );
        settings.disable_built_in_roots(true);
        for certificate in certificates {
            settings.add_root_certificate(native_tls::Certificate::from_der(&certificate)?);
        }
    }
    let connector = TlsConnector::from(settings.build().context("配置 TLS 客户端")?);
    timeout(Duration::from_secs(8), async {
        let socket = TcpStream::connect((host, port)).await?;
        socket.set_nodelay(true)?;
        connector
            .connect(certificate_name, socket)
            .await
            .context("验证 TLS 服务器证书")
    })
    .await
    .context("TLS 连接超时")?
}
