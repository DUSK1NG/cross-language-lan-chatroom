using System.ComponentModel;
using System.Diagnostics;
using System.Runtime.InteropServices;
using Microsoft.UI.Xaml;
using WinRT.Interop;

namespace LanChat_WinUI;

internal sealed class WindowMinimumSizeController : IDisposable
{
    private const uint WmGetMinMaxInfo = 0x0024;
    private const uint WmNcDestroy = 0x0082;
    private const uint DefaultDpi = 96;

    private static long s_nextSubclassId;

    private readonly nint _windowHandle;
    private readonly int _minimumWidth;
    private readonly int _minimumHeight;
    private readonly nuint _subclassId;
    private readonly SubclassProcedure _subclassProcedure;
    private bool _isInstalled;

    public WindowMinimumSizeController(Window window, int minimumWidth, int minimumHeight)
    {
        ArgumentNullException.ThrowIfNull(window);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(minimumWidth);
        ArgumentOutOfRangeException.ThrowIfNegativeOrZero(minimumHeight);

        _windowHandle = WindowNative.GetWindowHandle(window);
        if (_windowHandle == nint.Zero)
        {
            throw new InvalidOperationException("无法获取 WinUI 窗口句柄。");
        }

        _minimumWidth = minimumWidth;
        _minimumHeight = minimumHeight;
        _subclassId = unchecked((nuint)Interlocked.Increment(ref s_nextSubclassId));
        _subclassProcedure = ProcessWindowMessage;

        Marshal.SetLastPInvokeError(0);
        if (!SetWindowSubclass(_windowHandle, _subclassProcedure, _subclassId, 0))
        {
            throw CreateWin32Exception("无法安装 WinUI 最小尺寸窗口子类。");
        }

        _isInstalled = true;
    }

    public void Dispose()
    {
        if (!_isInstalled)
        {
            return;
        }

        Marshal.SetLastPInvokeError(0);
        if (!RemoveWindowSubclass(_windowHandle, _subclassProcedure, _subclassId))
        {
            throw CreateWin32Exception("无法卸载 WinUI 最小尺寸窗口子类。");
        }

        _isInstalled = false;
        GC.KeepAlive(_subclassProcedure);
    }

    private nint ProcessWindowMessage(
        nint windowHandle,
        uint message,
        nuint wParam,
        nint lParam,
        nuint subclassId,
        nuint referenceData)
    {
        try
        {
            if (message == WmGetMinMaxInfo)
            {
                ApplyMinimumSize(windowHandle, lParam);
            }
            else if (message == WmNcDestroy)
            {
                Marshal.SetLastPInvokeError(0);
                if (RemoveWindowSubclass(windowHandle, _subclassProcedure, subclassId))
                {
                    _isInstalled = false;
                }
                else
                {
                    Debug.WriteLine(CreateWin32Exception("WM_NCDESTROY 无法卸载 WinUI 最小尺寸窗口子类。"));
                }
            }
        }
        catch (Exception exception)
        {
            Debug.WriteLine($"WinUI 最小尺寸消息处理失败: {exception}");
        }

        return DefSubclassProc(windowHandle, message, wParam, lParam);
    }

    private void ApplyMinimumSize(nint windowHandle, nint minMaxInfoAddress)
    {
        if (minMaxInfoAddress == nint.Zero)
        {
            throw new InvalidOperationException("WM_GETMINMAXINFO 未提供 MINMAXINFO。");
        }

        Marshal.SetLastPInvokeError(0);
        var dpi = GetDpiForWindow(windowHandle);
        if (dpi == 0)
        {
            throw CreateWin32Exception("无法读取 WinUI 窗口 DPI。");
        }

        var minMaxInfo = Marshal.PtrToStructure<MinMaxInfo>(minMaxInfoAddress);
        minMaxInfo.MinTrackSize.X = Math.Max(
            minMaxInfo.MinTrackSize.X,
            ScaleForDpi(_minimumWidth, dpi));
        minMaxInfo.MinTrackSize.Y = Math.Max(
            minMaxInfo.MinTrackSize.Y,
            ScaleForDpi(_minimumHeight, dpi));
        Marshal.StructureToPtr(minMaxInfo, minMaxInfoAddress, false);
    }

    private static int ScaleForDpi(int effectivePixels, uint dpi) =>
        checked((int)Math.Ceiling(effectivePixels * dpi / (double)DefaultDpi));

    private static Win32Exception CreateWin32Exception(string message)
    {
        var error = Marshal.GetLastPInvokeError();
        return new Win32Exception(error, $"{message} Win32 错误 {error}: {new Win32Exception(error).Message}");
    }

    [UnmanagedFunctionPointer(CallingConvention.Winapi)]
    private delegate nint SubclassProcedure(
        nint windowHandle,
        uint message,
        nuint wParam,
        nint lParam,
        nuint subclassId,
        nuint referenceData);

    [StructLayout(LayoutKind.Sequential)]
    private struct NativePoint
    {
        public int X;
        public int Y;
    }

    [StructLayout(LayoutKind.Sequential)]
    private struct MinMaxInfo
    {
        public NativePoint Reserved;
        public NativePoint MaxSize;
        public NativePoint MaxPosition;
        public NativePoint MinTrackSize;
        public NativePoint MaxTrackSize;
    }

    [DllImport("comctl32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool SetWindowSubclass(
        nint windowHandle,
        SubclassProcedure subclassProcedure,
        nuint subclassId,
        nuint referenceData);

    [DllImport("comctl32.dll", SetLastError = true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    private static extern bool RemoveWindowSubclass(
        nint windowHandle,
        SubclassProcedure subclassProcedure,
        nuint subclassId);

    [DllImport("comctl32.dll")]
    private static extern nint DefSubclassProc(
        nint windowHandle,
        uint message,
        nuint wParam,
        nint lParam);

    [DllImport("user32.dll", SetLastError = true)]
    private static extern uint GetDpiForWindow(nint windowHandle);
}
