Imports System.IO
Imports System.Runtime.InteropServices
Imports System.Threading

Public Class Form1
    Public Shared Property 当前主窗体 As Form1
    Private Const 跳转秒数 As Integer = 5
    Private Shared ReadOnly 核心文件名称 As String() = {"FFF.Native.dll"}
    Private Shared ReadOnly FFmpeg核心文件前缀 As String() = {
        "avcodec",
        "avfilter",
        "avformat",
        "avutil",
        "swresample",
        "swscale"
    }

    Private 画面控件 As 播放器画面控件
    Private 播放控制器 As 播放器控制器
    Private 界面呈现器 As 播放器界面呈现器
    Private 窗口布局控制器 As 播放器窗口布局控制器
    Private 字幕图层呈现器 As 播放器定时文字图层呈现器
    Private 弹幕图层呈现器 As 播放器定时文字图层呈现器
    Private 歌词图层呈现器 As 播放器歌词呈现器
    Private 信息图层呈现器 As 播放器信息图层呈现器
    Private 剪辑区间控制器 As 播放器剪辑区间控制器
    Private 全屏交互控制器 As 播放器全屏交互控制器
    Private 流选择器 As 播放器流选择器
    Private 按钮图标 As 播放器按钮图标资源
    Private 设置窗口 As Form设置
    Private 当前弹幕路径 As String = String.Empty
    Private 正在关闭 As Boolean
    Private 核心文件检查通过 As Boolean
    Private 核心文件错误说明 As String = String.Empty

    Private Event 方向键快捷键已请求 As KeyEventHandler

    Private Sub Form1_Load(sender As Object, e As EventArgs) Handles MyBase.Load
        当前主窗体 = Me
        LakeUI.GlobalOptions.GlobalTextQuality = LakeUI.GlobalOptions.TextQualityMode.Outline
        ThisIsYourWindow1.Attach(Me)
        KeyPreview = True
        MinimumSize = New Size(875, 500)
        更新弹幕按钮可见性()
        SP加载器.启动时加载()
        设置.启动时加载设置()
        字体控制.更新所有控件字体属性()
        设置.应用SP个性化设置()
        设置.加载SP自定义图标()

        Dim 缺失文件 = 核心文件名称.Where(Function(文件名) Not 可以加载核心文件(文件名)).
            Concat(FFmpeg核心文件前缀.
                Where(Function(前缀) Not 可以加载核心文件(前缀)).
                Select(Function(前缀) $"{前缀}.dll 或 {前缀}-数字.dll")).
            ToArray()
        If 缺失文件.Length > 0 Then
            核心文件错误说明 = String.Join(vbCrLf, {
                "以下播放器核心文件缺失或无法加载：",
                String.Empty,
                String.Join(vbCrLf, 缺失文件.Select(Function(文件名) $"  {文件名}")),
                String.Empty,
                "请将完整且同一份的 Shared FFmpeg 的 DLL 放到程序目录或环境变量后重新启动。"})
            Return
        End If

        按钮图标 = 播放器按钮图标资源.加载()
        配置播放器按钮图标()

        画面控件 = New 播放器画面控件 With {.Dock = DockStyle.Fill}
        MP_DX视频容器.Controls.Add(画面控件)
        AddHandler 画面控件.文件拖入, AddressOf 画面控件_文件拖入

        播放控制器 = New 播放器控制器(
            Function() 画面控件.输出窗口句柄, SynchronizationContext.Current,
            CType(设置.实例对象.解码方式, 解码模式))
        播放控制器.设置SDR峰值亮度(设置.实例对象.HDR映射SDR参考亮度)
        播放控制器.设置HDR峰值亮度(设置.实例对象.HDR峰值亮度)
        剪辑区间控制器 = New 播放器剪辑区间控制器(播放控制器, 画面控件,
            MB_剪辑区间模式, MP_剪辑区间操作容器, P_剪辑区间进度条容器,
            P_剪辑区间按钮容器, MB_传给3FUI)
        更新WASAPI按钮()
        流选择器 = New 播放器流选择器(Me, MP_DX视频容器, MCB_流选择器, 播放控制器)
        界面呈现器 = 创建界面呈现器()
        字幕图层呈现器 = New 播放器定时文字图层呈现器(画面控件,
            AddressOf 播放控制器.安全读取快照, Function() 播放控制器.当前字幕,
            AddressOf 播放控制器.提交定时文字图层, Nothing, Nothing,
            定时文字图层内容.仅字幕)
        弹幕图层呈现器 = New 播放器定时文字图层呈现器(画面控件,
            AddressOf 播放控制器.安全读取快照, Function() Nothing,
            AddressOf 播放控制器.提交弹幕图层,
            Function() If(设置.实例对象.弹幕已启用, 播放控制器.当前弹幕, Nothing),
            设置.实例对象.创建弹幕显示配置(), 定时文字图层内容.仅弹幕)
        歌词图层呈现器 = New 播放器歌词呈现器(画面控件,
            AddressOf 播放控制器.安全读取快照,
            Function() If(设置.实例对象.启用歌词支持, 播放控制器.当前歌词, Nothing),
            Function() 设置.实例对象.渲染封面图 AndAlso 播放控制器.当前音乐有封面,
            AddressOf 播放控制器.提交歌词图层, AddressOf 创建歌词呈现设置)
        信息图层呈现器 = New 播放器信息图层呈现器(画面控件,
            AddressOf 播放控制器.安全读取快照, AddressOf 播放控制器.安全读取媒体信息,
            Function() 播放控制器.当前媒体路径, Function() 播放控制器.当前字幕,
            Function() 播放控制器.当前弹幕, AddressOf 播放控制器.读取定时文字状态,
            AddressOf 播放控制器.读取弹幕状态, Function() 播放控制器.WASAPI模式,
            AddressOf 播放控制器.提交播放器信息图层)
        窗口布局控制器 = New 播放器窗口布局控制器(Me, MP_DX视频容器, 画面控件,
            AddressOf 播放控制器.重绑输出窗口)
        全屏交互控制器 = New 播放器全屏交互控制器(Me, 画面控件,
            ModernPanel1, MP_剪辑区间操作容器, Function() 剪辑区间控制器.模式已启用)

        AddHandler ThisIsYourWindow1.FullScreenChanged, AddressOf ThisIsYourWindow1_FullScreenChanged
        AddHandler 播放控制器.状态已变化, AddressOf 播放控制器_状态已变化
        AddHandler 播放控制器.媒体已打开, AddressOf 播放控制器_媒体已打开
        AddHandler 播放控制器.媒体已打开, AddressOf 剪辑区间控制器.媒体已打开
        AddHandler 播放控制器.播放错误, AddressOf 播放控制器_播放错误
        AddHandler 播放控制器.操作提示, AddressOf 播放控制器_操作提示
        AddHandler 播放控制器.HDR输出状态已确认, AddressOf 播放控制器_HDR输出状态已确认
        AddHandler 播放控制器.外部字幕已加载, AddressOf 播放控制器_外部字幕已加载
        AddHandler 播放控制器.字幕选择已变化, AddressOf 播放控制器_字幕选择已变化
        AddHandler 播放控制器.外部弹幕已加载, AddressOf 播放控制器_外部弹幕已加载
        AddHandler 播放控制器.外部歌词已加载, AddressOf 播放控制器_外部歌词已加载
        AddHandler 界面呈现器.请求跳转到关键帧, AddressOf 界面呈现器_请求跳转到关键帧
        AddHandler 界面呈现器.音量已变更, AddressOf 界面呈现器_音量已变更
        AddHandler 界面呈现器.播放状态已刷新, AddressOf 剪辑区间控制器.播放状态已刷新
        AddHandler 剪辑区间控制器.模式已变化, AddressOf 剪辑区间控制器_模式已变化
        AddHandler MB_剪辑区间模式.Click, AddressOf 剪辑区间控制器.切换模式
        AddHandler P_剪辑区间按钮容器.SizeChanged, AddressOf 剪辑区间控制器.按钮容器大小已变化
        AddHandler 剪辑区间控制器.进度条.请求跳转, AddressOf 剪辑区间控制器.进度条请求跳转
        AddHandler MB_后退到关键帧.Click, AddressOf 剪辑区间控制器.后退到关键帧
        AddHandler MB_前进到关键帧.Click, AddressOf 剪辑区间控制器.前进到关键帧
        AddHandler MB_后退一帧.Click, AddressOf 剪辑区间控制器.后退一帧
        AddHandler MB_进一帧.Click, AddressOf 剪辑区间控制器.前进一帧
        AddHandler MB_设为入点.Click, AddressOf 剪辑区间控制器.设为入点
        AddHandler MB_设为出点.Click, AddressOf 剪辑区间控制器.设为出点
        AddHandler MB_去入点.Click, AddressOf 剪辑区间控制器.去入点
        AddHandler MB_去出点.Click, AddressOf 剪辑区间控制器.去出点
        AddHandler MB_传给3FUI.Click, AddressOf 剪辑区间控制器.传给3FUI
        AddHandler MB_停止.Click, AddressOf 剪辑区间控制器.停止已点击
        AddHandler 方向键快捷键已请求, AddressOf 剪辑区间控制器.处理方向键快捷键
        界面呈现器.启动()
        更新弹幕按钮状态()
        PerformLayout()
        窗口布局控制器.应用初始画面尺寸(设置.实例对象.取得初始画面尺寸())
        核心文件检查通过 = True
    End Sub

    Private Shared Function 可以加载核心文件(文件名 As String) As Boolean
        If FFmpeg核心文件前缀.Any(Function(前缀) String.Equals(前缀, 文件名, StringComparison.OrdinalIgnoreCase)) Then
            For Each 候选路径 In 枚举可加载核心文件(文件名)
                If 尝试加载核心文件(候选路径) Then Return True
            Next
            Return False
        End If

        Return 尝试加载核心文件(文件名)
    End Function

    Private Shared Function 尝试加载核心文件(文件名 As String) As Boolean
        Dim 句柄 = IntPtr.Zero
        Try
            If Path.IsPathFullyQualified(文件名) Then
                Return NativeLibrary.TryLoad(文件名, 句柄)
            End If
            Return NativeLibrary.TryLoad(文件名, GetType(Form1).Assembly, Nothing, 句柄)
        Catch ex As BadImageFormatException
            Return False
        Catch ex As DllNotFoundException
            Return False
        Finally
            If 句柄 <> IntPtr.Zero Then NativeLibrary.Free(句柄)
        End Try
    End Function

    Private Shared Iterator Function 枚举可加载核心文件(前缀 As String) As IEnumerable(Of String)
        Dim 已检查目录 As New List(Of String)
        Dim 目录列表 As New List(Of String) From {AppContext.BaseDirectory}
        Dim 程序集目录 = Path.GetDirectoryName(GetType(Form1).Assembly.Location)
        If Not String.IsNullOrWhiteSpace(程序集目录) Then 目录列表.Add(程序集目录)

        Dim 环境路径 = Environment.GetEnvironmentVariable("PATH")
        If Not String.IsNullOrWhiteSpace(环境路径) Then
            目录列表.AddRange(环境路径.Split(Path.PathSeparator).
                Select(Function(目录) 目录.Trim().Trim(""""c)).
                Where(Function(目录) Not String.IsNullOrWhiteSpace(目录)))
        End If

        For Each 目录 In 目录列表
            Dim 完整目录 As String
            Try
                完整目录 = Path.GetFullPath(目录)
                If 完整目录.Length > 1 Then
                    完整目录 = 完整目录.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
                End If
            Catch ex As Exception
                Continue For
            End Try
            If 已检查目录.Any(Function(已检查) String.Equals(已检查, 完整目录, StringComparison.OrdinalIgnoreCase)) Then Continue For
            已检查目录.Add(完整目录)

            Dim 候选文件 As IEnumerable(Of String)
            Try
                候选文件 = Directory.EnumerateFiles(完整目录, 前缀 & "*.dll", SearchOption.TopDirectoryOnly).
                    Where(Function(路径) 是支持的核心文件名(Path.GetFileName(路径), 前缀)).
                    OrderByDescending(Function(路径) String.Equals(Path.GetFileName(路径), 前缀 & ".dll", StringComparison.OrdinalIgnoreCase)).
                    ThenBy(Function(路径) Path.GetFileName(路径), StringComparer.OrdinalIgnoreCase).
                    ToArray()
            Catch ex As IOException
                Continue For
            Catch ex As UnauthorizedAccessException
                Continue For
            End Try

            For Each 候选路径 In 候选文件
                Yield 候选路径
            Next
        Next
    End Function

    Private Shared Function 是支持的核心文件名(文件名 As String, 前缀 As String) As Boolean
        If String.IsNullOrEmpty(文件名) OrElse
            Not 文件名.EndsWith(".dll", StringComparison.OrdinalIgnoreCase) OrElse
            Not 文件名.StartsWith(前缀, StringComparison.OrdinalIgnoreCase) Then Return False

        Dim 后缀 = 文件名.Substring(前缀.Length, 文件名.Length - 前缀.Length - 4)
        If 后缀.Length = 0 Then Return True
        If Not 后缀.StartsWith("-", StringComparison.Ordinal) OrElse 后缀.Length = 1 Then Return False
        Return 后缀.Skip(1).All(Function(字符) 字符 >= "0"c AndAlso 字符 <= "9"c)
    End Function

    Private Function 创建界面呈现器() As 播放器界面呈现器
        Return New 播放器界面呈现器(
            ETB_媒体进度条, ETB_音量条, MB_播放和暂停,
            按钮图标.取得(播放器按钮图标.播放), 按钮图标.取得(播放器按钮图标.暂停),
            MB_软件解码或硬件解码, MB_HDR模式,
            MB_当前视频编码显示, MB_当前音频编码显示, MB_当前声道数显示, HCL_时间戳显示, Panel4,
            JEC_HDR选项前面的空白占位, JEC_当前视频编码显示前面的空白占位,
            JEC_当前音频编码显示前面的空白占位, JEC_当前声道数显示前面的空白占位, 画面控件,
            AddressOf 播放控制器.安全读取快照,
            Function() 播放控制器.是否正在切换,
            Function() 播放控制器.解码器,
            Function() 播放控制器.色彩模式)
    End Function

    Private Sub 配置播放器按钮图标()
        按钮图标.应用(MB_播放和暂停, 播放器按钮图标.播放)
        按钮图标.应用(MB_停止, 播放器按钮图标.停止)
        按钮图标.应用(MB_倒退或上一个, 播放器按钮图标.倒退或上一个)
        按钮图标.应用(MB_快进或下一个, 播放器按钮图标.前进或下一个)
        按钮图标.应用(MB_打开文件, 播放器按钮图标.打开)
        按钮图标.应用(MB_软件设置, 播放器按钮图标.设置)
        按钮图标.应用(MB_播放列表, 播放器按钮图标.播放列表)
        按钮图标.应用(MB_剪辑区间模式, 播放器按钮图标.剪辑区间)
        按钮图标.应用(MB_查看当前媒体信息, 播放器按钮图标.元数据)
        按钮图标.应用(MB_选择流, 播放器按钮图标.流选择)
    End Sub

    Private Sub Form1_Shown(sender As Object, e As EventArgs) Handles Me.Shown
        If Not 核心文件检查通过 Then
            BeginInvoke(Sub()
                            LakeUI.ExOverlayMsgBox(Me, 核心文件错误说明,
                                MsgBoxStyle.Critical Or MsgBoxStyle.OkOnly, "播放器核心文件缺失")
                            Close()
                        End Sub)
            Return
        End If

        Dim 启动文件 = Environment.GetCommandLineArgs().Skip(1).FirstOrDefault(Function(x) File.Exists(x))
        If Not String.IsNullOrEmpty(启动文件) Then BeginInvoke(Sub() 打开或替换文件(启动文件))
    End Sub

    Private Sub Form1_FormClosed(sender As Object, e As FormClosedEventArgs) Handles Me.FormClosed
        正在关闭 = True
        设置.退出时保存设置()
        RemoveHandler ThisIsYourWindow1.FullScreenChanged, AddressOf ThisIsYourWindow1_FullScreenChanged
        全屏交互控制器?.Dispose()
        窗口布局控制器?.释放()
        界面呈现器?.释放()
        信息图层呈现器?.释放()
        歌词图层呈现器?.Dispose()
        弹幕图层呈现器?.释放()
        字幕图层呈现器?.释放()
        流选择器?.Dispose()
        播放控制器?.释放()
        播放器按钮图标资源.清除(MB_播放和暂停, MB_停止, MB_倒退或上一个, MB_快进或下一个,
            MB_打开文件, MB_软件设置, MB_播放列表, MB_剪辑区间模式, MB_查看当前媒体信息, MB_选择流)
        按钮图标?.Dispose()
        设置.释放SP资源()
        当前主窗体 = Nothing
    End Sub

    Private Sub ThisIsYourWindow1_FullScreenChanged(
        sender As Object, e As LakeUI.ThisIsYourWindow.FullScreenChangedEventArgs)
        If e.HostForm Is Me AndAlso Not 正在关闭 Then 全屏交互控制器?.设置全屏状态(e.IsFullScreen)
    End Sub

    Private Sub 剪辑区间控制器_模式已变化(sender As Object, e As 剪辑区间模式变化事件参数)
        界面呈现器.设置精确时间戳(e.已启用)
        全屏交互控制器?.剪辑区间模式已变化()
    End Sub

    Private Sub MB_打开文件_Click(sender As Object, e As EventArgs) Handles MB_打开文件.Click
        If 正在关闭 Then Return
        Using 对话框 As New OpenFileDialog With {
            .CheckFileExists = True,
            .Filter = "所有文件|*.*",
            .RestoreDirectory = True,
            .Title = "打开媒体或替换歌词/字幕/弹幕"
        }
            If 对话框.ShowDialog(Me) = DialogResult.OK Then 打开或替换文件(对话框.FileName)
        End Using
    End Sub

    Private Sub 画面控件_文件拖入(sender As Object, e As 播放器文件拖入事件参数)
        Dim 存在的文件 = e.文件路径.Where(Function(x) File.Exists(x)).ToArray()
        If 存在的文件.Length = 0 Then Return
        Dim 路径 As String
        If 播放控制器.是否有媒体 Then
            路径 = 存在的文件.FirstOrDefault(AddressOf LRC歌词自动加载器.是支持的歌词文件)
            If String.IsNullOrEmpty(路径) Then
                路径 = 存在的文件.FirstOrDefault(AddressOf 外部字幕自动加载器.是支持的字幕文件)
            End If
            If String.IsNullOrEmpty(路径) Then
                路径 = 存在的文件.FirstOrDefault(AddressOf 弹幕自动加载器.是支持的弹幕文件)
            End If
        Else
            路径 = 存在的文件.FirstOrDefault(
                Function(x) Not 外部字幕自动加载器.是支持的字幕文件(x) AndAlso
                            Not 弹幕自动加载器.是支持的弹幕文件(x) AndAlso
                            Not LRC歌词自动加载器.是支持的歌词文件(x))
        End If
        If String.IsNullOrEmpty(路径) Then 路径 = 存在的文件(0)
        打开或替换文件(路径)
    End Sub

    Private Sub 打开或替换文件(路径 As String)
        If LRC歌词自动加载器.是支持的歌词文件(路径) Then
            播放控制器.替换歌词(路径)
        ElseIf 外部字幕自动加载器.是支持的字幕文件(路径) Then
            播放控制器.替换字幕(路径)
        ElseIf 弹幕自动加载器.是支持的弹幕文件(路径) Then
            播放控制器.替换弹幕(路径)
        Else
            播放控制器.打开媒体(路径)
        End If
    End Sub

    Private Sub 播放控制器_状态已变化(sender As Object, e As EventArgs)
        If Not 正在关闭 Then
            界面呈现器.刷新()
            更新WASAPI按钮()
        End If
    End Sub

    Private Sub 播放控制器_媒体已打开(sender As Object, e As 播放器媒体事件参数)
        If 正在关闭 Then Return
        当前弹幕路径 = String.Empty
        更新弹幕按钮可见性()
        字幕图层呈现器?.使图层失效()
        弹幕图层呈现器?.使图层失效()
        歌词图层呈现器?.使图层失效()
        信息图层呈现器?.使内容失效()
        Text = Path.GetFileName(e.文件路径)
        界面呈现器.媒体已打开(e.保留剪辑区间)
        界面呈现器.更新媒体信息(e.媒体信息, e.快照)
        界面呈现器.刷新()
    End Sub

    Private Sub 播放控制器_播放错误(sender As Object, e As 播放器错误事件参数)
        If Not 正在关闭 Then
            LakeUI.ExOverlayMsgBox(Me, e.消息, MsgBoxStyle.Critical Or MsgBoxStyle.OkOnly, e.标题)
        End If
    End Sub

    Private Sub 播放控制器_操作提示(sender As Object, e As 播放器操作提示事件参数)
        If 正在关闭 Then Return
        If e.弹出提示 Then
            LakeUI.ExOverlayMsgBox(Me, e.说明, MsgBoxStyle.Exclamation Or MsgBoxStyle.OkOnly, e.标题)
        Else
            信息图层呈现器?.显示操作信息(e.说明, &HFFF0D35DUI, "解码回退")
        End If
    End Sub

    Private Sub 播放控制器_HDR输出状态已确认(sender As Object, e As 播放器HDR状态事件参数)
        If Not 正在关闭 Then 信息图层呈现器?.显示操作信息(e.说明, &HFF69DF8BUI)
    End Sub

    Private Sub 播放控制器_外部字幕已加载(sender As Object, e As 播放器字幕事件参数)
        If 正在关闭 Then Return
        字幕图层呈现器?.使图层失效()
        信息图层呈现器?.显示操作信息($"已加载 {e.格式.ToString().ToUpperInvariant()} 字幕 · {Path.GetFileName(e.路径)}", &HFF55E7EAUI)
        信息图层呈现器?.使内容失效()
    End Sub

    Private Sub 播放控制器_字幕选择已变化(sender As Object, e As EventArgs)
        If Not 正在关闭 Then
            字幕图层呈现器?.使图层失效()
            信息图层呈现器?.使内容失效()
        End If
    End Sub

    Private Sub MB_选择流_Click(sender As Object, e As EventArgs) Handles MB_选择流.Click
        If Not 正在关闭 Then 流选择器?.显示()
    End Sub

    Private Sub MB_查看当前媒体信息_MouseClick(sender As Object, e As MouseEventArgs) Handles MB_查看当前媒体信息.MouseClick
        If 正在关闭 Then Return
        Select Case e.Button
            Case MouseButtons.Left
                显示媒体信息窗口()
            Case MouseButtons.Right
                切换媒体信息层()
        End Select
    End Sub

    Private Sub 显示媒体信息窗口()
        Dim 窗口 As New Form媒体信息(
            AddressOf 播放控制器.安全读取媒体信息,
            AddressOf 播放控制器.安全读取快照,
            AddressOf 播放控制器.读取定时文字状态,
            AddressOf 播放控制器.读取弹幕状态,
            Function() 播放控制器.当前字幕,
            Function() 播放控制器.当前弹幕,
            Function() 播放控制器.WASAPI模式,
            Function() 画面控件.ClientSize,
            AddressOf 播放控制器.读取音频峰值)
        窗口.Location = 窗口.居中于(Bounds)
        窗口.Show()
    End Sub

    Private Sub 切换媒体信息层()
        信息图层呈现器?.切换调试信息()
        MB_查看当前媒体信息.BackColor1 = Color.Transparent
        If 画面控件 IsNot Nothing AndAlso 画面控件.CanFocus Then 画面控件.Focus()
    End Sub

    Private Sub MB_当前声道数显示_Click(sender As Object, e As EventArgs) Handles MB_当前声道数显示.Click
        If 正在关闭 OrElse Not 播放控制器.是否有媒体 Then Return
        Dim 原模式 = 播放控制器.WASAPI模式
        If 原模式 = WASAPI共享模式.共享 AndAlso Not 确认切换到WASAPI独占模式() Then Return
        If 正在关闭 OrElse Not 播放控制器.是否有媒体 OrElse 播放控制器.WASAPI模式 <> 原模式 Then Return
        播放控制器.切换WASAPI模式()
        信息图层呈现器?.显示操作信息(If(原模式 = WASAPI共享模式.共享,
            "正在切换到 WASAPI 独占模式", "正在切换到 WASAPI 共享模式"), &HFFFFA85AUI)
    End Sub

    <CodeAnalysis.SuppressMessage("Performance", "CA1861:不要将常量数组作为参数", Justification:="<挂起>")>
    Private Function 确认切换到WASAPI独占模式() As Boolean
        Dim 内容 = String.Join(vbCrLf, {String.Empty,
            "是否切换到 WASAPI 独占模式？",
            String.Empty,
            "独占模式直通设备，能提供理论最佳音质，但也有诸多要求和限制：",
            String.Empty,
            "1. 再次提醒，如果您正戴着耳机，请立刻取下！",
            "2. 无法通过系统控制输出音量，请调整硬件设备旋钮或按键",
            "3. 其他应用无法发出任何声音，可能导致您错过重要事项",
            "4. 如果已经有应用占用了，则本应用会失败",
            "5. 安装在系统中的音效软件无法在独占模式工作",
            "6. 硬件设备必须支持对应的音频输出规格才能正常工作"})
        Return LakeUI.ExOverlayMsgBox(Me, 内容,
            {"我已取下耳机并确认切换独占模式", "现在不"},
            "如果您正戴着耳机，请立即取下！",
            MsgBoxStyle.Exclamation, 1) = 0
    End Function

    Private Sub 更新WASAPI按钮()
        If 播放控制器 Is Nothing Then Return
        MB_当前声道数显示.ForeColor = If(播放控制器.WASAPI模式 = WASAPI共享模式.独占, Color.IndianRed, Color.Silver)
    End Sub

    Private Sub 播放控制器_外部弹幕已加载(sender As Object, e As 播放器弹幕事件参数)
        If 正在关闭 Then Return
        当前弹幕路径 = e.路径
        更新弹幕按钮可见性()
        弹幕图层呈现器?.使图层失效()
        信息图层呈现器?.显示操作信息($"已加载 {e.数量} 条弹幕 · {Path.GetFileName(e.路径)}", &HFFFFA85AUI)
        信息图层呈现器?.使内容失效()
    End Sub

    Friend Sub 应用HDR峰值设置()
        播放控制器?.设置HDR峰值亮度(设置.实例对象.HDR峰值亮度)
    End Sub

    Friend Sub 应用SDR亮度设置()
        播放控制器?.设置SDR峰值亮度(设置.实例对象.HDR映射SDR参考亮度)
    End Sub

    Friend Sub 应用字幕设置()
        播放控制器?.当前字幕?.SRT生成器?.设置样式(设置.实例对象.创建SRT字幕样式())
        字幕图层呈现器?.使图层失效()
    End Sub

    Friend Sub 应用弹幕设置()
        弹幕图层呈现器?.应用弹幕设置(设置.实例对象.创建弹幕显示配置())
        更新弹幕按钮状态()
    End Sub

    Friend Sub 应用歌词设置()
        歌词图层呈现器?.使图层失效()
    End Sub

    Private Function 创建歌词呈现设置() As 歌词呈现设置
        Return New 歌词呈现设置(20.0F,
            If(设置.实例对象.渲染封面图毛玻璃背景, 5, 0), 4, &H78000000UI,
            40.0F, 60.0F, 20.0F, 0.0F, 7.5F)
    End Function

    Private Sub 更新弹幕按钮状态()
        MB_弹幕开关.ForeColor = If(设置.实例对象.弹幕已启用,
                                  Color.FromArgb(251, 114, 153), Color.Silver)
    End Sub

    Private Sub 更新弹幕按钮可见性()
        Dim 当前弹幕 = 播放控制器?.当前弹幕
        Dim 有可用弹幕 = 当前弹幕 IsNot Nothing AndAlso 当前弹幕.数量 > 0
        MB_弹幕开关.Visible = 有可用弹幕
        JEC_弹幕开关前面的空白占位.Visible = 有可用弹幕
    End Sub

    Private Sub MB_弹幕开关_Click(sender As Object, e As EventArgs) Handles MB_弹幕开关.Click
        设置.实例对象.弹幕已启用 = Not 设置.实例对象.弹幕已启用
        弹幕图层呈现器?.使图层失效()
        更新弹幕按钮状态()
    End Sub

    Friend Sub 设置窗口应用字体(fontName As String)
        设置窗口?.应用字体(fontName)
    End Sub

    Private Sub 播放控制器_外部歌词已加载(sender As Object, e As 播放器歌词事件参数)
        If 正在关闭 Then Return
        歌词图层呈现器?.使图层失效()
        信息图层呈现器?.显示操作信息(
            $"已加载 {e.条目数} 组 LRC 歌词 · {Path.GetFileName(e.路径)}", &HFF55E7EAUI)
    End Sub

    Private Sub 界面呈现器_请求跳转到关键帧(sender As Object, e As 播放器跳转请求事件参数)
        播放控制器.跳转到关键帧(e.位置)
    End Sub

    Private Sub 界面呈现器_音量已变更(sender As Object, e As 播放器音量事件参数)
        播放控制器.设置音量(e.音量)
        信息图层呈现器?.显示操作信息($"音量 {界面呈现器.音量百分比}%", &HFFF0D35DUI, "音量")
    End Sub

    Private Sub MB_播放和暂停_Click(sender As Object, e As EventArgs) Handles MB_播放和暂停.Click
        播放控制器.切换播放暂停()
    End Sub

    Private Sub MB_停止_Click(sender As Object, e As EventArgs) Handles MB_停止.Click
        播放控制器.停止()
        Text = "FFF.Player"
        界面呈现器.清除媒体()
    End Sub

    Private Sub MB_倒退或上一个_Click(sender As Object, e As EventArgs) Handles MB_倒退或上一个.Click
        播放控制器.相对跳转(-跳转秒数)
    End Sub

    Private Sub MB_快进或下一个_Click(sender As Object, e As EventArgs) Handles MB_快进或下一个.Click
        播放控制器.相对跳转(跳转秒数)
    End Sub

    Private Sub MB_软件解码或硬件解码_Click(sender As Object, e As EventArgs) Handles MB_软件解码或硬件解码.Click
        If Not String.IsNullOrEmpty(播放控制器.切换解码器()) Then
            设置.实例对象.解码方式 = CInt(播放控制器.解码器偏好)
        End If
    End Sub

    Private Sub MB_HDR模式_Click(sender As Object, e As EventArgs) Handles MB_HDR模式.Click
        播放控制器.切换HDR模式()
    End Sub

    Private Sub Form1_DpiChanged(sender As Object, e As DpiChangedEventArgs) Handles Me.DpiChanged
        界面呈现器?.更新Dpi()
    End Sub

    Protected Overrides Function ProcessDialogKey(keyData As Keys) As Boolean
        If keyData = Keys.Tab AndAlso Not 正在关闭 Then
            切换媒体信息层()
            Return True
        End If
        If 处理方向键快捷键(keyData) Then Return True
        Return MyBase.ProcessDialogKey(keyData)
    End Function

    Protected Overrides Function ProcessCmdKey(ByRef msg As Message, keyData As Keys) As Boolean
        If 正在关闭 Then Return MyBase.ProcessCmdKey(msg, keyData)
        If 处理方向键快捷键(keyData) Then Return True
        Select Case keyData
            Case Keys.Control Or Keys.O
                MB_打开文件_Click(MB_打开文件, EventArgs.Empty)
            Case Keys.Space, Keys.MediaPlayPause
                播放控制器.切换播放暂停()
            Case Keys.S, Keys.MediaStop
                MB_停止_Click(MB_停止, EventArgs.Empty)
            Case Keys.M
                播放控制器.切换静音()
                信息图层呈现器?.显示操作信息(If(播放控制器.静音,
                    "已静音", $"音量 {界面呈现器.音量百分比}%"), &HFFF0D35DUI, "音量")
            Case Else
                Return MyBase.ProcessCmdKey(msg, keyData)
        End Select
        Return True
    End Function

    Private Function 处理方向键快捷键(keyData As Keys) As Boolean
        If 正在关闭 Then Return False
        Dim 快捷键事件参数 As New KeyEventArgs(keyData)
        RaiseEvent 方向键快捷键已请求(Me, 快捷键事件参数)
        If 快捷键事件参数.Handled Then Return True
        Dim 按键 = keyData And Keys.KeyCode
        Dim 修饰键 = keyData And Keys.Modifiers
        Select Case 按键
            Case Keys.Left, Keys.Right
                If 修饰键 <> Keys.None Then Return False
                Dim 方向 = If(按键 = Keys.Left, -1, 1)
                播放控制器.相对跳转(方向 * 跳转秒数)
            Case Keys.Up, Keys.Down
                If 修饰键 <> Keys.None Then Return False
                Dim 增量 = If(按键 = Keys.Up, 5, -5)
                界面呈现器.调整音量(增量)
            Case Else
                Return False
        End Select
        Return True
    End Function

    Private Sub MB_播放列表_Click(sender As Object, e As EventArgs) Handles MB_播放列表.Click

    End Sub

    Private Sub MB_软件设置_Click(sender As Object, e As EventArgs) Handles MB_软件设置.Click
        If 设置窗口 Is Nothing OrElse 设置窗口.IsDisposed Then 设置窗口 = New Form设置()
        设置窗口.显示窗口()
    End Sub
End Class
