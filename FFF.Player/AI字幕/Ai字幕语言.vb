Option Strict On
Option Explicit On

''' <summary>
''' 字幕语言标识：用户选什么、worker 传给 llama-server 的完整名、怎么显示、闸门怎么判。
'''
''' 为什么需要这一层（2026-10-23 在 b11669 + Qwen3-ASR-0.6B 上实测）：
'''   * llama-server 的 <c>/v1/audio/transcriptions</c> 接受 <c>language</c> 表单字段，
'''     但值是**原样拼进 prompt 的文本**（同一段音频 input_tokens 从 80 → 85）。于是：
'''       - 必须给模型认识的**完整英文名**（"Chinese"）：
'''         <c>language=Chinese</c> 能把英文音频的标签改成 Chinese；
'''       - 给 ISO 代码（<c>language=zh</c>）**不生效**，输出标签仍是模型自判的 English；
'''         <c>language=Klingon</c> 同样被忽略。宿主这边因此不能把 "zh" 直接丢给 worker。
'''   * 它是**软提示不是硬约束**：中文音频 + <c>language=English</c> 仍输出
'''     <c>language Chinese</c>（模型确信时提示压不住）。所以"只要所选语言"必须由
'''     宿主闸门（<see cref="应丢弃"/>）另外保证。
'''   * 它**不是翻译**：Qwen3-ASR 只转写。指定与片源不同的语言，得到的是
'''     "标签被改 + 正文仍是原语言"，还可能把英文强行音译成汉字。设置页文案必须说清。
'''
''' 语言清单与英文名取自模型卡（Qwen3-ASR-0.6B README）的 30 种语言；中文方言
''' （四川话/粤语港腔等 22 种）暂不列出——它们的提示词写法没有实证，宁可先不给。
'''
''' 界面只暴露 4 个主项（自动 / 中文 / English / 中英混说），其余语种**不上设置面板**
''' （2026-10-23 用户裁定"未验证的内容不应该出现"）：模型卡上见过名字 ≠ 真机验证过，
''' 摆在下拉里只会让人选了之后对不上号。<see cref="全部"/> 整表保留下来只为两件事：
''' check_l0.py 与 worker LANGUAGE_NAMES 的防漂移对比、以及后续逐语种验证后重新开面板。
''' </summary>
Friend NotInheritable Class Ai字幕语言
    Friend Const 自动 As String = "auto"

    ''' <summary>中英混说档：**不传 language 提示**（模型自判），但闸门同时放行中文与英文。
    ''' 为什么这么设计：Qwen3-ASR 每个窗只给一个语言标签，锁中文会把英文句子音译成汉字、
    ''' 锁英文会把中文音译成字母，而混说素材两种都不对。"两头都放行、提示不干预"
    ''' 是唯一不需要对模型行为下赌注的做法。</summary>
    Friend Const 混说 As String = "zh-en"

    ''' <summary>菜单顶部的一等公民：自动 / 中文 / English / 中英混说。
    ''' 设置面板的语言下拉只列这 4 项（都经过真机验证）。</summary>
    Friend Shared ReadOnly 主项 As String() = {自动, "zh", "en", 混说}

    ''' <summary>整表 32 项：前 4 个是主项，其余 28 个是"模型卡上见过、未真机验证"的语种。
    ''' **界面不暴露整表**（用户裁定未验证内容不出现），这里保留只为两件事：
    ''' check_l0.py / state-machine-test 与 worker LANGUAGE_NAMES 的防漂移对比，
    ''' 以及后续逐语种验证后重新开面板。<see cref="规整"/> 只认主项，不认其余 28 个。</summary>
    Friend Shared ReadOnly 全部 As String() = {
        自动, "zh", "en", 混说,
        "yue", "ja", "ko", "fr", "de", "es", "ru", "pt", "it", "ar",
        "th", "vi", "id", "ms", "tr", "hi", "nl", "pl", "sv", "da",
        "fi", "cs", "el", "hu", "ro", "fil", "fa", "mk"}

    ''' <summary>模型认的英文名（worker 侧必须用这个，不能用 ISO 代码 —— 实测 ISO 无效）。
    ''' 中英混说档**故意不在表里**：它不传提示，闸门另有逻辑（见 <see cref="允许集"/>）。
    ''' 整表与 worker LANGUAGE_NAMES 逐项对比（check_l0.py L0-1）防漂移；非主项的英文名
    ''' 目前没有调用方（<see cref="规整"/> 只认主项），留给后续逐语种验证后重新开面板。</summary>
    Private Shared ReadOnly 名称表 As New Dictionary(Of String, String)(StringComparer.Ordinal) From {
        {"zh", "Chinese"}, {"en", "English"}, {"yue", "Cantonese"},
        {"ja", "Japanese"}, {"ko", "Korean"}, {"fr", "French"},
        {"de", "German"}, {"es", "Spanish"}, {"ru", "Russian"},
        {"pt", "Portuguese"}, {"it", "Italian"}, {"ar", "Arabic"},
        {"th", "Thai"}, {"vi", "Vietnamese"}, {"id", "Indonesian"},
        {"ms", "Malay"}, {"tr", "Turkish"}, {"hi", "Hindi"},
        {"nl", "Dutch"}, {"pl", "Polish"}, {"sv", "Swedish"},
        {"da", "Danish"}, {"fi", "Finnish"}, {"cs", "Czech"},
        {"el", "Greek"}, {"hu", "Hungarian"}, {"ro", "Romanian"},
        {"fil", "Filipino"}, {"fa", "Persian"}, {"mk", "Macedonian"}
    }

    ''' <summary>未知值、空值、null 一律归到 auto —— 老设置文件和手写脏数据都往这里落。
    ''' 刻意只认 <see cref="主项"/>：未验证语种的 id（ja/yue/…，可能是老设置文件或手改
    ''' json 残留）也归 auto。否则会出现"界面显示自动、实际锁着日语"的隐蔽不一致——
    ''' 面板上明明没有那一项，行为却还跟着它走。</summary>
    Friend Shared Function 规整(值 As String) As String
        If String.IsNullOrWhiteSpace(值) Then Return 自动
        值 = 值.Trim()
        For Each id In 主项
            If String.Equals(值, id, StringComparison.OrdinalIgnoreCase) Then Return id
        Next
        Return 自动
    End Function

    ''' <summary>下拉显示名。面板只列主项，非主项 id 已被 <see cref="规整"/> 归到 auto，
    ''' 所以这里只需要处理 4 个主项的显示。</summary>
    Friend Shared Function 显示名(id As String) As String
        Return 基显示名(规整(id))
    End Function

    ''' <summary>显示名表。只收主项 —— 未验证语种不上面板，也就不该有显示名。</summary>
    Private Shared Function 基显示名(id As String) As String
        Select Case id
            Case 自动 : Return "自动（让模型自判）"
            Case "zh" : Return "中文（Chinese）"
            Case "en" : Return "English"
            Case 混说 : Return "中文 · English（中英混说）"
            Case Else : Return "自动（让模型自判）"
        End Select
    End Function

    ''' <summary>模型认的英文名；auto / 中英混说 / 未知都返回空串（= 不传 language 字段）。</summary>
    Friend Shared Function 规范名(id As String) As String
        Dim 值 As String = Nothing
        If 名称表.TryGetValue(规整(id), 值) Then Return 值
        Return String.Empty
    End Function

    ''' <summary>worker 侧 <c>--language</c> 的取值：完整英文名或 "auto"。
    ''' 中英混说走这里会得到 "auto" —— 这正是想要的：不给提示，让模型按窗自判。</summary>
    Friend Shared Function 传参值(id As String) As String
        Dim 名 = 规范名(id)
        Return If(名.Length = 0, 自动, 名)
    End Function

    ''' <summary>设置项是否"已指定语言"（用于决定闸门开关是否有意义）。</summary>
    Friend Shared Function 已指定(id As String) As Boolean
        Return 规整(id) <> 自动
    End Function

    ''' <summary>这个设置放行哪些语言标签（小写英文名）。空数组 = 不按语言筛。
    ''' 只有中英混说需要多个：worker 每个窗只报一个标签，单语言档交给 worker 的 matched。</summary>
    Friend Shared Function 允许集(id As String) As String()
        Dim 键 = 规整(id)
        If 键 = 混说 Then Return {"chinese", "english"}
        Dim 名 = 规范名(键)
        If 名.Length = 0 Then Return Array.Empty(Of String)()
        Return {名.ToLowerInvariant()}
    End Function

    ''' <summary>
    ''' 闸门判定：这一窗要不要丢。
    '''
    ''' 方向刻意做成"保守"——这个分支判错的后果是**整片空白且没有任何提示**
    ''' （和 P0 那个只置位不复位的标志同一类）：
    '''   * <paramref name="仅所选"/> 为 False → 永远不丢（默认：宁可看到语言错的字幕，
    '''     也好过屏上什么都没有）；
    '''   * 语言是"自动" → 不丢（没有目标可言）；
    '''   * 中英混说 → 看**模型自报的标签**是否在 {chinese, english} 里；标签缺失则不丢；
    '''   * 其余单语言 → 用 worker 算好的 <paramref name="matched"/>。
    '''
    ''' 单独抽成纯函数只为一件事：它能在不实例化整个管道的情况下被单元测试覆盖
    ''' （管道依赖 Process / 设置.实例对象 / WinForms，跑不起来）。
    ''' </summary>
    Friend Shared Function 应丢弃(实际语言 As String, matched As Boolean,
                                 语言id As String, 仅所选 As Boolean) As Boolean
        If Not 仅所选 Then Return False
        Dim 键 = 规整(语言id)
        If Not 已指定(键) Then Return False

        If 键 = 混说 Then
            Dim 名 = If(实际语言, String.Empty).Trim().ToLowerInvariant()
            If 名.Length = 0 Then Return False
            For Each 允许 In 允许集(键)
                If 名.StartsWith(允许, StringComparison.Ordinal) OrElse
                   允许.StartsWith(名, StringComparison.Ordinal) Then Return False
            Next
            Return True
        End If

        Return Not matched
    End Function
End Class
