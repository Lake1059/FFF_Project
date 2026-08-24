Imports System.ComponentModel
Imports System.Runtime.InteropServices

Friend NotInheritable Class 播放器画面控件
    Inherits Panel

    Private ReadOnly 视频输出窗口 As Panel
    Private 正在拖动全景视角 As Boolean
    Private 上次全景拖动位置 As Point
    Private 全景交互启用值 As Boolean

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
        AddHandler 视频输出窗口.MouseMove, AddressOf 视频输出窗口_MouseMove
        AddHandler 视频输出窗口.MouseUp, AddressOf 视频输出窗口_MouseUp
        AddHandler 视频输出窗口.MouseWheel, AddressOf 视频输出窗口_MouseWheel
    End Sub

    Friend Event 输出窗口创建 As EventHandler
    Friend Event 文件拖入 As EventHandler(Of 播放器文件拖入事件参数)
    Friend Event 音量滚轮 As EventHandler(Of MouseEventArgs)
    Friend Event 全景视场角滚轮 As EventHandler(Of MouseEventArgs)
    Friend Event 全景视角拖动 As EventHandler(Of 播放器360视角拖动事件参数)

    <Browsable(False), DesignerSerializationVisibility(DesignerSerializationVisibility.Hidden)>
    Friend Property 全景交互已启用 As Boolean
        Get
            Return 全景交互启用值
        End Get
        Set(value As Boolean)
            全景交互启用值 = value
            If value OrElse Not 正在拖动全景视角 Then Return
            正在拖动全景视角 = False
            视频输出窗口.Capture = False
        End Set
    End Property

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
        If 全景交互已启用 AndAlso e.Button = MouseButtons.Left Then
            正在拖动全景视角 = True
            上次全景拖动位置 = e.Location
            视频输出窗口.Capture = True
            Return
        End If
        If e.Button <> MouseButtons.Left OrElse 宿主窗口 Is Nothing OrElse
            宿主窗口.WindowState <> FormWindowState.Normal Then Return
        ReleaseCapture()
        SendMessage(宿主窗口.Handle, WM_NCLBUTTONDOWN, CType(HTCAPTION, IntPtr), IntPtr.Zero)
    End Sub

    Private Sub 视频输出窗口_MouseMove(sender As Object, e As MouseEventArgs)
        If Not 正在拖动全景视角 OrElse Not 全景交互已启用 Then Return
        Dim 水平位移 = e.X - 上次全景拖动位置.X
        Dim 垂直位移 = e.Y - 上次全景拖动位置.Y
        上次全景拖动位置 = e.Location
        If 水平位移 <> 0 OrElse 垂直位移 <> 0 Then
            RaiseEvent 全景视角拖动(Me, New 播放器360视角拖动事件参数(水平位移, 垂直位移))
        End If
    End Sub

    Private Sub 视频输出窗口_MouseUp(sender As Object, e As MouseEventArgs)
        If e.Button <> MouseButtons.Left OrElse Not 正在拖动全景视角 Then Return
        正在拖动全景视角 = False
        视频输出窗口.Capture = False
    End Sub

    Protected Overrides Sub OnMouseWheel(e As MouseEventArgs)
        MyBase.OnMouseWheel(e)
        RaiseEvent 音量滚轮(Me, e)
    End Sub

    Private Sub 视频输出窗口_MouseWheel(sender As Object, e As MouseEventArgs)
        RaiseEvent 音量滚轮(Me, e)
        RaiseEvent 全景视场角滚轮(Me, e)
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

Friend NotInheritable Class 播放器360视角拖动事件参数
    Inherits EventArgs

    Friend Sub New(水平位移值 As Integer, 垂直位移值 As Integer)
        水平位移 = 水平位移值
        垂直位移 = 垂直位移值
    End Sub

    Friend ReadOnly Property 水平位移 As Integer
    Friend ReadOnly Property 垂直位移 As Integer
End Class

Friend NotInheritable Class 播放器文件拖入事件参数
    Inherits EventArgs

    Friend Sub New(文件路径值 As String())
        文件路径 = 文件路径值
    End Sub

    Friend ReadOnly Property 文件路径 As String()
End Class
