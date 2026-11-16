''' <summary>
''' 系统媒体面板（SMTC）的宿主侧映射。这里只做“3FP 状态/元数据 → 面向系统的中立值”，
''' 不引用 WinRT，便于 <c>FFF.Player.Tests</c> 在没有系统媒体会话的进程里做纯回归。
'''
''' <para>枚举数值与 <c>Windows.Media</c> 的对应枚举刻意保持一致：
''' <c>MediaPlaybackStatus</c> 为 Closed=0 / Changing=1 / Stopped=2 / Playing=3 / Paused=4，
''' <c>SystemMediaTransportControlsButton</c> 的 Play/Pause/Stop/FastForward/Rewind/Next/Previous
''' 为 0/1/2/4/5/6/7。真正跨进 WinRT 时由 <c>系统媒体会话</c> 做一次显式转换。</para>
''' </summary>
Public Module 系统媒体映射

    ''' <summary>发布给系统媒体面板的播放状态；数值与 Windows.Media.MediaPlaybackStatus 对齐。</summary>
    Public Enum 系统媒体播放状态
        已关闭 = 0
        变化中 = 1
        已停止 = 2
        正在播放 = 3
        已暂停 = 4
    End Enum

    ''' <summary>卡片类型；图片永远不注册，所以这里没有 Image。</summary>
    Public Enum 系统媒体显示类型
        音乐 = 1
        视频 = 2
    End Enum

    ''' <summary>系统按钮；数值与 Windows.Media.SystemMediaTransportControlsButton 对齐。</summary>
    Public Enum 系统媒体按钮
        播放 = 0
        暂停 = 1
        停止 = 2
        快进 = 4
        快退 = 5
        下一曲 = 6
        上一曲 = 7
    End Enum

    ''' <summary>
    ''' 3FP 8 态到 SMTC 5 态的完整决策。没有媒体、失败、已关闭一律 Closed；
    ''' “播放结束”必须结合播放列表是否还有下一项，不能一律 Stopped。
    ''' </summary>
    Public Function 映射播放状态(状态 As 播放状态, 是否有媒体 As Boolean,
                                  播放结束后有下一项 As Boolean) As 系统媒体播放状态
        If Not 是否有媒体 Then Return 系统媒体播放状态.已关闭
        Select Case 状态
            Case 播放状态.空闲, 播放状态.失败, 播放状态.已关闭
                Return 系统媒体播放状态.已关闭
            Case 播放状态.正在打开
                Return 系统媒体播放状态.变化中
            Case 播放状态.就绪, 播放状态.已暂停
                Return 系统媒体播放状态.已暂停
            Case 播放状态.正在播放
                Return 系统媒体播放状态.正在播放
            Case 播放状态.播放结束
                Return If(播放结束后有下一项,
                          系统媒体播放状态.变化中,
                          系统媒体播放状态.已停止)
            Case Else
                Return 系统媒体播放状态.已关闭
        End Select
    End Function

    Public Function 映射显示类型(是视频 As Boolean) As 系统媒体显示类型
        Return If(是视频, 系统媒体显示类型.视频, 系统媒体显示类型.音乐)
    End Function

    ''' <summary>
    ''' 系统按钮 → 播放器命令名 的唯一映射表。Form1 的 <c>处理系统媒体按钮</c> 必须经此函数
    ''' 分派，FFF.Player.Tests 据此断言“每个枚举按钮都有对应处理器”。返回空串表示未接线，
    ''' 调用方必须按未处理返回 False——新增 <see cref="系统媒体按钮"/> 成员却忘了接线时，
    ''' 测试会因为这里返回空串而变红。
    ''' </summary>
    Public Function 映射按钮到命令名(按钮 As 系统媒体按钮) As String
        Select Case 按钮
            Case 系统媒体按钮.播放 : Return "play"
            Case 系统媒体按钮.暂停 : Return "pause"
            Case 系统媒体按钮.停止 : Return "stop"
            Case 系统媒体按钮.快进 : Return "fastforward"
            Case 系统媒体按钮.快退 : Return "rewind"
            Case 系统媒体按钮.下一曲 : Return "next"
            Case 系统媒体按钮.上一曲 : Return "previous"
            Case Else : Return String.Empty
        End Select
    End Function

    ''' <summary>标题优先取容器 metadata，空则回落到文件名（不含扩展名）。</summary>
    Public Function 取标题(信息 As 媒体信息, 路径 As String) As String
        Dim 值 As String = Nothing
        If 信息 IsNot Nothing AndAlso 信息.元数据 IsNot Nothing Then
            信息.元数据.TryGetValue("title", 值)
        End If
        If Not String.IsNullOrWhiteSpace(值) Then Return 值.Trim()
        If String.IsNullOrWhiteSpace(路径) Then Return String.Empty
        Return System.IO.Path.GetFileNameWithoutExtension(路径)
    End Function

    ''' <summary>artist 优先；没有 artist 时回落 album_artist，二者都没有就留空，不伪造。</summary>
    Public Function 取艺术家(信息 As 媒体信息) As String
        Dim 值 As String = Nothing
        If 信息 IsNot Nothing AndAlso 信息.元数据 IsNot Nothing Then
            信息.元数据.TryGetValue("artist", 值)
            If String.IsNullOrWhiteSpace(值) Then
                信息.元数据.TryGetValue("album_artist", 值)
            End If
        End If
        Return If(String.IsNullOrWhiteSpace(值), String.Empty, 值.Trim())
    End Function

    Public Function 取专辑(信息 As 媒体信息) As String
        Dim 值 As String = Nothing
        If 信息 IsNot Nothing AndAlso 信息.元数据 IsNot Nothing Then
            信息.元数据.TryGetValue("album", 值)
        End If
        Return If(String.IsNullOrWhiteSpace(值), String.Empty, 值.Trim())
    End Function

    Public Function 取专辑艺术家(信息 As 媒体信息) As String
        Dim 值 As String = Nothing
        If 信息 IsNot Nothing AndAlso 信息.元数据 IsNot Nothing Then
            信息.元数据.TryGetValue("album_artist", 值)
        End If
        Return If(String.IsNullOrWhiteSpace(值), String.Empty, 值.Trim())
    End Function

    ''' <summary>支持 “3” 与 “3/12”；无法解析返回 0，调用方不应把 0 写进 SMTC。</summary>
    Public Function 取轨道号(信息 As 媒体信息) As UInteger
        Dim 值 As String = Nothing
        If 信息 Is Nothing OrElse 信息.元数据 Is Nothing Then Return 0UI
        If Not 信息.元数据.TryGetValue("track", 值) OrElse String.IsNullOrWhiteSpace(值) Then Return 0UI
        Dim 斜杠 = 值.IndexOf("/"c)
        If 斜杠 >= 0 Then 值 = 值.Substring(0, 斜杠)
        Dim 结果 As UInteger
        If UInteger.TryParse(值.Trim(), 结果) Then Return 结果
        Return 0UI
    End Function

    ''' <summary>
    ''' SMTC 的 AppMediaId：同一路径稳定、不同路径不同，且不把本地路径直接暴露给系统 UI。
    ''' 路径为空（光盘/未知来源）返回空，调用方跳过赋值。
    ''' </summary>
    Public Function 取媒体标识(路径 As String) As String
        If String.IsNullOrWhiteSpace(路径) Then Return String.Empty
        Try
            Dim 规范路径 = System.IO.Path.GetFullPath(路径).ToLowerInvariant()
            Dim 字节 = System.Text.Encoding.UTF8.GetBytes(规范路径)
            Return Convert.ToHexString(System.Security.Cryptography.SHA256.HashData(字节))
        Catch ex As Exception
            ' 路径非法只影响卡片缓存键，不应打断打开媒体。
            Return String.Empty
        End Try
    End Function

End Module
