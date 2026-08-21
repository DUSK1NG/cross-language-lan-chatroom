export type ConnectionPhase = 'idle' | 'connecting' | 'connected' | 'reconnecting' | 'error';

export type ConversationRef =
  | { kind: 'room'; id: string; title: string }
  | { kind: 'dm'; id: string; title: string; userCode: string };

export type BridgeError = {
  code: string;
  message: string;
  retryable: boolean;
  source: 'bridge' | 'controller' | 'server' | 'webengine';
  commandId?: string;
};

export type RoomSummary = {
  roomName: string;
  memberCount: number;
  unreadCount: number;
  ownerCode?: string;
  private?: boolean;
  canManage?: boolean;
};

export type DirectMessageSummary = {
  displayName: string;
  userCode: string;
  unreadCount: number;
};

export type MessageItem = {
  messageId: string;
  displayName: string;
  userCode: string;
  time: string;
  content: string;
  selfMessage: boolean;
  systemMessage: boolean;
};

export type MemberSummary = {
  displayName: string;
  userCode: string;
  online: boolean;
  admin: boolean;
};

export type BridgeState = {
  schemaVersion: 1;
  connection: {
    phase: ConnectionPhase;
    statusText: string;
    retryable: boolean;
    lastError?: BridgeError;
  };
  identity: {
    displayName: string;
    userCode: string;
    admin: boolean;
  };
  navigation: {
    page: 'mode' | 'connect' | 'host' | 'workspace' | 'settings';
    activeConversation?: ConversationRef;
  };
  rooms: RoomSummary[];
  directMessages: DirectMessageSummary[];
  activeMessages: MessageItem[];
  members: MemberSummary[];
  permissions: { activeRoomCanManage: boolean };
  savedConnection: {
    serverIp: string;
    serverPort: number;
    username: string;
    userCode: string;
    caFile: string;
  };
};

export type BridgeCommand = {
  id: string;
  type: string;
  payload: Record<string, unknown>;
};

export type CommandResult = {
  id: string;
  ok: boolean;
  error?: BridgeError;
};

export interface ChatBridgeClient {
  currentStateJson(): string;
  dispatch(command: BridgeCommand): void;
  subscribe(listener: (state: BridgeState) => void): () => void;
  subscribeCommandResult(listener: (result: CommandResult) => void): () => void;
  subscribeBridgeError(listener: (error: BridgeError) => void): () => void;
}
