Imports System.Text.Json

Module Module1
    Public SP_UnLock As Boolean = False
    Public ReadOnly 程序路径 As String = 获取程序路径()
    Public ReadOnly 程序目录 As String = IO.Path.GetDirectoryName(程序路径)
    Public ReadOnly 宿主目录 As String = 宿主路径.解析宿主目录(程序目录)
    Public ReadOnly 数据目录 As String = 宿主路径.解析数据目录(宿主目录, 程序目录)

    Public JsonSO As New JsonSerializerOptions With {
    .WriteIndented = True,
    .PropertyNamingPolicy = Nothing,
    .DictionaryKeyPolicy = Nothing,
    .Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping
}

    Private Function 获取程序路径() As String
        Dim executablePath = Environment.ProcessPath
        If Not String.IsNullOrWhiteSpace(executablePath) Then Return IO.Path.GetFullPath(executablePath)
        Return IO.Path.GetFullPath(Application.ExecutablePath)
    End Function
End Module
