#include <windows.h>
#include <shellapi.h>

#include <filesystem>
#include <iostream>
#include <string>

namespace {
constexpr wchar_t kBootstrapScript[] = L"scripts\\bootstrap-github.ps1";

std::filesystem::path executablePath() {
    std::wstring path(MAX_PATH, L'\0');
    const DWORD length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (length == 0 || length == path.size()) {
        return {};
    }
    path.resize(length);
    return std::filesystem::path(path);
}

std::filesystem::path sourceRootFrom(const std::filesystem::path& start) {
    std::filesystem::path cursor = start;
    for (int depth = 0; depth < 10; ++depth) {
        if (std::filesystem::is_regular_file(cursor / kBootstrapScript)) {
            return cursor;
        }
        const std::filesystem::path parent = cursor.parent_path();
        if (parent.empty() || parent == cursor) {
            break;
        }
        cursor = parent;
    }
    return {};
}

std::wstring quoted(const std::filesystem::path& value) {
    return L"\"" + value.wstring() + L"\"";
}

bool hasArgument(int argc, wchar_t* argv[], const std::wstring& value) {
    for (int index = 1; index < argc; ++index) {
        if (std::wstring(argv[index]) == value) {
            return true;
        }
    }
    return false;
}

std::filesystem::path rootOverride(int argc, wchar_t* argv[]) {
    for (int index = 1; index + 1 < argc; ++index) {
        if (std::wstring(argv[index]) == L"--root") {
            return std::filesystem::path(argv[index + 1]);
        }
    }
    return {};
}

int showError(const std::wstring& message) {
    MessageBoxW(nullptr, message.c_str(), L"LAN Chat source launcher", MB_ICONERROR | MB_OK);
    return 1;
}
}

int runLauncher(int argc, wchar_t* argv[]) {
    std::filesystem::path root = rootOverride(argc, argv);
    if (root.empty()) {
        const std::filesystem::path exe = executablePath();
        if (exe.empty()) {
            return showError(L"Cannot locate LANChat-Launcher.exe.");
        }
        root = sourceRootFrom(exe.parent_path());
    }

    const std::filesystem::path script = root / kBootstrapScript;
    if (root.empty() || !std::filesystem::is_regular_file(script)) {
        return showError(L"The launcher must stay inside the LAN Chat source package.\n\n"
                         L"Expected scripts\\bootstrap-github.ps1 beside the source tree.");
    }

    const std::wstring command = L"powershell.exe -NoLogo -NoProfile -ExecutionPolicy Bypass -File " + quoted(script);
    if (hasArgument(argc, argv, L"--dry-run")) {
        std::wcout << command << std::endl;
        return 0;
    }

    SHELLEXECUTEINFOW launch{};
    launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_NOCLOSEPROCESS;
    launch.lpVerb = L"open";
    launch.lpFile = L"powershell.exe";
    const std::wstring parameters = command.substr(std::wstring(L"powershell.exe ").size());
    launch.lpParameters = parameters.c_str();
    const std::wstring workingDirectory = root.wstring();
    launch.lpDirectory = workingDirectory.c_str();
    launch.nShow = SW_SHOWNORMAL;

    if (!ShellExecuteExW(&launch)) {
        return showError(L"Could not start PowerShell. Please run scripts\\bootstrap-github.ps1 manually.");
    }
    if (launch.hProcess != nullptr) {
        CloseHandle(launch.hProcess);
    }
    return 0;
}

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int) {
    int argc = 0;
    wchar_t** argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv == nullptr) {
        return showError(L"Cannot read launcher command line.");
    }
    const int result = runLauncher(argc, argv);
    LocalFree(argv);
    return result;
}
