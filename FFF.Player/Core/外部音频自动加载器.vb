Imports System.IO
Imports System.Threading

''' <summary>扫描与视频同名的 MKA 外挂音频。</summary>
Public NotInheritable Class 外部音频自动加载器
    Private Sub New()
    End Sub

    Public Shared Function 是支持的音频文件(路径 As String) As Boolean
        Return Not String.IsNullOrWhiteSpace(路径) AndAlso
            String.Equals(Path.GetExtension(路径), ".mka", StringComparison.OrdinalIgnoreCase)
    End Function

    Public Shared Function 扫描同名音频Async(媒体路径 As String,
                                      取消令牌 As CancellationToken) As Task(Of IReadOnlyList(Of String))
        ArgumentException.ThrowIfNullOrWhiteSpace(媒体路径)
        Return Task.Run(Function() 扫描同名音频(媒体路径, 取消令牌), 取消令牌)
    End Function

    ''' <summary>
    ''' 扫描媒体所在目录内的同名 MKA。除完全同名文件外，也接受
    ''' “媒体名.语言/版本.mka”的常见命名。
    ''' </summary>
    Public Shared Function 扫描同名音频(媒体路径 As String,
                                  Optional 取消令牌 As CancellationToken = Nothing) As IReadOnlyList(Of String)
        ArgumentException.ThrowIfNullOrWhiteSpace(媒体路径)
        Dim 完整媒体路径 = Path.GetFullPath(媒体路径)
        Dim 目录 = Path.GetDirectoryName(完整媒体路径)
        If String.IsNullOrEmpty(目录) OrElse Not Directory.Exists(目录) Then
            Return Array.Empty(Of String)()
        End If

        Dim 媒体名 = Path.GetFileNameWithoutExtension(完整媒体路径)
        Dim 带分隔符前缀 = 媒体名 & "."
        Dim 结果 As New List(Of String)()
        Dim 已发现路径 As New HashSet(Of String)(StringComparer.OrdinalIgnoreCase)

        For Each 文件路径 In Directory.EnumerateFiles(目录)
            取消令牌.ThrowIfCancellationRequested()
            If Not 是支持的音频文件(文件路径) Then Continue For

            Dim 音频名 = Path.GetFileNameWithoutExtension(文件路径)
            If Not String.Equals(音频名, 媒体名, StringComparison.OrdinalIgnoreCase) AndAlso
                Not 音频名.StartsWith(带分隔符前缀, StringComparison.OrdinalIgnoreCase) Then Continue For

            Dim 完整音频路径 = Path.GetFullPath(文件路径)
            If 已发现路径.Add(完整音频路径) Then 结果.Add(完整音频路径)
        Next

        Return 结果.OrderBy(Function(x) If(
                String.Equals(Path.GetFileNameWithoutExtension(x), 媒体名, StringComparison.OrdinalIgnoreCase), 0, 1)).
            ThenBy(Function(x) Path.GetFileName(x), StringComparer.OrdinalIgnoreCase).
            ToArray()
    End Function
End Class
