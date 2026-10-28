' 信息图层和媒体信息窗口共用输入位深口径。
' 优先显示媒体声明的采样位数，不用有损音频的解码格式推断输入位深。
Friend Module 音频采样格式化
    Friend Function 输入位深(流 As 媒体流信息) As String
        If 流 Is Nothing Then Return String.Empty
        Dim 格式 = If(流.采样格式, String.Empty).Trim().ToLowerInvariant()
        Dim 采样位数 As Integer
        Dim 浮点 As Boolean
        Select Case 格式
            Case "u8", "u8p" : 采样位数 = 8
            Case "s16", "s16p" : 采样位数 = 16
            Case "s32", "s32p" : 采样位数 = 32
            Case "s64", "s64p" : 采样位数 = 64
            Case "flt", "fltp" : 采样位数 = 32 : 浮点 = True
            Case "dbl", "dblp" : 采样位数 = 64 : 浮点 = True
        End Select
        Dim 是PCM = If(流.编码, String.Empty).StartsWith("pcm_", StringComparison.OrdinalIgnoreCase)
        Dim 是无损 = 流.无损 OrElse 流.压缩模式 = "无损" OrElse 是PCM
        Dim 可用编码位数 = 是无损 OrElse String.Equals(流.编码, "aac", StringComparison.OrdinalIgnoreCase)
        Dim 位数 = If(流.原始采样位数 > 0, 流.原始采样位数,
            If(流.位深度 > 0, 流.位深度, If(可用编码位数 AndAlso 流.编码采样位数 > 0, 流.编码采样位数,
                If(是无损, 采样位数, 0))))
        If 位数 <= 0 Then Return String.Empty
        Return $"{位数}bit" & If(是无损 AndAlso 浮点, " 浮点", String.Empty)
    End Function
End Module
