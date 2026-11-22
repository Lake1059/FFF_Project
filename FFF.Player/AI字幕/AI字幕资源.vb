Imports System.IO

''' <summary>单文件发布时把 AI 字幕下载器需要的少量随包资源（worker、NPU 适配、
''' 声明、许可证）嵌进宿主程序集；首次启动解到数据目录，之后直接复用。
''' 散装/普通构建没有这些嵌入资源，保留原来的 ai-subtitles 目录行为。</summary>
Friend Module AI字幕资源
    Private Const 资源前缀 As String = "FFF.Player.AiSub."

    Private ReadOnly 文件表 As String() = {
        "asr_worker.py",
        "qwen3_ort.py",
        "AI字幕包声明.txt"
    }

    Private ReadOnly 许可表 As String() = {
        "LICENSE.txt",
        "Apache-2.0.txt",
        "BSD-3-Clause.txt",
        "MIT.txt",
        "PSF-2.0.txt",
        "NOTICE.txt"
    }

    ''' <summary>把嵌入资源写到 root；已有文件不覆盖。没有嵌入资源（散装构建）时返回 False。</summary>
    Friend Function 确保(root As String) As Boolean
        If String.IsNullOrWhiteSpace(root) Then Return False
        Try
            Dim 资源目录 = Path.Combine(root, "ai-subtitles")
            Dim 许可目录 = Path.Combine(root, "licenses", "ai")
            Directory.CreateDirectory(资源目录)
            Directory.CreateDirectory(许可目录)

            Dim 有资源 = False
            For Each 名称 In 文件表
                Dim 资源名 = 资源前缀 & 名称.Replace(".", "_")
                Dim 目标 = Path.Combine(资源目录, 名称)
                If File.Exists(目标) Then
                    有资源 = True
                Else
                    有资源 = 写资源(资源名, 目标) OrElse 有资源
                End If
            Next

            For Each 名称 In 许可表
                Dim 资源名 = 资源前缀 & "License_" & 名称.Replace(".", "_")
                Dim 目标 = If(名称 = "LICENSE.txt", Path.Combine(root, "LICENSE.txt"), Path.Combine(许可目录, 名称))
                If File.Exists(目标) Then
                    有资源 = True
                Else
                    有资源 = 写资源(资源名, 目标) OrElse 有资源
                End If
            Next

            Return 有资源 AndAlso File.Exists(Path.Combine(资源目录, "asr_worker.py"))
        Catch
            Return False
        End Try
    End Function

    Private Function 写资源(资源名 As String, 目标 As String) As Boolean
        Dim 资源 = GetType(AI字幕资源).Assembly.GetManifestResourceStream(资源名)
        If 资源 Is Nothing Then Return False
        Using 资源
            Dim 临时 = 目标 & ".new"
            Try
                Directory.CreateDirectory(Path.GetDirectoryName(目标))
                Using 输出 As New FileStream(临时, FileMode.Create, FileAccess.Write, FileShare.None)
                    资源.CopyTo(输出)
                End Using
                If File.Exists(目标) Then File.Delete(目标)
                File.Move(临时, 目标)
            Finally
                Try
                    If File.Exists(临时) Then File.Delete(临时)
                Catch
                End Try
            End Try
        End Using
        Return True
    End Function
End Module
