Option Strict On
Option Explicit On

''' <summary>
''' 「AI 实时字幕」菜单树：标题栏菜单里的一个子菜单，内部再挂「字幕语言 / 模型档 /
''' 推理设备」三个二级子菜单。LakeUI 的 <c>ModernMenuItem.SubMenu</c> 是递归的
''' （每个弹窗自己处理悬停再开下一层），所以层级不限，这里只用两层。
'''
''' 为什么把开关收进子菜单：标题栏菜单是播放器主菜单，AI 字幕是一整套参数
''' （开关 + 语言 + 模型档 + 设备），平铺进去会把它撑成一堵墙。收进一层之后，
''' 主菜单只多一行，二级子菜单里才展开细节。
'''
''' 本类只做「视图 + 写设置」，不碰 worker 生命周期：
'''   · 点开关 → <see cref="播放器AI字幕控制器.切换启用"/>；
'''   · 点语言 / 档 / 设备 → 写 <see cref="设置.实例对象"/> → 落盘 →
'''     <see cref="播放器AI字幕控制器.设置已变更"/>，由控制器比对启动期参数后决定是否重启管道。
'''
''' 单档模型**不做成子菜单**（与设置页同一条纪律：结论定死的不摆成选项，
''' 置灰也算摆 —— 那只是把"没别的可选项"这件事换个方式说给人听）。
''' </summary>
Friend NotInheritable Class Ai字幕菜单
    Implements IDisposable

    Private Const 启用文字 As String = "启用 AI 实时字幕"
    Private Const 语言文字 As String = "字幕语言"
    Private Const 双语文字 As String = "双语字幕"
    Private Const 模型档文字 As String = "模型档"
    Private Const 设备文字 As String = "推理设备"
    Private Const 设置文字 As String = "AI 字幕设置…"

    Private ReadOnly 根菜单 As LakeUI.ModernContextMenu
    Private ReadOnly 控制器 As 播放器AI字幕控制器

    ' 本类往根菜单里加过的项。拆树时按这张表逐个摘，不 Items.Clear() ——
    ' 将来 Designer 里给这个菜单补静态项时，不会被这里顺手清掉。
    Private ReadOnly 已加项 As New List(Of LakeUI.ModernContextMenu.ModernMenuItem)

    Private 启用项 As LakeUI.ModernContextMenu.ModernMenuItem
    Private 语言项 As LakeUI.ModernContextMenu.ModernMenuItem
    Private 语言子菜单 As LakeUI.ModernContextMenu
    Private 双语项 As LakeUI.ModernContextMenu.ModernMenuItem
    Private 双语子菜单 As LakeUI.ModernContextMenu
    Private 档项 As LakeUI.ModernContextMenu.ModernMenuItem
    Private 档子菜单 As LakeUI.ModernContextMenu
    Private 设备项 As LakeUI.ModernContextMenu.ModernMenuItem
    Private 设备子菜单 As LakeUI.ModernContextMenu
    Private 设置项 As LakeUI.ModernContextMenu.ModernMenuItem

    ' 项 ↔ 标识。用「项 + 统一处理器 + 查表」而不是给每项挂一个闭包：
    ' 拆树时能一次性 RemoveHandler，不留一堆匿名委托互相引用。
    Private ReadOnly 语言项表 As New List(Of KeyValuePair(Of LakeUI.ModernContextMenu.ModernMenuItem, String))
    Private ReadOnly 双语项表 As New List(Of KeyValuePair(Of LakeUI.ModernContextMenu.ModernMenuItem, String))
    Private ReadOnly 档项表 As New List(Of KeyValuePair(Of LakeUI.ModernContextMenu.ModernMenuItem, String))
    Private ReadOnly 设备项表 As New List(Of KeyValuePair(Of LakeUI.ModernContextMenu.ModernMenuItem, String))

    ' 结构签名：包换了（档表变了）才重建整棵树。勾选状态另走 刷新勾选，
    ' 播放中打开菜单时不会被抽走。
    Private _结构签名 As String = String.Empty
    Private 已释放 As Boolean

    Friend Sub New(根菜单值 As LakeUI.ModernContextMenu, 控制器值 As 播放器AI字幕控制器)
        ArgumentNullException.ThrowIfNull(根菜单值)
        ArgumentNullException.ThrowIfNull(控制器值)
        Me.根菜单 = 根菜单值
        控制器 = 控制器值
        刷新(控制器.当前包配置)
    End Sub

    ''' <summary>包/档表或任一设置变了之后重新对齐菜单。幂等，可高频调用。</summary>
    Friend Sub 刷新(配置 As Ai字幕包配置)
        If 已释放 Then Return
        重建结构(配置)
        刷新勾选()
    End Sub

    ' ---------------- 结构 ----------------

    Private Sub 重建结构(配置 As Ai字幕包配置)
        Dim 签名 = 结构签名(配置)
        If String.Equals(签名, _结构签名, StringComparison.Ordinal) Then Return
        _结构签名 = 签名
        拆树()
        建树(配置)
    End Sub

    ''' <summary>包的目录 + 档名表。档表变了（换包、包里加了权重）才需要重建 ——
    ''' 只看"有没有包"不够：同一个目录换了清单也是另一套档。</summary>
    Private Shared Function 结构签名(配置 As Ai字幕包配置) As String
        If 配置 Is Nothing Then Return "无包"
        Dim 档名 As New List(Of String)
        For Each 档 In 配置.模型档表
            档名.Add(档.档名)
        Next
        Return 配置.引擎 & "|" & 配置.包目录 & "→" & String.Join("|", 档名)
    End Function

    Private Sub 拆树()
        If 启用项 IsNot Nothing Then RemoveHandler 启用项.Click, AddressOf 启用_Click
        If 语言项 IsNot Nothing Then RemoveHandler 语言项.Click, AddressOf 语言_Click
        If 双语项 IsNot Nothing Then RemoveHandler 双语项.Click, AddressOf 双语_Click
        If 档项 IsNot Nothing Then RemoveHandler 档项.Click, AddressOf 档_Click
        If 设备项 IsNot Nothing Then RemoveHandler 设备项.Click, AddressOf 设备_Click
        If 设置项 IsNot Nothing Then RemoveHandler 设置项.Click, AddressOf 设置_Click
        For Each 项 In 已加项
            根菜单.Items.Remove(项)
        Next
        已加项.Clear()
        语言项表.Clear()
        双语项表.Clear()
        档项表.Clear()
        设备项表.Clear()
        启用项 = Nothing
        语言项 = Nothing
        语言子菜单 = Nothing
        双语项 = Nothing
        双语子菜单 = Nothing
        档项 = Nothing
        档子菜单 = Nothing
        设备项 = Nothing
        设备子菜单 = Nothing
        设置项 = Nothing
    End Sub

    Private Sub 建树(配置 As Ai字幕包配置)
        启用项 = 加项(根菜单, 启用文字)
        AddHandler 启用项.Click, AddressOf 启用_Click

        加分隔线()

        语言子菜单 = 创建子菜单()
        语言项 = 加项(根菜单, 语言文字)
        语言项.SubMenu = 语言子菜单
        For Each id In Ai字幕语言.主项
            Dim 子项 = 加项(语言子菜单, Ai字幕语言.显示名(id))
            语言项表.Add(New KeyValuePair(Of LakeUI.ModernContextMenu.ModernMenuItem, String)(子项, id))
            AddHandler 子项.Click, AddressOf 语言_Click
        Next

        ' 双语只有 GGUF 引擎做得到（同一 llama-server 多打一个 /completion 续写）。
        ' ONNX 那条腿是纯 ASR，没有文本生成能力 —— 那就**不摆出来**，而不是摆成灰色：
        ' 置灰仍在暗示"你能选"，与「单档不做子菜单」是同一条纪律。
        If 当前引擎() = Ai字幕包.引擎Llama Then
            双语子菜单 = 创建子菜单()
            双语项 = 加项(根菜单, 双语文字)
            双语项.SubMenu = 双语子菜单
            For Each id In Ai字幕双语.全部
                Dim 子项 = 加项(双语子菜单, Ai字幕双语.短名(id))
                双语项表.Add(New KeyValuePair(Of LakeUI.ModernContextMenu.ModernMenuItem, String)(子项, id))
                AddHandler 子项.Click, AddressOf 双语_Click
            Next
        End If

        ' 只有包里真放了第二套权重才给模型档子菜单；单档是事实，不是选项。
        If 配置 IsNot Nothing AndAlso 配置.模型档表.Count > 1 Then
            档子菜单 = 创建子菜单()
            档项 = 加项(根菜单, 模型档文字)
            档项.SubMenu = 档子菜单
            ' 第一项是「用包默认档」（设置里存空串），与设置页下拉一一对应。
            Dim 默认项 = 加项(档子菜单, 默认档文字(配置))
            档项表.Add(New KeyValuePair(Of LakeUI.ModernContextMenu.ModernMenuItem, String)(默认项, String.Empty))
            AddHandler 默认项.Click, AddressOf 档_Click
            For Each 档 In 配置.模型档表
                Dim 子项 = 加项(档子菜单, 档文字(档))
                档项表.Add(New KeyValuePair(Of LakeUI.ModernContextMenu.ModernMenuItem, String)(子项, 档.档名))
                AddHandler 子项.Click, AddressOf 档_Click
            Next
        End If

        设备子菜单 = 创建子菜单()
        设备项 = 加项(根菜单, 设备文字)
        设备项.SubMenu = 设备子菜单
        For Each id In Ai字幕推理档.设备全部
            Dim 子项 = 加项(设备子菜单, Ai字幕推理档.设备显示名(id))
            设备项表.Add(New KeyValuePair(Of LakeUI.ModernContextMenu.ModernMenuItem, String)(子项, id))
            AddHandler 子项.Click, AddressOf 设备_Click
        Next

        加分隔线()

        设置项 = 加项(根菜单, 设置文字)
        AddHandler 设置项.Click, AddressOf 设置_Click
    End Sub

    Private Function 加项(父 As LakeUI.ModernContextMenu,
                         文字 As String) As LakeUI.ModernContextMenu.ModernMenuItem
        Dim 项 As New LakeUI.ModernContextMenu.ModernMenuItem With {
            .Text = 文字, .ToggleCheckOnClick = False, .CloseOnClick = True
        }
        父.Items.Add(项)
        ' 根菜单的项由拆树负责摘；子菜单的项随子菜单对象一起丢弃，不必单独记。
        If 父 Is 根菜单 Then 已加项.Add(项)
        Return 项
    End Function

    ''' <summary>往根菜单加一条分割线。**必须**走 已加项 追踪：拆树按这张表摘项，
    ''' 分割线一旦漏记，下次重建（包从无到有必然发生一次）它就残留在子菜单顶部 ——
    ''' 表现为菜单头部多出一块空白，悬停时整块高亮。</summary>
    Private Sub 加分隔线()
        Dim 线 = 分隔线()
        根菜单.Items.Add(线)
        已加项.Add(线)
    End Sub

    Private Shared Function 分隔线() As LakeUI.ModernContextMenu.ModernMenuItem
        Return New LakeUI.ModernContextMenu.ModernMenuItem With {.IsSeparator = True}
    End Function

    ''' <summary>二级子菜单的样式全部从根菜单继承。手写第二份配置迟早和主菜单
    ''' 走形（改了字色/圆角只改一处），这里只表达"和上层一样"。</summary>
    Private Function 创建子菜单() As LakeUI.ModernContextMenu
        Return New LakeUI.ModernContextMenu With {
            .AnimationFPS = 根菜单.AnimationFPS,
            .BackColor = 根菜单.BackColor,
            .BackdropBlurRadius = 根菜单.BackdropBlurRadius,
            .BackdropMode = 根菜单.BackdropMode,
            .BackdropNoiseOpacity = 根菜单.BackdropNoiseOpacity,
            .BackdropTintColor = 根菜单.BackdropTintColor,
            .BorderSize = 根菜单.BorderSize,
            .DescriptionFont = 根菜单.DescriptionFont,
            .DescriptionForeColor = 根菜单.DescriptionForeColor,
            .HoverBackColor = 根菜单.HoverBackColor,
            .HoverRadius = 根菜单.HoverRadius,
            .IconSize = 根菜单.IconSize,
            .ItemHeight = 根菜单.ItemHeight,
            .ItemPadding = 根菜单.ItemPadding,
            .MenuFont = 根菜单.MenuFont,
            .MenuPadding = 根菜单.MenuPadding,
            .PressedBackColor = 根菜单.PressedBackColor,
            .SeparatorColor = 根菜单.SeparatorColor,
            .SeparatorHeight = 根菜单.SeparatorHeight
        }
    End Function

    ' ---------------- 勾选 ----------------

    Private Sub 刷新勾选()
        If 已释放 Then Return
        If 启用项 IsNot Nothing Then 启用项.Checked = 控制器.启用

        Dim 语言 = Ai字幕语言.规整(设置.实例对象.Ai字幕语言)
        For Each 对 In 语言项表
            对.Key.Checked = String.Equals(对.Value, 语言, StringComparison.Ordinal)
        Next

        Dim 档名 = If(设置.实例对象.Ai字幕模型档, String.Empty).Trim()
        For Each 对 In 档项表
            对.Key.Checked = String.Equals(对.Value, 档名, StringComparison.OrdinalIgnoreCase)
        Next

        Dim 双语 = Ai字幕双语.规整(设置.实例对象.Ai字幕双语)
        For Each 对 In 双语项表
            对.Key.Checked = String.Equals(对.Value, 双语, StringComparison.Ordinal)
        Next

        Dim 设备 = Ai字幕推理档.规整设备(设置.实例对象.Ai字幕运行设备)
        For Each 对 In 设备项表
            对.Key.Checked = String.Equals(对.Value, 设备, StringComparison.Ordinal)
        Next
    End Sub

    ' ---------------- 点击 ----------------

    Private Sub 启用_Click(sender As Object, e As EventArgs)
        ' 起停、提示、持久化偏好都在控制器里（它同时要动 worker 与 tap）。
        控制器.切换启用()
    End Sub

    Private Sub 语言_Click(sender As Object, e As EventArgs)
        Dim id = 查标识(语言项表, TryCast(sender, LakeUI.ModernContextMenu.ModernMenuItem))
        If id Is Nothing Then Return
        设置.实例对象.Ai字幕语言 = Ai字幕语言.规整(id)
        ' 菜单项的语义必须闭合：项写的是「只显示 X」，点了就得真的只显示 X。
        ' 闸门开关在菜单里没有独立入口，跟着语言项一起定——选具体语言即开闸，
        ' 选自动即关闸。不联动就留下"点了只显示中文、画面却全语种照出"的假动作，
        ' 那比不提供这个项更糟。想要"选定语言但不筛"的组合，去设置页手动关。
        设置.实例对象.Ai字幕仅所选语言 = Ai字幕语言.已指定(设置.实例对象.Ai字幕语言)
        提交()
    End Sub

    Private Sub 双语_Click(sender As Object, e As EventArgs)
        Dim id = 查标识(双语项表, TryCast(sender, LakeUI.ModernContextMenu.ModernMenuItem))
        If id Is Nothing Then Return
        设置.实例对象.Ai字幕双语 = Ai字幕双语.规整(id)
        提交()
    End Sub

    Private Sub 档_Click(sender As Object, e As EventArgs)
        Dim 名 = 查标识(档项表, TryCast(sender, LakeUI.ModernContextMenu.ModernMenuItem))
        If 名 Is Nothing Then Return
        设置.实例对象.Ai字幕模型档 = 名
        提交()
    End Sub

    Private Sub 设备_Click(sender As Object, e As EventArgs)
        Dim id = 查标识(设备项表, TryCast(sender, LakeUI.ModernContextMenu.ModernMenuItem))
        If id Is Nothing Then Return
        设置.实例对象.Ai字幕运行设备 = Ai字幕推理档.规整设备(id)
        ' 换设备必须把后端归位：旧后端可能根本不属于新设备（CUDA 不在 AMD 的表里），
        ' 不归位就留下「AMD 显卡 + CUDA」这种不存在的组合。与设置页 重建后端下拉 同一判据。
        设置.实例对象.Ai字幕推理后端 =
            Ai字幕推理档.规整后端(设置.实例对象.Ai字幕运行设备,
                                   设置.实例对象.Ai字幕推理后端, 当前引擎())
        提交()
    End Sub

    Private Sub 设置_Click(sender As Object, e As EventArgs)
        控制器.请求打开设置页()
    End Sub

    ''' <summary>菜单改设置后立刻落盘：菜单不依附设置窗口，没有"关页时统一保存"
    ''' 那一刻，不落盘就等于重启后回滚。落盘本身是原子的（写 tmp 再 move）。</summary>
    Private Sub 提交()
        Try
            设置.退出时保存设置()
        Catch
            ' 落盘失败不该让菜单点击抛出去；下次退出时还会再存一遍。
        End Try
        刷新勾选()
        控制器.设置已变更()
    End Sub

    ''' <summary>后端候选表跟包的引擎走（GGUF 只有 Vulkan 一条腿，ONNX 才有别的）。</summary>
    Private Function 当前引擎() As String
        Dim 配置 = 控制器.当前包配置
        Return If(配置 Is Nothing, Ai字幕包.引擎Onnx, 配置.引擎)
    End Function

    Private Shared Function 查标识(表 As List(Of KeyValuePair(Of LakeUI.ModernContextMenu.ModernMenuItem, String)),
                                    项 As LakeUI.ModernContextMenu.ModernMenuItem) As String
        If 项 Is Nothing Then Return Nothing
        For Each 对 In 表
            If 对.Key Is 项 Then Return 对.Value
        Next
        Return Nothing
    End Function

    ' ---------------- 项文字 ----------------

    Private Shared Function 默认档文字(配置 As Ai字幕包配置) As String
        If 配置 Is Nothing OrElse String.IsNullOrEmpty(配置.默认档形态) Then Return "默认（包自带权重）"
        Return $"默认（{配置.默认档形态}）"
    End Function

    ''' <summary>档名与形态重复时只留一个（官方包档名通常就叫 int8），
    ''' 否则出现「int8（int8 · 941 MB）」这种自我重复。与设置页 档项文字 同一规则。</summary>
    Private Shared Function 档文字(档 As Ai字幕模型档) As String
        Dim 部分 As New List(Of String)
        If Not String.IsNullOrEmpty(档.形态) AndAlso
           Not String.Equals(档.档名, 档.形态, StringComparison.OrdinalIgnoreCase) Then
            部分.Add(档.形态)
        End If
        Dim 体 = Ai字幕包.体积文本(档.体积字节)
        If 体 <> "" Then 部分.Add(体)
        If 部分.Count = 0 Then Return 档.档名
        Return $"{档.档名}（{String.Join(" · ", 部分)}）"
    End Function

    Public Sub Dispose() Implements IDisposable.Dispose
        If 已释放 Then Return
        已释放 = True
        拆树()
        GC.SuppressFinalize(Me)
    End Sub
End Class
