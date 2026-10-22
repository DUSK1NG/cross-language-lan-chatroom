import { act, cleanup, fireEvent, render, screen, within } from '@testing-library/react';
import { afterEach, beforeEach, describe, expect, it, vi } from 'vitest';

import type { ConnectionApprovalRequest } from '../../bridge/types';
import { ApprovalDialog, approvalTimeoutMs } from './ApprovalDialog';

const start = Date.parse('2026-10-21T10:00:00Z');

function request(id: string, requestedAt: string): ConnectionApprovalRequest {
  return { id, displayName: `User${id}`, userCode: `U00${id}`, requestedAt };
}

describe('ApprovalDialog', () => {
  let now = start;
  const clock = () => now;
  const advance = async (ms: number) => {
    now += ms;
    await act(async () => { vi.advanceTimersByTime(ms); });
  };

  beforeEach(() => { vi.useFakeTimers(); now = start; });
  afterEach(() => { cleanup(); vi.useRealTimers(); });

  it('counts down from the server request time and expires the request at the 60 second server timeout', async () => {
    const onResolve = vi.fn();
    render(<ApprovalDialog open onOpenChange={vi.fn()} now={clock} onResolve={onResolve}
      requests={[request('1', new Date(start - 15_000).toISOString())]} />);

    expect(approvalTimeoutMs).toBe(60_000);
    expect(screen.getByText('User1#U001')).toBeInTheDocument();
    expect(screen.getByRole('timer', { name: '剩余 45 秒' })).toBeInTheDocument();
    expect(screen.getByRole('button', { name: '允许连接' })).toBeEnabled();

    await advance(44_000);
    expect(screen.getByRole('timer', { name: '剩余 1 秒' })).toBeInTheDocument();

    await advance(1_000);
    expect(screen.getByText('请求已超时，对方需要重新申请。')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: '允许连接' })).toBeDisabled();
    expect(screen.getByRole('button', { name: '拒绝' })).toBeDisabled();
    fireEvent.click(screen.getByRole('button', { name: '允许连接' }));
    expect(onResolve).not.toHaveBeenCalled();

    // Expired requests fade out and the dialog becomes dismissible again.
    await advance(1_600);
    expect(screen.queryByText('User1#U001')).not.toBeInTheDocument();
    expect(screen.getByText('没有待确认的连接')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: 'close' })).toBeInTheDocument();
  });

  it('counts a full minute from first sight when the request time is from another clock', () => {
    render(<ApprovalDialog open onOpenChange={vi.fn()} now={clock} onResolve={vi.fn()}
      requests={[request('1', '2026-08-24T10:00:00Z'), request('2', new Date(start + 30_000).toISOString()), request('3', 'not a date')]} />);

    expect(screen.getAllByRole('timer', { name: '剩余 60 秒' })).toHaveLength(3);
  });

  it('resolves a live request and explains how to verify the member', () => {
    const onResolve = vi.fn();
    const pending = request('7', new Date(start).toISOString());
    render(<ApprovalDialog open onOpenChange={vi.fn()} now={clock} onResolve={onResolve} requests={[pending]} />);

    const dialog = screen.getByRole('dialog', { name: '连接审批' });
    expect(within(dialog).getByText(/用户名和用户代码不是密码/)).toBeInTheDocument();
    fireEvent.click(within(dialog).getByRole('button', { name: '拒绝' }));
    expect(onResolve).toHaveBeenCalledWith(pending, false);
  });
});
