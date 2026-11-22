Imports System.IO
Imports FFF.Player

Friend Module 内置资源回归
    Friend Function 运行() As Integer
        Dim 临时目录 = Directory.CreateTempSubdirectory("3FP-AiPack-")
        Try
            For Each NPU In {False, True}
                Dim 根 = Path.Combine(临时目录.FullName, If(NPU, "npu", "vulkan"))
                Ai字幕包资源.写入扩展包(根, NPU)
                Dim 文件名 = New List(Of String) From {
                    "asr_worker.py", "AI字幕包声明.txt", "LICENSE.txt",
                    "licenses\ai\Apache-2.0.txt", "licenses\ai\BSD-3-Clause.txt",
                    "licenses\ai\MIT.txt", "licenses\ai\NOTICE.txt", "licenses\ai\PSF-2.0.txt"
                }
                If NPU Then 文件名.Add("qwen3_ort.py")
                For Each 名称 In 文件名
                    If New FileInfo(Path.Combine(根, 名称)).Length = 0 Then Throw New IOException($"扩展包资源为空：{名称}")
                Next
                If Directory.GetFiles(根, "*", SearchOption.AllDirectories).Length <> 文件名.Count Then
                    Throw New IOException($"扩展包资源数量不符：{根}")
                End If
            Next
            Console.WriteLine("AI 字幕 Vulkan 和 NPU 扩展包资源回归通过。")
            Return 0
        Catch ex As Exception
            Console.Error.WriteLine(ex.Message)
            Return 1
        Finally
            临时目录.Delete(recursive:=True)
        End Try
    End Function
End Module
