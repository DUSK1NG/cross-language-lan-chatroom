import type { ChatBridgeClient } from '../bridge/types';

type Unlisten = () => void;
type Tauri = {
  core: { invoke<T>(command: string, args?: Record<string, unknown>): Promise<T> };
  event: { listen<T>(name: string, callback: (event: { payload: T }) => void): Promise<Unlisten> };
};

type Drop = { paths: string[]; position: { x: number; y: number } };
type Runtime = {
  OnFileDrop(callback: (x: number, y: number, paths: string[]) => void, useDropTarget: boolean): void;
  OnFileDropOff(): void;
  BrowserOpenURL(url: string): void;
};

declare global {
  interface Window {
    __TAURI__?: Tauri;
    runtime?: Runtime;
  }
}

export function installDesktopRuntime(tauri: Tauri): void {
  let drop: Promise<Unlisten> | undefined;
  const stop = () => {
    const subscription = drop;
    drop = undefined;
    void subscription?.then((unlisten) => unlisten());
  };
  window.runtime = {
    OnFileDrop: (callback, useDropTarget) => {
      stop();
      const active = tauri.event.listen<Drop>('tauri://drag-drop', ({ payload }) => {
        if (drop !== active) return;
        const { x, y } = payload.position;
        const scale = window.devicePixelRatio;
        const target = document.elementFromPoint(x / scale, y / scale);
        if (!useDropTarget || target?.closest('[data-file-drop]')) callback(x, y, payload.paths);
      });
      drop = active;
    },
    OnFileDropOff: stop,
    BrowserOpenURL: (url) => { void tauri.core.invoke('chat_open_url', { url }); }
  };
}

export type DesktopUpdate = {
  version: string;
  notes: string | null;
  date: string | null;
  size: number;
  installed: boolean;
  portableUrl: string;
};

export function desktopUpdateRuntime() {
  const tauri = window.__TAURI__;
  if (!tauri) return undefined;
  return {
    check: () => tauri.core.invoke<DesktopUpdate | null>('check_desktop_update'),
    download: (version: string) => tauri.core.invoke<void>('download_desktop_update', { version }),
    install: (version: string) => tauri.core.invoke<void>('install_desktop_update', { version }),
    progress: (update: (received: number) => void) => tauri.event.listen<number>('desktop-update:progress', ({ payload }) => update(payload)),
    openZip: (url: string) => tauri.core.invoke<void>('chat_open_url', { url })
  };
}

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
      : { error: '桌面服务未连接' };
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
