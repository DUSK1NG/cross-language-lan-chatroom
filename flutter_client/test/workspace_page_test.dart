import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lan_chat_flutter/main.dart';
import 'package:lan_chat_flutter/native/lan_chat_core.dart';

void main() {
  testWidgets('shows a startup error instead of hiding a core load failure', (
    tester,
  ) async {
    await tester.pumpWidget(
      const LanChatFlutterApp(startupError: '无法加载 LAN Chat 原生核心'),
    );

    expect(find.text('无法加载 LAN Chat 原生核心'), findsOneWidget);
  });

  testWidgets('renders the chat workspace and sends a room message', (
    tester,
  ) async {
    final core = _FakeCore();
    await tester.pumpWidget(LanChatFlutterApp(core: core));
    await tester.pump();

    expect(find.text('lobby'), findsOneWidget);
    expect(find.byTooltip('发送消息'), findsOneWidget);

    await tester.enterText(find.byType(TextField), '你好');
    await tester.tap(find.byTooltip('发送消息'));

    expect(core.dispatched.single, contains('chat.sendRoom'));
  });

  testWidgets(
    'shows remote connection fields and dispatches a connect command',
    (tester) async {
      final core = _FakeCore(snapshot: _idleStateJson);
      await tester.pumpWidget(LanChatFlutterApp(core: core));
      await tester.pump();

      expect(find.bySemanticsLabel('服务器地址'), findsOneWidget);
      expect(find.bySemanticsLabel('端口'), findsOneWidget);
      expect(find.bySemanticsLabel('用户名'), findsOneWidget);
      expect(find.bySemanticsLabel('用户代码'), findsOneWidget);
      expect(find.bySemanticsLabel('公共 CA 文件路径'), findsOneWidget);
      expect(find.bySemanticsLabel('使用 localhost TLS SNI'), findsOneWidget);

      await tester.enterText(find.bySemanticsLabel('服务器地址'), '127.0.0.1');
      await tester.enterText(find.bySemanticsLabel('端口'), '8888');
      await tester.enterText(find.bySemanticsLabel('用户名'), 'Alice');
      await tester.enterText(find.bySemanticsLabel('用户代码'), 'A001');
      await tester.tap(find.widgetWithText(FilledButton, '连接到服务器'));

      expect(core.dispatched.single, contains('session.connectRemote'));
    },
  );

  testWidgets('shows discovered LAN hosts and joins with identity only', (
    tester,
  ) async {
    final core = _FakeCore(snapshot: _lanDiscoveryStateJson);
    await tester.pumpWidget(LanChatFlutterApp(core: core));
    await tester.pump();

    expect(find.bySemanticsLabel('搜索局域网主机'), findsOneWidget);
    expect(find.text('正在搜索局域网主机…'), findsOneWidget);
    expect(find.text('Alice PC'), findsOneWidget);
    expect(find.text('192.168.8.23:8888\n新发现的主机'), findsOneWidget);

    await tester.tap(find.bySemanticsLabel('搜索局域网主机'));
    expect(core.dispatched.single, contains('session.discoverLanHosts'));

    await tester.enterText(find.bySemanticsLabel('用户名'), 'Alice');
    await tester.enterText(find.bySemanticsLabel('用户代码'), 'A001');
    final connectDiscoveredHost = find.widgetWithText(FilledButton, '使用此主机连接');
    await tester.ensureVisible(connectDiscoveredHost);
    await tester.tap(connectDiscoveredHost);

    expect(core.dispatched.last, contains('session.connectDiscoveredHost'));
    expect(core.dispatched.last, contains('"hostId":"host-1"'));
    expect(core.dispatched.last, contains('"username":"Alice"'));
    expect(core.dispatched.last, contains('"userCode":"A001"'));
    expect(core.dispatched.last, isNot(contains('caFile')));
  });

  testWidgets('shows an empty LAN discovery message', (tester) async {
    final core = _FakeCore(snapshot: _idleStateJson);
    await tester.pumpWidget(LanChatFlutterApp(core: core));
    await tester.pump();

    expect(find.text('未发现局域网主机'), findsOneWidget);
  });
}

class _FakeCore implements ChatCore {
  _FakeCore({String? snapshot}) : _snapshot = snapshot ?? _connectedStateJson;

  final dispatched = <String>[];
  final String _snapshot;

  @override
  int dispatch(String json) {
    dispatched.add(json);
    return 0;
  }

  @override
  List<String> drainEvents() => const [];

  @override
  void dispose() {}

  @override
  String stateJson() => _snapshot;
}

const _connectedStateJson =
    '{"schemaVersion":1,"connection":{"phase":"connected","statusText":"已连接"},"navigation":{"activeConversation":{"kind":"room","name":"lobby"}},"rooms":[{"roomName":"lobby","memberCount":1,"unreadCount":0}],"directMessages":[],"activeMessages":[{"sender":"Alice","content":"欢迎来到 LAN Chat"}]}';

const _idleStateJson =
    '{"schemaVersion":1,"connection":{"phase":"idle","statusText":"未连接"},"rooms":[],"directMessages":[],"activeMessages":[]}';

const _lanDiscoveryStateJson =
    '{"schemaVersion":1,"connection":{"phase":"idle","statusText":"未连接"},"rooms":[],"directMessages":[],"activeMessages":[],"lanDiscovery":{"scanning":true,"hosts":[{"id":"host-1","hostName":"Alice PC","serverIp":"192.168.8.23","serverPort":8888,"fingerprintSha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","known":false}]}}';
