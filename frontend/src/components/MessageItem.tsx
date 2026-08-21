import type { BridgeCommand, ChatBridgeClient, MessageItem as MessageItemData } from '../bridge/types';
import { createCommand } from '../bridge/chatBridge';

type MessageItemProps = {
  message: MessageItemData;
  bridge: ChatBridgeClient;
  onCopy?: (message: MessageItemData) => void;
  onQuote?: (message: MessageItemData) => void;
  onLocalDelete?: (message: MessageItemData) => void;
  onRecall?: (message: MessageItemData) => void;
};

export function MessageItem({ message, bridge, onCopy, onQuote, onLocalDelete, onRecall }: MessageItemProps) {
  const dispatch = (type: string) => {
    const command: BridgeCommand = createCommand(type, { messageId: message.messageId });
    bridge.dispatch(command);
  };

  return (
    <article className={`message ${message.selfMessage ? 'message--self' : 'message--peer'} ${message.systemMessage ? 'message--system' : ''}`} data-testid={`message-${message.messageId}`}>
      <div className="message-cluster">
        <div className="message-meta"><strong>{message.displayName}</strong><span>#{message.userCode} · {message.time}</span></div>
        <div className="message-bubble">
          <p className="message-content message-content--wrap" data-testid={`message-content-${message.messageId}`}>{message.content}</p>
          <div className="message-actions" aria-label={`actions-${message.messageId}`}>
            <button type="button" onClick={() => (onCopy ?? (() => dispatch('message.copy')))(message)}>Copy</button>
            <button type="button" onClick={() => (onQuote ?? (() => undefined))(message)}>Quote</button>
            {message.selfMessage && <>
              <button type="button" onClick={() => (onLocalDelete ?? (() => dispatch('message.removeLocal')))(message)}>Remove</button>
              <button type="button" onClick={() => (onRecall ?? (() => dispatch('message.recall')))(message)}>Recall</button>
            </>}
          </div>
        </div>
      </div>
    </article>
  );
}
