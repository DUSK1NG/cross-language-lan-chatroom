import 'package:flutter/material.dart';

import '../models/bridge_state.dart';

class ConversationSidebar extends StatelessWidget {
  const ConversationSidebar({
    required this.conversations,
    required this.selectedRoom,
    required this.onSelected,
    super.key,
  });

  final List<BridgeConversation> conversations;
  final String selectedRoom;
  final ValueChanged<BridgeConversation> onSelected;

  @override
  Widget build(BuildContext context) => SizedBox(
    width: 240,
    child: Material(
      color: Theme.of(context).colorScheme.surfaceContainerLow,
      child: ListView(
        children: [
          const Padding(
            padding: EdgeInsets.fromLTRB(16, 18, 16, 8),
            child: Text('会话', style: TextStyle(fontWeight: FontWeight.bold)),
          ),
          for (final conversation in conversations)
            Semantics(
              button: true,
              label: '打开会话 ${conversation.title}',
              child: ListTile(
                leading: Icon(
                  conversation.kind == 'room' ? Icons.tag : Icons.person,
                ),
                title: Text(conversation.title),
                subtitle: conversation.memberCount > 0
                    ? Text('${conversation.memberCount} 位成员')
                    : null,
                trailing: conversation.unreadCount > 0
                    ? Badge(label: Text('${conversation.unreadCount}'))
                    : null,
                selected: conversation.id == selectedRoom,
                onTap: () => onSelected(conversation),
              ),
            ),
        ],
      ),
    ),
  );
}
