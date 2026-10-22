import * as DropdownMenuPrimitive from '@radix-ui/react-dropdown-menu';
import * as ContextMenuPrimitive from '@radix-ui/react-context-menu';
import type { ReactNode } from 'react';

import { cn } from '../../lib/cn';

const contentClass = 'z-50 min-w-[200px] overflow-hidden rounded-md bg-elevated p-1 text-ink shadow-dialog';
const itemClass = 'flex h-9 cursor-pointer select-none items-center gap-3 rounded-sm px-3 text-body outline-none data-[highlighted]:bg-hover data-[disabled]:cursor-not-allowed data-[disabled]:opacity-50 [&_svg]:size-[18px] [&_svg]:text-ink-2';

export type MenuItem = {
  label: string;
  icon?: ReactNode;
  onSelect: () => void;
  danger?: boolean;
  disabled?: boolean;
  /** Shown at the item's end, e.g. a count. */
  trailing?: ReactNode;
};

function itemClasses(item: MenuItem) {
  return cn(itemClass, item.danger && 'text-danger-text [&_svg]:text-danger-text');
}

/** Dropdown menu (hamburger, header "more"). */
export function DropdownMenu({ trigger, items, align = 'start' }: { trigger: ReactNode; items: MenuItem[]; align?: 'start' | 'end' }) {
  return (
    <DropdownMenuPrimitive.Root modal={false}>
      <DropdownMenuPrimitive.Trigger asChild>{trigger}</DropdownMenuPrimitive.Trigger>
      <DropdownMenuPrimitive.Portal>
        <DropdownMenuPrimitive.Content align={align} sideOffset={8} className={contentClass}>
          {items.map((item) => (
            <DropdownMenuPrimitive.Item key={item.label} disabled={item.disabled} onSelect={item.onSelect} className={itemClasses(item)}>
              {item.icon}<span className="flex-1">{item.label}</span>{item.trailing}
            </DropdownMenuPrimitive.Item>
          ))}
        </DropdownMenuPrimitive.Content>
      </DropdownMenuPrimitive.Portal>
    </DropdownMenuPrimitive.Root>
  );
}

/** Right-click menu with the same items as a message's hover action bar. */
export function ContextMenu({ items, children }: { items: MenuItem[]; children: ReactNode }) {
  return (
    <ContextMenuPrimitive.Root modal={false}>
      <ContextMenuPrimitive.Trigger asChild>{children}</ContextMenuPrimitive.Trigger>
      <ContextMenuPrimitive.Portal>
        <ContextMenuPrimitive.Content className={contentClass}>
          {items.map((item) => (
            <ContextMenuPrimitive.Item key={item.label} disabled={item.disabled} onSelect={item.onSelect} className={itemClasses(item)}>
              {item.icon}<span className="flex-1">{item.label}</span>{item.trailing}
            </ContextMenuPrimitive.Item>
          ))}
        </ContextMenuPrimitive.Content>
      </ContextMenuPrimitive.Portal>
    </ContextMenuPrimitive.Root>
  );
}
