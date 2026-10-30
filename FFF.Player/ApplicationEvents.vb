Imports System.Diagnostics
Imports System.IO
Imports System.Linq
Imports System.Threading
Imports Microsoft.VisualBasic.ApplicationServices

Namespace My
    Partial Friend Class MyApplication
        Private 待处理启动文件 As String = String.Empty

        ''' <summary>启动期把进程拉到过 HIGH 的标记；Form1_Shown 后由宿主降回 Normal。</summary>
        Friend Shared 启动优先级已拉高 As Boolean = False

        Private Sub MyApplication_Startup(sender As Object, e As StartupEventArgs) Handles Me.Startup
            启动期拉高优先级()
            启动期预取大件()
            记录初次启动命令行(e.CommandLine)
        End Sub

        ''' <summary>
        ''' 启动期优先级：Main 到首帧之间进程还在 NORMAL，排在前面的往往是机器上其
        ''' 它忙线程。拉到 HIGH 覆盖整个启动窗口，Form1_Shown 之后由宿主降回 Normal。
        ''' 空闲机上收益≈0（没有争抢者），负载机上是把排队的启动时间抢回来。
        ''' 内核 PlacementPolicy 的播放期 Boost 用的也是这个旋钮，但它最早在播放泵
        ''' 启动后才触发且会恢复基线——本处在它之前拉、之后降，互不踩踏。
        ''' FFF_NO_STARTUP_PRIORITY=1 退出。
        ''' </summary>
        Private Sub 启动期拉高优先级()
            Try
                If Environment.GetEnvironmentVariable("FFF_NO_STARTUP_PRIORITY") = "1" Then Return
                Process.GetCurrentProcess().PriorityClass = ProcessPriorityClass.High
                启动优先级已拉高 = True
            Catch
                ' 优先级不是启动的前提：拿不到就按原样跑。
            End Try
        End Sub

        ''' <summary>
        ''' 启动预取：FFmpeg 六件加 shaderc 合计 ~113 MB，内核要到 +1.1s 才映射它们，
        ''' 那时 UI 线程正忙，磁盘读是在给启动计时器加时。这里用一条 BelowNormal
        ''' 后台线程把磁盘读提前做掉，后续映射变成缓存命中。首跑（单文件解包刚把
        ''' 文件写完）本来就在缓存里，收益≈0；收益在第二跑以后。
        ''' FFF_NO_PREFETCH=1 退出。
        ''' </summary>
        Private Sub 启动期预取大件()
            Try
                If Environment.GetEnvironmentVariable("FFF_NO_PREFETCH") = "1" Then Return
                Dim 文件名前缀 = {"avcodec", "avfilter", "avformat", "avutil", "swresample", "swscale", "libshaderc_shared"}
                Dim 候选目录 As New List(Of String)
                Dim exe目录 = Path.GetDirectoryName(Environment.ProcessPath)
                If Not String.IsNullOrEmpty(exe目录) Then 候选目录.Add(exe目录)
                Dim 托管目录 = AppContext.BaseDirectory
                If Not String.IsNullOrEmpty(托管目录) AndAlso
                    Not 候选目录.Any(Function(d) String.Equals(d, 托管目录, StringComparison.OrdinalIgnoreCase)) Then
                    候选目录.Add(托管目录)
                End If
                Dim 预取线程 As New Thread(Sub()
                    Try
                        Thread.CurrentThread.Priority = ThreadPriority.BelowNormal
                        Dim 缓冲(4194303) As Byte
                        For Each 目录 In 候选目录
                            If Not Directory.Exists(目录) Then Continue For
                            For Each 前缀 In 文件名前缀
                                Dim 命中 = Directory.GetFiles(目录, 前缀 & "*.dll").FirstOrDefault()
                                If 命中 Is Nothing Then Continue For
                                Using 流 As New FileStream(命中, FileMode.Open, FileAccess.Read, FileShare.Read, 4194304, FileOptions.SequentialScan)
                                    While 流.Read(缓冲, 0, 缓冲.Length) > 0
                                    End While
                                End Using
                            Next
                        Next
                    Catch
                        ' 预取失败不影响启动。
                    End Try
                End Sub) With {.IsBackground = True, .Name = "启动预取", .Priority = ThreadPriority.BelowNormal}
                预取线程.Start()
            Catch
            End Try
        End Sub

        Private Sub MyApplication_StartupNextInstance(sender As Object, e As StartupNextInstanceEventArgs) Handles Me.StartupNextInstance
            e.BringToForeground = True
            Dim 文件路径 = Form1.取得命令行文件(e.CommandLine)
            If String.IsNullOrEmpty(文件路径) Then Return
            Dim 主窗体 = TryCast(MainForm, Form1)
            If 主窗体 Is Nothing Then
                待处理启动文件 = 文件路径
            Else
                主窗体.打开命令行文件({文件路径})
            End If
        End Sub

        Friend Function 取出待处理启动文件() As String
            Dim 文件路径 = 待处理启动文件
            待处理启动文件 = String.Empty
            Return 文件路径
        End Function

        Friend Sub 记录初次启动命令行(参数 As IEnumerable(Of String))
            待处理启动文件 = Form1.取得命令行文件(参数)
        End Sub
    End Class
End Namespace
