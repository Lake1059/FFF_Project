Imports System.IO
Imports System.Linq
Imports System.Threading
Imports FFF.Player

Friend Module Program
    <STAThread>
    Public Function Main(args As String()) As Integer
        ' 外部色彩变换 kind=3 注入回归（真内核 + 真模块 DLL；票据须由驱动预铸，见回归文件头）。
        If args.Length > 0 AndAlso String.Equals(args(0), "--color-transform-injection",
                StringComparison.OrdinalIgnoreCase) Then
            Return ColorTransformInjectionRegression.运行(args.Skip(1).ToArray())
        End If

        If args.Length > 0 Then
            Console.Error.WriteLine($"未知测试入口：{args(0)}。未运行任何测试。")
            Return 1
        End If

        Dim 目录 = Path.Combine(Path.GetTempPath(), "3FP-Playlist-" & Guid.NewGuid().ToString("N"))
        Directory.CreateDirectory(目录)
        Try
            测试自动播放列表(目录)
            测试相邻范围(Path.Combine(目录, "range"))
            Console.WriteLine("播放列表测试通过。")
            Return 0
        Catch ex As Exception
            Console.Error.WriteLine(ex.ToString())
            Return 1
        Finally
            Directory.Delete(目录, True)
        End Try
    End Function

    Private Sub 测试自动播放列表(目录 As String)
        For Each 名称 In {"video10.mp4", "video2.mkv", "song.mp3", "picture.jpg", "ignore.txt"}
            File.WriteAllText(Path.Combine(目录, 名称), String.Empty)
        Next
        Dim 列表 As New 播放列表()
        Dim 当前路径 = Path.Combine(目录, "video10.mp4")
        列表.从媒体扫描同目录Async(当前路径).GetAwaiter().GetResult()
        Dim 名称列表 = 列表.取得项目().Select(Function(x) Path.GetFileName(x.路径)).ToArray()
        检查(名称列表.SequenceEqual({"picture.jpg", "song.mp3", "video2.mkv", "video10.mp4"}), "混合媒体或自然排序不正确")
        检查(列表.当前项目.路径 = 当前路径, "当前项目不正确")
        Dim 图片路径 = Path.Combine(目录, "picture.jpg")
        列表.从媒体扫描同目录Async(图片路径).GetAwaiter().GetResult()
        检查(列表.数量 = 4 AndAlso 列表.当前项目.路径 = 图片路径, "同目录重新打开失败")
        File.WriteAllText(Path.Combine(目录, "added.wav"), String.Empty)
        列表.从媒体扫描同目录Async(当前路径).GetAwaiter().GetResult()
        检查(列表.数量 = 5, "目录内容变化后未重新扫描")

        For i = 0 To 1000
            File.WriteAllText(Path.Combine(目录, $"extra{i:D4}.mp4"), String.Empty)
        Next
        列表.从媒体扫描同目录Async(当前路径).GetAwaiter().GetResult()
        检查(列表.数量 = 1000 AndAlso 列表.当前项目.路径 = 当前路径, "数量上限或当前项目不正确")

        Using 已取消 As New CancellationTokenSource()
            已取消.Cancel()
            Try
                列表.从媒体扫描同目录Async(Path.Combine(目录, "picture.jpg"), 已取消.Token).GetAwaiter().GetResult()
                Throw New Exception("取消扫描未生效")
            Catch ex As OperationCanceledException
            End Try
        End Using
        检查(列表.当前项目.路径 = 当前路径, "取消的扫描修改了列表")

        Dim 首次 = 列表.从媒体扫描同目录Async(当前路径)
        Dim 最新路径 = Path.Combine(目录, "picture.jpg")
        Dim 最新 = 列表.从媒体扫描同目录Async(最新路径)
        Threading.Tasks.Task.WhenAll(首次, 最新).GetAwaiter().GetResult()
        检查(列表.当前项目.路径 = 最新路径, "旧扫描覆盖了新扫描")
    End Sub

    Private Sub 测试相邻范围(目录 As String)
        Directory.CreateDirectory(目录)
        For i = 1500 To 0 Step -1
            File.WriteAllText(Path.Combine(目录, $"media{i:D4}.mp4"), String.Empty)
        Next
        Dim 列表 As New 播放列表()
        列表.从媒体扫描同目录Async(Path.Combine(目录, "media0750.mp4")).GetAwaiter().GetResult()
        Dim 名称 = 列表.取得项目().Select(Function(x) Path.GetFileName(x.路径)).ToArray()
        检查(名称.Length = 1000 AndAlso 名称(0) = "media0250.mp4" AndAlso 名称(999) = "media1249.mp4", "中间文件的前后范围不正确")
        列表.从媒体扫描同目录Async(Path.Combine(目录, "media0000.mp4")).GetAwaiter().GetResult()
        名称 = 列表.取得项目().Select(Function(x) Path.GetFileName(x.路径)).ToArray()
        检查(名称.Length = 1000 AndAlso 名称(0) = "media0000.mp4" AndAlso 名称(999) = "media0999.mp4", "目录开头的范围不正确")
        列表.从媒体扫描同目录Async(Path.Combine(目录, "media1500.mp4")).GetAwaiter().GetResult()
        名称 = 列表.取得项目().Select(Function(x) Path.GetFileName(x.路径)).ToArray()
        检查(名称.Length = 1000 AndAlso 名称(0) = "media0501.mp4" AndAlso 名称(999) = "media1500.mp4", "目录末尾的范围不正确")
    End Sub

    Private Sub 检查(条件 As Boolean, 消息 As String)
        If Not 条件 Then Throw New Exception(消息)
    End Sub
End Module
