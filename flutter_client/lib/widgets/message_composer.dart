import 'package:flutter/material.dart';
import 'package:flutter/services.dart';

class MessageComposer extends StatefulWidget {
  const MessageComposer({
    required this.enabled,
    required this.onSend,
    super.key,
  });

  final bool enabled;
  final ValueChanged<String> onSend;

  @override
  State<MessageComposer> createState() => _MessageComposerState();
}

class _MessageComposerState extends State<MessageComposer> {
  final _controller = TextEditingController();

  @override
  void dispose() {
    _controller.dispose();
    super.dispose();
  }

  void _send() {
    if (!widget.enabled || _controller.text.trim().isEmpty) return;
    widget.onSend(_controller.text);
    _controller.clear();
  }

  @override
  Widget build(BuildContext context) => Shortcuts(
    shortcuts: const {SingleActivator(LogicalKeyboardKey.enter): _SendIntent()},
    child: Actions(
      actions: {
        _SendIntent: CallbackAction<_SendIntent>(onInvoke: (_) => _send()),
      },
      child: Padding(
        padding: const EdgeInsets.all(16),
        child: Row(
          crossAxisAlignment: CrossAxisAlignment.end,
          children: [
            Expanded(
              child: TextField(
                controller: _controller,
                enabled: widget.enabled,
                minLines: 1,
                maxLines: 4,
                textInputAction: TextInputAction.newline,
                decoration: const InputDecoration(
                  border: OutlineInputBorder(),
                  hintText: '输入消息（Enter 发送，Shift+Enter 换行）',
                ),
              ),
            ),
            const SizedBox(width: 8),
            Semantics(
              label: '发送消息',
              button: true,
              child: IconButton.filled(
                icon: const Icon(Icons.send),
                onPressed: widget.enabled ? _send : null,
                tooltip: '发送消息',
              ),
            ),
          ],
        ),
      ),
    ),
  );
}

class _SendIntent extends Intent {
  const _SendIntent();
}
