import 'package:flutter/material.dart';

import 'native/lan_chat_core.dart';
import 'state/chat_session_controller.dart';
import 'widgets/connection_banner.dart';
import 'widgets/connection_form.dart';
import 'widgets/conversation_sidebar.dart';
import 'widgets/message_composer.dart';
import 'widgets/message_timeline.dart';

void main() {
  try {
    runApp(LanChatFlutterApp(core: LanChatCore.open()));
  } catch (error) {
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
  late final ChatSessionController _session;

  @override
  void initState() {
    super.initState();
    _session = ChatSessionController(widget.core ?? _IdleChatCore())..refresh();
  }

  @override
  void dispose() {
    _session.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => MaterialApp(
    title: 'LAN Chat',
    theme: ThemeData(
      colorSchemeSeed: const Color(0xFF4F46E5),
      useMaterial3: true,
    ),
    home: ListenableBuilder(
      listenable: _session,
      builder: (context, _) => Scaffold(
        appBar: AppBar(
          title: const Text('LAN Chat'),
          actions: [
            Padding(
              padding: const EdgeInsets.only(right: 12),
              child: ConnectionBanner(
                phase: _session.connectionPhase,
                statusText: _session.statusText,
                onDisconnect: _session.disconnect,
              ),
            ),
          ],
        ),
        body: Row(
          children: [
            ConversationSidebar(
              conversations: _session.conversations,
              selectedRoom: _session.selectedRoom,
              onSelected: _session.selectConversation,
            ),
            Expanded(
              child: Column(
                children: [
                  if (widget.startupError ?? _session.lastError
                      case final error?)
                    Semantics(
                      liveRegion: true,
                      label: '错误：$error',
                      child: Container(
                        width: double.infinity,
                        color: Theme.of(context).colorScheme.errorContainer,
                        padding: const EdgeInsets.all(12),
                        child: Text(error),
                      ),
                    ),
                  if (_session.connectionPhase == 'connecting')
                    TextButton.icon(
                      onPressed: _session.disconnect,
                      icon: const Icon(Icons.cancel_outlined),
                      label: const Text('取消连接'),
                    ),
                  if (!_session.isConnected)
                    Expanded(
                      child: SingleChildScrollView(
                        child: ConnectionForm(
                          enabled: _session.connectionPhase != 'connecting',
                          onConnect:
                              ({
                                required serverIp,
                                required serverPort,
                                required username,
                                required userCode,
                                required caFile,
                                required useLocalhostTlsSni,
                              }) => _session.connectRemote(
                                serverIp: serverIp,
                                serverPort: serverPort,
                                username: username,
                                userCode: userCode,
                                caFile: caFile,
                                useLocalhostTlsSni: useLocalhostTlsSni,
                              ),
                          isLanDiscoveryScanning:
                              _session.isLanDiscoveryScanning,
                          discoveredHosts: _session.discoveredHosts,
                          onDiscoverLanHosts: _session.discoverLanHosts,
                          onConnectDiscoveredHost:
                              ({
                                required hostId,
                                required username,
                                required userCode,
                              }) => _session.connectDiscoveredHost(
                                hostId,
                                username: username,
                                userCode: userCode,
                              ),
                        ),
                      ),
                    )
                  else ...[
                    Expanded(
                      child: MessageTimeline(messages: _session.messages),
                    ),
                    MessageComposer(
                      enabled: true,
                      onSend: _session.sendMessage,
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

class _IdleChatCore implements ChatCore {
  @override
  int dispatch(String json) => 0;

  @override
  List<String> drainEvents() => const [];

  @override
  void dispose() {}

  @override
  String stateJson() =>
      '{"schemaVersion":1,"connection":{"phase":"idle","statusText":"未连接"},"rooms":[],"directMessages":[],"activeMessages":[]}';
}
