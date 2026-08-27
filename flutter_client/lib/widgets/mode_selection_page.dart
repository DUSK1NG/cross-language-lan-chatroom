import 'package:flutter/material.dart';

enum ConnectionMode { remote, localHost, lan }

class ModeSelectionPage extends StatelessWidget {
  const ModeSelectionPage({required this.onSelected, super.key});

  final ValueChanged<ConnectionMode> onSelected;

  @override
  Widget build(BuildContext context) => Center(
    child: ConstrainedBox(
      constraints: const BoxConstraints(maxWidth: 720),
      child: Padding(
        padding: const EdgeInsets.all(24),
        child: Column(
          mainAxisSize: MainAxisSize.min,
          crossAxisAlignment: CrossAxisAlignment.stretch,
          children: [
            Text('选择连接方式', style: Theme.of(context).textTheme.headlineSmall),
            const SizedBox(height: 20),
            _modeCard(
              context,
              title: '远程服务器',
              description: '连接到已知服务器地址。',
              icon: Icons.public,
              mode: ConnectionMode.remote,
            ),
            _modeCard(
              context,
              title: '创建本地聊天室',
              description: '使用本机服务端文件创建聊天室。',
              icon: Icons.add_home_work_outlined,
              mode: ConnectionMode.localHost,
            ),
            _modeCard(
              context,
              title: '加入局域网聊天室',
              description: '搜索并连接局域网中已发现的主机。',
              icon: Icons.lan_outlined,
              mode: ConnectionMode.lan,
            ),
          ],
        ),
      ),
    ),
  );

  Widget _modeCard(
    BuildContext context, {
    required String title,
    required String description,
    required IconData icon,
    required ConnectionMode mode,
  }) => Card(
    child: ListTile(
      leading: Icon(icon),
      title: Text(title),
      subtitle: Text(description),
      trailing: const Icon(Icons.chevron_right),
      onTap: () => onSelected(mode),
    ),
  );
}
