Imports System.IO

''' <summary>宿主目录/数据目录解析：外层 bootstrapper 把内层 exe 放进
''' 3FP.runtime\payload 后，用它把 Modules、设置、图标和 FFmpeg 搜索路径
''' 指回原程序目录，避免这些随缓存目录漂移。</summary>
Module 宿主路径
    Public Const 宿主目录环境变量 As String = "FFF3FP_HOST_DIR"
    Public Const 数据目录环境变量 As String = "FFF_PLAYER_DATA_DIR"

    ''' <summary>显式 FFF3FP_HOST_DIR 优先；否则只识别
    ''' &lt;宿主&gt;\3FP.runtime\payload 这一种受控缓存布局，向上两级取宿主。
    ''' 不做通用的 ..\.. 猜测，避免普通目录把无关父目录带进来。</summary>
    Public Function 解析宿主目录(程序目录 As String) As String
        Dim 指定 = 环境值(宿主目录环境变量)
        If Not String.IsNullOrWhiteSpace(指定) Then
            Try
                Return Path.GetFullPath(指定.Trim().Trim(""""c))
            Catch
                Return 程序目录
            End Try
        End If

        Dim 进程目录 = Path.GetDirectoryName(Environment.ProcessPath)
        If String.IsNullOrWhiteSpace(进程目录) Then Return 程序目录
        Try
            Dim 完整 = Path.GetFullPath(进程目录).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
            Dim 叶 = Path.GetFileName(完整)
            Dim 父目录 = Path.GetDirectoryName(完整)
            If String.IsNullOrWhiteSpace(父目录) Then Return 程序目录
            Dim 父名 = Path.GetFileName(父目录)
            If String.Equals(叶, "payload", StringComparison.OrdinalIgnoreCase) AndAlso
               String.Equals(父名, "3FP.runtime", StringComparison.OrdinalIgnoreCase) Then
                Return Path.GetDirectoryName(父目录)
            End If
        Catch
        End Try
        Return 程序目录
    End Function

    ''' <summary>FFF_PLAYER_DATA_DIR 优先；否则沿用宿主目录。普通散装布局下
    ''' 宿主目录就是程序目录，行为不变。</summary>
    Public Function 解析数据目录(宿主目录 As String, 程序目录 As String) As String
        Dim 指定 = 环境值(数据目录环境变量)
        If Not String.IsNullOrWhiteSpace(指定) Then
            Try
                Return Path.GetFullPath(指定.Trim().Trim(""""c))
            Catch
                Return 宿主目录
            End Try
        End If
        If Not String.IsNullOrWhiteSpace(宿主目录) Then Return 宿主目录
        Return 程序目录
    End Function

    ''' <summary>把宿主目录预置到 PATH 最前，供 FFF.Native 的 delay-load
    ''' 和托管侧 NativeLibrary 探测找到原程序目录里的 Shared FFmpeg。
    ''' 保留原 PATH，不替换；已存在时不重复加。</summary>
    Public Sub 准备搜索路径(宿主目录 As String)
        If String.IsNullOrWhiteSpace(宿主目录) Then Return
        Try
            Dim 完整 = Path.GetFullPath(宿主目录).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
            Dim 原值 = Environment.GetEnvironmentVariable("PATH")
            If Not String.IsNullOrWhiteSpace(原值) Then
                For Each 段 In 原值.Split(Path.PathSeparator)
                    Dim 候选 = 段.Trim().Trim(""""c)
                    If String.IsNullOrWhiteSpace(候选) Then Continue For
                    Try
                        Dim 规范候选 = Path.GetFullPath(候选).TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
                        If String.Equals(规范候选, 完整, StringComparison.OrdinalIgnoreCase) Then Return
                    Catch
                    End Try
                Next
            End If
            Environment.SetEnvironmentVariable("PATH", 完整 & Path.PathSeparator & If(String.IsNullOrEmpty(原值), "", 原值))
        Catch
        End Try
    End Sub

    Private Function 环境值(名称 As String) As String
        Try
            Return Environment.GetEnvironmentVariable(名称)
        Catch
            Return Nothing
        End Try
    End Function
End Module
