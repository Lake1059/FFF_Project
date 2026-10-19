' 弹幕重锚（弹幕调度器.重新锚定到）—— 同进程回归
'
' 这一档补的是 docs/交接文档-2026-10-19-弹幕呈现重构落地与验证.md §4.1 点名的未验项：
' "重锚路径（Task 16）只经过编译与推理验证：停顿/回跳触发需要注入媒体时钟跳变，CLI 冒烟无法构造"。
'
' 为什么这一档能构造出来：端到端确实构造不了 —— 宿主任何跳转都进内核 DoSeek，
' timelineGeneration 自增，播放器弹幕图层呈现器.vb:253 就 重建调度器 那一路先调
' 当前弹幕调度器.重置()，再 生成帧()。重置() 清空活动列表并把 上一时间秒 打回 NaN，
' 于是 生成帧 里的不连续判定（弹幕调度器.vb:296-315）必然命中、必然走 重新锚定到，
' 但走的时候表是**空的** —— 线上跑的其实是"空表重锚"，"在飞条目原地续飞"那半段
' 从来没被执行过。这正是要抓的洞。
'
' 而 弹幕调度器 与 B站弹幕解析器 都是 Public（本工程与 FFF.Player 之间没有
' InternalsVisibleTo，Friend 面看不见），所以可以在**调度器这一层**直接按公开入口
' 连续喂帧、然后注入一次媒体时钟跳变：跳变前后都只调 Public 的 生成帧()。
'
' 判据全部独立手算，不拿被测模块的返回值当期望值（那是同义反复，永远红不了）：
'   夹具把 描边宽度 归零、阴影颜色 alpha 归零 ⇒ 更新活动项样式（:436）算出的
'   左外扩 = 右外扩 = 0；缩放系数 = 1 ⇒ 实际滚动速度（:522）= 配置.滚动速度。
'   于是 常规滚动 的 x（:326-327）化简成只含"我自己给的常数 + 条目自己的出现时间"：
'       x期望 = 1920 - (当前秒 - min(出现时间秒, 当前秒)) × 滚动速度
'   min() 那一项对应 刷新活动项（:403-404）的进度夹紧：回跳时 开始秒 晚于跳转点的
'   条目 progress 被夹到 0 ⇒ 开始秒 改写为跳转点 ⇒ 重新贴回右边缘。
'   重锚保留进度 ⇒ 未夹到的条目 开始秒 原样保留（= 出现时间，来自 XML，是输入数据）。
'
' 用法：FFF.Player.Tests --danmaku-reanchor-regression [弹幕XML] [输出目录]
' 退出码：0 全绿 / 1 有失败条 / 2 夹具或环境不具备（"没测"与"测了但红"分开，不许混淆）

Imports System.Collections.Generic
Imports System.Globalization
Imports System.IO
Imports System.Linq
Imports System.Text
Imports FFF.Player

Friend Module DanmakuReanchorRegression
    Private 通过 As Integer
    Private 失败 As Integer
    Private ReadOnly 日志 As New List(Of String)()

    ' 手算常量（夹具几何与配置，全部由本回归指定，不读被测模块的任何输出）。
    Private Const 区域X As Single = 0.0F
    Private Const 区域宽度 As Single = 1920.0F
    Private Const 区域高度 As Single = 1080.0F
    Private Const 缩放系数 As Single = 1.0F
    Private Const 字号 As Single = 36.0F
    Private Const 目标帧率 As Double = 60.0R
    Private Const 常规速度 As Single = 180.0F
    Private Const 快速度 As Single = 1800.0F
    ' 阈值格子档用定宽测量器：宽度取字号本身，于是飞行时长可以完全手算。
    Private Const 定宽 As Single = 36.0F
    ' 位置容差：Single 在 1920 量级的 ULP 约 1.2e-4，这里放 0.05 px（比数值噪声大
    ' 400 倍），而任何语义走偏（锚点错、进度丢、回填了已飞过去的条目）都是几百 px。
    Private Const 容差 As Double = 0.05R

    ''' <summary>定宽测量器：让 取得持续秒数 的分子只剩 1920+36，时长可手算。</summary>
    Private NotInheritable Class 定宽测量器
        Implements I弹幕文本测量器

        Public Function 测量宽度(文本 As String, 字体 As String, 字号像素 As Single) As Single _
            Implements I弹幕文本测量器.测量宽度
            Return 字号像素
        End Function
    End Class

    Private Sub 输出(文本 As String)
        Console.WriteLine(文本)
        日志.Add(文本)
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

    Private Function N3(值 As Double) As String
        Return 值.ToString("0.000", CultureInfo.InvariantCulture)
    End Function

    Private Function N2(值 As Double) As String
        Return 值.ToString("0.00", CultureInfo.InvariantCulture)
    End Function

    ' ---------------------------------------------------------------- 夹具与配置

    Private Function 区域() As 视频显示区域
        Return New 视频显示区域(区域X, 0.0F, 区域宽度, 区域高度, 缩放系数, 96.0F)
    End Function

    ''' <summary>
    ''' 把 x 公式里所有非"进度"的项都钉成已知量：描边 0 + 阴影 alpha 0 ⇒ 左/右外扩 0；
    ''' 缩放系数 1 ⇒ 字号=36、速度=配置速度；只用 常规滚动，单一 x 公式。
    ''' 目标帧率显式写 60（与 设置.创建弹幕显示配置 未赋值→弹幕显示配置.目标帧率 默认 60
    ''' 同值，见 设置.vb:253-269 / 弹幕调度器.vb:143），阈值随之 = max(4, round(0.1×60)) = 6 帧。
    ''' </summary>
    Private Function 新配置(滚动速度 As Single, 最大行数 As Integer) As 弹幕显示配置
        Dim 值 As New 弹幕显示配置()
        值.字号 = 字号
        值.使用源字号 = False
        值.使用源颜色 = False
        值.颜色ARGB = &HFFFFFFFFUI
        值.不透明度 = 255
        值.滚动速度 = 滚动速度
        值.目标帧率 = 60.0F
        值.同屏最大数量 = 4000
        值.常规滚动最大行数 = 最大行数
        值.顶部最大行数 = 最大行数
        值.行间距 = 0.0F
        值.顶部边距 = 0.0F
        值.固定弹幕持续秒数 = 4.0F
        值.基准视频高度 = 1080.0F
        值.描边宽度 = 0.0F
        值.描边颜色ARGB = &H00000000UI
        值.阴影深度 = 0.0F
        值.阴影颜色ARGB = &H00000000UI
        值.行内前后间距 = 0.0F
        值.尺寸缩放方式 = 0
        值.启用类型 = 弹幕类型.常规滚动
        Return 值
    End Function

    Private Function 新调度器(资料库 As 弹幕资料库, 滚动速度 As Single, 最大行数 As Integer,
                             测量器 As I弹幕文本测量器) As 弹幕调度器
        Dim 过滤 As New 弹幕过滤配置()
        过滤.启用类型 = 弹幕类型.常规滚动
        Return New 弹幕调度器(资料库, 新配置(滚动速度, 最大行数), 过滤.创建快照(), 测量器)
    End Function

    Private Function 时刻(帧号 As Integer) As TimeSpan
        Return TimeSpan.FromSeconds(CDbl(帧号) / 目标帧率)
    End Function

    ''' <summary>按 1/60 s 一格连续推进（每格帧号增量 1，远低于阈值 6，走正常路径）。</summary>
    Private Function 推进(调度器 As 弹幕调度器, 起始帧 As Integer, 结束帧 As Integer) As List(Of 弹幕绘制项)
        Dim 最近 As List(Of 弹幕绘制项) = Nothing
        For 帧号 = 起始帧 To 结束帧
            最近 = New List(Of 弹幕绘制项)()
            调度器.生成帧(时刻(帧号), 区域(), 最近)
        Next
        Return 最近
    End Function

    ''' <summary>喂单帧，返回该帧条目。</summary>
    Private Function 喂帧(调度器 As 弹幕调度器, 帧号 As Integer) As List(Of 弹幕绘制项)
        Dim 帧 As New List(Of 弹幕绘制项)()
        调度器.生成帧(时刻(帧号), 区域(), 帧)
        Return 帧
    End Function

    ''' <summary>
    ''' 条目同一性一律用**引用**（资料库里的 弹幕项目 实例跨帧稳定）。
    ''' 不能用 弹幕编号：本夹具 p 字段第 8 项是 "1000000+N"，尝试解析项目 的
    ''' Long.TryParse 认不下 '+' 号就静默留 0 ⇒ 3637 条的 弹幕编号 全是 0，
    ''' 拿它当身份会让"集合相等"退化成恒真（本回归第一版就是这么踩到的）。
    ''' </summary>
    Private Function 实例集(帧 As List(Of 弹幕绘制项)) As HashSet(Of 弹幕项目)
        Dim 集 As New HashSet(Of 弹幕项目)(ReferenceEqualityComparer.Instance)
        For Each 项 In 帧
            集.Add(项.项目)
        Next
        Return 集
    End Function

    ' ---------------------------------------------------------------- 独立手算

    ''' <summary>
    ''' x期望 = 区域X + 区域宽度 + 左外扩(0) - (当前秒 - 有效开始秒) × 速度，
    ''' 有效开始秒 = min(出现时间秒, 当前秒)：未越过跳转点的条目 开始秒 保持 出现时间
    ''' （重锚按进度续飞，进度 =（跳转点-开始秒)/时长 落在 [0,1) ⇒ 开始秒 不变），
    ''' 被夹到 0 进度的条目 开始秒 改写为跳转点 ⇒ 贴回右边缘。
    ''' </summary>
    Private Function 期望X(出现时间秒 As Double, 当前秒 As Double, 滚动速度 As Single) As Double
        Dim 有效开始 As Double = Math.Min(出现时间秒, 当前秒)
        Return CDbl(区域X) + CDbl(区域宽度) - (当前秒 - 有效开始) * CDbl(滚动速度)
    End Function

    ''' <summary>定宽档的飞行时长（复刻 取得持续秒数 的 Single 除法口径，只用已知常数）。</summary>
    Private Function 定宽时长() As Double
        Dim 总宽 As Single = 区域宽度 + 定宽
        Return 总宽 / 快速度
    End Function

    ''' <summary>
    ''' 逐条比对 x 与手算期望；同时确认这一帧只放了 常规滚动（夹具启用的唯一类型）。
    ''' 返回超差条数，并就地落一条断言。
    ''' </summary>
    Private Function 校验位置(档名 As String, 帧 As List(Of 弹幕绘制项), 当前秒 As Double,
                              滚动速度 As Single) As Integer
        Dim 超差 = 0
        Dim 串型 = 0
        Dim 最大误差 As Double = 0.0R
        Dim 最坏 As String = "（空帧）"
        For Each 项 In 帧
            If 项.项目.类型 <> 弹幕类型.常规滚动 Then 串型 += 1
            Dim 出现秒 = 项.项目.出现时间.TotalSeconds
            Dim 期望 = 期望X(出现秒, 当前秒, 滚动速度)
            Dim 误差 = Math.Abs(CDbl(项.X像素) - 期望)
            If 误差 > 最大误差 Then
                最大误差 = 误差
                最坏 = "出现=" & N3(出现秒) & " ticks=" & 项.项目.出现时间.Ticks &
                       " x期望=" & N2(期望) & " x实测=" & N2(项.X像素)
            End If
            If 误差 > 容差 Then 超差 += 1
        Next
        检查(串型 = 0, 档名 & "·只放常规滚动（无串型）",
              "串型条数=" & 串型 & " 总条数=" & 帧.Count)
        检查(超差 = 0, 档名 & "·每条 x 由自身进度推出（独立手算）",
              "超差=" & 超差 & "/" & 帧.Count & " 最大误差=" &
              最大误差.ToString("0.000000", CultureInfo.InvariantCulture) &
              "px 容差=" & N2(容差) & "px 最坏[" & 最坏 & "]")
        Return 超差
    End Function

    ''' <summary>出现时间落在开区间 (起秒, 止秒] 的 常规滚动 条目数（用资料库输入数据算，不读帧）。</summary>
    Private Function 窗口内候选(资料库 As 弹幕资料库, 起秒 As Double, 止秒 As Double) As Integer
        Dim 数 = 0
        For Each 项 In 资料库.项目
            Dim 秒 = 项.出现时间.TotalSeconds
            If 秒 > 止秒 Then Exit For
            If 秒 > 起秒 AndAlso 项.类型 = 弹幕类型.常规滚动 Then 数 += 1
        Next
        Return 数
    End Function

    Private Function 帧内窗口条目(帧 As List(Of 弹幕绘制项), 起秒 As Double, 止秒 As Double) As Integer
        Dim 数 = 0
        For Each 项 In 帧
            Dim 秒 = 项.项目.出现时间.TotalSeconds
            If 秒 > 起秒 AndAlso 秒 <= 止秒 Then 数 += 1
        Next
        Return 数
    End Function

    ''' <summary>
    ''' 给阈值格子挑一个锚定帧，前置条件是这一格**有牙**：
    ''' 50ms 与 100ms 窗口内至少各有一条 常规滚动 候选（否则「未触发重锚」那一臂恒为 0，
    ''' 判据退化成恒真）；而 500ms 与 7 帧 两个落点上**不许**正好有条目（重锚后游标停在
    ''' 首个不早于跳转点的位置，正好落在跳转点上的条目会合法入场，把「一条不回填」假判红）。
    ''' </summary>
    Private Function 搜索锚定帧(资料库 As 弹幕资料库) As Integer
        For 帧号 = 60 To 600
            Dim 起 = 时刻(帧号).TotalSeconds
            Dim 甲 = 时刻(帧号 + 3).TotalSeconds
            Dim 边 = 时刻(帧号 + 6).TotalSeconds
            Dim 乙 = 时刻(帧号 + 30).TotalSeconds
            Dim 超 = 时刻(帧号 + 7).TotalSeconds
            If 窗口内候选(资料库, 起, 甲) >= 1 AndAlso 窗口内候选(资料库, 起, 边) >= 1 AndAlso
               窗口内候选(资料库, 乙 - 0.0000001R, 乙) = 0 AndAlso
               窗口内候选(资料库, 超 - 0.0000001R, 超) = 0 Then
                Return 帧号
            End If
        Next
        Return -1
    End Function

    ' ---------------------------------------------------------------- 场景

    ''' <summary>
    ''' 场景 A：前进 5 s 跳变。
    ''' 臂 A = 直接喂跳变帧（重锚语义）；臂 B = 先 重置() 再喂（生产呈现器语义）。
    ''' 两臂必须分叉，否则判据没咬住。
    ''' </summary>
    Private Sub 场景A_前进五秒(资料库 As 弹幕资料库)
        输出("")
        输出("--- 场景 A：前进 5 秒跳变（t=2.0s → 7.0s，速度 180，5 行）---")
        Const 跳前帧 As Integer = 120       ' 2.000 s
        Const 跳后帧 As Integer = 420       ' 7.000 s
        Dim 跳前秒 = 时刻(跳前帧).TotalSeconds
        Dim 跳后秒 = 时刻(跳后帧).TotalSeconds

        ' ---- 臂 A：重锚 ----
        Dim 调度器 = 新调度器(资料库, 常规速度, 5, Nothing)
        Dim 前帧 = 推进(调度器, 0, 跳前帧)
        Dim 前集 = 实例集(前帧)
        检查(前帧.Count >= 1, "A·前置：跳变前确有在飞条目",
              "条数=" & 前帧.Count & " t=" & N3(跳前秒) & "s 帧序号=" & 调度器.当前帧序号)

        Dim 跳帧 = 喂帧(调度器, 跳后帧)
        Dim 跳集 = 实例集(跳帧)
        ' 定长下界：测量宽度恒 ≥ 字号（三条回退分支都 Math.Max），故 结束秒 ≥ 0 + (1920+36)/180 = 10.867s > 7s。
        Dim 时长下界 As Double = CDbl((区域宽度 + 字号) / 常规速度)
        检查(跳帧.Count >= 1, "A·跳变后第一帧活跃条目非空（重锚不清空的直接判据）",
              "条数=" & 跳帧.Count & "（若走 重置() 语义应出现空档）")
        检查(跳帧.Count = 前帧.Count AndAlso 跳集.SetEquals(前集),
              "A·在飞条目原地续飞（不多不少、无重复）",
              "跳前=" & 前帧.Count & " 跳后=" & 跳帧.Count & " 唯一实例=" & 跳集.Count &
              " 判定用时长下界=" & N3(时长下界) & "s > 跳距 5s")
        校验位置("A·跳变帧", 跳帧, 跳后秒, 常规速度)
        Dim 回填 = 帧内窗口条目(跳帧, 跳前秒, 跳后秒)
        检查(回填 = 0, "A·不回填跳转区间里的条目（游标重定位 :362）",
              "出现时间∈(2.0,7.0) 的条目数=" & 回填 &
              "（该区间资料库候选=" & 窗口内候选(资料库, 跳前秒, 跳后秒) & " 条）")
        检查(调度器.当前帧序号 = 420L AndAlso 跳帧.All(Function(项) 项.帧序号 = 420L),
              "A·帧序号 = round(7.0×60) = 420（手算）",
              "调度器.当前帧序号=" & 调度器.当前帧序号)

        ' ---- 跳变帧之后一格：差分状态已按新时刻重记，不该再触发重锚 ----
        Dim 续帧 = 喂帧(调度器, 跳后帧 + 1)
        Dim 续集 = 实例集(续帧)
        检查(续集.SetEquals(跳集) AndAlso 续帧.Count = 跳帧.Count,
              "A·续帧同批（跳变后不反复重锚/不清屏）",
              "跳后=" & 跳帧.Count & " 续帧=" & 续帧.Count)
        ' 跳变帧的 x 作为"上一时刻位置"，与续帧逐条比位移：正常路径下每条应正好走
        ' 速度/帧率 = 3.00 px。若 上一时间秒 没按新时刻重记，这一格会重复重锚或原地不动。
        Dim 跳帧位置 As New Dictionary(Of 弹幕项目, Single)(ReferenceEqualityComparer.Instance)
        For Each 项 In 跳帧
            跳帧位置(项.项目) = 项.X像素
        Next
        Dim 最大位移误差 As Double = 0.0R
        Dim 匹配数 = 0
        For Each 项 In 续帧
            Dim 旧值 As Single
            If 跳帧位置.TryGetValue(项.项目, 旧值) Then
                匹配数 += 1
                Dim 位移 = CDbl(旧值) - CDbl(项.X像素)
                最大位移误差 = Math.Max(最大位移误差, Math.Abs(位移 - CDbl(常规速度) / 目标帧率))
            End If
        Next
        检查(匹配数 = 跳帧.Count AndAlso 跳帧.Count > 0,
              "A·续帧逐条能与跳变帧对上号（比较有效，不是空循环）",
              "匹配=" & 匹配数 & "/" & 跳帧.Count)
        检查(最大位移误差 <= 容差, "A·续帧每条正好前进 速度/帧率 = 3.00px（时间轴不倒退）",
              "与手算 3px 的最大偏差=" & 最大位移误差.ToString("0.000000", CultureInfo.InvariantCulture) &
              "px 帧序号=" & 调度器.当前帧序号)
        检查(调度器.当前帧序号 = 421L, "A·续帧帧序号 = 421（手算）",
              "调度器.当前帧序号=" & 调度器.当前帧序号)

        ' ---- 臂 B：对照臂 = 生产呈现器那一路（先 重置() 再 生成帧()）----
        Dim 对照 = 新调度器(资料库, 常规速度, 5, Nothing)
        Dim 对照前帧 = 推进(对照, 0, 跳前帧)
        Dim 对照前集 = 实例集(对照前帧)
        检查(对照前帧.Count = 前帧.Count, "B·对照臂与臂 A 同起点（同夹具同配置）",
              "对照在飞=" & 对照前帧.Count & " 臂A在飞=" & 前帧.Count)
        对照.重置()
        Dim 对照跳帧 = 喂帧(对照, 跳后帧)
        Dim 对照集 = 实例集(对照跳帧)
        对照集.IntersectWith(对照前集)
        检查(对照跳帧.Count = 0, "B·对照臂（重置语义）跳变帧为空档",
              "条数=" & 对照跳帧.Count & "（重置后 上一时间秒=NaN ⇒ 仍走重锚分支，但表已被清空）")
        检查(对照集.Count = 0, "B·对照臂在飞条目全部消失（与臂 A 分叉的反面）",
              "与跳前交集=" & 对照集.Count & "/" & 前集.Count)
        检查(前帧.Count > 0 AndAlso 对照跳帧.Count = 0,
              "A/B 两臂结果不同（判据有牙，不是恒真）",
              "重锚臂=" & 跳帧.Count & " 条 vs 重置臂=" & 对照跳帧.Count & " 条")
    End Sub

    ''' <summary>场景 B：后退 5 s 跳变（回跳），含 progress 夹紧分支。</summary>
    Private Sub 场景B_后退五秒(资料库 As 弹幕资料库)
        输出("")
        输出("--- 场景 B：后退 5 秒跳变（t=12.0s → 7.0s，速度 180，5 行）---")
        Const 跳前帧 As Integer = 720       ' 12.000 s
        Const 跳后帧 As Integer = 420       ' 7.000 s
        Dim 跳前秒 = 时刻(跳前帧).TotalSeconds
        Dim 跳后秒 = 时刻(跳后帧).TotalSeconds

        Dim 调度器 = 新调度器(资料库, 常规速度, 5, Nothing)
        Dim 前帧 = 推进(调度器, 0, 跳前帧)
        Dim 前集 = 实例集(前帧)
        Dim 夹紧数 = 0
        For Each 项 In 前帧
            If 项.项目.出现时间.TotalSeconds > 跳后秒 Then 夹紧数 += 1
        Next
        检查(前帧.Count >= 1, "B·前置：回跳前确有在飞条目",
              "条数=" & 前帧.Count & " t=" & N3(跳前秒) & "s")
        输出("      其中 出现时间 晚于跳转点（走 progress 夹 0 分支）的条目=" & 夹紧数)

        Dim 跳帧 = 喂帧(调度器, 跳后帧)
        Dim 跳集 = 实例集(跳帧)
        检查(跳帧.Count >= 1, "B·回跳后第一帧活跃条目非空（重锚不清空）",
              "条数=" & 跳帧.Count)
        ' 回跳不淘汰：原 开始秒 ≤ 跳转点 的条目 elapsed 变小、必然仍在轨迹内；
        ' 原 开始秒 > 跳转点 的条目被夹到 0 进度、结束秒 = 跳转点+时长 > 跳转点。
        检查(跳帧.Count = 前帧.Count AndAlso 跳集.SetEquals(前集),
              "B·回跳后条目一条不掉（原地重锚）",
              "回跳前=" & 前帧.Count & " 回跳后=" & 跳帧.Count & " 唯一实例=" & 跳集.Count)
        校验位置("B·回跳帧", 跳帧, 跳后秒, 常规速度)
        Dim 未来数 = 0
        For Each 项 In 跳帧
            If 项.项目.出现时间.TotalSeconds > 跳前秒 Then 未来数 += 1
        Next
        检查(未来数 = 0, "B·不回捞跳转点之后的条目",
              "出现时间>12.0s 的条目=" & 未来数)
        检查(调度器.当前帧序号 = 420L, "B·回跳后帧序号跟随媒体时钟 = round(7.0×60) = 420（手算）",
              "调度器.当前帧序号=" & 调度器.当前帧序号 & "（回跳前=" & 720L & "）")
        Dim 续帧 = 喂帧(调度器, 跳后帧 + 1)
        检查(实例集(续帧).SetEquals(跳集) AndAlso 调度器.当前帧序号 = 421L,
              "B·续帧同批且帧序号回到 421（差分状态已重记，不倒着走）",
              "条数=" & 续帧.Count & " 帧序号=" & 调度器.当前帧序号)
    End Sub

    ''' <summary>
    ''' 场景 C：不连续阈值的活格子。目标帧率 60 ⇒ 阈值 = max(4, round(0.1×60)) = 6 帧 = 100ms。
    ''' 甲格跳 50ms（3 帧，不超阈）⇒ 走正常路径 ⇒ 游标不重定位 ⇒ 区间内条目被回填；
    ''' 乙格跳 500ms（30 帧，超阈）⇒ 走 重新锚定到 ⇒ 游标重定位 ⇒ 区间内条目一条不回填。
    ''' 两格的可观测差别只有"游标是否重定位"，所以判据用回填数（重锚与正常路径在
    ''' 配置不变时对当前帧的 x 是等价的，x 本身分辨不出分支）。
    ''' </summary>
    Private Sub 场景C_阈值格子(资料库 As 弹幕资料库)
        输出("")
        输出("--- 场景 C：阈值边界活格子（速度 1800，20 行，定宽测量器）---")
        Dim 阈值 As Long = Math.Max(4L, CLng(Math.Round(0.1R * 目标帧率)))
        输出("      手算阈值 max(4, round(0.1×60)) = " & 阈值 & " 帧 = " &
              N2(CDbl(阈值) / 目标帧率 * 1000.0R) & " ms")
        Dim 起帧 = 搜索锚定帧(资料库)
        If 起帧 < 0 Then
            输出("      未判定：夹具里找不到满足判据前置（区间有候选、落点不压线）的锚定帧，")
            输出("      场景 C 本轮 **没有执行任何断言** —— 这不是通过。")
            Return
        End If
        Dim 甲帧 = 起帧 + 3      ' +50 ms  = 3 帧（阈值内）
        Dim 边界帧 = 起帧 + 6    ' +100ms  = 6 帧（正好等于阈值，仍应判连续）
        Dim 超阈帧 = 起帧 + 7    ' +116.7ms = 7 帧（超阈 1 帧，应判不连续）
        Dim 乙帧 = 起帧 + 30     ' +500ms  = 30 帧（明显超阈）
        Dim 起秒 = 时刻(起帧).TotalSeconds
        Dim 甲秒 = 时刻(甲帧).TotalSeconds
        Dim 边界秒 = 时刻(边界帧).TotalSeconds
        Dim 超阈秒 = 时刻(超阈帧).TotalSeconds
        Dim 乙秒 = 时刻(乙帧).TotalSeconds
        Dim 甲候选 = 窗口内候选(资料库, 起秒, 甲秒)
        Dim 边界候选 = 窗口内候选(资料库, 起秒, 边界秒)
        Dim 乙候选 = 窗口内候选(资料库, 起秒, 乙秒)
        输出("      锚定帧 " & 起帧 & "（t=" & N3(起秒) & "s）；甲 " & 甲帧 & "（" & N3(甲秒) &
              "s，Δ=3 帧）、乙 " & 乙帧 & "（" & N3(乙秒) & "s，Δ=30 帧）")
        输出("      资料库候选：(" & N3(起秒) & "," & N3(甲秒) & "]=" & 甲候选 &
              " 条，(" & N3(起秒) & "," & N3(乙秒) & "]=" & 乙候选 & " 条")

        ' ---- 甲格：50ms（阈值内）----
        Dim 甲 = 新调度器(资料库, 快速度, 20, New 定宽测量器())
        Dim 甲前 = 推进(甲, 0, 起帧)
        检查(甲前.Count >= 1, "C·甲格前置：连续推进后有在飞条目", "条数=" & 甲前.Count)
        Dim 甲帧条 = 喂帧(甲, 甲帧)
        Dim 甲回填 = 帧内窗口条目(甲帧条, 起秒, 甲秒)
        检查(甲候选 >= 1, "C·甲格判据可用（区间内有候选，否则这一臂无牙）",
              "候选=" & 甲候选)
        检查(甲回填 > 0, "C·甲格跳 50ms 未触发重锚（游标没动 ⇒ 区间条目照常入场）",
              "区间内入场=" & 甲回填 & "/" & 甲候选 & " 条 总条数=" & 甲帧条.Count &
              " 帧序号=" & 甲.当前帧序号)
        检查(甲回填 = 甲候选, "C·甲格区间条目全部入场（定宽+20 行 ⇒ 不被行位丢弃）",
              "入场=" & 甲回填 & " 候选=" & 甲候选)
        校验位置("C·甲格", 甲帧条, 甲秒, 快速度)

        ' ---- 乙格：500ms（超阈）----
        Dim 乙 = 新调度器(资料库, 快速度, 20, New 定宽测量器())
        Dim 乙前 = 推进(乙, 0, 起帧)
        Dim 乙帧条 = 喂帧(乙, 乙帧)
        Dim 乙回填 = 帧内窗口条目(乙帧条, 起秒, 乙秒)
        检查(乙回填 = 0, "C·乙格跳 500ms 触发重锚（游标重定位 ⇒ 区间一条不回填）",
              "区间内入场=" & 乙回填 & "/" & 乙候选 & " 条 总条数=" & 乙帧条.Count &
              " 帧序号=" & 乙.当前帧序号)
        检查(乙帧条.Count >= 1, "C·乙格重锚后非空（在飞条目原地续飞）",
              "条数=" & 乙帧条.Count)
        校验位置("C·乙格", 乙帧条, 乙秒, 快速度)

        ' 乙格可以精确预测存活集合：定宽 ⇒ 时长手算 = (1920+36)/1800。
        Dim 时长 = 定宽时长()
        Dim 应存集 As New HashSet(Of 弹幕项目)(ReferenceEqualityComparer.Instance)
        For Each 项 In 乙前
            If 项.项目.出现时间.TotalSeconds + 时长 > 乙秒 Then 应存集.Add(项.项目)
        Next
        Dim 实测集 = 实例集(乙帧条)
        检查(实测集.SetEquals(应存集), "C·乙格存活集合 = 手算（出现时间+时长 越过跳转点）",
              "手算=" & 应存集.Count & " 实测=" & 实测集.Count & " 时长=" & N3(时长) & "s")

        检查(甲回填 > 0 AndAlso 乙回填 = 0, "C·50ms 与 500ms 两格结果分叉（阈值被咬住）",
              "甲(50ms)回填=" & 甲回填 & " vs 乙(500ms)回填=" & 乙回填)

        ' ---- 阈值两侧各贴一格：6 帧（=阈值，连续）vs 7 帧（>阈值，不连续）----
        Dim 边界 = 新调度器(资料库, 快速度, 20, New 定宽测量器())
        推进(边界, 0, 起帧)
        Dim 边界帧条 = 喂帧(边界, 边界帧)
        Dim 边界回填 = 帧内窗口条目(边界帧条, 起秒, 边界秒)
        检查(边界回填 > 0, "C·恰好 6 帧（100ms）仍判为连续（判定用严格大于阈值）",
              "Δ帧=6 区间内入场=" & 边界回填 & "/" & 边界候选 & " 条 帧序号=" & 边界.当前帧序号)
        Dim 超阈 = 新调度器(资料库, 快速度, 20, New 定宽测量器())
        推进(超阈, 0, 起帧)
        Dim 超阈帧条 = 喂帧(超阈, 超阈帧)
        Dim 超阈回填 = 帧内窗口条目(超阈帧条, 起秒, 超阈秒)
        检查(超阈回填 = 0, "C·7 帧（约 116.7ms）判为不连续并走重锚",
              "Δ帧=7 区间内入场=" & 超阈回填 & " 条 总条数=" & 超阈帧条.Count &
              " 帧序号=" & 超阈.当前帧序号)
        校验位置("C·超阈格", 超阈帧条, 超阈秒, 快速度)
    End Sub

    ' ---------------------------------------------------------------- 入口

    Friend Function 运行(参数 As String()) As Integer
        Dim 夹具 As String = Nothing
        Dim 输出目录 As String = Nothing
        If 参数 IsNot Nothing Then
            If 参数.Length > 0 AndAlso Not String.IsNullOrWhiteSpace(参数(0)) Then 夹具 = 参数(0)
            If 参数.Length > 1 AndAlso Not String.IsNullOrWhiteSpace(参数(1)) Then 输出目录 = 参数(1)
        End If
        If String.IsNullOrWhiteSpace(夹具) Then
            夹具 = Environment.GetEnvironmentVariable("FFF_DANMAKU_XML")
        End If
        If String.IsNullOrWhiteSpace(夹具) Then
            夹具 = Path.Combine("C:\PLAN\3FCompare\_3fc_tmp\smoke_danmaku", "long25to60.xml")
        End If
        If String.IsNullOrWhiteSpace(输出目录) Then
            输出目录 = Environment.GetEnvironmentVariable("FFF_DANMAKU_REANCHOR_OUT")
        End If
        If String.IsNullOrWhiteSpace(输出目录) Then
            输出目录 = Path.Combine(Path.GetTempPath(), "3fp-danmaku-reanchor")
        End If

        If Not File.Exists(夹具) Then
            输出("=== 弹幕重锚同进程回归 ===")
            输出("EXIT 2 夹具不存在：" & 夹具)
            输出("NOT JUDGED：0 条断言被执行 —— 这是「没测」，不是通过。")
            写日志(输出目录)
            Return 2
        End If

        Dim 资料库 As 弹幕资料库 = Nothing
        Try
            资料库 = B站弹幕解析器.解析文件(夹具)
        Catch ex As Exception
            输出("=== 弹幕重锚同进程回归 ===")
            输出("EXIT 2 夹具解析失败：" & ex.GetType().Name & " " & ex.Message)
            输出("NOT JUDGED：0 条断言被执行 —— 这是「没测」，不是通过。")
            写日志(输出目录)
            Return 2
        End Try

        输出("=== 弹幕重锚（重新锚定到）同进程回归 ===")
        输出("夹具：" & 夹具)
        输出("几何：区域 " & 区域宽度 & "x" & 区域高度 & " 缩放 1.0；目标帧率 60；启用类型 仅常规滚动")
        If 资料库 Is Nothing OrElse 资料库.数量 = 0 Then
            输出("EXIT 2 夹具解析出 0 条弹幕 —— 无法构造跳变，未判定。")
            写日志(输出目录)
            Return 2
        End If

        ' 夹具前置：解析器给出的首末时间必须和 XML 字面量一致（手抄），否则后面所有
        ' "按出现时间手算"的期望值都建立在坏数据上。
        Dim 首秒 = 资料库.项目(0).出现时间.TotalSeconds
        Dim 末秒 = 资料库.项目(资料库.数量 - 1).出现时间.TotalSeconds
        检查(资料库.数量 = 3637, "夹具·条目数 = 3637（XML 里 <d> 计数）",
              "解析得=" & 资料库.数量)
        检查(Math.Abs(首秒 - 0.0R) < 1e-9 AndAlso Math.Abs(末秒 - 119.988R) < 1e-6,
              "夹具·首末出现时间 = 0.000 / 119.988（照抄 XML p 字段）",
              "首=" & N3(首秒) & " 末=" & N3(末秒))
        Dim 常规候选 = 窗口内候选(资料库, 1.5R, 12.5R)
        检查(常规候选 > 0, "夹具·1.5~12.5s 之间有 常规滚动 条目可锚",
              "候选=" & 常规候选)
        ' 记账（非断言）：本夹具 弹幕编号 全为 0 —— p 字段第 8 项写成 "1000000+N"，
        ' 尝试解析项目（弹幕.vb:186）用 Long.TryParse 且不看返回值，'+' 认不下就静默留 0。
        ' 于是 弹幕资料库 构造里的 ThenBy(弹幕编号) 退化成不排序，任何拿 弹幕编号
        ' 当条目身份的判据都会变成恒真。本回归改用引用做身份，并把这条写进报告。
        Dim 编号种类 As New HashSet(Of Long)()
        For Each 项 In 资料库.项目
            编号种类.Add(项.弹幕编号)
        Next
        输出("      实测：弹幕编号 取值种类=" & 编号种类.Count & "/" & 资料库.数量 &
              "（故条目身份用引用，不用编号）")

        场景A_前进五秒(资料库)
        场景B_后退五秒(资料库)
        场景C_阈值格子(资料库)

        输出("")
        输出($"合计：ok {通过} / FAIL {失败}（共 {通过 + 失败} 条断言）")
        If 通过 + 失败 = 0 Then
            输出("本档 0 条断言被执行 —— 不是通过。")
        End If
        写日志(输出目录)
        If 失败 > 0 Then Return 1
        Return 0
    End Function

    Private Sub 写日志(输出目录 As String)
        Try
            Directory.CreateDirectory(输出目录)
            Dim 路径 = Path.Combine(输出目录, "danmaku_reanchor_regression.log")
            File.WriteAllText(路径, String.Join(Environment.NewLine, 日志) & Environment.NewLine,
                               New UTF8Encoding(False))
            Console.WriteLine("日志写入：" & 路径)
        Catch ex As Exception
            Console.Error.WriteLine("日志写入失败：" & ex.Message)
        End Try
    End Sub
End Module
