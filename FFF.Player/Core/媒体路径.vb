Imports System.IO
Imports System.Security
Imports System.Threading

''' <summary>
''' 媒体路径分类与文件夹展开。
''' 路径分类只在这里做一次，避免“任意目录都当光盘”和“网络路径一律拒绝”两套判断继续分叉。
''' </summary>
Public Module 媒体路径
    ''' <summary>规范化后是 UNC 路径（不以盘符或卷挂载点开头）。</summary>
    Friend Function 是网络路径(路径 As String) As Boolean
        If String.IsNullOrWhiteSpace(路径) Then Return False
        Dim 完整路径 As String
        Try
            完整路径 = IO.Path.GetFullPath(路径)
        Catch
            完整路径 = 路径.Trim()
        End Try
        Return 完整路径.StartsWith("\\", StringComparison.Ordinal) OrElse
              完整路径.StartsWith("//", StringComparison.Ordinal)
    End Function

    ''' <summary>
    ''' 只认真正的 Blu-ray 布局：.bdmv 文件、名为 BDMV 的目录，或包含 BDMV 子目录的根目录。
    ''' 普通文件夹不再被当成光盘；BDMV 入口本身是重解析点时也不认。
    ''' </summary>
    Friend Function 是蓝光光盘路径(路径 As String) As Boolean
        If String.IsNullOrWhiteSpace(路径) Then Return False
        Try
            If IO.File.Exists(路径) Then
                Return String.Equals(IO.Path.GetExtension(路径), ".bdmv", StringComparison.OrdinalIgnoreCase) AndAlso
                       Not 是重解析点(路径)
            End If
            If Not IO.Directory.Exists(路径) Then Return False
            Dim 修整路径 = 路径.TrimEnd(IO.Path.DirectorySeparatorChar, IO.Path.AltDirectorySeparatorChar)
            Dim 目录名 = IO.Path.GetFileName(修整路径)
            If String.Equals(目录名, "BDMV", StringComparison.OrdinalIgnoreCase) Then
                Return Not 是重解析点(路径)
            End If
            Dim BDMV目录 = IO.Path.Combine(路径, "BDMV")
            Return IO.Directory.Exists(BDMV目录) AndAlso Not 是重解析点(BDMV目录)
        Catch
            Return False
        End Try
    End Function

    ''' <summary>文件或目录带重解析点属性时返回 True；不跟随 junction/符号链接。</summary>
    Friend Function 是重解析点(路径 As String) As Boolean
        If String.IsNullOrWhiteSpace(路径) Then Return False
        Try
            Dim 信息 As New IO.FileInfo(路径)
            If 信息.Exists Then Return (信息.Attributes And IO.FileAttributes.ReparsePoint) <> 0
            Dim 目录信息 As New IO.DirectoryInfo(路径)
            If 目录信息.Exists Then Return (目录信息.Attributes And IO.FileAttributes.ReparsePoint) <> 0
        Catch
            ' 属性读取失败时按“可能是重解析点”处理，宁可少扫一个目录，
            ' 也不要在 SMB 属性不可靠时把递归带出用户选择的根。
            Return True
        End Try
        Return False
    End Function

    ''' <summary>
    ''' 有界枚举文件夹中的媒体。默认最多 1000 项、向下两层；网络目录不会无限递归。
    ''' 不进入重解析点目录，避免 junction/符号链接把扫描带出用户选择的根目录。
    ''' </summary>
    Friend Function 枚举媒体文件(文件夹 As String, Optional 最大数量 As Integer = 1000,
                                  Optional 最大深度 As Integer = 2,
                                  Optional 取消 As CancellationToken = Nothing) As IEnumerable(Of String)
        Dim 结果 As New List(Of String)
        If String.IsNullOrWhiteSpace(文件夹) OrElse 最大数量 <= 0 Then Return 结果
        If 是重解析点(文件夹) Then Return 结果
        ' 光盘目录由主窗口的光盘路径接管，播放列表不应把 BDMV/STREAM 里的
        ' 单个 m2ts 当成普通媒体文件加入。
        If 是蓝光光盘路径(文件夹) Then Return 结果
        Try
            Dim 已扫描 As Integer = 0
            收集媒体文件(IO.Path.GetFullPath(文件夹), 0, Math.Max(0, 最大深度), 最大数量, 结果, 已扫描, 取消)
        Catch ex As OperationCanceledException
            Throw
        Catch
            ' 根目录不可读时返回空集合，由调用方按“没有可播放媒体”处理。
        End Try
        结果.Sort(Function(a, b) StringComparer.CurrentCultureIgnoreCase.Compare(
                      IO.Path.GetFileName(a), IO.Path.GetFileName(b)))
        If 取消.IsCancellationRequested Then Throw New OperationCanceledException(取消)
        Return 结果
    End Function

    Private Sub 收集媒体文件(目录 As String, 深度 As Integer, 最大深度 As Integer,
                             最大数量 As Integer, 结果 As List(Of String),
                             ByRef 已扫描 As Integer, 取消 As CancellationToken)
        取消.ThrowIfCancellationRequested()
        If 结果.Count >= 最大数量 Then Return
        Dim 最大条目 = Math.Max(最大数量 * 8, 4096)
        Try
            For Each 文件 In IO.Directory.EnumerateFiles(目录, "*", IO.SearchOption.TopDirectoryOnly)
                取消.ThrowIfCancellationRequested()
                已扫描 += 1
                If 已扫描 > 最大条目 Then Return
                If 结果.Count >= 最大数量 Then Return
                If 播放列表.是支持的媒体文件(文件) Then 结果.Add(IO.Path.GetFullPath(文件))
            Next
        Catch ex As OperationCanceledException
            Throw
        Catch ex As UnauthorizedAccessException
        Catch ex As IO.IOException
        Catch ex As SecurityException
        End Try

        If 深度 >= 最大深度 Then Return
        Try
            For Each 子目录 In IO.Directory.EnumerateDirectories(目录, "*", IO.SearchOption.TopDirectoryOnly)
                取消.ThrowIfCancellationRequested()
                已扫描 += 1
                If 已扫描 > 最大条目 Then Return
                If 结果.Count >= 最大数量 Then Return
                ' 目录 junction/符号链接可能逃出根目录，直接跳过。
                If 是重解析点(子目录) Then Continue For
                收集媒体文件(子目录, 深度 + 1, 最大深度, 最大数量, 结果, 已扫描, 取消)
            Next
        Catch ex As OperationCanceledException
            Throw
        Catch ex As UnauthorizedAccessException
        Catch ex As IO.IOException
        Catch ex As SecurityException
        End Try
    End Sub

    ''' <summary>
    ''' 把主窗口收到的拖入项展开成可打开路径：文件原样返回；蓝光目录原样返回；
    ''' 普通目录展开为其下的媒体文件。这样文件夹不会再被送进 bd_open。
    ''' </summary>
    Friend Function 展开打开路径(路径 As IEnumerable(Of String), Optional 最大数量 As Integer = 1000,
                                  Optional 最大深度 As Integer = 2,
                                  Optional 取消 As CancellationToken = Nothing) As IEnumerable(Of String)
        Dim 结果 As New List(Of String)
        If 路径 Is Nothing OrElse 最大数量 <= 0 Then Return 结果
        For Each 值 In 路径
            取消.ThrowIfCancellationRequested()
            If String.IsNullOrWhiteSpace(值) Then Continue For
            Try
                If IO.File.Exists(值) Then
                    结果.Add(IO.Path.GetFullPath(值))
                ElseIf IO.Directory.Exists(值) Then
                    ' 根目录本身若是 junction/符号链接，不自动跟随；用户需要选真实目录。
                    If 是重解析点(值) Then Continue For
                    If 是蓝光光盘路径(值) Then
                        结果.Add(IO.Path.GetFullPath(值))
                    Else
                        For Each 文件 In 枚举媒体文件(值, 最大数量 - 结果.Count, Math.Max(0, 最大深度), 取消)
                            结果.Add(文件)
                            If 结果.Count >= 最大数量 Then Exit For
                        Next
                    End If
                End If
            Catch ex As OperationCanceledException
                Throw
            Catch
                ' 单个输入不可解析时继续处理下一项。
            End Try
            If 结果.Count >= 最大数量 Then Exit For
        Next
        Return 结果
    End Function
End Module
