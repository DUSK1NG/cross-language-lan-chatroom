import { useEffect, useRef, useState } from 'react';

import type { ConnectionPhase } from '../bridge/types';
import { Icon } from './Icon';

type Toast = { id: number; kind: 'lost' | 'restored'; title: string; detail: string };

/**
 * 滑入式状态卡片：连接中断时提示自动重连，恢复后提示消息与草稿已保留。
 * 只响应相位变化，不在首次进入工作区时弹出。
 */
export function ConnectionToast({ phase }: { phase: ConnectionPhase }) {
  const previousRef = useRef<ConnectionPhase>(phase);
  const [toast, setToast] = useState<Toast | null>(null);
  const [leaving, setLeaving] = useState(false);

  useEffect(() => {
    const previous = previousRef.current;
    previousRef.current = phase;
    if (previous === phase) return;
    if (phase === 'reconnecting' && previous === 'connected') {
      setLeaving(false);
      setToast({ id: Date.now(), kind: 'lost', title: '连接中断', detail: '正在自动重连，当前会话和草稿会保留。' });
    } else if (phase === 'connected' && previous === 'reconnecting') {
      setLeaving(false);
      setToast({ id: Date.now(), kind: 'restored', title: '连接已恢复', detail: '已重新同步当前会话的消息。' });
    }
  }, [phase]);

  useEffect(() => {
    if (!toast || toast.kind !== 'restored') return undefined;
    const leave = window.setTimeout(() => setLeaving(true), 3200);
    const remove = window.setTimeout(() => setToast(null), 3500);
    return () => { window.clearTimeout(leave); window.clearTimeout(remove); };
  }, [toast]);

  if (!toast) return null;
  return (
    <div key={toast.id} className={`connection-toast connection-toast--${toast.kind}${leaving ? ' connection-toast--leaving' : ''}`}
      aria-live="polite" data-testid="connection-toast">
      <span className="connection-toast__icon" aria-hidden="true"><Icon name={toast.kind === 'lost' ? 'wifi' : 'check'} size={18} /></span>
      <span className="connection-toast__copy"><strong>{toast.title}</strong><span>{toast.detail}</span></span>
      {toast.kind === 'lost' && <span className="connection-toast__spinner" aria-hidden="true" />}
      <button type="button" className="connection-toast__close" aria-label="关闭连接提示" onClick={() => setToast(null)}><Icon name="close" size={14} /></button>
      {toast.kind === 'restored' && <span className="connection-toast__timer" aria-hidden="true" />}
    </div>
  );
}
