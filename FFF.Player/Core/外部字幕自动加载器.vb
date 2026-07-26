Imports System.IO
Imports System.Threading

''' <summary>同名外部字幕的已加载资源。SUP 生成器持有原生解码器，必须随播放会话释放。</summary>
Public NotInheritable Class 外部字幕轨道
    Implements IDisposable

    Friend Sub New(路径 As String, 格式 As 外部字幕格式, SRT As SRT字幕帧生成器,
                   ASS As ASS字幕帧生成器, SUP As SUP字幕帧生成器)
        Me.路径 = 路径
        Me.格式 = 格式
        SRT生成器 = SRT
        ASS生成器 = ASS
        SUP生成器 = SUP
    End Sub

    Public ReadOnly Property 路径 As String
    Public ReadOnly Property 格式 As 外部字幕格式
    Public ReadOnly Property SRT生成器 As SRT字幕帧生成器
    Public ReadOnly Property ASS生成器 As ASS字幕帧生成器
    Public ReadOnly Property SUP生成器 As SUP字幕帧生成器

    Public Sub 释放() Implements IDisposable.Dispose
        SUP生成器?.Dispose()
        GC.SuppressFinalize(Me)
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
                Select Case 候选.格式
                    Case 外部字幕格式.SRT
                        Dim 文档 = SRT字幕解析器.解析文件(字幕路径)
                        Return New 外部字幕轨道(字幕路径, 候选.格式, New SRT字幕帧生成器(文档, New SRT字幕样式()), Nothing, Nothing)
                    Case 外部字幕格式.ASS, 外部字幕格式.SSA
                        Dim 文档 = ASS字幕解析器.解析文件(字幕路径)
                        Return New 外部字幕轨道(字幕路径, 候选.格式, Nothing, New ASS字幕帧生成器(文档), Nothing)
                    Case 外部字幕格式.SUP
                        Return New 外部字幕轨道(字幕路径, 候选.格式, Nothing, Nothing, New SUP字幕帧生成器(字幕路径))
                End Select
            Catch ex As OperationCanceledException
                Throw
            Catch
                ' 损坏的高优先级字幕不阻止后续格式被自动使用。
            End Try
        Next
        Return Nothing
    End Function
End Class
