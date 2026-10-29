Imports System.Globalization
Imports System.IO
Imports System.Text
Imports System.Text.RegularExpressions
Imports System.Threading
Imports System.Threading.Tasks

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
        ".wv", ".tak", ".aif", ".aiff", ".amr", ".au", ".ra", ".tta", ".mpc",
        ".png", ".jpg", ".jpeg", ".jpe", ".gif", ".apng", ".webp", ".jxl", ".bmp", ".tif", ".tiff",
        ".ico", ".avif", ".avifs", ".heic", ".heif", ".jxr", ".wdp", ".hdp"
    }
    ' 图片模式用此集合判断当前媒体是否为图片。
    Private Shared ReadOnly 图片扩展名 As New HashSet(Of String)(StringComparer.OrdinalIgnoreCase) From {
        ".png", ".jpg", ".jpeg", ".jpe", ".gif", ".apng", ".webp", ".jxl", ".bmp", ".tif", ".tiff",
        ".ico", ".avif", ".avifs", ".heic", ".heif", ".jxr", ".wdp", ".hdp"
    }
    Private Const 自动列表最大数量 As Integer = 1000
    Private Shared ReadOnly 自动列表比较器 As IComparer(Of 播放列表项) = Comparer(Of 播放列表项).Create(
        Function(a, b) 比较媒体路径(a.路径, b.路径))
    Private ReadOnly 项目 As New List(Of 播放列表项)()
    Private 当前值 As Integer = -1
    Private 扫描版本 As Integer
    Private 缓存版本 As Integer = -1
    Private 缓存目录时间 As DateTime

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
            Interlocked.Increment(扫描版本)
            项目.Add(新项目)
            If 当前值 < 0 Then 当前值 = 0
        End SyncLock
        RaiseEvent 列表变化(Me, EventArgs.Empty)
    End Sub

    ''' <summary>按传入顺序批量追加本地媒体，并只发布一次列表变化事件。</summary>
    Public Sub 添加多个(本地路径 As IEnumerable(Of String))
        ArgumentNullException.ThrowIfNull(本地路径)
        Dim 新项目 As New List(Of 播放列表项)()
        SyncLock 项目
            Dim 已存在路径 As New HashSet(Of String)(项目.Select(Function(x) x.路径), StringComparer.OrdinalIgnoreCase)
            For Each 路径 In 本地路径
                If String.IsNullOrWhiteSpace(路径) Then Continue For
                Dim 项 As New 播放列表项(路径)
                If 已存在路径.Add(项.路径) Then 新项目.Add(项)
            Next
            If 新项目.Count = 0 Then Return
            Interlocked.Increment(扫描版本)
            项目.AddRange(新项目)
            If 当前值 < 0 Then 当前值 = 项目.Count - 新项目.Count
        End SyncLock
        RaiseEvent 列表变化(Me, EventArgs.Empty)
    End Sub

    Public Sub 移除(索引 As Integer)
        SyncLock 项目
            If 索引 < 0 OrElse 索引 >= 项目.Count Then Throw New ArgumentOutOfRangeException(NameOf(索引))
            Interlocked.Increment(扫描版本)
            Dim 移除当前项目 = 当前值 = 索引
            项目.RemoveAt(索引)
            If 项目.Count = 0 OrElse 移除当前项目 Then
                当前值 = -1
            ElseIf 当前值 > 索引 Then
                当前值 -= 1
            End If
        End SyncLock
        RaiseEvent 列表变化(Me, EventArgs.Empty)
    End Sub

    Public Sub 移动(原索引 As Integer, 新索引 As Integer)
        SyncLock 项目
            If 原索引 < 0 OrElse 原索引 >= 项目.Count Then Throw New ArgumentOutOfRangeException(NameOf(原索引))
            If 新索引 < 0 OrElse 新索引 >= 项目.Count Then Throw New ArgumentOutOfRangeException(NameOf(新索引))
            Interlocked.Increment(扫描版本)
            Dim 当前路径 = If(当前值 >= 0, 项目(当前值).路径, Nothing)
            Dim 值 = 项目(原索引)
            项目.RemoveAt(原索引)
            项目.Insert(新索引, 值)
            当前值 = If(当前路径 Is Nothing, -1, 项目.FindIndex(Function(x) String.Equals(x.路径, 当前路径, StringComparison.OrdinalIgnoreCase)))
        End SyncLock
        RaiseEvent 列表变化(Me, EventArgs.Empty)
    End Sub

    Public Sub 按路径顺序重排(路径顺序 As IEnumerable(Of String))
        ArgumentNullException.ThrowIfNull(路径顺序)
        Dim 新顺序 = 路径顺序.Select(Function(x) IO.Path.GetFullPath(x)).ToArray()
        SyncLock 项目
            If 新顺序.Length <> 项目.Count Then Throw New ArgumentException("新顺序必须包含播放列表中的全部项目。", NameOf(路径顺序))
            Dim 原项目 = 项目.ToDictionary(Function(x) x.路径, StringComparer.OrdinalIgnoreCase)
            If 新顺序.Distinct(StringComparer.OrdinalIgnoreCase).Count() <> 新顺序.Length OrElse
                新顺序.Any(Function(x) Not 原项目.ContainsKey(x)) Then
                Throw New ArgumentException("新顺序中的项目与当前播放列表不一致。", NameOf(路径顺序))
            End If
            Interlocked.Increment(扫描版本)
            Dim 当前路径 = If(当前值 >= 0 AndAlso 当前值 < 项目.Count, 项目(当前值).路径, Nothing)
            项目.Clear()
            项目.AddRange(新顺序.Select(Function(x) 原项目(x)))
            当前值 = If(当前路径 Is Nothing, -1,
                     项目.FindIndex(Function(x) String.Equals(x.路径, 当前路径, StringComparison.OrdinalIgnoreCase)))
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

    Public Function 选择路径(本地路径 As String) As Boolean
        If String.IsNullOrWhiteSpace(本地路径) Then Return False
        Dim 完整路径 = IO.Path.GetFullPath(本地路径)
        Dim 已变化 As Boolean
        SyncLock 项目
            Dim 索引 = 项目.FindIndex(Function(x) String.Equals(x.路径, 完整路径, StringComparison.OrdinalIgnoreCase))
            If 索引 < 0 Then Return False
            已变化 = 当前值 <> 索引
            当前值 = 索引
        End SyncLock
        If 已变化 Then RaiseEvent 列表变化(Me, EventArgs.Empty)
        Return True
    End Function

    Public Sub 清空()
        SyncLock 项目
            Interlocked.Increment(扫描版本)
            项目.Clear()
            当前值 = -1
        End SyncLock
        RaiseEvent 列表变化(Me, EventArgs.Empty)
    End Sub

    Public Sub 取消自动扫描()
        SyncLock 项目
            Interlocked.Increment(扫描版本)
        End SyncLock
    End Sub

    Public Function 从媒体扫描同目录Async(本地路径 As String,
                                      Optional 取消令牌 As CancellationToken = Nothing) As Task
        Dim 版本 As Integer
        Dim 可复用 As Boolean
        Dim 目录时间 As DateTime
        SyncLock 项目
            可复用 = 缓存版本 = 扫描版本 AndAlso
                项目.Any(Function(x) String.Equals(x.路径, 本地路径, StringComparison.OrdinalIgnoreCase))
            目录时间 = 缓存目录时间
            版本 = Interlocked.Increment(扫描版本)
        End SyncLock
        Return Task.Run(Sub()
                            If 可复用 AndAlso 复用同目录列表(本地路径, 版本, 目录时间, 取消令牌) Then Return
                            扫描同目录(本地路径, 版本, 取消令牌)
                        End Sub, 取消令牌)
    End Function

    Private Function 复用同目录列表(本地路径 As String, 版本 As Integer, 目录时间 As DateTime, 取消令牌 As CancellationToken) As Boolean
        取消令牌.ThrowIfCancellationRequested()
        If IO.Directory.GetLastWriteTimeUtc(IO.Path.GetDirectoryName(本地路径)) <> 目录时间 Then Return False
        Dim 已变化 As Boolean
        SyncLock 项目
            If 扫描版本 <> 版本 OrElse 取消令牌.IsCancellationRequested Then Return True
            Dim 索引 = 项目.FindIndex(Function(x) String.Equals(x.路径, 本地路径, StringComparison.OrdinalIgnoreCase))
            If 索引 < 0 Then Return False
            已变化 = 当前值 <> 索引
            当前值 = 索引
            缓存版本 = 版本
        End SyncLock
        If 已变化 Then RaiseEvent 列表变化(Me, EventArgs.Empty)
        Return True
    End Function

    Private Sub 扫描同目录(本地路径 As String, 版本 As Integer, 取消令牌 As CancellationToken)
        取消令牌.ThrowIfCancellationRequested()
        Dim 当前项目 As New 播放列表项(本地路径)
        Dim 当前路径 = 当前项目.路径
        Dim 新列表 As New List(Of 播放列表项) From {当前项目}
        Dim 前项 As New SortedSet(Of 播放列表项)(自动列表比较器)
        Dim 后项 As New SortedSet(Of 播放列表项)(自动列表比较器)
        Dim 目录 = IO.Path.GetDirectoryName(当前路径)
        Dim 目录时间 = IO.Directory.GetLastWriteTimeUtc(目录)
        Dim 扫描完整 = True
        Try
            For Each 文件 In IO.Directory.EnumerateFiles(目录)
                取消令牌.ThrowIfCancellationRequested()
                If Volatile.Read(扫描版本) <> 版本 Then Return
                If Not 是支持的媒体文件(文件) OrElse String.Equals(文件, 当前路径, StringComparison.OrdinalIgnoreCase) Then Continue For
                Try
                    Dim 在前 = 比较媒体路径(文件, 当前路径) < 0
                    Dim 集合 = If(在前, 前项, 后项)
                    If 集合.Count >= 自动列表最大数量 - 1 Then
                        Dim 最远 = If(在前, 集合.Min, 集合.Max)
                        Dim 比较 = 比较媒体路径(文件, 最远.路径)
                        If If(在前, 比较 <= 0, 比较 >= 0) Then Continue For
                    End If
                    Dim 候选 As New 播放列表项(文件)
                    If 集合.Count >= 自动列表最大数量 - 1 Then
                        Dim 最远 = If(在前, 集合.Min, 集合.Max)
                        集合.Remove(最远)
                    End If
                    集合.Add(候选)
                Catch ex As IOException
                    扫描完整 = False
                    Continue For
                Catch ex As UnauthorizedAccessException
                    扫描完整 = False
                    Continue For
                Catch ex As ArgumentException
                    扫描完整 = False
                    Continue For
                End Try
            Next
        Catch ex As IOException
            扫描完整 = False
        Catch ex As UnauthorizedAccessException
            扫描完整 = False
        End Try
        Using 前枚举 As IEnumerator(Of 播放列表项) = 前项.Reverse().GetEnumerator(),
              后枚举 As IEnumerator(Of 播放列表项) = DirectCast(后项, IEnumerable(Of 播放列表项)).GetEnumerator()
            Dim 有前 = 前枚举.MoveNext()
            Dim 有后 = 后枚举.MoveNext()
            While 新列表.Count < 自动列表最大数量 AndAlso (有前 OrElse 有后)
                取消令牌.ThrowIfCancellationRequested()
                If Volatile.Read(扫描版本) <> 版本 Then Return
                If 有前 Then
                    新列表.Add(前枚举.Current)
                    有前 = 前枚举.MoveNext()
                End If
                If 新列表.Count < 自动列表最大数量 AndAlso 有后 Then
                    新列表.Add(后枚举.Current)
                    有后 = 后枚举.MoveNext()
                End If
            End While
        End Using
        新列表.Sort(自动列表比较器)
        取消令牌.ThrowIfCancellationRequested()
        Dim 可缓存 = 扫描完整 AndAlso 新列表.Count < 自动列表最大数量 AndAlso
            目录时间 = IO.Directory.GetLastWriteTimeUtc(目录)
        SyncLock 项目
            If 扫描版本 <> 版本 OrElse 取消令牌.IsCancellationRequested Then Return
            项目.Clear()
            项目.AddRange(新列表)
            当前值 = 项目.FindIndex(Function(x) String.Equals(x.路径, 当前路径, StringComparison.OrdinalIgnoreCase))
            缓存版本 = If(可缓存, 版本, -1)
            缓存目录时间 = 目录时间
        End SyncLock
        RaiseEvent 列表变化(Me, EventArgs.Empty)
    End Sub

    Private Shared Function 比较媒体路径(a As String, b As String) As Integer
        Dim 结果 = 自然文件名比较器.实例.Compare(IO.Path.GetFileName(a), IO.Path.GetFileName(b))
        Return If(结果 <> 0, 结果, StringComparer.OrdinalIgnoreCase.Compare(a, b))
    End Function

    ''' <summary>图片模式：按扩展名判断是不是图片（不含视频与音频）。</summary>
    Friend Shared Function 是图片文件(路径 As String) As Boolean
        Return Not String.IsNullOrWhiteSpace(路径) AndAlso 图片扩展名.Contains(IO.Path.GetExtension(路径))
    End Function

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
        Dim 已发现 As New HashSet(Of String)(StringComparer.OrdinalIgnoreCase)
        For Each 原始行 In IO.File.ReadLines(完整列表路径, Encoding.UTF8)
            Dim 行 = 原始行.Trim()
            If 行.Length = 0 OrElse 行.StartsWith("#", StringComparison.Ordinal) Then Continue For
            If 行.Contains("://", StringComparison.Ordinal) OrElse 行.StartsWith("\\", StringComparison.Ordinal) Then Throw New InvalidDataException("M3U8 中包含被禁止的网络路径。")
            Dim 路径 = If(IO.Path.IsPathRooted(行), 行, IO.Path.Combine(目录, 行))
            Dim 值 As New 播放列表项(路径)
            If 已发现.Add(值.路径) Then 新列表.Add(值)
        Next
        SyncLock 项目
            Interlocked.Increment(扫描版本)
            Dim 当前路径 = If(当前值 >= 0 AndAlso 当前值 < 项目.Count, 项目(当前值).路径, Nothing)
            项目.Clear()
            项目.AddRange(新列表)
            当前值 = If(当前路径 Is Nothing, -1,
                     项目.FindIndex(Function(x) String.Equals(x.路径, 当前路径, StringComparison.OrdinalIgnoreCase)))
        End SyncLock
        RaiseEvent 列表变化(Me, EventArgs.Empty)
    End Sub

    Public Function 移动到播放结束后的项目() As 播放列表项
        Dim 结果 As 播放列表项 = Nothing
        Dim 当前项已变化 As Boolean
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
                    当前项已变化 = True
                Case 列表播放模式.列表循环
                    当前值 = (当前值 + 1) Mod 项目.Count
                    当前项已变化 = 项目.Count > 1
                Case 列表播放模式.随机播放
                    If 项目.Count > 1 Then
                        Dim 下一个 As Integer
                        Do
                            下一个 = Random.Shared.Next(项目.Count)
                        Loop While 下一个 = 当前值
                        当前值 = 下一个
                        当前项已变化 = True
                    End If
            End Select
            结果 = 项目(当前值)
        End SyncLock
        If 当前项已变化 Then RaiseEvent 列表变化(Me, EventArgs.Empty)
        Return 结果
    End Function

    Public Function 移动到相邻项目(方向 As Integer) As 播放列表项
        If 方向 <> -1 AndAlso 方向 <> 1 Then Throw New ArgumentOutOfRangeException(NameOf(方向))
        Dim 结果 As 播放列表项
        SyncLock 项目
            If 项目.Count = 0 Then Return Nothing
            Dim 目标索引 = If(当前值 < 0, If(方向 > 0, 0, 项目.Count - 1), 当前值 + 方向)
            If 目标索引 < 0 OrElse 目标索引 >= 项目.Count Then Return Nothing
            当前值 = 目标索引
            结果 = 项目(当前值)
        End SyncLock
        RaiseEvent 列表变化(Me, EventArgs.Empty)
        Return 结果
    End Function

    Friend Shared Function 是支持的媒体文件(路径 As String) As Boolean
        Return Not String.IsNullOrWhiteSpace(路径) AndAlso 媒体扩展名.Contains(IO.Path.GetExtension(路径))
    End Function

    Friend Shared Function 规范本地文件(值 As String) As String
        ArgumentException.ThrowIfNullOrWhiteSpace(值)
        If 值.Contains("://", StringComparison.Ordinal) OrElse 值.StartsWith("\\", StringComparison.Ordinal) OrElse 值.StartsWith("//", StringComparison.Ordinal) Then Throw New ArgumentException("只允许本地普通文件。", NameOf(值))
        Dim 完整路径 = IO.Path.GetFullPath(值)
        If Not IO.File.Exists(完整路径) Then Throw New FileNotFoundException("找不到本地文件。", 完整路径)
        If (IO.File.GetAttributes(完整路径) And IO.FileAttributes.ReparsePoint) <> 0 Then Throw New ArgumentException("不允许重解析点文件。", NameOf(值))
        Return 完整路径
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
