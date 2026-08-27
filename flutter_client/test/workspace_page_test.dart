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

  testWidgets('refreshes both directories with explicit controls', (
    tester,
  ) async {
    final core = _FakeCore();
    await tester.pumpWidget(LanChatFlutterApp(core: core));
    await tester.pump();

    await tester.tap(find.byTooltip('刷新成员目录'));
    await tester.tap(find.byTooltip('刷新频道目录'));

    expect(core.dispatched, hasLength(2));
    expect(core.dispatched[0], contains('directory.refreshUsers'));
    expect(core.dispatched[0], contains('"payload":{}'));
    expect(core.dispatched[1], contains('directory.refreshRooms'));
    expect(core.dispatched[1], contains('"payload":{}'));
  });

  testWidgets('opens settings and changes only supported core settings', (
    tester,
  ) async {
    final core = _FakeCore(snapshot: _settingsStateJson);
    await tester.pumpWidget(LanChatFlutterApp(core: core));
    await tester.pump();

    await tester.tap(find.byTooltip('打开设置'));
    await tester.pump();
    expect(find.text('设置'), findsOneWidget);
    expect(find.bySemanticsLabel('性能等级'), findsOneWidget);
    expect(find.bySemanticsLabel('记录连接日志'), findsOneWidget);

    await tester.tap(find.bySemanticsLabel('性能等级'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('均衡').last);
    await tester.pumpAndSettle();
    await tester.tap(find.text('记录连接日志'));

    expect(core.dispatched[0], contains('settings.setPerformanceMode'));
    expect(core.dispatched[0], contains('"mode":"Balanced"'));
    expect(core.dispatched[1], contains('settings.setConnectionLogging'));
    expect(core.dispatched[1], contains('"enabled":true'));
  });

  testWidgets('only shows member administration for an administrator', (
    tester,
  ) async {
    await tester.pumpWidget(
      LanChatFlutterApp(core: _FakeCore(snapshot: _memberStateJson)),
    );
    await tester.pump();
    expect(find.byTooltip('管理成员'), findsNothing);

    await tester.pumpWidget(
      LanChatFlutterApp(
        key: UniqueKey(),
        core: _FakeCore(snapshot: _adminMemberStateJson),
      ),
    );
    await tester.pump();
    expect(find.byTooltip('管理成员'), findsOneWidget);
  });

  testWidgets(
    'lets a connected member open a private conversation from the directory',
    (tester) async {
      final core = _FakeCore(snapshot: _memberStateJson);
      await tester.pumpWidget(LanChatFlutterApp(core: core));
      await tester.pump();

      expect(find.byTooltip('查看成员目录'), findsOneWidget);
      expect(find.byTooltip('管理成员'), findsNothing);

      await tester.tap(find.byTooltip('查看成员目录'));
      await tester.pumpAndSettle();

      expect(find.text('Alice'), findsOneWidget);
      expect(find.text('Bob'), findsOneWidget);
      expect(find.byTooltip('与 B002 私聊'), findsOneWidget);
      expect(find.byTooltip('与 A001 私聊'), findsNothing);

      await tester.tap(find.byTooltip('与 B002 私聊'));

      expect(core.dispatched.single, contains('conversation.openPrivate'));
      expect(core.dispatched.single, contains('"displayName":"Bob"'));
      expect(core.dispatched.single, contains('"userCode":"B002"'));
    },
  );

  testWidgets('does not show administrator controls for the local member', (
    tester,
  ) async {
    await tester.pumpWidget(
      LanChatFlutterApp(core: _FakeCore(snapshot: _adminMemberStateJson)),
    );
    await tester.pump();

    await tester.tap(find.byTooltip('管理成员'));
    await tester.pumpAndSettle();

    expect(find.byTooltip('禁言或解禁 A001'), findsNothing);
    expect(find.byTooltip('踢出 A001'), findsNothing);
    expect(find.byTooltip('禁言或解禁 B002'), findsOneWidget);
    expect(find.byTooltip('踢出 B002'), findsOneWidget);
  });

  testWidgets('requires a member code for invite and remove room actions', (
    tester,
  ) async {
    final actions = <String>[];
    await tester.pumpWidget(
      MaterialApp(
        home: Scaffold(
          body: RoomActions(
            canCreateRoom: false,
            canManageActiveRoom: true,
            onCreateRoom: (_, {required isPrivate}) {},
            onRoomAction: (action, {required targetUserCode}) {
              actions.add('$action:$targetUserCode');
            },
          ),
        ),
      ),
    );

    await tester.tap(find.text('管理频道'));
    await tester.pumpAndSettle();
    await tester.tap(find.text('邀请成员'));
    await tester.pump();
    await tester.tap(find.text('移除成员'));
    await tester.pump();

    expect(actions, isEmpty);
    expect(find.text('管理频道'), findsWidgets);

    await tester.tap(find.text('删除频道'));
    expect(actions, ['delete:']);
  });

  testWidgets('only shows connection approvals to a connected administrator', (
    tester,
  ) async {
    await tester.pumpWidget(
      LanChatFlutterApp(core: _FakeCore(snapshot: _memberApprovalStateJson)),
    );
    await tester.pump();
    expect(find.byTooltip('管理成员'), findsNothing);
    expect(find.byTooltip('批准连接 approval-1'), findsNothing);

    final core = _FakeCore(snapshot: _adminApprovalStateJson);
    await tester.pumpWidget(LanChatFlutterApp(key: UniqueKey(), core: core));
    await tester.pump();
    await tester.tap(find.byTooltip('管理成员'));
    await tester.pumpAndSettle();

    expect(find.text('连接审批'), findsOneWidget);
    expect(find.text('Cara'), findsOneWidget);
    expect(find.text('C003'), findsOneWidget);
    expect(find.text('2026-08-27T10:00:00Z'), findsOneWidget);
    expect(find.byTooltip('批准连接 approval-1'), findsOneWidget);
    expect(find.byTooltip('拒绝连接 approval-1'), findsOneWidget);

    await tester.tap(find.byTooltip('批准连接 approval-1'));
    expect(core.dispatched.single, contains('admin.action'));
    expect(core.dispatched.single, contains('approve_connection'));
    expect(core.dispatched.single, contains('"messageId":"approval-1"'));
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

const _settingsStateJson =
    '{"schemaVersion":1,"connection":{"phase":"connected","statusText":"已连接"},"navigation":{"activeConversation":{"kind":"room","name":"lobby"}},"rooms":[{"roomName":"lobby","memberCount":1,"unreadCount":0}],"directMessages":[],"activeMessages":[],"diagnostics":{"enabled":false},"performance":{"mode":"Automatic"}}';

const _memberStateJson =
    '{"schemaVersion":1,"connection":{"phase":"connected","statusText":"已连接"},"identity":{"userCode":"A001","admin":false},"navigation":{"activeConversation":{"kind":"room","name":"lobby"}},"rooms":[{"roomName":"lobby","memberCount":2,"unreadCount":0}],"directMessages":[],"activeMessages":[],"members":[{"displayName":"Alice","userCode":"A001","online":true,"admin":false},{"displayName":"Bob","userCode":"B002","online":true,"admin":false}]}';

const _adminMemberStateJson =
    '{"schemaVersion":1,"connection":{"phase":"connected","statusText":"已连接"},"identity":{"userCode":"A001","admin":true},"navigation":{"activeConversation":{"kind":"room","name":"lobby"}},"rooms":[{"roomName":"lobby","memberCount":2,"unreadCount":0}],"directMessages":[],"activeMessages":[],"members":[{"displayName":"Alice","userCode":"A001","online":true,"admin":true},{"displayName":"Bob","userCode":"B002","online":true,"admin":false}]}';

const _memberApprovalStateJson =
    '{"schemaVersion":1,"connection":{"phase":"connected","statusText":"已连接"},"identity":{"admin":false},"navigation":{"activeConversation":{"kind":"room","name":"lobby"}},"rooms":[],"directMessages":[],"activeMessages":[],"connectionApprovals":[{"id":"approval-1","displayName":"Cara","userCode":"C003","requestedAt":"2026-08-27T10:00:00Z"}]}';

const _adminApprovalStateJson =
    '{"schemaVersion":1,"connection":{"phase":"connected","statusText":"已连接"},"identity":{"admin":true},"navigation":{"activeConversation":{"kind":"room","name":"lobby"}},"rooms":[],"directMessages":[],"activeMessages":[],"connectionApprovals":[{"id":"approval-1","displayName":"Cara","userCode":"C003","requestedAt":"2026-08-27T10:00:00Z"}]}';
