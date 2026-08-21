import { useEffect, useState } from 'react';

import type { BridgeState, ChatBridgeClient } from '../bridge/types';
import { ChatHeader } from '../components/ChatHeader';
import { ConversationSidebar } from '../components/ConversationSidebar';
import { IdentityCard } from '../components/IdentityCard';
import { MemberPanel } from '../components/MemberPanel';
import { MessageComposer } from '../components/MessageComposer';
import { MessageTimeline } from '../components/MessageTimeline';
import { ModalSurface } from '../components/ModalSurface';
import { WorkspaceRail } from '../components/WorkspaceRail';

type WorkspacePageProps = { bridge: ChatBridgeClient; state: BridgeState; onSettings?: () => void };

export function WorkspacePage({ bridge, state, onSettings }: WorkspacePageProps) {
  const [memberDrawerOpen, setMemberDrawerOpen] = useState(false);

  useEffect(() => {
    if (!memberDrawerOpen) return undefined;
    const closeOnEscape = (event: KeyboardEvent) => {
      if (event.key === 'Escape') setMemberDrawerOpen(false);
    };
    window.addEventListener('keydown', closeOnEscape);
    return () => window.removeEventListener('keydown', closeOnEscape);
  }, [memberDrawerOpen]);

  return (
    <main className="app-shell workspace-shell">
      <WorkspaceRail onSettings={onSettings} />
      <ConversationSidebar bridge={bridge} state={state} />
      <section className="chat-region">
        <ChatHeader state={state} onMembers={() => setMemberDrawerOpen(true)} />
        <MessageTimeline bridge={bridge} state={state} />
        <MessageComposer bridge={bridge} state={state} />
      </section>
      <MemberPanel state={state} />
      <IdentityCard state={state} />
      {memberDrawerOpen && <ModalSurface title="Members" onClose={() => setMemberDrawerOpen(false)}><MemberPanel state={state} className="member-panel--modal" /></ModalSurface>}
    </main>
  );
}
