# React WebEngine migration checks

This document records the repeatable local checks for the optional React UI. The default GUI build remains the QML path; the WebEngine path is enabled explicitly.

## Frontend

From `C:\Users\jking1\Desktop\my-project\chat_X`:

```powershell
$env:PATH = "C:\Users\jking1\Desktop\my-project\chat_X\.tools\node-v24.19.0-win-x64;$env:PATH"
& .\.tools\node-v24.19.0-win-x64\npm.cmd --prefix frontend test -- --run
& .\.tools\node-v24.19.0-win-x64\npm.cmd --prefix frontend run build
```

Expected result: all frontend test files pass and `frontend/dist/index.html` plus its hashed JS/CSS assets are generated.

## Optional WebEngine/MSVC build

The tested toolchain is Qt 6.11.2 MSVC 2022 x64, Visual Studio Build Tools 2022 x64, and the staged OpenSSL prefix below. Run CMake from a Visual Studio developer environment:

```powershell
cmd.exe /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" -S .\client-cpp\gui -B .\client-cpp\gui\build-webengine-msvc-ninja2 -G Ninja -DCMAKE_C_COMPILER=cl.exe -DCMAKE_CXX_COMPILER=cl.exe -DCMAKE_PREFIX_PATH=C:\Qt\6.11.2\msvc2022_64 -DQt6_DIR=C:\Qt\6.11.2\msvc2022_64\lib\cmake\Qt6 -DOPENSSL_ROOT_DIR=C:\Users\jking1\AppData\Local\Temp\lan-chat-vcpkg\vcpkg_installed\vcpkg\pkgs\openssl_x64-windows -DLAN_CHAT_ENABLE_WEB_UI=ON'
```

```powershell
cmd.exe /d /s /c 'call "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\VsDevCmd.bat" -arch=x64 -host_arch=x64 && "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" --build .\client-cpp\gui\build-webengine-msvc-ninja2 --target lan-chat-gui web-ui-host-tests -j 2'
$env:PATH = "C:\Qt\6.11.2\msvc2022_64\bin;C:\Users\jking1\AppData\Local\Temp\lan-chat-vcpkg\vcpkg_installed\vcpkg\pkgs\openssl_x64-windows\debug\bin;$env:PATH"
ctest --test-dir .\client-cpp\gui\build-webengine-msvc-ninja2 -R "^web-ui-host-tests$" --timeout 20 --output-on-failure
```

The generated `lan-chat-gui_autogen/*/qrc_frontend.cpp` should contain `:/frontend/index.html` and the current hashed JS/CSS paths. The production WebEngine UI can be selected with `--web-ui-dev` only for a loopback Vite server; the default WebEngine launch loads the qrc release page.

## Default QML regression

The QML path is built with the existing MinGW configuration and its runtime DLL directories must be on `PATH` when launching tests:

```powershell
$env:PATH = "C:\Qt\6.11.2\mingw_64\bin;C:\msys64\mingw64\bin;$env:PATH"
cmake --build .\client-cpp\gui\build-bridge --target lan-chat-gui chat-bridge-tests
ctest --test-dir .\client-cpp\gui\build-bridge -R "^(bridge-protocol-tests|chat-bridge-tests)$" --timeout 20 --output-on-failure
```

## Scope boundary

These checks verify the bridge seam, resource embedding, frontend behavior, and both GUI build paths. They do not replace the physical LAN two-client TLS acceptance run; that remains a separate Windows acceptance step.
