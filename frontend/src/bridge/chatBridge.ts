import type { BridgeCommand, BridgeState, ChatBridgeClient } from './types';

export class FakeChatBridge implements ChatBridgeClient {
  private state: BridgeState;
  private readonly listeners = new Set<(state: BridgeState) => void>();
  readonly commands: BridgeCommand[] = [];

  constructor(initialState: BridgeState) {
    this.state = initialState;
  }

  currentStateJson(): string {
    return JSON.stringify(this.state);
  }

  dispatch(command: BridgeCommand): void {
    this.commands.push(command);
  }

  subscribe(listener: (state: BridgeState) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  publish(state: BridgeState): void {
    this.state = state;
    this.listeners.forEach((listener) => listener(state));
  }
}

export function createFakeBridge(initialState: BridgeState): FakeChatBridge {
  return new FakeChatBridge(initialState);
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
