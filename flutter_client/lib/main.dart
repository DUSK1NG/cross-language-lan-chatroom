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
            ],
          ),
          body: Row(
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
                          color: Theme.of(context).colorScheme.errorContainer,
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
                          child: ConnectionForm(
                            enabled: session.connectionPhase != 'connecting',
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
                            isLanDiscoveryScanning:
                                session.isLanDiscoveryScanning,
                            discoveredHosts: session.discoveredHosts,
                            onDiscoverLanHosts: session.discoverLanHosts,
                            onConnectDiscoveredHost:
                                ({
                                  required hostId,
                                  required username,
                                  required userCode,
                                }) => session.connectDiscoveredHost(
                                  hostId,
                                  username: username,
                                  userCode: userCode,
                                ),
                          ),
                        ),
                      )
                    else ...[
                      Expanded(
                        child: MessageTimeline(messages: session.messages),
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
}
