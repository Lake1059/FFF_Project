Imports System.ComponentModel
Imports System.Runtime.InteropServices

Friend NotInheritable Class 播放器画面控件
    Inherits Panel

    Private ReadOnly 视频输出窗口 As Panel

    Private Const WM_NCLBUTTONDOWN As Integer = &HA1
    Private Const HTCAPTION As Integer = 2

    <DllImport("user32.dll", SetLastError:=False)>
    Private Shared Function ReleaseCapture() As Boolean
    End Function

    <DllImport("user32.dll", SetLastError:=False)>
    Private Shared Function SendMessage(窗口句柄 As IntPtr, 消息 As Integer,
                                        w参数 As IntPtr, l参数 As IntPtr) As IntPtr
    End Function

    Friend Sub New()
        BackColor = Color.Black
        Margin = Padding.Empty
        TabStop = True
        视频输出窗口 = New Panel With {
            .AllowDrop = True,
            .BackColor = Color.Black,
            .Dock = DockStyle.Fill,
            .Margin = Padding.Empty,
            .TabStop = True
        }
        Controls.Add(视频输出窗口)
        AddHandler 视频输出窗口.HandleCreated, AddressOf 视频输出窗口_HandleCreated
        AddHandler 视频输出窗口.DragEnter, AddressOf 文件_DragEnter
        AddHandler 视频输出窗口.DragDrop, AddressOf 文件_DragDrop
        AddHandler 视频输出窗口.MouseDown, AddressOf 视频输出窗口_MouseDown
        AddHandler 视频输出窗口.MouseWheel, AddressOf 视频输出窗口_MouseWheel
    End Sub

    Friend Event 输出窗口创建 As EventHandler
    Friend Event 文件拖入 As EventHandler(Of 播放器文件拖入事件参数)
    Friend Event 音量滚轮 As EventHandler(Of MouseEventArgs)

    <Browsable(False)>
    Friend ReadOnly Property 输出窗口句柄 As IntPtr
        Get
            Return 视频输出窗口.Handle
        End Get
    End Property

    Private Sub 视频输出窗口_HandleCreated(sender As Object, e As EventArgs)
        RaiseEvent 输出窗口创建(Me, EventArgs.Empty)
    End Sub

    Private Sub 视频输出窗口_MouseDown(sender As Object, e As MouseEventArgs)
        Dim 宿主窗口 = FindForm()
        宿主窗口?.Activate()
        Focus()
        If e.Button <> MouseButtons.Left OrElse 宿主窗口 Is Nothing OrElse
            宿主窗口.WindowState <> FormWindowState.Normal Then Return
        ReleaseCapture()
        SendMessage(宿主窗口.Handle, WM_NCLBUTTONDOWN, CType(HTCAPTION, IntPtr), IntPtr.Zero)
    End Sub

    Protected Overrides Sub OnMouseWheel(e As MouseEventArgs)
        MyBase.OnMouseWheel(e)
        RaiseEvent 音量滚轮(Me, e)
    End Sub

    Private Sub 视频输出窗口_MouseWheel(sender As Object, e As MouseEventArgs)
        RaiseEvent 音量滚轮(Me, e)
    End Sub

    Private Sub 文件_DragEnter(sender As Object, e As DragEventArgs)
        e.Effect = If(e.Data IsNot Nothing AndAlso e.Data.GetDataPresent(DataFormats.FileDrop),
            DragDropEffects.Copy, DragDropEffects.None)
    End Sub

    Private Sub 文件_DragDrop(sender As Object, e As DragEventArgs)
        If e.Data Is Nothing OrElse Not e.Data.GetDataPresent(DataFormats.FileDrop) Then Return
        Dim 路径 = TryCast(e.Data.GetData(DataFormats.FileDrop), String())
        If 路径 Is Nothing OrElse 路径.Length = 0 Then Return
        RaiseEvent 文件拖入(Me, New 播放器文件拖入事件参数(路径))
    End Sub
End Class

Friend NotInheritable Class 播放器文件拖入事件参数
    Inherits EventArgs

    Friend Sub New(文件路径值 As String())
        文件路径 = 文件路径值
    End Sub

    Friend ReadOnly Property 文件路径 As String()
End Class
