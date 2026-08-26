import 'package:flutter/material.dart';

void main() => runApp(const LanChatFlutterApp());

class LanChatFlutterApp extends StatelessWidget {
  const LanChatFlutterApp({super.key});

  @override
  Widget build(BuildContext context) => MaterialApp(
        title: 'LAN Chat',
        theme: ThemeData(
          colorSchemeSeed: const Color(0xFF4F46E5),
          useMaterial3: true,
        ),
        home: Scaffold(
          appBar: AppBar(title: Text('LAN Chat')),
          body: Center(child: Text('未连接')),
        ),
      );
}
