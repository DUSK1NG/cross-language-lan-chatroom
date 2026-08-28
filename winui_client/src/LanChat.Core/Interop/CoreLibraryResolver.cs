using System.Reflection;
using System.Runtime.InteropServices;

namespace LanChat.Core.Interop;

internal static class CoreLibraryResolver
{
    private const string LibraryName = "lan_chat_core.dll";
    private static readonly object Gate = new();
    private static string? _installedPath;
    private static nint _libraryHandle;

    internal static void Install(string appDirectory)
    {
        ArgumentException.ThrowIfNullOrWhiteSpace(appDirectory);

        var libraryPath = Path.Combine(Path.GetFullPath(appDirectory), LibraryName);
        lock (Gate)
        {
            if (_installedPath is not null)
            {
                if (string.Equals(_installedPath, libraryPath, StringComparison.OrdinalIgnoreCase))
                {
                    return;
                }

                throw new InvalidOperationException("原生核心 DLL 已从其他路径加载。");
            }

            if (!File.Exists(libraryPath))
            {
                throw new FileNotFoundException($"未找到原生核心 DLL：{libraryPath}", libraryPath);
            }

            _libraryHandle = NativeLibrary.Load(libraryPath);
            NativeLibrary.SetDllImportResolver(typeof(CoreLibraryResolver).Assembly, Resolve);
            _installedPath = libraryPath;
        }
    }

    private static nint Resolve(string libraryName, Assembly assembly, DllImportSearchPath? searchPath) =>
        assembly == typeof(CoreLibraryResolver).Assembly &&
        string.Equals(libraryName, LibraryName, StringComparison.OrdinalIgnoreCase)
            ? _libraryHandle
            : nint.Zero;
}
