use std::collections::HashMap;

use anyhow::{Context, Result, ensure};
use mls_rs::{
    CipherSuite, CipherSuiteProvider, Client, CryptoProvider, Group, MlsMessage,
    client_builder::{BaseConfig, PaddingMode, WithCryptoProvider, WithIdentityProvider},
    group::ReceivedMessage,
    identity::{
        SigningIdentity,
        basic::{BasicCredential, BasicIdentityProvider},
    },
    mls_rules::{CommitOptions, DefaultMlsRules, EncryptionOptions},
};
use mls_rs_crypto_rustcrypto::RustCryptoProvider;

type Config =
    WithIdentityProvider<BasicIdentityProvider, WithCryptoProvider<RustCryptoProvider, BaseConfig>>;

pub struct Mls {
    client: Client<Config>,
    groups: HashMap<String, Group<Config>>,
}

pub struct AddMember {
    pub proposal: Vec<u8>,
    pub commit: Vec<u8>,
    pub welcome: Vec<u8>,
}

impl Mls {
    pub fn new(identity: &str) -> Result<Self> {
        let provider = RustCryptoProvider::default();
        let suite = CipherSuite::P256_AES128;
        let (secret, public) = provider
            .cipher_suite_provider(suite)
            .context("MLS P-256 密码套件不可用")?
            .signature_key_generate()?;
        let credential = BasicCredential::new(identity.as_bytes().to_vec());
        let client = Client::builder()
            .crypto_provider(provider)
            .identity_provider(BasicIdentityProvider)
            .mls_rules(
                DefaultMlsRules::new()
                    .with_encryption_options(EncryptionOptions::new(true, PaddingMode::None))
                    .with_commit_options(CommitOptions::new().with_path_required(true)),
            )
            .signing_identity(
                SigningIdentity::new(credential.into_credential(), public),
                secret,
                suite,
            )
            .build();
        Ok(Self {
            client,
            groups: HashMap::new(),
        })
    }

    pub fn key_package(&self) -> Result<Vec<u8>> {
        let package = self.client.generate_key_package_message(
            Default::default(),
            Default::default(),
            None,
        )?;
        bare(&package, 5)
    }

    pub fn create(&mut self, id: &str) -> Result<()> {
        ensure!(!self.groups.contains_key(id), "MLS 群组已存在");
        let group = self.client.create_group_with_id(
            id.as_bytes().to_vec(),
            Default::default(),
            Default::default(),
            None,
        )?;
        self.groups.insert(id.into(), group);
        Ok(())
    }

    pub fn add(&mut self, id: &str, package: &[u8]) -> Result<AddMember> {
        let group = self.groups.get_mut(id).context("MLS 群组不存在")?;
        let proposal = group.propose_add(wrap(package, 5)?, Vec::new())?;
        let commit = group.commit(Vec::new())?;
        let welcome = commit
            .welcome_messages
            .first()
            .context("MLS 成员增加缺少 Welcome")?;
        let result = AddMember {
            proposal: proposal.to_bytes()?,
            commit: commit.commit_message.to_bytes()?,
            welcome: bare(welcome, 3)?,
        };
        group.apply_pending_commit()?;
        Ok(result)
    }

    pub fn join(&mut self, id: &str, welcome: &[u8]) -> Result<()> {
        ensure!(!self.groups.contains_key(id), "MLS 群组已存在");
        let (group, _) = self.client.join_group(None, &wrap(welcome, 3)?, None)?;
        ensure!(
            group.group_id() == id.as_bytes(),
            "MLS Welcome 群组 ID 不匹配"
        );
        self.groups.insert(id.into(), group);
        Ok(())
    }

    pub fn handle_proposal(&mut self, id: &str, proposal: &[u8]) -> Result<()> {
        ensure!(
            matches!(self.receive(id, proposal)?, ReceivedMessage::Proposal(_)),
            "MLS 消息不是 Proposal"
        );
        Ok(())
    }

    pub fn handle_commit(&mut self, id: &str, commit: &[u8]) -> Result<()> {
        ensure!(
            matches!(self.receive(id, commit)?, ReceivedMessage::Commit(_)),
            "MLS 消息不是 Commit"
        );
        Ok(())
    }

    pub fn protect(&mut self, id: &str, plaintext: &[u8]) -> Result<Vec<u8>> {
        Ok(self
            .groups
            .get_mut(id)
            .context("MLS 群组不存在")?
            .encrypt_application_message(plaintext, Vec::new())?
            .to_bytes()?)
    }

    pub fn unprotect(&mut self, id: &str, ciphertext: &[u8]) -> Result<Vec<u8>> {
        let ReceivedMessage::ApplicationMessage(message) = self.receive(id, ciphertext)? else {
            anyhow::bail!("MLS 消息不是应用数据");
        };
        Ok(message.data().to_vec())
    }

    fn receive(&mut self, id: &str, message: &[u8]) -> Result<ReceivedMessage> {
        Ok(self
            .groups
            .get_mut(id)
            .context("MLS 群组不存在")?
            .process_incoming_message(MlsMessage::from_bytes(message)?)?)
    }
}

// The original attachment protocol carries raw KeyPackage and Welcome records.
fn bare(message: &MlsMessage, kind: u8) -> Result<Vec<u8>> {
    let data = message.to_bytes()?;
    ensure!(data.starts_with(&[0, 1, 0, kind]), "MLS 控制消息格式无效");
    Ok(data[4..].to_vec())
}

fn wrap(data: &[u8], kind: u8) -> Result<MlsMessage> {
    Ok(MlsMessage::from_bytes(&[&[0, 1, 0, kind], data].concat())?)
}
