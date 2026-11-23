import { useCallback, useEffect, useLayoutEffect, useState } from 'react';
import { MotionConfig } from 'framer-motion';

import type { ChatBridgeClient } from '../bridge/types';
import { useBridgeState } from '../state/useBridgeState';
import { WorkspacePage } from './WorkspacePage';
import { SettingsPage } from './SettingsPage';
import { LocalHostPage, ModeSelectionPage, OnlineAccountPage, RemoteConnectionPage, type ConnectionMode } from './ConnectPages';
import { useFrameTelemetry } from '../performance/useFrameTelemetry';
import { useAppearanceAttributes, useAppSettings, useResolvedTheme } from '../state/appSettings';
import { setMotionScale } from '../lib/motion';
import { TooltipProvider } from '../components/ui/tooltip';
import { Toaster, toast } from '../components/ui/toast';
import { ReleaseNotesCard } from '../changelog/ReleaseNotesCard';
import { currentRelease, hasSeenRelease, markReleaseSeen } from '../changelog/releaseNotes';
import '../styles/global.css';

type AppProps = { bridge: ChatBridgeClient };

export function App({ bridge }: AppProps) {
  const state = useBridgeState(bridge);
  useFrameTelemetry(bridge);
  const settings = useAppSettings();
  const theme = useResolvedTheme(settings.theme);
  useAppearanceAttributes(settings);
  const [page, setPage] = useState(state.navigation.page);
  const [connectionMode, setConnectionMode] = useState<ConnectionMode>('remote');
  const hostAvailable = state.hostDefaults?.available !== false;
  const hostUnavailableReason = state.hostDefaults?.unavailableReason || '此安装包不包含本地服务端';

  const effectsEnabled = state.performance?.effectsEnabled !== false;
  const [releaseOpen, setReleaseOpen] = useState(false);
  const closeRelease = useCallback(() => setReleaseOpen(false), []);

  useEffect(() => bridge.subscribeBridgeError((error) => {
    toast.error(error.message);
  }), [bridge]);

  useEffect(() => bridge.subscribePresence((message) => {
    toast(message);
  }), [bridge]);

  // First launch of a new version: a non-blocking toast offers the release notes instead of a modal.
  useEffect(() => {
    if (hasSeenRelease()) return;
    markReleaseSeen();
    toast(`已更新到 LAN Chat ${currentRelease.version}`, {
      id: `release-${currentRelease.version}`, description: currentRelease.headline, duration: 8000,
      action: { label: '查看', onClick: () => setReleaseOpen(true) }
    });
  }, []);

  useLayoutEffect(() => {
    document.documentElement.dataset.effects = effectsEnabled ? 'on' : 'off';
    setMotionScale(state.performance?.animationDurationScale);
  }, [effectsEnabled, state.performance?.animationDurationScale]);

  // A freshly connected session opens the workspace, unless the user is in settings.
  const [phase, setPhase] = useState<string | null>(null);
  if (phase !== state.connection.phase) {
    setPhase(state.connection.phase);
    if (state.connection.phase === 'connected' && page !== 'settings') setPage('workspace');
  }

  let content;
  let view = page;
  if (page === 'connect') {
    content = <RemoteConnectionPage bridge={bridge} state={state} mode={connectionMode} onBack={() => setPage('mode')} />;
  } else if (page === 'host') {
    content = <LocalHostPage bridge={bridge} state={state} onBack={() => setPage('mode')} />;
  } else if (page === 'settings') {
    content = <SettingsPage bridge={bridge} state={state} onBack={() => setPage('workspace')} onShowReleaseNotes={() => setReleaseOpen(true)} />;
  } else if (page === 'workspace' && (state.connection.phase === 'connected' || state.connection.phase === 'reconnecting')) {
    content = <WorkspacePage bridge={bridge} state={state} theme={theme} onSettings={() => setPage('settings')} onReconnect={() => setPage('mode')} />;
  } else {
    view = 'mode';
    content = state.edition === 'online' ? <OnlineAccountPage bridge={bridge} state={state} /> : (
      <ModeSelectionPage
        lanOnly={state.edition === 'lan'}
        onRemote={() => { setConnectionMode('remote'); setPage('connect'); }}
        onGuest={() => { setConnectionMode('guest'); setPage('connect'); }}
        onTunnel={() => { setConnectionMode('tunnel'); setPage('connect'); }}
        onLocalHost={() => setPage('host')}
        hostAvailable={hostAvailable}
        hostUnavailableReason={hostUnavailableReason}
      />
    );
  }

  return (
    <MotionConfig reducedMotion={effectsEnabled ? 'never' : 'always'}>
      <TooltipProvider>
        <div className="app-atmosphere h-full w-full">
          <div key={view} className="view-enter h-full w-full">{content}</div>
        </div>
        <ReleaseNotesCard open={releaseOpen} onClose={closeRelease} />
        <Toaster />
      </TooltipProvider>
    </MotionConfig>
  );
}
