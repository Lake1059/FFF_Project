Imports System.Text.Json

Module Module1
    Public SP_UnLock As Boolean = False

    Public JsonSO As New JsonSerializerOptions With {
    .WriteIndented = True,
    .PropertyNamingPolicy = Nothing,
    .DictionaryKeyPolicy = Nothing,
    .Encoder = System.Text.Encodings.Web.JavaScriptEncoder.UnsafeRelaxedJsonEscaping
}

End Module
