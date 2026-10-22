import { useCallback, useEffect, useMemo, useRef, useState, type DragEvent } from 'react';
import { AnimatePresence, motion } from 'framer-motion';
import { Settings, Upload } from 'lucide-react';

import { createCommand } from '../bridge/chatBridge';
import type { BridgeState, ChatBridgeClient, ConnectionApprovalRequest, MemberSummary, MessageItem } from '../bridge/types';
import { ConversationSidebar } from '../components/sidebar/ConversationSidebar';
import { buildEntries, isActive, type ConversationFilter } from '../components/sidebar/conversations';
import { ChatHeader } from '../components/chat/ChatHeader';
import { ConnectionBanner } from '../components/chat/ConnectionBanner';
import { MessageList, conversationKey } from '../components/chat/MessageList';
import { parseQuote } from '../components/chat/MessageBubble';
import { Composer, type QuoteDraft } from '../components/composer/Composer';
import { InfoPanel } from '../components/shell/InfoPanel';
import { ResizeHandle, useSidebarWidth } from '../components/shell/ResizeHandle';
import { ApprovalDialog } from '../components/dialogs/ApprovalDialog';
import { AdminConfirmDialog, CreateRoomDialog, MemberDialog, RoomManageDialog, type AdminAction } from '../components/dialogs/RoomDialogs';
import { Avatar } from '../components/ui/avatar';
import { IconButton } from '../components/ui/icon-button';
import type { CommandFeedbackProps } from '../components/CommandFeedback';
import { useHotkeys } from '../hooks/useHotkeys';
import { compactQuery, narrowQuery, useMediaQuery } from '../hooks/useMediaQuery';
import { cn } from '../lib/cn';
import { ease } from '../lib/motion';

type WorkspacePageProps = { bridge: ChatBridgeClient; state: BridgeState; onSettings?: () => void; onReconnect?: () => void; theme?: 'dark' | 'light' };

const idle: CommandFeedbackProps = { status: 'idle', message: '' };

/** Subscribe once to the result of one command; returns an unsubscribe. */
function onResult(bridge: ChatBridgeClient, id: string, handle: (ok: boolean, message?: string) => void) {
  const unsubscribe = bridge.subscribeCommandResult((result) => {
    if (result.id !== id) return;
    unsubscribe();
    handle(result.ok, result.error?.message);
  });
  return unsubscribe;
}

/**
 * Three-column workspace (spec §2): conversation list (260–420px, resizable) · chat ·
 * optional 340px info panel. <900px the panel overlays; <700px list and chat alternate.
 */
export function WorkspacePage({ bridge, state, onSettings, onReconnect, theme = 'dark' }: WorkspacePageProps) {
  const narrow = useMediaQuery(narrowQuery);
  const compact = useMediaQuery(compactQuery);
  const [sidebarWidth, setSidebarWidth] = useSidebarWidth();
  const [phoneView, setPhoneView] = useState<'list' | 'chat'>('list');
  const [filter, setFilter] = useState<ConversationFilter>('rooms');
  const [infoOpen, setInfoOpen] = useState(false);
  const [searchOpen, setSearchOpen] = useState(false);
  const [historyQuery, setHistoryQuery] = useState('');
  const [drafts, setDrafts] = useState<Record<string, string>>({});
  const [quote, setQuote] = useState<QuoteDraft | null>(null);
  const [focusToken, setFocusToken] = useState(0);
  const [uploadToken, setUploadToken] = useState(0);
  const [dragging, setDragging] = useState(false);

  const [createOpen, setCreateOpen] = useState(false);
  const [createPending, setCreatePending] = useState(false);
  const [createFeedback, setCreateFeedback] = useState(idle);
  const [manageOpen, setManageOpen] = useState(false);
  const [managePending, setManagePending] = useState(false);
  const [manageFeedback, setManageFeedback] = useState(idle);
  const [approvalsOpen, setApprovalsOpen] = useState(false);
  const [profile, setProfile] = useState<MemberSummary | null>(null);
  const [pendingAdmin, setPendingAdmin] = useState<{ action: AdminAction; member: MemberSummary } | null>(null);
  const [adminPending, setAdminPending] = useState(false);
  const [adminFeedback, setAdminFeedback] = useState(idle);
  const subscriptions = useRef(new Set<() => void>());
  useEffect(() => () => { subscriptions.current.forEach((unsubscribe) => unsubscribe()); }, []);
  const track = (unsubscribe: () => void) => {
    subscriptions.current.add(unsubscribe);
    return () => { subscriptions.current.delete(unsubscribe); unsubscribe(); };
  };

  const active = state.navigation.activeConversation;
  const key = conversationKey(state);
  const activeRoom = active?.kind === 'room' ? active.id : '';
  const canManageRoom = state.identity.admin || state.permissions.activeRoomCanManage;
  const approvals = state.connectionApprovals ?? [];

  // Unread a conversation had just before it was opened (the bridge zeroes it on selection).
  const unreadSnapshot = useRef(new Map<string, number>());
  const entryKey = useRef(key);
  const entryUnread = useRef(0);
  if (entryKey.current !== key) {
    entryUnread.current = unreadSnapshot.current.get(key) ?? 0;
    entryKey.current = key;
  }
  useEffect(() => {
    const snapshot = new Map<string, number>();
    state.rooms.forEach((room) => snapshot.set(`room:${room.roomName}`, room.unreadCount));
    state.directMessages.forEach((direct) => snapshot.set(`dm:${direct.userCode}`, direct.unreadCount));
    unreadSnapshot.current = snapshot;
  }, [state.rooms, state.directMessages]);

  // Per-conversation drafts; switching conversations restores each one and drops the reply.
  const draft = drafts[key] ?? '';
  const setDraft = useCallback((value: string) => setDrafts((current) => ({ ...current, [key]: value })), [key]);
  useEffect(() => { setQuote(null); setHistoryQuery(''); setSearchOpen(false); }, [key]);

  // A new member request opens the approval dialog for the owner.
  const approvalCount = useRef(0);
  useEffect(() => {
    if (state.identity.admin && approvals.length > approvalCount.current) setApprovalsOpen(true);
    approvalCount.current = approvals.length;
  }, [approvals.length, state.identity.admin]);

  const selectFromList = () => { if (narrow) setPhoneView('chat'); };

  const startDirect = (member: MemberSummary) => {
    bridge.dispatch(createCommand('conversation.openPrivate', { displayName: member.displayName, userCode: member.userCode }));
    setProfile(null);
    if (narrow) setPhoneView('chat');
  };

  const quoteMessage = useCallback((message: MessageItem) => {
    setQuote({ displayName: message.displayName, content: parseQuote(message.content).body });
    setFocusToken((token) => token + 1);
  }, []);
  const canRecall = useCallback((message: MessageItem) => state.identity.admin || message.userCode === state.identity.userCode, [state.identity.admin, state.identity.userCode]);

  const updateHistoryQuery = (query: string) => {
    setHistoryQuery(query);
    bridge.dispatch(createCommand('history.search', { query }));
  };

  const submitCreate = (room: string, isPrivate: boolean, reset: () => void) => {
    const command = createCommand('room.create', { room, isPrivate });
    setCreatePending(true);
    setCreateFeedback({ status: 'pending', message: '正在创建频道…' });
    const stop = track(onResult(bridge, command.id, (ok, message) => {
      stop();
      setCreatePending(false);
      if (!ok) { setCreateFeedback({ status: 'error', message: message ?? '创建频道失败。' }); return; }
      bridge.dispatch(createCommand('directory.refreshRooms', {}));
      bridge.dispatch(createCommand('conversation.selectRoom', { room }));
      reset();
      setCreateFeedback(idle);
      setCreateOpen(false);
    }));
    bridge.dispatch(command);
  };

  const roomAction = (action: 'invite' | 'remove_member' | 'delete', memberCode: string, reset: () => void) => {
    if (!activeRoom || managePending) return;
    const command = createCommand('room.action', { action, room: activeRoom, ...(memberCode ? { targetUserCode: memberCode } : {}) });
    setManagePending(true);
    setManageFeedback({ status: 'pending', message: '正在执行操作…' });
    const stop = track(onResult(bridge, command.id, (ok, message) => {
      stop();
      setManagePending(false);
      if (!ok) { setManageFeedback({ status: 'error', message: message ?? '操作失败。' }); return; }
      reset();
      setManageFeedback({ status: 'success', message: '操作成功。' });
      if (action === 'delete') {
        setManageOpen(false);
        bridge.dispatch(createCommand('conversation.selectRoom', { room: 'lobby' }));
      }
    }));
    bridge.dispatch(command);
  };

  const confirmAdmin = () => {
    if (!pendingAdmin || adminPending) return;
    const command = createCommand('admin.action', { action: pendingAdmin.action, targetUserCode: pendingAdmin.member.userCode });
    setAdminPending(true);
    setAdminFeedback({ status: 'pending', message: '正在执行操作…' });
    const stop = track(onResult(bridge, command.id, (ok, message) => {
      stop();
      setAdminPending(false);
      setPendingAdmin(null);
      setAdminFeedback(ok ? { status: 'success', message: '操作成功。' } : { status: 'error', message: message ?? '操作失败。' });
    }));
    bridge.dispatch(command);
  };

  const resolveApproval = (request: ConnectionApprovalRequest, approve: boolean) => {
    bridge.dispatch(createCommand('admin.action', { action: approve ? 'approve_connection' : 'deny_connection', targetUserCode: request.userCode, messageId: request.id }));
  };

  const openManage = () => { setManageFeedback(idle); setManageOpen(true); };
  const openCreate = () => { setCreateFeedback(idle); setCreateOpen(true); };
  const refresh = () => { bridge.dispatch(createCommand('directory.refreshRooms', {})); bridge.dispatch(createCommand('directory.refreshUsers', {})); };

  const order = useMemo(() => buildEntries(state, filter, ''), [state, filter]);
  const stepConversation = (delta: number) => {
    if (order.length === 0) return;
    const current = order.findIndex((entry) => isActive(entry, active));
    const next = order[(current + delta + order.length) % order.length];
    bridge.dispatch(next.kind === 'room'
      ? createCommand('conversation.selectRoom', { room: next.room.roomName })
      : createCommand('conversation.selectDirect', { userCode: next.direct.userCode }));
  };

  const anyDialog = createOpen || manageOpen || approvalsOpen || profile !== null || pendingAdmin !== null;
  useHotkeys({
    focusSearch: (event) => {
      const search = document.querySelector<HTMLInputElement>('[data-hotkey="search"]');
      if (!search) return;
      event.preventDefault();
      if (narrow) setPhoneView('list');
      search.focus();
    },
    searchChat: (event) => { if (!active) return; event.preventDefault(); setSearchOpen(true); },
    prevConversation: (event) => { event.preventDefault(); stepConversation(-1); },
    nextConversation: (event) => { event.preventDefault(); stepConversation(1); },
    escape: () => {
      if (anyDialog) return;            // Radix dialogs handle their own Esc (and blocking ones refuse it)
      if (searchOpen) { updateHistoryQuery(''); setSearchOpen(false); return; }
      if (infoOpen) setInfoOpen(false);
    }
  });

  const onDragOver = (event: DragEvent) => {
    if (active?.kind !== 'room' || !event.dataTransfer.types.includes('Files')) return;
    event.preventDefault();
    setDragging(true);
  };
  const onDrop = (event: DragEvent) => {
    event.preventDefault();
    setDragging(false);
    // The page may not read local files (attachment contract), so a drop opens the native picker.
    if (active?.kind === 'room') setUploadToken((token) => token + 1);
  };

  const showList = !narrow || phoneView === 'list';
  const showChat = !narrow || phoneView === 'chat';

  return (
    <main className="flex h-full w-full overflow-hidden bg-bg text-ink">
      {showList && (
        <div className="relative flex h-full shrink-0 flex-col border-r border-divider bg-sidebar" style={{ width: narrow ? '100%' : sidebarWidth }}>
          <ConversationSidebar state={state} bridge={bridge} filter={filter} onFilterChange={setFilter} onCreateRoom={openCreate}
            onSettings={onSettings} onConnectionApprovals={() => setApprovalsOpen(true)} onManageRoom={openManage}
            canManageRoom={canManageRoom && Boolean(activeRoom)} onSelected={selectFromList}
            footer={<IdentityCard state={state} onSettings={onSettings} />} />
          {!narrow && <ResizeHandle width={sidebarWidth} onWidthChange={setSidebarWidth} />}
        </div>
      )}
      {showChat && (
        <section className="relative flex h-full min-w-0 flex-1" aria-label="聊天">
          <div className="relative flex min-w-0 flex-1 flex-col" onDragOver={onDragOver} onDragLeave={() => setDragging(false)} onDrop={onDrop}>
            <ChatHeader state={state} infoOpen={infoOpen} onToggleInfo={() => setInfoOpen((open) => !open)} canManage={canManageRoom}
              onManageRoom={openManage} onConnectionApprovals={() => setApprovalsOpen(true)} onRefresh={refresh}
              historyQuery={historyQuery} onHistoryQueryChange={updateHistoryQuery} searchOpen={searchOpen} onSearchOpenChange={setSearchOpen}
              onBack={narrow ? () => setPhoneView('list') : undefined} />
            <ConnectionBanner connection={state.connection} onReconnect={onReconnect} />
            <MessageList state={state} bridge={bridge} onQuote={quoteMessage} canRecall={canRecall} entryUnread={entryUnread.current} />
            <Composer state={state} bridge={bridge} quote={quote} onClearQuote={() => setQuote(null)} draft={draft} onDraftChange={setDraft}
              onCommandResult={() => undefined} focusAtEndToken={focusToken} uploadRequestToken={uploadToken} theme={theme} />
            <AnimatePresence>
              {dragging && (
                <motion.div initial={{ opacity: 0 }} animate={{ opacity: 1 }} exit={{ opacity: 0 }} transition={{ duration: 0.15, ease }}
                  className="pointer-events-none absolute inset-4 z-40 flex flex-col items-center justify-center gap-2 rounded-card border-2 border-dashed border-accent bg-bg/90 text-accent-text">
                  <Upload size={32} aria-hidden="true" />
                  <p className="text-title font-semibold">松开以发送附件</p>
                  <p className="text-small text-ink-2">将打开文件选择窗口确认要加密发送的文件</p>
                </motion.div>
              )}
            </AnimatePresence>
          </div>
          <InfoPanel state={state} open={infoOpen} overlay={compact} onClose={() => setInfoOpen(false)} onStartDirect={startDirect}
            onViewProfile={(member) => { setAdminFeedback(idle); setProfile(member); }} canManage={canManageRoom}
            onManageMember={(member) => { setAdminFeedback(idle); setProfile(member); }} onManageRoom={openManage} />
        </section>
      )}

      <CreateRoomDialog open={createOpen} onOpenChange={setCreateOpen} pending={createPending} feedback={createFeedback} onSubmit={submitCreate} />
      <RoomManageDialog open={manageOpen} onOpenChange={setManageOpen} room={activeRoom} pending={managePending} feedback={manageFeedback} onAction={roomAction} />
      <ApprovalDialog open={approvalsOpen} onOpenChange={setApprovalsOpen} requests={approvals} onResolve={resolveApproval} />
      <MemberDialog member={profile} localUserCode={state.identity.userCode} canManage={canManageRoom} feedback={adminFeedback} pending={adminPending}
        onOpenChange={(open) => { if (!open) setProfile(null); }} onDirect={startDirect}
        onAdmin={(action, member) => { setAdminFeedback(idle); setPendingAdmin({ action, member }); }} />
      <AdminConfirmDialog pendingAction={pendingAdmin} busy={adminPending} onCancel={() => setPendingAdmin(null)} onConfirm={confirmAdmin} />
    </main>
  );
}

/** The local identity and connection state at the foot of the conversation list. */
function IdentityCard({ state, onSettings }: { state: BridgeState; onSettings?: () => void }) {
  const connected = state.connection.phase === 'connected';
  // The banner above the messages carries the detailed reconnect text; the card only names the phase.
  const status = connected ? state.connection.statusText
    : state.connection.phase === 'reconnecting' || state.connection.phase === 'connecting' ? '连接中…' : '未连接';
  return (
    <div data-testid="identity-card" className="flex h-16 shrink-0 items-center gap-3 border-t border-divider px-4">
      <Avatar name={state.identity.displayName} seed={state.identity.userCode} size={36} online={connected} />
      <span className="flex min-w-0 flex-1 flex-col">
        <strong className="truncate text-body font-semibold text-ink">{state.identity.displayName} #{state.identity.userCode}</strong>
        <small className={cn('truncate text-small', connected ? 'text-ink-2' : 'text-warning')}>{status}</small>
      </span>
      {state.identity.admin && <span className="shrink-0 rounded-sm bg-accent-soft px-2 text-small text-accent-text">管理员</span>}
      {onSettings && <IconButton label="设置" size="sm" onClick={onSettings} tooltip="设置" ><Settings size={18} aria-hidden="true" /></IconButton>}
    </div>
  );
}
