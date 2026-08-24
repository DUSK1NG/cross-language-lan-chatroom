import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';

import { WorkspaceRail } from './WorkspaceRail';

describe('WorkspaceRail', () => {
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
