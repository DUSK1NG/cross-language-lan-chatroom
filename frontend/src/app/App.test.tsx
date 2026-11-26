import { act, cleanup, fireEvent, render, screen, waitFor, within } from '@testing-library/react';
import { afterEach, describe, expect, it, vi } from 'vitest';

import { createFakeBridge } from '../bridge/chatBridge';
import type { BridgeState, CommandResult } from '../bridge/types';
import { App } from './App';

const disconnectedState: BridgeState = {
  schemaVersion: 1,
  connection: { phase: 'idle', statusText: '未连接', retryable: false },
  identity: { displayName: 'Alice', userCode: 'A001', admin: false },
  navigation: { page: 'mode', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } },
  rooms: [], directMessages: [], activeMessages: [], members: [],
  permissions: { activeRoomCanManage: false },
  savedConnection: { serverIp: '127.0.0.1', serverPort: 8888, username: 'Alice', userCode: 'A001', caFile: '' },
  hostDefaults: {
    available: true,
    unavailableReason: ''
  }
};

describe('App', () => {
  afterEach(cleanup);

  it('registers an online account with the selected server and user code', () => {
    const bridge = createFakeBridge({ ...disconnectedState, edition: 'online' });
    render(<App bridge={bridge} />);
    expect(screen.queryByRole('button', { name: 'local-host-mode' })).not.toBeInTheDocument();
    expect(screen.getByLabelText('密码')).toHaveAttribute('autocomplete', 'current-password');
    fireEvent.click(screen.getByRole('button', { name: '注册账号' }));
    fireEvent.click(screen.getByRole('button', { name: '更改服务器' }));
    fireEvent.change(screen.getByLabelText('服务器地址'), { target: { value: 'chat.example.com' } });
    fireEvent.change(screen.getByLabelText('用户代码'), { target: { value: 'A001' } });
    fireEvent.change(screen.getByLabelText('密码'), { target: { value: 'test-password' } });
    expect(screen.getByLabelText('密码')).toHaveAttribute('autocomplete', 'new-password');
    fireEvent.click(screen.getByRole('button', { name: '注册并登录' }));
    expect(bridge.commands.at(-1)).toMatchObject({ type: 'session.connectOnline', payload: {
      serverIp: 'chat.example.com', serverPort: 8888, username: 'Alice', userCode: 'A001', password: 'test-password', register: true
    } });
  });

  it('shows the saved server instead of asking for it again', () => {
    const state: BridgeState = {
      ...disconnectedState, edition: 'online',
      savedConnection: { ...disconnectedState.savedConnection, serverIp: '1.15.129.115' }
    };
    render(<App bridge={createFakeBridge(state)} />);

    expect(screen.queryByLabelText('服务器地址')).not.toBeInTheDocument();
    expect(screen.getByText('1.15.129.115:8888')).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: '更改服务器' }));
    expect(screen.getByLabelText('服务器地址')).toHaveValue('1.15.129.115');
  });

  it('explains why a short online password cannot be submitted', () => {
    render(<App bridge={createFakeBridge({ ...disconnectedState, edition: 'online' })} />);

    fireEvent.change(screen.getByLabelText('密码'), { target: { value: 'short' } });

    expect(screen.getByText('密码需 8–72 个 UTF-8 字节，当前为 5 字节。')).toBeInTheDocument();
    expect(screen.getByRole('button', { name: '登录' })).toBeDisabled();
  });

  it('keeps the draft when the settings page is opened and closed', () => {
    render(<App bridge={createFakeBridge({
      ...disconnectedState,
      connection: { phase: 'connected', statusText: '已连接', retryable: false },
      navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } }
    })} />);

    fireEvent.change(screen.getByLabelText('消息输入框'), { target: { value: 'half a thought' } });
    fireEvent.click(screen.getByRole('button', { name: '设置' }));
    fireEvent.click(screen.getByRole('button', { name: '返回聊天' }));

    expect(screen.getByLabelText('消息输入框')).toHaveValue('half a thought');
  });

  it('keeps a selected host through a scan round that does not report it', () => {
    vi.useFakeTimers();
    try {
      const host = {
        id: 'a'.repeat(64), hostName: 'Alice PC', serverIp: '192.168.8.23', serverPort: 8888,
        fingerprintSha256: 'a'.repeat(64), known: false
      };
      const bridge = createFakeBridge({ ...disconnectedState, lanDiscovery: { scanning: false, hosts: [host] } });
      render(<App bridge={bridge} />);
      fireEvent.click(screen.getByRole('button', { name: 'guest-mode' }));
      fireEvent.click(screen.getByRole('button', { name: 'select-lan-host-Alice PC' }));

      act(() => { bridge.publish({ ...disconnectedState, lanDiscovery: { scanning: true, hosts: [] } }); });

      expect(screen.getByText('暂时没有收到房主广播，正在重新搜索；核对中的选择会保留片刻。')).toBeInTheDocument();
      expect(screen.getByRole('button', { name: 'one-click-join' })).toBeInTheDocument();

      act(() => { vi.advanceTimersByTime(10_100); });

      expect(screen.queryByRole('button', { name: 'one-click-join' })).not.toBeInTheDocument();
      expect(screen.getByRole('button', { name: 'connect-session' })).toBeInTheDocument();
    } finally {
      vi.useRealTimers();
    }
  });

  it('displays room presence as a toast instead of a chat message', async () => {
    const bridge = createFakeBridge({
      ...disconnectedState,
      connection: { phase: 'connected', statusText: '已连接', retryable: false },
      navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } }
    });
    render(<App bridge={bridge} />);

    const message = 'Bob#B002 joined the chat';
    act(() => bridge.publishPresence(message));

    expect((await screen.findByText(message)).closest('[data-sonner-toast]')).not.toBeNull();
    expect(within(screen.getByRole('region', { name: '聊天' })).queryByText(message)).not.toBeInTheDocument();
  });

  it('renders all connection modes while disconnected', () => {
    render(<App bridge={createFakeBridge(disconnectedState)} />);

    expect(screen.getByRole('button', { name: 'remote-mode' })).toBeEnabled();
    expect(screen.getByRole('button', { name: 'local-host-mode' })).toBeEnabled();
    expect(screen.getByRole('button', { name: 'guest-mode' })).toBeEnabled();
  });

  it('disables local hosting when a member package has no bundled server', () => {
    const bridge = createFakeBridge({
      ...disconnectedState,
      hostDefaults: {
        ...disconnectedState.hostDefaults,
        available: false,
        unavailableReason: '成员端不包含本地服务端'
      } as unknown as BridgeState['hostDefaults']
    });
    render(<App bridge={bridge} />);

    expect(screen.getByRole('button', { name: 'local-host-mode' })).toBeDisabled();
    expect(screen.getByText('成员端不包含本地服务端')).toBeInTheDocument();
  });

  it('opens the guest form with a guest identity and dispatches a remote connection', () => {
    const bridge = createFakeBridge(disconnectedState);
    render(<App bridge={bridge} />);
    fireEvent.click(screen.getByRole('button', { name: 'guest-mode' }));

    expect(screen.getByDisplayValue('Bob')).toBeInTheDocument();
    expect(screen.getByDisplayValue('B001')).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: 'connect-session' }));

    expect(bridge.commands.at(-1)).toMatchObject({
      type: 'session.connectRemote',
      payload: { serverIp: '127.0.0.1', serverPort: 8888, username: 'Bob', userCode: 'B001' }
    });
  });

  it('opens a TCP tunnel profile without exposing tunnel secrets', () => {
    const certificateFile = 'C:/chat/certs/host.crt';
    const bridge = createFakeBridge({ ...disconnectedState, savedConnection: { ...disconnectedState.savedConnection, caFile: certificateFile } });
    render(<App bridge={bridge} />);
    fireEvent.click(screen.getByRole('button', { name: 'tunnel-mode' }));

    expect(screen.getByRole('heading', { name: '通过 TCP 隧道加入' })).toBeInTheDocument();
    expect(screen.getByLabelText('隧道地址')).toBeInTheDocument();
    expect(screen.getByLabelText('端口')).toBeInTheDocument();
    expect(screen.getByText('host.crt')).toBeInTheDocument();
    expect(screen.queryByDisplayValue(certificateFile)).not.toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: '选择公开证书' }));
    expect(bridge.commands.at(-1)).toMatchObject({ type: 'session.chooseCertificate', payload: {} });
    expect(screen.queryByLabelText(/令牌|Token|私钥/)).not.toBeInTheDocument();
  });

  it('uses localhost as the certificate identity for a TCP tunnel endpoint', () => {
    const bridge = createFakeBridge(disconnectedState);
    render(<App bridge={bridge} />);
    fireEvent.click(screen.getByRole('button', { name: 'tunnel-mode' }));
    fireEvent.change(screen.getByLabelText('隧道地址'), { target: { value: 'frp-bus.com' } });
    fireEvent.change(screen.getByLabelText('端口'), { target: { value: '50440' } });
    fireEvent.click(screen.getByRole('button', { name: 'connect-session' }));

    expect(bridge.commands.at(-1)).toMatchObject({
      type: 'session.connectRemote',
      payload: { serverIp: 'frp-bus.com', serverPort: 50440, tlsServerName: 'localhost' }
    });
  });

  it('keeps automatically scanning for virtual-LAN hosts while the guest page is open', () => {
    vi.useFakeTimers();
    try {
      const bridge = createFakeBridge(disconnectedState);
      render(<App bridge={bridge} />);
      fireEvent.click(screen.getByRole('button', { name: 'guest-mode' }));

      expect(bridge.commands.filter((command) => command.type === 'session.discoverLanHosts')).toHaveLength(1);

      act(() => { vi.advanceTimersByTime(3000); });

      expect(bridge.commands.filter((command) => command.type === 'session.discoverLanHosts')).toHaveLength(2);
    } finally {
      vi.useRealTimers();
    }
  });

  it('identifies virtual-LAN discovery in the guest flow', () => {
    render(<App bridge={createFakeBridge(disconnectedState)} />);
    fireEvent.click(screen.getByRole('button', { name: 'guest-mode' }));

    expect(screen.getByText('自动发现同一局域网或虚拟局域网内的房主')).toBeInTheDocument();
  });

  it('joins a discovered LAN host without exposing an IP or certificate path', () => {
    const bridge = createFakeBridge({
      ...disconnectedState,
      lanDiscovery: {
        scanning: false,
        hosts: [{
          id: 'a'.repeat(64), hostName: 'Alice PC', serverIp: '192.168.8.23', serverPort: 8888,
          fingerprintSha256: 'a'.repeat(64), known: false
        }]
      }
    });
    render(<App bridge={bridge} />);
    fireEvent.click(screen.getByRole('button', { name: 'guest-mode' }));
    fireEvent.click(screen.getByRole('button', { name: 'select-lan-host-Alice PC' }));
    fireEvent.click(screen.getByRole('checkbox'));

    fireEvent.click(screen.getByRole('button', { name: 'one-click-join' }));

    expect(bridge.commands.at(-1)).toMatchObject({
      type: 'session.connectDiscoveredHost',
      payload: { hostId: 'a'.repeat(64), username: 'Bob', userCode: 'B001' }
    });
  });

  it('requires a password and submits only the three host credentials', () => {
    const bridge = createFakeBridge(disconnectedState);
    render(<App bridge={bridge} />);
    fireEvent.click(screen.getByRole('button', { name: 'local-host-mode' }));

    const password = screen.getByLabelText('密码');
    const form = password.closest('form')!;
    expect(form.querySelectorAll('input')).toHaveLength(3);
    expect(password).toHaveAttribute('type', 'password');
    expect(password).toBeRequired();

    const start = screen.getByRole('button', { name: 'start-local-host' });
    fireEvent.click(start);
    expect(bridge.commands.some((command) => command.type === 'session.connectLocalHost')).toBe(false);

    fireEvent.change(password, { target: { value: 'test-host-password' } });
    fireEvent.click(start);
    expect(bridge.commands.find((command) => command.type === 'session.connectLocalHost')?.payload).toEqual({
      username: 'Alice', userCode: 'A001', password: 'test-host-password'
    });
  });

  it('switches to the workspace when the bridge publishes a connected state', async () => {
    const bridge = createFakeBridge(disconnectedState);
    render(<App bridge={bridge} />);

    bridge.publish({
      ...disconnectedState,
      connection: { phase: 'connected', statusText: '已连接', retryable: false },
      navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } }
    });

    await waitFor(() => expect(screen.getByRole('heading', { name: '# lobby' })).toBeInTheDocument());
    expect(screen.getByTestId('identity-card')).toHaveTextContent('Alice #A001');

    fireEvent.click(screen.getByRole('button', { name: '设置' }));
    expect(screen.getByRole('heading', { name: '设置' })).toBeInTheDocument();
  });

  it('offers a system/light/dark theme switch that drives data-theme', async () => {
    localStorage.clear();
    document.documentElement.removeAttribute('data-theme');
    const bridge = createFakeBridge({
      ...disconnectedState,
      connection: { phase: 'connected', statusText: '已连接', retryable: false },
      navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } }
    });
    render(<App bridge={bridge} />);
    await waitFor(() => expect(screen.getByRole('button', { name: '设置' })).toBeInTheDocument());
    expect(document.documentElement.dataset.theme).toBe('light');
    fireEvent.click(screen.getByRole('button', { name: '设置' }));
    fireEvent.click(screen.getByRole('button', { name: '外观' }));

    const themes = screen.getByRole('radiogroup', { name: '外观主题' });
    expect(within(themes).getByRole('radio', { name: '跟随系统' })).toHaveAttribute('aria-checked', 'true');
    fireEvent.click(within(themes).getByRole('radio', { name: '深色' }));

    await waitFor(() => expect(document.documentElement.dataset.theme).toBe('dark'));
    expect(within(themes).getByRole('radio', { name: '深色' })).toHaveAttribute('aria-checked', 'true');
    expect(JSON.parse(localStorage.getItem('lan-chat.settings') ?? '{}').theme).toBe('dark');
  });

  it('persists the send-time setting from the settings page', async () => {
    localStorage.clear();
    const bridge = createFakeBridge({
      ...disconnectedState,
      connection: { phase: 'connected', statusText: '已连接', retryable: false },
      navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } }
    });
    render(<App bridge={bridge} />);
    await waitFor(() => expect(screen.getByRole('button', { name: '设置' })).toBeInTheDocument());
    fireEvent.click(screen.getByRole('button', { name: '设置' }));
    fireEvent.click(screen.getByRole('button', { name: '外观' }));
    const sendTime = screen.getByRole('switch', { name: '显示发送时间' });
    expect(sendTime).toHaveAttribute('aria-checked', 'true');
    fireEvent.click(sendTime);

    expect(localStorage.getItem('lan-chat.showSendTime')).toBe('false');
    expect(screen.getByRole('switch', { name: '显示发送时间' })).toHaveAttribute('aria-checked', 'false');
  });

  it('shows the LAN connection mode and endpoint in settings', async () => {
    const bridge = createFakeBridge({
      ...disconnectedState,
      connection: { phase: 'connected', statusText: '已连接', retryable: false },
      navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } }
    });
    render(<App bridge={bridge} />);
    await waitFor(() => expect(screen.getByRole('button', { name: '设置' })).toBeInTheDocument());
    fireEvent.click(screen.getByRole('button', { name: '设置' }));

    expect(screen.getByText('局域网')).toBeInTheDocument();
    expect(screen.getByText('127.0.0.1:8888')).toBeInTheDocument();
  });

  it('syncs the AstrBot OpenAPI key with the account and sends the model API key only to AstrBot', async () => {
    const bridge = createFakeBridge({
      ...disconnectedState,
      edition: 'online',
      connection: { phase: 'connected', statusText: '已连接', retryable: false },
      navigation: { page: 'workspace' }
    });
    const settings = { endpoint: 'https://bot.example.com', source_id: 'source', provider_id: 'model', persona_id: 'persona' };
    const listeners = new Set<(result: CommandResult) => void>();
    vi.spyOn(bridge, 'subscribeCommandResult').mockImplementation((listener) => {
      listeners.add(listener);
      return () => { listeners.delete(listener); };
    });
    const dispatch = vi.spyOn(bridge, 'dispatch').mockImplementation((command) => {
      queueMicrotask(() => listeners.forEach((listener) => listener({
        id: command.id, ok: true, botSettings: { revision: 3, settings, openapiKey: 'test-openapi-key' }
      })));
    });
    const invoke = vi.fn(async (_command: string, args: { operation: string }) => {
      if (args.operation === 'list') return ['default', 'persona', 'xiaojingyu'];
      return args.operation === 'read' ? { api_base: 'https://model.example.com/v1' } : null;
    });
    vi.stubGlobal('__TAURI__', { core: { invoke } });
    try {
      render(<App bridge={bridge} />);
      fireEvent.click(await screen.findByRole('button', { name: '设置' }));
      fireEvent.click(screen.getByRole('button', { name: '机器人' }));
      await screen.findByText('账号设置已同步');
      expect(screen.getByLabelText('AstrBot 地址')).toHaveValue(settings.endpoint);
      expect(screen.getByLabelText('AstrBot OpenAPI Key')).toHaveValue('test-openapi-key');
      const source = within(screen.getByRole('form', { name: '模型源' }));
      fireEvent.click(source.getByRole('button', { name: '读取配置' }));
      const apiKey = await source.findByLabelText('模型 API Key');
      expect(apiKey).toHaveValue('');
      fireEvent.change(apiKey, { target: { value: 'test-model-key' } });
      fireEvent.click(source.getByRole('button', { name: '保存到 AstrBot' }));
      await source.findByText('已保存到 AstrBot');
      expect(invoke).toHaveBeenLastCalledWith('astrbot_settings', expect.objectContaining({
        token: 'test-openapi-key', operation: 'save', fields: expect.objectContaining({ api_key: 'test-model-key' })
      }));
      fireEvent.click(screen.getByRole('button', { name: '保存到账号' }));
      expect(dispatch).toHaveBeenLastCalledWith(expect.objectContaining({
        type: 'botSettings.save', payload: { revision: 3, content: JSON.stringify(settings), openapiKey: 'test-openapi-key' }
      }));
      await screen.findByText('账号设置已同步');
      const persona = within(screen.getByRole('form', { name: '人格' }));
      const list = persona.getByRole('button', { name: '读取列表' });
      expect(persona.getByLabelText('人格编号')).toHaveValue('persona');
      fireEvent.keyDown(list, { key: 'ArrowDown' });
      const menu = within(await screen.findByRole('menu'));
      expect(await menu.findByRole('menuitem', { name: 'default' })).toBeVisible();
      expect(menu.getByRole('menuitem', { name: 'persona，当前编号' })).toBeVisible();
      fireEvent.click(menu.getByRole('menuitem', { name: 'xiaojingyu' }));
      expect(persona.getByLabelText('人格编号')).toHaveValue('xiaojingyu');
      fireEvent.change(screen.getByLabelText('AstrBot 地址'), { target: { value: 'https://other.example.com' } });
      expect(screen.getByLabelText('AstrBot OpenAPI Key')).toHaveValue('');
    } finally {
      vi.unstubAllGlobals();
    }
  });

  it('shows performance and graphics status and dispatches performance mode changes', async () => {
    const bridge = createFakeBridge({
      ...disconnectedState,
      connection: { phase: 'connected', statusText: '已连接', retryable: false },
      navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } },
      performance: {
        mode: 'Balanced', effectiveMode: 'Balanced', effectsEnabled: true,
        animationsEnabled: true, gradientsEnabled: true, animationDurationScale: 0.75,
        observedFrameCount: 24, observedFps: 60, observedP95FrameMs: 16.67,
        observedMaxFrameMs: 22, automaticReason: 'manual-selection'
      },
      graphics: {
        graphicsApi: 'Direct3D 11 / RHI', renderer: 'Unknown', vendor: 'Unknown',
        hardwareAcceleration: true, softwareRendering: false, refreshRate: 60,
        dpi: 96, resolution: '1920 × 1080'
      }
    });
    render(<App bridge={bridge} />);
    await waitFor(() => expect(screen.getByRole('button', { name: '设置' })).toBeInTheDocument());
    fireEvent.click(screen.getByRole('button', { name: '设置' }));
    fireEvent.click(screen.getByRole('button', { name: '外观' }));

    expect(screen.getByText('Direct3D 11 / RHI')).toBeInTheDocument();
    expect(screen.getByText('60.0 FPS')).toBeInTheDocument();
    const performanceMode = screen.getByRole('combobox', { name: '性能等级' });
    expect(performanceMode).toHaveTextContent('均衡');
    fireEvent.keyDown(performanceMode, { key: 'Enter' });
    fireEvent.click(await screen.findByRole('option', { name: '省电' }));

    expect(bridge.commands.at(-1)).toMatchObject({
      type: 'settings.setPerformanceMode',
      payload: { mode: 'Power Saving' }
    });
  });

  it('keeps the settings content in its own scroll container and dispatches connection log changes', async () => {
    const bridge = createFakeBridge({
      ...disconnectedState,
      connection: { phase: 'connected', statusText: '已连接', retryable: false },
      navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } },
      diagnostics: { enabled: true }
    } as BridgeState);
    render(<App bridge={bridge} />);
    await waitFor(() => expect(screen.getByRole('button', { name: '设置' })).toBeInTheDocument());
    fireEvent.click(screen.getByRole('button', { name: '设置' }));

    expect(screen.getByTestId('settings-scroll-container')).toBeInTheDocument();
    fireEvent.click(screen.getByRole('button', { name: '隐私与安全' }));
    expect(screen.queryByText('日志目录')).not.toBeInTheDocument();
    fireEvent.click(screen.getByRole('switch', { name: '记录连接日志' }));
    expect(bridge.commands.at(-1)).toMatchObject({
      type: 'settings.setConnectionLogging',
      payload: { enabled: false }
    });
  });
  it('applies accent, message size and bubble radius presets from the appearance settings', async () => {
    localStorage.clear();
    const bridge = createFakeBridge({
      ...disconnectedState,
      connection: { phase: 'connected', statusText: '已连接', retryable: false },
      navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } }
    });
    render(<App bridge={bridge} />);
    await waitFor(() => expect(document.documentElement.dataset.accent).toBe('orange'));
    fireEvent.click(screen.getByRole('button', { name: '设置' }));
    fireEvent.click(screen.getByRole('button', { name: '外观' }));

    fireEvent.click(within(screen.getByRole('radiogroup', { name: '强调色' })).getByRole('radio', { name: '紫' }));
    fireEvent.click(within(screen.getByRole('radiogroup', { name: '消息字号' })).getByRole('radio', { name: '大' }));
    fireEvent.click(within(screen.getByRole('radiogroup', { name: '气泡圆角' })).getByRole('radio', { name: '小圆角' }));

    await waitFor(() => expect(document.documentElement.dataset.accent).toBe('violet'));
    expect(document.documentElement.dataset.messageSize).toBe('large');
    expect(document.documentElement.dataset.bubbleRadius).toBe('small');
    expect(JSON.parse(localStorage.getItem('lan-chat.settings') ?? '{}')).toMatchObject({ accent: 'violet', messageSize: 'large', bubbleRadius: 'small' });
  });

  it('marks the notification settings as not yet available and returns to the chat', async () => {
    const bridge = createFakeBridge({
      ...disconnectedState,
      connection: { phase: 'connected', statusText: '已连接', retryable: false },
      navigation: { page: 'workspace', activeConversation: { kind: 'room', id: 'lobby', title: 'lobby' } }
    });
    render(<App bridge={bridge} />);
    await waitFor(() => expect(screen.getByRole('button', { name: '设置' })).toBeInTheDocument());
    fireEvent.click(screen.getByRole('button', { name: '设置' }));
    fireEvent.click(screen.getByRole('button', { name: '通知' }));
    expect(screen.getByText(/暂未开放/)).toBeInTheDocument();

    fireEvent.click(screen.getByRole('button', { name: '返回聊天' }));
    expect(screen.getByRole('heading', { name: '# lobby' })).toBeInTheDocument();
  });
});
