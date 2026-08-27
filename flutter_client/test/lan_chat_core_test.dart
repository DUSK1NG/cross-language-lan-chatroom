import 'dart:ffi';
import 'dart:io';

import 'package:ffi/ffi.dart';
import 'package:flutter_test/flutter_test.dart';
import 'package:lan_chat_flutter/native/lan_chat_core.dart';

void main() {
  test('creates and destroys the native core exactly once', () {
    final native = _FakeNativeApi();
    final core = LanChatCore.withNativeApiForTest(native);

    expect(native.createCalls, 1);
    core.dispose();
    core.dispose();

    expect(native.destroyCalls, 1);
  });

  test('dispatches JSON and releases every returned native string once', () {
    final native = _FakeNativeApi(
      state: '{"schemaVersion":1}',
      events: ['{"type":"connected"}', '{"type":"message"}'],
    );
    final core = LanChatCore.withNativeApiForTest(native);
    addTearDown(core.dispose);

    expect(core.dispatch('{"type":"session.connectRemote"}'), 0);
    expect(native.lastDispatch, '{"type":"session.connectRemote"}');
    expect(core.stateJson(), '{"schemaVersion":1}');
    expect(core.drainEvents(), ['{"type":"connected"}', '{"type":"message"}']);

    expect(native.freeCalls, 3);
    expect(native.freedAddresses.length, 3);
  });

  test(
    'reads a schema-versioned initial snapshot from the real core',
    () {
      final core = LanChatCore.openForTest();
      addTearDown(core.dispose);

      expect(core.stateJson(), contains('"schemaVersion":1'));
    },
    skip: Platform.environment['LAN_CHAT_CORE_DLL'] == null
        ? 'Set LAN_CHAT_CORE_DLL to run against the built native core.'
        : false,
  );
}

class _FakeNativeApi implements LanChatCoreNativeApi {
  _FakeNativeApi({
    this.state = '{"schemaVersion":1}',
    List<String> events = const [],
  }) : _events = List.of(events);

  final Pointer<Void> _handle = calloc<Uint8>(1).cast<Void>();
  final String state;
  final List<String> _events;
  final Set<int> freedAddresses = {};
  int createCalls = 0;
  int destroyCalls = 0;
  int freeCalls = 0;
  String? lastDispatch;

  @override
  Pointer<Void> create() {
    createCalls += 1;
    return _handle;
  }

  @override
  void destroy(Pointer<Void> handle) {
    expect(handle, _handle);
    destroyCalls += 1;
  }

  @override
  int dispatchJson(Pointer<Void> handle, Pointer<Char> commandJson) {
    expect(handle, _handle);
    lastDispatch = commandJson.cast<Utf8>().toDartString();
    return 0;
  }

  @override
  Pointer<Char> currentStateJson(Pointer<Void> handle) {
    expect(handle, _handle);
    return state.toNativeUtf8().cast<Char>();
  }

  @override
  Pointer<Char> takeEventJson(Pointer<Void> handle) {
    expect(handle, _handle);
    if (_events.isEmpty) return nullptr.cast<Char>();
    return _events.removeAt(0).toNativeUtf8().cast<Char>();
  }

  @override
  void freeString(Pointer<Char> value) {
    freeCalls += 1;
    expect(freedAddresses.add(value.address), isTrue);
  }
}
