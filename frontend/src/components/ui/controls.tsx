import * as SwitchPrimitive from '@radix-ui/react-switch';
import * as RadioGroupPrimitive from '@radix-ui/react-radio-group';
import * as SelectPrimitive from '@radix-ui/react-select';
import { Check, ChevronDown } from 'lucide-react';

import { cn } from '../../lib/cn';

/** shadcn/ui Switch. Renders role="switch" with aria-checked. */
export function Switch({ checked, onCheckedChange, label, disabled }: { checked: boolean; onCheckedChange: (checked: boolean) => void; label: string; disabled?: boolean }) {
  return (
    <SwitchPrimitive.Root checked={checked} onCheckedChange={onCheckedChange} aria-label={label} disabled={disabled}
      className="relative inline-flex h-6 w-10 shrink-0 cursor-pointer items-center rounded-full bg-input transition-ui transition-colors data-[state=checked]:bg-accent-strong disabled:cursor-not-allowed disabled:opacity-50">
      <SwitchPrimitive.Thumb className="block size-4 translate-x-1 rounded-full bg-ink transition-transform transition-ui data-[state=checked]:translate-x-5 data-[state=checked]:bg-on-accent" />
    </SwitchPrimitive.Root>
  );
}

export type RadioOption<T extends string> = { value: T; label: string; swatch?: string };

/** shadcn/ui RadioGroup rendered as a segmented control (role="radiogroup" / "radio"). */
export function SegmentedRadio<T extends string>({ value, onValueChange, options, label }: { value: T; onValueChange: (value: T) => void; options: RadioOption<T>[]; label: string }) {
  return (
    <RadioGroupPrimitive.Root value={value} onValueChange={(next) => onValueChange(next as T)} aria-label={label}
      className="inline-flex rounded-md bg-input p-1 gap-1">
      {options.map((option) => (
        <RadioGroupPrimitive.Item key={option.value} value={option.value} aria-label={option.label}
          className="press inline-flex h-8 cursor-pointer items-center gap-2 rounded-sm px-3 text-small text-ink-2 hover:text-ink data-[state=checked]:bg-active data-[state=checked]:text-on-accent">
          {option.swatch && <span aria-hidden="true" className="size-3 rounded-full" style={{ background: `var(${option.swatch})` }} />}
          {option.label}
        </RadioGroupPrimitive.Item>
      ))}
    </RadioGroupPrimitive.Root>
  );
}

export type SelectOption = { value: string; label: string };

/** shadcn/ui Select (role="combobox"). */
export function Select({ value, onValueChange, options, label }: { value: string; onValueChange: (value: string) => void; options: SelectOption[]; label: string }) {
  return (
    <SelectPrimitive.Root value={value} onValueChange={onValueChange}>
      <SelectPrimitive.Trigger aria-label={label}
        className="press inline-flex h-9 min-w-[160px] cursor-pointer items-center justify-between gap-2 rounded-md border border-control bg-input px-3 text-body text-ink">
        <SelectPrimitive.Value />
        <SelectPrimitive.Icon><ChevronDown size={16} className="text-ink-2" aria-hidden="true" /></SelectPrimitive.Icon>
      </SelectPrimitive.Trigger>
      <SelectPrimitive.Portal>
        <SelectPrimitive.Content position="popper" sideOffset={4} className="z-50 min-w-[var(--radix-select-trigger-width)] overflow-hidden rounded-md bg-elevated p-1 shadow-dialog">
          <SelectPrimitive.Viewport>
            {options.map((option) => (
              <SelectPrimitive.Item key={option.value} value={option.value}
                className={cn('relative flex h-9 cursor-pointer select-none items-center rounded-sm pl-8 pr-3 text-body text-ink outline-none data-[highlighted]:bg-hover')}>
                <SelectPrimitive.ItemIndicator className="absolute left-2 inline-flex"><Check size={16} className="text-accent-text" aria-hidden="true" /></SelectPrimitive.ItemIndicator>
                <SelectPrimitive.ItemText>{option.label}</SelectPrimitive.ItemText>
              </SelectPrimitive.Item>
            ))}
          </SelectPrimitive.Viewport>
        </SelectPrimitive.Content>
      </SelectPrimitive.Portal>
    </SelectPrimitive.Root>
  );
}
