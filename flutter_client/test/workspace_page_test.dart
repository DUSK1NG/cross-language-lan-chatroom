import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lan_chat_flutter/main.dart';
import 'package:lan_chat_flutter/models/bridge_state.dart';
import 'package:lan_chat_flutter/native/lan_chat_core.dart';
import 'package:lan_chat_flutter/widgets/message_timeline.dart';
import 'package:lan_chat_flutter/widgets/room_actions.dart';

void main() {
  testWidgets('shows a startup error instead of hiding a core load failure', (
    tester,
  ) async {
    await tester.pumpWidget(
      const LanChatFlutterApp(startupError: '无法加载 LAN Chat 原生核心'),
    );

    expect(find.text('无法加载 LAN Chat 原生核心'), findsOneWidget);
  });

  testWidgets('shows all three connection modes on the initial page', (
    tester,
  ) async {
    await tester.pumpWidget(
      LanChatFlutterApp(core: _FakeCore(snapshot: _idleStateJson)),
    );
    await tester.pump();

    expect(find.text('远程服务器'), findsOneWidget);
    expect(find.text('创建本地聊天室'), findsOneWidget);
    expect(find.text('加入局域网聊天室'), findsOneWidget);
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
      await _selectMode(tester, '远程服务器');

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
    await _selectMode(tester, '加入局域网聊天室');

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
    await _selectMode(tester, '加入局域网聊天室');

    expect(find.text('未发现局域网主机'), findsOneWidget);
  });

  testWidgets('does not show an empty message while LAN discovery scans', (
    tester,
  ) async {
    final core = _FakeCore(snapshot: _scanningEmptyLanDiscoveryStateJson);
    await tester.pumpWidget(LanChatFlutterApp(core: core));
    await tester.pump();
    await _selectMode(tester, '加入局域网聊天室');

    expect(find.text('正在搜索局域网主机…'), findsOneWidget);
    expect(find.text('未发现局域网主机'), findsNothing);
  });

  testWidgets('shows a failed LAN discovery command message', (tester) async {
    final core = _FakeCore(
      snapshot: _idleStateJson,
      events: const [
        '{"kind":"result","payload":{"id":"flutter-1","ok":false,"error":{"code":"discovery_unavailable","message":"局域网发现当前不可用","retryable":true,"source":"bridge"}}}',
      ],
    );
    await tester.pumpWidget(LanChatFlutterApp(core: core));
    await tester.pump(const Duration(milliseconds: 100));

    expect(find.text('局域网发现当前不可用'), findsOneWidget);
  });

  testWidgets('only exposes room management when the state permits it', (
    tester,
  ) async {
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: RoomActions(
            canCreateRoom: true,
            canManageActiveRoom: false,
            onCreateRoom: (_, {required isPrivate}) {},
            onRoomAction: (_, {required targetUserCode}) {},
          ),
        ),
      ),
    );

    expect(find.text('新建频道'), findsOneWidget);
    expect(find.text('管理频道'), findsNothing);
  });

  testWidgets('only exposes authorized message operations', (tester) async {
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: MessageTimeline(
            messages: const [
              BridgeMessage(
                id: 'm-1',
                sender: 'Bob',
                userCode: 'B002',
                content: '你好',
                time: '10:00',
                isSelf: false,
                isSystem: false,
                deliveryState: 'sent',
              ),
            ],
            canRecallMessages: false,
            onCopy: (_) {},
            onRemoveLocal: (_) {},
            onRecall: (_) {},
            onRetry: (_) {},
            onOpenPrivate: (_, ignoredUserCode) {},
          ),
        ),
      ),
    );

    expect(find.byTooltip('复制消息'), findsOneWidget);
    expect(find.byTooltip('删除本地消息'), findsOneWidget);
    expect(find.byTooltip('撤回消息'), findsNothing);
    expect(find.byTooltip('重试发送'), findsNothing);
    expect(find.byTooltip('发起私聊'), findsOneWidget);
  });
}

Future<void> _selectMode(WidgetTester tester, String label) async {
  await tester.tap(find.text(label));
  await tester.pump();
}

class _FakeCore implements ChatCore {
  _FakeCore({String? snapshot, List<String> events = const []})
    : _snapshot = snapshot ?? _connectedStateJson,
      _events = List.of(events);

  final dispatched = <String>[];
  final String _snapshot;
  final List<String> _events;

  @override
  int dispatch(String json) {
    dispatched.add(json);
    return 0;
  }

  @override
  List<String> drainEvents() {
    final events = List<String>.of(_events);
    _events.clear();
    return events;
  }

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

const _scanningEmptyLanDiscoveryStateJson =
    '{"schemaVersion":1,"connection":{"phase":"idle","statusText":"未连接"},"rooms":[],"directMessages":[],"activeMessages":[],"lanDiscovery":{"scanning":true,"hosts":[]}}';
