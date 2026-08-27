import 'package:flutter/material.dart';

import '../models/bridge_state.dart';

class AdminActions extends StatelessWidget {
  const AdminActions({
    required this.isAllowed,
    required this.members,
    required this.connectionApprovals,
    required this.onAction,
    required this.onApproveConnection,
    required this.onDenyConnection,
    super.key,
  });

  final bool isAllowed;
  final List<BridgeMember> members;
  final List<BridgeConnectionApproval> connectionApprovals;
  final void Function(String action, String userCode) onAction;
  final void Function(String messageId) onApproveConnection;
  final void Function(String messageId) onDenyConnection;

  @override
  Widget build(BuildContext context) {
    if (!isAllowed) return const SizedBox.shrink();
    return IconButton(
      tooltip: '管理成员',
      icon: const Icon(Icons.admin_panel_settings_outlined),
      onPressed: () => showModalBottomSheet<void>(
        context: context,
        builder: (context) => SafeArea(
          child: ListView(
            shrinkWrap: true,
            children: [
              const ListTile(title: Text('成员管理')),
              if (connectionApprovals.isNotEmpty) ...[
                const ListTile(title: Text('连接审批')),
                for (final approval in connectionApprovals)
                  ListTile(
                    title: Text(approval.displayName),
                    subtitle: Column(
                      crossAxisAlignment: CrossAxisAlignment.start,
                      children: [
                        Text(approval.userCode),
                        if (approval.requestedAt.isNotEmpty)
                          Text(approval.requestedAt),
                      ],
                    ),
                    trailing: Wrap(
                      spacing: 4,
                      children: [
                        IconButton(
                          tooltip: '批准连接 ${approval.id}',
                          icon: const Icon(Icons.check_circle_outline),
                          onPressed: () => onApproveConnection(approval.id),
                        ),
                        IconButton(
                          tooltip: '拒绝连接 ${approval.id}',
                          icon: const Icon(Icons.cancel_outlined),
                          onPressed: () => onDenyConnection(approval.id),
                        ),
                      ],
                    ),
                  ),
              ],
              for (final member in members)
                ListTile(
                  title: Text(member.displayName),
                  subtitle: Text(member.userCode),
                  trailing: Wrap(
                    spacing: 4,
                    children: [
                      IconButton(
                        tooltip: '禁言或解禁 ${member.userCode}',
                        icon: const Icon(Icons.volume_off_outlined),
                        onPressed: () => onAction('mute', member.userCode),
                      ),
                      IconButton(
                        tooltip: '踢出 ${member.userCode}',
                        icon: const Icon(Icons.person_remove_outlined),
                        onPressed: () => onAction('kick', member.userCode),
                      ),
                    ],
                  ),
                ),
            ],
          ),
        ),
      ),
    );
  }
}
