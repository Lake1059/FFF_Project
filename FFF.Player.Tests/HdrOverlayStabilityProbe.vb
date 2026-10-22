' HDR 行流级口径 + 粘滞动态段 + 媒体信息列表去逐帧 —— 三判据进程内探针
'
' 背景缺陷：内核对 HDR10+ 流的逐帧分类会让 快照.HDR规格 / 快照.动态HDR元数据有效
' 每个 200ms 周期横跳（有 SEI 的帧判 HDR10+，无 SEI 的帧回落流级状态），宿主两处
' 直接消费它：信息图层 HDR 行整行文本横跳；Form媒体信息 把这三个逐帧字段收进了
' 列表重建签名 ⇒ 列表每周期 Items.Clear，选中项"选上立马被取消"。
'
' 本探针按新口径的三条结构判据收口（合成快照 + 真 呈现器/真 Form，无播放会话）：
'   1) HDR 行规格取流级口径：快照枚举翻成 HDR10Plus 时，行文本仍以流声明
'      （流.HDR格式="HDR10"）为准 —— 旧逻辑在这一帧会输出 "HDR10+"，天然两臂分叉。
'   2) 动态段粘滞：本流内一旦见过 动态HDR元数据有效=1，后续回落帧仍显示
'      "动态元数据已用于处理"；换媒体路径后重置（未见之前不显示）。
'   3) 列表签名脱钩：只在签名外字段横跳时 刷新() 不重建（条目实例引用不变、
'      SelectedIndex 保持）；签名内字段（显示器峰值尼特）一变即重建 —— 牙齿臂。
'
' 用法：FFF.Player.Tests --hdr-row-stability  /  --media-list-stability
' 退出码：0 全绿 / 1 有失败条 / 2 前置不满足

Imports System
Imports System.Collections.Generic
Imports System.Drawing
Imports System.Reflection
Imports FFF.Player

Friend Module HdrOverlayStabilityProbe
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

    Private Function HDR行(行 As IReadOnlyList(Of String)) As String
        For Each 文本 In 行
            If 文本.StartsWith("HDR：", StringComparison.Ordinal) Then Return 文本
        Next
        Return String.Empty
    End Function

    Private Function 媒体信息合成() As 媒体信息
        Dim 流 = New 媒体流信息 With {
            .索引 = 0, .类型 = "video", .编码 = "hevc",
            .宽度 = 3840, .高度 = 2160,
            .HDR格式 = "HDR10", .是HDR = True, .动态HDR元数据 = True}
        Return New 媒体信息 With {.流 = New List(Of 媒体流信息) From {流}}
    End Function

    Friend Function 运行HDR行探针() As Integer
        Console.WriteLine("3FP HDR 行稳定性探针（--hdr-row-stability：真呈现器 + 合成快照）")
        Dim 画面 As New 播放器画面控件()
        Dim 当前快照 As 播放器快照 = Nothing
        Dim 媒体路径 = "C:\probe\hdr10plus.mp4"
        Dim 呈现器 As New 播放器信息图层呈现器(
            画面,
            Function() 当前快照,
            Function() 媒体信息合成(),
            Function() 媒体路径,
            Function() Nothing, Function() Nothing, Function() Nothing, Function() Nothing,
            Function() WASAPI共享模式.共享,
            Function() "P8.1",
            Sub(大小 As Size, 命令 As IReadOnlyList(Of 定时文字命令), 序号 As ULong, 值 As Single)
            End Sub)

        ' 帧1：无 SEI 帧（动态=0，快照枚举=HDR10）。流级 HDR10，不该有动态段。
        ' 帧1 也是 HDR 行锁存的取数点（是HDR源=1）：此后快照派生段全部冻结。
        当前快照 = 造快照(HDR格式.HDR10, False)
        Dim 行1 = HDR行(呈现器.读取调试文本行(媒体信息合成(), 当前快照, 媒体路径))
        检查(行1.Contains("HDR10") AndAlso Not 行1.Contains("HDR10+"), "帧1 规格走流级口径", 行1)
        检查(Not 行1.Contains("动态元数据"), "帧1 无动态段（尚未见过有效帧）", 行1)
        检查(行1.Contains("源峰值 1000尼特"), "帧1 锁存源峰值 1000", 行1)

        ' 帧2：带 SEI 帧（动态=1，快照枚举翻 HDR10Plus）。规格必须仍是流的 HDR10；
        ' 旧逻辑在这里会输出 "HDR10+" —— 本条就是两臂分叉判据。
        当前快照 = 造快照(HDR格式.HDR10Plus, True)
        Dim 行2 = HDR行(呈现器.读取调试文本行(媒体信息合成(), 当前快照, 媒体路径))
        检查(行2.Contains("HDR10") AndAlso Not 行2.Contains("HDR10+"),
             "帧2 逐帧枚举翻 HDR10Plus 后规格仍为流级 HDR10", 行2)
        检查(行2.Contains("P8.1"), "帧2 内容等级仍在行内", 行2)
        检查(行2.Contains("动态元数据已用于处理"), "帧2 出现动态段", 行2)

        ' 帧2.5：快照逐帧派生段开始横跳（枚举翻 HDR10+、源峰值翻 2000、回退位翻真）——
        ' 锁存后行内必须还是帧1 取到的值。旧逻辑在这里整行横跳。
        当前快照 = 造快照(HDR格式.HDR10Plus, True, 源峰值:=2000UI, 回退:=True)
        Dim 行25 = HDR行(呈现器.读取调试文本行(媒体信息合成(), 当前快照, 媒体路径))
        检查(行25.Contains("源峰值 1000尼特") AndAlso Not 行25.Contains("2000"),
             "帧2.5 源峰值锁存不随快照翻动", 行25)
        检查(Not 行25.Contains("HDR fallback"), "帧2.5 回退位锁存不随快照翻动", 行25)

        ' 帧3：回落帧（动态=0）。粘滞位保持。
        当前快照 = 造快照(HDR格式.HDR10, False)
        Dim 行3 = HDR行(呈现器.读取调试文本行(媒体信息合成(), 当前快照, 媒体路径))
        检查(行3.Contains("动态元数据已用于处理"), "帧3 回落帧动态段粘滞不消失", 行3)

        ' 帧4：换片（媒体路径变）→ 粘滞重置，未见有效帧前不显示。
        媒体路径 = "C:\probe\hdr10plus_第二部.mp4"
        当前快照 = 造快照(HDR格式.HDR10, False)
        Dim 行4 = HDR行(呈现器.读取调试文本行(媒体信息合成(), 当前快照, 媒体路径))
        检查(Not 行4.Contains("动态元数据"), "帧4 换片后粘滞重置", 行4)

        ' 帧5：新流里见到有效帧 → 再次出现。
        当前快照 = 造快照(HDR格式.HDR10Plus, True)
        Dim 行5 = HDR行(呈现器.读取调试文本行(媒体信息合成(), 当前快照, 媒体路径))
        检查(行5.Contains("动态元数据已用于处理"), "帧5 新流见到有效帧后动态段再现", 行5)

        ' 帧6：用户换档（请求色彩模式变）→ HDR 行重新取一次：换档时刻的
        ' 峰值映射档 + 目标 427 尼特必须可见（合法变化不能被冻结吞掉）。
        当前快照 = 造快照(HDR格式.HDR10Plus, True, 请求档:=色彩输出模式.峰值映射HDR,
                         目标尼特:=427UI, 实际档:=色彩输出模式.峰值映射HDR)
        Dim 行6 = HDR行(呈现器.读取调试文本行(媒体信息合成(), 当前快照, 媒体路径))
        检查(行6.Contains("显示目标 427尼特"), "帧6 用户换档后重新取数（目标可见）", 行6)

        ' 帧7：换档后快照目标再翻动 —— 新一轮锁存生效，行内保持帧6 的值。
        当前快照 = 造快照(HDR格式.HDR10Plus, True, 源峰值:=2000UI, 请求档:=色彩输出模式.峰值映射HDR,
                         目标尼特:=900UI, 实际档:=色彩输出模式.峰值映射HDR)
        Dim 行7 = HDR行(呈现器.读取调试文本行(媒体信息合成(), 当前快照, 媒体路径))
        检查(行7.Contains("显示目标 427尼特") AndAlso 行7.Contains("源峰值 1000尼特"),
             "帧7 换档后第二轮锁存冻结", 行7)

        Console.WriteLine($"HDR 行稳定性：通过 {通过} 条，失败 {失败} 条")
        Return If(失败 = 0, 0, 1)
    End Function

    Private Function 造快照(规格 As HDR格式, 动态有效 As Boolean,
                           Optional 源峰值 As UInteger = 1000UI, Optional 回退 As Boolean = False,
                           Optional 请求档 As 色彩输出模式 = 色彩输出模式.映射到SDR,
                           Optional 目标尼特 As UInteger = 0UI,
                           Optional 实际档 As 色彩输出模式 = 色彩输出模式.映射到SDR) As 播放器快照
        Dim 原生 As New 原生播放器快照 With {
            .大小 = CUInt(Runtime.InteropServices.Marshal.SizeOf(Of 原生播放器快照)()),
            .状态 = CUInt(播放状态.正在播放),
            .解码器 = CUInt(解码模式.GPU),
            .请求色彩模式 = CUInt(请求档),
            .实际色彩模式 = CUInt(实际档),
            .当前视频流 = 0,
            .视频宽度 = 3840UI, .视频高度 = 2160UI,
            .是HDR源 = 1UI,
            .视频队列帧数 = 8UI,
            .源峰值尼特 = 源峰值,
            .HDR格式 = CUInt(规格),
            .动态HDR元数据有效 = If(动态有效, 1UI, 0UI),
            .HDR有效目标峰值尼特 = 目标尼特,
            .HDR回退有效 = If(回退, 1UI, 0UI),
            .显示器峰值尼特 = 1000UI}
        Return New 播放器快照(原生)
    End Function

    Friend Function 运行媒体列表探针() As Integer
        Console.WriteLine("3FP 媒体信息列表稳定性探针（--media-list-stability：真 Form + 合成快照）")
        Dim 当前快照 As 播放器快照 = 造快照(HDR格式.HDR10Plus, True)
        Dim 表单 As New Form媒体信息(
            Function() 媒体信息合成(),
            Function() 当前快照)
        Dim 标志 = BindingFlags.NonPublic Or BindingFlags.Instance
        Dim 列表字段 = 表单.GetType().GetField("_UltraDetailListView1", 标志) ' Friend WithEvents 字段在 IL 里带 _ 前缀
        If 列表字段 Is Nothing Then 列表字段 = 表单.GetType().GetField("UltraDetailListView1", 标志)
        If 列表字段 Is Nothing Then
            Console.WriteLine("NOT JUDGED：找不到 UltraDetailListView1 字段（设计器改名了）。")
            Return 2
        End If
        ' 测试工程不引用 LakeUI（避免多一份程序集引用面），列表交互全走反射：
        ' Items / SelectedIndex / SelectedItem 都是控件自己的公共面，反射只是取址方式。
        Dim 列表 = DirectCast(列表字段.GetValue(表单), Object)
        Dim 列表类型 = 列表.GetType()
        Dim 条目集合 = 列表类型.GetProperty("Items").GetValue(列表)
        Dim 条目集合类型 = 条目集合.GetType()
        Dim 取条目 = Function(索引 As Integer) 条目集合类型.GetProperty("Item").GetValue(条目集合,
            New Object() {索引})

        ' 基线：先刷一次拿到条目实例与选中态。
        表单.刷新()
        Dim 条目数 = CInt(条目集合类型.GetProperty("Count").GetValue(条目集合))
        If 条目数 = 0 Then
            Console.WriteLine("NOT JUDGED：首次刷新没有产出条目（合成媒体信息被拒）。")
            Return 2
        End If
        Dim 基线条目 = 取条目(0)
        列表类型.GetProperty("SelectedIndex").SetValue(列表, 1)

        ' 逐帧横跳臂：动态位翻、枚举翻 —— 签名内已无这些字段，不该重建。
        当前快照 = 造快照(HDR格式.HDR10, False)
        表单.刷新()
        检查(CObj(取条目(0)) Is CObj(基线条目), "逐帧横跳后条目实例未重建", $"count={条目数}")
        检查(CInt(列表类型.GetProperty("SelectedIndex").GetValue(列表)) = 1,
             "逐帧横跳后选中项保持", $"selectedIndex={列表类型.GetProperty("SelectedIndex").GetValue(列表)}")

        ' 牙齿臂：签名内字段（显示器峰值尼特）变化 —— 必须重建（实例引用变、选中丢失）。
        当前快照 = 造列表快照(HDR格式.HDR10, False, 1200UI)
        表单.刷新()
        Dim 仍同实例 = CObj(取条目(0)) Is CObj(基线条目)
        检查(Not 仍同实例, "签名内字段变化触发重建（牙齿）", $"sameInstance={仍同实例}")
        检查(CInt(列表类型.GetProperty("SelectedIndex").GetValue(列表)) <> 1,
             "重建后旧选中项不再保持", $"selectedIndex={列表类型.GetProperty("SelectedIndex").GetValue(列表)}")

        Console.WriteLine($"媒体列表稳定性：通过 {通过} 条，失败 {失败} 条")
        Return If(失败 = 0, 0, 1)
    End Function

    Private Function 造列表快照(规格 As HDR格式, 动态有效 As Boolean, 显示器峰值 As UInteger) As 播放器快照
        Dim 原生 As New 原生播放器快照 With {
            .大小 = CUInt(Runtime.InteropServices.Marshal.SizeOf(Of 原生播放器快照)()),
            .状态 = CUInt(播放状态.正在播放),
            .解码器 = CUInt(解码模式.GPU),
            .当前视频流 = 0,
            .视频宽度 = 3840UI, .视频高度 = 2160UI,
            .是HDR源 = 1UI,
            .视频队列帧数 = 8UI,
            .源峰值尼特 = 1000UI,
            .HDR格式 = CUInt(规格),
            .动态HDR元数据有效 = If(动态有效, 1UI, 0UI),
            .显示器峰值尼特 = 显示器峰值}
        Return New 播放器快照(原生)
    End Function
End Module
