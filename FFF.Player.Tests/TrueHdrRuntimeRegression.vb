' 自动 HDR（RTX TrueHDR）运行时行为 —— 同进程回归
'
' 补的是 docs/交接文档-2026-10-19-自动HDR判词与VSR像素闭环.md §10.6.1 那笔账：
' "宿主 VB 的**运行时**行为（菜单勾选、状态读数文本）只有结构回归（17 条反射）；交互实跑未做。"
' 现有 TrueHdrPageRegression 那 17 条量的是"构造参数个数 / 方法存在 / 控件在不在 / Text 前缀"，
' **没有一条真调过 设置为 / 刷新状态，也没有一条下过请求**。所以本档只补行为，不重复结构。
'
' 为什么这一档不需要内核、不需要窗口：播放器视频自动HDR控制器 的五个入口全是委托注入
' （应用请求 / 读取状态文本 / 支持探测 / 操作提示 / 保存偏好，见该类 :34-39），
' 所以"宿主在什么条件下发什么请求、说什么话"可以整条用替身量出来 —— 而这恰好就是
' 那笔账里"运行时行为"缺的那一半。真会话那一半（勾一下内核状态会不会变）不在本档，
' 它需要真内核 + 真 HDR 输出，属 §2.4 剩余项。
'
' 用法：FFF.Player.Tests --truehdr-runtime-regression
' 退出码：0 全绿 / 1 有失败条

Imports System.Collections.Generic
Imports FFF.Player

Friend Module TrueHdrRuntimeRegression
    Private 通过 As Integer
    Private 失败 As Integer

    Private Sub 检查(条件 As Boolean, 名称 As String, 实测 As String)
        If 条件 Then
            通过 += 1
            Console.WriteLine($"  ok   {名称}  {实测}")
        Else
            失败 += 1
            Console.WriteLine($"  FAIL {名称}  {实测}")
        End If
    End Sub

    ' ── 替身：把每一次跨边界调用记成一条字符串，顺序即证据 ──────────────────
    Private Class 替身
        Friend 调用记录 As New List(Of String)
        Friend 支持值 As Boolean = True
        Friend 状态文本值 As String = "替身状态"
        Friend 状态抛异常 As Boolean = False
        Friend 支持抛异常 As Boolean = False

        Friend Function 支持探测() As Boolean
            调用记录.Add("探测")
            If 支持抛异常 Then Throw New InvalidOperationException("探测炸了")
            Return 支持值
        End Function

        Friend Sub 应用请求(开启 As Boolean)
            调用记录.Add("请求=" & If(开启, "True", "False"))
        End Sub

        Friend Function 读取状态文本() As String
            调用记录.Add("读状态")
            If 状态抛异常 Then Throw New ObjectDisposedException("会话已释放")
            Return 状态文本值
        End Function

        Friend Sub 操作提示(文本 As String)
            调用记录.Add("提示「" & 文本 & "」")
        End Sub

        Friend Sub 保存偏好(开启 As Boolean)
            调用记录.Add("存偏好=" & If(开启, "True", "False"))
        End Sub

        Friend Function 请求序列() As String
            Return String.Join(",", 调用记录.FindAll(Function(s) s.StartsWith("请求=")))
        End Function

        Friend Function 提示序列() As String
            Return String.Join(" / ", 调用记录.FindAll(Function(s) s.StartsWith("提示")))
        End Function

        ' 跨界的调用 = 真的会改内核/偏好/缓存读数的那些；探测与提示是本地簿记，不算。
        Friend Function 跨界调用数() As Integer
            Return 调用记录.FindAll(Function(s) s.StartsWith("请求=") OrElse
                                     s.StartsWith("存偏好=") OrElse s = "读状态").Count
        End Function
    End Class

    Private Function 新建控制器(替身 As 替身, 初值 As Boolean) As 播放器视频自动HDR控制器
        Return New 播放器视频自动HDR控制器(AddressOf 替身.应用请求, AddressOf 替身.读取状态文本,
                                       AddressOf 替身.支持探测, AddressOf 替身.操作提示,
                                       初值, AddressOf 替身.保存偏好)
    End Function

    Friend Function 运行(参数 As String()) As Integer
        Console.WriteLine("自动 HDR 运行时行为回归（委托全替身，不起内核、不建窗口）")
        Try
            RunA_状态文本()
            RunB_不支持路径()
            RunC_开关与幂等()
            RunD_重新探测与媒体打开()
            RunE_异常与释放()
            Console.WriteLine($"通过 {通过} 条，失败 {失败} 条")
            Return If(失败 > 0, 1, 0)
        Catch ex As Exception
            Console.WriteLine("替身接线不起来，本轮不产出结论：" & ex.GetType().Name & " " & ex.Message)
            Return 1
        End Try
    End Function

    ' ── A：内核原因码 → 人话。逐值对应 FFF3FPVideoAutoHdrReason，期望写手算字面量 ──
    Private Function 状态(生效 As UInteger, 原因 As UInteger, Optional 闩 As UInteger = 0UI,
                          Optional 源宽 As UInteger = 1920UI, Optional 源高 As UInteger = 1080UI,
                          Optional 输出宽 As UInteger = 3840UI, Optional 输出高 As UInteger = 2160UI) As 原生视频自动HDR状态
        Dim s As New 原生视频自动HDR状态()
        s.大小 = CUInt(System.Runtime.InteropServices.Marshal.SizeOf(GetType(原生视频自动HDR状态)))
        s.版本 = 1UI
        s.生效 = 生效 : s.原因 = 原因 : s.驱动拒绝闩 = 闩
        s.源宽 = 源宽 : s.源高 = 源高 : s.输出宽 = 输出宽 : s.输出高 = 输出高
        Return s
    End Function

    Private Sub 断言文本(状态值 As 原生视频自动HDR状态, 期望 As String, 档名 As String)
        Dim 实 = 播放器会话.视频自动HDR状态文本(状态值)
        检查(实 = 期望, "A·" & 档名, $"「{实}」")
    End Sub

    Private Sub RunA_状态文本()
        Dim 生效文本 = 播放器会话.视频自动HDR状态文本(状态(1UI, 0UI))
        检查(生效文本.Contains("自动 HDR 已启用") AndAlso
            生效文本.Contains("1920×1080") AndAlso 生效文本.Contains("3840×2160"),
            "A·生效=1 报「已启用」并带两副尺寸", $"「{生效文本}」")
        ' 该类文件头 :13-17 与函数注释都立了同一句规矩：TrueHDR 不是画质保证，
        ' 文案只说"已启用"不说"已增强"。这条判据就是给那句话装的牙。
        检查(Not 生效文本.Contains("已增强") AndAlso Not 生效文本.Contains("画质"),
             "A·生效文案不越界承诺画质（注释立的规矩）", $"「{生效文本}」")
        断言文本(状态(0UI, 1UI), "未请求", "原因1=未请求")
        断言文本(状态(0UI, 2UI), "渲染适配器不是 NVIDIA", "原因2=非 N 卡")
        断言文本(状态(0UI, 3UI), "驱动不支持该私有流扩展（探测未读到 1）", "原因3=扩展探测失败")
        断言文本(状态(0UI, 4UI), "当前源不是 SDR（HDR 源不予转换）", "原因4=HDR 源")
        断言文本(状态(0UI, 5UI), "当前输出路径不是 HDR（需 10 位 HDR 输出契约）", "原因5=输出路径")
        断言文本(状态(0UI, 6UI), "源格式不支持送入视频处理器", "原因6=源格式")
        断言文本(状态(0UI, 7UI), "驱动拒绝或 Blt 失败，已闩住；关掉再打开可重新探测", "原因7=闩住")
        断言文本(状态(0UI, 0UI, 1UI), "驱动拒绝过", "原因0+闩=1")
        断言文本(状态(0UI, 0UI, 0UI), "内核未报告原因", "原因0+闩=0（不猜一个原因出来）")
    End Sub

    ' ── B：内核不提供这个入口时，UI 必须说清楚，不能"勾了没反应" ──────────────
    Private Sub RunB_不支持路径()
        Dim 替 = New 替身()
        替.支持值 = False
        Dim 控 = 新建控制器(替, False)
        检查(Not 控.可用, "B·可用=False（探测回 False）", 替.调用记录.Count.ToString())
        控.设置为(True)
        检查(Not 替.调用记录.Exists(Function(s) s.StartsWith("请求=")),
             "B·不支持时一次请求都不下发（不制造「勾了但什么都没发生」）", 替.请求序列())
        检查(替.提示序列().Contains("当前内核不支持自动 HDR"), "B·提示说出原因", 替.提示序列())
        检查(Not 控.已启用, "B·开关值不被假装翻上", 控.已启用.ToString())
        检查(控.状态读数.Contains("当前内核不支持") AndAlso 控.状态读数.Contains("FFF.Native.dll"),
             "B·读数行写明「需更新 FFF.Native.dll」", 控.状态读数)
        控.Dispose()
    End Sub

    ' ── C：幂等与"关掉必须真发"。注释立的两条规矩都在这里量 ──────────────────
    Private Sub RunC_开关与幂等()
        Dim 替 = New 替身()
        Dim 控 = 新建控制器(替, False)
        检查(控.状态读数 = "未读取（会话未就绪）",
             "C·没读过状态时读数说「未读取」而不是伪造一个「未生效」", 控.状态读数)
        控.设置为(True)
        检查(替.请求序列() = "请求=True", "C·开一次只发一条请求", 替.请求序列())
        检查(控.已启用 AndAlso 替.调用记录.Contains("存偏好=True"),
             "C·状态与偏好同时更新", 替.调用记录.Count.ToString())
        检查(替.提示序列().Contains("已请求自动 HDR") AndAlso
            替.提示序列().Contains("强度归 NVIDIA App"),
             "C·开的提示带「强度归 NVIDIA App」这句限定", 替.提示序列())
        Dim 跨界数 = 替.跨界调用数()
        控.设置为(True)
        ' 只数真的动了内核/偏好/状态的调用。设置为 一进来必过一次 支持探测，
        ' 拿记录总数判幂等会把那条探测算进去 —— 第一版就红在这（判据量错操作数，
        ' 不是产品不幂等：请求序列 从头到尾没变）。
        检查(替.跨界调用数() = 跨界数, "C·重复开=幂等，不再下发（注释立的规矩）",
             $"跨界调用 前 {跨界数} → 后 {替.跨界调用数()}（请求序列={替.请求序列()}）")
        控.设置为(False)
        检查(替.调用记录.Exists(Function(s) s = "请求=False"),
             "C·关掉必须真发 False（内核「驱动拒绝闩」的唯一复位路径）", 替.请求序列())
        控.切换()
        检查(替.请求序列().EndsWith("请求=True"), "C·切换走的是同一条设置为", 替.请求序列())
        控.Dispose()
    End Sub

    ' ── D：重新探测的"先关后开"顺序，与媒体打开后的偏好重放 ──────────────────
    Private Sub RunD_重新探测与媒体打开()
        Dim 替 = New 替身()
        Dim 控 = 新建控制器(替, True)
        替.调用记录.Clear()
        控.重新探测()
        Dim 请求 = 替.调用记录.FindAll(Function(s) s.StartsWith("请求="))
        检查(请求.Count >= 2 AndAlso 请求(0) = "请求=False" AndAlso 请求(1) = "请求=True",
             "D·重新探测严格先 False 后 True（顺序错了闩就白清）", String.Join(",", 请求))
        检查(替.调用记录.Contains("存偏好=True") AndAlso 控.已启用,
             "D·重新探测把偏好钉成开", 控.已启用.ToString())

        Dim 替2 = New 替身()
        Dim 控2 = 新建控制器(替2, True)
        替2.调用记录.Clear()
        控2.媒体已打开(Nothing, Nothing)
        检查(替2.调用记录.Contains("请求=True"),
             "D·媒体打开后把偏好重放到新会话（内核每会话从 Off 起步）", 替2.请求序列())
        检查(替2.调用记录.Contains("读状态"), "D·重放之后回报一次状态", 替2.调用记录.Count.ToString())

        Dim 替3 = New 替身()
        Dim 控3 = 新建控制器(替3, False)
        替3.调用记录.Clear()
        控3.媒体已打开(Nothing, Nothing)
        检查(Not 替3.调用记录.Exists(Function(s) s.StartsWith("请求=")),
             "D·关着就不重放（不把用户的关闭翻成开）", 替3.请求序列())
        检查(替3.调用记录.Contains("读状态"), "D·关着也照回报状态", 替3.调用记录.Count.ToString())
        控.Dispose() : 控2.Dispose() : 控3.Dispose()
    End Sub

    ' ── E：委托炸了不能把 UI 炸了；释放之后一律静默 ─────────────────────────
    Private Sub RunE_异常与释放()
        Dim 替 = New 替身()
        替.状态抛异常 = True
        Dim 控 = 新建控制器(替, False)
        Dim 读数 = 控.刷新状态()
        检查(读数.Contains("未读取"), "E·读状态抛异常时回落「未读取」而不是抛给 UI", 读数)
        控.Dispose()
        Dim 记录数 = 替.调用记录.Count
        控.设置为(True)
        控.重新探测()
        控.切换()
        控.媒体已打开(Nothing, Nothing)
        检查(替.调用记录.Count = 记录数, "E·Dispose 之后四个动作全静默",
             $"新增 {替.调用记录.Count - 记录数} 条")
        Dim 释放后读数 = 控.刷新状态()
        ' 实测串必须与判据同向：If(a, "非空") 是 null 合并，a 为 Nothing 时它返回"非空"，
                ' 于是上一版打出的是"ok + 非空"这种自相矛盾的读数。
        检查(释放后读数 Is Nothing, "E·Dispose 后 刷新状态 返回 Nothing（不读已释放的会话）",
             If(释放后读数 Is Nothing, "Nothing", "非空「" & 释放后读数 & "」"))

        Dim 替2 = New 替身()
        替2.支持抛异常 = True
        Dim 控2 = 新建控制器(替2, False)
        检查(Not 控2.可用, "E·探测抛异常按「不支持」处理（不冒泡）", 控2.可用.ToString())
        控2.设置为(True)
        检查(Not 替2.调用记录.Exists(Function(s) s.StartsWith("请求=")),
             "E·探测抛异常时不下发请求", 替2.请求序列())
        控2.Dispose()
    End Sub
End Module
