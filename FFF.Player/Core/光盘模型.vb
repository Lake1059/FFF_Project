Imports System.IO
Imports System.Text.Json.Serialization

Public Enum 光盘命令
    上 = 0
    下 = 1
    左 = 2
    右 = 3
    确认 = 4
    根菜单 = 5
    弹出菜单 = 6
    返回 = 7
    鼠标移动 = 8
    鼠标确认 = 9
    标题 = 10
    章节 = 11
    音轨 = 12
    字幕 = 13
End Enum

Public NotInheritable Class 光盘状态
    <JsonPropertyName("kind")> Public Property 类型 As String = ""
    <JsonPropertyName("menu")> Public Property 菜单可见 As Boolean
    <JsonPropertyName("waiting")> Public Property 正在等待 As Boolean
    <JsonPropertyName("title")> Public Property 当前标题 As Integer
    <JsonPropertyName("chapter")> Public Property 当前章节 As Integer
    <JsonPropertyName("titles")> Public Property 标题数量 As Integer
    <JsonPropertyName("chapters")> Public Property 章节数量 As Integer
    <JsonPropertyName("playlist")> Public Property 播放列表编号 As Integer
    <JsonPropertyName("width")> Public Property 画布宽度 As Integer
    <JsonPropertyName("height")> Public Property 画布高度 As Integer
    <JsonPropertyName("aspect")> Public Property 显示比例 As Double
    <JsonPropertyName("overlaySequence")> Public Property 图形代次 As ULong
    Public ReadOnly Property 已打开 As Boolean
        Get
            Return 类型 = "bluray"
        End Get
    End Property
End Class

Public Module 光盘路径
    Public Function 是光盘路径(路径 As String) As Boolean
        Return 取得媒体路径核心(路径, 仅光盘:=True) IsNot Nothing
    End Function

    Public Function 媒体存在(路径 As String) As Boolean
        Return 取得媒体路径(路径) IsNot Nothing
    End Function

    Friend Function 取得媒体路径(路径 As String) As String
        Return 取得媒体路径核心(路径, 仅光盘:=False)
    End Function

    Private Function 取得媒体路径核心(路径 As String, 仅光盘 As Boolean) As String
        If String.IsNullOrWhiteSpace(路径) OrElse 路径.Contains("://", StringComparison.Ordinal) Then Return Nothing
        Try
            Dim 完整路径 = Path.TrimEndingDirectorySeparator(Path.GetFullPath(路径))
            If 完整路径.StartsWith("\\", StringComparison.Ordinal) OrElse
                (Not File.Exists(完整路径) AndAlso Not Directory.Exists(完整路径)) Then Return Nothing

            Dim 属性 = File.GetAttributes(完整路径)
            If (属性 And FileAttributes.ReparsePoint) <> 0 Then Return Nothing
            If (属性 And FileAttributes.Directory) <> 0 Then
                If 有蓝光索引(Path.Combine(完整路径, "BDMV")) Then Return 完整路径
                Return 取得BDMV所属光盘(完整路径)
            End If
            If String.Equals(Path.GetExtension(完整路径), ".bdmv", StringComparison.OrdinalIgnoreCase) Then
                Return If(取得BDMV所属光盘(Path.GetDirectoryName(完整路径)), 完整路径)
            End If
            Return If(仅光盘, Nothing, 完整路径)
        Catch ex As Exception When TypeOf ex Is IOException OrElse TypeOf ex Is UnauthorizedAccessException OrElse
                                   TypeOf ex Is ArgumentException OrElse TypeOf ex Is NotSupportedException
            Return Nothing
        End Try
    End Function

    Private Function 取得BDMV所属光盘(目录 As String) As String
        If Not String.Equals(Path.GetFileName(目录), "BDMV", StringComparison.OrdinalIgnoreCase) OrElse
            Not 有蓝光索引(目录) Then Return Nothing
        Dim 根目录 = Path.GetDirectoryName(目录)
        If (File.GetAttributes(根目录) And FileAttributes.ReparsePoint) <> 0 OrElse
            (File.GetAttributes(目录) And FileAttributes.ReparsePoint) <> 0 Then Return Nothing
        Return 根目录
    End Function

    Private Function 有蓝光索引(目录 As String) As Boolean
        Return File.Exists(Path.Combine(目录, "index.bdmv")) OrElse
            File.Exists(Path.Combine(目录, "BACKUP", "index.bdmv"))
    End Function
End Module
