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
            Return 类型 = "dvd" OrElse 类型 = "bluray"
        End Get
    End Property
End Class

Public Module 光盘路径
    Public Function 是光盘路径(路径 As String) As Boolean
        If String.IsNullOrWhiteSpace(路径) Then Return False
        Dim 扩展名 = Path.GetExtension(路径)
        Return String.Equals(扩展名, ".iso", StringComparison.OrdinalIgnoreCase) OrElse
            String.Equals(扩展名, ".bdmv", StringComparison.OrdinalIgnoreCase) OrElse
            String.Equals(扩展名, ".ifo", StringComparison.OrdinalIgnoreCase) OrElse Directory.Exists(路径)
    End Function
    Public Function 媒体存在(路径 As String) As Boolean
        Return Not String.IsNullOrWhiteSpace(路径) AndAlso (File.Exists(路径) OrElse Directory.Exists(路径))
    End Function
End Module
