namespace LanChat.Presentation;

public static class AsyncCleanupWaiter
{
    internal static Action<Task>? CompletionObserver { get; set; }

    public static async Task<bool> WaitAsync(ValueTask cleanup, TimeSpan timeout)
    {
        var cleanupTask = cleanup.AsTask();
        var timeoutTask = Task.Delay(timeout);
        if (await Task.WhenAny(cleanupTask, timeoutTask).ConfigureAwait(false) == cleanupTask)
        {
            await cleanupTask.ConfigureAwait(false);
            return true;
        }

        ObserveCompletion(cleanupTask);
        return false;
    }

    private static void ObserveCompletion(Task task)
    {
        _ = task.ContinueWith(
            static (completed, state) =>
            {
                _ = completed.Exception;
                ((Action<Task>?)state)?.Invoke(completed);
            },
            CompletionObserver,
            CancellationToken.None,
            TaskContinuationOptions.OnlyOnFaulted | TaskContinuationOptions.ExecuteSynchronously,
            TaskScheduler.Default);
    }
}
