using System.Runtime.InteropServices;
using LanChat.Core.Interop;

namespace LanChat.Core.Tests;

[TestClass]
public sealed class CoreLibraryResolverTests
{
    [TestMethod]
    public void Install_requires_fixed_core_dll_and_rejects_path_changes()
    {
        var appDirectory = Path.Combine(Path.GetTempPath(), $"LanChatCoreTests-{Guid.NewGuid():N}");
        Directory.CreateDirectory(appDirectory);
        var expectedPath = Path.Combine(Path.GetFullPath(appDirectory), "lan_chat_core.dll");

        var exception = Assert.ThrowsExactly<FileNotFoundException>(
            () => CoreLibraryResolver.Install(appDirectory));

        StringAssert.Contains(exception.Message, expectedPath);

        var corePath = expectedPath;
        File.Copy(Path.Combine(Environment.SystemDirectory, "kernel32.dll"), corePath);

        CoreLibraryResolver.Install(appDirectory);

        Assert.IsTrue(NativeLibrary.TryLoad("lan_chat_core.dll", typeof(CoreLibraryResolver).Assembly, null, out var handle));
        Assert.AreNotEqual(nint.Zero, handle);
        Assert.ThrowsExactly<InvalidOperationException>(
            () => CoreLibraryResolver.Install(Path.Combine(appDirectory, "other")));
    }
}
