use std::{
    collections::HashMap,
    net::{IpAddr, Ipv6Addr},
    sync::{Arc, Mutex},
    time::{Duration, Instant},
};

use anyhow::{Result, bail, ensure};
use lan_chat_protocol::{ACCOUNT_PASSWORD_MAX_BYTES, ACCOUNT_PASSWORD_MIN_BYTES, Message};
use tokio::{
    sync::{OwnedSemaphorePermit, Semaphore},
    time::timeout,
};

use crate::store::{Rejected, user_code};

pub(crate) const INVALID_CREDENTIALS_MSG: &str = "用户名或密码不正确";

const RATE_LIMITED: Rejected = Rejected("登录尝试过于频繁，请稍后再试");
const REGISTER_RATE_LIMITED: Rejected = Rejected("注册尝试过于频繁，请稍后再试");
const AUTH_BUSY: Rejected = Rejected("认证服务繁忙，请稍后再试");
const SERVER_FULL: Rejected = Rejected("服务器当前在线人数已满，请稍后再试");
const LOGIN_REQUIRED: Rejected = Rejected("请使用在线版客户端登录");

const MAX_SOURCE_LOGIN_ENTRIES: usize = 4096;
const MAX_SOURCE_REGISTER_ENTRIES: usize = 2048;
const MAX_ACCOUNT_FAILURE_ENTRIES: usize = 4096;
const MAX_ACCOUNT_IN_FLIGHT: u8 = 2;
/// 冷却期内失败响应前的最长惩罚延迟：既拖慢爆破，又避免长时间占住连接槽。
const MAX_LOGIN_PENALTY: Duration = Duration::from_secs(2);
const ACCOUNT_FAILURE_IDLE_TTL: Duration = Duration::from_secs(3600);

#[derive(Clone, Copy, Debug)]
pub struct AuthConfig {
    pub max_preauth: usize,
    pub max_preauth_per_source: usize,
    pub tls_handshake_timeout: Duration,
    pub login_frame_timeout: Duration,
    pub max_auth_concurrency: usize,
    pub auth_queue_wait: Duration,
    pub ipv6_prefix: u8,
    pub login_source_limit: u32,
    pub login_source_window: Duration,
    pub register_source_limit: u32,
    pub register_source_window: Duration,
    pub account_fail_threshold: u32,
    pub account_cooldown_base: Duration,
    pub account_cooldown_max: Duration,
    pub preauth_fail_limit: u32,
    pub preauth_fail_window: Duration,
    pub preauth_ban: Duration,
}

impl Default for AuthConfig {
    fn default() -> Self {
        Self {
            max_preauth: 128,
            max_preauth_per_source: 8,
            tls_handshake_timeout: Duration::from_secs(5),
            login_frame_timeout: Duration::from_secs(8),
            max_auth_concurrency: 4,
            auth_queue_wait: Duration::from_secs(3),
            ipv6_prefix: 64,
            login_source_limit: 20,
            login_source_window: Duration::from_secs(60),
            register_source_limit: 5,
            register_source_window: Duration::from_secs(300),
            account_fail_threshold: 5,
            account_cooldown_base: Duration::from_secs(30),
            account_cooldown_max: Duration::from_secs(900),
            preauth_fail_limit: 8,
            preauth_fail_window: Duration::from_secs(60),
            preauth_ban: Duration::from_secs(120),
        }
    }
}

pub(crate) struct ValidatedCredentials {
    pub(crate) username: String,
    pub(crate) normalized_username: String,
    pub(crate) user_code: Option<(String, String)>,
}

/// 统一校验并规范化登录与注册凭据，确保限流器与存储层使用完全一致的键规则。
pub(crate) fn validate_credentials(request: &Message) -> Result<ValidatedCredentials> {
    ensure!(
        matches!(request.kind.as_str(), "account_login" | "account_register"),
        LOGIN_REQUIRED
    );
    let username = request.username.trim();
    ensure!(
        (1..=32).contains(&username.len()),
        Rejected("用户名须为 1–32 个 UTF-8 字节")
    );
    ensure!(
        (ACCOUNT_PASSWORD_MIN_BYTES..=ACCOUNT_PASSWORD_MAX_BYTES)
            .contains(&request.account_password.len())
            && !request.account_password.trim().is_empty(),
        Rejected("密码须为 8–72 个 UTF-8 字节")
    );
    let normalized_username = username
        .chars()
        .map(|character| {
            character
                .to_lowercase()
                .next()
                .expect("lowercase character")
        })
        .collect::<String>();
    let user_code = if request.kind == "account_register" {
        Some((request.user_code.clone(), user_code(&request.user_code)?))
    } else {
        None
    };
    Ok(ValidatedCredentials {
        username: username.into(),
        normalized_username,
        user_code,
    })
}

pub(crate) fn is_invalid_credentials_error(error: &anyhow::Error) -> bool {
    error
        .downcast_ref::<Rejected>()
        .is_some_and(|rejected| rejected.0 == INVALID_CREDENTIALS_MSG)
}

/// 将 IPv4-mapped IPv6 归一为 IPv4，并按指定前缀聚合 IPv6 地址（默认 /64）。
pub fn normalize_source(ip: IpAddr, ipv6_prefix: u8) -> IpAddr {
    match ip {
        IpAddr::V4(v4) => IpAddr::V4(v4),
        IpAddr::V6(v6) => {
            if let Some(v4) = v6.to_ipv4_mapped() {
                return IpAddr::V4(v4);
            }
            let bits = u128::from(v6);
            let masked = if ipv6_prefix == 0 {
                0
            } else if ipv6_prefix >= 128 {
                bits
            } else {
                bits & (!0u128 << (128 - u32::from(ipv6_prefix)))
            };
            IpAddr::V6(Ipv6Addr::from(masked))
        }
    }
}

pub struct PreAuthPermit {
    _global: OwnedSemaphorePermit,
    per_source: Arc<Mutex<HashMap<IpAddr, usize>>>,
    source: IpAddr,
}

impl Drop for PreAuthPermit {
    fn drop(&mut self) {
        let mut map = self
            .per_source
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        let mut remove = false;
        if let Some(count) = map.get_mut(&self.source) {
            if *count <= 1 {
                remove = true;
            } else {
                *count -= 1;
            }
        }
        if remove {
            map.remove(&self.source);
        }
    }
}

struct SourceBucket {
    count: u32,
    window_end: Instant,
}

struct SourceWindowTable {
    entries: HashMap<IpAddr, SourceBucket>,
    max_entries: usize,
    limit: u32,
    window: Duration,
    rejected_limit: usize,
    rejected_capacity: usize,
    rejected_error: Rejected,
}

impl SourceWindowTable {
    fn new(max_entries: usize, limit: u32, window: Duration, rejected_error: Rejected) -> Self {
        Self {
            entries: HashMap::new(),
            max_entries,
            limit,
            window,
            rejected_limit: 0,
            rejected_capacity: 0,
            rejected_error,
        }
    }

    fn check_and_consume(&mut self, source: IpAddr, now: Instant) -> Result<()> {
        if let Some(bucket) = self.entries.get_mut(&source) {
            if now >= bucket.window_end {
                bucket.count = 0;
                bucket.window_end = now + self.window;
            }
            if bucket.count >= self.limit {
                self.rejected_limit += 1;
                bail!(self.rejected_error);
            }
            bucket.count += 1;
            return Ok(());
        }

        if self.entries.len() >= self.max_entries {
            self.entries.retain(|_, bucket| now < bucket.window_end);
        }
        if self.entries.len() >= self.max_entries {
            // Do not evict blocked/high-frequency entries; only allow a full table to recycle
            // single-probe entries or fail closed so key-flooding cannot bypass active limits.
            if let Some(evict_key) = self
                .entries
                .iter()
                .filter(|(_, bucket)| bucket.count <= 1)
                .min_by_key(|(_, bucket)| bucket.window_end)
                .map(|(key, _)| *key)
            {
                self.entries.remove(&evict_key);
            } else {
                self.rejected_capacity += 1;
                bail!(self.rejected_error);
            }
        }
        self.entries.insert(
            source,
            SourceBucket {
                count: 1,
                window_end: now + self.window,
            },
        );
        Ok(())
    }

    /// 成功登录退还一次来源计数，避免共享出口（NAT）被自己的成功连接挤掉。
    fn refund(&mut self, source: IpAddr, now: Instant) {
        if let Some(bucket) = self.entries.get_mut(&source)
            && now < bucket.window_end
        {
            bucket.count = bucket.count.saturating_sub(1);
        }
    }
}

struct AccountFailureEntry {
    failures: u32,
    next_allowed_at: Option<Instant>,
    expires_at: Instant,
    in_flight: u8,
    epoch: u64,
}

#[derive(Clone, Copy, PartialEq, Eq)]
pub enum AccountAuthOutcome {
    Success,
    InvalidCredentials,
    ServiceError,
}

pub struct RateLimiter {
    source_login: SourceWindowTable,
    source_register: SourceWindowTable,
    account_failures: HashMap<String, AccountFailureEntry>,
    max_account_entries: usize,
    account_fail_threshold: u32,
    account_cooldown_base: Duration,
    account_cooldown_max: Duration,
    account_idle_ttl: Duration,
    rejected_account_cooldown: usize,
    rejected_account_in_flight: usize,
    rejected_account_capacity: usize,
}

#[derive(Default, Debug, Clone, Copy, PartialEq, Eq)]
pub struct RateLimitMetrics {
    pub source_login_entries: usize,
    pub source_register_entries: usize,
    pub account_failure_entries: usize,
    pub rejected_source_login_limit: usize,
    pub rejected_source_login_capacity: usize,
    pub rejected_source_register_limit: usize,
    pub rejected_source_register_capacity: usize,
    pub rejected_account_cooldown: usize,
    pub rejected_account_in_flight: usize,
    pub rejected_account_capacity: usize,
}

impl RateLimiter {
    pub fn new(config: &AuthConfig) -> Self {
        Self {
            source_login: SourceWindowTable::new(
                MAX_SOURCE_LOGIN_ENTRIES,
                config.login_source_limit,
                config.login_source_window,
                RATE_LIMITED,
            ),
            source_register: SourceWindowTable::new(
                MAX_SOURCE_REGISTER_ENTRIES,
                config.register_source_limit,
                config.register_source_window,
                REGISTER_RATE_LIMITED,
            ),
            account_failures: HashMap::new(),
            max_account_entries: MAX_ACCOUNT_FAILURE_ENTRIES,
            account_fail_threshold: config.account_fail_threshold,
            account_cooldown_base: config.account_cooldown_base,
            account_cooldown_max: config.account_cooldown_max,
            account_idle_ttl: ACCOUNT_FAILURE_IDLE_TTL,
            rejected_account_cooldown: 0,
            rejected_account_in_flight: 0,
            rejected_account_capacity: 0,
        }
    }

    #[cfg(test)]
    fn with_table_caps(mut self, source_cap: usize, account_cap: usize) -> Self {
        self.source_login.max_entries = source_cap;
        self.source_register.max_entries = source_cap;
        self.max_account_entries = account_cap;
        self
    }

    pub fn metrics(&self) -> RateLimitMetrics {
        RateLimitMetrics {
            source_login_entries: self.source_login.entries.len(),
            source_register_entries: self.source_register.entries.len(),
            account_failure_entries: self.account_failures.len(),
            rejected_source_login_limit: self.source_login.rejected_limit,
            rejected_source_login_capacity: self.source_login.rejected_capacity,
            rejected_source_register_limit: self.source_register.rejected_limit,
            rejected_source_register_capacity: self.source_register.rejected_capacity,
            rejected_account_cooldown: self.rejected_account_cooldown,
            rejected_account_in_flight: self.rejected_account_in_flight,
            rejected_account_capacity: self.rejected_account_capacity,
        }
    }

    fn consume_source(&mut self, is_register: bool, source: IpAddr, now: Instant) -> Result<()> {
        if is_register {
            self.source_register.check_and_consume(source, now)
        } else {
            self.source_login.check_and_consume(source, now)
        }
    }

    fn begin_account_login(&mut self, account: &str, now: Instant) -> Result<u64> {
        if let Some(entry) = self.account_failures.get_mut(account) {
            if now >= entry.expires_at
                && entry.in_flight == 0
                && entry.next_allowed_at.is_none_or(|until| now >= until)
            {
                entry.failures = 0;
                entry.next_allowed_at = None;
            }
            if entry.next_allowed_at.is_some_and(|until| now < until) {
                // 冷却期不再直接拒绝：先验证密码，正确则立即放行，
                // 错误则按 finish 的惩罚延迟处理，避免他人恶意锁死账号。
                self.rejected_account_cooldown += 1;
            }
            if entry.in_flight >= MAX_ACCOUNT_IN_FLIGHT {
                self.rejected_account_in_flight += 1;
                bail!(AUTH_BUSY);
            }
            entry.in_flight += 1;
            entry.expires_at = entry.expires_at.max(now + self.account_idle_ttl);
            return Ok(entry.epoch);
        }

        if self.account_failures.len() >= self.max_account_entries {
            self.account_failures.retain(|_, entry| {
                entry.in_flight > 0
                    || now < entry.expires_at
                    || entry.next_allowed_at.is_some_and(|until| now < until)
            });
        }
        if self.account_failures.len() >= self.max_account_entries {
            // Preserve active cooldown/threshold entries so username flooding cannot flush locks.
            if let Some(evict_key) = self
                .account_failures
                .iter()
                .filter(|(_, entry)| {
                    entry.in_flight == 0
                        && entry.failures < self.account_fail_threshold
                        && entry.next_allowed_at.is_none_or(|until| now >= until)
                })
                .min_by_key(|(_, entry)| (entry.failures, entry.expires_at))
                .map(|(key, _)| key.clone())
            {
                self.account_failures.remove(&evict_key);
            } else {
                self.rejected_account_capacity += 1;
                bail!(AUTH_BUSY);
            }
        }
        self.account_failures.insert(
            account.to_owned(),
            AccountFailureEntry {
                failures: 0,
                next_allowed_at: None,
                expires_at: now + self.account_idle_ttl,
                in_flight: 1,
                epoch: 0,
            },
        );
        Ok(0)
    }

    fn finish_account_login(
        &mut self,
        account: &str,
        started_epoch: u64,
        outcome: AccountAuthOutcome,
        now: Instant,
    ) -> Duration {
        let mut penalty = Duration::ZERO;
        let Some(entry) = self.account_failures.get_mut(account) else {
            return penalty;
        };
        entry.in_flight = entry.in_flight.saturating_sub(1);
        match outcome {
            AccountAuthOutcome::Success => {
                entry.failures = 0;
                entry.next_allowed_at = None;
                entry.epoch = entry.epoch.wrapping_add(1);
            }
            AccountAuthOutcome::InvalidCredentials => {
                if started_epoch == entry.epoch {
                    entry.failures = entry.failures.saturating_add(1);
                    entry.expires_at = now + self.account_idle_ttl;
                    if entry.failures >= self.account_fail_threshold {
                        let shift = (entry.failures - self.account_fail_threshold).min(16);
                        let factor = 1u64.checked_shl(shift).unwrap_or(u64::MAX);
                        let duration = self
                            .account_cooldown_base
                            .saturating_mul(factor.min(u32::MAX as u64) as u32)
                            .min(self.account_cooldown_max);
                        let until = now + duration;
                        entry.next_allowed_at = Some(until);
                        entry.expires_at = entry.expires_at.max(until + self.account_idle_ttl);
                        penalty = duration.min(MAX_LOGIN_PENALTY);
                    }
                }
            }
            AccountAuthOutcome::ServiceError => {}
        }
        let remove = entry.in_flight == 0 && entry.failures == 0 && entry.next_allowed_at.is_none();
        if remove {
            self.account_failures.remove(account);
        }
        penalty
    }

    fn refund_source(&mut self, is_register: bool, source: IpAddr, now: Instant) {
        if is_register {
            self.source_register.refund(source, now);
        } else {
            self.source_login.refund(source, now);
        }
    }
}

pub struct AuthReservation<'a> {
    control: &'a AuthControl,
    account: Option<(String, u64)>,
    source: Option<(bool, IpAddr)>,
    finished: bool,
}

impl AuthReservation<'_> {
    pub fn record_success(mut self) {
        self.finish(AccountAuthOutcome::Success);
    }

    /// 失败时返回建议的响应延迟（冷却期内递增，最长 2 秒），由调用方异步等待。
    pub fn record_invalid_credentials(mut self) -> Duration {
        self.finish(AccountAuthOutcome::InvalidCredentials)
    }

    pub fn record_service_error(mut self) {
        self.finish(AccountAuthOutcome::ServiceError);
    }

    fn finish(&mut self, outcome: AccountAuthOutcome) -> Duration {
        if self.finished {
            return Duration::ZERO;
        }
        self.finished = true;
        let mut limiter = self
            .control
            .limiter
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        if outcome == AccountAuthOutcome::Success
            && let Some((is_register, source)) = self.source.take()
        {
            limiter.refund_source(is_register, source, Instant::now());
        }
        if let Some((account, started_epoch)) = self.account.take() {
            limiter.finish_account_login(&account, started_epoch, outcome, Instant::now())
        } else {
            Duration::ZERO
        }
    }
}

impl Drop for AuthReservation<'_> {
    fn drop(&mut self) {
        self.finish(AccountAuthOutcome::ServiceError);
    }
}

struct PreauthFailure {
    count: u32,
    window_start: Instant,
    banned_until: Option<Instant>,
}

pub struct AuthControl {
    config: AuthConfig,
    preauth_slots: Arc<Semaphore>,
    per_source_preauth: Arc<Mutex<HashMap<IpAddr, usize>>>,
    preauth_failures: Mutex<HashMap<IpAddr, PreauthFailure>>,
    bcrypt_slots: Arc<Semaphore>,
    limiter: Mutex<RateLimiter>,
}

impl AuthControl {
    pub fn new(config: AuthConfig) -> Self {
        Self {
            preauth_slots: Arc::new(Semaphore::new(config.max_preauth)),
            per_source_preauth: Arc::new(Mutex::new(HashMap::new())),
            preauth_failures: Mutex::new(HashMap::new()),
            bcrypt_slots: Arc::new(Semaphore::new(config.max_auth_concurrency)),
            limiter: Mutex::new(RateLimiter::new(&config)),
            config,
        }
    }

    pub fn config(&self) -> AuthConfig {
        self.config
    }

    pub fn metrics(&self) -> RateLimitMetrics {
        self.limiter
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner)
            .metrics()
    }

    /// 预认证并发许可：先查封禁，再按全局与单来源并发上限发放。
    pub fn try_acquire_preauth(&self, peer_ip: IpAddr) -> Option<PreAuthPermit> {
        let source = normalize_source(peer_ip, self.config.ipv6_prefix);
        if self.preauth_banned(source) {
            return None;
        }
        let global = self.preauth_slots.clone().try_acquire_owned().ok()?;
        let admitted = {
            let mut counts = self
                .per_source_preauth
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            let entry = counts.entry(source).or_insert(0);
            if *entry >= self.config.max_preauth_per_source {
                false
            } else {
                *entry += 1;
                true
            }
        };
        if !admitted {
            drop(global);
            return None;
        }
        Some(PreAuthPermit {
            _global: global,
            per_source: Arc::clone(&self.per_source_preauth),
            source,
        })
    }

    /// 记录一次握手中途失败/超时；同一来源在窗口内反复失败会被短暂封禁。
    pub fn record_preauth_failure(&self, peer_ip: IpAddr) {
        let source = normalize_source(peer_ip, self.config.ipv6_prefix);
        let now = Instant::now();
        let window = self.config.preauth_fail_window;
        let mut map = self
            .preauth_failures
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        if map.len() >= 4096 {
            map.retain(|_, failure| {
                failure.banned_until.is_some_and(|until| now < until)
                    || now.duration_since(failure.window_start) < window
            });
            if map.len() >= 4096 {
                return;
            }
        }
        let failure = map.entry(source).or_insert(PreauthFailure {
            count: 0,
            window_start: now,
            banned_until: None,
        });
        if now.duration_since(failure.window_start) >= window {
            failure.count = 0;
            failure.window_start = now;
            failure.banned_until = None;
        }
        failure.count = failure.count.saturating_add(1);
        if failure.count >= self.config.preauth_fail_limit {
            failure.banned_until = Some(now + self.config.preauth_ban);
        }
    }

    fn preauth_banned(&self, source: IpAddr) -> bool {
        let now = Instant::now();
        let mut map = self
            .preauth_failures
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        let Some(failure) = map.get_mut(&source) else {
            return false;
        };
        match failure.banned_until {
            Some(until) if now < until => true,
            Some(_) => {
                failure.banned_until = None;
                failure.count = 0;
                failure.window_start = now;
                false
            }
            None => false,
        }
    }

    /// 校验请求类型、来源限流、满员快速拒绝、格式规范化与账号冷却。
    pub fn reserve_attempt(
        &self,
        peer_ip: IpAddr,
        request: &Message,
        session_full: bool,
    ) -> Result<AuthReservation<'_>> {
        ensure!(
            matches!(request.kind.as_str(), "account_login" | "account_register"),
            LOGIN_REQUIRED
        );
        let is_register = request.kind == "account_register";
        let source = normalize_source(peer_ip, self.config.ipv6_prefix);
        let now = Instant::now();
        {
            let mut limiter = self
                .limiter
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            limiter.consume_source(is_register, source, now)?;
        }
        if session_full {
            bail!(SERVER_FULL);
        }
        // Validate credentials before touching the per-account table or consuming bcrypt slots.
        let validated = validate_credentials(request)?;
        let account = if is_register {
            None
        } else {
            let mut limiter = self
                .limiter
                .lock()
                .unwrap_or_else(std::sync::PoisonError::into_inner);
            let epoch = limiter.begin_account_login(&validated.normalized_username, now)?;
            Some((validated.normalized_username, epoch))
        };
        Ok(AuthReservation {
            control: self,
            account,
            source: Some((is_register, source)),
            finished: false,
        })
    }

    pub async fn acquire_bcrypt(&self) -> Result<OwnedSemaphorePermit> {
        match timeout(
            self.config.auth_queue_wait,
            self.bcrypt_slots.clone().acquire_owned(),
        )
        .await
        {
            Ok(Ok(permit)) => Ok(permit),
            _ => bail!(AUTH_BUSY),
        }
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    fn login_msg(user: &str, password: &str) -> Message {
        Message {
            kind: "account_login".into(),
            username: user.into(),
            account_password: password.into(),
            ..Message::default()
        }
    }

    fn register_msg(user: &str, code: &str, password: &str) -> Message {
        Message {
            kind: "account_register".into(),
            username: user.into(),
            user_code: code.into(),
            account_password: password.into(),
            ..Message::default()
        }
    }

    #[test]
    fn source_normalization_aggregates_ipv6_64_and_maps_ipv4() {
        let v4: IpAddr = "203.0.113.5".parse().unwrap();
        let mapped: IpAddr = "::ffff:203.0.113.5".parse().unwrap();
        assert_eq!(normalize_source(mapped, 64), v4);

        let a: IpAddr = "2001:db8:abcd:0001::10".parse().unwrap();
        let b: IpAddr = "2001:db8:abcd:0001:ffff::99".parse().unwrap();
        let other: IpAddr = "2001:db8:abcd:0002::1".parse().unwrap();
        assert_eq!(normalize_source(a, 64), normalize_source(b, 64));
        assert_ne!(normalize_source(a, 64), normalize_source(other, 64));
    }

    #[test]
    fn preauth_guards_release_global_and_per_source_slots() {
        let auth = AuthControl::new(AuthConfig {
            max_preauth: 3,
            max_preauth_per_source: 2,
            ..AuthConfig::default()
        });
        let ip1: IpAddr = "2001:db8:1:2::1".parse().unwrap();
        let ip1_same64: IpAddr = "2001:db8:1:2::99".parse().unwrap();
        let ip2: IpAddr = "198.51.100.1".parse().unwrap();

        let p1 = auth.try_acquire_preauth(ip1).unwrap();
        let p2 = auth.try_acquire_preauth(ip1_same64).unwrap();
        assert!(auth.try_acquire_preauth(ip1_same64).is_none());

        let p3 = auth.try_acquire_preauth(ip2).unwrap();
        assert!(
            auth.try_acquire_preauth("198.51.100.2".parse().unwrap())
                .is_none()
        );

        drop(p1);
        let p1_new = auth.try_acquire_preauth(ip1_same64).unwrap();
        drop((p1_new, p2, p3));
        assert!(auth.per_source_preauth.lock().unwrap().is_empty());
    }

    #[test]
    fn rate_limiter_isolates_login_register_and_formats_and_applies_nonblocking_cooldown() {
        let mut limiter = RateLimiter::new(&AuthConfig {
            login_source_limit: 5,
            register_source_limit: 2,
            account_fail_threshold: 3,
            account_cooldown_base: Duration::from_secs(30),
            account_cooldown_max: Duration::from_secs(120),
            ..AuthConfig::default()
        });
        let ip: IpAddr = "198.51.100.10".parse().unwrap();
        let now = Instant::now();

        // Register uses a separate bucket from login.
        for _ in 0..2 {
            assert!(limiter.consume_source(true, ip, now).is_ok());
        }
        let err = limiter.consume_source(true, ip, now).unwrap_err();
        assert_ne!(err.to_string(), INVALID_CREDENTIALS_MSG);
        assert!(limiter.consume_source(false, ip, now).is_ok());

        // Account failures open a cooldown but never reject up front: the password is
        // still verified, so a third party cannot lock the real owner out.
        let mut last_penalty = Duration::ZERO;
        for _ in 0..3 {
            let epoch = limiter.begin_account_login("alice", now).unwrap();
            last_penalty = limiter.finish_account_login(
                "alice",
                epoch,
                AccountAuthOutcome::InvalidCredentials,
                now,
            );
        }
        assert!(last_penalty > Duration::ZERO && last_penalty <= MAX_LOGIN_PENALTY);
        assert_eq!(limiter.metrics().rejected_account_cooldown, 0);

        // Inside the cooldown the attempt is admitted, counted, and the correct password wins.
        let epoch = limiter
            .begin_account_login("alice", now + Duration::from_secs(5))
            .unwrap();
        assert_eq!(limiter.metrics().rejected_account_cooldown, 1);
        limiter.finish_account_login(
            "alice",
            epoch,
            AccountAuthOutcome::Success,
            now + Duration::from_secs(5),
        );
        assert!(!limiter.account_failures.contains_key("alice"));

        // A wrong password during the cooldown still escalates the delay.
        for _ in 0..3 {
            let epoch = limiter
                .begin_account_login("alice", now + Duration::from_secs(10))
                .unwrap();
            limiter.finish_account_login(
                "alice",
                epoch,
                AccountAuthOutcome::InvalidCredentials,
                now + Duration::from_secs(10),
            );
        }
        let epoch = limiter
            .begin_account_login("alice", now + Duration::from_secs(12))
            .unwrap();
        let penalty = limiter.finish_account_login(
            "alice",
            epoch,
            AccountAuthOutcome::InvalidCredentials,
            now + Duration::from_secs(12),
        );
        assert!(penalty > Duration::ZERO && penalty <= MAX_LOGIN_PENALTY);
    }

    #[test]
    fn auth_control_isolates_format_errors_and_handles_concurrent_races_and_table_caps() {
        let auth = AuthControl::new(AuthConfig {
            login_source_limit: 100,
            account_fail_threshold: 2,
            ..AuthConfig::default()
        });
        let ip: IpAddr = "192.0.2.5".parse().unwrap();

        // Format error is rejected before touching account_failures or bcrypt.
        let invalid = login_msg("baduser", "short");
        assert!(auth.reserve_attempt(ip, &invalid, false).is_err());
        assert_eq!(auth.metrics().account_failure_entries, 0);

        // Concurrent failure + success race: stale failure must not re-penalize after success.
        let r1 = auth
            .reserve_attempt(ip, &login_msg("Bob", "password123"), false)
            .unwrap();
        let r2 = auth
            .reserve_attempt(ip, &login_msg("bob", "password123"), false)
            .unwrap();
        r2.record_success();
        let _ = r1.record_invalid_credentials();
        assert_eq!(auth.metrics().account_failure_entries, 0);

        // Dropped reservation treats task cancellation as ServiceError and clears in_flight cleanly.
        {
            let _dropped = auth
                .reserve_attempt(ip, &login_msg("bob", "password123"), false)
                .unwrap();
            assert_eq!(auth.metrics().account_failure_entries, 1);
        }
        assert_eq!(auth.metrics().account_failure_entries, 0);

        // Key flooding cannot evict penalized accounts from bounded account_failures table.
        let mut bounded = RateLimiter::new(&AuthConfig {
            account_fail_threshold: 2,
            account_cooldown_base: Duration::from_secs(60),
            ..AuthConfig::default()
        })
        .with_table_caps(2, 2);
        let t0 = Instant::now();
        for _ in 0..2 {
            let e = bounded.begin_account_login("victim", t0).unwrap();
            bounded.finish_account_login("victim", e, AccountAuthOutcome::InvalidCredentials, t0);
        }
        let e2 = bounded.begin_account_login("flood1", t0).unwrap();
        bounded.finish_account_login("flood1", e2, AccountAuthOutcome::InvalidCredentials, t0);
        // Victim's penalized entry survives flooding, yet an attempt is still admitted so a
        // correct password can clear the cooldown.
        let _ = bounded.begin_account_login("flood2", t0).unwrap();
        assert!(bounded.account_failures.contains_key("victim"));
        assert!(
            bounded
                .begin_account_login("victim", t0 + Duration::from_secs(10))
                .is_ok()
        );

        // Session-full quick reject does not consume account_failures.
        let auth_full = AuthControl::new(AuthConfig::default());
        let err = auth_full
            .reserve_attempt(ip, &login_msg("Carol", "password123"), true)
            .err()
            .expect("full reject");
        assert!(err.to_string().contains("已满"));
        assert_eq!(auth_full.metrics().account_failure_entries, 0);

        let reg = register_msg("NewUser", "NEW01", "password123");
        let reg_res = auth_full.reserve_attempt(ip, &reg, false).unwrap();
        drop(reg_res);
    }

    #[tokio::test]
    async fn bcrypt_queue_wait_times_out_with_busy_error() {
        let auth = AuthControl::new(AuthConfig {
            max_auth_concurrency: 1,
            auth_queue_wait: Duration::from_millis(40),
            ..AuthConfig::default()
        });
        let held = auth.acquire_bcrypt().await.unwrap();
        let err = auth.acquire_bcrypt().await.unwrap_err();
        assert_ne!(err.to_string(), INVALID_CREDENTIALS_MSG);
        assert!(err.to_string().contains("繁忙"));
        drop(held);
        assert!(auth.acquire_bcrypt().await.is_ok());
    }
}
