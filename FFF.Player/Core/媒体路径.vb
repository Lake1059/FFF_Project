Imports System.IO
Imports System.Threading

Friend Module 媒体路径
    Private Const 最大媒体数 As Integer = 1000
    Private Const 最大扫描条目 As Integer = 8000
    Private Const 最大目录深度 As Integer = 2
    Private ReadOnly 网络扫描槽 As New SemaphoreSlim(2, 2)

    ' 路径分类可在 UI 线程调用。共享地址的存在性由后台扫描或原生打开确认。
    Friend Function 规范网络路径(值 As String) As String
        If String.IsNullOrWhiteSpace(值) OrElse 值.Contains("://", StringComparison.Ordinal) Then Return Nothing
        Try
            Dim 输入 = 值.Replace("/"c, "\"c)
            If 输入.StartsWith("\\?\", StringComparison.Ordinal) OrElse 输入.StartsWith("\\.\", StringComparison.Ordinal) Then Return Nothing
            Dim 完整路径 = Path.TrimEndingDirectorySeparator(Path.GetFullPath(输入))
            If 完整路径.StartsWith("\\", StringComparison.Ordinal) Then
                Dim 根 = Path.GetPathRoot(完整路径)
                Dim 地址 = 根.Trim("\"c).Split("\"c)
                If 地址.Length <> 2 OrElse 地址(0).Length = 0 OrElse 地址(1).Length = 0 OrElse
                    地址(0) = "." OrElse 地址(0) = "?" OrElse 完整路径.Contains(":"c) OrElse
                    String.Equals(地址(1), "pipe", StringComparison.OrdinalIgnoreCase) OrElse
                    String.Equals(地址(1), "mailslot", StringComparison.OrdinalIgnoreCase) OrElse
                    String.Equals(地址(1), "IPC$", StringComparison.OrdinalIgnoreCase) Then Return Nothing
                Return 完整路径
            End If
            If 完整路径.Length >= 3 AndAlso 完整路径(1) = ":"c AndAlso
                New DriveInfo(完整路径.Substring(0, 3)).DriveType = DriveType.Network Then Return 完整路径
        Catch ex As Exception When TypeOf ex Is ArgumentException OrElse TypeOf ex Is IOException OrElse
                                   TypeOf ex Is NotSupportedException OrElse TypeOf ex Is UnauthorizedAccessException
        End Try
        Return Nothing
    End Function

    Friend Function 是网络路径(值 As String) As Boolean
        Return 规范网络路径(值) IsNot Nothing
    End Function

    Friend Async Function 扫描打开路径Async(路径 As String()) As Task(Of String())
        If Not 路径.Any(AddressOf 是网络路径) Then Return 展开打开路径(路径)
        Using 取消 As New CancellationTokenSource()
            Dim 令牌 = 取消.Token
            Dim 扫描 = Task.Run(Async Function()
                                  Await 网络扫描槽.WaitAsync(令牌).ConfigureAwait(False)
                                  Try
                                      Return 展开打开路径(路径, 令牌)
                                  Finally
                                      网络扫描槽.Release()
                                  End Try
                              End Function, 令牌)
            Try
                Return Await 扫描.WaitAsync(TimeSpan.FromSeconds(10))
            Catch ex As TimeoutException
                取消.Cancel()
                后台任务.观察(扫描)
                Throw
            End Try
        End Using
    End Function

    Friend Function 展开打开路径(路径 As IEnumerable(Of String),
                                Optional 取消 As CancellationToken = Nothing) As String()
        If 路径 Is Nothing Then Return Array.Empty(Of String)()
        Dim 结果 As New List(Of String)()
        Dim 已加入 As New HashSet(Of String)(StringComparer.OrdinalIgnoreCase)
        Dim 剩余条目 = 最大扫描条目
        For Each 值 In 路径
            取消.ThrowIfCancellationRequested()
            If String.IsNullOrWhiteSpace(值) Then Continue For
            Dim 候选 = 规范网络路径(值)
            Try
                If 候选 Is Nothing Then
                    Dim 本地媒体 = 光盘路径.取得媒体路径(值)
                    If 本地媒体 IsNot Nothing Then
                        If 已加入.Add(本地媒体) Then 结果.Add(本地媒体)
                        If 结果.Count >= 最大媒体数 Then Exit For
                        Continue For
                    End If
                    候选 = Path.GetFullPath(值)
                    If 候选.StartsWith("\\", StringComparison.Ordinal) Then Continue For
                End If
                Dim 属性 = File.GetAttributes(候选)
                If (属性 And FileAttributes.ReparsePoint) <> 0 Then Continue For
                If (属性 And FileAttributes.Directory) <> 0 Then
                    Dim 目录文件 As New List(Of String)()
                    收集目录媒体(候选, 0, 目录文件, 剩余条目, 最大媒体数 - 结果.Count, 取消)
                    目录文件.Sort(StringComparer.CurrentCultureIgnoreCase)
                    For Each 文件 In 目录文件
                        If 已加入.Add(文件) Then 结果.Add(文件)
                    Next
                ElseIf 已加入.Add(候选) Then
                    结果.Add(候选)
                End If
            Catch ex As Exception When TypeOf ex Is ArgumentException OrElse TypeOf ex Is IOException OrElse
                                       TypeOf ex Is NotSupportedException OrElse TypeOf ex Is UnauthorizedAccessException
            End Try
            If 结果.Count >= 最大媒体数 OrElse 剩余条目 <= 0 Then Exit For
        Next
        Return 结果.ToArray()
    End Function

    Private Sub 收集目录媒体(目录 As String, 深度 As Integer, 文件 As List(Of String),
                              ByRef 剩余条目 As Integer, 最大结果数 As Integer, 取消 As CancellationToken)
        If String.Equals(Path.GetFileName(目录), "BDMV", StringComparison.OrdinalIgnoreCase) Then Return
        Dim 选项 As New EnumerationOptions With {
            .AttributesToSkip = FileAttributes.ReparsePoint, .IgnoreInaccessible = True}
        Try
            For Each 条目 In New DirectoryInfo(目录).EnumerateFileSystemInfos("*", 选项)
                取消.ThrowIfCancellationRequested()
                剩余条目 -= 1
                If 剩余条目 < 0 OrElse 文件.Count >= 最大结果数 Then Exit For
                If (条目.Attributes And FileAttributes.Directory) <> 0 Then
                    If 深度 < 最大目录深度 Then 收集目录媒体(条目.FullName, 深度 + 1, 文件, 剩余条目, 最大结果数, 取消)
                ElseIf 播放列表.是支持的媒体文件(条目.Name) Then
                    文件.Add(条目.FullName)
                End If
            Next
        Catch ex As Exception When TypeOf ex Is IOException OrElse TypeOf ex Is UnauthorizedAccessException
        End Try
    End Sub
End Module
