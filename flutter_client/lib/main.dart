import 'package:flutter/material.dart';

import 'native/lan_chat_core.dart';
import 'state/chat_session_controller.dart';
import 'widgets/connection_banner.dart';
import 'widgets/connection_form.dart';
import 'widgets/conversation_sidebar.dart';
import 'widgets/admin_actions.dart';
import 'widgets/local_host_form.dart';
import 'widgets/message_composer.dart';
import 'widgets/message_timeline.dart';
import 'widgets/mode_selection_page.dart';
import 'widgets/room_actions.dart';
import 'widgets/settings_page.dart';

void main() {
  try {
    runApp(LanChatFlutterApp(core: LanChatCore.open()));
  } on StateError catch (error) {
    runApp(LanChatFlutterApp(startupError: error.toString()));
  } on ArgumentError catch (error) {
    runApp(LanChatFlutterApp(startupError: '无法加载 LAN Chat 原生核心：$error'));
  }
}

class LanChatFlutterApp extends StatefulWidget {
  const LanChatFlutterApp({this.core, this.startupError, super.key});

  final ChatCore? core;
  final String? startupError;

  @override
  State<LanChatFlutterApp> createState() => _LanChatFlutterAppState();
}

class _LanChatFlutterAppState extends State<LanChatFlutterApp> {
  ChatSessionController? _session;
  ConnectionMode? _connectionMode;
  bool _settingsOpen = false;

  @override
  void initState() {
    super.initState();
    if (widget.core case final core?) {
      _session = ChatSessionController(core)..refresh();
    }
  }

  @override
  void dispose() {
    _session?.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) {
    if (widget.startupError case final error?) {
      final detail = error == '无法加载 LAN Chat 原生核心'
          ? '请检查 Windows 原生运行时后重试。'
          : error;
      return MaterialApp(
        title: 'LAN Chat',
        theme: ThemeData(
          colorSchemeSeed: const Color(0xFF4F46E5),
          useMaterial3: true,
        ),
        home: Scaffold(
          body: Center(
            child: Padding(
              padding: const EdgeInsets.all(24),
              child: Column(
                mainAxisSize: MainAxisSize.min,
                children: [
                  const Text('无法加载 LAN Chat 原生核心'),
                  const SizedBox(height: 12),
                  Text(detail),
                ],
              ),
            ),
          ),
        ),
      );
    }

    final session = _session;
    if (session == null) {
      throw StateError('LanChatFlutterApp 需要原生核心或启动错误。');
    }
    return MaterialApp(
      title: 'LAN Chat',
      theme: ThemeData(
        colorSchemeSeed: const Color(0xFF4F46E5),
        useMaterial3: true,
      ),
      home: ListenableBuilder(
        listenable: session,
        builder: (context, _) => Scaffold(
          appBar: AppBar(
            title: const Text('LAN Chat'),
            actions: [
              Padding(
                padding: const EdgeInsets.only(right: 12),
                child: ConnectionBanner(
                  phase: session.connectionPhase,
                  statusText: session.statusText,
                  onDisconnect: session.disconnect,
                ),
              ),
              if (session.isConnected) ...[
                IconButton(
                  tooltip: '刷新成员目录',
                  onPressed: session.refreshUsersDirectory,
                  icon: const Icon(Icons.people_outline),
                ),
                IconButton(
                  tooltip: '刷新频道目录',
                  onPressed: session.refreshRoomsDirectory,
                  icon: const Icon(Icons.refresh),
                ),
                AdminActions(
                  isAllowed: session.isAdmin,
                  members: session.members,
                  onAction: (action, userCode) =>
                      session.sendAdminAction(action, targetUserCode: userCode),
                ),
                IconButton(
                  tooltip: '打开设置',
                  onPressed: () => setState(() => _settingsOpen = true),
                  icon: const Icon(Icons.settings_outlined),
                ),
              ],
              if (!session.isConnected && _connectionMode != null)
                IconButton(
                  tooltip: '返回连接方式',
                  onPressed: () => setState(() => _connectionMode = null),
                  icon: const Icon(Icons.arrow_back),
                ),
            ],
          ),
          body: _settingsOpen && session.isConnected
              ? SettingsPage(
                  performanceMode: session.performanceMode,
                  connectionLoggingEnabled: session.connectionLoggingEnabled,
                  onPerformanceModeChanged: session.setPerformanceMode,
                  onConnectionLoggingChanged: session.setConnectionLogging,
                  onBack: () => setState(() => _settingsOpen = false),
                )
              : Row(
                  children: [
                    ConversationSidebar(
                      conversations: session.conversations,
                      selectedRoom: session.selectedRoom,
                      onSelected: session.selectConversation,
                    ),
                    Expanded(
                      child: Column(
                        children: [
                          if (session.lastError case final error?)
                            Semantics(
                              liveRegion: true,
                              label: '错误：$error',
                              child: Container(
                                width: double.infinity,
                                color: Theme.of(context)
                                    .colorScheme
                                    .errorContainer,
                                padding: const EdgeInsets.all(12),
                                child: Text(error),
                              ),
                            ),
                          if (session.connectionPhase == 'connecting')
                            TextButton.icon(
                              onPressed: session.disconnect,
                              icon: const Icon(Icons.cancel_outlined),
                              label: const Text('取消连接'),
                            ),
                          if (!session.isConnected)
                            Expanded(
                              child: SingleChildScrollView(
                                child: _connectionPage(session),
                              ),
                            )
                          else ...[
                            Padding(
                              padding: const EdgeInsets.fromLTRB(16, 12, 16, 0),
                              child: RoomActions(
                                canCreateRoom: session.isConnected,
                                canManageActiveRoom:
                                    session.selectedConversationKind ==
                                        'room' &&
                                    session.activeRoomCanManage,
                                onCreateRoom: session.createRoom,
                                onRoomAction:
                                    (action, {required targetUserCode}) =>
                                        session.sendRoomAction(
                                          action,
                                          targetUserCode: targetUserCode,
                                        ),
                              ),
                            ),
                            Expanded(
                              child: MessageTimeline(
                                messages: session.messages,
                                canRecallMessages: session.isAdmin,
                                onCopy: session.copyMessage,
                                onRemoveLocal: session.removeLocalMessage,
                                onRecall: session.recallMessage,
                                onRetry: session.retryMessage,
                                onOpenPrivate: session.openPrivateConversation,
                              ),
                            ),
                            MessageComposer(
                              enabled: true,
                              onSend: session.sendMessage,
                            ),
                          ],
                        ],
                      ),
                    ),
                  ],
                ),
        ),
      ),
    );
  }

  Widget _connectionPage(ChatSessionController session) {
    final enabled = session.connectionPhase != 'connecting';
    return switch (_connectionMode) {
      null => ModeSelectionPage(
        onSelected: (mode) => setState(() => _connectionMode = mode),
      ),
      ConnectionMode.localHost => LocalHostForm(
        enabled: enabled,
        onConnect:
            ({
              required serverExe,
              required certFile,
              required keyFile,
              required dbFile,
              required username,
              required userCode,
            }) => session.connectLocalHost(
              serverExe: serverExe,
              certFile: certFile,
              keyFile: keyFile,
              dbFile: dbFile,
              username: username,
              userCode: userCode,
            ),
      ),
      ConnectionMode.remote || ConnectionMode.lan => ConnectionForm(
        enabled: enabled,
        onConnect:
            ({
              required serverIp,
              required serverPort,
              required username,
              required userCode,
              required caFile,
              required useLocalhostTlsSni,
            }) => session.connectRemote(
              serverIp: serverIp,
              serverPort: serverPort,
              username: username,
              userCode: userCode,
              caFile: caFile,
              useLocalhostTlsSni: useLocalhostTlsSni,
            ),
        isLanDiscoveryScanning: session.isLanDiscoveryScanning,
        discoveredHosts: session.discoveredHosts,
        onDiscoverLanHosts: session.discoverLanHosts,
        onConnectDiscoveredHost:
            ({required hostId, required username, required userCode}) =>
                session.connectDiscoveredHost(
                  hostId,
                  username: username,
                  userCode: userCode,
                ),
      ),
    };
  }
}
