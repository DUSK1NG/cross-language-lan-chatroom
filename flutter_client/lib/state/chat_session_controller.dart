import 'dart:async';
import 'dart:convert';

import 'package:flutter/widgets.dart';

import '../models/bridge_state.dart';
import '../native/lan_chat_core.dart';

class ChatSessionController extends ChangeNotifier with WidgetsBindingObserver {
  ChatSessionController(this._core, {bool startPolling = true}) {
    WidgetsBinding.instance.addObserver(this);
    if (startPolling) {
      _timer = Timer.periodic(const Duration(milliseconds: 100), (_) {
        if (_resumed) drainPendingEvents();
      });
    }
  }

  final ChatCore _core;
  BridgeState _state = BridgeState.idle();
  Timer? _timer;
  bool _resumed = true;
  int _commandCounter = 0;
  String? _localError;

  String get connectionPhase => _state.connectionPhase;
  String get statusText => _state.statusText;
  List<BridgeConversation> get conversations => _state.conversations;
  List<BridgeMessage> get messages => _state.messages;
  String get selectedRoom => _state.selectedRoom;
  bool get isLanDiscoveryScanning => _state.isLanDiscoveryScanning;
  List<BridgeDiscoveredHost> get discoveredHosts => _state.discoveredHosts;
  String? get lastError => _localError ?? _state.lastError;
  bool get isConnected => connectionPhase == 'connected';

  void refresh() => _readSnapshot(notify: true);

  void drainPendingEvents() {
    try {
      final events = _core.drainEvents();
      if (events.isEmpty) return;
      var hasStateEvent = false;
      String? bridgeError;
      for (final eventJson in events) {
        final decoded = jsonDecode(eventJson);
        if (decoded is! Map) continue;
        final event = decoded.map(
          (key, value) => MapEntry(key.toString(), value),
        );
        final kind = event['kind'] is String
            ? event['kind'] as String
            : event['type'] as String?;
        if (kind == 'state') {
          hasStateEvent = true;
          continue;
        }
        final payload = event['payload'];
        if (payload is! Map) continue;
        final details = payload.map(
          (key, value) => MapEntry(key.toString(), value),
        );
        final error = kind == 'result' && details['ok'] == false
            ? details['error']
            : kind == 'error'
            ? details
            : null;
        if (error is Map && error['message'] is String) {
          final message = (error['message'] as String).trim();
          if (message.isNotEmpty) bridgeError = message;
        }
      }
      if (hasStateEvent) _readSnapshot(notify: false);
      if (bridgeError != null) _localError = bridgeError;
      notifyListeners();
    } catch (error) {
      _setError(error);
    }
  }

  void sendMessage(String text) {
    final content = text.trim();
    if (content.isEmpty) {
      _localError = '消息不能为空';
      notifyListeners();
      return;
    }
    if (!isConnected) {
      _localError = '当前未连接，无法发送消息';
      notifyListeners();
      return;
    }
    _dispatch('chat.sendRoom', {
      'content': content,
      'room': _state.selectedRoom,
    });
  }

  bool connectRemote({
    required String serverIp,
    required String serverPort,
    required String username,
    required String userCode,
    String caFile = '',
    bool useLocalhostTlsSni = false,
  }) {
    final address = serverIp.trim();
    final port = int.tryParse(serverPort.trim());
    final name = username.trim();
    final code = userCode.trim();
    if (address.isEmpty || name.isEmpty || code.isEmpty) {
      _localError = '服务器地址、用户名和用户代码不能为空';
      notifyListeners();
      return false;
    }
    if (port == null || port < 1 || port > 65535) {
      _localError = '端口必须是 1 到 65535 的整数';
      notifyListeners();
      return false;
    }
    _dispatch('session.connectRemote', {
      'serverIp': address,
      'serverPort': port,
      'username': name,
      'userCode': code,
      'caFile': caFile.trim(),
      if (useLocalhostTlsSni) 'tlsServerName': 'localhost',
    });
    return true;
  }

  bool connectLocalHost({
    required String serverExe,
    required String certFile,
    required String keyFile,
    required String dbFile,
    required String username,
    required String userCode,
  }) {
    final executable = serverExe.trim();
    final certificate = certFile.trim();
    final key = keyFile.trim();
    final database = dbFile.trim();
    final name = username.trim();
    final code = userCode.trim();
    if ([
      executable,
      certificate,
      key,
      database,
      name,
      code,
    ].any((value) => value.isEmpty)) {
      _localError = '本地主机配置、用户名和用户代码不能为空';
      notifyListeners();
      return false;
    }
    _dispatch('session.connectLocalHost', {
      'serverExe': executable,
      'certFile': certificate,
      'keyFile': key,
      'dbFile': database,
      'username': name,
      'userCode': code,
    });
    return true;
  }

  void discoverLanHosts() => _dispatch('session.discoverLanHosts', const {});

  void connectDiscoveredHost(
    String hostId, {
    required String username,
    required String userCode,
  }) {
    _dispatch('session.connectDiscoveredHost', {
      'hostId': hostId.trim(),
      'username': username.trim(),
      'userCode': userCode.trim(),
    });
  }

  void selectConversation(BridgeConversation conversation) {
    _dispatch(
      conversation.kind == 'dm'
          ? 'conversation.selectDirect'
          : 'conversation.selectRoom',
      conversation.kind == 'dm'
          ? {'userCode': conversation.id}
          : {'room': conversation.id},
    );
  }

  void disconnect() => _dispatch('session.disconnect', const {});

  void _dispatch(String type, Map<String, dynamic> payload) {
    try {
      final result = _core.dispatch(
        jsonEncode({
          'id': 'flutter-${++_commandCounter}',
          'type': type,
          'payload': payload,
        }),
      );
      if (result != 0) {
        _localError = '原生核心拒绝了请求（$result）';
      } else {
        _localError = null;
      }
    } catch (error) {
      _setError(error, notify: false);
    }
    notifyListeners();
  }

  void _readSnapshot({required bool notify}) {
    try {
      final decoded = jsonDecode(_core.stateJson());
      if (decoded is! Map<String, dynamic>) {
        throw const FormatException('LAN Chat 状态不是对象');
      }
      _state = BridgeState.fromJson(decoded);
      _localError = null;
    } catch (error) {
      _setError(error, notify: false);
    }
    if (notify) notifyListeners();
  }

  void _setError(Object error, {bool notify = true}) {
    _localError = '无法读取 LAN Chat 状态：$error';
    if (notify) notifyListeners();
  }

  @override
  void didChangeAppLifecycleState(AppLifecycleState state) {
    _resumed = state == AppLifecycleState.resumed;
    if (_resumed) drainPendingEvents();
  }

  @override
  void dispose() {
    WidgetsBinding.instance.removeObserver(this);
    _timer?.cancel();
    _core.dispose();
    super.dispose();
  }
}
