Imports System.Text.Json
Imports FFF.Recorder

Friend Module Program
    <STAThread>
    Public Function Main(参数 As String()) As Integer
        If 参数.Length <> 1 OrElse Not String.Equals(参数(0), "--self-test", StringComparison.OrdinalIgnoreCase) Then
            Console.Error.WriteLine("用法：FFF.Recorder.Tests --self-test")
            Return 2
        End If

        Try
            Dim 结果 = 录制引擎.运行基础自检()
            Using 文档 = JsonDocument.Parse(结果)
                If Not 文档.RootElement.GetProperty("passed").GetBoolean() Then
                    Console.Error.WriteLine(结果)
                    Return 1
                End If
            End Using
            Console.WriteLine(结果)
            Return 0
        Catch 错误 As Exception
            Console.Error.WriteLine(错误.ToString())
            Return 1
        End Try
    End Function
End Module
