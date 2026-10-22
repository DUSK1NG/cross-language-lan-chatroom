import { forwardRef, type ButtonHTMLAttributes, type ReactNode } from 'react';

import { cn } from '../../lib/cn';
import { Tooltip } from './tooltip';

type IconButtonProps = ButtonHTMLAttributes<HTMLButtonElement> & {
  /** Required: becomes both the accessible name and the tooltip text. */
  label: string;
  tooltip?: ReactNode;
  size?: 'sm' | 'md';
  badge?: ReactNode;
};

/** Icon-only button: always has an aria-label and a tooltip (spec §12). */
export const IconButton = forwardRef<HTMLButtonElement, IconButtonProps>(function IconButton(
  { label, tooltip, size = 'md', badge, className, children, type = 'button', ...props }, ref
) {
  return (
    <Tooltip label={tooltip ?? label}>
      <button ref={ref} type={type} aria-label={label}
        className={cn('press relative inline-flex shrink-0 items-center justify-center rounded-full text-ink-2 hover:bg-hover hover:text-ink cursor-pointer disabled:cursor-not-allowed disabled:opacity-50',
          size === 'md' ? 'size-9' : 'size-8', className)} {...props}>
        {children}
        {badge}
      </button>
    </Tooltip>
  );
});
