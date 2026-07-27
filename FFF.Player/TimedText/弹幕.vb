Imports System.Collections.ObjectModel
Imports System.Globalization
Imports System.IO
Imports System.Text
Imports System.Threading
Imports System.Threading.Tasks
Imports System.Xml

<Flags>
Public Enum 弹幕类型
    无 = 0
    常规滚动 = 1
    底部 = 2
    顶部 = 4
    逆向滚动 = 8
    高级 = 16
    脚本 = 32
    常用 = 常规滚动 Or 底部 Or 顶部
    全部 = 常用 Or 逆向滚动 Or 高级 Or 脚本
End Enum

Public NotInheritable Class 弹幕项目
    Implements I时间轴项目

    Friend Sub New(timeValue As TimeSpan, typeValue As 弹幕类型, modeValue As Integer, fontSizeValue As Single,
                   colorValue As UInteger, sendTimeValue As Long, poolValue As Integer, userValue As String,
                   idValue As Long, textValue As String)
        出现时间 = timeValue
        类型 = typeValue
        原始模式 = modeValue
        原始字号 = fontSizeValue
        颜色ARGB = colorValue
        发送时间Unix秒 = sendTimeValue
        弹幕池 = poolValue
        用户标识 = userValue
        弹幕编号 = idValue
        文本 = textValue
    End Sub

    Public ReadOnly Property 出现时间 As TimeSpan
    Public ReadOnly Property 类型 As 弹幕类型
    Public ReadOnly Property 原始模式 As Integer
    Public ReadOnly Property 原始字号 As Single
    Public ReadOnly Property 颜色ARGB As UInteger
    Public ReadOnly Property 发送时间Unix秒 As Long
    Public ReadOnly Property 弹幕池 As Integer
    Public ReadOnly Property 用户标识 As String
    Public ReadOnly Property 弹幕编号 As Long
    Public ReadOnly Property 文本 As String

    Private ReadOnly Property 接口开始 As TimeSpan Implements I时间轴项目.开始时间
        Get
            Return 出现时间
        End Get
    End Property

    Private ReadOnly Property 接口结束 As TimeSpan Implements I时间轴项目.结束时间
        Get
            Return 出现时间 + TimeSpan.FromTicks(1)
        End Get
    End Property
End Class

Public NotInheritable Class 弹幕搜索条件
    Public Property 关键词 As String = String.Empty
    Public Property 开始时间 As TimeSpan = TimeSpan.Zero
    Public Property 结束时间 As TimeSpan = TimeSpan.MaxValue
    Public Property 用户标识 As String = String.Empty
    Public Property 类型 As 弹幕类型 = 弹幕类型.全部
    Public Property 最大结果数 As Integer = 200
End Class

Public NotInheritable Class 弹幕资料库
    Private ReadOnly 项目数组 As 弹幕项目()
    Private ReadOnly 只读项目 As ReadOnlyCollection(Of 弹幕项目)
    Private ReadOnly 时间索引 As 时间轴索引(Of 弹幕项目)

    Friend Sub New(items As IEnumerable(Of 弹幕项目))
        项目数组 = items.OrderBy(Function(x) x.出现时间).ThenBy(Function(x) x.弹幕编号).ToArray()
        只读项目 = Array.AsReadOnly(项目数组)
        时间索引 = New 时间轴索引(Of 弹幕项目)(项目数组)
    End Sub

    Public ReadOnly Property 项目 As IReadOnlyList(Of 弹幕项目)
        Get
            Return 只读项目
        End Get
    End Property

    Public ReadOnly Property 数量 As Integer
        Get
            Return 项目数组.Length
        End Get
    End Property

    Public Function 查询(条件 As 弹幕搜索条件) As IReadOnlyList(Of 弹幕项目)
        ArgumentNullException.ThrowIfNull(条件)
        If 条件.开始时间 < TimeSpan.Zero Then Throw New ArgumentOutOfRangeException(NameOf(条件.开始时间))
        If 条件.结束时间 < 条件.开始时间 Then Throw New ArgumentOutOfRangeException(NameOf(条件.结束时间))
        If 条件.最大结果数 <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(条件.最大结果数))
        Dim result As New List(Of 弹幕项目)(Math.Min(条件.最大结果数, 256))
        Dim index = 时间索引.首个开始不早于(条件.开始时间.Ticks)
        While index < 项目数组.Length AndAlso 项目数组(index).出现时间 <= 条件.结束时间
            Dim item = 项目数组(index)
            If (条件.类型 And item.类型) <> 0 AndAlso
               (String.IsNullOrEmpty(条件.用户标识) OrElse item.用户标识.Equals(条件.用户标识, StringComparison.OrdinalIgnoreCase)) AndAlso
               (String.IsNullOrEmpty(条件.关键词) OrElse item.文本.IndexOf(条件.关键词, StringComparison.OrdinalIgnoreCase) >= 0) Then
                result.Add(item)
                If result.Count >= 条件.最大结果数 Then Exit While
            End If
            index += 1
        End While
        Return result.AsReadOnly()
    End Function

    Friend Function 首个开始不早于(时间刻度 As Long) As Integer
        Return 时间索引.首个开始不早于(时间刻度)
    End Function
End Class

Public NotInheritable Class B站弹幕解析器
    Private Sub New()
    End Sub

    Public Shared Function 解析文件(路径 As String) As 弹幕资料库
        ArgumentException.ThrowIfNullOrWhiteSpace(路径)
        Dim settings As New XmlReaderSettings With {
            .DtdProcessing = DtdProcessing.Prohibit,
            .IgnoreComments = True,
            .IgnoreProcessingInstructions = True,
            .IgnoreWhitespace = True,
            .CloseInput = True}
        Dim stream = New FileStream(路径, FileMode.Open, FileAccess.Read, FileShare.Read, 128 * 1024, FileOptions.SequentialScan)
        Using reader = XmlReader.Create(stream, settings)
            Return 解析(reader)
        End Using
    End Function

    Public Shared Function 解析(reader As XmlReader) As 弹幕资料库
        ArgumentNullException.ThrowIfNull(reader)
        Dim items As New List(Of 弹幕项目)(65536)
        If reader.ReadState = ReadState.Initial Then reader.Read()
        While Not reader.EOF
            If reader.NodeType = XmlNodeType.Element AndAlso reader.Name.Equals("d", StringComparison.Ordinal) Then
                Dim parameter = reader.GetAttribute("p")
                If String.IsNullOrEmpty(parameter) Then
                    reader.Skip()
                Else
                    Dim text = reader.ReadElementContentAsString()
                    Dim item As 弹幕项目 = Nothing
                    If 尝试解析项目(parameter, text, item) Then items.Add(item)
                End If
                Continue While
            End If
            reader.Read()
        End While
        Return New 弹幕资料库(items)
    End Function

    Private Shared Function 尝试解析项目(parameter As String, text As String, ByRef result As 弹幕项目) As Boolean
        Dim cursor = 0
        Dim timeText = 下一字段(parameter, cursor)
        Dim modeText = 下一字段(parameter, cursor)
        Dim sizeText = 下一字段(parameter, cursor)
        Dim colorText = 下一字段(parameter, cursor)
        Dim sendText = 下一字段(parameter, cursor)
        Dim poolText = 下一字段(parameter, cursor)
        Dim userText = 下一字段(parameter, cursor)
        Dim idText = 下一字段(parameter, cursor)
        If timeText Is Nothing OrElse modeText Is Nothing OrElse sizeText Is Nothing OrElse colorText Is Nothing Then Return False
        Dim seconds As Double
        Dim mode As Integer
        Dim fontSize As Single
        Dim rgb As UInteger
        If Not Double.TryParse(timeText, NumberStyles.Float, CultureInfo.InvariantCulture, seconds) OrElse seconds < 0 OrElse Not Double.IsFinite(seconds) Then Return False
        If Not Integer.TryParse(modeText, NumberStyles.Integer, CultureInfo.InvariantCulture, mode) Then Return False
        If Not Single.TryParse(sizeText, NumberStyles.Float, CultureInfo.InvariantCulture, fontSize) OrElse fontSize <= 0 OrElse Not Single.IsFinite(fontSize) Then Return False
        If Not UInteger.TryParse(colorText, NumberStyles.Integer, CultureInfo.InvariantCulture, rgb) Then Return False
        Dim sendTime As Long
        Dim pool As Integer
        Dim id As Long
        Long.TryParse(sendText, NumberStyles.Integer, CultureInfo.InvariantCulture, sendTime)
        Integer.TryParse(poolText, NumberStyles.Integer, CultureInfo.InvariantCulture, pool)
        Long.TryParse(idText, NumberStyles.Integer, CultureInfo.InvariantCulture, id)
        Dim type = 映射类型(mode)
        If type = 弹幕类型.无 Then Return False
        result = New 弹幕项目(TimeSpan.FromSeconds(seconds), type, mode, fontSize,
                           &HFF000000UI Or (rgb And &HFFFFFFUI), sendTime, pool, If(userText, String.Empty), id, text)
        Return True
    End Function

    Private Shared Function 下一字段(value As String, ByRef cursor As Integer) As String
        If cursor > value.Length Then Return Nothing
        Dim comma = value.IndexOf(","c, cursor)
        If comma < 0 Then
            Dim last = value.Substring(cursor)
            cursor = value.Length + 1
            Return last
        End If
        Dim result = value.Substring(cursor, comma - cursor)
        cursor = comma + 1
        Return result
    End Function

    Private Shared Function 映射类型(mode As Integer) As 弹幕类型
        Select Case mode
            Case 1, 2, 3 : Return 弹幕类型.常规滚动
            Case 4 : Return 弹幕类型.底部
            Case 5 : Return 弹幕类型.顶部
            Case 6 : Return 弹幕类型.逆向滚动
            Case 7 : Return 弹幕类型.高级
            Case 8 : Return 弹幕类型.脚本
            Case Else : Return 弹幕类型.无
        End Select
    End Function
End Class

''' <summary>按媒体文件名查找并后台解析 B 站 XML 弹幕。</summary>
Public NotInheritable Class 弹幕自动加载器
    Private Sub New()
    End Sub

    Public Shared Function 尝试加载同名弹幕Async(媒体路径 As String,
                                              取消令牌 As CancellationToken) As Task(Of 弹幕资料库)
        ArgumentException.ThrowIfNullOrWhiteSpace(媒体路径)
        Return Task.Run(Function() 尝试加载同名弹幕(媒体路径, 取消令牌), 取消令牌)
    End Function

    Public Shared Function 尝试加载同名弹幕(媒体路径 As String,
                                         Optional 取消令牌 As CancellationToken = Nothing) As 弹幕资料库
        ArgumentException.ThrowIfNullOrWhiteSpace(媒体路径)
        取消令牌.ThrowIfCancellationRequested()
        Dim 弹幕路径 = Path.ChangeExtension(媒体路径, ".xml")
        If Not File.Exists(弹幕路径) Then Return Nothing
        Dim database = B站弹幕解析器.解析文件(弹幕路径)
        取消令牌.ThrowIfCancellationRequested()
        Return If(database.数量 > 0, database, Nothing)
    End Function
End Class

Public NotInheritable Class 弹幕过滤配置
    Public Property 启用类型 As 弹幕类型 = 弹幕类型.常用
    Public ReadOnly Property 屏蔽用户 As ISet(Of String) = New HashSet(Of String)(StringComparer.OrdinalIgnoreCase)
    Public ReadOnly Property 屏蔽关键词 As IList(Of String) = New List(Of String)()

    Public Function 创建快照() As 弹幕过滤器
        Return New 弹幕过滤器(启用类型, 屏蔽用户, 屏蔽关键词)
    End Function
End Class

Public NotInheritable Class 弹幕过滤器
    Private ReadOnly 启用类型值 As 弹幕类型
    Private ReadOnly 用户 As HashSet(Of String)
    Private ReadOnly 关键词 As 多关键词匹配器

    Friend Sub New(types As 弹幕类型, users As IEnumerable(Of String), words As IEnumerable(Of String))
        启用类型值 = types
        用户 = New HashSet(Of String)(users.Where(Function(x) Not String.IsNullOrWhiteSpace(x)), StringComparer.OrdinalIgnoreCase)
        关键词 = New 多关键词匹配器(words.Where(Function(x) Not String.IsNullOrWhiteSpace(x)).Distinct(StringComparer.OrdinalIgnoreCase))
    End Sub

    Public Function 允许(item As 弹幕项目) As Boolean
        ArgumentNullException.ThrowIfNull(item)
        Return (启用类型值 And item.类型) <> 0 AndAlso Not 用户.Contains(item.用户标识) AndAlso Not 关键词.包含匹配(item.文本)
    End Function
End Class

Friend NotInheritable Class 多关键词匹配器
    Private NotInheritable Class 节点
        Public ReadOnly 转移 As New Dictionary(Of Char, Integer)()
        Public 失败 As Integer
        Public 命中 As Boolean
    End Class

    Private ReadOnly 节点列表 As New List(Of 节点) From {New 节点()}

    Public Sub New(words As IEnumerable(Of String))
        For Each word In words
            Dim state = 0
            For Each rawChar In word
                Dim c = Char.ToUpperInvariant(rawChar)
                Dim nextState As Integer
                If Not 节点列表(state).转移.TryGetValue(c, nextState) Then
                    nextState = 节点列表.Count
                    节点列表(state).转移(c) = nextState
                    节点列表.Add(New 节点())
                End If
                state = nextState
            Next
            节点列表(state).命中 = True
        Next
        Dim queue As New Queue(Of Integer)()
        For Each child In 节点列表(0).转移.Values
            queue.Enqueue(child)
        Next
        While queue.Count > 0
            Dim state = queue.Dequeue()
            For Each pair In 节点列表(state).转移
                Dim failure = 节点列表(state).失败
                Dim fallback As Integer
                While failure <> 0 AndAlso Not 节点列表(failure).转移.TryGetValue(pair.Key, fallback)
                    failure = 节点列表(failure).失败
                End While
                If 节点列表(failure).转移.TryGetValue(pair.Key, fallback) AndAlso fallback <> pair.Value Then
                    节点列表(pair.Value).失败 = fallback
                End If
                节点列表(pair.Value).命中 = 节点列表(pair.Value).命中 OrElse 节点列表(节点列表(pair.Value).失败).命中
                queue.Enqueue(pair.Value)
            Next
        End While
    End Sub

    Public Function 包含匹配(text As String) As Boolean
        If 节点列表.Count = 1 Then Return False
        Dim state = 0
        For Each rawChar In text
            Dim c = Char.ToUpperInvariant(rawChar)
            Dim nextState As Integer
            While state <> 0 AndAlso Not 节点列表(state).转移.TryGetValue(c, nextState)
                state = 节点列表(state).失败
            End While
            If 节点列表(state).转移.TryGetValue(c, nextState) Then state = nextState
            If 节点列表(state).命中 Then Return True
        Next
        Return False
    End Function
End Class
