' 设置页只写偏好；一次媒体打开捕获一份快照，用于创建和复用会话。
' 新偏好不能漏过原位重开路径，否则缩放质量/解码设备会一直沿用旧会话。
Friend Structure 播放性能选项
    Friend ReadOnly 缩放质量 As 视频缩放质量
    Friend ReadOnly GPU设备标识 As String
    Friend ReadOnly GPU设备索引 As Integer
    Friend ReadOnly 视频缓冲区策略 As Integer
    Friend ReadOnly 音频缓冲区策略 As Integer
    Friend ReadOnly 启用空间音频 As Boolean

    Friend Sub New(配置 As 设置, Optional 枚举设备 As Func(Of IReadOnlyList(Of GPU解码设备)) = Nothing)
        缩放质量 = If(配置.视频缩放质量选项 = 0, 视频缩放质量.均衡, 视频缩放质量.高画质)
        GPU设备标识 = 配置.GPU解码设备标识
        GPU设备索引 = -1
        If Not String.IsNullOrEmpty(GPU设备标识) Then
            Try
                Dim 保存标识 = GPU设备标识
                Dim 设备 = If(枚举设备, AddressOf GPU解码设备.枚举).Invoke().
                    FirstOrDefault(Function(x) x.标识 = 保存标识)
                If 设备 IsNot Nothing Then GPU设备索引 = 设备.索引
            Catch ex As Exception
                Debug.WriteLine("枚举 GPU 解码设备失败，使用自动选择：" & ex.Message)
            End Try
        End If
        视频缓冲区策略 = 配置.视频缓冲区策略
        音频缓冲区策略 = 配置.音频缓冲区策略
        启用空间音频 = 配置.启用空间音频
    End Sub
End Structure
