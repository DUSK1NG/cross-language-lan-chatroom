import 'package:flutter/material.dart';

typedef LocalHostConnectRequested = void Function({
  required String serverExe,
  required String certFile,
  required String keyFile,
  required String dbFile,
  required String username,
  required String userCode,
});

class LocalHostForm extends StatefulWidget {
  const LocalHostForm({
    required this.enabled,
    required this.onConnect,
    super.key,
  });

  final bool enabled;
  final LocalHostConnectRequested onConnect;

  @override
  State<LocalHostForm> createState() => _LocalHostFormState();
}

class _LocalHostFormState extends State<LocalHostForm> {
  final _formKey = GlobalKey<FormState>();
  final _serverExe = TextEditingController();
  final _certFile = TextEditingController();
  final _keyFile = TextEditingController();
  final _dbFile = TextEditingController();
  final _username = TextEditingController();
  final _userCode = TextEditingController();

  @override
  void dispose() {
    _serverExe.dispose();
    _certFile.dispose();
    _keyFile.dispose();
    _dbFile.dispose();
    _username.dispose();
    _userCode.dispose();
    super.dispose();
  }

  @override
  Widget build(BuildContext context) => Form(
    key: _formKey,
    child: Padding(
      padding: const EdgeInsets.all(16),
      child: Column(
        crossAxisAlignment: CrossAxisAlignment.stretch,
        children: [
          Text('创建本地聊天室', style: Theme.of(context).textTheme.titleMedium),
          const SizedBox(height: 12),
          _field(_serverExe, '服务端程序路径'),
          _field(_certFile, '证书文件路径'),
          _field(_keyFile, '私钥文件路径'),
          _field(_dbFile, '数据库文件路径'),
          _field(_username, '用户名'),
          _field(_userCode, '用户代码'),
          const SizedBox(height: 8),
          Align(
            alignment: Alignment.centerLeft,
            child: FilledButton.icon(
              onPressed: widget.enabled ? _submit : null,
              icon: const Icon(Icons.play_arrow),
              label: const Text('创建并连接'),
            ),
          ),
        ],
      ),
    ),
  );

  Widget _field(TextEditingController controller, String label) =>
      TextFormField(
        controller: controller,
        enabled: widget.enabled,
        decoration: InputDecoration(labelText: label),
        validator: (value) =>
            value == null || value.trim().isEmpty ? '请输入$label' : null,
      );

  void _submit() {
    if (!(_formKey.currentState?.validate() ?? false)) return;
    widget.onConnect(
      serverExe: _serverExe.text,
      certFile: _certFile.text,
      keyFile: _keyFile.text,
      dbFile: _dbFile.text,
      username: _username.text,
      userCode: _userCode.text,
    );
  }
}
