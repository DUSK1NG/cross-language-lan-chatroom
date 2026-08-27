class BridgeConversation {
  const BridgeConversation({
    required this.kind,
    required this.id,
    required this.title,
    required this.unreadCount,
    required this.memberCount,
  });

  final String kind;
  final String id;
  final String title;
  final int unreadCount;
  final int memberCount;
}

class BridgeMessage {
  const BridgeMessage({
    required this.sender,
    required this.content,
    required this.time,
    required this.isSelf,
    required this.isSystem,
  });

  final String sender;
  final String content;
  final String time;
  final bool isSelf;
  final bool isSystem;
}

class BridgeDiscoveredHost {
  const BridgeDiscoveredHost({
    required this.id,
    required this.hostName,
    required this.serverIp,
    required this.serverPort,
    required this.isKnown,
  });

  final String id;
  final String hostName;
  final String serverIp;
  final int serverPort;
  final bool isKnown;
}

class BridgeState {
  const BridgeState({
    required this.connectionPhase,
    required this.statusText,
    required this.conversations,
    required this.messages,
    required this.selectedRoom,
    required this.isLanDiscoveryScanning,
    required this.discoveredHosts,
    this.lastError,
  });

  factory BridgeState.idle() => const BridgeState(
    connectionPhase: 'idle',
    statusText: '未连接',
    conversations: [],
    messages: [],
    selectedRoom: 'lobby',
    isLanDiscoveryScanning: false,
    discoveredHosts: [],
  );

  factory BridgeState.fromJson(Map<String, dynamic> json) {
    if (json['schemaVersion'] != 1) {
      throw const FormatException('不支持的 LAN Chat 状态版本');
    }
    final connection = _map(json['connection']);
    final navigation = _map(json['navigation']);
    final activeConversation = _map(navigation['activeConversation']);
    final rooms = _list(json['rooms']).map((value) {
      final room = _map(value);
      final name = _string(room['roomName'], 'lobby');
      return BridgeConversation(
        kind: 'room',
        id: name,
        title: name,
        unreadCount: _int(room['unreadCount']),
        memberCount: _int(room['memberCount']),
      );
    });
    final directMessages = _list(json['directMessages']).map((value) {
      final directMessage = _map(value);
      final userCode = _string(directMessage['userCode']);
      return BridgeConversation(
        kind: 'dm',
        id: userCode,
        title: _string(directMessage['displayName'], userCode),
        unreadCount: _int(directMessage['unreadCount']),
        memberCount: 0,
      );
    });
    final lanDiscovery = _map(json['lanDiscovery']);
    final discoveredHosts = _list(lanDiscovery['hosts'])
        .map(_discoveredHostFromJson)
        .whereType<BridgeDiscoveredHost>()
        .toList(growable: false);
    final error = _map(connection['lastError']);

    return BridgeState(
      connectionPhase: _string(connection['phase'], 'idle'),
      statusText: _string(connection['statusText'], '未连接'),
      conversations: [...rooms, ...directMessages],
      messages: _list(json['activeMessages'])
          .map((value) {
            final message = _map(value);
            return BridgeMessage(
              sender: _string(
                message['displayName'],
                _string(message['sender'], '系统'),
              ),
              content: _string(message['content']),
              time: _string(message['time']),
              isSelf: message['selfMessage'] == true,
              isSystem: message['systemMessage'] == true,
            );
          })
          .toList(growable: false),
      selectedRoom: _string(
        activeConversation['name'],
        _string(
          activeConversation['id'],
          _string(activeConversation['title'], 'lobby'),
        ),
      ),
      isLanDiscoveryScanning: lanDiscovery['scanning'] == true,
      discoveredHosts: discoveredHosts,
      lastError: _string(error['message']).isEmpty
          ? null
          : _string(error['message']),
    );
  }

  final String connectionPhase;
  final String statusText;
  final List<BridgeConversation> conversations;
  final List<BridgeMessage> messages;
  final String selectedRoom;
  final bool isLanDiscoveryScanning;
  final List<BridgeDiscoveredHost> discoveredHosts;
  final String? lastError;

  static Map<String, dynamic> _map(Object? value) => value is Map
      ? value.map((key, nestedValue) => MapEntry(key.toString(), nestedValue))
      : const {};

  static List<Object?> _list(Object? value) => value is List ? value : const [];

  static String _string(Object? value, [String fallback = '']) =>
      value is String ? value : fallback;

  static int _int(Object? value) => value is num ? value.toInt() : 0;

  static BridgeDiscoveredHost? _discoveredHostFromJson(Object? value) {
    final host = _map(value);
    final id = _string(host['id']);
    final hostName = _string(host['hostName']);
    final serverIp = _string(host['serverIp']);
    final serverPort = _int(host['serverPort']);
    if (id.isEmpty || hostName.isEmpty || serverIp.isEmpty || serverPort < 1) {
      return null;
    }
    return BridgeDiscoveredHost(
      id: id,
      hostName: hostName,
      serverIp: serverIp,
      serverPort: serverPort,
      isKnown: host['known'] == true,
    );
  }
}
