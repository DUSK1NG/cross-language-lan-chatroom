import { describe, expect, it } from 'vitest';

import type { MessageItem } from '../bridge/types';
import { continuesGroup, groupPositions, isRenderable, minutesOf } from './messageGroups';

function msg(id: string, userCode: string, time: string, extra: Partial<MessageItem> = {}): MessageItem {
  return { messageId: id, displayName: userCode, userCode, time, content: id, selfMessage: false, systemMessage: false, ...extra };
}

describe('message grouping', () => {
  it('parses HH:mm and rejects anything else', () => {
    expect(minutesOf('09:05')).toBe(545);
    expect(minutesOf('9:05')).toBe(545);
    expect(minutesOf('24:00')).toBeNull();
    expect(minutesOf('yesterday')).toBeNull();
  });

  it('groups one sender within five minutes and splits after', () => {
    expect(continuesGroup(msg('a', 'B', '10:00'), msg('b', 'B', '10:05'))).toBe(true);
    expect(continuesGroup(msg('a', 'B', '10:00'), msg('b', 'B', '10:06'))).toBe(false);
    expect(continuesGroup(msg('a', 'B', '10:00'), msg('b', 'C', '10:01'))).toBe(false);
    expect(continuesGroup(msg('a', 'B', '23:59'), msg('b', 'B', '00:01'))).toBe(false);
  });

  it('never groups across system messages or between self and peer', () => {
    expect(continuesGroup(msg('a', 'B', '10:00', { systemMessage: true }), msg('b', 'B', '10:00'))).toBe(false);
    expect(continuesGroup(msg('a', 'A', '10:00', { selfMessage: true }), msg('b', 'A', '10:00'))).toBe(false);
  });

  it('marks the first and last message of each group (the last carries the tail)', () => {
    const list = [msg('1', 'B', '10:00'), msg('2', 'B', '10:01'), msg('3', 'B', '10:02'), msg('4', 'C', '10:02')];
    expect(groupPositions(list)).toEqual([
      { first: true, last: false }, { first: false, last: false }, { first: false, last: true }, { first: true, last: true }
    ]);
  });

  it('does not render an empty bubble', () => {
    expect(isRenderable(msg('1', 'B', '10:00', { content: '   ' }))).toBe(false);
    expect(isRenderable(msg('1', 'B', '10:00', { content: '', attachment: { attachmentId: 'x', fileName: 'a.txt', logicalSize: 1 } }))).toBe(true);
  });
});
