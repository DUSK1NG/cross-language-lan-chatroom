import { useCallback, useEffect, useLayoutEffect, useRef, useState } from 'react';

import { createCommand } from '../bridge/chatBridge';
import type { BridgeState, ChatBridgeClient, MemberSummary } from '../bridge/types';
import { ChatHeader } from '../components/ChatHeader';
import { ConversationSidebar } from '../components/ConversationSidebar';
import { IdentityCard } from '../components/IdentityCard';
import { MemberPanel } from '../components/MemberPanel';
import { MessageComposer, type QuoteDraft } from '../components/MessageComposer';
import { MessageTimeline } from '../components/MessageTimeline';
import { ModalSurface } from '../components/ModalSurface';
import { WorkspaceRail } from '../components/WorkspaceRail';
import type { WorkspaceSection } from '../components/WorkspaceRail';
import { CommandFeedback, type CommandFeedbackProps } from '../components/CommandFeedback';
import { animateWorkspacePanels } from '../animation/motion';

type WorkspacePageProps = { bridge: ChatBridgeClient; state: BridgeState; onSettings?: () => void };
type AdminAction = 'mute' | 'kick';

const roomNamePattern = /^[A-Za-z0-9_]{1,32}$/;

export function WorkspacePage({ bridge, state, onSettings }: WorkspacePageProps) {
  const workspaceRef = useRef<HTMLElement>(null);
  const [memberDrawerOpen, setMemberDrawerOpen] = useState(false);
  const [sidebarOpen, setSidebarOpen] = useState(false);
  const [roomManageOpen, setRoomManageOpen] = useState(false);
  const [connectionApprovalOpen, setConnectionApprovalOpen] = useState(false);
  const [memberToManage, setMemberToManage] = useState<MemberSummary | null>(null);
  const [memberProfile, setMemberProfile] = useState<MemberSummary | null>(null);
  const [pendingAdminAction, setPendingAdminAction] = useState<{ action: AdminAction; member: MemberSummary } | null>(null);
  const [memberCode, setMemberCode] = useState('');
  const [section, setSection] = useState<WorkspaceSection>('rooms');
  const [draft, setDraft] = useState('');
  const [historyQuery, setHistoryQuery] = useState('');
  const [quote, setQuote] = useState<QuoteDraft | null>(null);
  const [focusComposerAtEndToken, setFocusComposerAtEndToken] = useState(0);
  const [createRoomOpen, setCreateRoomOpen] = useState(false);
  const [roomName, setRoomName] = useState('');
  const [isPrivate, setIsPrivate] = useState(false);
  const [roomFeedback, setRoomFeedback] = useState<CommandFeedbackProps>({ status: 'idle', message: '' });
  const [roomActionFeedback, setRoomActionFeedback] = useState<CommandFeedbackProps>({ status: 'idle', message: '' });
  const [adminFeedback, setAdminFeedback] = useState<CommandFeedbackProps>({ status: 'idle', message: '' });
  const pendingRoomCommandIdRef = useRef<string | null>(null);
  const pendingRoomUnsubscribeRef = useRef<(() => void) | null>(null);
  const pendingRoomActionIdRef = useRef<string | null>(null);
  const pendingRoomActionUnsubscribeRef = useRef<(() => void) | null>(null);
  const pendingAdminActionIdRef = useRef<string | null>(null);
  const pendingAdminActionUnsubscribeRef = useRef<(() => void) | null>(null);
  const canManageRoom = state.identity.admin || state.permissions.activeRoomCanManage;
  const hasBlockingConnectionApproval = state.identity.admin && (state.connectionApprovals?.length ?? 0) > 0;

  useLayoutEffect(() => animateWorkspacePanels(workspaceRef.current!), []);

  useEffect(() => {
    if (!memberDrawerOpen && !sidebarOpen && !createRoomOpen && !roomManageOpen && !connectionApprovalOpen && !memberToManage && !memberProfile && !pendingAdminAction) return undefined;
    const closeOnEscape = (event: KeyboardEvent) => {
      if (event.key !== 'Escape') return;
      if (hasBlockingConnectionApproval) return;
      setMemberDrawerOpen(false);
      setSidebarOpen(false);
      setRoomManageOpen(false);
      setConnectionApprovalOpen(false);
      setMemberToManage(null);
      setMemberProfile(null);
      setPendingAdminAction(null);
      if (!pendingRoomCommandIdRef.current) setCreateRoomOpen(false);
    };
    window.addEventListener('keydown', closeOnEscape);
    return () => window.removeEventListener('keydown', closeOnEscape);
  }, [memberDrawerOpen, sidebarOpen, createRoomOpen, roomManageOpen, connectionApprovalOpen, memberToManage, memberProfile, pendingAdminAction, hasBlockingConnectionApproval]);

  useEffect(() => () => {
    pendingRoomUnsubscribeRef.current?.();
    pendingRoomActionUnsubscribeRef.current?.();
    pendingAdminActionUnsubscribeRef.current?.();
  }, []);

  useEffect(() => {
    if (state.identity.admin && (state.connectionApprovals?.length ?? 0) > 0) {
      setConnectionApprovalOpen(true);
    }
  }, [state.connectionApprovals?.length, state.identity.admin]);

  const validRoomName = roomNamePattern.test(roomName.trim());

  function openCreateRoom() {
    setRoomFeedback({ status: 'idle', message: '' });
    setCreateRoomOpen(true);
  }

  function openRoomManage() {
    setMemberCode('');
    setRoomActionFeedback({ status: 'idle', message: '' });
    setRoomManageOpen(true);
  }

  function closeCreateRoom() {
    if (pendingRoomCommandIdRef.current) return;
    setCreateRoomOpen(false);
  }

  function submitCreateRoom() {
    const room = roomName.trim();
    if (!roomNamePattern.test(room) || pendingRoomCommandIdRef.current) return;

    const command = createCommand('room.create', { room, isPrivate });
    pendingRoomCommandIdRef.current = command.id;
    setRoomFeedback({ status: 'pending', message: '正在创建频道…' });
    const unsubscribe = bridge.subscribeCommandResult((result) => {
      if (result.id !== command.id || pendingRoomCommandIdRef.current !== command.id) return;
      unsubscribe();
      if (pendingRoomUnsubscribeRef.current === unsubscribe) pendingRoomUnsubscribeRef.current = null;
      pendingRoomCommandIdRef.current = null;
      if (!result.ok) {
        setRoomFeedback({ status: 'error', message: result.error?.message ?? '创建频道失败。' });
        return;
      }
      bridge.dispatch(createCommand('directory.refreshRooms', {}));
      bridge.dispatch(createCommand('conversation.selectRoom', { room }));
      setRoomName('');
      setIsPrivate(false);
      setRoomFeedback({ status: 'idle', message: '' });
      setCreateRoomOpen(false);
    });
    pendingRoomUnsubscribeRef.current = unsubscribe;
    bridge.dispatch(command);
  }

  function startDirect(member: { displayName: string; userCode: string }) {
    bridge.dispatch(createCommand('conversation.openPrivate', {
      displayName: member.displayName,
      userCode: member.userCode
    }));
  }

  function sendRoomAction(action: string) {
    const activeRoom = state.navigation.activeConversation?.kind === 'room' ? state.navigation.activeConversation.id : '';
    if (!activeRoom || pendingRoomActionIdRef.current || (action !== 'delete' && !memberCode.trim())) return;
    const command = createCommand('room.action', { action, room: activeRoom, ...(memberCode.trim() ? { targetUserCode: memberCode.trim() } : {}) });
    pendingRoomActionIdRef.current = command.id;
    setRoomActionFeedback({ status: 'pending', message: '正在执行操作…' });
    const unsubscribe = bridge.subscribeCommandResult((result) => {
      if (result.id !== command.id || pendingRoomActionIdRef.current !== command.id) return;
      unsubscribe();
      if (pendingRoomActionUnsubscribeRef.current === unsubscribe) pendingRoomActionUnsubscribeRef.current = null;
      pendingRoomActionIdRef.current = null;
      if (!result.ok) {
        setRoomActionFeedback({ status: 'error', message: result.error?.message ?? '操作失败。' });
        return;
      }
      setMemberCode('');
      setRoomActionFeedback({ status: 'success', message: '操作成功。' });
      if (action === 'delete') {
        setRoomManageOpen(false);
        bridge.dispatch(createCommand('conversation.selectRoom', { room: 'lobby' }));
      }
    });
    pendingRoomActionUnsubscribeRef.current = unsubscribe;
    bridge.dispatch(command);
  }

  function requestAdminAction(action: AdminAction, member: MemberSummary) {
    setAdminFeedback({ status: 'idle', message: '' });
    setPendingAdminAction({ action, member });
  }

  function confirmAdminAction() {
    if (!pendingAdminAction || pendingAdminActionIdRef.current) return;
    const { action, member } = pendingAdminAction;
    const command = createCommand('admin.action', { action, targetUserCode: member.userCode });
    pendingAdminActionIdRef.current = command.id;
    setAdminFeedback({ status: 'pending', message: '正在执行操作…' });
    const unsubscribe = bridge.subscribeCommandResult((result) => {
      if (result.id !== command.id || pendingAdminActionIdRef.current !== command.id) return;
      unsubscribe();
      if (pendingAdminActionUnsubscribeRef.current === unsubscribe) pendingAdminActionUnsubscribeRef.current = null;
      pendingAdminActionIdRef.current = null;
      setPendingAdminAction(null);
      setAdminFeedback(result.ok
        ? { status: 'success', message: '操作成功。' }
        : { status: 'error', message: result.error?.message ?? '操作失败。' });
    });
    pendingAdminActionUnsubscribeRef.current = unsubscribe;
    bridge.dispatch(command);
  }

  function resolveConnectionApproval(id: string, userCode: string, approve: boolean) {
    bridge.dispatch(createCommand('admin.action', {
      action: approve ? 'approve_connection' : 'deny_connection', targetUserCode: userCode, messageId: id
    }));
  }

  const quoteMessage = useCallback((message: { displayName: string; content: string }) => {
    const [, ...replyLines] = message.content.split('\n');
    const reply = message.content.startsWith('> ') && replyLines.length > 0
      ? replyLines.join('\n')
      : message.content;
    setQuote({ displayName: message.displayName, content: reply });
    setFocusComposerAtEndToken((token) => token + 1);
  }, []);

  function updateHistoryQuery(query: string) {
    setHistoryQuery(query);
    bridge.dispatch(createCommand('history.search', { query }));
  }

  return (
    <main ref={workspaceRef} className="app-shell workspace-shell">
      <WorkspaceRail section={section} onSectionChange={setSection} onSettings={onSettings} onOpenSidebar={() => setSidebarOpen(true)} />
      <ConversationSidebar bridge={bridge} state={state} section={section} onCreateRoom={openCreateRoom} />
      <section className="chat-region" data-motion="workspace-panel">
        <ChatHeader state={state} onMembers={() => setMemberDrawerOpen(true)} canManage={canManageRoom} onManageRoom={openRoomManage} onConnectionApprovals={() => setConnectionApprovalOpen(true)} historyQuery={historyQuery} onHistoryQueryChange={updateHistoryQuery} />
        <MessageTimeline bridge={bridge} state={state} onQuote={quoteMessage} canRecall={(message) => state.identity.admin || message.userCode === state.identity.userCode} />
        <MessageComposer bridge={bridge} state={state} quote={quote} onClearQuote={() => setQuote(null)} draft={draft} onDraftChange={setDraft} onCommandResult={() => undefined} focusAtEndToken={focusComposerAtEndToken} />
      </section>
      <MemberPanel state={state} localUserCode={state.identity.userCode} onStartDirect={startDirect} onViewProfile={setMemberProfile} canManage={canManageRoom} onManageMember={(member) => { setAdminFeedback({ status: 'idle', message: '' }); setMemberToManage(member); }} />
      <IdentityCard state={state} />
      {sidebarOpen && <ModalSurface title={section === 'rooms' ? '群聊导航' : '私信导航'} onClose={() => setSidebarOpen(false)}><ConversationSidebar bridge={bridge} state={state} section={section} onCreateRoom={openCreateRoom} /></ModalSurface>}
      {memberDrawerOpen && <ModalSurface title="成员" onClose={() => setMemberDrawerOpen(false)}><MemberPanel state={state} className="member-panel--modal" localUserCode={state.identity.userCode} onStartDirect={startDirect} onViewProfile={setMemberProfile} canManage={canManageRoom} onManageMember={(member) => { setAdminFeedback({ status: 'idle', message: '' }); setMemberToManage(member); }} /></ModalSurface>}
      {memberProfile && <ModalSurface title="成员资料" onClose={() => setMemberProfile(null)}>
        <div className="admin-profile">
          <strong>{memberProfile.displayName}#{memberProfile.userCode}</strong>
          <p>{memberProfile.admin ? '管理员' : '普通成员'} · {memberProfile.online ? '在线' : '离线'}</p>
        </div>
        <div className="admin-actions">
          {memberProfile.userCode !== state.identity.userCode && <button className="secondary-button" type="button" onClick={() => startDirect(memberProfile)}>私聊</button>}
          {canManageRoom && memberProfile.userCode !== state.identity.userCode && !memberProfile.admin && <>
            <button className="secondary-button" type="button" onClick={() => requestAdminAction('mute', memberProfile)}>禁言/解禁</button>
            <button className="danger-button" type="button" onClick={() => requestAdminAction('kick', memberProfile)}>踢出</button>
          </>}
        </div>
        <CommandFeedback {...adminFeedback} />
      </ModalSurface>}
      {memberToManage && <ModalSurface title="成员管理" onClose={() => setMemberToManage(null)}>
        <div className="admin-profile"><strong>{memberToManage.displayName}#{memberToManage.userCode}</strong><p>请选择要执行的管理操作。</p></div>
        <div className="admin-actions">
          <button className="secondary-button" type="button" disabled={pendingAdminActionIdRef.current !== null} onClick={() => requestAdminAction('mute', memberToManage)}>禁言/解禁</button>
          <button className="danger-button" type="button" disabled={pendingAdminActionIdRef.current !== null} onClick={() => requestAdminAction('kick', memberToManage)}>踢出</button>
        </div>
        <CommandFeedback {...adminFeedback} />
      </ModalSurface>}
      {roomManageOpen && <ModalSurface title="频道管理" onClose={() => setRoomManageOpen(false)}>
        <p className="settings-note">管理 #{state.navigation.activeConversation?.kind === 'room' ? state.navigation.activeConversation.id : ''}</p>
        <input className="admin-member-input" aria-label="成员代码" placeholder="成员代码，例如 B001" value={memberCode} onChange={(event) => setMemberCode(event.target.value)} />
        <CommandFeedback {...roomActionFeedback} />
        <div className="admin-actions">
          <button className="secondary-button" type="button" disabled={!memberCode.trim() || pendingRoomActionIdRef.current !== null} onClick={() => sendRoomAction('invite')}>邀请成员</button>
          <button className="secondary-button" type="button" disabled={!memberCode.trim() || pendingRoomActionIdRef.current !== null} onClick={() => sendRoomAction('remove_member')}>移除成员</button>
          <button className="danger-button" type="button" disabled={pendingRoomActionIdRef.current !== null} onClick={() => sendRoomAction('delete')}>删除频道</button>
        </div>
      </ModalSurface>}
      {pendingAdminAction && <ModalSurface title={pendingAdminAction.action === 'kick' ? '确认踢出成员' : '确认切换禁言'} onClose={() => setPendingAdminAction(null)}>
        <div className="admin-profile"><strong>{pendingAdminAction.member.displayName}#{pendingAdminAction.member.userCode}</strong><p>{pendingAdminAction.action === 'kick' ? '确定要将该成员踢出当前频道吗？' : '确定要切换该成员的禁言状态吗？'}</p></div>
        <div className="admin-actions">
          <button className="secondary-button" type="button" onClick={() => setPendingAdminAction(null)}>取消</button>
          <button className={pendingAdminAction.action === 'kick' ? 'danger-button' : 'primary-button'} type="button" disabled={pendingAdminActionIdRef.current !== null} onClick={confirmAdminAction}>确认</button>
        </div>
      </ModalSurface>}
      {connectionApprovalOpen && <ModalSurface title="连接审批" onClose={() => setConnectionApprovalOpen(false)} dismissible={!hasBlockingConnectionApproval}>
        {(state.connectionApprovals ?? []).length === 0 ? <p className="settings-note">没有待确认的成员连接。</p> : <div className="connection-approval-list">
          {(state.connectionApprovals ?? []).map((request) => <div className="connection-approval-item" key={request.id}>
            <strong>{request.displayName}#{request.userCode}</strong>
            <p>该成员正在请求连接。请先通过可信渠道核对身份；批准后会立即进入聊天室。</p>
            <div className="admin-actions">
              <button className="secondary-button" type="button" onClick={() => resolveConnectionApproval(request.id, request.userCode, false)}>拒绝</button>
              <button className="primary-button" type="button" onClick={() => resolveConnectionApproval(request.id, request.userCode, true)}>允许连接</button>
            </div>
          </div>)}
        </div>}
      </ModalSurface>}
      {createRoomOpen && (
        <ModalSurface title="新建频道" onClose={closeCreateRoom}>
          <form className="room-form" onSubmit={(event) => { event.preventDefault(); submitCreateRoom(); }}>
            <label className="room-form__field">频道名称
              <input aria-label="频道名称" value={roomName} onChange={(event) => setRoomName(event.target.value)} autoFocus />
            </label>
            {roomName && !validRoomName && <p className="room-form__hint">只能使用字母、数字和下划线，长度 1-32 位</p>}
            <label className="room-form__check">
              <input aria-label="私有频道" type="checkbox" checked={isPrivate} onChange={(event) => setIsPrivate(event.target.checked)} />
              私有频道
            </label>
            <CommandFeedback {...roomFeedback} />
            <div className="room-form__actions">
              <button type="button" className="secondary-button" onClick={closeCreateRoom} disabled={pendingRoomCommandIdRef.current !== null}>取消</button>
              <button type="submit" className="primary-button" aria-label="确认新建频道" disabled={!validRoomName || pendingRoomCommandIdRef.current !== null}>创建</button>
            </div>
          </form>
        </ModalSurface>
      )}
    </main>
  );
}
