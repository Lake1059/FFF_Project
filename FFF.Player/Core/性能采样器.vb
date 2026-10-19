Option Strict On
Option Explicit On

Imports System.Drawing
Imports System.Globalization
Imports System.IO
Imports System.Text
Imports System.Windows.Forms

''' <summary>
''' 实机性能采样：在**真实播放器进程内**按固定间隔读取内核快照，逐窗口做差并落成 CSV。
'''
''' 为什么必须有这个东西：之前的性能结论是用一个自建的最小窗口 harness 量出来的，
''' 而那个 harness 的窗口是 640x480。8K 素材在 640x480 窗口里做的是 1:12 的极端降采样，
''' 缩放链逐级减半会出十几个 pass，于是"上传占总耗时 84%"这个数是极端降采样这一
''' 特殊工况下的读数，不能被当成全屏播放时的瓶颈分布。要回答"开销聚集在哪"，
''' 必须在播放器本体、在真实视口下量。
'''
''' 默认完全不活动：只有同时给出 FFF3FP_PERF_OUT 与 FFF3FP_PERF_SECONDS 才会启动，
''' 所以正常使用时本类型连一个对象都不会被创建。
'''
''' 环境变量：
'''   FFF3FP_PERF_OUT          CSV 输出路径（必需）
'''   FFF3FP_PERF_SECONDS      采样窗口数（必需；一个窗口 1 秒）
'''   FFF3FP_PERF_FULLSCREEN   1 = 无边框最大化（模拟全屏播放）
'''   FFF3FP_PERF_WIDTH        客户端宽度（全屏为 1 时忽略）
'''   FFF3FP_PERF_HEIGHT       客户端高度（全屏为 1 时忽略）
''' </summary>
Friend NotInheritable Class 性能采样器
    Implements IDisposable

    Private Const 采样间隔毫秒 As Integer = 1000

    Private ReadOnly 读取快照 As Func(Of 播放器快照)
    Private ReadOnly 读取视口 As Func(Of Size)
    Private ReadOnly 宿主窗体 As Form
    Private ReadOnly 输出路径 As String
    Private ReadOnly 窗口数上限 As Integer

    Private WithEvents 计时器 As Timer
    Private ReadOnly 累积 As New StringBuilder()
    Private 上一次 As 播放器快照
    Private 已记窗口 As Integer
    Private 已收尾 As Boolean
    Private 表头已写 As Boolean
    Private 启动时刻 As DateTime
    ' 连续多少个窗口没有新帧。素材播完后 presented 就不再前进，靠窗口数收尾会永远等下去。
    Private 无进展窗口 As Integer

    Private Sub New(读取快照 As Func(Of 播放器快照), 读取视口 As Func(Of Size),
                    宿主窗体 As Form, 输出路径 As String, 窗口数上限 As Integer)
        Me.读取快照 = 读取快照
        Me.读取视口 = 读取视口
        Me.宿主窗体 = 宿主窗体
        Me.输出路径 = 输出路径
        Me.窗口数上限 = 窗口数上限
    End Sub

    ''' <summary>环境变量齐全才创建；否则返回 Nothing，调用方无需任何分支。</summary>
    Friend Shared Function 尝试创建(读取快照 As Func(Of 播放器快照), 读取视口 As Func(Of Size),
                                    宿主窗体 As Form) As 性能采样器
        If 读取快照 Is Nothing OrElse 读取视口 Is Nothing OrElse 宿主窗体 Is Nothing Then Return Nothing

        Dim 输出 = Environment.GetEnvironmentVariable("FFF3FP_PERF_OUT")
        If String.IsNullOrWhiteSpace(输出) Then Return Nothing

        Dim 秒数文本 = Environment.GetEnvironmentVariable("FFF3FP_PERF_SECONDS")
        Dim 秒数 As Integer
        If String.IsNullOrWhiteSpace(秒数文本) Then Return Nothing
        If Not Integer.TryParse(秒数文本, NumberStyles.Integer, CultureInfo.InvariantCulture, 秒数) Then Return Nothing
        If 秒数 <= 0 Then Return Nothing

        Return New 性能采样器(读取快照, 读取视口, 宿主窗体, 输出, 秒数)
    End Function

    ''' <summary>按环境变量摆放视口。测量口径的关键变量，不设就沿用默认窗口。</summary>
    Friend Sub 应用视口()
        Dim 全屏 = Environment.GetEnvironmentVariable("FFF3FP_PERF_FULLSCREEN")
        If 全屏 = "1" Then
            宿主窗体.FormBorderStyle = FormBorderStyle.None
            宿主窗体.WindowState = FormWindowState.Maximized
            Return
        End If

        Dim 宽文本 = Environment.GetEnvironmentVariable("FFF3FP_PERF_WIDTH")
        Dim 高文本 = Environment.GetEnvironmentVariable("FFF3FP_PERF_HEIGHT")
        Dim 宽 As Integer, 高 As Integer
        Dim 有宽 = Integer.TryParse(宽文本, NumberStyles.Integer, CultureInfo.InvariantCulture, 宽)
        Dim 有高 = Integer.TryParse(高文本, NumberStyles.Integer, CultureInfo.InvariantCulture, 高)
        If 有宽 AndAlso 有高 AndAlso 宽 > 0 AndAlso 高 > 0 Then
            宿主窗体.WindowState = FormWindowState.Normal
            宿主窗体.ClientSize = New Size(宽, 高)
        End If
    End Sub

    Friend Sub 启动()
        If 计时器 IsNot Nothing Then Return
        应用视口()
        启动时刻 = DateTime.Now
        计时器 = New Timer With {.Interval = 采样间隔毫秒}
        计时器.Start()
    End Sub

    Private Sub 计时器_触发(sender As Object, e As EventArgs) Handles 计时器.Tick
        Dim 当前 = 读取快照()
        If 当前 Is Nothing Then Exit Sub

        ' 只有真的呈现了新帧才算一个有效窗口：打开媒体、起播、seek 的空档不记，
        ' 否则窗口分母被稀释，每帧耗时会莫名其妙地变小。
        Dim 呈现增量 As Long = 0
        If 上一次 IsNot Nothing Then 呈现增量 = 差值(当前.已呈现视频帧数, 上一次.已呈现视频帧数)

        If 上一次 IsNot Nothing AndAlso 呈现增量 > 0 Then
            写入一行(上一次, 当前, 呈现增量)
            已记窗口 += 1
            无进展窗口 = 0
        ElseIf 上一次 IsNot Nothing Then
            无进展窗口 += 1
        End If

        上一次 = 当前

        ' 两道兜底，缺一不可：素材比采样时长短（播放自然结束），或一直没起播。
        ' 只看窗口数的话这两种情况都会让播放器永远不退出，整轮矩阵挂在第一臂。
        Dim 墙钟超时 = (DateTime.Now - 启动时刻).TotalSeconds > 窗口数上限 * 3.0R + 30.0R
        If 已记窗口 >= 窗口数上限 OrElse 无进展窗口 >= 3 OrElse 墙钟超时 Then 收尾()
    End Sub

    Private Sub 写入一行(前 As 播放器快照, 后 As 播放器快照, 呈现增量 As Long)
        If Not 表头已写 Then
            累积.AppendLine("window,clientW,clientH,videoW,videoH," &
                "presented,dropped,coalesced,decoded,queued," &
                "sw_ms,hw_ms,upload_ms,mapwait_ms,copy_ms,resample_ms," &
                "devlock_ms,presentwait_ms,pass_per_frame,uploads,bytes_per_upload,upath,total_ms,fps")
            表头已写 = True
        End If

        ' 所有计时字段都是累计单调量，做差得本窗口增量；除以呈现帧数得每帧耗时。
        ' 差值() 是饱和减法：计数器一旦回绕（重建设备、换流），下溢会把 1 秒变成天文数字。
        Dim 软转换 = 毫秒(时差(后.软件转换时长, 前.软件转换时长), 呈现增量)
        Dim 硬传输 = 毫秒(时差(后.硬件传输时长, 前.硬件传输时长), 呈现增量)
        Dim 上传 = 毫秒(时差(后.视频上传时长, 前.视频上传时长), 呈现增量)
        Dim 映射等待 = 毫秒(时差(后.上传映射等待时长, 前.上传映射等待时长), 呈现增量)
        Dim 复制 = 毫秒(时差(后.上传复制时长, 前.上传复制时长), 呈现增量)
        Dim 重采样 = 毫秒(时差(后.降档重采样时长, 前.降档重采样时长), 呈现增量)
        Dim 设备锁 = 毫秒(时差(后.设备锁等待时长, 前.设备锁等待时长), 呈现增量)
        Dim 呈现等待 = 毫秒(时差(后.呈现等待时长, 前.呈现等待时长), 呈现增量)
        Dim 每帧趟数 = 差值(后.缩放链趟数, 前.缩放链趟数) / CDbl(呈现增量)

        ' 带宽的两个操作数：搬了多少字节、搬了几次。字节/次 = 每次上传的真实体量，
        ' 字节/(copy_ms×次数) = 真实带宽 —— 之前这两个数都只能从"理论帧大小"猜。
        Dim 上传增量 = 差值(后.上传次数, 前.上传次数)
        Dim 字节增量 = 差值(后.上传复制字节, 前.上传复制字节)
        Dim 每次字节 = If(上传增量 > 0, CDbl(字节增量) / CDbl(上传增量), 0.0R)

        ' 记的是**画面控件**的视口，不是窗体客户区：两者差着边框、标题栏和控制条，
        ' 而决定缩放链长度的正是画面控件那块区域。用窗体尺寸会把口径再搞错一次。
        Dim 客户端 = If(读取视口 IsNot Nothing, 读取视口(), 宿主窗体.ClientSize)
        累积.AppendLine(String.Join(",", {
            已记窗口.ToString(CultureInfo.InvariantCulture),
            客户端.Width.ToString(CultureInfo.InvariantCulture),
            客户端.Height.ToString(CultureInfo.InvariantCulture),
            后.视频宽度.ToString(CultureInfo.InvariantCulture),
            后.视频高度.ToString(CultureInfo.InvariantCulture),
            呈现增量.ToString(CultureInfo.InvariantCulture),
            差值(后.已丢弃视频帧数, 前.已丢弃视频帧数).ToString(CultureInfo.InvariantCulture),
            差值(后.已合并视频帧数, 前.已合并视频帧数).ToString(CultureInfo.InvariantCulture),
            差值(后.已解码视频帧数, 前.已解码视频帧数).ToString(CultureInfo.InvariantCulture),
            后.视频队列帧数.ToString(CultureInfo.InvariantCulture),
            软转换.ToString("F3", CultureInfo.InvariantCulture),
            硬传输.ToString("F3", CultureInfo.InvariantCulture),
            上传.ToString("F3", CultureInfo.InvariantCulture),
            映射等待.ToString("F3", CultureInfo.InvariantCulture),
            复制.ToString("F3", CultureInfo.InvariantCulture),
            重采样.ToString("F3", CultureInfo.InvariantCulture),
            设备锁.ToString("F3", CultureInfo.InvariantCulture),
            呈现等待.ToString("F3", CultureInfo.InvariantCulture),
            每帧趟数.ToString("F2", CultureInfo.InvariantCulture),
            上传增量.ToString(CultureInfo.InvariantCulture),
            每次字节.ToString("F0", CultureInfo.InvariantCulture),
            后.上传路径.ToString(CultureInfo.InvariantCulture),
            (软转换 + 硬传输 + 上传 + 设备锁 + 呈现等待 + 重采样).ToString("F3", CultureInfo.InvariantCulture),
            (呈现增量 / 1.0R).ToString("F2", CultureInfo.InvariantCulture)
        }))
    End Sub

    Private Shared Function 差值(现在 As ULong, 之前 As ULong) As Long
        If 现在 <= 之前 Then Return 0L
        Dim 差 = 现在 - 之前
        If 差 > CULng(Long.MaxValue) Then Return Long.MaxValue
        Return CLng(差)
    End Function

    Private Shared Function 时差(现在 As TimeSpan, 之前 As TimeSpan) As Long
        Dim 差 = 现在.Ticks - 之前.Ticks
        Return If(差 > 0L, 差, 0L)
    End Function

    Private Shared Function 毫秒(ticks As Long, 帧数 As Long) As Double
        Return ticks / 10000.0R / CDbl(帧数)
    End Function

    Private Sub 收尾()
        If 已收尾 Then Return
        已收尾 = True
        If 计时器 IsNot Nothing Then 计时器.Stop()
        Try
            Directory.CreateDirectory(Path.GetDirectoryName(输出路径))
            File.WriteAllText(输出路径, 累积.ToString(), New UTF8Encoding(False))
        Catch
            ' 采样是观测设施，写盘失败不该影响播放器本身。
        End Try
        ' 无人值守跑完就退出，否则每一臂都要人工关窗口。
        Try
            宿主窗体.BeginInvoke(Sub()
                                     宿主窗体.Close()
                                     Application.Exit()
                                 End Sub)
        Catch
        End Try
    End Sub

    Public Sub Dispose() Implements IDisposable.Dispose
        If 计时器 IsNot Nothing Then
            计时器.Stop()
            计时器.Dispose()
            计时器 = Nothing
        End If
    End Sub
End Class
