Option Strict On
Option Explicit On

Imports System.Collections.Generic
Imports System.Drawing
Imports System.Globalization
Imports System.Windows.Forms

''' <summary>
''' AI 字幕设置页：选扩展包 → 选设备 + 后端 →（可选）调窗口/步长/语言/偏移。
'''
''' 推理相关的两个下拉是**一排两格**：左边「设备」（CPU / NVIDIA / AMD / Intel /
''' NPU），右边「后端」（CUDA / DirectML / OpenVINO / onnxruntime CPU），后者列出
''' 什么由前者决定。设备在前是因为用户先知道自己有什么卡，才谈得上选哪条软件栈；
''' 反过来的「一张混着所有运行时的大列表」是 llama.cpp 时代的老毛病 —— 挑了个名字，
''' 最后要在日志里才知道到底跑了哪。
'''
''' 只有一项可选时下拉置灰、原因写进项文字（auto 设备只剩 auto，AMD 只剩 DirectML）。
''' 这是本页一贯的口径：藏起来用户会以为这项丢了，只留一句水印则等于没说
''' （SelectedIndex ≥ 0 时水印根本看不见）。
'''
''' 删掉的老东西不再复活：GPU 层数（EP 自己决定算子落在哪，没有「卸几层」这回事）、
''' 设备序号（同一个 EP 的下标由引擎给）、附加参数（没有 llama-server 可传了）。
''' </summary>
Friend Class Form设置_AI字幕
    Private 正在初始化 As Boolean

    ' 设备下拉当前项 ↔ 标识的映射由 Ai字幕推理档.设备全部 现算（存的是标识字符串
    ' 不是下标，所以顺序可以安全调整）。
    ' 预置按「算力倍率」排，不按手感排。倍率 = 窗口 ÷ 步长，就是每过 1 秒要处理几秒
    ' 音频；实时判据是 rtf × 倍率 ≤ 1（rtf = 处理 1 秒音频花几秒）。CPU int8 实测
    ' rtf ≈ 1，所以只有 1.0× 追得上。旧的三档（2/1、5/3、8/4）倍率是 2.0 / 1.67 / 2.0
    ' —— 在 CPU 上全部会积压，那是 llama.cpp 时代 2 秒窗也能用的年代留下的。
    ' 项文字只留「名字 + 倍率」（下拉宽 248），具体秒数在项提示里，选中后两格也会填。
    Private Shared ReadOnly _预置显示 As String() = {
        "跟随包清单", "不重叠 1.0×", "均衡 1.5×", "长句 1.6×", "低延迟 2.0×", "自定义"}
    Private Shared ReadOnly _预置窗口 As Double() = {0, 6, 6, 8, 4, 0}
    Private Shared ReadOnly _预置步长 As Double() = {0, 6, 4, 5, 2, 0}
    Private Shared ReadOnly _预置说明 As String() = {
        "用清单里的 window / step，不覆盖",
        "窗 6s · 步长 6s：窗口等于步长，每段音频只算一次 —— CPU 上唯一追得上实时的组合",
        "窗 6s · 步长 4s：每 4 秒要处理 6 秒音频（1.5 倍），rtf 需 ≤ 0.67",
        "窗 8s · 步长 5s：窗更大、上下文更完整，倍率 1.6",
        "窗 4s · 步长 2s：出字最快，算力翻倍（2.0×），建议配显卡",
        "当前值与任何预置都对不上（手填的）"}

    ' 推理线程数：0 = 交给 worker 取 min(6, 核数)。6 不是随手定的上限 —— 超过 6 时在
    ' P+E 混合架构上前台线程会落到 E-core，同步点被拖住，实测反而更慢。全大核的机器
    ' 可以往上试，所以 8 / 12 也列出来，但项提示写明前提。
    Private Shared ReadOnly _线程显示 As String() = {"自动（最多 6）", "4", "6", "8（全大核）", "12（全大核）"}
    Private Shared ReadOnly _线程值 As Integer() = {0, 4, 6, 8, 12}
    Private Shared ReadOnly _线程说明 As String() = {
        "worker 取 min(6, 核数) —— 混合架构上的最优解",
        "4 线程：把核留给播放与系统",
        "6 线程：默认上限",
        "8 线程：P+E 混合架构上通常更慢；全大核机器可以试",
        "12 线程：只有全大核且核数足够时才可能有用"}

    Public Sub New()
        正在初始化 = True
        Try
            InitializeComponent()
            ' 语言项只列经过真机验证的主项 4 个；顺序与 Ai字幕语言.主项 一一对应，
            ' 于是 SelectedIndex 就是语言下标，不需要另存映射。
            For Each id In Ai字幕语言.主项
                MCB_语言.Items.Add(Ai字幕语言.显示名(id))
            Next
            ' 预置下拉是静态表：构造时填一次（初始化页面会被反复调用，在里面 Add
            ' 会越加越多），选中项等设置读进来后再同步（见 同步预置下拉）。
            For i = 0 To _预置显示.Length - 1
                MCB_预置.Items.Add(_预置显示(i))
                MCB_预置.ItemToolTips.Add(_预置显示(i), _预置说明(i))
            Next
            ' 线程下拉是静态表：问的是「给它几个核」，与本机装了什么无关。
            For i = 0 To _线程显示.Length - 1
                MCB_线程.Items.Add(_线程显示(i))
                MCB_线程.ItemToolTips.Add(_线程显示(i), _线程说明(i))
            Next
            ' 设备下拉是静态表：它问的是「这台机器有哪类硬件」，与装没装运行时无关。
            ' 真正能不能跑由 worker 在启动时试（试不到就降档并回报一句），这一页
            ' 不需要为此扫描任何东西。
            For Each id In Ai字幕推理档.设备全部
                ' 项提示挂在 ItemToolTips（按项文本索引的字典），不是 Items 的重载：
                ' 设备维的取舍（AMD 在 Windows 上只剩 DirectML、NPU 只有 OpenVINO
                ' 一条路）放这里，不另占面板行。
                Dim 设备项 = Ai字幕推理档.设备显示名(id)
                MCB_设备.Items.Add(设备项)
                MCB_设备.ItemToolTips.Add(设备项, Ai字幕推理档.设备说明(id))
            Next
            ' 后端下拉不在这里填：它的内容由当前设备决定，见 重建后端下拉
            ' （构造期还不知道设置里存的是哪台设备）。
        Finally
            正在初始化 = False
        End Try
    End Sub

    Friend Sub 初始化页面()
        正在初始化 = True
        Try
            对齐面板宽度()
            MTB_包目录.Text = 设置.实例对象.Ai字幕包目录
            MCB_设备.SelectedIndex = 设备下标(设置.实例对象.Ai字幕运行设备)
            ' 后端下拉必须在这里显式重建一次：赋值 SelectedIndex 时正在初始化，
            ' 事件被吞掉了，不重建就是打开设置页看到一个空下拉。
            重建后端下拉()
            ' 残留的非主项语言值（老设置/手改 json，如 "ja"）在这里一次性归 auto：
            ' 只改内存、不主动落盘，用户一动语言就会把主项值写回去。
            Dim 旧语言 = 设置.实例对象.Ai字幕语言
            If Not String.IsNullOrEmpty(旧语言) AndAlso Ai字幕语言.规整(旧语言) <> 旧语言 Then
                设置.实例对象.Ai字幕语言 = Ai字幕语言.规整(旧语言)
            End If
            MCB_语言.SelectedIndex = 语言下标(设置.实例对象.Ai字幕语言)
            MCK_仅所选语言.Checked = 设置.实例对象.Ai字幕仅所选语言
            刷新语言可用性()
            ' 高级项默认展开：窗口/步长/模型档/偏移都是用户明确想调才会来调的东西，
            ' 藏一层折叠只会让人以为"这些不让改"。
            MCK_显示高级.Checked = True
            MTB_窗口秒.Text = 覆盖文本(设置.实例对象.Ai字幕窗口秒覆盖)
            MTB_步长秒.Text = 覆盖文本(设置.实例对象.Ai字幕步长秒覆盖)
            MTB_偏移.Text = 偏移文本(设置.实例对象.Ai字幕字幕偏移秒)
            同步预置下拉()
            同步线程下拉()
            显示高级(MCK_显示高级.Checked)
            刷新状态()
        Finally
            正在初始化 = False
        End Try
    End Sub

    ' ---------------- 包 ----------------

    Private Sub MTB_包目录_Leave(sender As Object, e As EventArgs) Handles MTB_包目录.Leave
        If 正在初始化 Then Return
        设置.实例对象.Ai字幕包目录 = MTB_包目录.Text.Trim()
        刷新状态()
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    Private Sub MB_浏览_Click(sender As Object, e As EventArgs) Handles MB_浏览.Click
        Try
            Using 对话框 As New OpenFileDialog With {
                .Title = "选择扩展包清单（ai-subtitles.json）",
                .Filter = "AI 字幕扩展包清单|ai-subtitles.json|JSON 文件|*.json|所有文件|*.*",
                .CheckFileExists = True
            }
                If 对话框.ShowDialog() <> DialogResult.OK Then Return
                Dim 目录 = IO.Path.GetDirectoryName(对话框.FileName)
                If String.IsNullOrWhiteSpace(目录) Then Return
                MTB_包目录.Text = 目录
                设置.实例对象.Ai字幕包目录 = 目录
                刷新状态()
                Form1.当前主窗体?.应用Ai字幕设置()
            End Using
        Catch
            ' 浏览失败不该打断设置页。
        End Try
    End Sub

    ' ---------------- 推理档 ----------------

    Private Sub MCB_设备_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_设备.SelectedIndexChanged
        If 正在初始化 OrElse MCB_设备.SelectedIndex < 0 Then Return
        设置.实例对象.Ai字幕运行设备 = 设备标识(MCB_设备.SelectedIndex)
        ' 换设备必须重建后端：旧后端可能根本不属于新设备（CUDA 不在 AMD 的表里），
        ' 不重建就会留下「AMD 显卡 + CUDA」这种不存在的组合。
        重建后端下拉()
        刷新状态()
        ' 设备 / 后端都是启动期参数：控制器比对新旧值后决定是否重启 worker。
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    Private Sub MCB_后端_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_后端.SelectedIndexChanged
        If 正在初始化 OrElse MCB_后端.SelectedIndex < 0 Then Return
        设置.实例对象.Ai字幕推理后端 = 后端标识(MCB_后端.SelectedIndex)
        刷新状态()
        ' 换后端 = 换运行时 / 换算子落点，同样只在启动期生效。
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    ' ---------------- 后端下拉（随设备变） ----------------
    '
    ' 项来自 Ai字幕推理档.设备后端，**不写死** —— 写死就成了第二份真值表，
    ' 症状是「下拉里有一项、代码不认」（L0 门禁钉住这条）。
    Private _后端表 As String() = {Ai字幕推理档.后端自动}

    Private Sub 重建后端下拉()
        Dim 设备 = Ai字幕推理档.规整设备(设置.实例对象.Ai字幕运行设备)
        ' 后端候选表跟着**包的引擎**变：GGUF 包只有「显卡（Vulkan）」一条腿，
        ' ONNX 包才有 DirectML / OpenVINO 那些。取不到包时按 ONNX 处理，
        ' 这样没选包的时候下拉不是空的（状态行那句「未启用」已经说清了）。
        Dim 引擎 = Ai字幕包.引擎Onnx
        Dim 载入错误 As String = String.Empty
        Dim 当前包 = Ai字幕包.载入(MTB_包目录.Text.Trim(), 载入错误)
        If 当前包 IsNot Nothing Then 引擎 = 当前包.引擎
        Dim 列表 = Ai字幕推理档.设备后端(设备, 引擎)
        Dim 选中 = Ai字幕推理档.规整后端(设备, 设置.实例对象.Ai字幕推理后端, 引擎)
        ' 归位只改内存不落盘：用户一动下拉就会把正确值写回去（语言残留值同一套路）。
        ' 但不归位就是「界面显示 CPU、worker 却收到 dml」，绝不能省。
        设置.实例对象.Ai字幕推理后端 = 选中
        _后端表 = 列表
        Dim 外层初始化 = 正在初始化
        正在初始化 = True
        Try
            MCB_后端.Items.Clear()
            ' 项提示按项文本建索引，项清了提示也要清 —— 不然后端换几次之后
            ' 字典里堆着上一次设备的项，纯属泄漏。
            MCB_后端.ItemToolTips.Clear()
            If 列表.Length = 1 Then
                ' 没得选（auto 设备 / AMD 只有 DirectML / NPU 只有 OpenVINO）：
                ' 置灰并把原因写进项文字。水印在 SelectedIndex ≥ 0 时看不见，
                ' 靠水印说话用户只看到一个灰下拉、不知道为什么。
                Dim 单项 = If(设备 = Ai字幕推理档.设备自动,
                              "自动（不用选，交给能力探测）",
                              Ai字幕推理档.后端显示名(列表(0), 引擎) & "（唯一可选）")
                MCB_后端.Items.Add(单项)
                MCB_后端.ItemToolTips.Add(单项, Ai字幕推理档.后端说明(列表(0), 引擎))
                MCB_后端.SelectedIndex = 0
                MCB_后端.Enabled = False
            Else
                For Each id In 列表
                    ' 项提示＝这个后端把哪一段放到加速器上 + 验证到哪一步。两件事
                    ' 状态行也写，但只写当前选中那条；没选中时用户正是靠悬浮比较。
                    Dim 项 = Ai字幕推理档.后端显示名(id, 引擎)
                    MCB_后端.Items.Add(项)
                    MCB_后端.ItemToolTips.Add(项, Ai字幕推理档.后端说明(id, 引擎) & " ｜ " &
                                               Ai字幕推理档.后端状态(id, 引擎))
                Next
                MCB_后端.SelectedIndex = 后端下标(列表, 选中)
                MCB_后端.Enabled = True
            End If
        Finally
            正在初始化 = 外层初始化
        End Try
    End Sub

    Private Shared Function 后端下标(列表 As String(), 值 As String) As Integer
        For i = 0 To 列表.Length - 1
            If String.Equals(列表(i), 值, StringComparison.Ordinal) Then Return i
        Next
        Return 0
    End Function

    Private Function 后端标识(下标 As Integer) As String
        If 下标 < 0 OrElse 下标 >= _后端表.Length Then Return _后端表(0)
        Return _后端表(下标)
    End Function

    Private Shared Function 设备标识(下标 As Integer) As String
        If 下标 < 0 OrElse 下标 >= Ai字幕推理档.设备全部.Length Then Return Ai字幕推理档.设备自动
        Return Ai字幕推理档.设备全部(下标)
    End Function

    Private Shared Function 设备下标(值 As String) As Integer
        Dim id = Ai字幕推理档.规整设备(值)
        For i = 0 To Ai字幕推理档.设备全部.Length - 1
            If String.Equals(Ai字幕推理档.设备全部(i), id, StringComparison.Ordinal) Then Return i
        Next
        Return 0
    End Function

    ' ---------------- 语言 ----------------

    Private Sub MCB_语言_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_语言.SelectedIndexChanged
        If 正在初始化 OrElse MCB_语言.SelectedIndex < 0 Then Return
        设置.实例对象.Ai字幕语言 = 语言标识(MCB_语言.SelectedIndex)
        ' 与菜单同一条规则：语言项的文案是结果句（"只显示中文"），选定即开闸、
        ' 选回自动即关闸，复选框跟着走。用户之后仍可手动改复选框——那是显式选择。
        If Ai字幕语言.已指定(设置.实例对象.Ai字幕语言) Then
            设置.实例对象.Ai字幕仅所选语言 = True
            正在初始化 = True
            Try
                MCK_仅所选语言.Checked = True
            Finally
                正在初始化 = False
            End Try
        End If
        刷新语言可用性()
        刷新状态()
        ' 语言是 worker 的启动期参数，改它必须重启管道；控制器自己比对后端口语决定。
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    Private Sub MCK_仅所选语言_CheckedChanged(sender As Object, e As EventArgs) Handles MCK_仅所选语言.CheckedChanged
        If 正在初始化 Then Return
        ' 没指定语言时这个开关没有语义，勾上也归零（语言下拉改回 auto 时同理）。
        设置.实例对象.Ai字幕仅所选语言 =
            MCK_仅所选语言.Checked AndAlso Ai字幕语言.已指定(设置.实例对象.Ai字幕语言)
        刷新状态()
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    ''' <summary>"只显示所选语言"没有 auto 可言：没指定语言时置灰并归零。
    ''' 归零必须写设置——否则界面上框是空的、worker 却在按旧值筛除。</summary>
    Private Sub 刷新语言可用性()
        Dim 已指定 = Ai字幕语言.已指定(设置.实例对象.Ai字幕语言)
        MCK_仅所选语言.Enabled = 已指定
        If Not 已指定 AndAlso MCK_仅所选语言.Checked Then
            正在初始化 = True
            Try
                MCK_仅所选语言.Checked = False
            Finally
                正在初始化 = False
            End Try
            设置.实例对象.Ai字幕仅所选语言 = False
        End If
    End Sub

    ' ---------------- 节奏 / 显示 ----------------

    Private Sub MTB_窗口秒_Leave(sender As Object, e As EventArgs) Handles MTB_窗口秒.Leave
        If 正在初始化 Then Return
        设置.实例对象.Ai字幕窗口秒覆盖 = 解析覆盖(MTB_窗口秒.Text)
        MTB_窗口秒.Text = 覆盖文本(设置.实例对象.Ai字幕窗口秒覆盖)
        同步预置下拉()
        刷新状态()
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    Private Sub MTB_步长秒_Leave(sender As Object, e As EventArgs) Handles MTB_步长秒.Leave
        If 正在初始化 Then Return
        设置.实例对象.Ai字幕步长秒覆盖 = 解析覆盖(MTB_步长秒.Text)
        MTB_步长秒.Text = 覆盖文本(设置.实例对象.Ai字幕步长秒覆盖)
        同步预置下拉()
        刷新状态()
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    ''' <summary>字幕驻留偏移：走 Leave 不 TextChanged，理由同路径框。偏移是纯显示侧
    ''' （控制器同步进状态机，不重启 worker），但照样要过 应用Ai字幕设置。</summary>
    Private Sub MTB_偏移_Leave(sender As Object, e As EventArgs) Handles MTB_偏移.Leave
        If 正在初始化 Then Return
        Dim 值 = 解析偏移(MTB_偏移.Text)
        设置.实例对象.Ai字幕字幕偏移秒 = 值
        MTB_偏移.Text = 偏移文本(值)
        刷新状态()
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    ''' <summary>窗口/步长预置：只是"快速填数值"的写入器——真存储仍是窗口/步长两个
    ''' 覆盖项（0 = 跟随包清单）。选中预置即写设置并回填两个文本框；"自定义"没有
    ''' 自己的数值，选中它什么都不写，它只表示"当前值与任何预置都对不上"。</summary>
    Private Sub MCB_预置_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_预置.SelectedIndexChanged
        If 正在初始化 OrElse MCB_预置.SelectedIndex < 0 Then Return
        If MCB_预置.SelectedIndex >= _预置显示.Length - 1 Then Return   ' 自定义：只反映现状
        设置.实例对象.Ai字幕窗口秒覆盖 = _预置窗口(MCB_预置.SelectedIndex)
        设置.实例对象.Ai字幕步长秒覆盖 = _预置步长(MCB_预置.SelectedIndex)
        MTB_窗口秒.Text = 覆盖文本(设置.实例对象.Ai字幕窗口秒覆盖)
        MTB_步长秒.Text = 覆盖文本(设置.实例对象.Ai字幕步长秒覆盖)
        刷新状态()
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    ''' <summary>推理线程数：启动期参数，worker 只在进程启动时拿一次 --threads，
    ''' 改它必须重启 worker（控制器比对新旧值）。</summary>
    Private Sub MCB_线程_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_线程.SelectedIndexChanged
        If 正在初始化 OrElse MCB_线程.SelectedIndex < 0 Then Return
        Dim 下标 = MCB_线程.SelectedIndex
        设置.实例对象.Ai字幕线程数 = If(下标 < _线程值.Length, _线程值(下标), 0)
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    ''' <summary>设置值 → 下拉下标。未知值（老设置里的 3、16）落「自动」，与 0 同义。</summary>
    Private Sub 同步线程下拉()
        Dim 值 = 设置.实例对象.Ai字幕线程数
        Dim 下标 = 0
        For i = 0 To _线程值.Length - 1
            If _线程值(i) = 值 Then 下标 = i
        Next
        Dim 外层初始化 = 正在初始化
        正在初始化 = True
        Try
            MCB_线程.SelectedIndex = 下标
        Finally
            正在初始化 = 外层初始化
        End Try
    End Sub

    Private Sub MCK_显示高级_CheckedChanged(sender As Object, e As EventArgs) Handles MCK_显示高级.CheckedChanged
        If 正在初始化 Then Return
        显示高级(MCK_显示高级.Checked)
    End Sub

    Private Sub 显示高级(显示 As Boolean)
        LBL_窗口秒.Visible = 显示
        MTB_窗口秒.Visible = 显示
        LBL_步长秒.Visible = 显示
        MTB_步长秒.Visible = 显示
        LBL_模型档.Visible = 显示
        切换模型控件(显示)
        LBL_偏移.Visible = 显示
        MTB_偏移.Visible = 显示
        LBL_线程.Visible = 显示
        MCB_线程.Visible = 显示
        LBL_预置.Visible = 显示
        MCB_预置.Visible = 显示
    End Sub

    ' ---------------- 状态行 ----------------

    ''' <summary>状态行：包是否有效、实际生效的窗口/步长、设备+后端、模型档、语言，
    ''' 外加「这个后端把哪一段放到加速器上」和「验证到哪一步」。
    ''' 只描述"将会怎么跑"，不再有"包内有没有该后端二进制"这类问题 —— 已经没有
    ''' 二进制可分，跑不起来就是 worker 那边加载失败并降档。</summary>
    Private Sub 刷新状态()
        Dim 目录 = MTB_包目录.Text.Trim()
        Dim 错误 As String = String.Empty
        Dim 配置 = Ai字幕包.载入(目录, 错误)
        If 配置 Is Nothing Then
            刷新模型档(Nothing)
            If String.IsNullOrWhiteSpace(目录) Then
                LBL_状态.ForeColor = Color.Gray
                LBL_状态.Text = "未启用。选择一个扩展包目录（含 ai-subtitles.json）即可启用。"
            Else
                LBL_状态.ForeColor = Color.OrangeRed
                LBL_状态.Text = "未启用：" & 错误
            End If
            Return
        End If

        Dim 窗口 = 配置.窗口秒
        Dim 步长 = 配置.步长秒
        Dim 覆盖窗口 = 设置.实例对象.Ai字幕窗口秒覆盖
        Dim 覆盖步长 = 设置.实例对象.Ai字幕步长秒覆盖
        If 覆盖窗口 > 0 Then 窗口 = 覆盖窗口
        If 覆盖步长 > 0 Then 步长 = Math.Min(覆盖步长, 窗口)

        刷新模型档(配置)

        Dim 设备 = Ai字幕推理档.规整设备(设置.实例对象.Ai字幕运行设备)
        Dim 后端 = Ai字幕推理档.解析(设备, 设置.实例对象.Ai字幕推理后端, 配置.引擎)
        Dim 语言id = Ai字幕语言.规整(设置.实例对象.Ai字幕语言)
        Dim 语言说明 = Ai字幕语言.显示名(语言id)
        ' 第二行要一起放下设备 / 后端 / 模型档，语言只留短名 + 闸门标记；
        ' 长句留给最后一行的注意项。闸门关着时显示名那句「只显示…」不成立，
        ' 必须换成短名 +（不筛选）——状态行说谎比没有状态行更糟。
        ' （"（提示，不筛除）"也一并废除：worker 已不接受语言提示，那是旧文案。）
        If Ai字幕语言.已指定(语言id) AndAlso Not 设置.实例对象.Ai字幕仅所选语言 Then
            语言说明 = Ai字幕语言.短名(语言id) & "（不筛选）"
        End If
        ' 第二行报的是「实际在跑什么」。单档时直接用形态（int8），不写「默认档」——
        ' 那是个位置名词，用户看不出这包跑的是什么精度。
        Dim 档名 = If(String.IsNullOrEmpty(配置.默认档形态), 默认档显示, 配置.默认档形态)
        If _有多档 AndAlso Not String.IsNullOrWhiteSpace(设置.实例对象.Ai字幕模型档) Then
            Dim 档命中 As Boolean
            Dim 档 = 配置.取模型档(设置.实例对象.Ai字幕模型档, 档命中)
            If 档命中 Then 档名 = 档.档名
        End If

        LBL_状态.ForeColor = Color.OliveDrab
        Dim 后缀 = If(覆盖窗口 > 0 OrElse 覆盖步长 > 0, "（高级覆盖生效）", "")
        ' 五行封顶（Label 是 568x112）：包/节奏、设备+后端+模型、后端做什么、
        ' 验证到哪一步、必须现在说清的注意事项。每行都压在一行内，越界会换行顶掉下一行。
        LBL_状态.Text = $"已启用：{配置.名称} —— 窗口 {窗口:0.###}s / 步长 {步长:0.###}s{后缀}" & vbCrLf &
                        $"设备：{Ai字幕推理档.设备显示名(设备)}　后端：{Ai字幕推理档.后端显示名(后端, 配置.引擎)}　模型：{档名}" & vbCrLf &
                        $"说明：{Ai字幕推理档.后端说明(后端, 配置.引擎)}" & vbCrLf &
                        $"状态：{Ai字幕推理档.后端状态(后端, 配置.引擎)}"
        ' 第五行只放「现在就影响结果」的事，且最多一条：worker 还没接这个后端时，
        ' 它比语言闸门重要得多 —— 前者决定跑在哪，后者只是不筛。说不清就是让用户
        ' 以为选了什么就在跑什么。
        If Not Ai字幕推理档.当前worker可用(后端, 配置.引擎) Then
            LBL_状态.ForeColor = Color.Orange
            LBL_状态.Text &= vbCrLf & "注意：worker 尚未接入该后端（本版只认 CPU / CUDA），会按自动处理。"
        ElseIf 设置.实例对象.Ai字幕仅所选语言 Then
            LBL_状态.Text &= vbCrLf & "注意：当前运行时不返回语言标签，只显示所选语言实际不筛除。"
        End If
    End Sub

    ' ---------------- 模型 ----------------

    ' 本版只有一个模型：Qwen3-ASR 0.6B · int8。**定了的事不摆成选项** ——
    ' 一个永远只有一项的下拉，哪怕置灰，也在暗示「还能选别的」，用户就得去试。
    ' 所以单档时这一格只报事实（形态 · 体积），只有包作者真并排放了第二套权重
    ' （清单 models，PACK_FORMAT §2.4）时，下拉才出现 —— 那时是包要求选，不是
    ' 我们在造维度。
    Private Const 默认档显示 As String = "默认档"
    Private _档名表 As New List(Of String) From {String.Empty}
    Private _档表签名 As String = String.Empty
    Private _有多档 As Boolean = False

    ''' <summary>默认档的「形态 · 体积」，形如 int8 · 941 MB。缺什么就不带什么。</summary>
    Private Shared Function 默认档提示(配置 As Ai字幕包配置) As String
        If 配置 Is Nothing Then Return String.Empty
        Dim 部分 As New List(Of String)
        If Not String.IsNullOrEmpty(配置.默认档形态) Then 部分.Add(配置.默认档形态)
        Dim 体 = Ai字幕包.体积文本(配置.默认档体积)
        If 体 <> "" Then 部分.Add(体)
        If 部分.Count = 0 Then Return String.Empty
        Return String.Join(" · ", 部分)
    End Function

    ''' <summary>一档的项文字。档名与形态重复时只留一个（官方包档名通常就叫 int8），
    ''' 否则会出现「int8（int8 · 941 MB）」这种自我重复。</summary>
    Private Shared Function 档项文字(档 As Ai字幕模型档) As String
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

    ''' <summary>项提示：体积 + 目录。目录是排查「这档到底指到哪」的唯一线索。</summary>
    Private Shared Function 档提示(档 As Ai字幕模型档) As String
        Dim 体 = Ai字幕包.体积文本(档.体积字节)
        If 体 = "" Then Return 档.模型目录
        Return $"{体}　{档.模型目录}"
    End Function

    ''' <summary>刷新模型一格。**单档时不给下拉**，只写一行事实；只有包清单真放了
    ''' 第二套权重时才出现下拉。列表没变就不重建 —— 否则每次刷新状态都会清空，
    ''' 用户正打开的下拉会被抽走。</summary>
    Private Sub 刷新模型档(配置 As Ai字幕包配置)
        Dim 新 As New List(Of String) From {String.Empty}
        If 配置 IsNot Nothing Then
            For Each 档 In 配置.模型档表
                新.Add(档.档名)
            Next
        End If
        ' 有没有第二档，由表长度判定，不靠档名猜。
        _有多档 = 新.Count > 1

        ' 签名带上「有没有包」与「形态 · 体积」：前者区分「有包无档」和「没包」
        ' （两者文字理由不同）；后者让同一个档名换了包（体积不同）时也重建。
        Dim 签名 = If(配置 Is Nothing, "无包", "有包") & String.Join("|", 新) &
                   "|" & 默认档提示(配置)
        If 签名 <> _档表签名 Then
            _档表签名 = 签名
            _档名表 = 新

            Dim 选中 = If(设置.实例对象.Ai字幕模型档, String.Empty)
            Dim 下标 = 0
            For i = 1 To 新.Count - 1
                If String.Equals(新(i), 选中, StringComparison.OrdinalIgnoreCase) Then 下标 = i
            Next

            正在初始化 = True
            Try
                MCB_模型档.Items.Clear()
                MCB_模型档.ItemToolTips.Clear()
                If _有多档 Then
                    MCB_模型档.Items.Add(默认档显示)
                    MCB_模型档.ItemToolTips.Add(默认档显示, 默认档提示(配置))
                    For i = 1 To 新.Count - 1
                        Dim 项 = 档项文字(配置.模型档表(i - 1))
                        MCB_模型档.Items.Add(项)
                        MCB_模型档.ItemToolTips.Add(项, 默认档提示(配置) & "　vs　" & 档提示(配置.模型档表(i - 1)))
                    Next
                    MCB_模型档.SelectedIndex = 下标
                Else
                    ' 单档分支这里只占位：下拉整个不显示，见下方 切换模型控件。
                    MCB_模型档.Items.Add(默认档显示)
                    MCB_模型档.SelectedIndex = 0
                End If
                MCB_模型档.WaterText = "模型档位"
            Finally
                正在初始化 = False
            End Try
        End If

        ' 事实行。没包时也要给理由 —— 空白格子会被当成「还在加载」。
        If 配置 Is Nothing Then
            LBL_模型档值.Text = "未选择扩展包"
            LBL_模型档值.ForeColor = Color.DarkOrange
        Else
            Dim 详 = 默认档提示(配置)
            LBL_模型档值.Text = If(详 = "", "未标注量化形态", 详)
            LBL_模型档值.ForeColor = Color.Gainsboro
        End If
        切换模型控件(MCK_显示高级.Checked)
    End Sub

    ''' <summary>下拉与只读行互斥：**只有包里有第二档时下拉才存在**。单档时
    ''' 这个位置上写的是事实，不是一个「点了也没别的」的控件。</summary>
    Private Sub 切换模型控件(显示 As Boolean)
        MCB_模型档.Visible = 显示 AndAlso _有多档
        LBL_模型档值.Visible = 显示 AndAlso Not _有多档
    End Sub

    Private Sub MCB_模型档_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_模型档.SelectedIndexChanged
        If 正在初始化 Then Return
        Dim 下标 = MCB_模型档.SelectedIndex
        If 下标 < 0 Then Return
        设置.实例对象.Ai字幕模型档 =
            If(下标 < _档名表.Count, _档名表(下标), String.Empty)
        ' 档位是启动期参数：控制器会比对新旧档名并重启 worker。
        Form1.当前主窗体?.应用Ai字幕设置()
        刷新状态()
    End Sub

    ' ---------------- 取值助手 ----------------

    ''' <summary>语言下拉下标 ← → 标识。项按 Ai字幕语言.主项 的顺序填（只 4 项）。</summary>
    Private Shared Function 语言下标(值 As String) As Integer
        Dim id = Ai字幕语言.规整(值)
        For i = 0 To Ai字幕语言.主项.Length - 1
            If String.Equals(Ai字幕语言.主项(i), id, StringComparison.Ordinal) Then Return i
        Next
        Return 0
    End Function

    Private Shared Function 语言标识(下标 As Integer) As String
        If 下标 < 0 OrElse 下标 >= Ai字幕语言.主项.Length Then Return Ai字幕语言.自动
        Return Ai字幕语言.主项(下标)
    End Function

    ''' <summary>字幕驻留偏移：留空或非法 = 0；范围钳 [-3, +3] 秒（设置项同区间，双保险）。</summary>
    Private Shared Function 解析偏移(文本 As String) As Double
        Dim 值 As Double
        If Double.TryParse(文本, 值) Then Return Math.Clamp(值, -3.0, 3.0)
        Return 0
    End Function

    Private Shared Function 偏移文本(值 As Double) As String
        Return 值.ToString("0.###", Globalization.CultureInfo.InvariantCulture)
    End Function

    ''' <summary>按当前窗口/步长覆盖值同步预置下拉：两个都是 0 = 跟随包清单；与某项
    ''' 预置完全相等 = 选中它；都对不上 = 自定义（手填过）。</summary>
    Private Sub 同步预置下拉()
        Dim 窗口 = 设置.实例对象.Ai字幕窗口秒覆盖
        Dim 步长 = 设置.实例对象.Ai字幕步长秒覆盖
        Dim 下标 = _预置显示.Length - 1   ' 默认落"自定义"
        If 窗口 <= 0 AndAlso 步长 <= 0 Then
            下标 = 0
        Else
            For i = 1 To _预置显示.Length - 2
                If _预置窗口(i) = 窗口 AndAlso _预置步长(i) = 步长 Then
                    下标 = i
                    Exit For
                End If
            Next
        End If
        Dim 外层初始化 = 正在初始化
        正在初始化 = True
        Try
            MCB_预置.SelectedIndex = 下标
        Finally
            正在初始化 = 外层初始化
        End Try
    End Sub

    Private Shared Function 解析覆盖(文本 As String) As Double
        Dim 值 As Double
        If Double.TryParse(文本, 值) AndAlso 值 >= 0.5 AndAlso 值 <= 30 Then Return 值
        Return 0   ' 0 = 跟随包清单（留空或非法值都归到这里）
    End Function

    Private Shared Function 覆盖文本(值 As Double) As String
        Return If(值 > 0, 值.ToString("0.###"), String.Empty)
    End Function

    ' ---------------- 布局 ----------------

    ' 这一页设计高度 644 逻辑像素，而设置窗口的客户区只有 461（Form设置.Designer
    ' 的 ClientSize）—— 装不下，底部 183px 原本被直接裁掉，模型 / 偏移 / 线程 /
    ' 预置 四格在 150% DPI 下完全看不见。两处配合才修好：
    '   * 窗体 AutoScroll = True（Designer）
    '   * 面板不用 Dock —— Dock 的子控件不参与 AutoScroll 滚动，会被钉在原处，
    '     滚动条于是只是空转。这里改成绝对定位 + 高度固定，宽度跟随容器。
    Private Sub 对齐面板宽度()
        Dim 宽 = Math.Max(598, ClientSize.Width)
        If Math.Abs(ModernPanel1.Width - 宽) > 2 Then ModernPanel1.Width = 宽
    End Sub

    Private Sub Form设置_AI字幕_Resize(sender As Object, e As EventArgs) Handles MyBase.Resize
        对齐面板宽度()
    End Sub
End Class
