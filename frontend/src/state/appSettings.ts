import { useEffect, useMemo, useRef, useState } from 'react';

export type ThemePreference = 'system' | 'light' | 'dark';
export type AccentPreset = 'blue' | 'green' | 'violet' | 'orange' | 'rose';
export type SizePreference = 'small' | 'medium' | 'large';

export type AppSettings = {
  showSendTime: boolean;
  theme: ThemePreference;
  accent: AccentPreset;
  messageSize: SizePreference;
  bubbleRadius: SizePreference;
};

const STORAGE_KEY = 'lan-chat.settings';
const SETTINGS_EVENT = 'lan-chat-settings-changed';
const defaults: AppSettings = { showSendTime: true, theme: 'system', accent: 'blue', messageSize: 'medium', bubbleRadius: 'medium' };
const themes: ThemePreference[] = ['system', 'light', 'dark'];
export const accentPresets: AccentPreset[] = ['blue', 'green', 'violet', 'orange', 'rose'];
const sizes: SizePreference[] = ['small', 'medium', 'large'];

/** Drops unknown values from storage so a hand-edited or older entry cannot break the UI. */
function sanitize(settings: AppSettings): AppSettings {
  return {
    showSendTime: settings.showSendTime !== false,
    theme: themes.includes(settings.theme) ? settings.theme : defaults.theme,
    accent: accentPresets.includes(settings.accent) ? settings.accent : defaults.accent,
    messageSize: sizes.includes(settings.messageSize) ? settings.messageSize : defaults.messageSize,
    bubbleRadius: sizes.includes(settings.bubbleRadius) ? settings.bubbleRadius : defaults.bubbleRadius
  };
}

export function getAppSettings(): AppSettings {
  try {
    const saved = window.localStorage.getItem(STORAGE_KEY);
    if (!saved) {
      const showSendTime = window.localStorage.getItem('lan-chat.showSendTime');
      return showSendTime === null ? defaults : { ...defaults, showSendTime: showSendTime !== 'false' };
    }
    return sanitize({ ...defaults, ...(JSON.parse(saved) as Partial<AppSettings>) });
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

/** Mirrors the appearance settings onto `<html>` so tokens.css can switch presets. */
export function useAppearanceAttributes(settings: AppSettings) {
  useEffect(() => {
    const root = document.documentElement;
    root.dataset.accent = settings.accent;
    root.dataset.messageSize = settings.messageSize;
    root.dataset.bubbleRadius = settings.bubbleRadius;
  }, [settings.accent, settings.messageSize, settings.bubbleRadius]);
}

/**
 * Resolves the theme preference, keeps `<html data-theme>` in sync (following the OS when set
 * to "system") and returns the resolved theme.
 */
export function useResolvedTheme(preference: ThemePreference): 'dark' | 'light' {
  const media = useMemo(() => (typeof window.matchMedia === 'function' ? window.matchMedia('(prefers-color-scheme: dark)') : null), []);
  const [systemDark, setSystemDark] = useState(() => media?.matches ?? true);
  const firstRunRef = useRef(true);

  useEffect(() => {
    if (!media) return undefined;
    const update = () => setSystemDark(media.matches);
    update();
    media.addEventListener?.('change', update);
    return () => media.removeEventListener?.('change', update);
  }, [media]);

  const resolved = preference === 'system' ? (systemDark ? 'dark' : 'light') : preference;

  useEffect(() => {
    const root = document.documentElement;
    // Cross-fade colours only when the user actually switches theme, never on first paint.
    if (!firstRunRef.current) {
      root.classList.add('theme-transition');
      window.setTimeout(() => root.classList.remove('theme-transition'), 300);
    }
    firstRunRef.current = false;
    root.dataset.theme = resolved;
  }, [resolved]);

  return resolved;
}
