import 'package:flutter/material.dart';

class ConnectionBanner extends StatelessWidget {
  const ConnectionBanner({
    required this.phase,
    required this.statusText,
    required this.onDisconnect,
    super.key,
  });

  final String phase;
  final String statusText;
  final VoidCallback onDisconnect;

  @override
  Widget build(BuildContext context) {
    final connected = phase == 'connected';
    final color = connected
        ? Colors.green
        : Theme.of(context).colorScheme.outline;
    return Semantics(
      label: '连接状态：$statusText',
      child: Row(
        mainAxisSize: MainAxisSize.min,
        children: [
          Icon(Icons.circle, size: 12, color: color),
          const SizedBox(width: 8),
          Text(statusText),
          if (connected) ...[
            const SizedBox(width: 4),
            Tooltip(
              message: '断开连接',
              child: Semantics(
                label: '断开连接',
                button: true,
                child: IconButton(
                  icon: const Icon(Icons.link_off),
                  onPressed: onDisconnect,
                ),
              ),
            ),
          ],
        ],
      ),
    );
  }
}
