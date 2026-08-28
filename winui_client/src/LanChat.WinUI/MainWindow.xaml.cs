using Microsoft.UI.Xaml;
using System.Diagnostics;
using LanChat.Core;
using LanChat.Core.Runtime;
using LanChat.Presentation;
using Windows.Graphics;

namespace LanChat_WinUI;

public sealed partial class MainWindow : Window
{
    private bool _startRequested;
    private bool _started;
    private bool _isActive;
    private bool _isClosed;

    public MainWindow()
    {
        var runtime = new CoreRuntime(() => LanChatCoreClient.Open(AppContext.BaseDirectory));
        ViewModel = new ShellViewModel(runtime, new DispatcherQueueAdapter(DispatcherQueue));

        InitializeComponent();

        ExtendsContentIntoTitleBar = true;
        SetTitleBar(AppTitleBar);
        AppWindow.SetIcon("Assets/AppIcon.ico");
        AppWindow.Resize(new SizeInt32(1100, 720));

        Activated += OnActivated;
        Closed += OnClosed;
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
        catch (ObjectDisposedException) when (_isClosed)
        {
        }
        catch (Exception exception)
        {
            Debug.WriteLine($"WinUI 生命周期处理失败: {exception}");
        }
    }

    private async void OnClosed(object sender, WindowEventArgs args)
    {
        _isClosed = true;
        Activated -= OnActivated;
        Closed -= OnClosed;

        try
        {
            await ViewModel.DisposeAsync();
        }
        catch (Exception exception)
        {
            Debug.WriteLine($"WinUI 关闭清理失败: {exception}");
        }
    }
}
