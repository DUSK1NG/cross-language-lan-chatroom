import { fireEvent, render, screen } from '@testing-library/react';
import { describe, expect, it, vi } from 'vitest';

import { BridgeUnavailablePage } from './BridgeUnavailablePage';

describe('BridgeUnavailablePage', () => {
  it('shows a controlled error and exposes retry', () => {
    const onRetry = vi.fn();
    render(<BridgeUnavailablePage message="桌面服务未连接" onRetry={onRetry} />);

    expect(screen.getByRole('heading', { name: '界面暂时不可用' })).toBeInTheDocument();
    expect(screen.getByRole('alert')).toHaveTextContent('桌面服务未连接');
    fireEvent.click(screen.getByRole('button', { name: '重试连接' }));
    expect(onRetry).toHaveBeenCalledOnce();
  });
});
