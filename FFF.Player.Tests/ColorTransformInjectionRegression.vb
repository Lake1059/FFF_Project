' 3FP 外部色彩变换 kind=3 注入回归 —— 同进程直连内核
'
' 测的是宿主侧 2026-10-17 落地的整条通路：
'   帧边数据环（FFF3FP_CopyFrameSideDataAfter）→ 外部模块（FFF.HDRdp.Test.dll 的
'   analyzeFrame）→ kind=3 注入（FFF3FP_SetDynamicMetadata）→ 快照落“外部色彩变换”路径。
' 2026-10-18 追加：容器配置记录通道（FFF3FP_CopyStreamSideData → analyzeFrame 的
'   配置记录 字段 → FFF_ColorTransformContentText）——场景四新增“内容描述”读数：
'   它是本次唯一“跨解锁态翻转”的新读数（内容是模块产出的分类文本，未解锁必为空串）。
' 走 FFF.Player 托管包装（播放器会话，Public + InternalsVisibleTo），不 mock，
' P/Invoke 到 x64/Release 的 FFF.Native.dll 真内核；模块 DLL 是真件（库外独立仓产物）。
'
' 两个场景互为对照：
'   场景一 模块在场且已解锁（FC_COLORTRANSFORM_DIR 指向模块目录 + 票据在测试 exe 旁）
'          ⇒ 必须注入：快照 HDR处理路径落 外部色彩变换，注入帧数 > 0，媒体信息回填可用位。
'   场景二 模块缺席（FC_COLORTRANSFORM_DIR 指向空目录）
'          ⇒ 必须静默：通路不启动、快照不落外部路径、播放不报错。
'
' 前置条件（缺一则 SKIP，exit 2，不判红）：
'   · 素材：由环境变量 FFF_COLORTRANSFORM_SAMPLE 指定；未指定时用本机的一份既有
'     测试样本（本机路径，非仓库资产）。宿主从不解释素材里的元数据内容，因此本
'     回归对素材没有特殊要求——注入与否的判据是快照的处理路径与泵计数器。
'   · 模块：FC_COLORTRANSFORM_MODULE 指向 FFF.HDRdp.Test.dll 所在目录；
'   · 票据：测试 exe 同目录的 FFF.HDRdp.Test.ticket（由 New-AuthTicket.ps1 -Kind valid 预铸，
'     绑定本机用户；DPAPI 保护，换机/换用户需重铸）。
'
' 用法：FFF.Player.Tests --color-transform-injection
' 退出码：0 全绿 / 1 有失败条 / 2 前置条件不具备

Imports System
Imports System.IO
Imports System.Threading
Imports FFF.Player

Friend Module ColorTransformInjectionRegression
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

    ''' <summary>轮询快照直到条件成立或超时，返回最后一次读数。</summary>
    Private Function 等待快照(会话 As 播放器会话, 条件 As Func(Of 播放器快照, Boolean), Optional 超时秒 As Double = 30.0) As 播放器快照
        Dim 截止 = DateTimeOffset.UtcNow.AddSeconds(超时秒)
        Dim 快照 = 会话.当前快照
        While DateTimeOffset.UtcNow < 截止
            快照 = 会话.当前快照
            If 条件(快照) Then Return 快照
            Thread.Sleep(50)
        End While
        Return 快照
    End Function

    Private Function 取素材() As String
        ' 素材一律由环境变量提供，不在源码里写死任何具体文件名或本机路径
        ' （本机资产不属于本仓，写进源码既不可移植，也会把无关字样带进产品树）。
        Dim 指定 = Environment.GetEnvironmentVariable("FFF_COLORTRANSFORM_SAMPLE")
        If Not String.IsNullOrWhiteSpace(指定) AndAlso File.Exists(指定) Then Return 指定
        Return Nothing
    End Function

    Private Function 取模块目录() As String
        Dim 指定 = Environment.GetEnvironmentVariable("FC_COLORTRANSFORM_MODULE")
        If Not String.IsNullOrWhiteSpace(指定) Then Return 指定
        Return Nothing
    End Function

    Friend Function 运行(参数 As String()) As Integer
        Console.WriteLine("3FP 外部色彩变换 kind=3 注入回归")

        Dim 素材 = 取素材()
        Dim 模块目录 = 取模块目录()
        Dim 模块路径 = If(模块目录 IsNot Nothing, Path.Combine(模块目录, "FFF.HDRdp.Test.dll"), Nothing)
        Dim 票据 = Path.Combine(AppContext.BaseDirectory, "FFF.HDRdp.Test.ticket")

        If 素材 Is Nothing OrElse 模块目录 Is Nothing OrElse Not File.Exists(模块路径) Then
            Console.WriteLine("SKIP：素材或模块不具备。")
            Console.WriteLine($"  素材={素材}")
            Console.WriteLine($"  模块目录={模块目录}（应含 FFF.HDRdp.Test.dll；用 FC_COLORTRANSFORM_MODULE 指定）")
            Console.WriteLine($"通过 {通过} 条，失败 {失败} 条")
            Return 2
        End If
        If Not File.Exists(票据) Then
            Console.WriteLine("SKIP：测试 exe 旁没有已解锁票据（模块会一直判未锁定态，无人值守不会弹框）。")
            Console.WriteLine($"  期望票据：{票据}")
            Console.WriteLine("  预铸：powershell -File C:\PLAN\ColorTransform\New-AuthTicket.ps1 -Kind valid -OutFile <该路径>")
            Console.WriteLine($"通过 {通过} 条，失败 {失败} 条")
            Return 2
        End If

        Console.WriteLine($"素材：{素材}")
        Console.WriteLine($"模块：{模块路径}（{New FileInfo(模块路径).Length} B）")
        Console.WriteLine($"票据：{票据}（{New FileInfo(票据).Length} B）")

        ' ── 场景零：ABI 与能力门禁 ──
        ' 这两条是“通路能不能起来”的前置闸：尺寸漂移会让每一帧被整批拒绝，
        ' 内核缺导出会让通路静默降级——两者都不该被读成绿灯。
        Console.WriteLine("场景零：ABI 尺寸与内核能力门禁")
        检查(播放器原生接口.色彩变换载荷尺寸 = 431296,
            "kind=3 载荷结构尺寸 = 431296（与内核 static_assert 同水）",
            $"实测 {播放器原生接口.色彩变换载荷尺寸}")
        检查(播放器原生接口.支持帧边数据环, "内核提供帧边数据环导出", $"支持={播放器原生接口.支持帧边数据环}")
        检查(播放器原生接口.支持流边数据, "内核提供流边数据导出", $"支持={播放器原生接口.支持流边数据}")
        检查(播放器原生接口.支持动态元数据注入, "内核提供动态元数据注入导出",
            $"支持={播放器原生接口.支持动态元数据注入}")

        场景注入(素材, 模块目录)
        场景缺席(素材)
        场景锁态开局中途解锁(素材, 模块目录, 票据)
        场景解锁前后对照(素材, 模块目录, 票据)

        Console.WriteLine($"通过 {通过} 条，失败 {失败} 条")
        If 失败 > 0 Then Return 1
        Return 0
    End Function

    ' ── 场景四：解锁前后行为对照（同一素材跑两遍，只差“票据在不在”）──
    ' 目的不是再证一次注入，而是把“解锁究竟改变了什么”做成可复现的读数对照：
    ' 哪些读数会翻、哪些读数不动（后者同样重要——外部色彩变换可用=真并不代表已解锁）。
    Private Sub 场景解锁前后对照(素材 As String, 模块目录 As String, 票据 As String)
        Console.WriteLine("场景四：解锁前后行为对照（同一素材，只差票据在不在）")
        Dim 暂存 = 票据 & ".held"
        Environment.SetEnvironmentVariable("FC_COLORTRANSFORM_DIR", 模块目录)
        Try
            If File.Exists(暂存) Then File.Delete(暂存)
            If File.Exists(票据) Then File.Move(票据, 暂存)

            Dim 锁态 As 对照读数 = Nothing
            Dim 解锁态 As 对照读数 = Nothing
            Try
                锁态 = 采集(素材)
                File.Move(暂存, 票据)
                解锁态 = 采集(素材)
            Catch ex As Exception
                检查(False, "场景四无异常", ex.GetType().Name & ": " & ex.Message)
                Return
            Finally
                Try
                    If File.Exists(暂存) AndAlso Not File.Exists(票据) Then File.Move(暂存, 票据)
                Catch
                End Try
            End Try

            Console.WriteLine("  " & "读数".PadRight(22) & "未解锁".PadRight(26) & "已解锁")
            For Each 名称 In 锁态.顺序
                Console.WriteLine("  " & 名称.PadRight(22) &
                    If(锁态.取值(名称), "").PadRight(26) & 解锁态.取值(名称))
            Next

            ' 会翻的读数：
            检查(锁态.处理路径 <> HDR处理路径.外部色彩变换 AndAlso
                 解锁态.处理路径 = HDR处理路径.外部色彩变换,
                "HDR处理路径：未解锁≠外部色彩变换 ⇒ 已解锁=外部色彩变换",
                $"{锁态.处理路径} → {解锁态.处理路径}")
            检查(锁态.已采用 = False AndAlso 解锁态.已采用 = True,
                "模块采用：未解锁未采用 ⇒ 已解锁已采用", $"{锁态.已采用} → {解锁态.已采用}")
            检查(锁态.注入帧数 = 0UI AndAlso 解锁态.注入帧数 > 0UI,
                "注入帧数：0 ⇒ >0", $"{锁态.注入帧数} → {解锁态.注入帧数}")
            ' 不会翻的读数（必须说清楚，否则会被读成解锁标志）：
            检查(锁态.可用位 AndAlso 解锁态.可用位,
                "外部色彩变换可用：两种状态都为真（它只表示模块在盘上，不代表已解锁）",
                $"{锁态.可用位} / {解锁态.可用位}")
            检查(锁态.播放推进 AndAlso 解锁态.播放推进,
                "播放推进：两种状态都正常（未解锁不是故障，是静默降级）",
                $"{锁态.帧序号} / {解锁态.帧序号}")
            ' 内容描述：这是本次唯一“跨解锁态翻转”的新读数——文本是模块算出来的分类
            ' （配置记录通道 + 分析帧 的产物），未解锁时模块压根没被采用，所以必为空串。
            检查(锁态.内容描述 = "", "内容描述：未解锁态必为空串", $"实测 '{锁态.内容描述}'")
            检查(解锁态.内容描述.Length > 0 AndAlso 解锁态.内容描述.StartsWith("P", StringComparison.Ordinal),
                "内容描述：已解锁态非空且以 P 开头（模块产出的分类文本）",
                $"实测 '{解锁态.内容描述}'")
            ' 已知夹具（dv-regular 样本）的强断言：只有“配置记录真的喂进了 分析帧”
            ' 才会是 P8.1 —— 若只跑 RPU 启发式，那只可能给出 P8（缺 .1）。
            If 素材.IndexOf("dv-regular", StringComparison.OrdinalIgnoreCase) >= 0 Then
                检查(解锁态.内容描述 = "P8.1",
                    "内容描述：已知夹具 ⇒ P8.1（配置记录通道端到端判据）",
                    $"实测 '{解锁态.内容描述}'（若为 P8 或空，说明配置记录通道没通）")
            Else
                Console.WriteLine("  （素材名不含已知夹具标记，跳过 P8.1 强断言）")
            End If

            ' 合规观测：模块不影响任何终端用户可见的信息。
            ' 内核把这条路径的用户可见文案定成中性措辞（Caller-supplied colour transform），
            ' 而且媒体信息 JSON 记录的是打开时确定的静态路径 —— 两者都不随解锁而变。
            ' 若将来有人让它跟随注入变化、或写成非中性措辞，这条会先红。
            检查(String.Equals(锁态.可见文案, 解锁态.可见文案, StringComparison.Ordinal),
                "合规：用户可见文案解锁前后一致（模块不泄漏到用户可见层）",
                $"{锁态.可见文案} / {解锁态.可见文案}")
        Finally
            Environment.SetEnvironmentVariable("FC_COLORTRANSFORM_DIR", Nothing)
        End Try
    End Sub

    ''' <summary>一次会话的可观测读数快照（解锁前后对照用）。</summary>
    Private Class 对照读数
        Public 处理路径 As HDR处理路径
        Public 已采用 As Boolean
        Public 注入帧数 As UInteger
        Public 可用位 As Boolean
        Public 帧序号 As Long
        ''' <summary>终端用户实际会看到的那句 HDR 路径文案（合规性观测点）。</summary>
        Public 可见文案 As String = String.Empty
        ''' <summary>模块产出的内容分类文本（配置记录通道的端到端读数；未解锁必为空串）。</summary>
        Public 内容描述 As String = String.Empty
        Public ReadOnly Property 播放推进 As Boolean
            Get
                Return 帧序号 > 3
            End Get
        End Property
        Public ReadOnly Property 顺序 As String()
            Get
                Return {"HDR处理路径", "模块已采用", "注入帧数", "外部色彩变换可用", "帧序号", "用户可见文案", "内容描述"}
            End Get
        End Property
        Public Function 取值(名称 As String) As String
            Select Case 名称
                Case "HDR处理路径" : Return 处理路径.ToString()
                Case "模块已采用" : Return 已采用.ToString()
                Case "注入帧数" : Return 注入帧数.ToString()
                Case "外部色彩变换可用" : Return 可用位.ToString()
                Case "帧序号" : Return 帧序号.ToString()
                Case "用户可见文案" : Return 可见文案
                Case "内容描述" : Return 内容描述
                Case Else : Return "?"
            End Select
        End Function
    End Class

    ''' <summary>开一次会话、播同一素材、取对照读数。</summary>
    Private Function 采集(素材 As String) As 对照读数
        Dim 结果 As New 对照读数()
        Using 会话 = New 播放器会话(New 播放器配置 With {.解码器 = 解码模式.CPU})
            会话.打开Async(素材).GetAwaiter().GetResult()
            会话.播放()
            Dim 快照 = 等待快照(会话, Function(s) s.帧序号 > 3, 30.0)
            结果.帧序号 = 快照.帧序号
            结果.处理路径 = 快照.HDR处理路径
            Dim 诊断 = 会话.色彩变换诊断
            结果.已采用 = 诊断.Contains("模块已采用")
            Dim 信息 = 会话.当前媒体信息
            Dim 视频流 = 信息?.流.FirstOrDefault(Function(流) 流.类型 = "video")
            结果.可用位 = 视频流 IsNot Nothing AndAlso 视频流.外部色彩变换可用
            结果.可见文案 = 视频流?.HDR处理说明
            ' 内容描述：模块产出的分类文本。只有模块已采用才可能非空——未解锁时它恒为空串，
            ' 所以非空证明的是“配置记录真的喂进了 分析帧”，不是“模块在盘上”。
            结果.内容描述 = 会话.色彩变换内容描述
            If 结果.已采用 AndAlso String.IsNullOrEmpty(结果.内容描述) Then
                ' 分类文本要等泵下一拍取完配置记录再出（每拍 2–12ms，给足窗口）。
                Dim 截止 = DateTimeOffset.UtcNow.AddSeconds(10)
                While DateTimeOffset.UtcNow < 截止 AndAlso String.IsNullOrEmpty(结果.内容描述)
                    结果.内容描述 = 会话.色彩变换内容描述
                    Thread.Sleep(50)
                End While
            End If
            ' 注入帧数：从诊断串里取（形如“注入 N 帧”）
            Dim 位置 = 诊断.IndexOf("注入 ")
            If 位置 >= 0 Then
                Dim 尾部 = 诊断.Substring(位置 + 3)
                Dim 空格 = 尾部.IndexOf(" "c)
                If 空格 > 0 Then UInteger.TryParse(尾部.Substring(0, 空格), 结果.注入帧数)
            End If
        End Using
        Return 结果
    End Function

    ' ── 场景三：模块在但锁着开局 ⇒ 泵空转补装载；中途解锁 ⇒ 自动装载并注入 ──
    ' 这是“解锁之后通路能不能自己起来”的唯一正证据：用户可能在启动弹框里磨蹭，
    ' 也可能切出去找码——票据落盘的时刻不可预测，泵必须自己补装载（2s 一次、上限 30 次）。
    Private Sub 场景锁态开局中途解锁(素材 As String, 模块目录 As String, 票据 As String)
        Console.WriteLine("场景三：锁着开局 ⇒ 泵空转补装载；中途解锁 ⇒ 自动装载并注入")
        Dim 暂存 = 票据 & ".held"
        Environment.SetEnvironmentVariable("FC_COLORTRANSFORM_DIR", 模块目录)
        Try
            ' 开局把票据挪走 ⇒ 模块判“已锁定”（无人值守不会弹框）。
            If File.Exists(暂存) Then File.Delete(暂存)
            File.Move(票据, 暂存)
            Try
                Using 会话 = New 播放器会话(New 播放器配置 With {.解码器 = 解码模式.CPU})
                    会话.打开Async(素材).GetAwaiter().GetResult()
                    会话.播放()
                    等待快照(会话, Function(s) s.帧序号 > 3, 30.0)

                    ' 锁着时：通路建起来了（空转），但模块不能采用 ⇒ 不能注入。
                    Dim 锁态诊断 = 会话.色彩变换诊断
                    检查(锁态诊断.Contains("模块未采用"), "锁态开局：模块未采用（无人值守不弹框）", 锁态诊断)
                    检查(会话.当前快照.HDR处理路径 <> HDR处理路径.外部色彩变换,
                        "锁态开局：快照不落外部色彩变换路径", $"路径={会话.当前快照.HDR处理路径}")

                    ' 中途解锁：把票据放回（等价于用户在弹框里输对了码）。
                    File.Move(暂存, 票据)
                    Dim 解锁后 = 等待快照(会话,
                        Function(快照) 快照.HDR处理路径 = HDR处理路径.外部色彩变换, 60.0)
                    检查(解锁后.HDR处理路径 = HDR处理路径.外部色彩变换,
                        "中途解锁：泵自动补装载并把快照切到外部色彩变换",
                        $"路径={解锁后.HDR处理路径}")
                    Dim 诊断后 = 会话.色彩变换诊断
                    检查(诊断后.Contains("模块已采用") AndAlso Not 诊断后.Contains("注入 0 帧"),
                        "中途解锁：泵计数器显示已注入", 诊断后)
                End Using
            Catch ex As Exception
                检查(False, "场景三无异常", ex.GetType().Name & ": " & ex.Message)
            Finally
                ' 恢复票据（无论成败）：后面的运行还要用它。
                Try
                    If File.Exists(暂存) AndAlso Not File.Exists(票据) Then File.Move(暂存, 票据)
                Catch
                End Try
            End Try
        Finally
            Environment.SetEnvironmentVariable("FC_COLORTRANSFORM_DIR", Nothing)
        End Try
    End Sub

    ' ── 场景一：模块在场且已解锁 ⇒ 必须注入 ──
    Private Sub 场景注入(素材 As String, 模块目录 As String)
        Console.WriteLine("场景一：模块在场且已解锁 ⇒ kind=3 注入")
        Environment.SetEnvironmentVariable("FC_COLORTRANSFORM_DIR", 模块目录)
        Try
            Using 会话 = New 播放器会话(New 播放器配置 With {.解码器 = 解码模式.CPU})
                会话.打开Async(素材).GetAwaiter().GetResult()
                会话.播放()

                ' 先等几帧播出，让环里有东西可查。
                等待快照(会话, Function(s) s.帧序号 > 5, 30.0)

                ' 直查环：绕开泵，验证内核确实在往环里放 type-24 条目。
                ' 这一步把“0 注入”分成两种读法：环空（内核侧）vs 泵/模块侧。
                Dim 需要 As UInteger = 0UI
                Dim 找到 As Long = 0L
                Dim 环结果 = 播放器原生接口.复制帧边数据(会话.取得句柄(),
                    播放器原生接口.边数据契约类型, 0, Nothing, 需要, 找到)
                检查(环结果 = 原生播放器结果.成功 AndAlso 需要 > 0UI,
                    "环里有 type-24 条目（直查，绕开泵）",
                    $"结果={环结果} 需要={需要} 找到={找到}")

                ' 注入生效的内核侧证据：快照的 HDR 处理路径翻到“外部色彩变换”。
                Dim 注入后 = 等待快照(会话,
                    Function(快照) 快照.HDR处理路径 = HDR处理路径.外部色彩变换, 30.0)
                检查(注入后.HDR处理路径 = HDR处理路径.外部色彩变换,
                    "快照落外部色彩变换路径（内核收下了 kind=3 表）",
                    $"HDR处理路径={注入后.HDR处理路径}")

                ' 宿主侧证据：泵的计数器（注入帧数 > 0）。
                Dim 诊断 = 会话.色彩变换诊断
                检查(诊断 IsNot Nothing AndAlso 诊断.Contains("注入") AndAlso Not 诊断.Contains("注入 0 帧"),
                    "泵计数器显示已注入", 诊断)

                ' 回填位：媒体信息的外部色彩变换可用（模块在盘上）。
                Dim 信息 = 会话.当前媒体信息
                Dim 视频流 = 信息?.流.FirstOrDefault(Function(流) 流.类型 = "video")
                检查(视频流 IsNot Nothing AndAlso 视频流.外部色彩变换可用,
                    "媒体信息回填 外部色彩变换可用=真", $"可用={视频流?.外部色彩变换可用}")

                ' 合规门禁：终端用户实际看到的那句文案必须是中性的。
                '   内部标识符（本仓枚举名“外部色彩变换”）与用户可见文案是两回事——
                '   后者由内核 HdrProcessor::ProcessingPathName 给出，这条路径答
                '   "Caller-supplied colour transform"。把这条做成回归断言，将来谁改
                '   了内核字符串引入专有字样，这里会先红。
                Dim 可见文案 = 视频流?.HDR处理说明
                ' COMPLIANCE-SCAN-WHITELIST-BEGIN
                ' 下面这串是“禁止出现在用户可见文案里”的黑名单，不是本机使用清单：
                ' 命中即判定本仓存在不合格的用户可见措辞。未来的机械门禁请跳过此区间。
                Dim 敏感词 = {"dolby", "dovi", "杜比", "视界", "rpu"}
                ' COMPLIANCE-SCAN-WHITELIST-END
                Dim 命中 = 敏感词.Where(Function(词) 可见文案 IsNot Nothing AndAlso
                                            可见文案.IndexOf(词, StringComparison.OrdinalIgnoreCase) >= 0).ToArray()
                检查(Not String.IsNullOrEmpty(可见文案) AndAlso 命中.Length = 0,
                    "合规：用户可见的 HDR 处理路径文案为中性措辞",
                    If(String.IsNullOrEmpty(可见文案), "（空）",
                       可见文案 & If(命中.Length > 0, " ← 命中敏感词: " & String.Join(",", 命中), "")))

                ' 复位自愈：seek 之后必须还能再注入（游标复位 + 模块 reset + 强制重发）。
                会话.跳转(TimeSpan.FromSeconds(1))
                Dim 复位后 = 等待快照(会话,
                    Function(快照) 快照.帧序号 >= 0 AndAlso 快照.HDR处理路径 = HDR处理路径.外部色彩变换, 30.0)
                检查(复位后.HDR处理路径 = HDR处理路径.外部色彩变换,
                    "seek 复位后通路仍在注入", $"帧序号={复位后.帧序号}, 路径={复位后.HDR处理路径}")
                Console.WriteLine($"  诊断（seek 后）：{会话.色彩变换诊断}")
            End Using
        Catch ex As Exception
            检查(False, "场景一无异常", ex.GetType().Name & ": " & ex.Message)
        Finally
            Environment.SetEnvironmentVariable("FC_COLORTRANSFORM_DIR", Nothing)
        End Try
    End Sub

    ' ── 场景二：模块缺席 ⇒ 必须静默 ──
    Private Sub 场景缺席(素材 As String)
        Console.WriteLine("场景二：模块缺席 ⇒ 静默降级")
        ' 空目录放测试 exe 旁（AppContext.BaseDirectory 必然可写；计划任务上下文里的
        ' %TEMP% 可能指向系统目录，建子目录会 Access denied）。
        Dim 空目录 = Path.Combine(AppContext.BaseDirectory, "ct-absent-probe")
        Try
            Directory.Delete(空目录, True)
        Catch
        End Try
        Directory.CreateDirectory(空目录)
        Environment.SetEnvironmentVariable("FC_COLORTRANSFORM_DIR", 空目录)
        Try
            Using 会话 = New 播放器会话(New 播放器配置 With {.解码器 = 解码模式.CPU})
                会话.打开Async(素材).GetAwaiter().GetResult()
                会话.播放()

                ' 播一会儿：没有模块也必须正常播放，不能报错、不能落外部路径。
                Dim 快照 = 等待快照(会话, Function(s) s.帧序号 > 5, 30.0)
                检查(快照.帧序号 > 5, "无模块时播放正常推进", $"帧序号={快照.帧序号}")
                检查(快照.HDR处理路径 <> HDR处理路径.外部色彩变换,
                    "无模块时快照不落外部色彩变换路径", $"HDR处理路径={快照.HDR处理路径}")
                检查(会话.色彩变换诊断.Contains("通路未启动"),
                    "无模块时注入通路不启动", 会话.色彩变换诊断)

                Dim 信息 = 会话.当前媒体信息
                Dim 视频流 = 信息?.流.FirstOrDefault(Function(流) 流.类型 = "video")
                检查(视频流 IsNot Nothing AndAlso Not 视频流.外部色彩变换可用,
                    "无模块时 外部色彩变换可用=假", $"可用={视频流?.外部色彩变换可用}")
            End Using
        Catch ex As Exception
            检查(False, "场景二无异常", ex.GetType().Name & ": " & ex.Message)
        Finally
            Environment.SetEnvironmentVariable("FC_COLORTRANSFORM_DIR", Nothing)
            Try
                Directory.Delete(空目录, True)
            Catch
            End Try
        End Try
    End Sub
End Module
