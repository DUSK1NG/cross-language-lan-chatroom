import 'package:flutter/material.dart';

import '../models/bridge_state.dart';

class MemberDirectory extends StatelessWidget {
  const MemberDirectory({
    required this.members,
    required this.localUserCode,
    required this.onOpenPrivate,
    super.key,
  });

  final List<BridgeMember> members;
  final String localUserCode;
  final void Function(String displayName, String userCode) onOpenPrivate;

  @override
  Widget build(BuildContext context) => IconButton(
    tooltip: '查看成员目录',
    icon: const Icon(Icons.groups_outlined),
    onPressed: () => showModalBottomSheet<void>(
      context: context,
      builder: (context) => SafeArea(
        child: ListView(
          shrinkWrap: true,
          children: [
            const ListTile(title: Text('成员目录')),
            for (final member in members)
              ListTile(
                title: Text(member.displayName),
                subtitle: Text(member.userCode),
                trailing: member.userCode.trim() == localUserCode.trim()
                    ? null
                    : IconButton(
                        tooltip: '与 ${member.userCode} 私聊',
                        icon: const Icon(Icons.chat_bubble_outline),
                        onPressed: () {
                          onOpenPrivate(member.displayName, member.userCode);
                          Navigator.pop(context);
                        },
                      ),
              ),
          ],
        ),
      ),
    ),
  );
}
