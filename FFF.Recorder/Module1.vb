Imports System.Text.Json

Module Module1
    Public SP_UnLock As Boolean = False
    Public ReadOnly 程序目录 As String = 获取程序目录()

    Public JsonSO As New JsonSerializerOptions With {
    .WriteIndented = True,
    .PropertyNamingPolicy = Nothing,
    .DictionaryKeyPolicy = Nothing,
    .Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping
}

    Private Function 获取程序目录() As String
        Dim executablePath = Environment.ProcessPath
        If Not String.IsNullOrWhiteSpace(executablePath) Then
            Dim directory = IO.Path.GetDirectoryName(executablePath)
            If Not String.IsNullOrWhiteSpace(directory) Then Return directory
        End If
        Return Application.StartupPath
    End Function

End Module
