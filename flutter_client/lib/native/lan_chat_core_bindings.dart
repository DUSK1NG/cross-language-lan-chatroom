import 'dart:ffi';

typedef _CreateNative = Pointer<Void> Function();
typedef _CreateDart = Pointer<Void> Function();
typedef _DestroyNative = Void Function(Pointer<Void>);
typedef _DestroyDart = void Function(Pointer<Void>);
typedef _DispatchNative = Int32 Function(Pointer<Void>, Pointer<Char>);
typedef _DispatchDart = int Function(Pointer<Void>, Pointer<Char>);
typedef _ReadStringNative = Pointer<Char> Function(Pointer<Void>);
typedef _ReadStringDart = Pointer<Char> Function(Pointer<Void>);
typedef _FreeStringNative = Void Function(Pointer<Char>);
typedef _FreeStringDart = void Function(Pointer<Char>);

abstract interface class LanChatCoreNativeApi {
  Pointer<Void> create();
  void destroy(Pointer<Void> handle);
  int dispatchJson(Pointer<Void> handle, Pointer<Char> commandJson);
  Pointer<Char> currentStateJson(Pointer<Void> handle);
  Pointer<Char> takeEventJson(Pointer<Void> handle);
  void freeString(Pointer<Char> value);
}

class LanChatCoreBindings implements LanChatCoreNativeApi {
  LanChatCoreBindings(DynamicLibrary library)
    : _create = library.lookupFunction<_CreateNative, _CreateDart>(
        'lan_chat_core_create',
      ),
      _destroy = library.lookupFunction<_DestroyNative, _DestroyDart>(
        'lan_chat_core_destroy',
      ),
      _dispatch = library.lookupFunction<_DispatchNative, _DispatchDart>(
        'lan_chat_core_dispatch_json',
      ),
      _currentState = library
          .lookupFunction<_ReadStringNative, _ReadStringDart>(
            'lan_chat_core_current_state_json',
          ),
      _takeEvent = library.lookupFunction<_ReadStringNative, _ReadStringDart>(
        'lan_chat_core_take_event_json',
      ),
      _freeString = library.lookupFunction<_FreeStringNative, _FreeStringDart>(
        'lan_chat_core_free_string',
      );

  final _CreateDart _create;
  final _DestroyDart _destroy;
  final _DispatchDart _dispatch;
  final _ReadStringDart _currentState;
  final _ReadStringDart _takeEvent;
  final _FreeStringDart _freeString;

  @override
  Pointer<Void> create() => _create();

  @override
  void destroy(Pointer<Void> handle) => _destroy(handle);

  @override
  int dispatchJson(Pointer<Void> handle, Pointer<Char> commandJson) =>
      _dispatch(handle, commandJson);

  @override
  Pointer<Char> currentStateJson(Pointer<Void> handle) => _currentState(handle);

  @override
  Pointer<Char> takeEventJson(Pointer<Void> handle) => _takeEvent(handle);

  @override
  void freeString(Pointer<Char> value) => _freeString(value);
}
