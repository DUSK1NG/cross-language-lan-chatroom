import 'package:flutter_test/flutter_test.dart';
import 'package:lan_chat_flutter/main.dart';

void main() {
  testWidgets('shows the Flutter prototype shell', (tester) async {
    await tester.pumpWidget(const LanChatFlutterApp());
    expect(find.text('LAN Chat'), findsOneWidget);
    expect(find.text('未连接'), findsOneWidget);
  });
}
