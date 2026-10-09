Imports System.Text

' 内嵌歌词提取器：从媒体文件的**标签**里读歌词。
'
' 为什么需要单独一层：歌词在容器里有**多种互不相同的承载方式**，各家工具对
' 键名大小写、挂载层级、是否带时间戳的处理都不一致。实测（本地素材包的 16 件样本
' 测试包 16 件）需要同时容忍以下三根轴：
'
' ① 键名与大小写
'    LYRICS / SYNCEDLYRICS / UNSYNCEDLYRICS / lyrics（小写）/ lyrics（m4a 映射后）
'    / syncedlyrics（mp3 落成 TXXX）/ COMMENT（有人把词塞这里）
'    ⇒ 必须**大小写不敏感**、按优先级逐个尝试，不能只认大写一种。
'
' ② 挂载层级
'    .opus 的歌词在**流级**（ffprobe -show_format 为空）；
'    .mka/.flac/.mp3/.m4a 在**容器级**。
'    ⇒ 两级都要查，且容器级优先（更具体的那份通常是被主流工具写出来的）。
'
' ③ 是否带时间戳
'    SYNCEDLYRICS / USLT / ©lyr 通常带 [mm:ss.xx]；UNSYNCEDLYRICS 是纯文本。
'    ⇒ 带时间轴的直接解析；纯文本的**不能当同步词用**，否则整首歌词会挤在一行。
'      纯文本只作为"有词但无法同步"的降级资料保留。
'
' 另外：BOM。ffmpeg 系的 LRC 解析器会**静默丢掉带 BOM 的首行**（本机实测 12→11 行）。
' 我们自己做剥离，所以不受影响——但要**先剥再解析**，且只剥行首那一个。
Friend NotInheritable Class 内嵌歌词提取器
    Private Sub New()
    End Sub

    ' 带时间戳的键，按优先级排列。匹配时大小写不敏感。
    Private Shared ReadOnly 同步键 As String() = {
        "LYRICS", "SYNCEDLYRICS", "SYNCED_LYRICS", "UNSYNCEDLYRICS", "SYNCEDLYRIC",
        "\u00A9lyr", "clyr", "USLT", "LYRIC"
    }

    ' 可能藏歌词但不是专用键的位置（放在最后，权重最低）。
    Private Shared ReadOnly 兜底键 As String() = {"COMMENT", "DESCRIPTION"}

    ''' <summary>提取结果：能同步的、以及"有词但无时间轴"的纯文本。</summary>
    Friend NotInheritable Class 提取结果
        Friend Sub New(资料 As LRC歌词资料, 纯文本 As String, 来源键 As String, 来自流级 As Boolean)
            Me.资料 = 资料
            Me.纯文本 = If(纯文本, String.Empty)
            Me.来源键 = If(来源键, String.Empty)
            Me.来自流级 = 来自流级
        End Sub

        ''' <summary>可同步的歌词；纯文本载体时为 Nothing。</summary>
        Friend ReadOnly Property 资料 As LRC歌词资料
        ''' <summary>无时间轴的纯文本歌词（用于显示"有词但不能同步"）。</summary>
        Friend ReadOnly Property 纯文本 As String
        ''' <summary>命中的键名，便于日志/诊断。</summary>
        Friend ReadOnly Property 来源键 As String
        ''' <summary>True 表示来自流级标签（.opus 是这种）。</summary>
        Friend ReadOnly Property 来自流级 As Boolean

        Friend ReadOnly Property 有条目 As Boolean
            Get
                Return 资料 IsNot Nothing AndAlso 资料.条目.Count > 0
            End Get
        End Property
    End Class

    ''' <summary>从媒体信息里找内嵌歌词。找不到返回 Nothing。
    ''' <paramref name="信息"/> 由宿主从内核的 GetMediaInfo JSON 反序列化而来。</summary>
    Friend Shared Function 提取(信息 As 媒体信息) As 提取结果
        If 信息 Is Nothing Then Return Nothing

        ' 容器级优先，再流级：容器级通常是被主流工具正式写出的那一份。
        ' 每一项都带着它命中的**键名**，便于诊断与提示。
        Dim 候选 As New List(Of (文本 As String, 键 As String, 来自流级 As Boolean))()
        Dim 容器 = 取键(信息.元数据)
        If 容器 IsNot Nothing Then 候选.Add((容器.Value.值, 容器.Value.键, False))
        If 信息.流 IsNot Nothing Then
            For Each 流 In 信息.流
                If 流?.元数据 Is Nothing Then Continue For
                Dim 流文本 = 取键(流.元数据)
                If 流文本 IsNot Nothing Then 候选.Add((流文本.Value.值, 流文本.Value.键, True))
            Next
        End If
        If 候选.Count = 0 Then Return Nothing

        ' 先挑"带时间轴且能解析成功"的；纯文本留作降级。
        Dim 纯文本 As String = Nothing
        Dim 纯文本键 As String = String.Empty
        Dim 纯文本来自流级 As Boolean = False
        For Each 项 In 候选
            Dim 文本 = 规范化(项.文本)
            If String.IsNullOrWhiteSpace(文本) Then Continue For
            If 看起来有同步轴(文本) Then
                Try
                    Using reader As New IO.StringReader(文本)
                        Dim 资料 = LRC歌词解析器.解析(reader, "内嵌标签：" & 项.键)
                        If 资料.条目.Count > 0 Then
                            Return New 提取结果(资料, Nothing, 项.键, 项.来自流级)
                        End If
                    End Using
                Catch ex As NotSupportedException
                    ' 有时间戳但解析不出（畸形）——继续试下一个候选，最后仍可降级为纯文本。
                End Try
            End If
            ' 记录第一份纯文本作为降级
            If 纯文本 Is Nothing Then
                纯文本 = 文本
                纯文本键 = 项.键
                纯文本来自流级 = 项.来自流级
            End If
        Next

        If 纯文本 IsNot Nothing Then Return New 提取结果(Nothing, 纯文本, 纯文本键, 纯文本来自流级)
        Return Nothing
    End Function

    ''' <summary>在字典里按"同步键 → 兜底键"的顺序找一个非空值（大小写不敏感）。
    ''' 返回的**键是字典里实际出现的那个拼写**，不是我们搜索时用的规范名——
    ''' 诊断信息必须如实反映文件里写了什么（实测 F/J/M 是小写 lyrics，
    ''' 若回报 "LYRICS" 会让人以为文件里是大写）。</summary>
    Private Shared Function 取键(字典 As IDictionary(Of String, String)) As (键 As String, 值 As String)?
        If 字典 Is Nothing OrElse 字典.Count = 0 Then Return Nothing
        For Each 候选 In 同步键
            Dim 命中 = 查(字典, 候选)
            If 命中 IsNot Nothing AndAlso Not String.IsNullOrWhiteSpace(命中.Value.值) Then
                Return (命中.Value.键, 命中.Value.值)
            End If
        Next
        For Each 候选 In 兜底键
            Dim 命中 = 查(字典, 候选)
            ' COMMENT 也常用来放"封面(front)"之类的说明，只有含时间轴或足够长才认。
            If 命中 IsNot Nothing AndAlso Not String.IsNullOrWhiteSpace(命中.Value.值) AndAlso
                (看起来有同步轴(命中.Value.值) OrElse 命中.Value.值.Length >= 24) Then
                Return (命中.Value.键, 命中.Value.值)
            End If
        Next
        Return Nothing
    End Function

    ''' <summary>返回值所在的**实际键名**（保留原拼写）与值。</summary>
    Private Shared Function 查(字典 As IDictionary(Of String, String),
                      规范键 As String) As (键 As String, 值 As String)?
        ' 多数情况下模型用 OrdinalIgnoreCase，但不要依赖调用方的比较器：显式再兜一层。
        Dim 值 As String = Nothing
        If 字典.TryGetValue(规范键, 值) AndAlso 值 IsNot Nothing Then
            Return (实际键名(字典, 规范键), 值)
        End If
        For Each 对 In 字典
            If String.Equals(对.Key, 规范键, StringComparison.OrdinalIgnoreCase) Then
                Return (对.Key, 对.Value)
            End If
        Next
        Return Nothing
    End Function

    ''' <summary>在字典里找出与规范键大小写不敏感匹配的实际键名；找不到就用规范键。</summary>
    Private Shared Function 实际键名(字典 As IDictionary(Of String, String), 规范键 As String) As String
        For Each 对 In 字典
            If String.Equals(对.Key, 规范键, StringComparison.OrdinalIgnoreCase) Then Return 对.Key
        Next
        Return 规范键
    End Function

    ''' <summary>判断一段歌词文本是否自带 LRC 时间轴。</summary>
    Private Shared Function 看起来有同步轴(文本 As String) As Boolean
        If String.IsNullOrEmpty(文本) Then Return False
        ' 只看前若干行就够：同步词一定从头就带时间戳。
        Dim 检查长度 = Math.Min(文本.Length, 4096)
        Dim 片段 = 文本.Substring(0, 检查长度)
        Return 片段.Contains("[") AndAlso 片段.Contains(":") AndAlso 片段.Contains("]")
    End Function

    ''' <summary>剥掉 BOM 与多余空行。
    ''' ⚠ 只剥**首个**字符：LRC 里 U+FEFF 也可能是正文的一部分，但行首那一个是 BOM。</summary>
    Private Shared Function 规范化(文本 As String) As String
        If String.IsNullOrEmpty(文本) Then Return String.Empty
        Return 文本.TrimStart(ChrW(&HFEFF)).Replace(vbCrLf, vbLf).Trim()
    End Function
End Class
