import { installDesktopRuntime } from '../app/runtimeBridge';
import type { AttachmentEvent, BridgeCommand, BridgeError, BridgeState, ChatBridgeClient, CommandResult } from './types';

export class FakeChatBridge implements ChatBridgeClient {
  private state: BridgeState;
  private readonly listeners = new Set<(state: BridgeState) => void>();
  private readonly resultListeners = new Set<(result: CommandResult) => void>();
  private readonly errorListeners = new Set<(error: BridgeError) => void>();
  private readonly attachmentListeners = new Set<(event: AttachmentEvent) => void>();
  private readonly presenceListeners = new Set<(message: string) => void>();
  readonly commands: BridgeCommand[] = [];

  constructor(initialState: BridgeState) {
    this.state = initialState;
  }

  currentStateJson(): string {
    return JSON.stringify(this.state);
  }

  dispatch(command: BridgeCommand): void {
    this.commands.push(command);
    queueMicrotask(() => this.resultListeners.forEach((listener) => listener({ id: command.id, ok: true })));
  }

  subscribe(listener: (state: BridgeState) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  subscribeCommandResult(listener: (result: CommandResult) => void): () => void {
    this.resultListeners.add(listener);
    return () => this.resultListeners.delete(listener);
  }

  subscribeAttachmentEvents(listener: (event: AttachmentEvent) => void): () => void {
    this.attachmentListeners.add(listener);
    return () => this.attachmentListeners.delete(listener);
  }

  subscribeBridgeError(listener: (error: BridgeError) => void): () => void {
    this.errorListeners.add(listener);
    return () => this.errorListeners.delete(listener);
  }

  subscribePresence(listener: (message: string) => void): () => void {
    this.presenceListeners.add(listener);
    return () => this.presenceListeners.delete(listener);
  }

  publishPresence(message: string): void {
    this.presenceListeners.forEach((listener) => listener(message));
  }

  publish(state: BridgeState): void {
    this.state = state;
    this.listeners.forEach((listener) => listener(state));
  }

  publishAttachmentEvent(event: AttachmentEvent): void {
    this.attachmentListeners.forEach((listener) => listener(event));
  }
}

export function createFakeBridge(initialState: BridgeState): FakeChatBridge {
  return new FakeChatBridge(initialState);
}

export function hasDesktopBridge(): boolean {
  return Boolean(window.__TAURI__?.core.invoke && window.__TAURI__?.event.listen);
}

export async function createDesktopBridge(): Promise<ChatBridgeClient> {
  const tauri = window.__TAURI__;
  if (!tauri) throw new Error('桌面服务未连接');

  const stateListeners = new Set<(state: BridgeState) => void>();
  const resultListeners = new Set<(result: CommandResult) => void>();
  const attachmentListeners = new Set<(event: AttachmentEvent) => void>();
  const errorListeners = new Set<(error: BridgeError) => void>();
  const presenceListeners = new Set<(message: string) => void>();
  const subscribe = <T,>(listeners: Set<(value: T) => void>, listener: (value: T) => void) => {
    listeners.add(listener);
    return () => { listeners.delete(listener); };
  };

  let state: BridgeState | undefined;
  const register = <T,>(name: string, handler: (event: T) => void) =>
    tauri.event.listen<T>(name, ({ payload }) => handler(payload));
  const registrations = await Promise.allSettled([
    register<BridgeState>('chat:state', (payload) => {
      const next = validateState(payload);
      state = next;
      stateListeners.forEach((listener) => listener(next));
    }),
    register<CommandResult>('chat:result', (payload) => resultListeners.forEach((listener) => listener(payload))),
    register<AttachmentEvent>('chat:attachment', (payload) => attachmentListeners.forEach((listener) => listener(payload))),
    register<BridgeError>('chat:error', (payload) => errorListeners.forEach((listener) => listener(payload))),
    register<string>('chat:presence', (payload) => {
      if (typeof payload !== 'string') throw new Error('chat:presence 应为字符串');
      presenceListeners.forEach((listener) => listener(payload));
    })
  ]);
  try {
    for (const registration of registrations) {
      if (registration.status === 'rejected') throw registration.reason;
    }
    const initialState = validateState(await tauri.core.invoke<BridgeState>('chat_state'));
    state ??= initialState;
  } catch (error) {
    for (const registration of registrations) {
      if (registration.status === 'fulfilled') registration.value();
    }
    throw error;
  }
  installDesktopRuntime(tauri);

  return {
    currentStateJson: () => JSON.stringify(state),
    dispatch: (command) => {
      void tauri.core.invoke('chat_dispatch', { json: JSON.stringify(command) }).catch((error: unknown) => {
        const failure: BridgeError = { code: 'desktop_dispatch', message: String(error), retryable: false, source: 'bridge', commandId: command.id };
        resultListeners.forEach((listener) => listener({ id: command.id, ok: false, error: failure }));
      });
    },
    subscribe: (listener) => subscribe(stateListeners, listener),
    subscribeCommandResult: (listener) => subscribe(resultListeners, listener),
    subscribeAttachmentEvents: (listener) => subscribe(attachmentListeners, listener),
    subscribeBridgeError: (listener) => subscribe(errorListeners, listener),
    subscribePresence: (listener) => subscribe(presenceListeners, listener)
  };
}

export function parseBridgeState(json: string): BridgeState {
  return validateState(JSON.parse(json) as BridgeState);
}

function validateState(state: BridgeState): BridgeState {
  if (state.schemaVersion !== 1) {
    throw new Error('不支持的 bridge 状态版本');
  }
  return state;
}

export function createCommand(type: string, payload: Record<string, unknown>): BridgeCommand {
  return { id: `web-${Date.now()}-${Math.random().toString(16).slice(2)}`, type, payload };
}
