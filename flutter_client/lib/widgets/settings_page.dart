import 'package:flutter/material.dart';

class SettingsPage extends StatelessWidget {
  const SettingsPage({
    required this.performanceMode,
    required this.connectionLoggingEnabled,
    required this.onPerformanceModeChanged,
    required this.onConnectionLoggingChanged,
    required this.onBack,
    super.key,
  });

  final String performanceMode;
  final bool connectionLoggingEnabled;
  final ValueChanged<String> onPerformanceModeChanged;
  final ValueChanged<bool> onConnectionLoggingChanged;
  final VoidCallback onBack;

  static const _performanceModes = <String, String>{
    'Automatic': '自动',
    'High': '高性能',
    'Balanced': '均衡',
    'Power Saving': '省电',
  };

  @override
  Widget build(BuildContext context) => Center(
    child: ConstrainedBox(
      constraints: const BoxConstraints(maxWidth: 560),
      child: ListView(
        padding: const EdgeInsets.all(24),
        children: [
          Row(
            children: [
              IconButton(
                tooltip: '返回聊天',
                onPressed: onBack,
                icon: const Icon(Icons.arrow_back),
              ),
              const SizedBox(width: 8),
              Text('设置', style: Theme.of(context).textTheme.headlineSmall),
            ],
          ),
          const SizedBox(height: 24),
          Text('性能', style: Theme.of(context).textTheme.titleMedium),
          const SizedBox(height: 8),
          Semantics(
            label: '性能等级',
            button: true,
            child: DropdownButton<String>(
              value: _performanceModes.containsKey(performanceMode)
                  ? performanceMode
                  : 'Automatic',
              isExpanded: true,
              items: _performanceModes.entries
                  .map(
                    (entry) => DropdownMenuItem<String>(
                      value: entry.key,
                      child: Text(entry.value),
                    ),
                  )
                  .toList(growable: false),
              onChanged: (value) {
                if (value != null) onPerformanceModeChanged(value);
              },
            ),
          ),
          const SizedBox(height: 24),
          Text('连接日志', style: Theme.of(context).textTheme.titleMedium),
          SwitchListTile(
            contentPadding: EdgeInsets.zero,
            title: const Text('记录连接日志'),
            value: connectionLoggingEnabled,
            onChanged: onConnectionLoggingChanged,
          ),
          const Text('仅记录连接元数据；不记录消息、证书或私钥内容。'),
        ],
      ),
    ),
  );
}
