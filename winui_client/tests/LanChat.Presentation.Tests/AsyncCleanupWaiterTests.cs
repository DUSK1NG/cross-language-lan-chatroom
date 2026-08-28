using LanChat.Presentation;

namespace LanChat.Presentation.Tests;

[TestClass]
public sealed class AsyncCleanupWaiterTests
{
    [TestMethod]
    public async Task Completed_cleanup_returns_true()
    {
        var result = await AsyncCleanupWaiter.WaitAsync(
            ValueTask.CompletedTask,
            TimeSpan.FromSeconds(1));

        Assert.IsTrue(result);
    }

    [TestMethod]
    public async Task Cleanup_timeout_returns_false_without_waiting_for_cleanup()
    {
        var cleanup = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);

        var result = await AsyncCleanupWaiter.WaitAsync(
            new ValueTask(cleanup.Task),
            TimeSpan.FromMilliseconds(20));

        Assert.IsFalse(result);
        Assert.IsFalse(cleanup.Task.IsCompleted);

        cleanup.SetResult();
        await cleanup.Task;
    }

    [TestMethod]
    public async Task Cleanup_timeout_registers_fault_observer_for_late_failure()
    {
        var cleanup = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        var observerCalled = new TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously);
        AsyncCleanupWaiter.CompletionObserver = task =>
        {
            if (ReferenceEquals(task, cleanup.Task))
            {
                observerCalled.TrySetResult();
            }
        };

        try
        {
            var result = await AsyncCleanupWaiter.WaitAsync(
                new ValueTask(cleanup.Task),
                TimeSpan.FromMilliseconds(20));

            Assert.IsFalse(result);
            Assert.IsFalse(observerCalled.Task.IsCompleted);

            cleanup.SetException(new InvalidOperationException("late cleanup fault"));
            Assert.IsTrue(observerCalled.Task.Wait(TimeSpan.FromSeconds(1)));
        }
        finally
        {
            AsyncCleanupWaiter.CompletionObserver = null;
        }
    }

    [TestMethod]
    public async Task Cleanup_exception_is_propagated_when_it_completes_before_timeout()
    {
        var expected = new InvalidOperationException("cleanup failed");

        var exception = await Assert.ThrowsAsync<InvalidOperationException>(
            async () => await AsyncCleanupWaiter.WaitAsync(
                ValueTask.FromException(expected),
                TimeSpan.FromSeconds(1)));

        Assert.AreSame(expected, exception);
    }
}
