import 'package:flutter/material.dart';

class RoomActions extends StatefulWidget {
  const RoomActions({
    required this.canCreateRoom,
    required this.canManageActiveRoom,
    required this.onCreateRoom,
    required this.onRoomAction,
    super.key,
  });

  final bool canCreateRoom;
  final bool canManageActiveRoom;
  final void Function(String room, {required bool isPrivate}) onCreateRoom;
  final void Function(String action, {required String targetUserCode}) onRoomAction;

  @override
  State<RoomActions> createState() => _RoomActionsState();
}

class _RoomActionsState extends State<RoomActions> {
  Future<void> _showCreateRoom() async {
    final room = TextEditingController();
    var isPrivate = false;
    await showDialog<void>(
      context: context,
      builder: (context) => StatefulBuilder(
        builder: (context, setDialogState) => AlertDialog(
          title: const Text('新建频道'),
          content: Column(
            mainAxisSize: MainAxisSize.min,
            children: [
              TextField(
                controller: room,
                autofocus: true,
                decoration: const InputDecoration(labelText: '频道名称'),
              ),
              SwitchListTile(
                contentPadding: EdgeInsets.zero,
                title: const Text('私有频道'),
                value: isPrivate,
                onChanged: (value) => setDialogState(() => isPrivate = value),
              ),
            ],
          ),
          actions: [
            TextButton(
              onPressed: () => Navigator.pop(context),
              child: const Text('取消'),
            ),
            FilledButton(
              onPressed: () {
                final name = room.text.trim();
                if (name.isEmpty) return;
                widget.onCreateRoom(name, isPrivate: isPrivate);
                Navigator.pop(context);
              },
              child: const Text('创建'),
            ),
          ],
        ),
      ),
    );
    room.dispose();
  }

  Future<void> _showRoomManagement() async {
    final memberCode = TextEditingController();
    await showDialog<void>(
      context: context,
      builder: (context) => AlertDialog(
        title: const Text('管理频道'),
        content: TextField(
          controller: memberCode,
          decoration: const InputDecoration(labelText: '成员代码'),
        ),
        actions: [
          TextButton(
            onPressed: () => Navigator.pop(context),
            child: const Text('取消'),
          ),
          TextButton(
            onPressed: () {
              widget.onRoomAction(
                'remove_member',
                targetUserCode: memberCode.text.trim(),
              );
              Navigator.pop(context);
            },
            child: const Text('移除成员'),
          ),
          FilledButton(
            onPressed: () {
              widget.onRoomAction(
                'invite',
                targetUserCode: memberCode.text.trim(),
              );
              Navigator.pop(context);
            },
            child: const Text('邀请成员'),
          ),
          TextButton(
            onPressed: () {
              widget.onRoomAction('delete', targetUserCode: '');
              Navigator.pop(context);
            },
            child: const Text('删除频道'),
          ),
        ],
      ),
    );
    memberCode.dispose();
  }

  @override
  Widget build(BuildContext context) => Wrap(
    spacing: 8,
    children: [
      if (widget.canCreateRoom)
        OutlinedButton.icon(
          onPressed: _showCreateRoom,
          icon: const Icon(Icons.add),
          label: const Text('新建频道'),
        ),
      if (widget.canManageActiveRoom)
        OutlinedButton.icon(
          onPressed: _showRoomManagement,
          icon: const Icon(Icons.settings),
          label: const Text('管理频道'),
        ),
    ],
  );
}
