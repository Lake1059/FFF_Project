Friend NotInheritable Class 预览控制器
    Implements IDisposable

    Private ReadOnly 目标控件 As 实时预览控件
    Private ReadOnly 源 As 视频源条目
    Private ReadOnly 捕获鼠标 As Boolean
    Private ReadOnly 预览宽度上限 As Integer
    Private ReadOnly 预览高度上限 As Integer
    Private ReadOnly 生命周期锁 As New Object
    Private 捕获器 As 窗口捕获器
    Private 处理器 As 视频处理器
    Private 已释放 As Boolean

    Friend Sub New(控件 As 实时预览控件, 视频源 As 视频源条目, 是否捕获鼠标 As Boolean,
        最大预览宽度 As Integer, 最大预览高度 As Integer)
        目标控件 = 控件
        源 = 视频源
        捕获鼠标 = 是否捕获鼠标
        预览宽度上限 = Math.Max(1, 最大预览宽度)
        预览高度上限 = Math.Max(1, 最大预览高度)
    End Sub

    Friend Sub 开始()
        SyncLock 生命周期锁
            If 已释放 Then Throw New ObjectDisposedException(NameOf(预览控制器))
            If 捕获器 IsNot Nothing Then
                捕获器.开始(捕获鼠标)
                Return
            End If
            Dim 源宽度 As UInteger
            Dim 源高度 As UInteger
            Dim 客户区裁剪 As 窗口裁剪信息 = Nothing
            If 源.显示器 IsNot Nothing Then
                源宽度 = 源.显示器.宽度
                源高度 = 源.显示器.高度
            ElseIf 源.捕获模式 = 2 Then
                客户区裁剪 = 窗口发现.获取客户区裁剪(源.窗口.窗口句柄)
                源宽度 = 客户区裁剪.宽度
                源高度 = 客户区裁剪.高度
            Else
                源宽度 = CUInt(Math.Max(1, 源.窗口.右边 - 源.窗口.左边))
                源高度 = CUInt(Math.Max(1, 源.窗口.底边 - 源.窗口.顶边))
            End If
            Dim 缩放 = Math.Min(1.0, Math.Min(CDbl(预览宽度上限) / 源宽度, CDbl(预览高度上限) / 源高度))
            Dim 宽度 = CUInt(Math.Max(1.0, Math.Round(源宽度 * 缩放)))
            Dim 高度 = CUInt(Math.Max(1.0, Math.Round(源高度 * 缩放)))
            Dim 输出HDR = 设置.实例对象.色彩模式 = 1
            If 源.显示器 IsNot Nothing Then
                捕获器 = 窗口捕获器.创建显示器(源.显示器, 输出HDR, True)
            Else
                捕获器 = 窗口捕获器.创建(源.窗口.窗口句柄, 输出HDR, True)
            End If
            Dim 配置 As New 视频处理配置 With {
                .输出宽度 = 宽度, .输出高度 = 高度,
                .输出HDR10 = 输出HDR,
                .允许HDR转SDR = Not 输出HDR,
                .高质量缩放 = True,
                .裁剪左边 = If(客户区裁剪 Is Nothing, 0UI, 客户区裁剪.左边),
                .裁剪顶边 = If(客户区裁剪 Is Nothing, 0UI, 客户区裁剪.顶边),
                .裁剪右边 = If(客户区裁剪 Is Nothing, 0UI, 客户区裁剪.右边),
                .裁剪底边 = If(客户区裁剪 Is Nothing, 0UI, 客户区裁剪.底边),
                .目标峰值尼特 = If(输出HDR, 设置.实例对象.HDR峰值, 设置.实例对象.SDR亮度),
                .参考白尼特 = 80.0F}
            ' 普通 SDR 预览直接使用 WGC 纹理；HDR 色调映射和客户区裁剪才建立处理管线。
            If 输出HDR OrElse 客户区裁剪 IsNot Nothing Then
                处理器 = New 视频处理器(捕获器.设备, 配置)
            End If
            AddHandler 捕获器.收到帧, AddressOf 收到捕获帧
            AddHandler 捕获器.捕获失败, AddressOf 捕获失败
            AddHandler 捕获器.捕获已关闭, AddressOf 捕获关闭
            捕获器.开始(捕获鼠标)
        End SyncLock
    End Sub

    Friend Sub 暂停()
        SyncLock 生命周期锁
            If 已释放 Then Return
            If 捕获器 IsNot Nothing Then
                捕获器.停止()
                捕获器.设备.清空并刷新()
            End If
        End SyncLock
    End Sub

    Private Sub 收到捕获帧(sender As Object, e As 窗口捕获帧事件参数)
        Try
            Using e.帧
                SyncLock 生命周期锁
                    If 已释放 Then Return
                    If 处理器 Is Nothing Then
                        ' 捕获纹理由 Using 保护到提交完成，控件在同一 D3D 设备上
                        ' 直接创建 SRV 并绘制到交换链，完全绕过 CPU/计算着色器处理。
                        目标控件.提交空闲原始帧(Me, 捕获器.设备, e.帧)
                    Else
                        Using 帧 = 处理器.处理帧(e.帧)
                            If 帧 IsNot Nothing Then 目标控件.提交空闲GPU帧(Me, 捕获器.设备, 帧)
                        End Using
                    End If
                End SyncLock
            End Using
        Catch ex As Exception
            目标控件.报告预览错误(Me, ex.Message)
        End Try
    End Sub

    Private Sub 捕获失败(sender As Object, e As 窗口捕获错误事件参数)
        目标控件.报告预览错误(Me, e.异常.Message)
    End Sub

    Private Sub 捕获关闭(sender As Object, e As EventArgs)
        目标控件.报告预览错误(Me, "目标窗口已关闭")
    End Sub

    Public Sub 释放() Implements IDisposable.Dispose
        SyncLock 生命周期锁
            If 已释放 Then Return
            已释放 = True
            Dim 待释放捕获器 = 捕获器
            Dim 待释放处理器 = 处理器
            捕获器 = Nothing
            处理器 = Nothing
            If 待释放捕获器 IsNot Nothing Then
                RemoveHandler 待释放捕获器.收到帧, AddressOf 收到捕获帧
                RemoveHandler 待释放捕获器.捕获失败, AddressOf 捕获失败
                RemoveHandler 待释放捕获器.捕获已关闭, AddressOf 捕获关闭
            End If
            Try
                待释放捕获器?.停止()
            Finally
                Try
                    ' 视频处理资源依赖捕获器持有的 D3D 设备，必须先释放子资源。
                    待释放处理器?.释放()
                Finally
                    待释放捕获器?.释放()
                End Try
            End Try
            GC.SuppressFinalize(Me)
        End SyncLock
    End Sub
End Class
