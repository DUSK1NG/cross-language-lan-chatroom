using System.ComponentModel;
using System.Diagnostics;
using LanChat.Core;
using LanChat.Core.Runtime;
using LanChat.Presentation;
using Microsoft.UI.Windowing;
using Microsoft.UI.Xaml;
using Microsoft.UI.Xaml.Automation;

namespace LanChat_WinUI;

public sealed partial class MainWindow : Window
{
    private static readonly TimeSpan DisposeTimeout = TimeSpan.FromSeconds(5);
    private readonly WindowMinimumSizeController _minimumSizeController;
    private bool _startRequested;
    private bool _started;
    private bool _isActive;
    private bool _isClosing;
    private bool _allowClose;
    private bool _isClosed;

    public MainWindow()
    {
        var runtime = new CoreRuntime(() => LanChatCoreClient.Open(AppContext.BaseDirectory));
        ViewModel = new ShellViewModel(runtime, new DispatcherQueueAdapter(DispatcherQueue));

        InitializeComponent();

        ExtendsContentIntoTitleBar = true;
        SetTitleBar(AppTitleBar);
        AppWindow.SetIcon("Assets/AppIcon.ico");
        _minimumSizeController = new WindowMinimumSizeController(this, 900, 620);
        AppWindow.Resize(_minimumSizeController.ScaleToPhysicalSize(1100, 720));

        ViewModel.PropertyChanged += OnViewModelPropertyChanged;
        Activated += OnActivated;
        AppWindow.Closing += OnAppWindowClosing;
        Closed += OnClosed;
        UpdateProgressRingState();
    }

    public ShellViewModel ViewModel { get; }

    private async void OnActivated(object sender, WindowActivatedEventArgs args)
    {
        try
        {
            _isActive = args.WindowActivationState != WindowActivationState.Deactivated;
            if (!_isActive)
            {
                if (_started)
                {
                    ViewModel.SetActive(false);
                }

                return;
            }

            if (!_startRequested)
            {
                _startRequested = true;
                await ViewModel.StartAsync();
                _started = !ViewModel.HasFatalError;
            }

            if (_isActive && _started)
            {
                ViewModel.SetActive(true);
            }
        }
        catch (ObjectDisposedException) when (_isClosing || _isClosed)
        {
        }
        catch (Exception exception)
        {
            Debug.WriteLine($"WinUI 生命周期处理失败: {exception}");
        }
    }

    private void OnViewModelPropertyChanged(object? sender, PropertyChangedEventArgs args)
    {
        if (string.IsNullOrEmpty(args.PropertyName) ||
            args.PropertyName == nameof(ShellViewModel.ConnectionPhase) ||
            args.PropertyName == nameof(ShellViewModel.HasFatalError))
        {
            UpdateProgressRingState();
        }
    }

    private void UpdateProgressRingState()
    {
        var isStarting = ViewModel.ConnectionPhase == "starting" && !ViewModel.HasFatalError;
        CoreProgressRing.IsActive = isStarting;
        CoreProgressRing.Visibility = isStarting ? Visibility.Visible : Visibility.Collapsed;
        AutomationProperties.SetName(
            CoreProgressRing,
            ViewModel.HasFatalError
                ? "Core 启动失败"
                : isStarting
                    ? "Core 正在初始化"
                    : "Core 初始化完成");
    }

    private void OnAppWindowClosing(AppWindow sender, AppWindowClosingEventArgs args)
    {
        if (_allowClose)
        {
            return;
        }

        args.Cancel = true;
        if (_isClosing)
        {
            return;
        }

        _isClosing = true;
        _isActive = false;
        Activated -= OnActivated;
        ViewModel.PropertyChanged -= OnViewModelPropertyChanged;
        _ = DisposeAndCloseAsync();
    }

    private async Task DisposeAndCloseAsync()
    {
        try
        {
            var disposed = await AsyncCleanupWaiter.WaitAsync(ViewModel.DisposeAsync(), DisposeTimeout);
            if (!disposed)
            {
                Debug.WriteLine($"WinUI 关闭清理超时（{DisposeTimeout.TotalSeconds:0} 秒）");
            }
        }
        catch (Exception exception)
        {
            Debug.WriteLine($"WinUI 关闭清理失败: {exception}");
        }

        _allowClose = true;
        try
        {
            Close();
        }
        catch (Exception exception)
        {
            Debug.WriteLine($"WinUI 二次关闭失败: {exception}");
        }
    }

    private void OnClosed(object sender, WindowEventArgs args)
    {
        _isClosed = true;
        Activated -= OnActivated;
        AppWindow.Closing -= OnAppWindowClosing;
        Closed -= OnClosed;
        ViewModel.PropertyChanged -= OnViewModelPropertyChanged;

        try
        {
            _minimumSizeController.Dispose();
        }
        catch (Exception exception)
        {
            Debug.WriteLine($"WinUI 最小尺寸钩子卸载失败: {exception}");
        }
    }
}
