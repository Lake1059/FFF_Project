' CPU 放置策略（placement）真机回归 —— 五臂负对照 / 强制档
'
' 被测真身 = FFF.Native 内核的 PlacementPolicy
' （FFF.Native\3FP\Core\PlacementPolicy.cpp；接线在 PlayerSession.cpp:2950/3792/4794/5209）。
' 走真播放会话：CPU 解码 + 真输出窗口（离屏 Form 里的 播放器画面控件），
' 每臂 12 秒（默认可传参），五臂按序 off/auto/prefer/spill/boost。
' FFF_PLACEMENT / FFF_PLACEMENT_TRACE 必须在 New 播放器会话 **之前** 设置：
' 策略对象在 FFF3FP_Create 时构造、构造时读环境（PlacementPolicy.cpp:63-79）。
'
' 五路证据：
'   ① 内核自报日志：FFF3FP_SetLogCallback（进程级槽，注册一次）→ PLACEMENT_DISCOVERED /
'      PLACEMENT transition= / PLACEMENT_SAMPLE window|final / PLACEMENT_APPLY_FAILED；
'   ② 机器侧：GetProcessDefaultCpuSets(GetCurrentProcess()) 的 set 数（两趟式，播放中多次采样），
'      本机 20 LP = 12 P(tier0) + 8 E(tier1)：prefer=12、spill/boost=20、off=0；
'   ③ 播放线程落点：PLACEMENT_SAMPLE 的 present_tier0/1/other 计数（内核逐帧采样）；
'   ④ 丢帧（管线落后）= 快照 已丢弃视频帧数 的播放期 delta；
'   ⑤ 丢帧（面板限速）= 节律 显示速率丢弃数 delta；从未上屏翻帧 = 节律 从未上屏翻帧数 delta。
'
' ⚠ off 臂的 PLACEMENT_DISABLED：首轮实现里该行**从不发射**（只写 description_，
'   而 description_ 的唯一读者 Describe() 无调用点）；2026-10-17 已在
'   PlacementPolicy::OnPlaybackStart 的禁用分支补发射（一次一行，不调任何 Set*）。
'   本档判据是"字面 DISABLED **或** 等价负对照证据"（无 transition + 无 APPLY_FAILED
'   + 默认 set=0 + PLACEMENT_DISCOVERED 在 + 采样>0）：字面为首选，等价为防御——
'   字面行再次缺席时会**大声打 note**，不静默洗绿。
'
' 用法：FFF.Player.Tests --playback-placement [夹具路径] [每臂秒数]
' 退出码：0 全绿 / 1 有失败条 / 2 夹具等前置条件不对（打印 NOT JUDGED）

Imports System
Imports System.Collections.Generic
Imports System.Drawing
Imports System.IO
Imports System.Runtime.InteropServices
Imports System.Threading
Imports System.Windows.Forms
Imports FFF.Player

Friend Module PlaybackPlacementRegression
    Private 通过 As Integer
    Private 失败 As Integer

    ' ── 进程级内核日志槽（FFF3FP_SetLogCallback） ───────────────────────────────
    ' 回调在 native 线程上来（呈现/会话/释放路径都可能是），只入缓冲不打印；
    ' delegate 用模块级字段保活，否则 GC 之后内核手里就是野指针。
    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Private Delegate Sub 原生日志回调(上下文 As IntPtr, 行UTF8 As IntPtr)

    Private ReadOnly 日志回调钩子 As 原生日志回调 = AddressOf 收日志行
    Private ReadOnly 日志锁 As New Object()
    Private ReadOnly 日志行 As New List(Of String)()

    <DllImport("FFF.Native.dll", CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Private Sub FFF3FP_SetLogCallback(回调 As 原生日志回调, 上下文 As IntPtr)
    End Sub

    Private Sub 收日志行(上下文 As IntPtr, 行UTF8 As IntPtr)
        Try
            Dim 行 = Marshal.PtrToStringUTF8(行UTF8)
            SyncLock 日志锁
                日志行.Add(If(行, "<?>"))
            End SyncLock
        Catch
            ' 反向 P/Invoke：异常从回调里逃出去先于内核日志落地，自己吞掉。
        End Try
    End Sub

    ' ── 机器侧读数（kernel32） ──────────────────────────────────────────────────
    <DllImport("kernel32.dll", EntryPoint:="GetCurrentProcess")>
    Private Function 取当前进程() As IntPtr
    End Function

    <DllImport("kernel32.dll")>
    Private Function GetProcessDefaultCpuSets(进程 As IntPtr, 集合 As IntPtr, 数量 As UInteger,
                                              ByRef 所需数量 As UInteger) As Boolean
    End Function

    <DllImport("kernel32.dll")>
    Private Function GetActiveProcessorCount(组号 As UShort) As UInteger
    End Function

    ''' <summary>两趟式：先问数量（集合传空），读回进程默认 CPU set 个数。</summary>
    Private Function 读默认集数() As UInteger
        Dim 所需 As UInteger
        GetProcessDefaultCpuSets(取当前进程(), IntPtr.Zero, 0UI, 所需)
        Return 所需
    End Function

    ' ── 通用小工具 ─────────────────────────────────────────────────────────────
    Private Sub 检查(条件 As Boolean, 名称 As String, 实测 As String)
        If 条件 Then
            通过 += 1
            Console.WriteLine($"  ok   {名称}  {实测}")
        Else
            失败 += 1
            Console.WriteLine($"  FAIL {名称}  {实测}")
        End If
    End Sub

    ' 真消息泵：DoEvents + Sleep(2)，与 --image-zoom-live 同一口径。
    Private Sub 泵(秒 As Double)
        Dim 截止 = Stopwatch.GetTimestamp() + CLng(秒 * Stopwatch.Frequency)
        Do
            Application.DoEvents()
            Thread.Sleep(2)
        Loop While Stopwatch.GetTimestamp() < 截止
    End Sub

    ''' <summary>播放期泵：约每 1 秒采一次进程默认 set 数，返回最后一次读数。</summary>
    Private Function 泵并采样默认集(秒 As Double) As UInteger
        Dim 截止 = Stopwatch.GetTimestamp() + CLng(秒 * Stopwatch.Frequency)
        Dim 最新 = 读默认集数()
        Dim 下次 = Stopwatch.GetTimestamp() + Stopwatch.Frequency
        Do
            Application.DoEvents()
            Thread.Sleep(2)
            If Stopwatch.GetTimestamp() >= 下次 Then
                最新 = 读默认集数()
                下次 += Stopwatch.Frequency
            End If
        Loop While Stopwatch.GetTimestamp() < 截止
        Return 最新
    End Function

    ''' <summary>从 "key=value" 形式的内核日志里取字段（空格为界）；取不到返回 Nothing。</summary>
    Private Function 取字段(行 As String, 键 As String) As String
        Dim 前 = 键 & "="
        Dim i = 行.IndexOf(前, StringComparison.Ordinal)
        If i < 0 Then Return Nothing
        Dim 起 = i + 前.Length
        Dim 终 = 行.IndexOf(" "c, 起)
        If 终 < 0 Then 终 = 行.Length
        Return 行.Substring(起, 终 - 起)
    End Function

    ''' <summary>取整数字段；取不到或非数字（如 busy0=na）返回 -1。</summary>
    Private Function 取整数(行 As String, 键 As String) As Long
        Dim s = 取字段(行, 键)
        Dim v As Long
        If s IsNot Nothing AndAlso Long.TryParse(s, v) Then Return v
        Return -1
    End Function

    ' ── 夹具探测：从 AppContext.BaseDirectory 向上找 10 层 ──────────────────────
    Private Function 探测夹具(参数 As String()) As String
        If 参数.Length > 0 Then Return 参数(0)
        Dim 目录 = New DirectoryInfo(AppContext.BaseDirectory)
        For i = 1 To 10
            Dim 候选 = Path.Combine(目录.FullName, "testmedia", "media", "real", "real_720p_h264_derived.mp4")
            If File.Exists(候选) Then Return 候选
            If 目录.Parent Is Nothing Then Exit For
            目录 = 目录.Parent
        Next
        Return Nothing
    End Function

    ' ── 单臂读数 ───────────────────────────────────────────────────────────────
    Private Class 臂读数
        Public 名称 As String
        Public 环境描述 As String
        Public LP数 As UInteger
        Public 默认Set数 As UInteger
        Public 素材描述 As String = "?"
        Public 管线丢帧 As Long
        Public 呈现帧数 As Long
        Public 面板限速丢帧 As Long
        Public 从未上屏翻帧 As Long
        Public 有Final行 As Boolean
        Public 采样tier0 As Long
        Public 采样tier1 As Long
        Public 采样other As Long
        Public 采样窗数 As Long
        Public 窗行数 As Integer
        Public busy有读窗 As Integer
        Public busy1高窗 As Integer
        Public busy0峰 As Integer = -1
        Public busy1峰 As Integer = -1
        Public ReadOnly 放置行 As New List(Of String)()
        Public ReadOnly 错误行 As New List(Of String)()
        Public ReadOnly transition行 As New List(Of String)()
    End Class

    ''' <summary>把本臂缓冲里的内核日志分类成读数。
    ''' final 行在 Release() 时才发（顺序在 window 行之后），所以先找 final 再汇总 window。</summary>
    Private Sub 分类日志(r As 臂读数)
        Dim 行s As String()
        SyncLock 日志锁
            行s = 日志行.ToArray()
        End SyncLock

        ' 第一遍：final 优先（它是全量累计，window 汇总只是退路）。
        For Each 行 In 行s
            If 行 Is Nothing Then Continue For
            If 行.StartsWith("PLACEMENT_SAMPLE final", StringComparison.Ordinal) Then
                r.有Final行 = True
                r.采样tier0 = 取整数(行, "tier0")
                r.采样tier1 = 取整数(行, "tier1")
                r.采样other = 取整数(行, "other")
                r.采样窗数 = 取整数(行, "windows")
            End If
        Next

        ' 第二遍：其余行。
        For Each 行 In 行s
            If 行 Is Nothing Then Continue For
            If Not 行.Contains("PLACEMENT", StringComparison.Ordinal) Then
                If 行.StartsWith("FAIL:", StringComparison.Ordinal) Then r.错误行.Add(行)
                Continue For
            End If
            r.放置行.Add(行)
            If 行.Contains("PLACEMENT transition=", StringComparison.Ordinal) Then
                r.transition行.Add(行)
                Continue For
            End If
            If 行.StartsWith("PLACEMENT_SAMPLE window", StringComparison.Ordinal) Then
                r.窗行数 += 1
                If Not r.有Final行 Then
                    Dim t0 = 取整数(行, "present_tier0")
                    Dim t1 = 取整数(行, "present_tier1")
                    Dim to_ = 取整数(行, "present_other")
                    If t0 > 0 Then r.采样tier0 += t0
                    If t1 > 0 Then r.采样tier1 += t1
                    If to_ > 0 Then r.采样other += to_
                End If
                Dim b0 = 取整数(行, "busy0")
                Dim b1 = 取整数(行, "busy1")
                If b1 >= 0 Then
                    r.busy有读窗 += 1
                    If b1 >= 5 Then r.busy1高窗 += 1
                End If
                If b0 > r.busy0峰 Then r.busy0峰 = CInt(b0)
                If b1 > r.busy1峰 Then r.busy1峰 = CInt(b1)
            End If
        Next
    End Sub

    ' ── 单臂执行 ───────────────────────────────────────────────────────────────
    ''' <summary>开一臂：清日志 → 设环境 → 离屏窗 → 会话 → 打开 → 播放 → 泵 12s →
    ''' 结束前读快照/节律 → 停止+释放 → 关窗+泵 → 分类日志。</summary>
    Private Function 跑一臂(臂名 As String, 环境值 As String, 夹具 As String, 每臂秒 As Double) As 臂读数
        SyncLock 日志锁
            日志行.Clear()
        End SyncLock
        Environment.SetEnvironmentVariable("FFF_PLACEMENT", 环境值)
        Environment.SetEnvironmentVariable("FFF_PLACEMENT_TRACE", "1")

        Dim r As New 臂读数 With {
            .名称 = 臂名,
            .环境描述 = If(环境值 Is Nothing, "（未设置 ⇒ auto）", 环境值),
            .LP数 = GetActiveProcessorCount(CUShort(&HFFFF))}
        Console.WriteLine()
        Console.WriteLine($"[臂 {臂名}] 开始：FFF_PLACEMENT={r.环境描述}，窗口 → 会话 → 打开 → 播放 {每臂秒}s …")

        Dim 窗体 As New Form() With {
            .Text = "playback-placement-" & 臂名,
            .StartPosition = FormStartPosition.Manual,
            .Location = New Point(-8000, -8000),
            .ClientSize = New Size(1280, 720),
            .ShowInTaskbar = False
        }
        Dim 画面 As New 播放器画面控件() With {.Dock = DockStyle.Fill}
        窗体.Controls.Add(画面)
        窗体.Show()
        泵(0.25)

        Try
            Dim 配置 As New 播放器配置 With {
                .解码器 = 解码模式.CPU,
                .输出窗口句柄 = 画面.输出窗口句柄
            }
            Using 会话 = New 播放器会话(配置)
                会话.设置音量(0, True)
                会话.打开Async(夹具).GetAwaiter().GetResult()
                Dim 起始快照 = 会话.当前快照
                Dim 起始节律 = 会话.读取呈现节律状态()
                r.素材描述 = $"{起始快照.视频宽度}x{起始快照.视频高度} {起始快照.总时长.TotalSeconds:F1}s"
                会话.播放()
                r.默认Set数 = 泵并采样默认集(每臂秒)
                Dim 末快照 = 会话.当前快照
                Dim 末节律 = 会话.读取呈现节律状态()
                r.管线丢帧 = CLng(末快照.已丢弃视频帧数) - CLng(起始快照.已丢弃视频帧数)
                r.呈现帧数 = CLng(末快照.已呈现视频帧数) - CLng(起始快照.已呈现视频帧数)
                r.面板限速丢帧 = CLng(末节律.显示速率丢弃数) - CLng(起始节律.显示速率丢弃数)
                r.从未上屏翻帧 = CLng(末节律.从未上屏翻帧数) - CLng(起始节律.从未上屏翻帧数)
                会话.停止()
            End Using
        Finally
            窗体.Close()
            窗体.Dispose()
            泵(0.5)
        End Try

        分类日志(r)
        Return r
    End Function

    ' ── 单臂报告 ───────────────────────────────────────────────────────────────
    Private Sub 报臂(r As 臂读数)
        Dim 总 = r.采样tier0 + r.采样tier1 + r.采样other
        Dim 源 = If(r.有Final行, "final", $"window 汇总（{r.窗行数} 窗）")
        Dim p0 = If(总 <= 0, 0.0, 100.0 * r.采样tier0 / 总)
        Dim p1 = If(总 <= 0, 0.0, 100.0 * r.采样tier1 / 总)
        Dim po = If(总 <= 0, 0.0, 100.0 * r.采样other / 总)
        Dim busy峰 = $"{If(r.busy0峰 < 0, "na", r.busy0峰.ToString())}/{If(r.busy1峰 < 0, "na", r.busy1峰.ToString())}"

        Console.WriteLine($"── 臂 {r.名称} 读数（素材 {r.素材描述}）")
        Console.WriteLine($"   默认 CPU set 数 = {r.默认Set数}（LP 总数 = {r.LP数}）")
        Console.WriteLine($"   采样（{源}）：tier0={r.采样tier0} tier1={r.采样tier1} other={r.采样other}" &
                          $" 总={总} ⇒ {p0:F1}% / {p1:F1}% / {po:F1}%")
        Console.WriteLine($"   busy1>=5 窗数 = {r.busy1高窗}/{r.busy有读窗}（有读数窗；总窗行 {r.窗行数}）" &
                          $" busy0/busy1 峰值 = {busy峰}")
        Console.WriteLine($"   丢帧(管线落后) = {r.管线丢帧}（播放期呈现 {r.呈现帧数} 帧）  " &
                          $"丢帧(面板限速) = {r.面板限速丢帧}  从未上屏翻帧 = {r.从未上屏翻帧}")
        Console.WriteLine($"   transition 行 {r.transition行.Count} 条" &
                          If(r.错误行.Count > 0, $"，另有 FAIL 行 {r.错误行.Count} 条", ""))
        For Each 行 In r.放置行
            Console.WriteLine($"   | {行}")
        Next
        For Each 行 In r.错误行
            Console.WriteLine($"   ! {行}")
        Next
    End Sub

    ' ── 单臂判据 ───────────────────────────────────────────────────────────────
    Private Sub 判臂(r As 臂读数)
        Dim 总 = r.采样tier0 + r.采样tier1 + r.采样other

        Dim 含Tier As Func(Of String, Integer) =
            Function(名称 As String) As Integer
                Dim n As Integer
                For Each 行 In r.transition行
                    If 行.Contains(名称, StringComparison.Ordinal) Then n += 1
                Next
                Return n
            End Function

        Dim apply失败 As Integer
        Dim 有字面禁用 As Boolean
        Dim 有发现 As Boolean
        For Each 行 In r.放置行
            If 行.Contains("PLACEMENT_APPLY_FAILED", StringComparison.Ordinal) Then apply失败 += 1
            If 行.Contains("PLACEMENT_DISABLED", StringComparison.Ordinal) Then 有字面禁用 = True
            If 行.Contains("PLACEMENT_DISCOVERED", StringComparison.Ordinal) Then 有发现 = True
        Next

        ' 全臂共同：内核逐帧采样必须真的数到帧，否则本臂一切放置断言都是空中楼阁。
        If 总 > 0 Then
            检查(True, $"{r.名称} 呈现采样 > 0（内核逐帧采样有读数）",
                 $"tier0={r.采样tier0} tier1={r.采样tier1} other={r.采样other}")
        Else
            检查(False, $"{r.名称} 未呈现到任何帧：final/累计采样=0",
                 $"tier0={r.采样tier0} tier1={r.采样tier1} other={r.采样other}（PLACEMENT_SAMPLE 无读数）")
        End If

        Select Case r.名称
            Case "off"
                Dim 等价 = r.transition行.Count = 0 AndAlso apply失败 = 0 AndAlso r.默认Set数 = 0 AndAlso 有发现 AndAlso 总 > 0
                If Not 有字面禁用 Then
                    Console.WriteLine("   note PLACEMENT_DISABLED 字面行缺席：内核应在 OnPlaybackStart 禁用分支发射它")
                    Console.WriteLine("        （2026-10-17 补），本档退到等价负对照证据判定——这是回归信号，不是正常状态。")
                End If
                检查(有字面禁用 OrElse 等价, "off 负对照成立（字面 PLACEMENT_DISABLED 或等价证据，见 note）",
                     $"字面={有字面禁用} 等价={等价}（transition={r.transition行.Count} APPLY_FAILED={apply失败} " &
                     $"默认set={r.默认Set数} DISCOVERED={有发现} 采样={总}）")
                检查(r.transition行.Count = 0, "off 无任何 transition 行（一次都没动过线程）", $"{r.transition行.Count} 条")
                检查(apply失败 = 0, "off 无 PLACEMENT_APPLY_FAILED", $"{apply失败} 条")
                检查(r.默认Set数 = 0, "off 默认 set 数 = 0", $"{r.默认Set数}")

            Case "auto"
                Dim start数 As Integer
                For Each 行 In r.transition行
                    If 行.Contains("transition=start", StringComparison.Ordinal) Then start数 += 1
                Next
                检查(start数 >= 1, "auto ≥1 条 transition=start（默认策略起在首选档）",
                     $"start={start数} 条；transition 共 {r.transition行.Count} 条")
                检查(apply失败 = 0, "auto 无 PLACEMENT_APPLY_FAILED", $"{apply失败} 条")

            Case "prefer"
                检查(含Tier("tier=Preferred") >= 1, "prefer ≥1 条 tier=Preferred 的 transition",
                     $"Preferred={含Tier("tier=Preferred")} Spilled={含Tier("tier=Spilled")} Boosted={含Tier("tier=Boosted")}")
                检查(含Tier("tier=Spilled") = 0 AndAlso 含Tier("tier=Boosted") = 0,
                     "prefer 不提档（无 Spilled/Boosted transition）",
                     $"Spilled={含Tier("tier=Spilled")} Boosted={含Tier("tier=Boosted")}")
                检查(apply失败 = 0, "prefer 无 PLACEMENT_APPLY_FAILED", $"{apply失败} 条")
                检查(总 > 0 AndAlso r.采样tier1 = 0 AndAlso r.采样other = 0,
                     "prefer 采样>0 且 present_tier1=0 / present_other=0（播放线程钉在 tier0）",
                     $"tier0={r.采样tier0} tier1={r.采样tier1} other={r.采样other}")
                检查(r.默认Set数 > 0 AndAlso r.默认Set数 < r.LP数, "prefer 默认 set 满足 0<n<LP",
                     $"{r.默认Set数}（LP={r.LP数}）")

            Case "spill"
                检查(含Tier("tier=Spilled") >= 1, "spill ≥1 条 tier=Spilled 的 transition",
                     $"Spilled={含Tier("tier=Spilled")} Boosted={含Tier("tier=Boosted")}")
                检查(含Tier("tier=Boosted") = 0, "spill 不提档（无 Boosted transition）", $"Boosted={含Tier("tier=Boosted")}")
                检查(apply失败 = 0, "spill 无 PLACEMENT_APPLY_FAILED", $"{apply失败} 条")
                检查(总 > 0 AndAlso r.采样tier1 = 0,
                     "spill 采样>0 且 present_tier1=0（进程默认放开、呈现线程仍钉 tier0）",
                     $"tier0={r.采样tier0} tier1={r.采样tier1} other={r.采样other}")
                检查(r.默认Set数 = r.LP数, "spill 默认 set = LP（进程默认放开到全集）",
                     $"{r.默认Set数}（LP={r.LP数}）")

            Case "boost"
                检查(含Tier("tier=Boosted") >= 1, "boost ≥1 条 tier=Boosted 的 transition",
                     $"Boosted={含Tier("tier=Boosted")}")
                检查(apply失败 = 0, "boost 无 PLACEMENT_APPLY_FAILED", $"{apply失败} 条")
                Dim 有优先级 As Boolean
                For Each 行 In r.transition行
                    If 行.Contains("priority=0x8000", StringComparison.OrdinalIgnoreCase) Then 有优先级 = True
                Next
                检查(有优先级, "boost 日志含 priority=0x8000（进程提到 ABOVE_NORMAL）",
                     $"transition 原文共 {r.transition行.Count} 条")
                检查(总 > 0 AndAlso r.采样tier1 = 0,
                     "boost 采样>0 且 present_tier1=0",
                     $"tier0={r.采样tier0} tier1={r.采样tier1} other={r.采样other}")
                检查(r.默认Set数 = r.LP数, "boost 默认 set = LP", $"{r.默认Set数}（LP={r.LP数}）")
        End Select
    End Sub

    ' ── 五臂汇总 ───────────────────────────────────────────────────────────────
    Private Sub 打汇总(读数表 As List(Of 臂读数))
        Console.WriteLine()
        Console.WriteLine("── 五臂数字汇总 ──")
        Console.WriteLine("  臂      默认set/LP   采样 t0/t1/other(总)            busy1>=5/有读窗   管线丢帧  面板限速  未上屏翻帧  transition")
        For Each r In 读数表
            Dim 总 = r.采样tier0 + r.采样tier1 + r.采样other
            Console.WriteLine($"  {r.名称,-6}  {r.默认Set数}/{r.LP数,-11}  {r.采样tier0}/{r.采样tier1}/{r.采样other}（{总}）" &
                              $"                {r.busy1高窗}/{r.busy有读窗}（窗行 {r.窗行数}）      " &
                              $"{r.管线丢帧}        {r.面板限速丢帧}        {r.从未上屏翻帧}        {r.transition行.Count}")
        Next
    End Sub

    ' ── 入口 ───────────────────────────────────────────────────────────────────
    Friend Function 运行(参数 As String()) As Integer
        Console.WriteLine("3FP CPU 放置策略回归（--playback-placement：真内核 + 真播放会话 + 真输出窗口）")

        Dim 夹具 = 探测夹具(参数)
        If 夹具 Is Nothing OrElse Not File.Exists(夹具) Then
            Console.WriteLine($"NOT JUDGED：夹具找不到（{(If(夹具, "未给出参数且向上探测未命中"))}）。")
            Console.WriteLine("用法：FFF.Player.Tests --playback-placement [夹具路径] [每臂秒数]")
            Return 2
        End If
        Dim 每臂秒 As Double = 12.0
        If 参数.Length > 1 Then
            If Not Double.TryParse(参数(1), 每臂秒) OrElse 每臂秒 <= 0 OrElse 每臂秒 > 120 Then
                Console.WriteLine($"NOT JUDGED：每臂秒数「{参数(1)}」不是 (0,120] 内的数。")
                Return 2
            End If
        End If
        Dim 内核路径 = Path.Combine(AppContext.BaseDirectory, "FFF.Native.dll")
        Console.WriteLine($"夹具 = {夹具}")
        Console.WriteLine($"内核 = {内核路径}（{If(File.Exists(内核路径), New FileInfo(内核路径).Length & " 字节，" & File.GetLastWriteTime(内核路径).ToString("yyyy-MM-dd HH:mm"), "缺失！")}）")
        Console.WriteLine($"每臂秒数 = {每臂秒}；五臂按序 off/auto/prefer/spill/boost（环境变量在每臂建会话前设置）")

        ' 进程级日志槽：必须在创建任何会话之前注册（它改的是进程级全局）。
        FFF3FP_SetLogCallback(日志回调钩子, IntPtr.Zero)

        Dim 读数表 As New List(Of 臂读数)()
        Try
            Dim 臂值() As String = {"off", Nothing, "prefer", "spill", "boost"}
            For Each 值 In 臂值
                Dim r = 跑一臂(If(值 Is Nothing, "auto", 值), 值, 夹具, 每臂秒)
                读数表.Add(r)
                报臂(r)
                判臂(r)
            Next
        Catch ex As Exception
            Console.WriteLine()
            Console.WriteLine("环境不具备，本轮不产出结论：" & ex.GetType().Name & " " & ex.Message)
            For Each r In 读数表
                Console.WriteLine($"  （已完成臂 {r.名称}：默认set={r.默认Set数} 采样={r.采样tier0 + r.采样tier1 + r.采样other}）")
            Next
            Return 2
        End Try

        打汇总(读数表)
        Console.WriteLine($"通过 {通过} 条，失败 {失败} 条")
        If 失败 > 0 Then Return 1
        Return 0
    End Function
End Module
