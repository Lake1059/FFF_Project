Option Strict On
Option Explicit On

''' <summary>
''' 双语对照档：一次识别同时出两行字幕，上行一种语言、下行另一种。
'''
''' 为什么这件事不需要第二个模型（2026-10-29 实测）：
'''   * Qwen3-ASR 是**纯转写**权重 —— <c>language</c> 与 <c>prompt</c> 都改不动它的
'''     输出（基线 / language=English / prompt=要求翻译 三组逐字相同）；把它喂纯文本，
'''     它照样回 <c>language X&lt;asr_text&gt;</c> + 原样文本，底座文本能力看着像没了。
'''   * 但那是**chat 模板**锁的：换成 llama-server 的原始 <c>/completion</c> 续写，
'''     同一份权重就能中英互译（0.08–0.19 s/句），且与 ASR 交替调用互不污染
'''     （三轮 ASR 结果逐字一致）。所以双语 = 同一进程多打一个请求，权重一个字节不改。
'''   * ONNX（NPU / Intel）那条腿是纯 ASR，做不到；worker 会降级成单语并报 warning。
'''
''' 行的语义是**位置**不是来源：<c>zh-en</c> 表示"上行必须中文、下行必须英文"。
''' 识别出来是中文就留在上行、下行补译文；识别出来是英文就反过来 —— 否则用户换了
''' 语种的片子，"中文在上"就悄悄失效了。
''' </summary>
Friend NotInheritable Class Ai字幕双语
    Friend Const 关闭 As String = ""
    Friend Const 中上英下 As String = "zh-en"
    Friend Const 英上中下 As String = "en-zh"

    ''' <summary>界面暴露的三项。第三种语言（日/韩等）不在档里：模型自报第三种语言时
    ''' 两行都要翻译，那是这一档的内部行为，不是用户要选的模式。</summary>
    Friend Shared ReadOnly 全部 As String() = {关闭, 中上英下, 英上中下}

    ''' <summary>脏值一律归"关闭"：老设置文件没有这个键（默认空串），手改 json 写错
    ''' 也从关闭兜底 —— 双语会多占一行画面，静默打开比静默关掉更糟。</summary>
    Friend Shared Function 规整(值 As String) As String
        If String.IsNullOrWhiteSpace(值) Then Return 关闭
        值 = 值.Trim()
        For Each id In 全部
            If String.Equals(值, id, StringComparison.OrdinalIgnoreCase) Then Return id
        Next
        Return 关闭
    End Function

    Friend Shared Function 已启用(值 As String) As Boolean
        Return 规整(值) <> 关闭
    End Function

    Friend Shared Function 显示名(值 As String) As String
        Select Case 规整(值)
            Case 中上英下 : Return "双语：中文在上 · English 在下"
            Case 英上中下 : Return "双语：English 在上 · 中文在下"
            Case Else : Return "关闭（只出一行）"
        End Select
    End Function

    ''' <summary>菜单里的短名。子菜单要一眼看出选的是哪一档，不能整行都是长描述。</summary>
    Friend Shared Function 短名(值 As String) As String
        Select Case 规整(值)
            Case 中上英下 : Return "中文在上 · English 在下"
            Case 英上中下 : Return "English 在上 · 中文在下"
            Case Else : Return "关闭"
        End Select
    End Function

    ''' <summary>worker 的 <c>--bilingual</c> 取值，与档名一一对应（关闭时传空串）。</summary>
    Friend Shared Function 传参值(值 As String) As String
        Return 规整(值)
    End Function
End Class
