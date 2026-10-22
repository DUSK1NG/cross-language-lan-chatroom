import { useEffect, useMemo, useRef, useState } from 'react';
import { AnimatePresence, motion } from 'framer-motion';
import { ShieldCheck } from 'lucide-react';

import type { ConnectionApprovalRequest } from '../../bridge/types';
import { cn } from '../../lib/cn';
import { ease } from '../../lib/motion';
import { Avatar } from '../ui/avatar';
import { Button } from '../ui/button';
import { Dialog } from '../ui/dialog';
import { EmptyState } from '../ui/states';

/** Matches the Go server's connectionApprovalTimeout (server-go/client.go:12). */
export const approvalTimeoutMs = 60_000;
const fadeAfterExpiryMs = 1500;

type ApprovalDialogProps = {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  requests: ConnectionApprovalRequest[];
  onResolve: (request: ConnectionApprovalRequest, approve: boolean) => void;
  /** Injectable clock for tests. */
  now?: () => number;
};

/**
 * Deadline for one request. requestedAt is the server's CreatedAt (RFC 3339). The host
 * normally shares the server's clock, but a remote admin may not, so a timestamp that is
 * in the future or already older than the timeout counts from when this UI first saw it.
 */
function deadlineFor(request: ConnectionApprovalRequest, firstSeen: number): number {
  const created = Date.parse(request.requestedAt);
  const age = firstSeen - created;
  const trusted = Number.isFinite(created) && age >= 0 && age < approvalTimeoutMs;
  return (trusted ? created : firstSeen) + approvalTimeoutMs;
}

/** Owner approval of incoming members (spec §9): countdown ring, 允许 / 拒绝, verification hint. */
export function ApprovalDialog({ open, onOpenChange, requests, onResolve, now = Date.now }: ApprovalDialogProps) {
  const firstSeenRef = useRef(new Map<string, number>());
  const [clock, setClock] = useState(now);
  const deadlines = useMemo(() => {
    const map = new Map<string, number>();
    for (const request of requests) {
      if (!firstSeenRef.current.has(request.id)) firstSeenRef.current.set(request.id, now());
      map.set(request.id, deadlineFor(request, firstSeenRef.current.get(request.id)!));
    }
    return map;
  }, [requests, now]);

  useEffect(() => {
    if (!open || requests.length === 0) return undefined;
    const timer = window.setInterval(() => setClock(now()), 250);
    return () => window.clearInterval(timer);
  }, [open, requests.length, now]);

  const visible = requests.filter((request) => clock < (deadlines.get(request.id) ?? 0) + fadeAfterExpiryMs);
  const live = visible.filter((request) => clock < (deadlines.get(request.id) ?? 0));
  const blocking = live.length > 0;

  return (
    <Dialog open={open} onOpenChange={onOpenChange} title="连接审批" dismissible={!blocking}
      description="用户名和用户代码不是密码，请通过当面或电话等可信渠道核验对方身份后再允许。">
      {visible.length === 0 ? (
        <EmptyState icon={<ShieldCheck />} title="没有待确认的连接" description="有成员申请加入时会在这里提醒你。" />
      ) : (
        <ul className="flex flex-col gap-3">
          <AnimatePresence initial={false}>
            {visible.map((request) => {
              const deadline = deadlines.get(request.id) ?? 0;
              const remaining = Math.max(0, deadline - clock);
              const expired = remaining === 0;
              return (
                <motion.li key={request.id} layout initial={{ opacity: 0, y: 8 }} animate={{ opacity: expired ? 0.5 : 1, y: 0 }} exit={{ opacity: 0 }}
                  transition={{ duration: 0.2, ease }}
                  className={cn('connection-approval-item rounded-md border border-divider bg-bg p-4', expired && 'grayscale')}>
                  <div className="flex items-center gap-3">
                    <Avatar name={request.displayName} seed={request.userCode} size={48} ringClass="ring-bg" />
                    <div className="flex min-w-0 flex-1 flex-col">
                      <strong className="truncate text-name font-semibold text-ink">{request.displayName}#{request.userCode}</strong>
                      <span className="num text-small text-ink-2">
                        用户代码 {request.userCode} · {formatRequestTime(request.requestedAt)}
                      </span>
                    </div>
                    <CountdownRing remaining={remaining} />
                  </div>
                  <p className="mt-3 text-small text-ink-2">{expired ? '请求已超时，对方需要重新申请。' : '该成员正在请求连接，批准后会立即进入聊天室。'}</p>
                  <div className="mt-3 flex justify-end gap-2">
                    <Button variant="danger-outline" size="sm" disabled={expired} onClick={() => onResolve(request, false)}>拒绝</Button>
                    <Button variant="primary" size="sm" disabled={expired} onClick={() => onResolve(request, true)}>允许连接</Button>
                  </div>
                </motion.li>
              );
            })}
          </AnimatePresence>
        </ul>
      )}
    </Dialog>
  );
}

function formatRequestTime(requestedAt: string): string {
  const parsed = new Date(requestedAt);
  return Number.isNaN(parsed.getTime()) ? requestedAt : parsed.toLocaleTimeString('zh-CN', { hour: '2-digit', minute: '2-digit', second: '2-digit', hour12: false });
}

/** 60-second ring: drawn with stroke-dashoffset (an SVG attribute, not a layout property). */
function CountdownRing({ remaining }: { remaining: number }) {
  const radius = 16;
  const circumference = 2 * Math.PI * radius;
  const fraction = remaining / approvalTimeoutMs;
  const seconds = Math.ceil(remaining / 1000);
  return (
    <span role="timer" aria-label={`剩余 ${seconds} 秒`} className="relative inline-flex size-10 shrink-0 items-center justify-center">
      <svg viewBox="0 0 40 40" aria-hidden="true" className="absolute inset-0 -rotate-90">
        <circle cx="20" cy="20" r={radius} fill="none" strokeWidth="3" className="stroke-hover" />
        <circle cx="20" cy="20" r={radius} fill="none" strokeWidth="3" strokeLinecap="round"
          className={cn(seconds <= 10 ? 'stroke-warning' : 'stroke-accent')}
          strokeDasharray={circumference} strokeDashoffset={circumference * (1 - fraction)} />
      </svg>
      <span className={cn('num text-small font-medium', seconds <= 10 ? 'text-warning' : 'text-ink')}>{seconds}</span>
    </span>
  );
}
