import { cleanup, fireEvent, render, screen } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { WorkspaceRail } from './WorkspaceRail';

describe('WorkspaceRail', () => {
  afterEach(cleanup);

  it('exposes the active workspace section to assistive technology', () => {
    render(<WorkspaceRail section="rooms" onSectionChange={vi.fn()} />);

    expect(screen.getByRole('button', { name: '群' })).toHaveAttribute('aria-pressed', 'true');
    expect(screen.getByRole('button', { name: '私' })).toHaveAttribute('aria-pressed', 'false');
  });

  it('swaps the pressed state when the direct section is active', () => {
    render(<WorkspaceRail section="direct" onSectionChange={vi.fn()} />);

    expect(screen.getByRole('button', { name: '群' })).toHaveAttribute('aria-pressed', 'false');
    expect(screen.getByRole('button', { name: '私' })).toHaveAttribute('aria-pressed', 'true');
  });

  it('shows a compact cat brand mark without replacing workspace controls', () => {
    const onSectionChange = vi.fn();
    const onSettings = vi.fn();

    render(<WorkspaceRail section="rooms" onSectionChange={onSectionChange} onSettings={onSettings} />);

    expect(screen.getByRole('img', { name: 'LAN Chat 猫咪标识' })).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: '私' }));
    fireEvent.click(screen.getByRole('button', { name: '设置' }));
    expect(onSectionChange).toHaveBeenCalledWith('direct');
    expect(onSettings).toHaveBeenCalledOnce();
  });
});
