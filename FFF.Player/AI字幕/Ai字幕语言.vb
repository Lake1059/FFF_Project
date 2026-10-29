Option Strict On
Option Explicit On

''' <summary>
''' 字幕语言标识：用户选什么、worker 传给 llama-server 的完整名、怎么显示、闸门怎么判。
'''
''' 为什么需要这一层（2026-10-23 实测建立，2026-10-29 按现状修订）：
'''   * 上一版 llama-server 把 <c>language</c> 表单字段**原样拼进 prompt**，那时
'''     指定语言是"软提示"。现在两个引擎都不接受语言输入（sherpa 没这个口子，
'''     llama-server 也不再拼接），<c>language</c> 只剩一个用途：worker 拿它算
'''     <c>matched</c>，供宿主闸门使用。
'''   * 因此这个设置的真实语义是**过滤器**：按模型每个窗自报的语言标签筛。
'''     「只显示中文」对英语片意味着整片丢弃——这是字面承诺，30 秒一条的语言
'''     不符日志是用户唯一的线索；sherpa 后端自报缺失时闸门失效（ready 行
'''     lang-gate=off 有报）。界面文案必须按这个结果写，不能写"倾向/提示"。
'''   * 它**不是翻译**：Qwen3-ASR 只转写。双语对照是另一条路（双语字幕设置），
'''     与这里无关。
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

    ''' <summary>中英混说档：worker 不指定语言（模型按窗自判），闸门同时放行中文与英文。
    ''' 为什么这么设计：Qwen3-ASR 每个窗只给一个语言标签，筛中文会丢英文句、筛英文会丢
    ''' 中文句，而混说素材两种都有。"两头都放行"是唯一不对模型行为下赌注的做法。</summary>
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
            Case 自动 : Return "自动（显示所有语种）"
            Case "zh" : Return "只显示中文"
            Case "en" : Return "只显示 English"
            Case 混说 : Return "只显示中文和 English"
            Case Else : Return "自动（显示所有语种）"
        End Select
    End Function

    ''' <summary>日志与状态行的短名。显示名是结果句（"只显示中文"），拼进
    ''' "只显示 …" 这类句式会变成"只显示 只显示中文"，那种场合用这个。</summary>
    Friend Shared Function 短名(id As String) As String
        Select Case 规整(id)
            Case "zh" : Return "中文"
            Case "en" : Return "English"
            Case 混说 : Return "中文和 English"
            Case Else : Return "自动"
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
