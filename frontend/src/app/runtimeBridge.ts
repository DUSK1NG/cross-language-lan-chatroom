import type { ChatBridgeClient } from '../bridge/types';

export type RuntimeBridgeResolution = {
  bridge?: ChatBridgeClient;
  error?: string;
};

export async function resolveRuntimeBridge(options: {
  hasTransport: boolean;
  isDevelopment: boolean;
  createBridge(): Promise<ChatBridgeClient>;
  createPreviewBridge(): ChatBridgeClient;
}): Promise<RuntimeBridgeResolution> {
  if (!options.hasTransport) {
    return options.isDevelopment
      ? { bridge: options.createPreviewBridge() }
      : { error: 'QWebChannel transport is unavailable' };
  }

  try {
    return { bridge: await options.createBridge() };
  } catch (error) {
    const message = error instanceof Error ? error.message : String(error);
    return options.isDevelopment
      ? { bridge: options.createPreviewBridge() }
      : { error: message || 'Unable to connect to ChatBridge' };
  }
}
