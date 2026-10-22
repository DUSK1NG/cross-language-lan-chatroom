import type { BridgeState, ConversationRef, DirectMessageSummary, RoomSummary } from '../../bridge/types';

export type ConversationFilter = 'all' | 'rooms' | 'direct';

/** One row in the unified list. Built only from fields the bridge already publishes. */
export type ConversationEntry =
  | { kind: 'room'; key: string; title: string; seed: string; unread: number; room: RoomSummary }
  | { kind: 'dm'; key: string; title: string; seed: string; unread: number; direct: DirectMessageSummary; online?: boolean };

export function buildEntries(state: BridgeState, filter: ConversationFilter, query: string): ConversationEntry[] {
  const q = query.trim().toLowerCase();
  // Presence is only known for people in the active room's member list (spec note, plan §一.5).
  const presence = new Map(state.members.map((member) => [member.userCode, member.online]));
  const rooms: ConversationEntry[] = filter === 'direct' ? [] : state.rooms
    .filter((room) => room.roomName.toLowerCase().includes(q))
    .map((room) => ({ kind: 'room', key: `room:${room.roomName}`, title: room.roomName, seed: room.roomName, unread: room.unreadCount, room }));
  const dms: ConversationEntry[] = filter === 'rooms' ? [] : state.directMessages
    .filter((direct) => `${direct.displayName} ${direct.userCode}`.toLowerCase().includes(q))
    .map((direct) => ({ kind: 'dm', key: `dm:${direct.userCode}`, title: direct.displayName, seed: direct.userCode, unread: direct.unreadCount, direct, online: presence.get(direct.userCode) }));
  return [...rooms, ...dms];
}

export function isActive(entry: ConversationEntry, active: ConversationRef | undefined): boolean {
  if (!active) return false;
  return entry.kind === 'room'
    ? active.kind === 'room' && active.id === entry.room.roomName
    : active.kind === 'dm' && active.userCode === entry.direct.userCode;
}

/** 99+ cap for unread badges (spec §3). */
export function formatUnread(count: number): string {
  return count > 99 ? '99+' : String(count);
}

/** Accessible names kept from the previous sidebar so commands and tests stay stable. */
export function entryLabel(entry: ConversationEntry): string {
  return entry.kind === 'room' ? `room-${entry.room.roomName}` : `direct-${entry.direct.displayName.toLowerCase()}`;
}
