' 弹幕高压力探针 —— 同进程，量"宿主每拍生成成本能不能进泵预算"
'
' 背景（播放器弹幕图层呈现器.vb）：播放中每拍必提交一次。泵间隔是
' 弹幕目标节拍由显示器探针决定，最高 120Hz；单拍成本超过当前间隔就会被 PrecisionTimer 的 Drop
' 策略丢拍（已丢弃节拍数 > 0）。探针按 8ms 的最高档预算测量：过得了 8ms，较低刷新率
' 的保底预算自动满足。本探针量的是宿主侧最大那块：弹幕调度器.生成帧 ——
' lane 分配 + 碰撞 + 真实 DirectWrite 文本测量（默认弹幕文本测量器，Shared 缓存在场）。
' 明确不量：命令对象复用与转换、P/Invoke 过界、内核合成（GPU 半边在内核线程上，
' 本探针够不着）—— 这三块各有自己的计数/判据，别拿本探针的读数替它们背书。
'
' 臂：
'   A 常规高压：60 s 素材、50 条/s arrival = 3000 条，真实测量器。冷（前 150 帧）只报数，
'     热（后 1800 帧）进判据。
'   B 极限库：120 s、~167 条/s = 20000 条，独立文本池保证 Shared 测量缓存对 B 是冷的。
'   C 系统负载：B 的热稳态重跑、外挂 4 个忙旋线程压 CPU。
'
' 判据（带牙）：每档热稳态 p95 单帧生成 < 8 ms（最紧泵预算）。冷臂不判 —— Shared
' 缓存让"冷"只在首轮成立，拿它当判据等于判缓存策略而不是判调度器。
' 本机并行任务注意：C 臂的读数受同机其它进程影响，若当时有其它计时任务在跑，复测后再定。
'
' 用法：FFF.Player.Tests --danmaku-stress [输出目录]
' 退出码：0 判据绿 / 1 有判据红 / 2 夹具或环境不具备（"没测"与"测了但红"分开）

Imports System.Diagnostics
Imports System.Globalization
Imports System.IO
Imports System.Linq
Imports System.Text
Imports System.Threading
Imports FFF.Player

Friend Module DanmakuStressProbe
    Private Const 区域宽度 As Single = 1920.0F
    Private Const 区域高度 As Single = 1080.0F
    Private Const 目标帧率 As Double = 60.0R
    ' 压力预算按最高 120Hz 的 8ms 估算；较低刷新率显示器拥有更宽裕的预算。
    Private Const 泵预算毫秒 As Double = 8.0R
    Private Const 保底预算毫秒 As Double = 16.0R

    Private 通过 As Integer
    Private 失败 As Integer
    Private ReadOnly 日志 As New List(Of String)()

    Private Sub 输出(文本 As String)
        Console.WriteLine(文本)
        日志.Add(文本)
    End Sub

    Private Function N3(值 As Double) As String
        Return 值.ToString("0.000", CultureInfo.InvariantCulture)
    End Function

    Private Function N1(值 As Double) As String
        Return 值.ToString("0.0", CultureInfo.InvariantCulture)
    End Function

    ''' <summary>升序分位（线性插值关掉，取上取整索引——保守侧）。</summary>
    Private Function 分位(样本 As List(Of Double), p As Double) As Double
        If 样本 Is Nothing OrElse 样本.Count = 0 Then Return Double.NaN
        Dim 排序 = 样本.ToArray()
        Array.Sort(排序)
        Dim 索引 = CInt(Math.Ceiling(p * 排序.Length)) - 1
        If 索引 < 0 Then 索引 = 0
        If 索引 >= 排序.Length Then 索引 = 排序.Length - 1
        Return 排序(索引)
    End Function

    Private Function 新配置() As 弹幕显示配置
        Dim 值 As New 弹幕显示配置()
        值.字号 = 36.0F
        值.使用源字号 = False
        值.使用源颜色 = False
        值.滚动速度 = 180.0F
        值.目标帧率 = 60.0F
        值.同屏最大数量 = 4000
        值.常规滚动最大行数 = 5
        值.顶部最大行数 = 5
        值.基准视频高度 = 1080.0F
        值.启用类型 = 弹幕类型.常规滚动
        值.验证()
        Return 值
    End Function

    Private Function 区域() As 视频显示区域
        Return New 视频显示区域(0.0F, 0.0F, 区域宽度, 区域高度, 1.0F, 96.0F)
    End Function

    Private Function 时刻(帧号 As Integer) As TimeSpan
        Return TimeSpan.FromSeconds(CDbl(帧号) / 目标帧率)
    End Function

    ''' <summary>文本池：每条都带唯一后缀，保证测量缓存键不重复（冷臂才有测量成本）。</summary>
    Private Function 造文本池(前缀 As String, 数量 As Integer) As List(Of String)
        Dim 池 As New List(Of String)(数量)
        Dim 随机 As New Random(20261021)
        For n = 1 To 数量
            Dim 填充 = 随机.[Next](0, 26)
            池.Add(前缀 & n & "·" & New String("字"c, 4 + 填充))
        Next
        Return 池
    End Function

    ''' <summary>生成 B 站格式 XML 夹具并落盘；时间均匀铺满 [0, 时长秒)。
    ''' 混合类型时按 1/4/5（滚动/底部/顶部）轮转，让全类型臂真的有三种条目。</summary>
    Private Function 写夹具(路径 As String, 条数 As Integer, 时长秒 As Double, 文本池 As List(Of String),
                             Optional 混合类型 As Boolean = False) As Boolean
        Try
            Dim 输出目录 = Path.GetDirectoryName(路径)
            If Not String.IsNullOrEmpty(输出目录) Then Directory.CreateDirectory(输出目录)
            Dim sb As New StringBuilder(条数 * 96)
            sb.Append("<?xml version=""1.0"" encoding=""UTF-8""?><i><chatserver>chat.bilibili.com</chatserver>" &
                      "<chatid>0</chatid><maxlimit>100000</maxlimit>")
            Dim 随机 As New Random(20261022)
            Dim 模式列表() As Integer = {1, 4, 5}
            For n = 1 To 条数
                Dim t = CDbl(n - 1) * 时长秒 / CDbl(条数)
                Dim 文本 = 文本池(随机.[Next](文本池.Count))
                Dim 模式 = If(混合类型, 模式列表((n - 1) Mod 模式列表.Length), 1)
                sb.Append("<d p=""").Append(t.ToString("0.000", CultureInfo.InvariantCulture)).
                    Append(",").Append(模式).Append(",25,16777215,").Append(CLng(t)).Append(",0,100000").Append(n).
                    Append(",").Append(n).Append(""">").Append(文本).Append("</d>")
            Next
            sb.Append("</i>")
            File.WriteAllText(路径, sb.ToString(), New UTF8Encoding(False))
            Return True
        Catch
            Return False
        End Try
    End Function

    ''' <summary>推进并逐帧计时。返回每帧毫秒样本；同时把最后一帧的条目数写进 活条数。</summary>
    Private Function 跑帧(调度器 As 弹幕调度器, 起始帧 As Integer, 帧数 As Integer,
                          ByRef 峰值活条数 As Integer) As List(Of Double)
        Dim 样本 As New List(Of Double)(帧数)
        Dim 帧条目 As New List(Of 弹幕绘制项)(512)
        For k = 0 To 帧数 - 1
            帧条目.Clear()
            Dim 开始 = Stopwatch.GetTimestamp()
            调度器.生成帧(时刻(起始帧 + k), 区域(), 帧条目)
            Dim 毫秒 = CDbl(Stopwatch.GetTimestamp() - 开始) * 1000.0R / CDbl(Stopwatch.Frequency)
            样本.Add(毫秒)
            If 帧条目.Count > 峰值活条数 Then 峰值活条数 = 帧条目.Count
        Next
        Return 样本
    End Function

    Private Sub 报样本(标签 As String, 样本 As List(Of Double), 峰值活条数 As Integer, 进判据 As Boolean)
        Dim p50 = 分位(样本, 0.5R)
        Dim p95 = 分位(样本, 0.95R)
        Dim 最大 = 样本.Max()
        Dim 均值 = 样本.Average()
        输出("  " & 标签 & " | n=" & 样本.Count & " 均值=" & N3(均值) & "ms p50=" & N3(p50) &
             "ms p95=" & N3(p95) & "ms max=" & N3(最大) & "ms 峰值活条数=" & 峰值活条数 &
             " 预算=" & N1(泵预算毫秒) & "ms（60Hz 保底=" & N1(保底预算毫秒) & "ms） p95占=" &
             N1(p95 / 泵预算毫秒 * 100.0R) & "%")
        If 进判据 Then
            检查(p95 < 泵预算毫秒, 标签 & "·p95 < 泵预算", "p95=" & N3(p95) & "ms 预算=" & N1(泵预算毫秒) & "ms")
        End If
    End Sub

    Private Sub 检查(条件 As Boolean, 名称 As String, 实测 As String)
        If 条件 Then
            通过 += 1
            输出("  ok    " & 名称 & " | " & 实测)
        Else
            失败 += 1
            输出("  FAIL  " & 名称 & " | " & 实测)
        End If
    End Sub

    Friend Function 运行(参数 As String()) As Integer
        Dim 输出目录 As String = Nothing
        If 参数 IsNot Nothing AndAlso 参数.Length > 0 AndAlso Not String.IsNullOrWhiteSpace(参数(0)) Then
            输出目录 = 参数(0)
        End If
        If String.IsNullOrWhiteSpace(输出目录) Then
            输出目录 = Path.Combine(Path.GetTempPath(), "3fp-danmaku-stress")
        End If

        输出("=== 弹幕高压力探针（同进程，宿主每拍成本 vs 8ms 最紧泵预算） ===")
        输出("输出目录：" & 输出目录)

        Dim 夹具A = Path.Combine(输出目录, "stress_a_3000.xml")
        Dim 夹具B = Path.Combine(输出目录, "stress_b_20000.xml")
        Dim 池A = 造文本池("高压甲", 1200)
        Dim 池B = 造文本池("极限乙", 4000)
        If Not 写夹具(夹具A, 3000, 60.0R, 池A) OrElse Not 写夹具(夹具B, 20000, 120.0R, 池B) Then
            输出("EXIT 2 夹具写盘失败（" & 输出目录 & "）—— 未测。")
            写日志(输出目录)
            Return 2
        End If

        Dim 库A As 弹幕资料库 = Nothing
        Dim 库B As 弹幕资料库 = Nothing
        Try
            库A = B站弹幕解析器.解析文件(夹具A)
            库B = B站弹幕解析器.解析文件(夹具B)
        Catch ex As Exception
            输出("EXIT 2 夹具解析失败：" & ex.GetType().Name & " " & ex.Message & " —— 未测。")
            写日志(输出目录)
            Return 2
        End Try
        If 库A Is Nothing OrElse 库A.数量 = 0 OrElse 库B Is Nothing OrElse 库B.数量 = 0 Then
            输出("EXIT 2 夹具解析出 0 条弹幕 —— 未测。")
            写日志(输出目录)
            Return 2
        End If
        检查(库A.数量 = 3000, "夹具A·条目数 = 3000", "解析得=" & 库A.数量)
        检查(库B.数量 = 20000, "夹具B·条目数 = 20000", "解析得=" & 库B.数量)

        Dim 过滤 As New 弹幕过滤配置()
        过滤.启用类型 = 弹幕类型.常规滚动

        ' ---- A 常规高压：冷 150 帧（报数）→ 热 1800 帧（判据）----
        输出("A 常规高压：60s/3000 条，真实测量器，5 行，同屏上限 4000")
        Dim 调度A As New 弹幕调度器(库A, 新配置(), 过滤.创建快照(), New 默认弹幕文本测量器())
        Dim 峰A冷 As Integer = 0
        Dim 冷A = 跑帧(调度A, 0, 150, 峰A冷)
        报样本("A·冷·前150帧", 冷A, 峰A冷, False)
        Dim 峰A热 As Integer = 0
        Dim 热A = 跑帧(调度A, 150, 1800, 峰A热)
        报样本("A·热·1800帧", 热A, 峰A热, True)

        ' ---- B 极限库：冷 150 帧（报数）→ 热 1800 帧（判据）----
        输出("B 极限库：120s/20000 条（~167 条/s），独立文本池（对 Shared 缓存冷）")
        Dim 调度B As New 弹幕调度器(库B, 新配置(), 过滤.创建快照(), New 默认弹幕文本测量器())
        Dim 峰B冷 As Integer = 0
        Dim 冷B = 跑帧(调度B, 0, 150, 峰B冷)
        报样本("B·冷·前150帧", 冷B, 峰B冷, False)
        Dim 峰B热 As Integer = 0
        Dim 热B = 跑帧(调度B, 150, 1800, 峰B热)
        报样本("B·热·1800帧", 热B, 峰B热, True)

        ' ---- C 系统负载：B 热稳态重跑 + 4 忙旋线程 ----
        输出("C 系统负载：B 热稳态 + 4 忙旋线程压 CPU（读数受同机其它任务影响）")
        Dim 停标志 As Integer = 0
        Dim 旋线程 As New List(Of Thread)(4)
        For n = 1 To 4
            Dim t As New Thread(
                Sub()
                    Dim x = 0.0R
                    While Volatile.Read(停标志) = 0
                        x += 1.0R
                        If x > 1.0E+15R Then x = 0.0R
                    End While
                End Sub)
            t.IsBackground = True
            t.Start()
            旋线程.Add(t)
        Next
        Thread.Sleep(300)
        Dim 峰C As Integer = 0
        Dim 负载C = 跑帧(调度B, 1950, 600, 峰C)
        Volatile.Write(停标志, 1)
        For Each t In 旋线程
            t.Join(2000)
        Next
        报样本("C·负载·600帧", 负载C, 峰C, True)

        ' ---- D 全类型高活条数：常规+顶部+固定全开、行数放宽，把"活条数"顶上去 ----
        ' C 臂暴露了盲区：5 条 lane 的碰撞把活条数压在 ~20，生成成本没被活条数维度压过。
        Dim 配置D = 新配置()
        配置D.启用类型 = 弹幕类型.全部
        配置D.常规滚动最大行数 = 20
        配置D.顶部最大行数 = 20
        配置D.验证()
        Dim 夹具D = Path.Combine(输出目录, "stress_d_12000.xml")
        If Not 写夹具(夹具D, 12000, 60.0R, 池A, 混合类型:=True) Then
            输出("EXIT 2 夹具D写盘失败 —— 未测。")
            写日志(输出目录)
            Return 2
        End If
        Dim 库D As 弹幕资料库
        Try
            库D = B站弹幕解析器.解析文件(夹具D)
        Catch ex As Exception
            输出("EXIT 2 夹具D解析失败：" & ex.GetType().Name & " —— 未测。")
            写日志(输出目录)
            Return 2
        End Try
        检查(库D.数量 = 12000, "夹具D·条目数 = 12000", "解析得=" & 库D.数量)
        输出("D 全类型：60s/12000 条（~200 条/s），滚动/底部/顶部各 1/3，行数放宽")
        ' 调度器要求 配置.启用类型 与 过滤器 同时允许（弹幕调度器.vb 的 允许），
        ' 所以 D 必须用一份全类型过滤器；沿用 A/B/C 的"只放滚动"快照会让顶部/底部
        ' 一条都进不来，D 的绿色 p95 就只是滚动臂的重复读数。
        Dim 过滤D As New 弹幕过滤配置()
        过滤D.启用类型 = 弹幕类型.全部
        Dim 调度D As New 弹幕调度器(库D, 配置D, 过滤D.创建快照(), New 默认弹幕文本测量器())
        Dim 峰D热身 As Integer = 0
        跑帧(调度D, 0, 150, 峰D热身)
        Dim 峰D热 As Integer = 0
        Dim 热D = 跑帧(调度D, 150, 1800, 峰D热)
        报样本("D·全类型·热1800帧", 热D, 峰D热, True)
        ' 只报 p95 还不够：要证明这一帧里三种类型真的都在，而不是夹具/过滤器
        ' 又把顶部、底部挡在外面（那会让 D 退化成 A/B/C 的重复读数）。
        Dim 类型样本 As New List(Of 弹幕绘制项)(512)
        调度D.生成帧(时刻(1950), 区域(), 类型样本)
        Dim 类型集 As New HashSet(Of 弹幕类型)(类型样本.Select(Function(x) x.项目.类型))
        检查(类型集.Contains(弹幕类型.常规滚动) AndAlso 类型集.Contains(弹幕类型.底部) AndAlso
                类型集.Contains(弹幕类型.顶部),
             "D 帧内滚动/底部/顶部都有", String.Join("/", 类型集))

        输出("")
        输出($"合计：ok {通过} / FAIL {失败}（共 {通过 + 失败} 条断言）")
        Dim 退出码 As Integer = If(失败 = 0, 0, 1)
        If 通过 + 失败 = 0 Then
            输出("本档 0 条断言被执行 —— 不是通过。")
            退出码 = 2
        End If
        输出("EXIT " & 退出码)
        写日志(输出目录)
        Return 退出码
    End Function

    Private Sub 写日志(输出目录 As String)
        Try
            Directory.CreateDirectory(输出目录)
            File.WriteAllLines(Path.Combine(输出目录, "danmaku_stress.log"), 日志)
        Catch
        End Try
    End Sub
End Module
