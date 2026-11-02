Imports System.Reflection

Friend Enum 播放器按钮图标
    播放
    暂停
    停止
    倒退或上一个
    前进或下一个
    打开
    设置
    播放列表
    剪辑区间
    元数据
    流选择
End Enum

''' <summary>从程序集资源读取 SVG，并持有按钮使用的解码后图像。</summary>
Friend NotInheritable Class 播放器按钮图标资源
    Implements IDisposable

    Private Const 资源前缀 = "FFF.Player.Resources.PlayerButtons."
    ''' <summary>运行时资源扩展名。O4（2026-10-30）：图标改为构建期烘焙 PNG，不再运行时光栅化 SVG。</summary>
    Private Const 资源扩展名 = ".png"
    Private ReadOnly 图像 As New Dictionary(Of 播放器按钮图标, Image)
    Private 已释放 As Boolean

    Friend Shared Function 加载() As 播放器按钮图标资源
        Dim 资源 = New 播放器按钮图标资源()
        Try
            For Each 图标 In [Enum].GetValues(Of 播放器按钮图标)()
                资源.图像.Add(图标, 读取图像(图标))
            Next
            Return 资源
        Catch
            资源.Dispose()
            Throw
        End Try
    End Function

    Friend Function 取得(图标 As 播放器按钮图标) As Image
        If 已释放 Then Throw New ObjectDisposedException(NameOf(播放器按钮图标资源))
        Return 图像(图标)
    End Function

    Friend Sub 应用(按钮 As LakeUI.ModernButton, 图标 As 播放器按钮图标)
        按钮.Text = String.Empty
        按钮.BackImage = 取得(图标)
    End Sub

    Friend Shared Sub 清除(ParamArray 按钮() As LakeUI.ModernButton)
        For Each 当前按钮 In 按钮
            If 当前按钮 IsNot Nothing Then 当前按钮.BackImage = Nothing
        Next
    End Sub

    Private Shared Function 读取图像(图标 As 播放器按钮图标) As Image
        Dim 资源名称 = 资源前缀 & 取得资源文件名(图标) & 资源扩展名
        Dim 程序集 = Assembly.GetExecutingAssembly()
        Using 资源流 = 程序集.GetManifestResourceStream(资源名称)
            If 资源流 Is Nothing Then
                Throw New InvalidOperationException($"找不到播放器按钮资源：{资源名称}")
            End If
            ' GDI+ 的图像会惰性回读流：必须在流存活期间复制一份独立位图，不能把 Image 直接交出去。
            Using 原始 = New Bitmap(资源流)
                Return New Bitmap(原始)
            End Using
        End Using
    End Function

    Private Shared Function 取得资源文件名(图标 As 播放器按钮图标) As String
        Select Case 图标
            Case 播放器按钮图标.播放 : Return "Play"
            Case 播放器按钮图标.暂停 : Return "Pause"
            Case 播放器按钮图标.停止 : Return "Stop"
            Case 播放器按钮图标.倒退或上一个 : Return "Previous"
            Case 播放器按钮图标.前进或下一个 : Return "Next"
            Case 播放器按钮图标.打开 : Return "Open"
            Case 播放器按钮图标.设置 : Return "Settings"
            Case 播放器按钮图标.播放列表 : Return "Playlist"
            Case 播放器按钮图标.剪辑区间 : Return "ClipRange"
            Case 播放器按钮图标.元数据 : Return "Metadata"
            Case 播放器按钮图标.流选择 : Return "Streams"
            Case Else : Throw New ArgumentOutOfRangeException(NameOf(图标))
        End Select
    End Function

    Public Sub Dispose() Implements IDisposable.Dispose
        If 已释放 Then Return
        已释放 = True
        For Each 当前图像 In 图像.Values
            当前图像.Dispose()
        Next
        图像.Clear()
    End Sub
End Class
