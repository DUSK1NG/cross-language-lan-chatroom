import 'dart:convert';

import 'package:flutter_test/flutter_test.dart';
import 'package:lan_chat_flutter/native/lan_chat_core.dart';
import 'package:lan_chat_flutter/state/chat_session_controller.dart';

void main() {
  TestWidgetsFlutterBinding.ensureInitialized();

  test(
    'maps a connected bridge snapshot and sends through the selected room',
    () {
      final core = FakeLanChatCore(snapshot: connectedLobbyStateJson);
      final controller = ChatSessionController(core)..refresh();
      addTearDown(controller.dispose);

      expect(controller.connectionPhase, 'connected');
      expect(controller.conversations.single.title, 'lobby');
      controller.sendMessage('hello');

      final command =
          jsonDecode(core.dispatched.single) as Map<String, dynamic>;
      expect(command['type'], 'chat.sendRoom');
      expect(command['payload'], {'content': 'hello', 'room': 'lobby'});
      expect(command['id'], startsWith('flutter-'));
    },
  );

  test('rejects blank messages without dispatching a command', () {
    final core = FakeLanChatCore(snapshot: connectedLobbyStateJson);
    final controller = ChatSessionController(core)..refresh();
    addTearDown(controller.dispose);

    controller.sendMessage('  \n ');

    expect(core.dispatched, isEmpty);
    expect(controller.lastError, '消息不能为空');
  });

  test('uses navigation activeConversation name for a selected room send', () {
    final core = FakeLanChatCore(
      snapshot: '{"schemaVersion":1,"connection":{"phase":"connected","statusText":"已连接"},"navigation":{"activeConversation":{"kind":"room","name":"study"}},"rooms":[{"roomName":"lobby","memberCount":1,"unreadCount":0},{"roomName":"study","memberCount":2,"unreadCount":0}],"directMessages":[],"activeMessages":[]}',
    );
    final controller = ChatSessionController(core)..refresh();
    addTearDown(controller.dispose);

    controller.sendMessage('资料已更新');

    expect(controller.selectedRoom, 'study');
    expect(jsonDecode(core.dispatched.single)['payload'], {
      'content': '资料已更新',
      'room': 'study',
    });
  });

  test('keeps malformed bridge JSON as a non-throwing error', () {
    final core = FakeLanChatCore(snapshot: '{not json');
    final controller = ChatSessionController(core);
    addTearDown(controller.dispose);

    controller.refresh();

    expect(controller.lastError, isNotEmpty);
    expect(controller.connectionPhase, 'idle');
  });

  test('coalesces queued state events into one refreshed snapshot', () {
    final core = FakeLanChatCore(
      snapshot: connectedLobbyStateJson,
      events: const ['{"type":"state"}', '{"type":"state"}'],
    );
    final controller = ChatSessionController(core);
    addTearDown(controller.dispose);
    var changes = 0;
    controller.addListener(() => changes += 1);

    controller.drainPendingEvents();

    expect(changes, 1);
    expect(controller.connectionPhase, 'connected');
  });

  test('surfaces a failed bridge command result as a safe error message', () {
    final core = FakeLanChatCore(
      snapshot: connectedLobbyStateJson,
      events: const [
        '{"kind":"result","payload":{"id":"flutter-1","ok":false,"error":{"code":"discovery_unavailable","message":"局域网发现当前不可用","retryable":true,"source":"bridge"}}}',
      ],
    );
    final controller = ChatSessionController(core);
    addTearDown(controller.dispose);

    controller.drainPendingEvents();

    expect(controller.lastError, '局域网发现当前不可用');
  });

  test('dispatches an exact validated remote connection payload', () {
    final core = FakeLanChatCore(snapshot: connectedLobbyStateJson);
    final controller = ChatSessionController(core)..refresh();
    addTearDown(controller.dispose);

    final accepted = controller.connectRemote(
      serverIp: ' 192.168.1.40 ',
      serverPort: ' 8888 ',
      username: ' Alice ',
      userCode: ' A001 ',
      caFile: ' C:/public-ca.pem ',
      useLocalhostTlsSni: true,
    );

    expect(accepted, isTrue);
    final command = jsonDecode(core.dispatched.single) as Map<String, dynamic>;
    expect(command['type'], 'session.connectRemote');
    expect(command['payload'], {
      'serverIp': '192.168.1.40',
      'serverPort': 8888,
      'username': 'Alice',
      'userCode': 'A001',
      'caFile': 'C:/public-ca.pem',
      'tlsServerName': 'localhost',
    });
  });

  test('rejects incomplete remote connection fields without dispatching', () {
    final core = FakeLanChatCore(snapshot: connectedLobbyStateJson);
    final controller = ChatSessionController(core)..refresh();
    addTearDown(controller.dispose);

    expect(
      controller.connectRemote(
        serverIp: '',
        serverPort: '8888',
        username: '',
        userCode: '',
      ),
      isFalse,
    );
    expect(core.dispatched, isEmpty);
    expect(controller.lastError, '服务器地址、用户名和用户代码不能为空');
  });

  test('rejects an out-of-range remote port without dispatching', () {
    final core = FakeLanChatCore(snapshot: connectedLobbyStateJson);
    final controller = ChatSessionController(core)..refresh();
    addTearDown(controller.dispose);

    expect(
      controller.connectRemote(
        serverIp: '192.168.1.40',
        serverPort: '70000',
        username: 'Alice',
        userCode: 'A001',
      ),
      isFalse,
    );
    expect(core.dispatched, isEmpty);
    expect(controller.lastError, '端口必须是 1 到 65535 的整数');
  });

  test('selects a direct conversation using the bridge command name', () {
    final core = FakeLanChatCore(
      snapshot: '{"schemaVersion":1,"connection":{"phase":"connected","statusText":"已连接"},"rooms":[],"directMessages":[{"userCode":"B002","displayName":"Bob","unreadCount":0}],"activeMessages":[]}',
    );
    final controller = ChatSessionController(core)..refresh();
    addTearDown(controller.dispose);

    controller.selectConversation(controller.conversations.single);

    final command = jsonDecode(core.dispatched.single) as Map<String, dynamic>;
    expect(command['type'], 'conversation.selectDirect');
    expect(command['payload'], {'userCode': 'B002'});
  });

  test(
    'maps discovered host metadata and dispatches LAN discovery commands',
    () {
      final core = FakeLanChatCore(snapshot: lanDiscoveryStateJson);
      final controller = ChatSessionController(core)..refresh();
      addTearDown(controller.dispose);

      expect(controller.isLanDiscoveryScanning, isTrue);
      expect(controller.discoveredHosts, hasLength(1));
      expect(controller.discoveredHosts.single.hostName, 'Alice PC');
      expect(controller.discoveredHosts.single.serverIp, '192.168.8.23');
      expect(controller.discoveredHosts.single.serverPort, 8888);
      expect(controller.discoveredHosts.single.isKnown, isFalse);

      controller.discoverLanHosts();

      expect(jsonDecode(core.dispatched.single), {
        'id': startsWith('flutter-'),
        'type': 'session.discoverLanHosts',
        'payload': <String, dynamic>{},
      });

      controller.connectDiscoveredHost(
        'host-1',
        username: ' Alice ',
        userCode: ' A001 ',
      );

      final command = jsonDecode(core.dispatched.last) as Map<String, dynamic>;
      expect(command['type'], 'session.connectDiscoveredHost');
      expect(command['payload'], {
        'hostId': 'host-1',
        'username': 'Alice',
        'userCode': 'A001',
      });
    },
  );
}

class FakeLanChatCore implements ChatCore {
  FakeLanChatCore({required this.snapshot, List<String> events = const []})
    : _events = List.of(events);

  String snapshot;
  final List<String> dispatched = <String>[];
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
  String stateJson() => snapshot;
}

const connectedLobbyStateJson =
    '{"schemaVersion":1,"connection":{"phase":"connected","statusText":"已连接"},"navigation":{"activeConversation":{"kind":"room","name":"lobby"}},"rooms":[{"roomName":"lobby","memberCount":1,"unreadCount":0}],"directMessages":[],"activeMessages":[]}';

const lanDiscoveryStateJson =
    '{"schemaVersion":1,"connection":{"phase":"idle","statusText":"未连接"},"rooms":[],"directMessages":[],"activeMessages":[],"lanDiscovery":{"scanning":true,"hosts":[{"id":"host-1","hostName":"Alice PC","serverIp":"192.168.8.23","serverPort":8888,"fingerprintSha256":"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa","known":false}]}}';
