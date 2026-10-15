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
        Dim 资源名称 = 资源前缀 & 取得资源文件名(图标)
        Dim 程序集 = Assembly.GetExecutingAssembly()
        Using 资源流 = 程序集.GetManifestResourceStream(资源名称)
            If 资源流 Is Nothing Then
                Throw New InvalidOperationException($"找不到播放器按钮资源：{资源名称}")
            End If
            Dim 文档 = Svg.SvgDocument.Open(Of Svg.SvgDocument)(资源流)
            Return 文档.Draw(48, 48)
        End Using
    End Function

    Private Shared Function 取得资源文件名(图标 As 播放器按钮图标) As String
        Select Case 图标
            Case 播放器按钮图标.播放 : Return "Play.svg"
            Case 播放器按钮图标.暂停 : Return "Pause.svg"
            Case 播放器按钮图标.停止 : Return "Stop.svg"
            Case 播放器按钮图标.倒退或上一个 : Return "Previous.svg"
            Case 播放器按钮图标.前进或下一个 : Return "Next.svg"
            Case 播放器按钮图标.打开 : Return "Open.svg"
            Case 播放器按钮图标.设置 : Return "Settings.svg"
            Case 播放器按钮图标.播放列表 : Return "Playlist.svg"
            Case 播放器按钮图标.剪辑区间 : Return "ClipRange.svg"
            Case 播放器按钮图标.元数据 : Return "Metadata.svg"
            Case 播放器按钮图标.流选择 : Return "Streams.svg"
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
