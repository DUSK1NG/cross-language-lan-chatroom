import { forwardRef, type ButtonHTMLAttributes } from 'react';
import { cva, type VariantProps } from 'class-variance-authority';

import { cn } from '../../lib/cn';

/** shadcn/ui-style button. Variants and sizes reference design tokens only. */
export const buttonVariants = cva(
  'press inline-flex items-center justify-center gap-2 whitespace-nowrap font-medium select-none cursor-pointer disabled:cursor-not-allowed disabled:opacity-50 [&_svg]:shrink-0',
  {
    variants: {
      variant: {
        primary: 'bg-accent-strong text-on-accent hover:brightness-110',
        secondary: 'bg-input text-ink hover:bg-hover',
        ghost: 'bg-transparent text-ink-2 hover:bg-hover hover:text-ink',
        outline: 'border border-control bg-transparent text-ink hover:bg-hover',
        'danger-outline': 'border border-danger bg-transparent text-danger hover:bg-hover',
        danger: 'bg-danger text-on-accent hover:brightness-110'
      },
      size: {
        sm: 'h-8 px-3 text-small rounded-sm',
        md: 'h-9 px-4 text-body rounded-md',
        icon: 'size-9 rounded-full',
        'icon-sm': 'size-8 rounded-full'
      }
    },
    defaultVariants: { variant: 'secondary', size: 'md' }
  }
);

export type ButtonProps = ButtonHTMLAttributes<HTMLButtonElement> & VariantProps<typeof buttonVariants>;

export const Button = forwardRef<HTMLButtonElement, ButtonProps>(function Button({ className, variant, size, type = 'button', ...props }, ref) {
  return <button ref={ref} type={type} className={cn(buttonVariants({ variant, size }), className)} {...props} />;
});
