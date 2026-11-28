use std::{
    collections::HashMap,
    net::{IpAddr, Ipv6Addr},
    sync::{Arc, Mutex},
};

use tokio::sync::{OwnedSemaphorePermit, Semaphore};

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

pub(crate) struct AdmissionLimits {
    pub(crate) global: usize,
    pub(crate) per_source: usize,
    pub(crate) ipv6_prefix: u8,
}

pub(crate) struct AdmissionPermit {
    _global: OwnedSemaphorePermit,
    source: IpAddr,
    counts: Arc<Mutex<HashMap<IpAddr, usize>>>,
}

impl Drop for AdmissionPermit {
    fn drop(&mut self) {
        let mut counts = self
            .counts
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        if let Some(count) = counts.get_mut(&self.source) {
            *count -= 1;
            if *count == 0 {
                counts.remove(&self.source);
            }
        }
    }
}

pub(crate) struct AdmissionControl {
    global: Arc<Semaphore>,
    per_source: usize,
    ipv6_prefix: u8,
    counts: Arc<Mutex<HashMap<IpAddr, usize>>>,
}

impl AdmissionControl {
    pub(crate) fn new(limits: AdmissionLimits) -> Self {
        Self {
            global: Arc::new(Semaphore::new(limits.global)),
            per_source: limits.per_source,
            ipv6_prefix: limits.ipv6_prefix,
            counts: Arc::new(Mutex::new(HashMap::new())),
        }
    }

    pub(crate) fn try_acquire(&self, ip: IpAddr) -> Option<AdmissionPermit> {
        let source = normalize_source(ip, self.ipv6_prefix);
        let global = self.global.clone().try_acquire_owned().ok()?;
        let mut counts = self
            .counts
            .lock()
            .unwrap_or_else(std::sync::PoisonError::into_inner);
        let count = counts.get(&source).copied().unwrap_or(0);
        if count >= self.per_source {
            return None;
        }
        counts.insert(source, count + 1);
        drop(counts);
        Some(AdmissionPermit {
            _global: global,
            source,
            counts: self.counts.clone(),
        })
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn permits_are_bounded_and_released() {
        let control = AdmissionControl::new(AdmissionLimits {
            global: 2,
            per_source: 1,
            ipv6_prefix: 64,
        });
        let ip = "198.51.100.10".parse().unwrap();
        let first = control.try_acquire(ip).unwrap();
        assert!(control.try_acquire(ip).is_none());
        let other = control
            .try_acquire("198.51.100.11".parse().unwrap())
            .unwrap();
        assert!(
            control
                .try_acquire("198.51.100.12".parse().unwrap())
                .is_none()
        );
        drop(first);
        assert!(control.try_acquire(ip).is_some());
        drop(other);
    }
}
