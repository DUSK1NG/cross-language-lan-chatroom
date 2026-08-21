# React WebEngine dependency check

The approved toolchain choice is Qt 6.11.2 MSVC 2022 x64, because the official Qt package source does not provide a Qt 6.11.2 Windows MinGW WebEngine binary.

Verified local components:

- Qt prefix: `C:\Qt\6.11.2\msvc2022_64`
- CMake packages: `Qt6WebEngineWidgets`, `Qt6WebEngineCore`, `Qt6WebChannel`, and `Qt6Positioning`
- MSVC compiler: Visual Studio 2022 Build Tools, MSVC 14.44.35207
- Qt WebEngine/WebChannel runtime DLLs exist under the selected Qt prefix
- MSVC OpenSSL 3.6.0 Debug package is staged in the temporary vcpkg prefix used for local verification

The original MinGW QML build remains unchanged. Web UI compilation is opt-in through `LAN_CHAT_ENABLE_WEB_UI=ON`.
