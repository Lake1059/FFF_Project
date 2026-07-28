Imports System.IO
Imports System.Diagnostics
Imports System.Threading

Public Class Form1
    Private Const 跳转秒数 As Integer = 5

    Private 画面控件 As 播放器画面控件
    Private 播放控制器 As 播放器控制器
    Private 界面呈现器 As 播放器界面呈现器
    Private 窗口布局控制器 As 播放器窗口布局控制器
    Private 字幕图层呈现器 As 播放器定时文字图层呈现器
    Private 弹幕图层呈现器 As 播放器定时文字图层呈现器
    Private 剪辑区间进度条 As 剪辑区间进度条控件
    Private 按钮图标 As 播放器按钮图标资源
    Private 正在关闭 As Boolean
    Private 剪辑区间模式已启用 As Boolean

    Private Sub Form1_Load(sender As Object, e As EventArgs) Handles MyBase.Load
        ThisIsYourWindow1.Attach(Me)
        KeyPreview = True
        MB_剪辑区间模式.TabStop = False
        MinimumSize = New Size(875, 500)

        按钮图标 = 播放器按钮图标资源.加载()
        配置播放器按钮图标()

        画面控件 = New 播放器画面控件 With {.Dock = DockStyle.Fill}
        MP_DX视频容器.Controls.Add(画面控件)
        AddHandler 画面控件.文件拖入, AddressOf 画面控件_文件拖入

        剪辑区间进度条 = New 剪辑区间进度条控件 With {.Dock = DockStyle.Fill}
        P_剪辑区间进度条容器.Controls.Add(剪辑区间进度条)
        AddHandler 剪辑区间进度条.请求跳转, AddressOf 剪辑区间进度条_请求跳转
        校正剪辑按钮宽度()

        播放控制器 = New 播放器控制器(Function() 画面控件.输出窗口句柄, SynchronizationContext.Current)
        界面呈现器 = 创建界面呈现器()
        字幕图层呈现器 = New 播放器定时文字图层呈现器(画面控件,
            AddressOf 播放控制器.安全读取快照, Function() 播放控制器.当前字幕,
            AddressOf 播放控制器.提交定时文字图层, Nothing, Nothing,
            定时文字图层内容.仅字幕)
        弹幕图层呈现器 = New 播放器定时文字图层呈现器(画面控件,
            AddressOf 播放控制器.安全读取快照, Function() Nothing,
            AddressOf 播放控制器.提交弹幕图层, Function() 播放控制器.当前弹幕,
            Nothing, 定时文字图层内容.仅弹幕)
        窗口布局控制器 = New 播放器窗口布局控制器(Me, MP_DX视频容器, 画面控件,
            AddressOf 播放控制器.重绑输出窗口)

        AddHandler 播放控制器.状态已变化, AddressOf 播放控制器_状态已变化
        AddHandler 播放控制器.媒体已打开, AddressOf 播放控制器_媒体已打开
        AddHandler 播放控制器.播放错误, AddressOf 播放控制器_播放错误
        AddHandler 播放控制器.HDR输出状态已确认, AddressOf 播放控制器_HDR输出状态已确认
        AddHandler 播放控制器.外部字幕已加载, AddressOf 播放控制器_外部字幕已加载
        AddHandler 播放控制器.外部弹幕已加载, AddressOf 播放控制器_外部弹幕已加载
        AddHandler 界面呈现器.请求跳转到关键帧, AddressOf 界面呈现器_请求跳转到关键帧
        AddHandler 界面呈现器.音量已变更, AddressOf 界面呈现器_音量已变更
        界面呈现器.启动()
    End Sub

    Private Function 创建界面呈现器() As 播放器界面呈现器
        Return New 播放器界面呈现器(
            ETB_媒体进度条, ETB_音量条, MB_播放和暂停,
            按钮图标.取得(播放器按钮图标.播放), 按钮图标.取得(播放器按钮图标.暂停),
            MB_软件解码或硬件解码, MB_HDR模式,
            MB_当前视频编码显示, MB_当前音频编码显示, MB_当前声道数显示, HCL_时间戳显示, Panel4,
            JEC_HDR选项前面的空白占位, JEC_当前视频编码显示前面的空白占位,
            JEC_当前音频编码显示前面的空白占位, JEC_当前声道数显示前面的空白占位, 画面控件,
            剪辑区间进度条,
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
        BeginInvoke(New MethodInvoker(AddressOf 窗口布局控制器.校正初始视频比例))
        Dim 启动文件 = Environment.GetCommandLineArgs().Skip(1).FirstOrDefault(Function(x) File.Exists(x))
        If Not String.IsNullOrEmpty(启动文件) Then BeginInvoke(Sub() 打开或替换文件(启动文件))
    End Sub

    Private Sub Form1_FormClosed(sender As Object, e As FormClosedEventArgs) Handles Me.FormClosed
        正在关闭 = True
        窗口布局控制器?.释放()
        界面呈现器?.释放()
        弹幕图层呈现器?.释放()
        字幕图层呈现器?.释放()
        播放控制器?.释放()
        播放器按钮图标资源.清除(MB_播放和暂停, MB_停止, MB_倒退或上一个, MB_快进或下一个,
            MB_打开文件, MB_软件设置, MB_播放列表, MB_剪辑区间模式, MB_查看当前媒体信息, MB_选择流)
        按钮图标?.Dispose()
    End Sub

    Private Sub MB_打开文件_Click(sender As Object, e As EventArgs) Handles MB_打开文件.Click
        If 正在关闭 Then Return
        Using 对话框 As New OpenFileDialog With {
            .CheckFileExists = True,
            .Filter = "媒体、字幕或弹幕文件|*.3gp;*.aac;*.ape;*.avi;*.flac;*.flv;*.gif;*.jxl;*.m2ts;*.m4a;*.m4v;*.mka;*.mkv;*.mov;*.mp3;*.mp4;*.mpeg;*.mpg;*.ogg;*.opus;*.png;*.ts;*.wav;*.webm;*.webp;*.wmv;*.srt;*.ass;*.ssa;*.sup;*.xml|字幕文件|*.srt;*.ass;*.ssa;*.sup|弹幕文件|*.xml|所有文件|*.*",
            .RestoreDirectory = True,
            .Title = "打开媒体或替换字幕/弹幕"
        }
            If 对话框.ShowDialog(Me) = DialogResult.OK Then 打开或替换文件(对话框.FileName)
        End Using
    End Sub

    Private Sub 画面控件_文件拖入(sender As Object, e As 播放器文件拖入事件参数)
        Dim 存在的文件 = e.文件路径.Where(Function(x) File.Exists(x)).ToArray()
        If 存在的文件.Length = 0 Then Return
        Dim 路径 As String
        If 播放控制器.是否有媒体 Then
            路径 = 存在的文件.FirstOrDefault(AddressOf 外部字幕自动加载器.是支持的字幕文件)
            If String.IsNullOrEmpty(路径) Then
                路径 = 存在的文件.FirstOrDefault(AddressOf 弹幕自动加载器.是支持的弹幕文件)
            End If
        Else
            路径 = 存在的文件.FirstOrDefault(
                Function(x) Not 外部字幕自动加载器.是支持的字幕文件(x) AndAlso
                            Not 弹幕自动加载器.是支持的弹幕文件(x))
        End If
        If String.IsNullOrEmpty(路径) Then 路径 = 存在的文件(0)
        打开或替换文件(路径)
    End Sub

    Private Sub 打开或替换文件(路径 As String)
        If 外部字幕自动加载器.是支持的字幕文件(路径) Then
            播放控制器.替换字幕(路径)
        ElseIf 弹幕自动加载器.是支持的弹幕文件(路径) Then
            播放控制器.替换弹幕(路径)
        Else
            播放控制器.打开媒体(路径)
        End If
    End Sub

    Private Sub 播放控制器_状态已变化(sender As Object, e As EventArgs)
        If Not 正在关闭 Then 界面呈现器.刷新()
    End Sub

    Private Sub 播放控制器_媒体已打开(sender As Object, e As 播放器媒体事件参数)
        If 正在关闭 Then Return
        字幕图层呈现器?.使图层失效()
        弹幕图层呈现器?.使图层失效()
        If Not e.保留剪辑区间 Then 剪辑区间进度条?.清除媒体()
        Text = Path.GetFileName(e.文件路径)
        界面呈现器.更新媒体信息(e.媒体信息, e.快照)
        界面呈现器.刷新()
    End Sub

    Private Sub 播放控制器_播放错误(sender As Object, e As 播放器错误事件参数)
        If Not 正在关闭 Then
            LakeUI.ExOverlayMsgBox(Me, e.消息, MsgBoxStyle.Critical Or MsgBoxStyle.OkOnly, e.标题)
        End If
    End Sub

    Private Sub 播放控制器_HDR输出状态已确认(sender As Object, e As 播放器HDR状态事件参数)
        If Not 正在关闭 Then LakeUI.ExFloatingTip(MB_HDR模式, e.说明)
    End Sub

    Private Sub 播放控制器_外部字幕已加载(sender As Object, e As 播放器字幕事件参数)
        If 正在关闭 Then Return
        字幕图层呈现器?.使图层失效()
        LakeUI.ExFloatingTip(MB_查看当前媒体信息, $"已加载 {e.格式} 字幕")
    End Sub

    Private Sub 播放控制器_外部弹幕已加载(sender As Object, e As 播放器弹幕事件参数)
        If 正在关闭 Then Return
        弹幕图层呈现器?.使图层失效()
        LakeUI.ExFloatingTip(MB_查看当前媒体信息, $"已加载 {e.数量} 条弹幕")
    End Sub

    Private Sub 界面呈现器_请求跳转到关键帧(sender As Object, e As 播放器跳转请求事件参数)
        播放控制器.跳转到关键帧(e.位置)
    End Sub

    Private Sub 界面呈现器_音量已变更(sender As Object, e As 播放器音量事件参数)
        播放控制器.设置音量(e.音量)
    End Sub

    Private Sub MB_播放和暂停_Click(sender As Object, e As EventArgs) Handles MB_播放和暂停.Click
        播放控制器.切换播放暂停()
    End Sub

    Private Sub MB_停止_Click(sender As Object, e As EventArgs) Handles MB_停止.Click
        播放控制器.停止()
        Text = "FFF.Player"
        界面呈现器.清除媒体()
        剪辑区间进度条?.清除媒体()
    End Sub

    Private Sub MB_倒退或上一个_Click(sender As Object, e As EventArgs) Handles MB_倒退或上一个.Click
        播放控制器.相对跳转(-跳转秒数)
    End Sub

    Private Sub MB_快进或下一个_Click(sender As Object, e As EventArgs) Handles MB_快进或下一个.Click
        播放控制器.相对跳转(跳转秒数)
    End Sub

    Private Sub MB_软件解码或硬件解码_Click(sender As Object, e As EventArgs) Handles MB_软件解码或硬件解码.Click
        Dim 模式 = 播放控制器.切换解码器()
        If Not String.IsNullOrEmpty(模式) Then LakeUI.ExFloatingTip(MB_软件解码或硬件解码, $"{模式} 解码")
    End Sub

    Private Sub MB_HDR模式_Click(sender As Object, e As EventArgs) Handles MB_HDR模式.Click
        播放控制器.切换HDR模式()
    End Sub

    Private Sub MB_剪辑区间模式_Click(sender As Object, e As EventArgs) Handles MB_剪辑区间模式.Click
        If 正在关闭 Then Return
        剪辑区间模式已启用 = Not 剪辑区间模式已启用
        MP_剪辑区间操作容器.Visible = 剪辑区间模式已启用
        MB_剪辑区间模式.BackColor1 = If(剪辑区间模式已启用,
            Color.FromArgb(40, 220, 220, 220), Color.Transparent)
        界面呈现器?.设置剪辑区间模式(剪辑区间模式已启用)
        If 画面控件 IsNot Nothing AndAlso 画面控件.CanFocus Then 画面控件.Focus()
    End Sub

    Private Sub P_剪辑区间按钮容器_SizeChanged(sender As Object, e As EventArgs) Handles P_剪辑区间按钮容器.SizeChanged
        校正剪辑按钮宽度()
    End Sub

    Private Sub 校正剪辑按钮宽度()
        If P_剪辑区间按钮容器 Is Nothing OrElse P_剪辑区间按钮容器.ClientSize.Width <= 0 Then Return
        Dim 按钮 = P_剪辑区间按钮容器.Controls.OfType(Of LakeUI.ModernButton)().ToArray()
        If 按钮.Length = 0 Then Return
        Dim 分割宽度 = P_剪辑区间按钮容器.Controls.OfType(Of LakeUI.JustEmptyControl)().Sum(Function(x) x.Width)
        Dim 可用宽度 = Math.Max(按钮.Length, P_剪辑区间按钮容器.ClientSize.Width - 分割宽度)
        Dim 统一宽度 = Math.Max(1, 可用宽度 \ 按钮.Length)

        P_剪辑区间按钮容器.SuspendLayout()
        Try
            P_剪辑区间按钮容器.Padding = Padding.Empty
            For Each 当前按钮 In 按钮
                If 当前按钮 IsNot MB_传给3FUI Then 当前按钮.Width = 统一宽度
            Next
        Finally
            P_剪辑区间按钮容器.ResumeLayout(True)
        End Try
    End Sub

    Private Sub 剪辑区间进度条_请求跳转(sender As Object, e As 播放器跳转请求事件参数)
        播放控制器?.跳转到位置(e.位置)
    End Sub

    Private Function 当前剪辑时间() As TimeSpan?
        Dim 快照 = 播放控制器?.安全读取快照()
        If 快照 Is Nothing OrElse 快照.总时长 <= TimeSpan.Zero Then Return Nothing
        Return 快照.播放位置
    End Function

    Private Sub MB_后退到关键帧_Click(sender As Object, e As EventArgs) Handles MB_后退到关键帧.Click
        播放控制器?.跳转到相邻关键帧(-1)
    End Sub

    Private Sub MB_前进到关键帧_Click(sender As Object, e As EventArgs) Handles MB_前进到关键帧.Click
        播放控制器?.跳转到相邻关键帧(1)
    End Sub

    Private Sub MB_后退一帧_Click(sender As Object, e As EventArgs) Handles MB_后退一帧.Click
        播放控制器?.逐帧(-1)
    End Sub

    Private Sub MB_进一帧_Click(sender As Object, e As EventArgs) Handles MB_进一帧.Click
        播放控制器?.逐帧(1)
    End Sub

    Private Sub MB_设为入点_Click(sender As Object, e As EventArgs) Handles MB_设为入点.Click
        Dim 时间 = 当前剪辑时间()
        If 时间.HasValue Then 剪辑区间进度条?.设为入点(时间.Value)
    End Sub

    Private Sub MB_设为出点_Click(sender As Object, e As EventArgs) Handles MB_设为出点.Click
        Dim 时间 = 当前剪辑时间()
        If 时间.HasValue Then 剪辑区间进度条?.设为出点(时间.Value)
    End Sub

    Private Sub MB_去入点_Click(sender As Object, e As EventArgs) Handles MB_去入点.Click
        If 剪辑区间进度条 IsNot Nothing AndAlso 剪辑区间进度条.入点.HasValue Then
            播放控制器?.跳转到位置(剪辑区间进度条.入点.Value)
        End If
    End Sub

    Private Sub MB_去出点_Click(sender As Object, e As EventArgs) Handles MB_去出点.Click
        If 剪辑区间进度条 IsNot Nothing AndAlso 剪辑区间进度条.出点.HasValue Then
            播放控制器?.跳转到位置(剪辑区间进度条.出点.Value)
        End If
    End Sub

    Private Sub MB_传给3FUI_Click(sender As Object, e As EventArgs) Handles MB_传给3FUI.Click
        Dim 时间轴 = 剪辑区间进度条
        If 时间轴 Is Nothing OrElse (Not 时间轴.入点.HasValue AndAlso Not 时间轴.出点.HasValue) Then
            LakeUI.ExFloatingTip(MB_传给3FUI, "没有可发送的入点或出点")
            Return
        End If
        If 时间轴.入点.HasValue AndAlso 时间轴.出点.HasValue AndAlso 时间轴.出点.Value < 时间轴.入点.Value Then
            LakeUI.ExFloatingTip(MB_传给3FUI, "出点不能早于入点")
            Return
        End If

        Try
            Dim 已找到3FUI As Boolean
            Dim 文件路径 = 查找3FUI路径(已找到3FUI)
            If Not 已找到3FUI Then
                LakeUI.ExFloatingTip(MB_传给3FUI, "请先启动 3FUI")
                Return
            End If
            If String.IsNullOrWhiteSpace(文件路径) Then
                LakeUI.ExFloatingTip(MB_传给3FUI, "无法取得 3FUI 路径")
                Return
            End If

            Dim 启动信息 As New ProcessStartInfo With {
                .FileName = 文件路径,
                .UseShellExecute = False,
                .CreateNoWindow = True}
            If 时间轴.入点.HasValue Then
                启动信息.ArgumentList.Add("-3fuiVideoHelperInPointTime")
                启动信息.ArgumentList.Add(剪辑区间进度条控件.格式化时长(时间轴.入点.Value))
            End If
            If 时间轴.出点.HasValue Then
                启动信息.ArgumentList.Add("-3fuiVideoHelperOutPointTime")
                启动信息.ArgumentList.Add(剪辑区间进度条控件.格式化时长(时间轴.出点.Value))
            End If
            Using 已启动 = Process.Start(启动信息)
            End Using
            LakeUI.ExFloatingTip(MB_传给3FUI, "剪辑区间已发送")
        Catch ex As Exception
            LakeUI.ExFloatingTip(MB_传给3FUI, $"发送失败：{ex.Message}")
        End Try
    End Sub

    Private Shared Function 查找3FUI路径(ByRef 已找到 As Boolean) As String
        已找到 = False
        For Each 候选进程 In Process.GetProcesses()
            Try
                If Not 候选进程.ProcessName.Contains("FFmpegFreeUI", StringComparison.OrdinalIgnoreCase) Then Continue For
                已找到 = True
                Dim 路径 = 候选进程.MainModule?.FileName
                If Not String.IsNullOrWhiteSpace(路径) Then Return 路径
            Catch ex As Exception When TypeOf ex Is InvalidOperationException OrElse
                                             TypeOf ex Is ComponentModel.Win32Exception OrElse
                                             TypeOf ex Is NotSupportedException
                ' 枚举期间进程可能退出或拒绝访问；继续查找其他 3FUI 实例。
            Finally
                候选进程.Dispose()
            End Try
        Next
        Return Nothing
    End Function

    Private Sub Form1_DpiChanged(sender As Object, e As DpiChangedEventArgs) Handles Me.DpiChanged
        界面呈现器?.更新Dpi()
    End Sub

    Protected Overrides Function ProcessDialogKey(keyData As Keys) As Boolean
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
                LakeUI.ExFloatingTip(ETB_音量条, If(播放控制器.静音, "已静音", $"音量 {界面呈现器.音量百分比}%"))
            Case Else
                Return MyBase.ProcessCmdKey(msg, keyData)
        End Select
        Return True
    End Function

    Private Function 处理方向键快捷键(keyData As Keys) As Boolean
        If 正在关闭 Then Return False
        Dim 按键 = keyData And Keys.KeyCode
        Dim 修饰键 = keyData And Keys.Modifiers
        Select Case 按键
            Case Keys.Left, Keys.Right
                Dim 方向 = If(按键 = Keys.Left, -1, 1)
                If 剪辑区间模式已启用 Then
                    Select Case 修饰键
                        Case Keys.None
                            播放控制器.逐帧(方向)
                        Case Keys.Control
                            播放控制器.跳转到相邻关键帧(方向)
                        Case Keys.Alt
                            播放控制器.相对跳转(方向 * 10)
                        Case Else
                            Return False
                    End Select
                Else
                    If 修饰键 <> Keys.None Then Return False
                    播放控制器.相对跳转(方向 * 跳转秒数)
                End If
            Case Keys.Up, Keys.Down
                If 修饰键 <> Keys.None Then Return False
                Dim 增量 = If(按键 = Keys.Up, 5, -5)
                显示音量提示(界面呈现器.调整音量(增量))
            Case Else
                Return False
        End Select
        Return True
    End Function

    Private Sub 显示音量提示(百分比 As Integer)
        LakeUI.ExFloatingTip(ETB_音量条, $"音量 {百分比}%")
    End Sub
End Class
