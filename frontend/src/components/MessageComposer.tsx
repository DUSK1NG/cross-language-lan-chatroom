import { useState } from 'react';

import { createCommand } from '../bridge/chatBridge';
import type { BridgeState, ChatBridgeClient } from '../bridge/types';

type MessageComposerProps = { state: BridgeState; bridge: ChatBridgeClient };

export function MessageComposer({ state, bridge }: MessageComposerProps) {
  const [content, setContent] = useState('');
  const active = state.navigation.activeConversation;

  function submit() {
    const trimmed = content.trim();
    if (!trimmed || !active) return;
    const type = active.kind === 'room' ? 'chat.sendRoom' : 'chat.sendPrivate';
    const payload = active.kind === 'room'
      ? { room: active.id, content: trimmed }
      : { targetUserCode: active.userCode, content: trimmed };
    bridge.dispatch(createCommand(type, payload));
    setContent('');
  }

  return (
    <form className="message-composer" onSubmit={(event) => { event.preventDefault(); submit(); }}>
      <input aria-label="message composer" value={content} onChange={(event) => setContent(event.target.value)} placeholder="Write a message…" />
      <button className="send-button" type="submit" aria-label="send message">↗</button>
    </form>
  );
}
