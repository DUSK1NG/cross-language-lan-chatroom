import type { BridgeCommand, BridgeError, BridgeState, ChatBridgeClient, CommandResult } from './types';

export type QtBridgeProxy = {
  currentStateJson(callback: (json: string) => void): void | string;
  dispatch(commandJson: string): void;
  stateChanged: { connect(listener: (json: string) => void): void };
  commandResult: { connect(listener: (json: string) => void): void };
  bridgeError: { connect(listener: (json: string) => void): void };
};

type QtWebChannelConstructor = new (transport: unknown, callback: (channel: { objects: Record<string, QtBridgeProxy> }) => void) => unknown;

declare global {
  interface Window {
    QWebChannel?: QtWebChannelConstructor;
    qt?: { webChannelTransport?: unknown };
  }
}

export class FakeChatBridge implements ChatBridgeClient {
  private state: BridgeState;
  private readonly listeners = new Set<(state: BridgeState) => void>();
  private readonly resultListeners = new Set<(result: CommandResult) => void>();
  private readonly errorListeners = new Set<(error: BridgeError) => void>();
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

  subscribeBridgeError(listener: (error: BridgeError) => void): () => void {
    this.errorListeners.add(listener);
    return () => this.errorListeners.delete(listener);
  }

  publish(state: BridgeState): void {
    this.state = state;
    this.listeners.forEach((listener) => listener(state));
  }
}

export function createFakeBridge(initialState: BridgeState): FakeChatBridge {
  return new FakeChatBridge(initialState);
}

function readCurrentState(proxy: QtBridgeProxy): Promise<string> {
  return new Promise((resolve, reject) => {
    let settled = false;
    const finish = (json: string) => {
      if (!settled) {
        settled = true;
        resolve(json);
      }
    };
    try {
      const result = proxy.currentStateJson(finish);
      if (typeof result === 'string') finish(result);
    } catch (error) {
      if (!settled) {
        settled = true;
        reject(error);
      }
    }
  });
}

class QtChatBridge implements ChatBridgeClient {
  private readonly listeners = new Set<(state: BridgeState) => void>();
  private readonly resultListeners = new Set<(result: CommandResult) => void>();
  private readonly errorListeners = new Set<(error: BridgeError) => void>();

  constructor(private readonly proxy: QtBridgeProxy, private state: BridgeState) {
    proxy.stateChanged.connect((json) => {
      const nextState = parseBridgeState(json);
      this.state = nextState;
      this.listeners.forEach((listener) => listener(nextState));
    });
    proxy.commandResult.connect((json) => {
      const result = JSON.parse(json) as CommandResult;
      this.resultListeners.forEach((listener) => listener(result));
    });
    proxy.bridgeError.connect((json) => {
      const error = JSON.parse(json) as BridgeError;
      this.errorListeners.forEach((listener) => listener(error));
    });
  }

  currentStateJson(): string {
    return JSON.stringify(this.state);
  }

  dispatch(command: BridgeCommand): void {
    this.proxy.dispatch(JSON.stringify(command));
  }

  subscribe(listener: (state: BridgeState) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  subscribeCommandResult(listener: (result: CommandResult) => void): () => void {
    this.resultListeners.add(listener);
    return () => this.resultListeners.delete(listener);
  }

  subscribeBridgeError(listener: (error: BridgeError) => void): () => void {
    this.errorListeners.add(listener);
    return () => this.errorListeners.delete(listener);
  }
}

export async function createQtBridge(proxy: QtBridgeProxy): Promise<ChatBridgeClient> {
  const state = parseBridgeState(await readCurrentState(proxy));
  return new QtChatBridge(proxy, state);
}

export async function createWebChannelBridge(): Promise<ChatBridgeClient> {
  const transport = window.qt?.webChannelTransport;
  const WebChannel = window.QWebChannel;
  if (!transport || !WebChannel) {
    throw new Error('QWebChannel transport is unavailable');
  }

  return new Promise((resolve, reject) => {
    new WebChannel(transport, (channel) => {
      const proxy = channel.objects?.chatBridge;
      if (!proxy) {
        reject(new Error('chatBridge object is unavailable'));
        return;
      }
      void createQtBridge(proxy).then(resolve, reject);
    });
  });
}

export function parseBridgeState(json: string): BridgeState {
  const parsed = JSON.parse(json) as BridgeState;
  if (parsed.schemaVersion !== 1) {
    throw new Error('不支持的 bridge 状态版本');
  }
  return parsed;
}

export function createCommand(type: string, payload: Record<string, unknown>): BridgeCommand {
  return { id: `web-${Date.now()}-${Math.random().toString(16).slice(2)}`, type, payload };
}
