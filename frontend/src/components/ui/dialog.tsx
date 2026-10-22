import * as DialogPrimitive from '@radix-ui/react-dialog';
import { AnimatePresence, motion } from 'framer-motion';
import { X } from 'lucide-react';
import type { ReactNode } from 'react';

import { cn } from '../../lib/cn';

type DialogProps = {
  open: boolean;
  onOpenChange: (open: boolean) => void;
  title: string;
  /** Visually hide the title while keeping it as the dialog's accessible name. */
  hideTitle?: boolean;
  description?: ReactNode;
  /** When false the dialog cannot be dismissed with Esc, the overlay or a close button. */
  dismissible?: boolean;
  className?: string;
  children: ReactNode;
};

/**
 * The one dialog used everywhere (spec §9): centred, 16px radius, --bg-sidebar surface,
 * rgba(0,0,0,.5) overlay, scale .96 → 1 with fade. Radix supplies role="dialog",
 * aria-labelledby, focus trapping and Esc handling.
 */
export function Dialog({ open, onOpenChange, title, hideTitle = false, description, dismissible = true, className, children }: DialogProps) {
  const block = (event: Event) => { if (!dismissible) event.preventDefault(); };
  return (
    <DialogPrimitive.Root open={open} onOpenChange={(next) => { if (next || dismissible) onOpenChange(next); }}>
      <AnimatePresence>
        {open && (
          <DialogPrimitive.Portal forceMount>
            <DialogPrimitive.Overlay asChild forceMount>
              <motion.div className="fixed inset-0 z-40 bg-overlay"
                initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }}
                transition={{ duration: 0.2, ease: [0.2, 0.8, 0.2, 1] }} />
            </DialogPrimitive.Overlay>
            <div className="fixed inset-0 z-50 grid place-items-center p-4 pointer-events-none">
              <DialogPrimitive.Content asChild forceMount onEscapeKeyDown={block} onPointerDownOutside={block} onInteractOutside={block}
                aria-describedby={description ? undefined : undefined}>
                <motion.section
                  className={cn('pointer-events-auto relative flex max-h-[calc(100vh-64px)] w-full max-w-[440px] flex-col overflow-hidden rounded-card bg-elevated text-ink shadow-dialog', className)}
                  initial={{ opacity: 0, scale: 0.96 }} animate={{ opacity: 1, scale: 1 }} exit={{ opacity: 0, scale: 0.96 }}
                  transition={{ duration: 0.2, ease: [0.2, 0.8, 0.2, 1] }}>
                  <header className={cn('flex items-center gap-3 px-6 pt-6', hideTitle && 'visually-hidden')}>
                    <DialogPrimitive.Title className="flex-1 text-title font-semibold">{title}</DialogPrimitive.Title>
                    {dismissible && !hideTitle && (
                      <DialogPrimitive.Close aria-label="close" className="press -mr-2 inline-flex size-8 items-center justify-center rounded-full text-ink-2 hover:bg-hover hover:text-ink cursor-pointer">
                        <X size={18} aria-hidden="true" />
                      </DialogPrimitive.Close>
                    )}
                  </header>
                  {description
                    ? <DialogPrimitive.Description className="px-6 pt-2 text-small text-ink-2">{description}</DialogPrimitive.Description>
                    : <DialogPrimitive.Description className="visually-hidden">{title}</DialogPrimitive.Description>}
                  <div className="scroll-thin min-h-0 flex-1 overflow-y-auto px-6 pb-6 pt-4">{children}</div>
                  {dismissible && hideTitle && (
                    <DialogPrimitive.Close aria-label="close" className="press absolute right-3 top-3 z-10 inline-flex size-8 items-center justify-center rounded-full bg-overlay text-on-accent cursor-pointer">
                      <X size={18} aria-hidden="true" />
                    </DialogPrimitive.Close>
                  )}
                </motion.section>
              </DialogPrimitive.Content>
            </div>
          </DialogPrimitive.Portal>
        )}
      </AnimatePresence>
    </DialogPrimitive.Root>
  );
}
