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
