''' <summary>GGUF 双语模式。译文在第一行，原文在第二行。</summary>
Friend NotInheritable Class Ai字幕双语
    Friend Const 关闭 As String = ""
    Friend Const 中文在上 As String = "zh-above"
    Friend Const 中文在下 As String = "zh-below"
    Friend Const 译文优先 As String = 中文在上
    Friend Shared ReadOnly 全部 As String() = {关闭, 中文在上, 中文在下}

    Friend Shared Function 规整(值 As String) As String
        Select Case If(值, String.Empty).Trim().ToLowerInvariant()
            Case 中文在上, "zh-en", "en-zh" : Return 中文在上
            Case 中文在下 : Return 中文在下
            Case Else : Return 关闭
        End Select
    End Function

    Friend Shared Function 下标(值 As String) As Integer
        Return Array.IndexOf(全部, 规整(值))
    End Function

    Friend Shared Function 已启用(值 As String) As Boolean
        Return 规整(值) <> 关闭
    End Function

    Friend Shared Function 排列结果(首行 As String, 次行 As String, 语言 As String,
                                 已按译文优先 As Boolean) As (上行 As String, 下行 As String)
        If String.IsNullOrWhiteSpace(首行) Then Return (次行, String.Empty)
        If String.IsNullOrWhiteSpace(次行) Then Return (首行, String.Empty)
        If Not 已按译文优先 Then
            ' 旧 worker 的 zh-en 输出按语言固定排序。中文原文需要从上行移到下行。
            Select Case If(语言, String.Empty).Trim().ToLowerInvariant()
                Case "zh", "chinese", "cantonese", "mandarin", "yue", "cmn", "中文", "粤语", "普通话"
                    Return (次行, 首行)
            End Select
        End If
        Return (首行, 次行)
    End Function
End Class
