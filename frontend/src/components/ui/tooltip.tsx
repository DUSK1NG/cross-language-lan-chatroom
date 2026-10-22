import * as TooltipPrimitive from '@radix-ui/react-tooltip';
import type { ReactNode } from 'react';

export const TooltipProvider = TooltipPrimitive.Provider;

/** A text tooltip. Wrap the app once in <TooltipProvider delayDuration={400}>. */
export function Tooltip({ label, children, side = 'bottom' }: { label: ReactNode; children: ReactNode; side?: 'top' | 'bottom' | 'left' | 'right' }) {
  return (
    <TooltipPrimitive.Root>
      <TooltipPrimitive.Trigger asChild>{children}</TooltipPrimitive.Trigger>
      <TooltipPrimitive.Portal>
        <TooltipPrimitive.Content side={side} sideOffset={8}
          className="z-50 rounded-sm bg-elevated px-2 py-1 text-small text-ink shadow-float data-[state=delayed-open]:animate-in">
          {label}
        </TooltipPrimitive.Content>
      </TooltipPrimitive.Portal>
    </TooltipPrimitive.Root>
  );
}
