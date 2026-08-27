import 'package:flutter_test/flutter_test.dart';
import 'package:lan_chat_flutter/main.dart';
import 'package:lan_chat_flutter/native/lan_chat_core.dart';

void main() {
  testWidgets('shows the Flutter prototype shell', (tester) async {
    await tester.pumpWidget(LanChatFlutterApp(core: _FakeCore()));
    expect(find.text('LAN Chat'), findsOneWidget);
    expect(find.text('未连接'), findsOneWidget);
  });

  testWidgets(
    'shows a non-interactive startup error when the core cannot load',
    (tester) async {
      await tester.pumpWidget(
        const LanChatFlutterApp(startupError: 'native core unavailable'),
      );

      expect(find.text('无法加载 LAN Chat 原生核心'), findsOneWidget);
      expect(find.text('native core unavailable'), findsOneWidget);
      expect(find.text('连接服务器'), findsNothing);
    },
  );
}

class _FakeCore implements ChatCore {
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
