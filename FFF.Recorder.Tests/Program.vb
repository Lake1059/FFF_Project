Imports System

Friend Module Program
    <STAThread>
    Public Function Main(参数 As String()) As Integer
        ' L1-P2-01：本工程当前没有任何真实测试项。
        ' 历史上这里写死 Return 0，等于“假通过”——空壳比缺测试更有害。
        ' 现在明确打印“未注册测试项”并以非 0 退出，让任何把它当成验收信号的脚本
        ' 立刻变红，而不是被静默吞掉。该工程不参与本轮 106 验收（见 docs/gainmap-testing.md
        ' 与 F3 修复报告的 L1-P2-01 条目）；要恢复测试时，在这里注册真实用例并改回 0。
        Console.Error.WriteLine("FFF.Recorder.Tests: 尚未注册任何测试项。本工程不参与本轮验收，退出码 1 表示空壳，而非通过。")
        Return 1
    End Function
End Module
