import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';

import 'lan_chat_core_bindings.dart';

export 'lan_chat_core_bindings.dart' show LanChatCoreNativeApi;

abstract interface class ChatCore {
  String stateJson();
  int dispatch(String json);
  List<String> drainEvents();
  void dispose();
}

class LanChatCore implements ChatCore {
  LanChatCore._(this._bindings) : _handle = _bindings.create() {
    if (_handle == nullptr) {
      throw StateError('无法创建 LAN Chat 原生核心。');
    }
  }

  factory LanChatCore.open() {
    final executableDirectory = File(Platform.resolvedExecutable).parent;
    return LanChatCore._(
      LanChatCoreBindings(
        DynamicLibrary.open(
          _requireLibrary(
            '${executableDirectory.path}${Platform.pathSeparator}lan_chat_core.dll',
          ),
        ),
      ),
    );
  }

  factory LanChatCore.openForTest() {
    final coreDll = Platform.environment['LAN_CHAT_CORE_DLL'];
    if (coreDll == null || coreDll.isEmpty) {
      throw StateError('LAN_CHAT_CORE_DLL 未设置，无法加载原生核心。');
    }
    final runtimeDirectory = Platform.environment['LAN_CHAT_CORE_RUNTIME_DIR'];
    final libraryPath = runtimeDirectory == null || runtimeDirectory.isEmpty
        ? coreDll
        : '$runtimeDirectory${Platform.pathSeparator}lan_chat_core.dll';
    return LanChatCore._(
      LanChatCoreBindings(DynamicLibrary.open(_requireLibrary(libraryPath))),
    );
  }

  factory LanChatCore.withNativeApiForTest(LanChatCoreNativeApi bindings) =>
      LanChatCore._(bindings);

  final LanChatCoreNativeApi _bindings;
  final Pointer<Void> _handle;
  bool _disposed = false;

  @override
  String stateJson() {
    _ensureOpen();
    return _takeString(_bindings.currentStateJson(_handle)) ??
        (throw StateError('原生核心未返回状态快照。'));
  }

  @override
  int dispatch(String json) {
    _ensureOpen();
    final command = json.toNativeUtf8().cast<Char>();
    try {
      return _bindings.dispatchJson(_handle, command);
    } finally {
      malloc.free(command);
    }
  }

  @override
  List<String> drainEvents() {
    _ensureOpen();
    final events = <String>[];
    while (true) {
      final event = _takeString(_bindings.takeEventJson(_handle));
      if (event == null) return events;
      events.add(event);
    }
  }

  @override
  void dispose() {
    if (_disposed) return;
    _disposed = true;
    _bindings.destroy(_handle);
  }

  String? _takeString(Pointer<Char> pointer) {
    if (pointer == nullptr) return null;
    try {
      return pointer.cast<Utf8>().toDartString();
    } finally {
      _bindings.freeString(pointer);
    }
  }

  void _ensureOpen() {
    if (_disposed) {
      throw StateError('LAN Chat 原生核心已释放。');
    }
  }

  static String _requireLibrary(String path) {
    if (!File(path).existsSync()) {
      throw StateError('lan_chat_core.dll 未找到: $path');
    }
    return path;
  }
}
