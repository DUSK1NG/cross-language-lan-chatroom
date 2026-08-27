import 'package:flutter/material.dart';

import '../models/bridge_state.dart';

class MessageTimeline extends StatelessWidget {
  const MessageTimeline({
    required this.messages,
    this.canRecallMessages = false,
    this.onCopy,
    this.onRemoveLocal,
    this.onRecall,
    this.onRetry,
    this.onOpenPrivate,
    super.key,
  });

  final List<BridgeMessage> messages;
  final bool canRecallMessages;
  final ValueChanged<String>? onCopy;
  final ValueChanged<String>? onRemoveLocal;
  final ValueChanged<String>? onRecall;
  final ValueChanged<String>? onRetry;
  final void Function(String displayName, String userCode)? onOpenPrivate;

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
                  if (!message.isSystem)
                    Wrap(
                      spacing: 4,
                      children: [
                        if (onCopy != null)
                          IconButton(
                            tooltip: '复制消息',
                            onPressed: () => onCopy!(message.content),
                            icon: const Icon(Icons.copy_outlined),
                          ),
                        if (onRemoveLocal != null && message.id.isNotEmpty)
                          IconButton(
                            tooltip: '删除本地消息',
                            onPressed: () => onRemoveLocal!(message.id),
                            icon: const Icon(Icons.delete_outline),
                          ),
                        if (onRecall != null &&
                            message.id.isNotEmpty &&
                            (canRecallMessages || message.isSelf))
                          IconButton(
                            tooltip: '撤回消息',
                            onPressed: () => onRecall!(message.id),
                            icon: const Icon(Icons.undo),
                          ),
                        if (onRetry != null &&
                            message.id.isNotEmpty &&
                            message.isSelf &&
                            message.deliveryState == 'failed')
                          IconButton(
                            tooltip: '重试发送',
                            onPressed: () => onRetry!(message.id),
                            icon: const Icon(Icons.refresh),
                          ),
                        if (onOpenPrivate != null &&
                            !message.isSelf &&
                            message.sender.isNotEmpty &&
                            message.userCode.isNotEmpty)
                          IconButton(
                            tooltip: '发起私聊',
                            onPressed: () =>
                                onOpenPrivate!(message.sender, message.userCode),
                            icon: const Icon(Icons.person_outline),
                          ),
                      ],
                    ),
                ],
              ),
            ),
          ),
        );
      },
    );
  }
}
