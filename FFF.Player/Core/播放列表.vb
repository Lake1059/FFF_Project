Imports System.Globalization
Imports System.IO
Imports System.Text
Imports System.Text.RegularExpressions

Public Enum 列表播放模式
    播完停止 = 0
    顺序播放 = 1
    单项循环 = 2
    列表循环 = 3
    随机播放 = 4
End Enum

Public NotInheritable Class 播放列表项
    Public Sub New(本地路径 As String)
        路径 = 播放列表.规范本地文件(本地路径)
        标题 = IO.Path.GetFileNameWithoutExtension(路径)
    End Sub
    Public ReadOnly Property 路径 As String
    Public Property 标题 As String
End Class

Public NotInheritable Class 播放列表
    Private Shared ReadOnly 媒体扩展名 As New HashSet(Of String)(StringComparer.OrdinalIgnoreCase) From {
        ".mkv", ".mp4", ".m4v", ".mov", ".avi", ".wmv", ".webm", ".flv", ".ts", ".m2ts", ".mts",
        ".mpg", ".mpeg", ".vob", ".ogv", ".3gp", ".3g2", ".rm", ".rmvb", ".asf", ".divx",
        ".mp3", ".flac", ".wav", ".m4a", ".aac", ".ogg", ".opus", ".wma", ".ape", ".ac3", ".eac3", ".dts", ".mka",
        ".png", ".jpg", ".jpeg", ".gif", ".apng", ".webp", ".jxl", ".bmp", ".tif", ".tiff"
    }
    Private Shared ReadOnly 末尾数字 As New Regex("^(?<prefix>.*?)(?<number>\d+)(?<suffix>\D*)$", RegexOptions.Compiled Or RegexOptions.CultureInvariant)
    Private ReadOnly 项目 As New List(Of 播放列表项)()
    Private 当前值 As Integer = -1

    Public Event 列表变化 As EventHandler
    Public Property 播放模式 As 列表播放模式 = 列表播放模式.播完停止

    Public ReadOnly Property 数量 As Integer
        Get
            SyncLock 项目
                Return 项目.Count
            End SyncLock
        End Get
    End Property

    Public ReadOnly Property 当前索引 As Integer
        Get
            SyncLock 项目
                Return 当前值
            End SyncLock
        End Get
    End Property

    Public ReadOnly Property 当前项目 As 播放列表项
        Get
            SyncLock 项目
                Return If(当前值 >= 0 AndAlso 当前值 < 项目.Count, 项目(当前值), Nothing)
            End SyncLock
        End Get
    End Property

    Public Function 取得项目() As IReadOnlyList(Of 播放列表项)
        SyncLock 项目
            Return 项目.ToArray()
        End SyncLock
    End Function

    Public Sub 添加(本地路径 As String)
        Dim 新项目 As New 播放列表项(本地路径)
        SyncLock 项目
            If 项目.Any(Function(x) String.Equals(x.路径, 新项目.路径, StringComparison.OrdinalIgnoreCase)) Then Return
            项目.Add(新项目)
            If 当前值 < 0 Then 当前值 = 0
        End SyncLock
        RaiseEvent 列表变化(Me, EventArgs.Empty)
    End Sub

    Public Sub 移除(索引 As Integer)
        SyncLock 项目
            If 索引 < 0 OrElse 索引 >= 项目.Count Then Throw New ArgumentOutOfRangeException(NameOf(索引))
            项目.RemoveAt(索引)
            If 项目.Count = 0 Then
                当前值 = -1
            ElseIf 当前值 > 索引 Then
                当前值 -= 1
            ElseIf 当前值 >= 项目.Count Then
                当前值 = 项目.Count - 1
            End If
        End SyncLock
        RaiseEvent 列表变化(Me, EventArgs.Empty)
    End Sub

    Public Sub 移动(原索引 As Integer, 新索引 As Integer)
        SyncLock 项目
            If 原索引 < 0 OrElse 原索引 >= 项目.Count Then Throw New ArgumentOutOfRangeException(NameOf(原索引))
            If 新索引 < 0 OrElse 新索引 >= 项目.Count Then Throw New ArgumentOutOfRangeException(NameOf(新索引))
            Dim 当前路径 = If(当前值 >= 0, 项目(当前值).路径, Nothing)
            Dim 值 = 项目(原索引)
            项目.RemoveAt(原索引)
            项目.Insert(新索引, 值)
            当前值 = If(当前路径 Is Nothing, -1, 项目.FindIndex(Function(x) String.Equals(x.路径, 当前路径, StringComparison.OrdinalIgnoreCase)))
        End SyncLock
        RaiseEvent 列表变化(Me, EventArgs.Empty)
    End Sub

    Public Sub 选择(索引 As Integer)
        SyncLock 项目
            If 索引 < 0 OrElse 索引 >= 项目.Count Then Throw New ArgumentOutOfRangeException(NameOf(索引))
            当前值 = 索引
        End SyncLock
        RaiseEvent 列表变化(Me, EventArgs.Empty)
    End Sub

    Public Sub 清空()
        SyncLock 项目
            项目.Clear()
            当前值 = -1
        End SyncLock
        RaiseEvent 列表变化(Me, EventArgs.Empty)
    End Sub

    Public Sub 从媒体创建并扫描相似文件(本地路径 As String)
        Dim 当前路径 = 规范本地文件(本地路径)
        Dim 当前名称 = IO.Path.GetFileNameWithoutExtension(当前路径)
        Dim 签名 = 取得系列签名(当前名称)
        Dim 新列表 As New List(Of 播放列表项)()
        If 签名 IsNot Nothing Then
            For Each 文件 In IO.Directory.EnumerateFiles(IO.Path.GetDirectoryName(当前路径))
                If Not 媒体扩展名.Contains(IO.Path.GetExtension(文件)) Then Continue For
                If String.Equals(取得系列签名(IO.Path.GetFileNameWithoutExtension(文件)), 签名, StringComparison.OrdinalIgnoreCase) Then
                    Try
                        新列表.Add(New 播放列表项(文件))
                    Catch
                    End Try
                End If
            Next
        End If
        If Not 新列表.Any(Function(x) String.Equals(x.路径, 当前路径, StringComparison.OrdinalIgnoreCase)) Then 新列表.Add(New 播放列表项(当前路径))
        新列表.Sort(Function(a, b) 自然文件名比较器.实例.Compare(IO.Path.GetFileName(a.路径), IO.Path.GetFileName(b.路径)))
        SyncLock 项目
            项目.Clear()
            项目.AddRange(新列表)
            当前值 = 项目.FindIndex(Function(x) String.Equals(x.路径, 当前路径, StringComparison.OrdinalIgnoreCase))
        End SyncLock
        RaiseEvent 列表变化(Me, EventArgs.Empty)
    End Sub

    Public Sub 导出M3U8(列表路径 As String)
        ArgumentException.ThrowIfNullOrWhiteSpace(列表路径)
        Dim 完整列表路径 = IO.Path.GetFullPath(列表路径)
        Dim 目录 = IO.Path.GetDirectoryName(完整列表路径)
        Dim 行 As New List(Of String) From {"#EXTM3U"}
        SyncLock 项目
            For Each 值 In 项目
                行.Add("#EXTINF:-1," & 值.标题.Replace(ControlChars.Cr, " ").Replace(ControlChars.Lf, " "))
                行.Add(IO.Path.GetRelativePath(目录, 值.路径))
            Next
        End SyncLock
        IO.File.WriteAllLines(完整列表路径, 行, New UTF8Encoding(False))
    End Sub

    Public Sub 导入M3U8(列表路径 As String)
        Dim 完整列表路径 = 规范本地文件(列表路径)
        Dim 目录 = IO.Path.GetDirectoryName(完整列表路径)
        Dim 新列表 As New List(Of 播放列表项)()
        For Each 原始行 In IO.File.ReadLines(完整列表路径, Encoding.UTF8)
            Dim 行 = 原始行.Trim()
            If 行.Length = 0 OrElse 行.StartsWith("#", StringComparison.Ordinal) Then Continue For
            If 行.Contains("://", StringComparison.Ordinal) OrElse 行.StartsWith("\\", StringComparison.Ordinal) Then Throw New InvalidDataException("M3U8 中包含被禁止的网络路径。")
            Dim 路径 = If(IO.Path.IsPathRooted(行), 行, IO.Path.Combine(目录, 行))
            Dim 值 As New 播放列表项(路径)
            If Not 新列表.Any(Function(x) String.Equals(x.路径, 值.路径, StringComparison.OrdinalIgnoreCase)) Then 新列表.Add(值)
        Next
        SyncLock 项目
            项目.Clear()
            项目.AddRange(新列表)
            当前值 = If(项目.Count > 0, 0, -1)
        End SyncLock
        RaiseEvent 列表变化(Me, EventArgs.Empty)
    End Sub

    Public Function 移动到播放结束后的项目() As 播放列表项
        SyncLock 项目
            If 当前值 < 0 OrElse 项目.Count = 0 Then Return Nothing
            Select Case 播放模式
                Case 列表播放模式.播完停止
                    Return Nothing
                Case 列表播放模式.单项循环
                    Return 项目(当前值)
                Case 列表播放模式.顺序播放
                    If 当前值 + 1 >= 项目.Count Then Return Nothing
                    当前值 += 1
                Case 列表播放模式.列表循环
                    当前值 = (当前值 + 1) Mod 项目.Count
                Case 列表播放模式.随机播放
                    If 项目.Count > 1 Then
                        Dim 下一个 As Integer
                        Do
                            下一个 = Random.Shared.Next(项目.Count)
                        Loop While 下一个 = 当前值
                        当前值 = 下一个
                    End If
            End Select
            Return 项目(当前值)
        End SyncLock
    End Function

    Friend Shared Function 规范本地文件(值 As String) As String
        ArgumentException.ThrowIfNullOrWhiteSpace(值)
        If 值.Contains("://", StringComparison.Ordinal) OrElse 值.StartsWith("\\", StringComparison.Ordinal) OrElse 值.StartsWith("//", StringComparison.Ordinal) Then Throw New ArgumentException("只允许本地普通文件。", NameOf(值))
        Dim 完整路径 = IO.Path.GetFullPath(值)
        If Not IO.File.Exists(完整路径) Then Throw New FileNotFoundException("找不到本地文件。", 完整路径)
        If (IO.File.GetAttributes(完整路径) And IO.FileAttributes.ReparsePoint) <> 0 Then Throw New ArgumentException("不允许重解析点文件。", NameOf(值))
        Return 完整路径
    End Function

    Private Shared Function 取得系列签名(名称 As String) As String
        Dim 匹配 = 末尾数字.Match(名称)
        If Not 匹配.Success Then Return Nothing
        Dim 前缀 = Regex.Replace(匹配.Groups("prefix").Value.Trim(), "[\s._-]+", " ")
        Dim 后缀 = Regex.Replace(匹配.Groups("suffix").Value.Trim(), "[\s._-]+", " ")
        Return (前缀 & "#" & 后缀).ToUpperInvariant()
    End Function

    Private NotInheritable Class 自然文件名比较器
        Implements IComparer(Of String)
        Friend Shared ReadOnly 实例 As New 自然文件名比较器()
        Public Function Compare(x As String, y As String) As Integer Implements IComparer(Of String).Compare
            Dim 左 = Regex.Split(If(x, String.Empty), "(\d+)")
            Dim 右 = Regex.Split(If(y, String.Empty), "(\d+)")
            For 索引 = 0 To Math.Min(左.Length, 右.Length) - 1
                Dim 左数, 右数 As ULong
                Dim 结果 As Integer
                If ULong.TryParse(左(索引), NumberStyles.None, CultureInfo.InvariantCulture, 左数) AndAlso ULong.TryParse(右(索引), NumberStyles.None, CultureInfo.InvariantCulture, 右数) Then
                    结果 = 左数.CompareTo(右数)
                Else
                    结果 = StringComparer.CurrentCultureIgnoreCase.Compare(左(索引), 右(索引))
                End If
                If 结果 <> 0 Then Return 结果
            Next
            Return 左.Length.CompareTo(右.Length)
        End Function
    End Class
End Class

Public NotInheritable Class 播放列表控制器
    Implements IDisposable
    Private ReadOnly 播放器 As 播放器会话
    Private ReadOnly 列表 As 播放列表
    Private 等待自动播放 As Boolean

    Public Sub New(会话 As 播放器会话, 列表对象 As 播放列表)
        ArgumentNullException.ThrowIfNull(会话)
        ArgumentNullException.ThrowIfNull(列表对象)
        播放器 = 会话
        列表 = 列表对象
        AddHandler 播放器.播放结束, AddressOf 处理播放结束
        AddHandler 播放器.打开完成, AddressOf 处理打开完成
        AddHandler 播放器.错误, AddressOf 处理打开失败
    End Sub

    Public Sub 播放项目(索引 As Integer)
        列表.选择(索引)
        等待自动播放 = True
        播放器.打开(列表.当前项目.路径)
    End Sub

    Private Sub 处理播放结束(sender As Object, e As 播放器事件参数)
        Dim 下一项 = 列表.移动到播放结束后的项目()
        If 下一项 Is Nothing Then Return
        等待自动播放 = True
        播放器.打开(下一项.路径)
    End Sub

    Private Sub 处理打开完成(sender As Object, e As 播放器事件参数)
        If Not 等待自动播放 Then Return
        等待自动播放 = False
        播放器.播放()
    End Sub

    Private Sub 处理打开失败(sender As Object, e As 播放器事件参数)
        等待自动播放 = False
    End Sub

    Public Sub 释放() Implements IDisposable.Dispose
        RemoveHandler 播放器.播放结束, AddressOf 处理播放结束
        RemoveHandler 播放器.打开完成, AddressOf 处理打开完成
        RemoveHandler 播放器.错误, AddressOf 处理打开失败
        等待自动播放 = False
    End Sub
End Class
