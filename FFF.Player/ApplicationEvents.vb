Imports System.Diagnostics
Imports System.IO
Imports System.Linq
Imports System.Threading
Imports Microsoft.VisualBasic.ApplicationServices

Namespace My
    Partial Friend Class MyApplication
        Private 待处理启动文件 As String = String.Empty

        ''' <summary>启动期把进程拉到过 HIGH 的标记；Form1_Shown 后由宿主降回原始优先级。
        ''' P0-1 起只有显式 opt-in（FFF_STARTUP_PRIORITY=1）才会置 True；默认 False 是 Form1
        ''' 降回线程的关键守卫——默认路径不会出现该线程，也不会写 PriorityClass。</summary>
        Friend Shared 启动优先级已拉高 As Boolean = False

        ''' <summary>拉高之前保存的进程原始 PriorityClass；降回线程据此恢复原值而非常量 Normal（start /high 启动时恢复 High）。仅在显式 opt-in 臂里被写入。</summary>
        Friend Shared 启动期原始优先级 As ProcessPriorityClass = ProcessPriorityClass.Normal

        ''' <summary>启动期 GPU 预热后台线程；UI 线程须在首个 D2D 触点（Form1_Load）前 Join 它，使 D3D_DeviceGlobals 的设备创建与 UI 使用在时间上互斥。</summary>
        Friend Shared GPU预热线程 As Threading.Thread

        ''' <summary>启动期 CPU 放置（docs/146）是否真的写下了进程默认 CPU 集合；Form1_Shown 的 3 s 兜底据此决定要不要发结束。</summary>
        Friend Shared 启动CPU放置已启用 As Boolean = False

        Private Sub MyApplication_Startup(sender As Object, e As StartupEventArgs) Handles Me.Startup
            启动期CPU放置开始()
            启动期拉高优先级()
            启动期预取大件()
            启动期预热GPU设备()
            记录初次启动命令行(e.CommandLine)
        End Sub

        ''' <summary>
        ''' 启动期 GPU 设备预热：Attach 里的首渲染会同步创建 D3D11/D2D/DXGI/DWrite
        ''' 设备，实测 266 ms，全部压在 Form1_Load 的调用栈上。设备创建本来就在
        ''' D3D_DeviceGlobals 的锁保护下、与界面线程的控件树构建无依赖，这里用一条
        ''' 后台线程把它提前做掉；Attach 首次渲染时 _initialized 已为 True，直接跳过。
        ''' 仅取 D2D Device（不动 DeviceContext——那个必须在 UI 线程用）。
        ''' 线程对象保存于 GPU预热线程 字段：UI 线程在首个 D2D 触点前 Join 它，
        ''' 使设备创建与 UI 线程的使用串行（LakeUI 侧双检/双锁并非严格互斥，不能并发）。
        ''' FFF_NO_GPU_WARMUP=1 退出。
        ''' </summary>
        Private Sub 启动期预热GPU设备()
            Try
                If Environment.GetEnvironmentVariable("FFF_NO_GPU_WARMUP") = "1" Then Return
                GPU预热线程 = New Thread(Sub()
                    Try
                        Dim 设备 = LakeUI.D3D_DeviceGlobals.GetD2DDevice()
                        GC.KeepAlive(设备)
                    Catch ex As Exception
                        ' 预热失败不影响启动：Attach 里仍会按需创建（并走它自己的错误处理）。
                    End Try
                End Sub) With {.IsBackground = True, .Name = "GPU预热"}
                GPU预热线程.Start()
            Catch
            End Try
        End Sub

        ''' <summary>
        ''' 启动期优先级（P0-1 起默认关）：只有显式 opt-in（FFF_STARTUP_PRIORITY=1）才在
        ''' Main 到首帧之间把进程拉到 HIGH，Form1_Shown 之后由宿主降回拉高前保存的原始
        ''' 优先级（启动期原始优先级 字段，非常量 Normal）。默认路径**完全不碰**
        ''' Process.PriorityClass —— 进程优先级是内核 PlacementPolicy 的领地，宿主只在
        ''' 显式 A/B 负控时临时扮演旧行为。
        ''' 默认关的理由：docs/145 两次独立实测都不支持它有效 —— 唯一一次支持性读数
        ''' （docs/120 的 −111.7 ms）是「预取+优先级」合测的归因伪影，拆开后优先级单独测
        ''' 是 Δ +4.4 ms、p=0.783（docs/124 §2.3，14 轮交替），落在噪声内且符号对 ON 不利。
        ''' 附带收益：docs/124 §4.1 的基线竞态降级为「默认不可能发生」——内核 PlacementPolicy
        ''' 在首次播放 OnPlaybackStart 采样 baselinePriorityClass_，若播放泵在 Shown+1500 ms
        ''' 之前启动就会把 HIGH 记成基线；默认不写 class 时这里根本不存在 HIGH 窗口。
        ''' 内核 PlacementPolicy 的播放期 Boost 与本处互不踩踏：它最早在播放泵启动后才触发
        ''' 且会恢复基线。
        ''' A/B 负控用法：FFF_STARTUP_PRIORITY=1 恢复旧行为（旧 exe 的默认臂）；
        ''' FFF_NO_STARTUP_PRIORITY=1 保留为兼容 no-op，两个都设时 NO 优先（不拉高）。
        ''' </summary>
        Private Sub 启动期拉高优先级()
            Try
                ' 兼容旧开关：显式 NO 一律不碰 PriorityClass（与旧 exe 语义一致），且优先于 opt-in。
                If Environment.GetEnvironmentVariable("FFF_NO_STARTUP_PRIORITY") = "1" Then Return
                ' 显式 opt-in：未设置或非 "1" 都不拉高。默认路径在第一次 Process.GetCurrentProcess 前返回。
                If Environment.GetEnvironmentVariable("FFF_STARTUP_PRIORITY") <> "1" Then Return
                Dim _p = Process.GetCurrentProcess()
                Using _p
                    ' 拉高前先保存原始 PriorityClass（同一处完成 Process 的 Using 释放）；
                    ' 读优先级失败会抛异常走外层 Catch：不拉高、不置 启动优先级已拉高。
                    启动期原始优先级 = _p.PriorityClass
                    _p.PriorityClass = ProcessPriorityClass.High
                End Using
                启动优先级已拉高 = True
            Catch
                ' 优先级不是启动的前提：拿不到就按原样跑。
            End Try
        End Sub

        ''' <summary>
        ''' 启动期 CPU 放置（docs/146）：把启动窗口的进程默认 CPU 集合软限制到首选域——
        ''' Intel 大小核 = EfficiencyClass 最高的 P-core 组；AMD 多 CCD = UI 线程当前所在的
        ''' L3 域（Windows 不暴露 CCD 编号，"主 CCD"没有权威定义，所以不猜、只保局部性）；
        ''' 多 processor group 的机型内核会拒绝（CPU Set 跨 group 赋值会被 OS 忽略）。
        ''' 只在 FFF_STARTUP_PLACEMENT=1 时启用：默认关，未做 A/B 之前不叠进启动路径；
        ''' 这个判断必须放在调用内核导出之前，否则调用本身就会把 FFF.Native 提前加载。
        ''' 结束由内核在首帧呈现时自己做，没有媒体可放时 Form1_Shown 起 3 s 兜底。
        ''' </summary>
        Private Sub 启动期CPU放置开始()
            Try
                If Environment.GetEnvironmentVariable("FFF_STARTUP_PLACEMENT") <> "1" Then Return
                If Not 播放器原生接口.支持启动期CPU放置 Then Return
                启动CPU放置已启用 = 播放器原生接口.启动期CPU放置开始()
            Catch
                ' 放置不是启动的前提：拿不到就按原样跑。
            End Try
        End Sub

        ''' <summary>
        ''' 启动预取：FFmpeg 六件加 shaderc 合计 ~113 MB，内核要到 +1.1s 才映射它们，
        ''' 那时 UI 线程正忙，磁盘读是在给启动计时器加时。这里用一条 BelowNormal
        ''' 后台线程把磁盘读提前做掉，后续映射变成缓存命中。首跑（单文件解包刚把
        ''' 文件写完）本来就在缓存里，收益≈0；收益在第二跑以后。
        ''' FFF_PREFETCH=1 可重新开启（默认关闭，见下方实测）。
        ''' </summary>
        Private Sub 启动期预取大件()
            Try
                ' 【2026-11-01 实测】预取在快窗里是净亏：同 exe 只切 FFF_NO_PREFETCH，配对 14 轮
                ' 关掉后 T2 −96.4 ms（U=41, p=0.009；两侧 28/28 全 fast 档）。它把 113 MB 读进
                ' 文件缓存，与 UI 线程争磁盘/内存带宽，而内核真正映射这批大件时缓存往往已经太晚。
                ' ⇒ 默认关闭。这是 PR #48 第 4 项的一半，本机判为负收益。
                If Environment.GetEnvironmentVariable("FFF_PREFETCH") <> "1" Then Return
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

        ''' <summary>
        ''' 只读窥视初次启动命令行带来的文件（**不消费**，Shown 里仍由 取出待处理启动文件 取走）。
        ''' 用途：Form1_Load 要按媒体类型决定本次启动走「UI 优先」还是「画面优先」，
        ''' 而顺序在 Load 里就定死了，那时还没有别的途径知道要开什么文件。
        ''' 第二实例转发（StartupNextInstance）时主窗体已存在，走的是直接开图，与此无关。
        ''' </summary>
        Friend Function 待处理启动文件快照() As String
            Return 待处理启动文件
        End Function

        Friend Sub 记录初次启动命令行(参数 As IEnumerable(Of String))
            待处理启动文件 = Form1.取得命令行文件(参数)
        End Sub
    End Class
End Namespace
