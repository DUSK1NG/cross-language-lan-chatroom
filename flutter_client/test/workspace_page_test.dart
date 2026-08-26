import 'package:flutter/material.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lan_chat_flutter/main.dart';
import 'package:lan_chat_flutter/native/lan_chat_core.dart';

void main() {
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
}

class _FakeCore implements ChatCore {
  final dispatched = <String>[];

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
  String stateJson() =>
      '{"schemaVersion":1,"connection":{"phase":"connected","statusText":"已连接"},"navigation":{"activeConversation":{"kind":"room","name":"lobby"}},"rooms":[{"roomName":"lobby","memberCount":1,"unreadCount":0}],"directMessages":[],"activeMessages":[{"sender":"Alice","content":"欢迎来到 LAN Chat"}]}';
}
