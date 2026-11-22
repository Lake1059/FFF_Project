Imports System.Collections.Generic
Imports System.Drawing
Imports System.Globalization
Imports System.IO
Imports System.Threading
Imports System.Threading.Tasks
Imports System.Windows.Forms

''' <summary>管理 AI 字幕扩展包、推理选项和识别语言。</summary>
Friend Class Form设置_AI字幕
    Private 正在初始化 As Boolean = True
    Private _包操作中 As Boolean
    Private _已初始化下载栏 As Boolean
    Private _目录已编辑 As Boolean
    Private _加载的包设置 As String = String.Empty
    Private ReadOnly _已选目录(1) As String

    ' 与设计器中的节奏预置一一对应，首项和末项分别为跟随清单和自定义。
    Private Shared ReadOnly _预置窗口 As Double() = {0, 8, 6, 8, 4, 0}
    Private Shared ReadOnly _预置步长 As Double() = {0, 6, 4, 5, 2, 0}
    ' 与设计器中的线程档位顺序一致，0 表示交由 worker 决定。
    Private Shared ReadOnly _线程值 As Integer() = {0, 4, 6, 8, 12}

    Public Sub New()
        InitializeComponent()
        正在初始化 = False
    End Sub

    Friend Sub 初始化页面()
        正在初始化 = True
        Try
            If Not _下载中 AndAlso Not _包操作中 AndAlso (Not _已初始化下载栏 OrElse
                Not String.Equals(_加载的包设置, 设置.实例对象.Ai字幕包目录, StringComparison.OrdinalIgnoreCase)) Then
                _已初始化下载栏 = True
                MP_下载进度条的容器.Visible = False
                _加载的包设置 = 设置.实例对象.Ai字幕包目录
                Dim 已选包 = Ai字幕包.载入(_加载的包设置, String.Empty)
                MCB_选择推理包.SelectedIndex = If(已选包?.引擎 = Ai字幕包.引擎Onnx, 1, 0)
                Dim 目录 = If(已选包?.包目录, If(String.IsNullOrWhiteSpace(_加载的包设置),
                    默认包目录(当前包类型()), _加载的包设置))
                设置包目录文本(目录)
                _已选目录(CInt(当前包类型())) = 目录
            End If
            MCB_推理设备.SelectedIndex = 设备下标(设置.实例对象.Ai字幕运行设备)
            ' 残留的非主项语言值（老设置/手改 json，如 "ja"）在这里一次性归 auto：
            ' 只改内存、不主动落盘，用户一动语言就会把主项值写回去。
            Dim 旧语言 = 设置.实例对象.Ai字幕语言
            If Not String.IsNullOrEmpty(旧语言) AndAlso Ai字幕语言.规整(旧语言) <> 旧语言 Then
                设置.实例对象.Ai字幕语言 = Ai字幕语言.规整(旧语言)
            End If
            MCB_字幕语言.SelectedIndex = 语言下标(设置.实例对象.Ai字幕语言)
            MCB_自动翻译为中文.SelectedIndex = Ai字幕双语.下标(设置.实例对象.Ai字幕双语)
            MTB_窗口秒.Text = 覆盖文本(设置.实例对象.Ai字幕窗口秒覆盖)
            MTB_步长秒.Text = 覆盖文本(设置.实例对象.Ai字幕步长秒覆盖)
            MTB_偏移.Text = 偏移文本(设置.实例对象.Ai字幕字幕偏移秒)
            同步预置下拉()
            同步线程下拉()
            更新包选项(提示目录错误:=Not _下载中)
            调整列宽()
        Finally
            正在初始化 = False
        End Try
    End Sub

    ' ---------------- 包 ----------------

    Private Sub 设置包目录文本(目录 As String)
        MTB_包目录.Text = 目录
        _目录已编辑 = False
    End Sub

    Private Sub MTB_包目录_TextChanged(sender As Object, e As EventArgs) Handles MTB_包目录.TextChanged
        If Not 正在初始化 Then _目录已编辑 = True
    End Sub

    Private Sub MTB_包目录_Leave(sender As Object, e As EventArgs) Handles MTB_包目录.Leave
        If 正在初始化 OrElse _下载中 OrElse _包操作中 OrElse Not _目录已编辑 Then Return
        _目录已编辑 = False
        Dim 目录 = MTB_包目录.Text.Trim()
        If String.Equals(目录, 设置.实例对象.Ai字幕包目录, StringComparison.Ordinal) Then Return
        MP_下载进度条的容器.Visible = False
        同步包类型(目录)
        选用包目录(目录)
        更新包选项(提示目录错误:=True)
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
                MP_下载进度条的容器.Visible = False
                设置包目录文本(目录)
                同步包类型(目录)
                选用包目录(目录)
                更新包选项(提示目录错误:=True)
                Form1.当前主窗体?.应用Ai字幕设置()
            End Using
        Catch
            ' 浏览失败不该打断设置页。
        End Try
    End Sub

    ' ---------------- 包的获取与移除 ----------------

    Private _下载中 As Boolean
    Private _下载取消 As CancellationTokenSource

    ''' <summary>包类型下拉下标 → 下载器枚举。越界落 GPU+CPU（默认）。</summary>
    Private Function 当前包类型() As Ai字幕包下载器.包类型
        If MCB_选择推理包.SelectedIndex = 1 Then Return Ai字幕包下载器.包类型.Npu
        Return Ai字幕包下载器.包类型.GpuCpu
    End Function

    Private Sub 选用包目录(目录 As String)
        设置.实例对象.Ai字幕包目录 = 目录
        _加载的包设置 = 目录
        _已选目录(CInt(当前包类型())) = 目录
    End Sub

    Private Shared Function 默认包目录(类型 As Ai字幕包下载器.包类型) As String
        Return Path.Combine(AppContext.BaseDirectory, If(类型 = Ai字幕包下载器.包类型.Npu,
                            "3FP-AI-Subtitles-NPU", "3FP-AI-Subtitles"))
    End Function

    Private Sub 同步包类型(目录 As String)
        If String.IsNullOrWhiteSpace(目录) Then Return
        Dim 包 = Ai字幕包.载入(目录, String.Empty)
        If 包 Is Nothing Then Return
        正在初始化 = True
        Try
            MCB_选择推理包.SelectedIndex = If(包.引擎 = Ai字幕包.引擎Onnx, 1, 0)
        Finally
            正在初始化 = False
        End Try
    End Sub

    Private Function 下载目标目录() As String
        Dim 填 = MTB_包目录.Text.Trim()
        Return If(String.IsNullOrWhiteSpace(填), 默认包目录(当前包类型()), 填)
    End Function

    Private Function 选中目录包(目录 As String) As Ai字幕包配置
        Dim 错误 = String.Empty
        Dim 配置 = Ai字幕包.载入(目录, 错误)
        Dim 引擎 = If(当前包类型() = Ai字幕包下载器.包类型.Npu, Ai字幕包.引擎Onnx, Ai字幕包.引擎Llama)
        Return If(配置?.引擎 = 引擎, 配置, Nothing)
    End Function

    Private Shared Function 包已就绪(包 As Ai字幕包配置) As Boolean
        If 包 Is Nothing Then Return False
        ' 外部包可使用系统 Python；官方包则必须完成其自带运行时的安装。
        Return Not Ai字幕包.有归属哨兵(包.包目录) OrElse
            (File.Exists(包.Python) AndAlso File.Exists(Path.Combine(Path.GetDirectoryName(包.Python),
                "Lib", "site-packages", "numpy", "__init__.py")))
    End Function

    Private Sub 刷新包按钮()
        Dim 目录 = 下载目标目录()
        Dim 包 = 选中目录包(目录)
        Dim 已下载 = 包已就绪(包)
        Dim 显示下载 = Not _下载中 AndAlso Not _包操作中 AndAlso Not 已下载
        Dim 显示删除 = Not _下载中 AndAlso Not _包操作中 AndAlso 已下载 AndAlso
            Ai字幕包.有指定类型归属哨兵(目录, 当前包类型().ToString())
        MB_下载包.Visible = 显示下载
        JEC_下载间距.Visible = 显示下载
        MB_删除选中包.Visible = 显示删除
        JEC_删除间距.Visible = 显示删除
    End Sub

    Private Sub MB_下载包_Click(sender As Object, e As EventArgs) Handles MB_下载包.Click
        If _下载中 OrElse _包操作中 Then Return
        Dim 目录 = 下载目标目录()
        Dim 类型 = 当前包类型()
        If 包已就绪(选中目录包(目录)) Then
            刷新包按钮()
            Return
        End If
        ' 下载前把“包含哪些第三方组件、音频是否上传、AI 字幕是否标注”说清楚，
        ' 而不是等下载完再让用户去包目录里找声明。
        Dim 确认 = MessageBox.Show(
            "本包会从公开官方源下载 AI 字幕所需的第三方模型/运行时：" & vbCrLf &
            "Qwen3-ASR、llama.cpp、sherpa-onnx、OpenVINO、Python、NumPy、ONNX Runtime。" & vbCrLf & vbCrLf &
            "许可与来源见下载后的 AI字幕包声明.txt 与 licenses\ai\；音频只在本机处理，不上传；" & vbCrLf &
            "AI 生成字幕可能不准确，仅供参考。" & vbCrLf & vbCrLf &
            "继续下载？",
            "AI 字幕包声明",
            MessageBoxButtons.OKCancel,
            MessageBoxIcon.Information,
            MessageBoxDefaultButton.Button2)
        If 确认 <> DialogResult.OK Then Return
        开始下载(目录, 类型)
    End Sub

    ''' <summary>下载并选用扩展包，在进度条内显示百分比和当前阶段。</summary>
    Private Async Sub 开始下载(目录 As String, 类型 As Ai字幕包下载器.包类型)
        _下载中 = True
        Dim 取消 As New CancellationTokenSource()
        _下载取消 = 取消
        设置下载中(True)
        Try
            Dim 进度 = New Progress(Of Ai字幕包下载器.下载进度)(
                Sub(p)
                    If Not ReferenceEquals(_下载取消, 取消) OrElse 取消.IsCancellationRequested OrElse IsDisposed Then Return
                    更新下载进度(Math.Clamp(p.总百分比, 0, 99), p.文本, p.速度字节每秒)
                End Sub)
            Dim 错误 = Await Ai字幕包下载器.下载包Async(目录, 类型, 进度, 取消.Token)
            If IsDisposed Then Return
            If Not String.IsNullOrEmpty(错误) Then
                更新下载进度(EPB_下载.Value, "下载失败：" & 错误)
                Return
            End If
            设置包目录文本(目录)
            选用包目录(目录)
            更新下载进度(100, "下载完成")
            更新包选项()
            Form1.当前主窗体?.应用Ai字幕设置()
        Catch ex As OperationCanceledException When 取消.IsCancellationRequested
            If Not IsDisposed Then 更新下载进度(EPB_下载.Value, "已取消，可重新下载以继续")
        Catch ex As Exception
            If Not IsDisposed Then 更新下载进度(EPB_下载.Value, "下载失败：" & ex.Message)
        Finally
            _下载中 = False
            _下载取消 = Nothing
            取消.Dispose()
            If Not IsDisposed Then 设置下载中(False)
        End Try
    End Sub

    Private Async Sub MB_删除包_Click(sender As Object, e As EventArgs) Handles MB_删除选中包.Click
        If _下载中 OrElse _包操作中 Then Return
        Dim 选中 = Ai字幕包.载入(下载目标目录(), String.Empty)
        If 选中 Is Nothing Then
            刷新包按钮()
            Return
        End If
        Dim 目录 = Path.GetFullPath(选中.包目录)
        Dim 类型名 = 当前包类型().ToString()
        Dim 配置 = 选中目录包(目录)
        If Not 包已就绪(配置) OrElse Not Ai字幕包.有指定类型归属哨兵(目录, 类型名) Then
            刷新包按钮()
            Return
        End If
        If LakeUI.ExOverlayMsgBox(Me,
            $"确定删除选中的 AI 字幕扩展包？{vbCrLf}{配置.名称}{vbCrLf}{目录}",
            {"删除", "取消"}, "删除 AI 字幕扩展包", MsgBoxStyle.Exclamation, 1) <> 0 Then Return

        Dim 设置对象 = 设置.实例对象
        Dim 当前 = Ai字幕包.载入(设置对象.Ai字幕包目录, String.Empty)
        Dim 删除的是当前包 = 当前 IsNot Nothing AndAlso
            String.Equals(Path.GetFullPath(当前.包目录), 目录, StringComparison.OrdinalIgnoreCase)
        _包操作中 = True
        设置下载中(True)
        Dim 原包目录 = 设置对象.Ai字幕包目录
        If 删除的是当前包 Then
            设置对象.Ai字幕包目录 = ""
            _加载的包设置 = ""
            Form1.当前主窗体?.应用Ai字幕设置()
        End If
        Try
            Await Task.Run(Sub() Ai字幕包.删除指定下载包(目录, 类型名))
            If Not IsDisposed Then 显示包操作状态("已删除选中的扩展包。")
        Catch ex As Exception
            If 删除的是当前包 AndAlso String.IsNullOrEmpty(设置对象.Ai字幕包目录) Then 选用包目录(原包目录)
            If Not IsDisposed Then 显示包操作状态("删除失败：" & ex.Message)
        Finally
            _包操作中 = False
            If Not IsDisposed Then
                设置下载中(False)
                更新包选项()
            End If
            If 删除的是当前包 Then Form1.当前主窗体?.应用Ai字幕设置()
        End Try
    End Sub

    Private Sub 设置下载中(操作中 As Boolean)
        MB_浏览.Enabled = Not 操作中
        MCB_选择推理包.Enabled = Not 操作中
        MTB_包目录.ReadOnly = 操作中
        刷新包按钮()
        If _下载中 Then 更新下载进度(0, "正在准备下载…")
    End Sub

    Friend Sub 更新下载进度(百分比 As Integer, 阶段 As String, Optional 速度 As Double = 0)
        EPB_下载.Value = 百分比
        Dim 速率 = If(速度 <= 0, String.Empty, If(速度 >= 1048576,
            $"　{速度 / 1048576:0.0} MB/s", $"　{速度 / 1024:0.0} KB/s"))
        EPB_下载.Text = $"{EPB_下载.Value}%　{阶段}{速率}"
        MP_下载进度条的容器.Visible = True
    End Sub

    Private Sub 显示包操作状态(文案 As String)
        EPB_下载.Value = 0
        EPB_下载.Text = 文案
        MP_下载进度条的容器.Visible = True
    End Sub

    Private Sub Form设置_AI字幕_Disposed(sender As Object, e As EventArgs) Handles Me.Disposed
        _下载取消?.Cancel()
    End Sub

    ' ---------------- 推理档 ----------------

    Private Sub MCB_设备_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_推理设备.SelectedIndexChanged
        If 正在初始化 OrElse MCB_推理设备.SelectedIndex < 0 Then Return
        设置.实例对象.Ai字幕运行设备 = 设备标识(MCB_推理设备.SelectedIndex)
        更新包选项()
        ' 设备 / 后端都是启动期参数：控制器比对新旧值后决定是否重启 worker。
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    Private Sub MCB_后端_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_推理后端.SelectedIndexChanged
        If 正在初始化 OrElse MCB_推理后端.SelectedIndex < 0 Then Return
        设置.实例对象.Ai字幕推理后端 = 后端标识(MCB_推理后端.SelectedIndex)
        ' 换后端 = 换运行时 / 换算子落点，同样只在启动期生效。
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    ' ---------------- 后端下拉（随设备变） ----------------
    '
    ' 项来自 Ai字幕推理档.设备后端，**不写死** —— 写死就成了第二份真值表，
    ' 症状是「下拉里有一项、代码不认」（L0 门禁钉住这条）。
    Private _后端表 As String() = {Ai字幕推理档.后端自动}

    ' 自动设备的初始选项由设计器提供。候选项在设备或包引擎变化时重建。
    Private _后端设备 As String = Ai字幕推理档.设备自动
    Private _后端引擎 As String = Ai字幕包.引擎Onnx

    Private Sub 同步后端下拉(引擎 As String)
        Dim 设备 = Ai字幕推理档.规整设备(设置.实例对象.Ai字幕运行设备)
        Dim 选中 = Ai字幕推理档.规整后端(设备, 设置.实例对象.Ai字幕推理后端, 引擎)
        设置.实例对象.Ai字幕推理后端 = 选中
        Dim 外层初始化 = 正在初始化
        正在初始化 = True
        Try
            If 设备 <> _后端设备 OrElse 引擎 <> _后端引擎 Then
                _后端设备 = 设备
                _后端引擎 = 引擎
                _后端表 = Ai字幕推理档.设备后端(设备, 引擎)
                MCB_推理后端.Items.Clear()
                MCB_推理后端.ItemToolTips.Clear()
                For Each id In _后端表
                    Dim 项 = Ai字幕推理档.后端显示名(id, 引擎)
                    If _后端表.Length = 1 Then
                        项 = If(设备 = Ai字幕推理档.设备自动, "自动（由能力探测）", 项 & "（唯一可选）")
                    End If
                    MCB_推理后端.Items.Add(项)
                    MCB_推理后端.ItemToolTips.Add(项, Ai字幕推理档.后端说明(id, 引擎) & " ｜ " &
                                               Ai字幕推理档.后端状态(id, 引擎))
                Next
            End If
            MCB_推理后端.SelectedIndex = 后端下标(_后端表, 选中)
            MCB_推理后端.Enabled = _后端表.Length > 1
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

    Private Sub MCB_语言_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_字幕语言.SelectedIndexChanged
        If 正在初始化 OrElse MCB_字幕语言.SelectedIndex < 0 Then Return
        设置.实例对象.Ai字幕语言 = 语言标识(MCB_字幕语言.SelectedIndex)
        设置.实例对象.Ai字幕仅所选语言 = Ai字幕语言.已指定(设置.实例对象.Ai字幕语言)
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    Private Sub 翻译_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_自动翻译为中文.SelectedIndexChanged
        If 正在初始化 OrElse MCB_自动翻译为中文.SelectedIndex < 0 Then Return
        设置.实例对象.Ai字幕双语 = Ai字幕双语.全部(MCB_自动翻译为中文.SelectedIndex)
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    ' ---------------- 节奏 / 显示 ----------------

    Private Sub MTB_窗口秒_Leave(sender As Object, e As EventArgs) Handles MTB_窗口秒.Leave
        If 正在初始化 Then Return
        设置.实例对象.Ai字幕窗口秒覆盖 = 解析覆盖(MTB_窗口秒.Text)
        MTB_窗口秒.Text = 覆盖文本(设置.实例对象.Ai字幕窗口秒覆盖)
        同步预置下拉()
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    Private Sub MTB_步长秒_Leave(sender As Object, e As EventArgs) Handles MTB_步长秒.Leave
        If 正在初始化 Then Return
        设置.实例对象.Ai字幕步长秒覆盖 = 解析覆盖(MTB_步长秒.Text)
        MTB_步长秒.Text = 覆盖文本(设置.实例对象.Ai字幕步长秒覆盖)
        同步预置下拉()
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    ''' <summary>字幕驻留偏移：走 Leave 不 TextChanged，理由同路径框。偏移是纯显示侧
    ''' （控制器同步进状态机，不重启 worker），但照样要过 应用Ai字幕设置。</summary>
    Private Sub MTB_偏移_Leave(sender As Object, e As EventArgs) Handles MTB_偏移.Leave
        If 正在初始化 Then Return
        Dim 值 = 解析偏移(MTB_偏移.Text)
        设置.实例对象.Ai字幕字幕偏移秒 = 值
        MTB_偏移.Text = 偏移文本(值)
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    ''' <summary>窗口/步长预置：只是"快速填数值"的写入器——真存储仍是窗口/步长两个
    ''' 覆盖项（0 = 跟随包清单）。选中预置即写设置并回填两个文本框；"自定义"没有
    ''' 自己的数值，选中它什么都不写，它只表示"当前值与任何预置都对不上"。</summary>
    Private Sub MCB_预置_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_节奏预置.SelectedIndexChanged
        If 正在初始化 OrElse MCB_节奏预置.SelectedIndex < 0 Then Return
        If MCB_节奏预置.SelectedIndex >= _预置窗口.Length - 1 Then Return   ' 自定义：只反映现状
        设置.实例对象.Ai字幕窗口秒覆盖 = _预置窗口(MCB_节奏预置.SelectedIndex)
        设置.实例对象.Ai字幕步长秒覆盖 = _预置步长(MCB_节奏预置.SelectedIndex)
        MTB_窗口秒.Text = 覆盖文本(设置.实例对象.Ai字幕窗口秒覆盖)
        MTB_步长秒.Text = 覆盖文本(设置.实例对象.Ai字幕步长秒覆盖)
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    ''' <summary>推理线程数：启动期参数，worker 只在进程启动时拿一次 --threads，
    ''' 改它必须重启 worker（控制器比对新旧值）。</summary>
    Private Sub MCB_线程_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_推理线程.SelectedIndexChanged
        If 正在初始化 OrElse MCB_推理线程.SelectedIndex < 0 Then Return
        Dim 下标 = MCB_推理线程.SelectedIndex
        设置.实例对象.Ai字幕线程数 = If(下标 < _线程值.Length, _线程值(下标), 0)
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    Private Sub MCB_包类型_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_选择推理包.SelectedIndexChanged
        If 正在初始化 OrElse MCB_选择推理包.SelectedIndex < 0 Then Return
        MP_下载进度条的容器.Visible = False
        Dim 类型 = 当前包类型()
        设置包目录文本(If(_已选目录(CInt(类型)), 默认包目录(类型)))
        If 包已就绪(选中目录包(下载目标目录())) Then
            选用包目录(下载目标目录())
            Form1.当前主窗体?.应用Ai字幕设置()
        End If
        更新包选项()
    End Sub

    ''' <summary>设置值 → 下拉下标。未知值（老设置里的 3、16）落「自动」，与 0 同义。</summary>
    Private Sub 同步线程下拉()
        Dim 值 = 设置.实例对象.Ai字幕线程数
        Dim 下标 = 0
        Dim 命中 = False
        For i = 0 To _线程值.Length - 1
            If _线程值(i) = 值 Then
                下标 = i
                命中 = True
            End If
        Next
        ' 落「自动」时必须**同时写回设置**：否则界面说"自动（最多 6）"，
        ' 而 控制器→管道 照旧把这个残留值当 --threads 传给 worker —— 界面与
        ' 实跑不一致，同页语言脏值是写回内存的，这里漏了（P1，2026-11-02 定案）。
        If Not 命中 AndAlso 值 <> 0 Then 设置.实例对象.Ai字幕线程数 = 0
        Dim 外层初始化 = 正在初始化
        正在初始化 = True
        Try
            MCB_推理线程.SelectedIndex = 下标
        Finally
            正在初始化 = 外层初始化
        End Try
    End Sub

    Private Sub 调整列宽() Handles MP_推理.SizeChanged, MP_语言.SizeChanged, MP_高级下拉.SizeChanged, MP_高级时间.SizeChanged
        If MCB_推理设备 Is Nothing OrElse MP_偏移列 Is Nothing Then Return
        Form设置.均分设置列(MP_推理, JEC_推理间距.Width, MCB_推理设备)
        Form设置.均分设置列(MP_语言, JEC_语言间距.Width, MCB_字幕语言)
        Form设置.均分设置列(MP_高级下拉, JEC_高级下拉间距.Width, MCB_节奏预置)
        Form设置.均分设置列(MP_高级时间, JEC_窗口步长间距.Width + JEC_步长偏移间距.Width + JEC_模型档位间距.Width,
            MP_窗口列, MP_步长列, MP_偏移列)
    End Sub

    Private Sub 更新包选项(Optional 提示目录错误 As Boolean = False)
        Dim 目录 = 设置.实例对象.Ai字幕包目录
        Dim 错误 As String = String.Empty
        Dim 配置 = Ai字幕包.载入(目录, 错误)
        同步后端下拉(If(配置?.引擎, Ai字幕包.引擎Onnx))
        刷新模型档(配置)
        MCB_自动翻译为中文.Enabled = 配置?.引擎 = Ai字幕包.引擎Llama
        刷新包按钮()
        If 提示目录错误 AndAlso 配置 Is Nothing AndAlso Not String.IsNullOrWhiteSpace(目录) Then
            显示包操作状态("无法使用扩展包：" & 错误)
        End If
    End Sub

    ' ---------------- 模型 ----------------

    ' 模型档由清单提供。仅有默认档时显示默认项，禁用下拉。
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
    ''' 第二套权重时才出现下拉。列表没变就不重建 —— 否则每次刷新包选项都会清空，
    ''' 用户正打开的下拉会被抽走。</summary>
    Private Sub 刷新模型档(配置 As Ai字幕包配置)
        Dim 档位 = 配置?.模型档表
        _有多档 = 档位 IsNot Nothing AndAlso 档位.Count > 0
        MCB_模型档.Enabled = _有多档
        If 档位 Is Nothing Then 档位 = New List(Of Ai字幕模型档)()
        Dim 默认说明 = 默认档提示(配置)
        Dim 签名 = 默认说明 & "|" & String.Join("|", 档位.Select(Function(档) 档.档名 & ":" & 档提示(档)))
        Dim 外层初始化 = 正在初始化
        正在初始化 = True
        Try
            If 签名 <> _档表签名 Then
                _档表签名 = 签名
                _档名表 = New List(Of String) From {String.Empty}
                While MCB_模型档.Items.Count > 1
                    MCB_模型档.Items.RemoveAt(MCB_模型档.Items.Count - 1)
                End While
                MCB_模型档.ItemToolTips.Clear()
                MCB_模型档.ItemToolTips.Add(默认档显示, 默认说明)
                For Each 档 In 档位
                    _档名表.Add(档.档名)
                    Dim 项 = 档项文字(档)
                    MCB_模型档.Items.Add(项)
                    MCB_模型档.ItemToolTips.Add(项, 档提示(档))
                Next
            End If
            Dim 选中 = _档名表.FindIndex(Function(档名) String.Equals(档名, 设置.实例对象.Ai字幕模型档,
                                                                   StringComparison.OrdinalIgnoreCase))
            MCB_模型档.SelectedIndex = Math.Max(0, 选中)
        Finally
            正在初始化 = 外层初始化
        End Try
    End Sub

    Private Sub MCB_模型档_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_模型档.SelectedIndexChanged
        If 正在初始化 Then Return
        Dim 下标 = MCB_模型档.SelectedIndex
        If 下标 < 0 Then Return
        设置.实例对象.Ai字幕模型档 =
            If(下标 < _档名表.Count, _档名表(下标), String.Empty)
        ' 档位是启动期参数：控制器会比对新旧档名并重启 worker。
        Form1.当前主窗体?.应用Ai字幕设置
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
        Dim 下标 = _预置窗口.Length - 1   ' 默认落"自定义"
        If 窗口 <= 0 AndAlso 步长 <= 0 Then
            下标 = 0
        Else
            For i = 1 To _预置窗口.Length - 2
                If _预置窗口(i) = 窗口 AndAlso _预置步长(i) = 步长 Then
                    下标 = i
                    Exit For
                End If
            Next
        End If
        Dim 外层初始化 = 正在初始化
        正在初始化 = True
        Try
            MCB_节奏预置.SelectedIndex = 下标
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

End Class
