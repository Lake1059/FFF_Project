Imports System.IO
Imports System.Threading

Public Class Form1
    Private Const 跳转秒数 As Integer = 5

    Private 画面控件 As 播放器画面控件
    Private 播放控制器 As 播放器控制器
    Private 界面呈现器 As 播放器界面呈现器
    Private 窗口布局控制器 As 播放器窗口布局控制器
    Private 定时文字图层呈现器 As 播放器定时文字图层呈现器
    Private 正在关闭 As Boolean

    Private Sub Form1_Load(sender As Object, e As EventArgs) Handles MyBase.Load
        ThisIsYourWindow1.Attach(Me)
        KeyPreview = True
        MinimumSize = New Size(875, 500)

        画面控件 = New 播放器画面控件 With {.Dock = DockStyle.Fill}
        MP_DX视频容器.Controls.Add(画面控件)
        AddHandler 画面控件.文件拖入, AddressOf 画面控件_文件拖入

        播放控制器 = New 播放器控制器(Function() 画面控件.输出窗口句柄, SynchronizationContext.Current)
        界面呈现器 = 创建界面呈现器()
        定时文字图层呈现器 = New 播放器定时文字图层呈现器(画面控件,
            AddressOf 播放控制器.安全读取快照, Function() 播放控制器.当前字幕,
            AddressOf 播放控制器.提交定时文字图层)
        窗口布局控制器 = New 播放器窗口布局控制器(Me, MP_DX视频容器, 画面控件,
            AddressOf 播放控制器.重绑输出窗口)

        AddHandler 播放控制器.状态已变化, AddressOf 播放控制器_状态已变化
        AddHandler 播放控制器.媒体已打开, AddressOf 播放控制器_媒体已打开
        AddHandler 播放控制器.播放错误, AddressOf 播放控制器_播放错误
        AddHandler 播放控制器.HDR输出状态已确认, AddressOf 播放控制器_HDR输出状态已确认
        AddHandler 播放控制器.外部字幕已加载, AddressOf 播放控制器_外部字幕已加载
        AddHandler 界面呈现器.请求跳转到关键帧, AddressOf 界面呈现器_请求跳转到关键帧
        AddHandler 界面呈现器.音量已变更, AddressOf 界面呈现器_音量已变更
        界面呈现器.启动()
    End Sub

    Private Function 创建界面呈现器() As 播放器界面呈现器
        Return New 播放器界面呈现器(
            ETB_媒体进度条, ETB_音量条, MB_播放和暂停, MB_软件解码或硬件解码, MB_HDR模式,
            MB_当前视频编码显示, MB_当前音频编码显示, MB_当前声道数显示, HCL_时间戳显示, Panel4,
            JEC_HDR选项前面的空白占位, JEC_当前视频编码显示前面的空白占位,
            JEC_当前音频编码显示前面的空白占位, JEC_当前声道数显示前面的空白占位, 画面控件,
            AddressOf 播放控制器.安全读取快照,
            Function() 播放控制器.是否正在切换,
            Function() 播放控制器.解码器,
            Function() 播放控制器.色彩模式)
    End Function

    Private Sub Form1_Shown(sender As Object, e As EventArgs) Handles Me.Shown
        BeginInvoke(New MethodInvoker(AddressOf 窗口布局控制器.校正初始视频比例))
        Dim 启动文件 = Environment.GetCommandLineArgs().Skip(1).FirstOrDefault(Function(x) File.Exists(x))
        If Not String.IsNullOrEmpty(启动文件) Then BeginInvoke(Sub() 播放控制器.打开媒体(启动文件))
    End Sub

    Private Sub Form1_FormClosed(sender As Object, e As FormClosedEventArgs) Handles Me.FormClosed
        正在关闭 = True
        窗口布局控制器?.释放()
        界面呈现器?.释放()
        定时文字图层呈现器?.释放()
        播放控制器?.释放()
    End Sub

    Private Sub MB_打开文件_Click(sender As Object, e As EventArgs) Handles MB_打开文件.Click
        If 正在关闭 Then Return
        Using 对话框 As New OpenFileDialog With {
            .CheckFileExists = True,
            .Filter = "媒体文件|*.3gp;*.aac;*.ape;*.avi;*.flac;*.flv;*.gif;*.jxl;*.m2ts;*.m4a;*.m4v;*.mka;*.mkv;*.mov;*.mp3;*.mp4;*.mpeg;*.mpg;*.ogg;*.opus;*.png;*.ts;*.wav;*.webm;*.webp;*.wmv|所有文件|*.*",
            .RestoreDirectory = True,
            .Title = "打开媒体文件"
        }
            If 对话框.ShowDialog(Me) = DialogResult.OK Then 播放控制器.打开媒体(对话框.FileName)
        End Using
    End Sub

    Private Sub 画面控件_文件拖入(sender As Object, e As 播放器文件拖入事件参数)
        Dim 路径 = e.文件路径.FirstOrDefault(Function(x) File.Exists(x))
        If Not String.IsNullOrEmpty(路径) Then 播放控制器.打开媒体(路径)
    End Sub

    Private Sub 播放控制器_状态已变化(sender As Object, e As EventArgs)
        If Not 正在关闭 Then 界面呈现器.刷新()
    End Sub

    Private Sub 播放控制器_媒体已打开(sender As Object, e As 播放器媒体事件参数)
        If 正在关闭 Then Return
        定时文字图层呈现器?.使图层失效()
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
        定时文字图层呈现器?.使图层失效()
        LakeUI.ExFloatingTip(MB_查看当前媒体信息, $"已加载 {e.格式} 字幕")
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

    Private Sub Form1_DpiChanged(sender As Object, e As DpiChangedEventArgs) Handles Me.DpiChanged
        界面呈现器?.更新Dpi()
    End Sub

    Protected Overrides Function ProcessCmdKey(ByRef msg As Message, keyData As Keys) As Boolean
        If 正在关闭 Then Return MyBase.ProcessCmdKey(msg, keyData)
        Select Case keyData
            Case Keys.Control Or Keys.O
                MB_打开文件_Click(MB_打开文件, EventArgs.Empty)
            Case Keys.Space, Keys.MediaPlayPause
                播放控制器.切换播放暂停()
            Case Keys.S, Keys.MediaStop
                MB_停止_Click(MB_停止, EventArgs.Empty)
            Case Keys.Left
                播放控制器.相对跳转(-跳转秒数)
            Case Keys.Right
                播放控制器.相对跳转(跳转秒数)
            Case Keys.Up
                显示音量提示(界面呈现器.调整音量(5))
            Case Keys.Down
                显示音量提示(界面呈现器.调整音量(-5))
            Case Keys.M
                播放控制器.切换静音()
                LakeUI.ExFloatingTip(ETB_音量条, If(播放控制器.静音, "已静音", $"音量 {界面呈现器.音量百分比}%"))
            Case Else
                Return MyBase.ProcessCmdKey(msg, keyData)
        End Select
        Return True
    End Function

    Private Sub 显示音量提示(百分比 As Integer)
        LakeUI.ExFloatingTip(ETB_音量条, $"音量 {百分比}%")
    End Sub
End Class
