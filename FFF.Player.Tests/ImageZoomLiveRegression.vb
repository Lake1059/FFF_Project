' 图片模式缩放体感 —— 同进程真控件回归
'
' 跑的是产品那一份：真 播放器画面控件（Panel，宽高是真实的）+ 真 播放器图片浏览控制器
' + 真 WinForms 计时器 + 真消息循环。只有"内核"换成假实现 —— 它只做真内核那一件事：
' 乘倍数、按 [0.05,64] 夹、把夹后的值回读给宿主（VideoRenderer.cpp:2153 同一条钳位）。
' 要量的就是宿主在这条链上发了多少包、什么时候停、终值落在哪。
'
' 为什么需要这一档：docs/79 §11.8 的"未测"清单（逐 tick 推进 / 停表 / 连滚合并 / 双击合并 /
' 大 dt 落目标 / 门控不可达）全部只在消息循环跑起来后才观测得到；手点产品界面留不下读数。
'
' S11/S12 补的是**小图档**（源 200×200 < 控件）：内核把适配盒恒封顶在源原生尺寸
' （PlayerSession.cpp:3708/1642 → VideoRenderer.cpp:967-970）⇒ f = Min(1, 681/200) = 1，
' 于是上界 = 64/1 = 64、拖动分母 = 200；按未封顶的 3.405 建模就变成 18.79 与 681。
' 大图档 f<1 封顶不咬，所以旧场景一路绿灯也照样带着这个错 —— 新两条才有牙。
' 这一档的期望值一律写手算字面量（1.25F / 64.0F / 200.0F），不引用 图片缩放几何 的常数，
' 免得断言变成同义反复（拿被测模块自己的 内核最大缩放 当期望值就永远红不了）。
'
' 用法：FFF.Player.Tests --image-zoom-live
' 退出码：0 全绿 / 1 有失败条 / 2 环境不具备（起不了真控件）

Imports System
Imports System.Collections.Generic
Imports System.Diagnostics
Imports System.Runtime.InteropServices
Imports System.Drawing
Imports System.Threading
Imports System.Windows.Forms
Imports FFF.Player

Friend Module ImageZoomLiveRegression
    Private 通过 As Integer
    Private 失败 As Integer

    ' 宿主每一次"下发视图"都被这里收到 —— 这就是 present 驱动次数的一手证据。
    Private 下发次数 As Integer
    Private 轨迹 As New List(Of Single)
    Private 水平记录 As New List(Of Single)
    Private 垂直记录 As New List(Of Single)
    Private 最近提示 As String = ""
    Private 绝对下发 As Integer
    Private 末次戳 As Long
    Private 泵睡毫秒 As Integer = 2
    Private 提示记录 As New List(Of String)

    ' 假内核状态。
    Private 假内核缩放 As Single = 1.0F
    Private Const 假内核下钳 As Single = 0.05F
    Private Const 假内核上钳 As Single = 64.0F
    ' 绝对提交（SetViewTransform）在图片模式是**另一套**界：见 VideoRenderer.cpp:2327-2328。
    ' 与上面那对（滚轮 ZoomViewAt，:2377）刻意不同值，假内核才能重现真内核的分叉。
    Private Const 假内核绝对下钳 As Single = 1.0F
    Private Const 假内核绝对上钳 As Single = 32.0F

    Private 锚定下发 As Integer
    Private 全局窗体 As Form
    Private 全局画面 As 播放器画面控件
    Private 全局控制器 As 播放器图片浏览控制器

    Private Sub 检查(条件 As Boolean, 名称 As String, 实测 As String)
        If 条件 Then
            通过 += 1
            Console.WriteLine($"  ok   {名称}  {实测}")
        Else
            失败 += 1
            Console.WriteLine($"  FAIL {名称}  {实测}")
        End If
    End Sub

    Private Sub 记(文本 As String)
        Console.WriteLine($"       {文本}")
    End Sub

    ''' <summary>绝对提交（SetViewTransform）的落点记录：用来钉住一条不变量 ——
    ''' 图片模式下宿主**只**用它回适应 (1,0,0)。内核这条路径在图片模式是
    ''' [1.0, 32]（VideoRenderer.cpp:2327-2328），与滚轮那条 ZoomViewAt 的
    ''' [0.05, 64]（:2377）**不是一套数**；只要有人开始用它做绝对缩放，
    ''' 这个记录就会先红，而不是等到用户看见"设 48 却停在 32"。</summary>
    Private Class 绝对提交
        Public 缩放 As Single
        Public 水平 As Single
        Public 垂直 As Single
    End Class
    Private ReadOnly 绝对提交记录 As New List(Of 绝对提交)

    Private Sub 下发(缩放 As Single, 水平 As Single, 垂直 As Single)
        下发次数 += 1
        绝对下发 += 1
        末次戳 = Stopwatch.GetTimestamp()
        轨迹.Add(缩放)
        水平记录.Add(水平)
        垂直记录.Add(垂直)
        Dim 记为 As New 绝对提交 With {.缩放 = 缩放, .水平 = 水平, .垂直 = 垂直}
        绝对提交记录.Add(记为)
        ' 假内核必须跟绝对下发同步：真内核的 SetViewTransform 会把绝对值**存下来**。
        ' 不跟的话，宿主每次收尾的绝对写都会让下一拍的乘法回读"变小"，被产品判成
        ' "内核把我们夹低"⇒ 目标被拉回现值（S11 停在 25.2 就是它）。
        ' ⚠ 钳位用的是**图片模式绝对路径那一套** [1.0, 32]（:2227-2328），不是
        ' 滚轮 ZoomViewAt 的 [0.05, 64]。旧注释把两者混为一谈、还引用了 DirectWrite
        ' 的 :2120 行；分开建模才能真的暴露"有人拿绝对路径做缩放"这件事。
        假内核缩放 = Math.Min(假内核绝对上钳, Math.Max(假内核绝对下钳, 缩放))
    End Sub

    Private Sub 提示(文本 As String)
        最近提示 = 文本
        提示记录.Add(文本)
    End Sub

    Private Function 锚定(倍数 As Single, 锚点水平 As Single, 锚点垂直 As Single,
                          ByRef 结果缩放 As Single) As Boolean
        假内核缩放 = Math.Min(假内核上钳, Math.Max(假内核下钳, 假内核缩放 * 倍数))
        结果缩放 = 假内核缩放
        下发次数 += 1
        锚定下发 += 1
        末次戳 = Stopwatch.GetTimestamp()
        轨迹.Add(结果缩放)
        Return True
    End Function

    ' 合并后控制器是上游 8 参构造器，补足后三个委托（假内核读数 / 平移 / 旋转）。
    Private Function 读取缩放(ByRef 当前倍率 As Single, ByRef 最小倍率 As Single,
                              ByRef 最大倍率 As Single) As Boolean
        当前倍率 = 假内核缩放
        最小倍率 = 假内核下钳
        最大倍率 = 假内核上钳
        Return True
    End Function

    Private Function 平移(水平 As Single, 垂直 As Single) As Boolean
        水平记录.Add(水平)
        垂直记录.Add(垂直)
        Return True
    End Function

    Private Function 当前旋转() As UInteger
        Return 0
    End Function

    Private Sub 泵(秒 As Double)
        Dim 截止 = Stopwatch.GetTimestamp() + CLng(秒 * Stopwatch.Frequency)
        Do
            Application.DoEvents()
            Thread.Sleep(泵睡毫秒)
        Loop While Stopwatch.GetTimestamp() < 截止
    End Sub

    Private Function 末值() As Single
        If 轨迹.Count = 0 Then Return -1.0F
        Return 轨迹(轨迹.Count - 1)
    End Function

    Private Function 峰值() As Single
        Dim 最高 = Single.NegativeInfinity
        For Each v In 轨迹
            If v > 最高 Then 最高 = v
        Next
        Return If(轨迹.Count = 0, -1.0F, 最高)
    End Function

    Private Function 单调不减() As Boolean
        For i = 1 To 轨迹.Count - 1
            If 轨迹(i) < 轨迹(i - 1) - 0.000001F Then Return False
        Next
        Return True
    End Function

    ''' <summary>开一档干净场景：进图片模式（会复位视图）→ 清计数 → 假内核同步到 1.0。</summary>
    Private Sub 场景开始(源宽 As UInteger, 源高 As UInteger)
        全局控制器.测试进入图片模式(源宽, 源高)
        假内核缩放 = 1.0F
        下发次数 = 0
        绝对下发 = 0
        锚定下发 = 0
        轨迹.Clear()
        水平记录.Clear()
        垂直记录.Clear()
        提示记录.Clear()
        最近提示 = ""
    End Sub

    <DllImport("user32.dll")>
    Private Function SendMessageW(h As IntPtr, msg As UInteger, wParam As IntPtr, lParam As IntPtr) As IntPtr
    End Function

    ' 真投一条 WM_MOUSEWHEEL 到产品的输出子窗。补的是注入缝绕过去那一层：
    ' 画面控件的滚轮分流有两处门控（OnMouseWheel 与 视频输出窗口_MouseWheel），
    ' 非图片模式必须落到音量分支 —— 此前只有手点产品才能验，也正是 GUI 档归因不上的那个问题。
    Private Sub 投滚轮(目标 As IntPtr, delta As Integer, 屏幕x As Integer, 屏幕y As Integer)
        Dim wp = New IntPtr((delta And &HFFFF) << 16)
        Dim lpValue As Integer = (屏幕x And &HFFFF) Or ((屏幕y And &HFFFF) << 16)
        SendMessageW(目标, &H20A, wp, New IntPtr(lpValue))
    End Sub

    Friend Function 运行(参数 As String()) As Integer
        If 参数.Length > 0 AndAlso Integer.TryParse(参数(0), 泵睡毫秒) Then
            Console.WriteLine($"泵粒度 = Thread.Sleep({泵睡毫秒}) —— 用来分清「tick 慢」是产品还是量具的锅")
        End If
        Console.WriteLine("图片缩放体感回归（真控件 + 真计时器 + 真消息循环）")
        Dim 窗体 As Form = Nothing
        Try
            全局窗体 = New Form() With {
                .Text = "image-zoom-live",
                .StartPosition = FormStartPosition.Manual,
                .Location = New Point(-8000, -8000),
                .FormBorderStyle = FormBorderStyle.Sizable,
                .ClientSize = New Size(1264, 681),
                .ShowInTaskbar = False
            }
            全局画面 = New 播放器画面控件() With {.Dock = DockStyle.Fill}
            全局窗体.Controls.Add(全局画面)
            全局控制器 = New 播放器图片浏览控制器(全局画面, AddressOf 下发,
                                              AddressOf 忽略切换,
                                              AddressOf 提示,
                                              AddressOf 锚定,
                                              AddressOf 平移,
                                              AddressOf 当前旋转,
                                              AddressOf 读取缩放)
            全局窗体.Show()
            泵(0.25)

            ' 控件宽高必须是**真的**：几何层用它算 f，进而算 1:1 与上限。
            Dim 视口宽 = 全局画面.Width
            Dim 视口高 = 全局画面.Height
            If 视口宽 < 400 OrElse 视口高 < 300 Then
                Console.WriteLine($"控件没拿到真实尺寸（{视口宽}x{视口高}），本轮不产出结论。")
                Return 2
            End If
            记($"控件真实尺寸 = {视口宽}x{视口高}")

            RunS1_to_S3()
            RunS4_bound()
            RunS5_doubleclick()
            RunS6_unreachable()
            RunS7_bigdt()
            RunS8_gating()
            RunS9_real_routing()
            RunS10_drag_follows_pointer()
            RunS11_small_image()
            RunS12_doubleclick_small_image()

            ' ── S13 绝对提交不变量：把"两套钳位分叉"从隐患变成有人看着的判据 ──────
            ' 内核图片模式下两条入口的界**不是同一套**：绝对路径 [1.0, 32]
            ' （VideoRenderer.cpp:2327-2328），滚轮路径 [0.05, 64]（:2377）。
            ' 目前宿主只用绝对路径回适应（播放器图片浏览控制器.vb:157 唯一调用点，
            ' 值恒为 (1,0,0)），所以分叉**不可达**——"不可达"这个结论必须由判据守着，
            ' 而不是靠注释。变异证明：把 :157 的 1.0F 改成 48.0F ⇒ 本组立刻红。
            Dim 绝对次数 = 0
            Dim 越界次数 = 0
            Dim 非回适应次数 = 0
            For Each a In 绝对提交记录
                绝对次数 += 1
                If a.缩放 < 假内核绝对下钳 OrElse a.缩放 > 假内核绝对上钳 Then 越界次数 += 1
                If a.缩放 <> 1.0F OrElse a.水平 <> 0.0F OrElse a.垂直 <> 0.0F Then 非回适应次数 += 1
            Next
            检查(绝对次数 > 0, "S13 绝对提交路径确实被走过（为 0 则本组判据是空的）", $"{绝对次数} 次")
            检查(越界次数 = 0, "S13 绝对提交全部落在图片模式绝对界 [1.0,32] 内",
                 $"越界 {越界次数} / 共 {绝对次数}")
            检查(非回适应次数 = 0, "S13 绝对提交只有回适应 (1,0,0) —— 不再唯一即两套界分叉可达",
                 $"非回适应 {非回适应次数} / 共 {绝对次数}")
            ' 读数必须跟着判据走：这里一度硬印"全部为回适应"，变异跑里明明 14 次越界
            ' 却照样这么写 —— 判据红了而日志说没事，那是另一种假绿。
            记($"S13 绝对提交 {绝对次数} 次：越界 {越界次数}、非回适应 {非回适应次数}" &
                $"（两套界 [1.0,32] vs [0.05,64] 的分叉当前{(If(非回适应次数 = 0, "不可达", "已可达"))}）")

            Console.WriteLine($"通过 {通过} 条，失败 {失败} 条")
            If 失败 > 0 Then Return 1
            Return 0
        Catch ex As Exception
            Console.WriteLine("环境不具备，本轮不产出结论：" & ex.GetType().Name & " " & ex.Message)
            Return 2
        Finally
            全局控制器?.Dispose()
            全局窗体?.Dispose()
        End Try
    End Function

    Private Sub 忽略切换(方向 As Integer)
    End Sub

    ' ── S1/S2/S3：单格轨迹、停表、连滚合并 ────────────────────────────────────
    Private Sub RunS1_to_S3()
        场景开始(12000UI, 9000UI)
        Dim f = 图片缩放几何.单位屏幕比(视口宽(), 视口高(), 12000UI, 9000UI)
        Dim 上限 = 图片缩放几何.最大缩放(f)

        Dim 发起戳 = Stopwatch.GetTimestamp()
        全局控制器.测试滚轮(120, CInt(视口宽() * 0.75), CInt(视口高() * 0.3))
        泵(0.6)
        Dim 用时ms = CSng((末次戳 - 发起戳) * 1000.0 / Stopwatch.Frequency)
        Dim 静止时次数 = 下发次数
        泵(0.4)
        Dim 期望 = Math.Min(1.25F, 上限)
        ' ── 下发次数窗口：按临界阻尼解析式独立手算，不抄实测值 ──────────────────
        ' 控制器用的是 ω = 8/τ，τ = 300ms ⇒ ω = 26.667 s⁻¹。一格滚轮：
        '   目标 = ln(1.25) = 0.223144，d₀ = −0.223144，v₀ = 0
        '   |x−目标| = |d₀|(1+u)e^{−u}，|v| = ω|d₀|·u·e^{−u}，u ≡ ωt
        ' 停表双阈值（几何层 已静止，按目标幅度缩放并各留下限）：
        '   位移 ε = max(0.0002, 0.223144×0.004) = 0.000893 ⇒ (1+u)e^{−u}=0.004 ⇒ u≈7.6
        '   速度 ε = max(0.0005, 0.223144×0.02)  = 0.004463 ⇒ u·e^{−u}=7.5e−4     ⇒ u≈9.45
        ' 取更慢的那个 ⇒ t = 9.45/26.667 = 0.354 s。计时器请求 1ms、实跑约 7.7~8.2ms
        ' ⇒ 期望落点 43~46 次；把 tick 容许范围放宽到 6.4~11.8ms ⇒ **30~55**。
        ' 交叉核对：同一次运行里"末次下发 80~500ms"那条独立地卡住 354ms，两者一致。
        ' ⚠ 双向变异实测过（改的就是上面那个 τ）：τ 300→3000 ⇒ 实跑 **124 次**、17 条红；
        ' τ 300→30 ⇒ 实跑 **4 次**、2 条红。两个方向都跳出窗口，说明这条判据确实咬住
        ' 阻尼常数，不是照实测结果放宽出来的带。（解析式在 τ×10 时给的 430 是高估：
        ' 其它判据先崩、动画被提前停掉，真值 124。以实测为准。）
        检查(下发次数 >= 30 AndAlso 下发次数 <= 55, "S1 单格滚轮的下发次数落在 30~55（解析 43~46）",
             $"{下发次数} 次（锚定 {锚定下发} + 绝对 {绝对下发}）")
        检查(Math.Abs(末值() - 期望) < 0.002, "S1 终值就是目标（钳位后）", $"末={末值():F4} 期望={期望:F4}")
        检查(峰值() <= 期望 + 0.0005F, "S1 临界阻尼不过冲", $"峰={峰值():F4}")
        检查(单调不减(), "S1 轨迹单调（不回抽）", $"{轨迹.Count} 点")
        检查(下发次数 = 静止时次数, "S2 静止后再等 0.4s 零新增（停表真的发生了）",
             $"{静止时次数} → {下发次数}")
        ' 纯函数层预测 208~240ms；接线后必须落在同一个量级，否则说明计时器根本没按标称跑。
        检查(用时ms >= 80.0F AndAlso 用时ms <= 500.0F, "S1 真计时器下滚轮到末次下发 80~500ms",
             $"{用时ms:F0} ms（{下发次数} 次下发 ⇒ 实测 tick 周期≈{用时ms / Math.Max(1, 下发次数 - 1):F1} ms）")

        ' S3：连滚 5 格，中途不泵 —— 目标是合并而不是重启 5 段动画。
        场景开始(12000UI, 9000UI)
        For i = 1 To 5
            全局控制器.测试滚轮(120, CInt(视口宽() * 0.75), CInt(视口高() * 0.3))
        Next
        泵(1.0)
        Dim 连滚目标 = Math.Min(CSng(Math.Pow(1.25F, 5)), 上限)
        检查(Math.Abs(末值() - 连滚目标) < 0.01, "S3 连滚 5 格合到 1.25^5",
             $"末={末值():F4} 期望={连滚目标:F4}")
        检查(下发次数 <= 6 * 静止时次数, "S3 连滚没有变成 5 段独立动画", $"{下发次数} 次（单格={静止时次数}）")
        检查(单调不减(), "S3 连滚轨迹单调", $"{轨迹.Count} 点")
    End Sub

    ' ── S4：顶到上限后不再增长，并给一句反馈 ──────────────────────────────────
    Private Sub RunS4_bound()
        场景开始(12000UI, 9000UI)
        Dim f = 图片缩放几何.单位屏幕比(视口宽(), 视口高(), 12000UI, 9000UI)
        Dim 上限 = 图片缩放几何.最大缩放(f)
        For i = 1 To 40
            全局控制器.测试滚轮(120, 600, 200)
        Next
        泵(1.2)
        检查(Math.Abs(末值() - 上限) < 0.01, "S4 连滚 40 格停在上限", $"末={末值():F2} 上限={上限:F2}")
        Dim 到过界 = False
        For Each m In 提示记录
            If m.Contains("已到最大缩放") Then 到过界 = True
        Next
        检查(最近提示.Contains("已到最大缩放"), "S4 末句就说到界（曾被静止提示顶掉，已修）",
             $"共 {提示记录.Count} 句，末句「{最近提示}」")
    End Sub

    ' ── S5：双击 适应 ↔ 1:1，且 1:1 的值与几何层独立算的一致 ──────────────────
    Private Sub RunS5_doubleclick()
        场景开始(12000UI, 9000UI)
        全局控制器.测试双击(600, 200)
        泵(0.9)
        Dim f = 图片缩放几何.单位屏幕比(视口宽(), 视口高(), 12000UI, 9000UI)
        Dim 一比一 = 图片缩放几何.钳制缩放(图片缩放几何.一比一缩放(f),
                                       图片缩放几何.最小缩放(),
                                       图片缩放几何.最大缩放(f))
        检查(Math.Abs(末值() - 一比一) < 0.02, "S5 双击到 1:1 = 1/f（真实控件尺寸算出）",
             $"末={末值():F4} 期望={一比一:F4}（f={f:F6}）")
        ' 独立手算的一票（D2）：期望不取自几何层，只取自**实测控件尺寸**与源尺寸 ——
        ' 1:1 = 1/f，而 f 是两轴里更小的那个：min(控件宽/12000, 控件高/9000)。
        ' 名义 1264×681 下 = min(0.105333, 0.0756667) = 0.0756667 ⇒ 1/f = 9000/681 = 13.2158。
        ' 这条能独立抓住"1:1 档整体差 f 倍"那一类单位错误，上面那条对照不了。
        Dim 手算f = Math.Min(CSng(视口宽()) / 12000.0F, CSng(视口高()) / 9000.0F)
        Dim 手算一比一 = 1.0F / 手算f
        检查(Math.Abs(末值() - 手算一比一) < 0.02, "S5 1:1 也等于手算 1/min(宽/12000,高/9000)",
             $"末={末值():F4} 期望={手算一比一:F4}（视口 {视口宽()}x{视口高()}）")
        全局控制器.测试双击(600, 200)
        泵(0.9)
        检查(Math.Abs(末值() - 1.0F) < 0.002, "S5 再双击回适应档 1.0", $"末={末值():F4}")
    End Sub

    ' ── S6：够不到 1:1 时必须说清楚，不能把"停在上限"讲成"已到 1:1" ────────────
    Private Sub RunS6_unreachable()
        全局窗体.ClientSize = New Size(200, 200)
        泵(0.2)
        If 全局画面.Width > 400 OrElse 全局画面.Height > 300 Then
            检查(False, "S6 视口没能真的缩小（Dock=Fill 下改 Panel.Size 无效）",
                 $"{全局画面.Width}x{全局画面.Height}")
            全局窗体.ClientSize = New Size(1264, 681)
            泵(0.2)
            Return
        End If
        场景开始(16384UI, 2000UI)
        全局控制器.测试双击(100, 100)
        泵(0.9)
        检查(Not 图片缩放几何.一比一可达(全局画面.Width, 全局画面.Height, 16384UI, 2000UI),
             "S6 判据认定该组合够不到 1:1", $"{全局画面.Width}x{全局画面.Height}")
        检查(Math.Abs(末值() - 假内核上钳) < 0.02, "S6 实际停在内核上限而不是谎称 1:1", $"末={末值():F2}")
        检查(最近提示.Contains("够不到 1:1"), "S6 末句仍带着「够不到 1:1」没被顶掉", $"提示「{最近提示}」")
        全局窗体.ClientSize = New Size(1264, 681)
        泵(0.2)
    End Sub

    ' ── S7：消息循环被拖住（大 dt）时必须落到目标并停表，不许慢慢补 ────────────
    Private Sub RunS7_bigdt()
        场景开始(12000UI, 9000UI)
        全局控制器.测试滚轮(120, 900, 200)
        Thread.Sleep(500)          ' 故意不泵：制造一次远超 0.1s 的 tick 间隔
        泵(0.5)
        Dim 增长 = 下发次数
        检查(增长 <= 6, "S7 被拖住后下发不爆炸", $"{增长} 次")
        检查(Math.Abs(末值() - 1.25F) < 0.002, "S7 大 dt 直接落到目标", $"末={末值():F4}")
    End Sub

    ' ── S8：图片模式关掉后，滚轮根本到不了视图层（铁律 2 的"不可达"这一侧）────
    Private Sub RunS8_gating()
        场景开始(12000UI, 9000UI)
        全局控制器.测试退出图片模式()
        下发次数 = 0
        轨迹.Clear()
        For i = 1 To 5
            全局控制器.测试滚轮(120, 900, 200)
        Next
        全局控制器.测试双击(900, 200)
        泵(0.5)
        检查(下发次数 = 0, "S8 关闭图片模式后滚轮/双击零下发（不可达）", $"{下发次数} 次")
    End Sub

    ' ── S9：真消息进真控件 —— 验滚轮分流这一层 ──────────────────────────────
    Private Sub RunS9_real_routing()
        If 全局画面.Controls.Count = 0 Then
            检查(False, "S9 拿不到输出子窗", $"{全局画面.Controls.Count} 个")
            Return
        End If
        Dim 输出面 = 全局画面.Controls(0)
        场景开始(12000UI, 9000UI)
        下发次数 = 0
        轨迹.Clear()
        Dim r = 输出面.RectangleToScreen(输出面.ClientRectangle)
        Dim cx = r.Left + r.Width \ 2
        Dim cy = r.Top + r.Height \ 2
        投滚轮(输出面.Handle, 120, cx, cy)
        泵(0.8)
        检查(下发次数 >= 6, "S9 真 WM_MOUSEWHEEL 走完分流并驱动动画",
             $"{下发次数} 次下发（子窗 {输出面.GetType().Name} {r.Width}x{r.Height}）")
        检查(轨迹.Count > 0 AndAlso Math.Abs(轨迹(轨迹.Count - 1) - 1.25F) < 0.02,
             "S9 真消息那一格最终落在 1.25", If(轨迹.Count = 0, "无下发", $"末={轨迹(轨迹.Count - 1):F4}"))

        ' 反向门控：退出图片模式后同一条消息不得再驱动缩放。
        场景开始(12000UI, 9000UI)
        全局控制器.测试退出图片模式()
        下发次数 = 0
        轨迹.Clear()
        For i = 1 To 4
            投滚轮(输出面.Handle, 120, cx, cy)
        Next
        泵(0.5)
        检查(下发次数 = 0, "S9 退出图片模式后真消息零缩放下发（该走音量）", $"{下发次数} 次")
    End Sub

    ' ── S10：真鼠标 按下-移动-抬起，量"画面位移 == 手指位移"吗 ──────────────
    ' 内核摆画面的公式（读自源码）：offsetX = panX * |zoomedWidth − fittedWidth| / 2，
    ' 且 zoomedWidth = fittedWidth * zoom ⇒ 一格 pan 换成屏幕位移的分母是 **适配盒宽**，
    ' 不是控件宽。宿主若用控件宽，在非限制轴上就按比例算错 —— 这不需要靠眼判，能算。
    Private Sub RunS10_drag_follows_pointer()
        If 全局画面.Controls.Count = 0 Then
            检查(False, "S10 拿不到输出子窗", $"{全局画面.Controls.Count} 个")
            Return
        End If
        Dim 输出面 = 全局画面.Controls(0)
        场景开始(12000UI, 9000UI)
        全局控制器.测试双击(600, 300)
        泵(1.2)
        Dim z = If(轨迹.Count > 0, 轨迹(轨迹.Count - 1), -1.0F)
        Dim x0 = 300, y0 = 300, dx = 120
        下发次数 = 0
        轨迹.Clear()
        水平记录.Clear()
        垂直记录.Clear()
        ' WM_LBUTTONDOWN=0x0201 WM_MOUSEMOVE=0x0200 WM_LBUTTONUP=0x0202 MK_LBUTTON=1
        ' lParam 用该窗口的客户区坐标（合成消息不经过系统光标换算）。
        投鼠标(输出面.Handle, &H201, New IntPtr(1), x0, y0)
        投鼠标(输出面.Handle, &H200, New IntPtr(1), x0 + dx, y0)
        投鼠标(输出面.Handle, &H202, IntPtr.Zero, x0 + dx, y0)
        泵(0.4)
        If 水平记录.Count = 0 Then
            检查(False, "S10 真鼠标拖动没走到视图层（分流或门控没通）", "零下发")
            Return
        End If
        ' 宿主每拍提交的是**像素增量**（画面控件_图片平移拖动 累加 e.水平位移，
        ' 提交 值-已提交），内核 PanView 做 pan += delta/travel、呈现做 offset = pan·travel
        ' ⇒ 净位移恒等于 Σdelta。所以这里必须：①累加而不是取最后一拍，②**不再乘**
        ' 内核余量 —— 旧写法把像素又乘了一次像素，量纲就是错的，读数才会恒等于 0。
        Dim 累加平移 As Single = 0.0F
        For Each v In 水平记录
            累加平移 += v
        Next
        Dim 适配盒宽 = 12000.0F * 图片缩放几何.适应比(视口宽(), 视口高(), 12000UI, 9000UI)
        Dim 内核余量 = 适配盒宽 * Math.Abs(z - 1.0F) / 2.0F
        Dim 实际位移 = 累加平移
        Dim 比值 = 实际位移 / dx
        检查(True, "S10 真鼠标拖动确实走到视图层", $"{下发次数} 次下发, Σ平移={累加平移:F3}px 末拍={水平记录(水平记录.Count - 1):F6}")
        记($"z={z:F4} 适配盒宽={适配盒宽:F1} 控件宽={视口宽()} 内核余量={内核余量:F1} Σ下发={实际位移:F1}px 期望={dx} 比值={比值:F4}")
        检查(Math.Abs(比值 - 1.0F) < 0.02F, "S10 拖 120px 画面就走 120px（跟手）", $"比值={比值:F4}")
    End Sub

    ' ── S11：小图档（源 200×200 < 控件）走真消息 ─────────────────────────────
    ' D1 的两个后果都落在这一档：内核把适配盒恒封顶在源原生尺寸 ⇒ f = min(1, 681/200) = 1，
    ' 于是上界 = 64/1 = 64（按未封顶的 3.405 建模就成了 64/3.405 = 18.79），
    ' 而平移分母 = fittedWidth = 200（未封顶就成 681，灵敏度大 3.4 倍 ⇒ 拖 120px 画面只走 35px）。
    ' 打真 WM_MOUSEWHEEL 进真输出子窗，量的是宿主在链末端的终值，不是几何层的自述。
    Private Sub RunS11_small_image()
        Dim 输出面 = 取输出面()
        If 输出面 Is Nothing Then
            检查(False, "S11 拿不到输出子窗", $"{全局画面.Controls.Count} 个")
            Return
        End If
        场景开始(200UI, 200UI)
        Dim r = 输出面.RectangleToScreen(输出面.ClientRectangle)
        Dim cx = r.Left + r.Width \ 2
        Dim cy = r.Top + r.Height \ 2
        记($"S11 视口 {视口宽()}x{视口高()} 源 200x200 ⇒ 内核 fittedWidth 应为原生 200")

        ' 一格真滚轮：目标 = 1.0 × 1.25 = 1.25（上界 64 远够不着，钳位不咬）。
        投滚轮(输出面.Handle, 120, cx, cy)
        泵(0.6)
        检查(Math.Abs(末值() - 1.25F) < 0.002, "S11 小图一格滚轮落到 1.25", $"末={末值():F4}（1.0×1.25）")

        ' 再连滚 5×5 = 25 格：1.25^26 = 330.9 远超上界 ⇒ 终点只能是上界本身。
        ' 上界手算 = 每像素最多 64 屏幕像素 ÷ f(=1) = 64.0，与内核上钳同值 ⇒ 期望写死 64.0F。
        ' 修前那条界算成 64/3.405 = 18.79，滚到 18.79 就"滚轮没反应"，这就是本档的牙。
        For 批 As Integer = 1 To 5
            For i As Integer = 1 To 5
                投滚轮(输出面.Handle, 120, cx, cy)
            Next
            泵(0.45)
        Next
        泵(0.8)
        检查(Math.Abs(末值() - 64.0F) < 0.02, "S11 连滚 26 格顶到 64（不是修前的 18.79）",
             $"末={末值():F2} 期望=64.0=64/1.0；未封顶建模是 64/3.405=18.79")
        检查(峰值() <= 64.0F + 0.0005F, "S11 全程没越过内核 64", $"峰={峰值():F4}")
        检查(单调不减(), "S11 轨迹单调（不回抽）", $"{轨迹.Count} 点")

        ' 拖动灵敏度：内核 offsetX = pan × |fittedWidth×z − fittedWidth| / 2（VideoRenderer.cpp:5933），
        ' 小图的 fittedWidth = 原生 200（VideoRenderer.cpp:967-970 的封顶，手算不查被测模块）。
        ' 宿主拿 681 当分母时比值 = 200/681 = 0.2937 —— z 在分子分母里对消，所以不依赖终值精度。
        Dim 顶格z = 末值()
        下发次数 = 0
        轨迹.Clear()
        水平记录.Clear()
        垂直记录.Clear()
        Dim x0 = 300, y0 = 300, dx = 120
        投鼠标(输出面.Handle, &H201, New IntPtr(1), x0, y0)
        投鼠标(输出面.Handle, &H200, New IntPtr(1), x0 + dx, y0)
        投鼠标(输出面.Handle, &H202, IntPtr.Zero, x0 + dx, y0)
        泵(0.4)
        If 水平记录.Count = 0 Then
            检查(False, "S11 小图顶格后真鼠标拖动没走到视图层", "零下发")
            Return
        End If
        ' 顶格后 z 很大 ⇒ 200px 的图显示成 200·z px，远超视口 ⇒ 确有平移余量，
        ' 宿主会提交。判据同 S10：累加每拍的**像素**增量，不再乘内核余量（乘了就变 px²）。
        Dim 累加平移11 As Single = 0.0F
        For Each v In 水平记录
            累加平移11 += v
        Next
        Dim 内核fitted宽 = 200.0F
        Dim 内核余量 = 内核fitted宽 * Math.Abs(顶格z - 1.0F) / 2.0F
        Dim 比值 = 累加平移11 / dx
        检查(True, "S11 小图顶格后真鼠标拖动走到视图层", $"{下发次数} 次下发, Σ平移={累加平移11:F3}px 末拍={水平记录(水平记录.Count - 1):F6}")
        记($"z={顶格z:F4} 内核 fitted宽={内核fitted宽:F0} 控件宽={视口宽()} 内核余量={内核余量:F1} Σ下发={累加平移11:F1}px 期望={dx} 比值={比值:F4}")
        检查(Math.Abs(比值 - 1.0F) < 0.02F, "S11 小图顶格后拖 120px 画面就走 120px（提交量即像素，与 fitted 无关）",
             $"比值={比值:F4}（旧写法取末拍再乘余量，读数恒为 0）")
    End Sub

    ' ── S12：小图双击 —— 适应就是 1:1，得"说清楚"而不是闷着不动（D4）────────
    Private Sub RunS12_doubleclick_small_image()
        场景开始(200UI, 200UI)
        ' 先钉判据本身：整图两条边都在视口里 ⇒ 适应档与 1:1 档重合，双击本来没有可切的。
        检查(图片缩放几何.适应即一比一(全局画面.Width, 全局画面.Height, 200UI, 200UI),
             "S12 几何判据：200×200 在真控件尺寸下 适应 == 1:1",
             $"{全局画面.Width}x{全局画面.Height} vs 200x200")
        Dim 输出面 = 取输出面()
        If 输出面 Is Nothing Then
            检查(False, "S12 拿不到输出子窗", $"{全局画面.Controls.Count} 个")
            Return
        End If
        Dim 中点x = Math.Max(1, 输出面.Width \ 2)
        Dim 中点y = Math.Max(1, 输出面.Height \ 2)
        ' 真双击序列 DOWN/UP/DBLCLK/UP（0x0201 / 0x0202 / 0x0203 / 0x0202，wParam 带 MK_LBUTTON=1）。
        ' 产品的门控写在 视频输出窗口_MouseDown 的 e.Clicks>=2 上，而 WinForms 把 WM_LBUTTONDBLCLK
        ' 报成 MouseDown(Clicks=2) —— 所以必须投 DBLCLK 那一条，连投两次 DOWN 不算双击。
        投鼠标(输出面.Handle, &H201, New IntPtr(1), 中点x, 中点y)
        泵(0.05)
        投鼠标(输出面.Handle, &H202, IntPtr.Zero, 中点x, 中点y)
        泵(0.05)
        投鼠标(输出面.Handle, &H203, New IntPtr(1), 中点x, 中点y)
        泵(0.05)
        投鼠标(输出面.Handle, &H202, IntPtr.Zero, 中点x, 中点y)
        泵(0.9)
        ' 归因用：先确认真双击确实走到了处理函数 —— 这一档它是靠"说一句话"证明的，
        ' 不是靠下发。没有这一条，下面的绿可能是"消息根本没投进去"的假绿。
        检查(提示记录.Count > 0 OrElse 轨迹.Count > 0, "S12 真双击走到了处理函数（有提示或有下发）",
             $"下发 {轨迹.Count} 次、提示 {提示记录.Count} 句")
        ' 手算：适应档 = 1.0（整图入窗），1:1 档 = 1/f = 1/1.0 = 1.0 ⇒ 两档重合 ⇒ 双击后仍是 1.0。
        ' 判据写成"没有一帧落到 1.0 之外"，而不是"必须有下发"：处理器在这一档直接 Return，
        ' 一次都不发才是对的，发了别的值才是 bug。
        检查(轨迹.Count = 0 OrElse (峰值() <= 1.0005F AndAlso 末值() >= 0.9995F),
             "S12 小图双击后缩放没动（适应 == 1:1，档位仍是 1.0）",
             $"下发 {轨迹.Count} 次，末={末值():F4} 峰={峰值():F4}（-1 就是一次都没发）")
        ' 必须说清楚"为什么没动"：静默 Return 会被读成"1:1 坏了"（docs/81 P-3）。产品的原话是
        ' 「这张图比窗口小：适应窗口就是 1:1」，见 播放器图片浏览控制器 画面控件_图片双击切换。
        ' 这里只按大小关系那半句「比窗口小」判，措辞再改也不至于把牙磨掉。
        Dim 说了小图 As Boolean = False
        For Each m In 提示记录
            If m.Contains("比窗口小") Then 说了小图 = True
        Next
        检查(提示记录.Count > 0, "S12 双击至少给一句反馈（不是闷着不动）", $"末句「{最近提示}」")
        检查(说了小图, "S12 双击那句反馈讲清了「图片比窗口小，适应就是 1:1」（D4）",
             $"共 {提示记录.Count} 句，末句「{最近提示}」")
    End Sub

    ' 输出子窗（画面控件的第一个子控件）。拿不到就判失败，不静默跳过。
    Private Function 取输出面() As Control
        If 全局画面 Is Nothing OrElse 全局画面.Controls.Count = 0 Then Return Nothing
        Return 全局画面.Controls(0)
    End Function

    Private Sub 投鼠标(目标 As IntPtr, msg As UInteger, wp As IntPtr, x As Integer, y As Integer)
        Dim lp As Integer = (x And &HFFFF) Or ((y And &HFFFF) << 16)
        SendMessageW(目标, msg, wp, New IntPtr(lp))
    End Sub

    Private Function 视口宽() As Integer
        Return 全局画面.Width
    End Function

    Private Function 视口高() As Integer
        Return 全局画面.Height
    End Function
End Module
