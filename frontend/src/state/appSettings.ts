import { useEffect, useState } from 'react';

export type AppSettings = {
  showSendTime: boolean;
};

const STORAGE_KEY = 'lan-chat.settings';
const SETTINGS_EVENT = 'lan-chat-settings-changed';
const defaults: AppSettings = { showSendTime: true };

export function getAppSettings(): AppSettings {
  try {
    const saved = window.localStorage.getItem(STORAGE_KEY);
    if (!saved) {
      const showSendTime = window.localStorage.getItem('lan-chat.showSendTime');
      return showSendTime === null ? defaults : { ...defaults, showSendTime: showSendTime !== 'false' };
    }
    const parsed = JSON.parse(saved) as Partial<AppSettings>;
    return { ...defaults, ...parsed };
  } catch {
    return defaults;
  }
}

export function saveAppSettings(settings: AppSettings) {
  window.localStorage.setItem(STORAGE_KEY, JSON.stringify(settings));
  window.localStorage.setItem('lan-chat.showSendTime', String(settings.showSendTime));
  window.dispatchEvent(new CustomEvent(SETTINGS_EVENT));
}

export function useAppSettings() {
  const [settings, setSettings] = useState<AppSettings>(() => getAppSettings());

  useEffect(() => {
    const refresh = () => setSettings(getAppSettings());
    window.addEventListener(SETTINGS_EVENT, refresh);
    window.addEventListener('storage', refresh);
    return () => {
      window.removeEventListener(SETTINGS_EVENT, refresh);
      window.removeEventListener('storage', refresh);
    };
  }, []);

  return settings;
}
