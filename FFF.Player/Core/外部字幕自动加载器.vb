Imports System.IO
Imports System.Threading

''' <summary>同名外部字幕的已加载资源。原生字幕渲染器必须随播放会话释放。</summary>
Public NotInheritable Class 外部字幕轨道
    Implements IDisposable

    Private 活动使用者 As Integer
    Private 已请求释放 As Integer
    Private 已释放资源 As Integer

    Friend Sub New(路径 As String, 格式 As 外部字幕格式, SRT As SRT字幕帧生成器,
                   SUP As SUP字幕帧生成器,
                   Optional ASS特效 As ASS特效字幕帧生成器 = Nothing)
        Me.路径 = 路径
        Me.格式 = 格式
        SRT生成器 = SRT
        SUP生成器 = SUP
        ASS特效生成器 = ASS特效
    End Sub

    Public ReadOnly Property 路径 As String
    Public ReadOnly Property 格式 As 外部字幕格式
    Public ReadOnly Property SRT生成器 As SRT字幕帧生成器
    Public ReadOnly Property SUP生成器 As SUP字幕帧生成器
    Friend ReadOnly Property ASS特效生成器 As ASS特效字幕帧生成器

    ''' <summary>
    ''' 定时文字在后台线程读取轨道。替换操作先停止新租约，最后一个读者退出后
    ''' 才释放原生字幕资源，避免原子换轨与正在生成的位图帧互相踩踏。
    ''' </summary>
    Friend Function 尝试进入使用() As Boolean
        If Volatile.Read(已请求释放) <> 0 Then Return False
        Interlocked.Increment(活动使用者)
        If Volatile.Read(已请求释放) = 0 Then Return True
        离开使用()
        Return False
    End Function

    Friend Sub 离开使用()
        If Interlocked.Decrement(活动使用者) = 0 AndAlso Volatile.Read(已请求释放) <> 0 Then
            释放资源一次()
        End If
    End Sub

    Public Sub 释放() Implements IDisposable.Dispose
        If Interlocked.Exchange(已请求释放, 1) = 0 AndAlso Volatile.Read(活动使用者) = 0 Then
            释放资源一次()
        End If
        GC.SuppressFinalize(Me)
    End Sub

    Private Sub 释放资源一次()
        If Interlocked.Exchange(已释放资源, 1) = 0 Then
            ASS特效生成器?.Dispose()
            SUP生成器?.Dispose()
        End If
    End Sub
End Class

Public Enum 外部字幕格式
    SRT
    ASS
    SSA
    SUP
End Enum

''' <summary>按固定优先级找到并预加载与媒体文件同名的外部字幕。</summary>
Public NotInheritable Class 外部字幕自动加载器
    Private Shared ReadOnly 候选项 As (扩展名 As String, 格式 As 外部字幕格式)() = {
        (".srt", 外部字幕格式.SRT),
        (".ass", 外部字幕格式.ASS),
        (".ssa", 外部字幕格式.SSA),
        (".sup", 外部字幕格式.SUP)}

    Private Sub New()
    End Sub

    Public Shared Function 尝试加载同名字幕Async(媒体路径 As String,
                                              取消令牌 As CancellationToken) As Task(Of 外部字幕轨道)
        ArgumentException.ThrowIfNullOrWhiteSpace(媒体路径)
        Return Task.Run(Function() 尝试加载同名字幕(媒体路径, 取消令牌), 取消令牌)
    End Function

    Public Shared Function 是支持的字幕文件(路径 As String) As Boolean
        If String.IsNullOrWhiteSpace(路径) Then Return False
        Dim 忽略 As 外部字幕格式
        Return 尝试取得格式(Path.GetExtension(路径), 忽略)
    End Function

    Public Shared Function 加载字幕Async(字幕路径 As String,
                                     取消令牌 As CancellationToken) As Task(Of 外部字幕轨道)
        ArgumentException.ThrowIfNullOrWhiteSpace(字幕路径)
        Return Task.Run(Function() 加载字幕(字幕路径, 取消令牌), 取消令牌)
    End Function

    Public Shared Function 加载字幕Async(字幕路径 As String, 媒体路径 As String,
                                     取消令牌 As CancellationToken) As Task(Of 外部字幕轨道)
        ArgumentException.ThrowIfNullOrWhiteSpace(字幕路径)
        ArgumentException.ThrowIfNullOrWhiteSpace(媒体路径)
        Return Task.Run(Function() 加载字幕(字幕路径, 媒体路径, 取消令牌), 取消令牌)
    End Function

    ''' <summary>完整加载指定字幕；成功返回的轨道可原子替换当前轨道。</summary>
    Public Shared Function 加载字幕(字幕路径 As String,
                                Optional 取消令牌 As CancellationToken = Nothing) As 外部字幕轨道
        Return 加载字幕(字幕路径, 字幕路径, 取消令牌)
    End Function

    ''' <summary>完整加载字幕，并从指定媒体所在目录发现此轨道的私有字体。</summary>
    Public Shared Function 加载字幕(字幕路径 As String, 媒体路径 As String,
                                Optional 取消令牌 As CancellationToken = Nothing) As 外部字幕轨道
        ArgumentException.ThrowIfNullOrWhiteSpace(字幕路径)
        ArgumentException.ThrowIfNullOrWhiteSpace(媒体路径)
        Dim 完整路径 = Path.GetFullPath(字幕路径)
        If Not File.Exists(完整路径) Then Throw New FileNotFoundException("字幕文件不存在。", 完整路径)
        Dim 格式 As 外部字幕格式
        If Not 尝试取得格式(Path.GetExtension(完整路径), 格式) Then
            Throw New NotSupportedException("仅支持 SRT、ASS、SSA 和 SUP 外部字幕。")
        End If
        取消令牌.ThrowIfCancellationRequested()
        Select Case 格式
            Case 外部字幕格式.SRT
                Dim 文档 = SRT字幕解析器.解析文件(完整路径)
                取消令牌.ThrowIfCancellationRequested()
                Return New 外部字幕轨道(完整路径, 格式,
                    New SRT字幕帧生成器(文档, New SRT字幕样式()), Nothing)
            Case 外部字幕格式.ASS, 外部字幕格式.SSA
                Dim 特效生成器 As ASS特效字幕帧生成器 = Nothing
                Try
                    特效生成器 = New ASS特效字幕帧生成器(完整路径, 媒体路径)
                    取消令牌.ThrowIfCancellationRequested()
                    Return New 外部字幕轨道(完整路径, 格式, Nothing,
                        Nothing, 特效生成器)
                Catch
                    特效生成器?.Dispose()
                    Throw
                End Try
            Case 外部字幕格式.SUP
                Dim 生成器 = New SUP字幕帧生成器(完整路径)
                Try
                    取消令牌.ThrowIfCancellationRequested()
                    Return New 外部字幕轨道(完整路径, 格式, Nothing, 生成器)
                Catch
                    生成器.Dispose()
                    Throw
                End Try
            Case Else
                Throw New NotSupportedException("不支持此外部字幕格式。")
        End Select
    End Function

    ''' <summary>
    ''' 返回首个可成功解析的同名字幕；文件不存在或解析失败时继续尝试下一种优先级。
    ''' </summary>
    Public Shared Function 尝试加载同名字幕(媒体路径 As String,
                                         Optional 取消令牌 As CancellationToken = Nothing) As 外部字幕轨道
        ArgumentException.ThrowIfNullOrWhiteSpace(媒体路径)
        Dim 基础路径 = Path.Combine(Path.GetDirectoryName(媒体路径), Path.GetFileNameWithoutExtension(媒体路径))
        For Each 候选 In 候选项
            取消令牌.ThrowIfCancellationRequested()
            Dim 字幕路径 = 基础路径 & 候选.扩展名
            If Not File.Exists(字幕路径) Then Continue For
            Try
                Return 加载字幕(字幕路径, 媒体路径, 取消令牌)
            Catch ex As OperationCanceledException
                Throw
            Catch ex As NotSupportedException
                Throw
            Catch
                ' 损坏的高优先级字幕不阻止后续格式被自动使用。
            End Try
        Next
        Return Nothing
    End Function

    Private Shared Function 尝试取得格式(扩展名 As String, ByRef 格式 As 外部字幕格式) As Boolean
        For Each 候选 In 候选项
            If String.Equals(扩展名, 候选.扩展名, StringComparison.OrdinalIgnoreCase) Then
                格式 = 候选.格式
                Return True
            End If
        Next
        Return False
    End Function
End Class
