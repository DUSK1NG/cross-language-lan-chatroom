export type ConnectionPhase = 'idle' | 'connecting' | 'connected' | 'reconnecting' | 'error';

export type ConversationRef =
  | { kind: 'room'; id: string; title: string; mls?: boolean }
  | { kind: 'dm'; id: string; title: string; userCode: string; mls?: boolean };

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
  mls?: boolean;
};

export type DirectMessageSummary = {
  displayName: string;
  userCode: string;
  unreadCount: number;
  mls?: boolean;
};

export type MessageAttachment = {
  attachmentId: string;
  fileName: string;
  logicalSize: number;
  status?: 'available' | 'downloading' | 'verified-failed' | 'expired';
  receivedChunks?: number;
  totalChunks?: number;
};

export type MessageItem = {
  messageId: string;
  displayName: string;
  userCode: string;
  time: string;
  content: string;
  selfMessage: boolean;
  systemMessage: boolean;
  deliveryState?: 'queued' | 'sent' | 'delivered' | 'failed';
  attachment?: MessageAttachment;
};

export type MemberSummary = {
  displayName: string;
  userCode: string;
  online: boolean;
  admin: boolean;
};

export type ConnectionApprovalRequest = {
  id: string;
  displayName: string;
  userCode: string;
  requestedAt: string;
};

export type HostDefaults = {
  serverExe: string;
  certFile: string;
  keyFile: string;
  dbFile: string;
  available?: boolean;
  unavailableReason?: string;
};

export type LanDiscoveredHost = {
  id: string;
  hostName: string;
  serverIp: string;
  serverPort: number;
  fingerprintSha256: string;
  known: boolean;
};

export type LanDiscoveryState = {
  scanning: boolean;
  hosts: LanDiscoveredHost[];
};

export type PerformanceState = {
  mode: 'Automatic' | 'High' | 'Balanced' | 'Power Saving';
  effectiveMode: string;
  effectsEnabled: boolean;
  animationsEnabled: boolean;
  gradientsEnabled: boolean;
  animationDurationScale: number;
  observedFrameCount: number;
  observedFps: number;
  observedP95FrameMs: number;
  observedMaxFrameMs: number;
  automaticReason: string;
};

export type GraphicsState = {
  graphicsApi: string;
  renderer: string;
  vendor: string;
  hardwareAcceleration: boolean;
  softwareRendering: boolean;
  refreshRate: number;
  dpi: number;
  resolution: string;
};

export type DiagnosticsState = {
  enabled: boolean;
  directory: string;
};

export type BridgeState = {
  schemaVersion: 1;
  connection: {
    phase: ConnectionPhase;
    statusText: string;
    retryable: boolean;
    reconnectAttempt?: number;
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
  connectionApprovals?: ConnectionApprovalRequest[];
  savedConnection: {
    serverIp: string;
    serverPort: number;
    username: string;
    userCode: string;
    caFile: string;
  };
  lanDiscovery?: LanDiscoveryState;
  hostDefaults?: HostDefaults;
  performance?: PerformanceState;
  graphics?: GraphicsState;
  diagnostics?: DiagnosticsState;
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

export type AttachmentEventPayload =
  | { type: 'attachment.init'; attachmentId: string; uploadId: string;
      chunkSize: number; chunkIndex: number; receivedIndexes: number[];
      expiresAt: string; content: string;
      // Plaintext bytes from the native transfer; optional for older bridge events.
      logicalSize?: number }
  | { type: 'attachment.chunk'; attachmentId: string; chunkIndex: number;
      content: string }
  | { type: 'attachment.resume'; attachmentId: string;
      receivedIndexes: number[] }
  | { type: 'attachment.commit'; attachmentId: string; content: string }
  | { type: 'manifest'; attachmentId: string; chunkSize: number;
      content: string }
  | { type: 'error'; code: string; message: string };

export type AttachmentEvent = {
  type: 'attachment.event';
  id: string;
  payload: AttachmentEventPayload;
};

export interface ChatBridgeClient {
  currentStateJson(): string;
  dispatch(command: BridgeCommand): void;
  subscribe(listener: (state: BridgeState) => void): () => void;
  subscribeCommandResult(listener: (result: CommandResult) => void): () => void;
  subscribeAttachmentEvents(listener: (event: AttachmentEvent) => void): () => void;
  subscribeBridgeError(listener: (error: BridgeError) => void): () => void;
}
