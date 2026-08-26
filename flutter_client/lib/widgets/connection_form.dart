import 'package:flutter/material.dart';

typedef ConnectRequested = void Function({
  required String serverIp,
  required String serverPort,
  required String username,
  required String userCode,
  required String caFile,
  required bool useLocalhostTlsSni,
});

class ConnectionForm extends StatefulWidget {
  const ConnectionForm({
    required this.enabled,
    required this.onConnect,
    super.key,
  });

  final bool enabled;
  final ConnectRequested onConnect;

  @override
  State<ConnectionForm> createState() => _ConnectionFormState();
}

class _ConnectionFormState extends State<ConnectionForm> {
  final _formKey = GlobalKey<FormState>();
  final _serverIp = TextEditingController();
  final _serverPort = TextEditingController(text: '8888');
  final _username = TextEditingController();
  final _userCode = TextEditingController();
  final _caFile = TextEditingController();
  bool _useLocalhostTlsSni = false;

  @override
  void dispose() {
    _serverIp.dispose();
    _serverPort.dispose();
    _username.dispose();
    _userCode.dispose();
    _caFile.dispose();
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
          Text('连接到服务器', style: Theme.of(context).textTheme.titleMedium),
          const SizedBox(height: 12),
          _field(_serverIp, '服务器地址'),
          _field(_serverPort, '端口', keyboardType: TextInputType.number),
          _field(_username, '用户名'),
          _field(_userCode, '用户代码'),
          TextFormField(
            controller: _caFile,
            enabled: widget.enabled,
            decoration: const InputDecoration(labelText: '公共 CA 文件路径'),
          ),
          CheckboxListTile(
            contentPadding: EdgeInsets.zero,
            title: const Text('使用 localhost TLS SNI'),
            value: _useLocalhostTlsSni,
            onChanged: widget.enabled
                ? (value) =>
                      setState(() => _useLocalhostTlsSni = value ?? false)
                : null,
          ),
          Align(
            alignment: Alignment.centerLeft,
            child: FilledButton.icon(
              onPressed: widget.enabled ? _submit : null,
              icon: const Icon(Icons.login),
              label: const Text('连接到服务器'),
            ),
          ),
        ],
      ),
    ),
  );

  Widget _field(
    TextEditingController controller,
    String label, {
    TextInputType? keyboardType,
  }) => TextFormField(
    controller: controller,
    enabled: widget.enabled,
    keyboardType: keyboardType,
    decoration: InputDecoration(labelText: label),
    validator: (value) =>
        value == null || value.trim().isEmpty ? '请输入$label' : null,
  );

  void _submit() {
    if (!(_formKey.currentState?.validate() ?? false)) return;
    final port = int.tryParse(_serverPort.text.trim());
    if (port == null || port < 1 || port > 65535) {
      ScaffoldMessenger.of(context)
          .showSnackBar(const SnackBar(content: Text('端口必须是 1 到 65535 的整数')));
      return;
    }
    widget.onConnect(
      serverIp: _serverIp.text,
      serverPort: _serverPort.text,
      username: _username.text,
      userCode: _userCode.text,
      caFile: _caFile.text,
      useLocalhostTlsSni: _useLocalhostTlsSni,
    );
  }
}
