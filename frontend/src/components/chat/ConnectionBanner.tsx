import { useEffect, useRef, useState } from 'react';
import { AnimatePresence, motion } from 'framer-motion';
import { CheckCircle2, Loader2, WifiOff } from 'lucide-react';

import type { BridgeState } from '../../bridge/types';
import { cn } from '../../lib/cn';
import { Button } from '../ui/button';

type ConnectionBannerProps = { connection: BridgeState['connection']; onReconnect?: () => void };

/**
 * Thin strip under the chat header (spec §4): reconnecting/disconnected in the warning
 * colour with a 重新连接 action; after recovery it says so briefly and disappears.
 */
export function ConnectionBanner({ connection, onReconnect }: ConnectionBannerProps) {
  const previous = useRef(connection.phase);
  const [recovered, setRecovered] = useState(false);

  useEffect(() => {
    const before = previous.current;
    previous.current = connection.phase;
    if (connection.phase === 'connected' && (before === 'reconnecting' || before === 'connecting')) {
      setRecovered(true);
      const timer = window.setTimeout(() => setRecovered(false), 2400);
      return () => window.clearTimeout(timer);
    }
    if (connection.phase !== 'connected') setRecovered(false);
    return undefined;
  }, [connection.phase]);

  const busy = connection.phase === 'reconnecting' || connection.phase === 'connecting';
  const down = connection.phase === 'error' || connection.phase === 'idle';
  const visible = busy || down || recovered;
  const attempt = connection.reconnectAttempt;

  return (
    <AnimatePresence initial={false}>
      {visible && (
        <motion.div key="banner" role="status" aria-live="polite" data-testid="connection-banner"
          initial={{ opacity: 0, y: -8 }} animate={{ opacity: 1, y: 0 }} exit={{ opacity: 0, y: -8 }}
          transition={{ duration: 0.2, ease: [0.2, 0.8, 0.2, 1] }}
          className={cn('flex h-8 shrink-0 items-center gap-2 px-4 text-small',
            recovered ? 'bg-sidebar text-online' : 'bg-sidebar text-warning')}>
          {recovered ? <CheckCircle2 size={16} aria-hidden="true" />
            : busy ? <Loader2 size={16} aria-hidden="true" className="animate-[spin_0.8s_linear_infinite]" />
              : <WifiOff size={16} aria-hidden="true" />}
          <span className="min-w-0 flex-1 truncate">
            {recovered ? '已重新连接' : `${connection.statusText}${busy && attempt ? ` · 第 ${attempt} 次重连` : ''}`}
          </span>
          {down && onReconnect && <Button variant="ghost" size="sm" className="h-6 text-warning hover:text-warning" onClick={onReconnect}>重新连接</Button>}
        </motion.div>
      )}
    </AnimatePresence>
  );
}
