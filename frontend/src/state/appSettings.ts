import { useEffect, useState } from 'react';

export type ThemePreference = 'system' | 'light' | 'dark';

export type AppSettings = {
  showSendTime: boolean;
  theme: ThemePreference;
};

const STORAGE_KEY = 'lan-chat.settings';
const SETTINGS_EVENT = 'lan-chat-settings-changed';
const defaults: AppSettings = { showSendTime: true, theme: 'system' };
const themes: ThemePreference[] = ['system', 'light', 'dark'];

export function getAppSettings(): AppSettings {
  try {
    const saved = window.localStorage.getItem(STORAGE_KEY);
    if (!saved) {
      const showSendTime = window.localStorage.getItem('lan-chat.showSendTime');
      return showSendTime === null ? defaults : { ...defaults, showSendTime: showSendTime !== 'false' };
    }
    const parsed = JSON.parse(saved) as Partial<AppSettings>;
    const merged = { ...defaults, ...parsed };
    return themes.includes(merged.theme) ? merged : { ...merged, theme: defaults.theme };
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

/** Resolves the theme preference and keeps `<html data-theme>` in sync, following the OS when set to "system". */
export function useResolvedTheme(preference: ThemePreference) {
  useEffect(() => {
    const root = document.documentElement;
    const media = typeof window.matchMedia === 'function' ? window.matchMedia('(prefers-color-scheme: dark)') : null;
    const apply = () => {
      root.dataset.theme = preference === 'system' ? (media?.matches ? 'dark' : 'light') : preference;
    };
    apply();
    if (preference !== 'system' || !media) return undefined;
    media.addEventListener?.('change', apply);
    return () => media.removeEventListener?.('change', apply);
  }, [preference]);
}
