import type { MessageItem } from '../bridge/types';

/** Spec §6: consecutive messages from one sender within 5 minutes form a group. */
export const groupWindowMinutes = 5;

/** The bridge sends "HH:mm" only (gui_chat_controller.cpp:224) — no date. Returns minutes since midnight, or null. */
export function minutesOf(time: string): number | null {
  const match = /^(\d{1,2}):(\d{2})$/.exec(time.trim());
  if (!match) return null;
  const hours = Number(match[1]);
  const minutes = Number(match[2]);
  if (hours > 23 || minutes > 59) return null;
  return hours * 60 + minutes;
}

function sameSender(a: MessageItem, b: MessageItem): boolean {
  return !a.systemMessage && !b.systemMessage && a.userCode === b.userCode && a.selfMessage === b.selfMessage;
}

/** Whether b continues a's group. Unknown times fall back to same-sender grouping. A backwards clock (past midnight) starts a new group. */
export function continuesGroup(a: MessageItem | undefined, b: MessageItem): boolean {
  if (!a || !sameSender(a, b)) return false;
  const ta = minutesOf(a.time);
  const tb = minutesOf(b.time);
  if (ta === null || tb === null) return true;
  const gap = tb - ta;
  return gap >= 0 && gap <= groupWindowMinutes;
}

export type GroupPosition = {
  /** First message of its group: shows the sender name (group chats, peers). */
  first: boolean;
  /** Last message of its group: carries the tail corner and the avatar. */
  last: boolean;
};

export function groupPositions(messages: MessageItem[]): GroupPosition[] {
  return messages.map((message, index) => {
    const next = messages[index + 1];
    return {
      first: !continuesGroup(messages[index - 1], message),
      last: next === undefined || !continuesGroup(message, next)
    };
  });
}

/** Empty text with no attachment renders nothing (spec §6 "修复空气泡"). */
export function isRenderable(message: MessageItem): boolean {
  return Boolean(message.attachment) || message.content.trim().length > 0;
}
