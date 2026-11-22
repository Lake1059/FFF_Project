Imports System.IO

''' <summary>将播放器内嵌的 Python 脚本和适用的许可文件写入扩展包。</summary>
Friend Module Ai字幕包资源
    Private Const 资源根 As String = "AiSubtitles.Pack/"
    Private Const Worker文件名 As String = "asr_worker.py"
    Private Const Onnx文件名 As String = "qwen3_ort.py"
    Private ReadOnly 内置Worker As New Lazy(Of String)(AddressOf 准备内置Worker)

    Friend Function 取得内置Worker() As String
        Return 内置Worker.Value
    End Function

    Friend Sub 写入扩展包(根 As String, 包含Onnx As Boolean)
        写入资源(根, Worker文件名)
        If 包含Onnx Then 写入资源(根, Onnx文件名)
        写入资源(根, "AI字幕包声明.txt")
        写入资源(根, "LICENSE.txt")
        For Each 名称 In {"Apache-2.0.txt", "BSD-3-Clause.txt", "MIT.txt", "NOTICE.txt", "PSF-2.0.txt"}
            写入资源(根, "licenses/ai/" & 名称)
        Next
    End Sub

    Private Function 准备内置Worker() As String
        Try
            Dim 本地数据 = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData)
            If String.IsNullOrEmpty(本地数据) Then Return Nothing
            Dim 版本 = GetType(Ai字幕包资源).Module.ModuleVersionId.ToString("N")
            Dim 目录 = Path.Combine(本地数据, "FFF.Player", "AI字幕", "worker", 版本)
            ' 按宿主构建隔离；多个实例同时展开时也不会读到半份脚本。
            写入资源(目录, Onnx文件名)
            写入资源(目录, Worker文件名)
            Return Path.Combine(目录, Worker文件名)
        Catch ex As Exception When TypeOf ex Is IOException OrElse TypeOf ex Is UnauthorizedAccessException
            Debug.WriteLine("无法展开内置 AI 字幕 worker，使用扩展包自己的脚本：" & ex.Message)
            Return Nothing
        End Try
    End Function

    Private Sub 写入资源(根 As String, 相对路径 As String)
        Dim 目标 = Path.Combine(根, 相对路径.Replace("/"c, Path.DirectorySeparatorChar))
        Directory.CreateDirectory(Path.GetDirectoryName(目标))
        Dim 临时文件 = 目标 & ".new-" & Guid.NewGuid().ToString("N")
        Try
            Using 输入 = GetType(Ai字幕包资源).Assembly.GetManifestResourceStream(资源根 & 相对路径)
                If 输入 Is Nothing Then Throw New IOException($"播放器缺少 AI 字幕包资源：{相对路径}")
                Using 输出 = New FileStream(临时文件, FileMode.CreateNew, FileAccess.Write)
                    输入.CopyTo(输出)
                End Using
            End Using
            File.Move(临时文件, 目标, overwrite:=True)
        Finally
            File.Delete(临时文件)
        End Try
    End Sub
End Module
