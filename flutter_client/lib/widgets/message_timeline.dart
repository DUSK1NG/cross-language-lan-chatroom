import 'package:flutter/material.dart';

import '../models/bridge_state.dart';

class MessageTimeline extends StatelessWidget {
  const MessageTimeline({required this.messages, super.key});

  final List<BridgeMessage> messages;

  @override
  Widget build(BuildContext context) {
    if (messages.isEmpty) {
      return const Center(child: Text('还没有消息'));
    }
    return ListView.builder(
      padding: const EdgeInsets.all(16),
      itemCount: messages.length,
      itemBuilder: (context, index) {
        final message = messages[index];
        final alignment = message.isSelf
            ? Alignment.centerRight
            : Alignment.centerLeft;
        return Align(
          alignment: alignment,
          child: Semantics(
            label: '${message.sender}：${message.content}',
            child: Container(
              constraints: const BoxConstraints(maxWidth: 560),
              margin: const EdgeInsets.only(bottom: 12),
              padding: const EdgeInsets.all(12),
              decoration: BoxDecoration(
                color: message.isSelf
                    ? Theme.of(context).colorScheme.primaryContainer
                    : Theme.of(context).colorScheme.surfaceContainerHighest,
                borderRadius: BorderRadius.circular(12),
              ),
              child: Column(
                crossAxisAlignment: CrossAxisAlignment.start,
                children: [
                  Text(
                    message.sender,
                    style: const TextStyle(fontWeight: FontWeight.w600),
                  ),
                  const SizedBox(height: 4),
                  Text(message.content),
                  if (message.time.isNotEmpty) ...[
                    const SizedBox(height: 4),
                    Text(
                      message.time,
                      style: Theme.of(context).textTheme.labelSmall,
                    ),
                  ],
                ],
              ),
            ),
          ),
        );
      },
    );
  }
}
