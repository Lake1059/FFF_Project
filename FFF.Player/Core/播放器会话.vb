Imports System.Runtime.InteropServices
Imports System.Collections.Concurrent
Imports System.Drawing
Imports System.Text.Json
Imports System.Threading

Public NotInheritable Class 播放器会话
    Implements IDisposable

    Private NotInheritable Class 回调状态
        Public Property 目标 As WeakReference(Of 播放器会话)
    End Class

    Private Shared ReadOnly 共享原生回调 As 原生播放器回调 = AddressOf 接收原生事件
    Private ReadOnly 句柄 As 播放器原生句柄
    Private ReadOnly 同步上下文 As SynchronizationContext
    Private ReadOnly 释放取消源 As New CancellationTokenSource()
    Private ReadOnly 待处理事件 As New ConcurrentQueue(Of 播放器事件参数)()
    Private Const 定时文字UTF8缓存上限 As Integer = 2048
    Private Shared ReadOnly 原生播放器配置大小 As UInteger = CUInt(Marshal.SizeOf(Of 原生播放器配置)())
    Private Shared ReadOnly 原生定时文字命令大小 As UInteger = CUInt(Marshal.SizeOf(Of 原生定时文字命令)())
    Private Shared ReadOnly 原生定时文字图层大小 As UInteger = CUInt(Marshal.SizeOf(Of 原生定时文字图层)())
    Private Shared ReadOnly 原生定时文字状态大小 As UInteger = CUInt(Marshal.SizeOf(Of 原生定时文字状态)())
    Private Shared ReadOnly 原生播放器快照大小 As UInteger = CUInt(Marshal.SizeOf(Of 原生播放器快照)())
    Private Shared ReadOnly 原生视频像素探针大小 As UInteger = CUInt(Marshal.SizeOf(Of 原生视频像素探针)())
    Private Shared ReadOnly 原生音频峰值大小 As UInteger = CUInt(Marshal.SizeOf(Of 原生音频峰值)())
    ' RTX VSR 状态：内核要求 size >= sizeof(44) 且 version = PlayerApiVersion，两者缺一即拒。
    Private Shared ReadOnly 原生视频超分辨率状态大小 As UInteger =
        CUInt(Marshal.SizeOf(Of 原生视频超分辨率状态)())
    ' 自动 HDR（TrueHDR）状态：与 VSR 同尺寸同闸门（size >= sizeof(44) 且 version = 20）。
    Private Shared ReadOnly 原生视频自动HDR状态大小 As UInteger =
        CUInt(Marshal.SizeOf(Of 原生视频自动HDR状态)())
    ' 呈现节律状态：内核同样按 size >= sizeof 且 version = PlayerApiVersion 校验。
    Private Shared ReadOnly 原生呈现节律状态大小 As UInteger =
        CUInt(Marshal.SizeOf(Of 原生呈现节律状态)())
    Private ReadOnly 定时文字提交锁 As New Object()
    Private ReadOnly 定时文字UTF8缓存 As New Dictionary(Of String, IntPtr)(StringComparer.Ordinal)
    Private ReadOnly 定时文字临时指针 As New List(Of IntPtr)()
    Private ReadOnly 定时文字位图固定句柄 As New List(Of GCHandle)()
    Private 定时文字原生缓冲 As 原生定时文字命令() = Array.Empty(Of 原生定时文字命令)()
    Private 定时文字原生缓冲句柄 As GCHandle
    Private 已释放 As Integer
    Private 事件排程中 As Integer
    Private 空间音频已启用 As Integer = 1
    ''' <summary>外部色彩变换注入泵（模块缺席/未解锁/内核无环导出时为 Nothing，通路整体静默）。</summary>
    Private 注入泵 As 色彩变换注入泵

    ''' <summary>宿主对应的原生 API 版本，须与 PlayerApi.cpp 的 PlayerApiVersion 同步。</summary>
    Friend Const 期望API版本 As UInteger = 20UI

    Public Sub New(配置 As 播放器配置)
        ArgumentNullException.ThrowIfNull(配置)
        配置.验证()
        空间音频已启用 = If(配置.启用空间音频, 1, 0)
        ' 创建前检查实际加载的内核版本；配置大小仍由 FFF3FP_Create 校验。
        Dim 内核API版本 = 播放器原生接口.FFF3FP_GetApiVersion()
        If 内核API版本 <> 期望API版本 Then
            Throw New InvalidOperationException(
                $"FFF.Native 的 3FP API 版本不匹配：内核为 {内核API版本}，本程序需要 {期望API版本}。" &
                "请确认 FFF.Native.dll 与播放器来自同一次发布（两者必须一起替换）。")
        End If
        同步上下文 = 配置.事件同步上下文
        Dim 状态 = New 回调状态()
        Dim 回调句柄 = GCHandle.Alloc(状态)
        Dim 端点指针 = IntPtr.Zero
        Try
            If Not String.IsNullOrEmpty(配置.音频端点标识) Then 端点指针 = Marshal.StringToCoTaskMemUTF8(配置.音频端点标识)
            ' -1 按窗口所在显示器选卡；0 是合法适配器索引。
            ' 自适应CPU预缩放（上传前降档）内核默认是关的，本次所有性能读数都在关闭态下取
            ' —— 快照里 降档重采样时长 恒为 0、bytes_per_upload 恒等于整帧字节数就是证据。
            ' 这条最省字节的通路此前从未被量过；下面那个环境变量只是给测量开门，不设时逐字
            ' 保持现网行为。（注：多行对象初始值设定项里不能夹注释行，说明写在这里。）
            ' 门控打开时阈值一并从现网 20% 压到 1%：本机当前最好的 8K HEVC 臂只丢约 4%，
            ' 用 20% 永远越不过门槛，那一臂就退化成"开了但没启用"，量不到降档的代价与收益。
            ' 解码线程阶梯（自适应解码线程）取 1：内核 PlacementPolicy 的三档放只在非 0 时参与，
            ' 留 0 会把 9b82322（PR #27）那条已合并的通路整个关成死代码。
            Dim 原生配置 As New 原生播放器配置 With {
                .大小 = 原生播放器配置大小, .版本 = 期望API版本,
                .输出窗口 = 配置.输出窗口句柄, .解码器 = CUInt(配置.解码器),
                .色彩模式 = CUInt(配置.色彩模式), .SDR峰值 = 配置.SDR峰值尼特,
                .HDR峰值 = 配置.HDR峰值尼特, .SDR纸白 = 配置.SDR纸白尼特,
                .音频端点UTF8 = 端点指针,
                .回调 = Marshal.GetFunctionPointerForDelegate(共享原生回调),
                .回调上下文 = GCHandle.ToIntPtr(回调句柄),
                .视频缩放质量 = CUInt(配置.缩放质量),
                .强制HDR输出 = If(配置.强制HDR输出, 1UI, 0UI),
                .首选适配器索引 = -1,
                .SDRscRGB模式 = 配置.SDRscRGB模式,
                .自适应CPU预缩放 = If(Environment.GetEnvironmentVariable("FFF3FP_ADAPTIVE_DOWNSCALE") = "1", 1UI, 0UI),
                .自适应CPU预缩放丢帧百分比 = If(Environment.GetEnvironmentVariable("FFF3FP_ADAPTIVE_DOWNSCALE") = "1", 1UI, 20UI),
                .自适应解码线程 = 1UI,
                .软解最小线程数 = 4UI,
                .软解最大线程数 = 32UI,
                .解码升档丢帧百分比 = 10UI,
                .解码降档丢帧百分比 = 2UI
            }
            Dim 原生指针 = IntPtr.Zero
            Dim 结果 = 播放器原生接口.FFF3FP_Create(原生配置, 原生指针)
            If 结果 <> 原生播放器结果.成功 Then Throw New 播放器异常(CInt(结果), "创建 3FP 播放器会话失败。")
            句柄 = New 播放器原生句柄(原生指针, 回调句柄)
            状态.目标 = New WeakReference(Of 播放器会话)(Me)
        Catch
            If 回调句柄.IsAllocated Then 回调句柄.Free()
            Throw
        Finally
            If 端点指针 <> IntPtr.Zero Then Marshal.FreeCoTaskMem(端点指针)
        End Try
        Try
            检查结果(播放器原生接口.FFF3FP_ConfigurePlaybackPerformance(句柄,
                配置.首选解码适配器索引, CUInt(配置.视频缓冲区策略), CUInt(配置.音频缓冲区策略)))
        Catch
            释放()
            Throw
        End Try
    End Sub

    Public Event 播放器事件 As EventHandler(Of 播放器事件参数)
    Public Event 状态变化 As EventHandler(Of 播放器事件参数)
    Public Event 打开完成 As EventHandler(Of 播放器事件参数)
    Public Event 操作完成 As EventHandler(Of 播放器事件参数)
    Public Event 播放结束 As EventHandler(Of 播放器事件参数)
    Public Event 错误 As EventHandler(Of 播放器事件参数)
    Public Event 色彩模式变化 As EventHandler(Of 播放器事件参数)
    Public Event 设备变化 As EventHandler(Of 播放器事件参数)

    Public Sub 打开(本地路径 As String)
        调用路径(AddressOf 播放器原生接口.FFF3FP_Open, 本地路径)
        ' 呈现策略一次性接上：请求 VRR 低时延路径（tearing present）。偏好语义——
        ' 渲染侧只在自适应降速（pace）真正介入时兑现，健康播放仍是 vblank 钳制；
        ' 旧内核没有该导出时静默跳过，不影响播放。
        播放器原生接口.设置呈现配置(取得句柄(), True)
        启动色彩变换通路()
    End Sub

    ' ---- 外部色彩变换通路（kind=3 注入） ----
    ' 三态探测：缺席 / 旧件 ⇒ 静默（绝大多数用户）；已解锁 ⇒ 装载建泵；
    ' 已锁定 ⇒ 建泵走 60 秒补装载（用户可能在启动弹框里输码，也可能切出去找码）。
    ' 铁律：任何意外都按“没有这条通路”处理，绝不能把一个好会话弄坏。
    Private Sub 启动色彩变换通路()
        If Volatile.Read(已释放) <> 0 Then Return
        Try
            ' 旧内核没有环/注入导出：这条路不存在，静默。
            If Not 播放器原生接口.支持帧边数据环 OrElse Not 播放器原生接口.支持动态元数据注入 Then Return

            Dim 原因 As String = Nothing
            Dim 形态 = 色彩变换模块接口.探测(原因)
            If 形态 = 色彩变换模块接口.模块在场形态.缺席 OrElse
              形态 = 色彩变换模块接口.模块在场形态.旧件 Then
                Debug.WriteLine("色彩变换模块：" & 原因)
                Return
            End If

            If 注入泵 Is Nothing Then 注入泵 = New 色彩变换注入泵(Me)

            ' 已解锁且泵里还没有活模块时才装载：避免每次打开文件都重载一遍（431 KB 钉住缓冲 + 握手）。
            If 形态 = 色彩变换模块接口.模块在场形态.就绪 AndAlso Not 注入泵.通路可用 Then
                Dim 模块 = 色彩变换模块接口.尝试装载(原因)
                If 模块 Is Nothing Then
                    Debug.WriteLine("色彩变换模块：" & 原因)
                Else
                    注入泵.采用模块(模块)
                    ' 泵与床渲染插槽共用这一个活实例：再走一次 尝试装载 会再钉一份
                    ' 431 KB 载荷缓冲，而交出去的函数表必须绑在同一份模块映射上。
                    色彩模块实例 = 模块
                End If
            End If

            ' 模块在场且已解锁 ⇒ 把外部床渲染函数表交给内核。未解锁或旧模块时一次都不叫，
            ' 床继续由内核自己的矩阵折叠处理——那是现网行为，不是新的失败分支。
            安装音频渲染插槽()

            ' 已锁定时泵是空转起步的：后台按 2 秒节流补装载，票据一落盘下一拍就转入正常节拍。
            注入泵.启动()
            注入泵.流重启()
        Catch ex As Exception
            Debug.WriteLine("色彩变换模块：通路启动异常（已忽略）：" & ex.GetType().Name & ": " & ex.Message)
        End Try
    End Sub

    ''' <summary>流重启通知：seek / 帧步进 / 停止 / 换文件 / 换轨之后调。
    ''' 内核在这些点把边数据环整个丢掉、presentation index 从那里重来。</summary>
    Private Sub 通知注入泵流重启()
        Try
            注入泵?.流重启()
        Catch ex As Exception
            ' 复位失败不该把调用方（可能是 UI 线程的播放控制）带倒。
        End Try
    End Sub

    ''' <summary>解锁成功后由 UI 线程调用：把通路从“已放弃 / 未采用”里拉回来。
    ''' <para>为什么需要它：泵的补装载预算只有 60 秒（30 × 2s）。用户切出去找码、
    ''' 再回来输码很容易超时；超时后泵线程自行退场，而票据此刻才落盘 —— 不显式
    ''' 唤醒的话这一场播放永远不会开始注入，界面上却已经报了“已解锁”。</para>
    ''' <para>只做探测/装载/采用/启动，不碰流状态：解锁不改变时间线，泵会从环里
    ''' 现有条目接着取。任何失败都答 False，绝不抛。</para></summary>
    Public Function 唤醒色彩变换通路() As Boolean
        If Volatile.Read(已释放) <> 0 Then Return False
        Try
            If Not 播放器原生接口.支持帧边数据环 OrElse
               Not 播放器原生接口.支持动态元数据注入 Then Return False

            Dim 原因 As String = Nothing
            Dim 形态 = 色彩变换模块接口.探测(原因)
            If 形态 <> 色彩变换模块接口.模块在场形态.就绪 Then
                Debug.WriteLine("色彩变换模块：解锁后唤醒未成（" & 形态.ToString() & "）" &
                    If(String.IsNullOrEmpty(原因), "", "：" & 原因))
                Return False
            End If

            If 注入泵 Is Nothing Then 注入泵 = New 色彩变换注入泵(Me)

            If Not 注入泵.通路可用 Then
                Dim 模块 = 色彩变换模块接口.尝试装载(原因)
                If 模块 Is Nothing Then
                    Debug.WriteLine("色彩变换模块：解锁后装载失败（" & 原因 & "）")
                    Return False
                End If
                注入泵.采用模块(模块)
                ' 与 启动色彩变换通路 同一份实例：泵与床渲染插槽必须绑在同一份映射上。
                色彩模块实例 = 模块
                ' 解锁也可能刚刚让音频那一面可用（未解锁时模块拒交那张表）。
                安装音频渲染插槽()
            End If

            ' 线程已退场（补装载预算用尽）时这里重启；还活着则是空操作。
            注入泵.启动()
            Return 注入泵.通路可用
        Catch ex As Exception
            Debug.WriteLine("色彩变换模块：解锁后唤醒异常（已忽略）：" & ex.GetType().Name)
            Return False
        End Try
    End Function

    Public Function 打开Async(本地路径 As String, Optional 取消标记 As CancellationToken = Nothing) As Task
        If 取消标记.IsCancellationRequested Then Return Task.FromCanceled(取消标记)
        Dim 完成源 As New TaskCompletionSource(TaskCreationOptions.RunContinuationsAsynchronously)
        Dim 联合取消 = CancellationTokenSource.CreateLinkedTokenSource(取消标记, 释放取消源.Token)
        Dim 取消注册 As CancellationTokenRegistration
        Dim 已结束 As Integer
        Dim 完成处理 As EventHandler(Of 播放器事件参数) = Nothing
        Dim 错误处理 As EventHandler(Of 播放器事件参数) = Nothing
        Dim 清理 As Action = Sub()
                               RemoveHandler 打开完成, 完成处理
                               RemoveHandler 错误, 错误处理
                               取消注册.Unregister()
                               联合取消.Dispose()
                           End Sub
        完成处理 = Sub(sender, e)
                   If Interlocked.Exchange(已结束, 1) <> 0 Then Return
                   完成源.TrySetResult()
                   清理()
               End Sub
        错误处理 = Sub(sender, e)
                   If Interlocked.Exchange(已结束, 1) <> 0 Then Return
                   完成源.TrySetException(New 播放器异常(-1, 读取事件消息(e.详情JSON)))
                   清理()
               End Sub
        AddHandler 打开完成, 完成处理
        AddHandler 错误, 错误处理
        取消注册 = 联合取消.Token.Register(
            Sub()
                If Interlocked.Exchange(已结束, 1) <> 0 Then Return
                完成源.TrySetCanceled(If(取消标记.IsCancellationRequested, 取消标记, 联合取消.Token))
                If Volatile.Read(已释放) = 0 Then
                    Try
                        播放器原生接口.FFF3FP_Close(取得句柄())
                    Catch
                    End Try
                End If
                清理()
            End Sub)
        ' Register may invoke the callback synchronously when cancellation races
        ' with registration. Do not enqueue an open after the task has completed.
        If Volatile.Read(已结束) <> 0 Then Return 完成源.Task
        Try
            打开(本地路径)
        Catch
            If Interlocked.Exchange(已结束, 1) = 0 Then 清理()
            Throw
        End Try
        Return 完成源.Task
    End Function

    Public Sub 播放()
        检查结果(播放器原生接口.FFF3FP_Play(取得句柄()))
    End Sub
    Public Sub 暂停()
        检查结果(播放器原生接口.FFF3FP_Pause(取得句柄()))
    End Sub
    Public Sub 丢弃音频输出()
        检查结果(播放器原生接口.FFF3FP_DiscardAudioOutput(取得句柄()))
    End Sub
    Public Sub 停止()
        检查结果(播放器原生接口.FFF3FP_Stop(取得句柄()))
        通知注入泵流重启()
    End Sub
    Public Sub 关闭媒体()
        检查结果(播放器原生接口.FFF3FP_Close(取得句柄()))
        通知注入泵流重启()
    End Sub
    Public Sub 跳转(位置 As TimeSpan)
        If 位置 < TimeSpan.Zero Then Throw New ArgumentOutOfRangeException(NameOf(位置))
        检查结果(播放器原生接口.FFF3FP_Seek(取得句柄(), 位置.Ticks))
        通知注入泵流重启()
    End Sub
    Public Sub 跳转到关键帧(位置 As TimeSpan)
        If 位置 < TimeSpan.Zero Then Throw New ArgumentOutOfRangeException(NameOf(位置))
        检查结果(播放器原生接口.FFF3FP_SeekKeyframe(取得句柄(), 位置.Ticks))
        通知注入泵流重启()
    End Sub
    Public Sub 跳转到帧(帧序号 As Long)
        If 帧序号 < 0 Then Throw New ArgumentOutOfRangeException(NameOf(帧序号))
        检查结果(播放器原生接口.FFF3FP_SeekFrame(取得句柄(), 帧序号))
        通知注入泵流重启()
    End Sub
    Public Sub 上一帧()
        检查结果(播放器原生接口.FFF3FP_StepFrame(取得句柄(), -1))
        通知注入泵流重启()
    End Sub
    Public Sub 下一帧()
        检查结果(播放器原生接口.FFF3FP_StepFrame(取得句柄(), 1))
        通知注入泵流重启()
    End Sub
    Public Sub 上一关键帧()
        检查结果(播放器原生接口.FFF3FP_StepKeyframe(取得句柄(), -1))
        通知注入泵流重启()
    End Sub
    Public Sub 下一关键帧()
        检查结果(播放器原生接口.FFF3FP_StepKeyframe(取得句柄(), 1))
        通知注入泵流重启()
    End Sub
    Public Sub 选择视频流(索引 As Integer)
        检查结果(播放器原生接口.FFF3FP_SelectVideoStream(取得句柄(), 索引))
        通知注入泵流重启()
    End Sub
    Public Sub 选择音频流(索引 As Integer)
        检查结果(播放器原生接口.FFF3FP_SelectAudioStream(取得句柄(), 索引))
    End Sub
    Public Sub 加载外部音轨(本地路径 As String, Optional 流索引 As Integer = -1, Optional 偏移 As TimeSpan = Nothing)
        调用路径(Function(h, p) 播放器原生接口.FFF3FP_LoadExternalAudio(h, p, 流索引, 偏移.Ticks), 本地路径)
    End Sub
    Public Sub 清除外部音轨()
        检查结果(播放器原生接口.FFF3FP_ClearExternalAudio(取得句柄()))
    End Sub
    Public Sub 设置外部音轨偏移(偏移 As TimeSpan)
        检查结果(播放器原生接口.FFF3FP_SetExternalAudioOffset(取得句柄(), 偏移.Ticks))
    End Sub
    ''' <summary>内嵌音轨的音频延迟（唇音同步）：正值 = 声音落后于画面。
    ''' 外挂音轨请用 加载外部音轨/设置外部音轨偏移。内核约束 ±10 分钟；
    ''' 会话级偏好：跨 关闭/打开媒体 保留。内核收到后会重 seek 使其立即生效。</summary>
    Public Sub 设置音频延迟(延迟 As TimeSpan)
        检查结果(播放器原生接口.FFF3FP_SetAudioDelay(取得句柄(), 延迟.Ticks))
    End Sub
    Public Sub 设置色彩模式(模式 As 色彩输出模式, SDR峰值尼特 As Single, HDR峰值尼特 As Single,
                         SDR纸白尼特 As Single, Optional 强制HDR输出 As Boolean = False)
        检查结果(播放器原生接口.FFF3FP_SetColorMode(取得句柄(), CUInt(模式), SDR峰值尼特,
                                                  HDR峰值尼特, SDR纸白尼特,
                                                  If(强制HDR输出, 1UI, 0UI)))
    End Sub
    ''' <summary>读回呈现器实际在按什么配速：面板声明值/网格标定值/生效上限/是否锁上/
    ''' 没锁的原因/提交精度，以及屏幕停留格数的带内对账。诊断用，不改变任何行为。
    ''' 窗口翻帧上限本身不可调：内核恒取 min(面板率, 视频率)。</summary>
    Friend Function 读取呈现节律状态() As 原生呈现节律状态
        Dim 值 As New 原生呈现节律状态 With {.大小 = 原生呈现节律状态大小, .版本 = 期望API版本}
        检查结果(播放器原生接口.FFF3FP_GetPresentPacingStatus(取得句柄(), 值))
        Return 值
    End Function
    Public Sub 设置输出窗口(窗口句柄 As IntPtr)
        检查结果(播放器原生接口.FFF3FP_SetOutputWindow(取得句柄(), 窗口句柄))
    End Sub
    Public Sub 设置窗口移动状态(启用 As Boolean)
        检查结果(播放器原生接口.FFF3FP_SetInteractiveMove(取得句柄(), If(启用, 1UI, 0UI)))
    End Sub
    Public Sub 设置360视角(启用 As Boolean, 水平角度 As Single, 垂直角度 As Single,
                       Optional 视场角 As Single = 90.0F)
        If Not Single.IsFinite(水平角度) OrElse Not Single.IsFinite(垂直角度) OrElse
            Not Single.IsFinite(视场角) OrElse 视场角 <= 0 Then
            Throw New ArgumentOutOfRangeException(NameOf(视场角))
        End If
        检查结果(播放器原生接口.FFF3FP_Set360View(取得句柄(), If(启用, 1UI, 0UI),
                                                水平角度, 垂直角度, 视场角))
    End Sub

    ''' <summary>视图旋转（四分一转，顺时针 0..3）。内核不支持时静默忽略（旧内核回落）。</summary>
    Public Sub 设置视图旋转(四分一转 As UInteger)
        If 四分一转 > 3UI Then Throw New ArgumentOutOfRangeException(NameOf(四分一转))
        播放器原生接口.设置视图旋转(取得句柄(), 四分一转)
    End Sub

    ''' <summary>当前视图旋转（四分一转，顺时针）。内核不支持旋转时恒为 0。</summary>
    Public ReadOnly Property 本机旋转 As UInteger
        Get
            ' 内核会在打开媒体时自动转正，必须读取实际方向，不能仅缓存手动设置值。
            Dim 当前 As UInteger
            If 播放器原生接口.读取视图旋转(取得句柄(), 当前) Then Return 当前
            Return 0UI
        End Get
    End Property

    ''' <summary>本机加载的内核是否提供 RTX 视频超分辨率入口。
    ''' 供 UI 决定"置灰 + 说明"还是"可切换"，不是靠试调用捕获异常来猜。</summary>
    Public ReadOnly Property 支持视频超分辨率 As Boolean
        Get
            Return 播放器原生接口.支持视频超分辨率
        End Get
    End Property

    ''' <summary>RTX 视频超分辨率开关：True = 请求 Auto（内核逐帧判定要不要真用），
    ''' False = 请求 Off。
    ''' <para>内核不支持该导出、或返回非成功时**静默忽略**：VSR 是可选增强，
    ''' 不该让一次开关把播放会话变成错误弹窗。UI 侧用 支持视频超分辨率 区分
    ''' "没这个能力"与"有但这一帧没满足条件"。</para>
    ''' <para>注意：重新请求 True 会清掉内核的"驱动拒绝"闩并重新探测，
    ''' 所以关掉再打开就是那条唯一的复位路径。</para></summary>
    Public Sub 设置视频超分辨率(开启 As Boolean)
        If Not 播放器原生接口.设置视频超分辨率(取得句柄(), 开启) Then Return
        本机视频超分辨率已请求 = 开启
    End Sub

    ''' <summary>宿主侧最后一次成功登记的请求（仅供 UI 回显勾选态；
    ''' 真实生效与否要看 读取视频超分辨率状态）。</summary>
    Public Property 本机视频超分辨率已请求 As Boolean = False

    ''' <summary>读取内核的 VSR 状态。内核不支持或未成功时返回 False。
    ''' Friend 而非 Public：状态是 Friend 结构，Public 成员不能外泄它（BC30909）。</summary>
    Friend Function 读取视频超分辨率状态(ByRef 状态 As 原生视频超分辨率状态) As Boolean
        状态 = New 原生视频超分辨率状态 With {
            .大小 = 原生视频超分辨率状态大小, .版本 = 期望API版本}
        Return 播放器原生接口.读取视频超分辨率状态(取得句柄(), 状态)
    End Function

    ''' <summary>本机加载的内核是否提供自动 HDR（TrueHDR）入口。
    ''' 供 UI 决定"置灰 + 说明"还是"可切换"，与 VSR 同一分工。</summary>
    Public ReadOnly Property 支持视频自动HDR As Boolean
        Get
            Return 播放器原生接口.支持视频自动HDR
        End Get
    End Property

    ''' <summary>自动 HDR 开关：True = 请求 Auto（内核逐帧判定要不要真用），False = 请求 Off。
    ''' <para>内核不支持该导出、或返回非成功时**静默忽略**：与 VSR 同性质的可选增强，
    ''' 不该让一次开关把播放会话变成错误弹窗。</para>
    ''' <para>注意两点与 VSR 不同：① compare 模式激活时内核返回 InvalidState——
    ''' 互斥是内核强制的（对比模式不许任何帧增强），宿主不绕过；② 重新请求 True 同样会清掉
    ''' 内核的"驱动拒绝"闩并重新探测，所以关掉再打开就是那条唯一的复位路径。</para></summary>
    Public Sub 设置视频自动HDR(开启 As Boolean)
        If Not 播放器原生接口.设置视频自动HDR(取得句柄(), 开启) Then Return
        本机视频自动HDR已请求 = 开启
    End Sub

    ''' <summary>宿主侧最后一次成功登记的请求（仅供 UI 回显勾选态；
    ''' 真实生效与否要看 读取视频自动HDR状态）。</summary>
    Public Property 本机视频自动HDR已请求 As Boolean = False

    ''' <summary>读取内核的自动 HDR 状态。内核不支持或未成功时返回 False。</summary>
    Friend Function 读取视频自动HDR状态(ByRef 状态 As 原生视频自动HDR状态) As Boolean
        状态 = New 原生视频自动HDR状态 With {
            .大小 = 原生视频自动HDR状态大小, .版本 = 期望API版本}
        Return 播放器原生接口.读取视频自动HDR状态(取得句柄(), 状态)
    End Function

    ''' <summary>把内核的原因码翻成人话。逐值对应 FFF3FPVideoSuperResolutionReason。
    ''' <para>最后一句是刻意的：内核的"生效"只表示驱动没有拒绝这次请求，
    ''' 该路径没有任何能力查询可用（实测连伪造的 GUID 都返回成功），
    ''' 所以这里不能写成"已增强"。</para></summary>
    Friend Shared Function 视频超分辨率状态文本(状态 As 原生视频超分辨率状态) As String
        If 状态.生效 <> 0UI Then Return "驱动已接受本次请求（不等于画面已被增强）"
        Select Case 状态.原因
            ' 内核的 原因 初值就是 NotRequested，不等于用户没勾：请求已下发但本帧还没走到
            ' 超分那条路（自动 HDR 占用处理通道、compare 模式互斥）时它仍是这个值。
            Case 1UI : Return If(状态.请求模式 = 0UI, "未请求",
                               "已请求，内核尚未逐帧判定（自动 HDR 可能占用了处理通道）")
            Case 2UI : Return "渲染适配器不是 NVIDIA"
            Case 3UI : Return "当前不是放大（窗口不大于画面）；把播放窗口拉大或全屏才会生效"
            Case 4UI : Return $"源高度 {状态.源高} 不在 360..1440 区间（4K/8K 源本功能不适用）"
            Case 5UI : Return "源格式不支持送入视频处理器（如软解上传路径）；换硬件解码可播的片源才会生效"
            Case 6UI : Return "驱动拒绝过，已闩住；关掉再打开可重新探测"
            ' 内核明确报了这一值（FFF.Player.Api.h:753 YieldedToAutoHdr），漏了它就会落到
            ' Case Else 报"内核未报告原因"——把一条有名字的读数说成没有。
            Case 7UI : Return "自动 HDR 占用同一条视频处理器通道，本次超分未单独生效（两者互斥）"
            Case Else : Return If(状态.驱动拒绝闩 <> 0UI, "驱动拒绝过", "内核未报告原因")
        End Select
    End Function

    ''' <summary>Tab 图层的超分行：一个判词 + 一句人话，仅此两样。判词固定两态开头
    ''' （已激活 / 未生效），未请求时不显示，判据仍然取自内核的 生效 字段
    ''' 而不是用户的勾选态——勾选态实测不等于生效，拿它点亮就是假凭证。
    ''' 裸读数（active / reason / 尺寸 / 闩）不进界面：人话与字段并排会把行挤到窗口右沿
    ''' 截断，而排查要的原值 FFF3FP_GetVideoSuperResolutionStatus 逐字段都还在报。</summary>
    Friend Shared Function 视频超分辨率图层文本(状态 As 原生视频超分辨率状态) As String
        If 状态.请求模式 = 0UI Then Return Nothing
        If 状态.生效 <> 0UI Then Return "已激活 · 驱动已接受（不等于画面已被增强）"
        Return "未生效 · " & 视频超分辨率状态文本(状态)
    End Function

    ''' <summary>把内核的原因码翻成人话。逐值对应 FFF3FPVideoAutoHdrReason。
    ''' <para>与 VSR 的本质差别写进文案：TrueHDR 有真实的能力探测
    ''' （VideoProcessorGetStreamExtension 读回 1），所以 "生效" 比 VSR 的"驱动没拒绝"
    ''' 更强——但它仍然不是画质保证：生成的 HDR 是驱动算的，强度档位归 NVIDIA App，
    ''' 本程序设不了也读不回来。文案说"已启用"不说"已增强"。</para></summary>
    Friend Shared Function 视频自动HDR状态文本(状态 As 原生视频自动HDR状态) As String
        If 状态.生效 <> 0UI Then
            Return $"自动 HDR 已启用（驱动探测器通过，最后一帧 Blt 成功；" &
                   $"源 {状态.源宽}×{状态.源高} → PQ/BT.2020 {状态.输出宽}×{状态.输出高}）"
        End If
        Select Case 状态.原因
            ' 与超分同理：NotRequested 是内核初值，请求已下发但本帧还没走到这条通道时
            ' 它仍是这个值，不能翻成"未请求"。
            Case 1UI : Return If(状态.请求模式 = 0UI, "未请求", "已请求，内核尚未逐帧判定")
            Case 2UI : Return "渲染适配器不是 NVIDIA"
            Case 3UI : Return "驱动不支持该私有流扩展（探测未读到 1）"
            Case 4UI : Return "当前源不是 SDR（HDR 源不予转换）"
            Case 5UI : Return "当前输出路径不是 HDR（需 10 位 HDR 输出契约）"
            Case 6UI : Return "源格式不支持送入视频处理器"
            Case 7UI : Return "驱动拒绝或 Blt 失败，已闩住；关掉再打开可重新探测"
            Case Else : Return If(状态.驱动拒绝闩 <> 0UI, "驱动拒绝过", "内核未报告原因")
        End Select
    End Function

    ''' <summary>Tab 图层的自动 HDR 行，与超分行同一条规则：判词 + 一句人话，不摊字段。
    ''' 判词取自内核的 生效 而不是勾选态——自动 HDR 与 compare 模式互斥由内核强制，
    ''' 那类场合同样是"已请求但未启用"。TrueHDR 的能力探测没有独立读数可报
    ''' （内核把"探测读回 1"折进了 生效），所以这里不写"探测通过"之类的话，
    ''' 那等于替内核报一个它没报的数。</summary>
    Friend Shared Function 视频自动HDR图层文本(状态 As 原生视频自动HDR状态) As String
        If 状态.请求模式 = 0UI Then Return Nothing
        If 状态.生效 <> 0UI Then Return "已启用 · SDR → HDR10（驱动 TrueHDR）"
        Return "未生效 · " & 视频自动HDR状态文本(状态)
    End Function


    Private 自动转正偏好 As Boolean = True

    ''' <summary>自动转正是否启用（默认 True）。关闭会把当前视图旋转归零；
    ''' 重新启用后由下一次打开媒体重新套用（内核在打开时读取源旋转）。</summary>
    Public Property 自动转正已启用 As Boolean
        Get
            Return 自动转正偏好
        End Get
        Set(值 As Boolean)
            自动转正偏好 = 值
            If Not 值 Then 设置视图旋转(0UI)
        End Set
    End Property

    ''' <summary>手动旋转：R = 顺时针 90°，Shift+R = 逆时针 90°。
    ''' 手动值覆盖自动值，直到用户按 R 转满一圈或重新打开媒体。</summary>
    Public Sub 手动旋转(顺时针 As Boolean)
        Dim 当前 = 本机旋转
        设置视图旋转(If(顺时针, (当前 + 1UI) Mod 4UI, (当前 + 3UI) Mod 4UI))
    End Sub

    ' 以下四个导出是 additive 新增（内核 API 版本号未升），旧内核 DLL 没有这些符号；
    ' 静态 DllImport 在符号缺失时会抛 EntryPointNotFoundException，而启动时的
    ' FFF3FP_GetApiVersion 版本门探测不出这种差异。与 读取音频渲染插槽状态() 的既有
    ' 惯例一致：调用点捕获并降级（倍速/均衡器/缩放算法静默回退内核默认，Redraw 跳过），
    ' 并缓存判定结果，避免旧内核下每次调用都走异常路径。
    Private 支持倍速导出 As Boolean? = Nothing
    Private 支持均衡器导出 As Boolean? = Nothing
    Private 支持缩放导出 As Boolean? = Nothing
    Private 支持重绘导出 As Boolean? = Nothing

    Friend Sub 设置播放倍速(倍速 As Double)
        If 支持倍速导出.HasValue AndAlso Not 支持倍速导出.Value Then Return
        Try
            检查结果(播放器原生接口.FFF3FP_SetPlaybackRate(取得句柄(), 倍速))
            支持倍速导出 = True
        Catch ex As EntryPointNotFoundException
            支持倍速导出 = False
        End Try
    End Sub
    Friend Sub 设置均衡器(启用 As Boolean, 增益 As Single())
        If 支持均衡器导出.HasValue AndAlso Not 支持均衡器导出.Value Then Return
        Try
            检查结果(播放器原生接口.FFF3FP_SetAudioEffects(取得句柄(), 启用, 增益, 1.0F))
            支持均衡器导出 = True
        Catch ex As EntryPointNotFoundException
            支持均衡器导出 = False
        End Try
    End Sub
    Friend Sub 重绘画面()
        If 支持重绘导出.HasValue AndAlso Not 支持重绘导出.Value Then Return
        Try
            播放器原生接口.FFF3FP_Redraw(取得句柄())
            支持重绘导出 = True
        Catch ex As EntryPointNotFoundException
            支持重绘导出 = False
        End Try
    End Sub
    Friend Sub 设置缩放算法(模式 As UInteger)
        If 支持缩放导出.HasValue AndAlso Not 支持缩放导出.Value Then Return
        Try
            检查结果(播放器原生接口.FFF3FP_SetScalingMode(取得句柄(), 模式))
            支持缩放导出 = True
        Catch ex As EntryPointNotFoundException
            支持缩放导出 = False
        End Try
    End Sub
    ''' <summary>图片模式：设置缩放与平移。缩放=1 表示适应窗口。</summary>
    Public Sub 设置视图变换(缩放 As Single, 水平平移 As Single, 垂直平移 As Single)
        If Not Single.IsFinite(缩放) OrElse 缩放 <= 0.0F OrElse
            Not Single.IsFinite(水平平移) OrElse Not Single.IsFinite(垂直平移) Then
            Throw New ArgumentOutOfRangeException(NameOf(缩放))
        End If
        检查结果(播放器原生接口.FFF3FP_SetViewTransform(取得句柄(), 缩放, 水平平移, 垂直平移))
    End Sub

    ''' <summary>按客户区像素增量平移，保留内核光标缩放后的视图状态。</summary>
    Public Function 平移视图(水平位移 As Single, 垂直位移 As Single) As Boolean
        If Not Single.IsFinite(水平位移) OrElse Not Single.IsFinite(垂直位移) Then
            Throw New ArgumentOutOfRangeException(NameOf(水平位移))
        End If
        Return 播放器原生接口.平移视图(取得句柄(), 水平位移, 垂直位移)
    End Function

    Public Function 读取图片缩放(ByRef 当前倍率 As Single, ByRef 最小倍率 As Single,
                             ByRef 最大倍率 As Single) As Boolean
        Return 播放器原生接口.读取图片缩放(取得句柄(), 当前倍率, 最小倍率, 最大倍率)
    End Function

    ''' <summary>图片模式：以光标为锚做一次缩放步进（放大 1.25 / 缩小 0.8）。
    ''' 锚点按客户区归一化到 [0,1]，由内核保持该点下的画面内容不动。
    ''' 成功时 结果缩放 返回缩放后的绝对值，便于宿主同步本地缓存。
    ''' 成功时 结果缩放 按源像素计，1 表示原生尺寸。
    ''' 返回 False 表示当前内核没有该导出，调用方应回退。</summary>
    Public Function 光标锚定缩放(倍数 As Single, 锚点水平 As Single, 锚点垂直 As Single,
                                  ByRef 结果缩放 As Single) As Boolean
        If Not Single.IsFinite(倍数) OrElse 倍数 <= 0.0F OrElse
            Not Single.IsFinite(锚点水平) OrElse Not Single.IsFinite(锚点垂直) Then
            Throw New ArgumentOutOfRangeException(NameOf(倍数))
        End If
        Dim 调用 = 播放器原生接口.取光标锚定缩放委托()
        If 调用 Is Nothing Then Return False
        ' 把结果缩放的接收位置钉住，避免 GC 在调用期间移动它。
        Dim 缓冲(0) As Single
        Dim 句柄 = GCHandle.Alloc(缓冲, GCHandleType.Pinned)
        Try
            检查结果(调用(取得句柄(), 倍数, 锚点水平, 锚点垂直, 句柄.AddrOfPinnedObject()))
            结果缩放 = 缓冲(0)
        Finally
            句柄.Free()
        End Try
        Return True
    End Function

    ' 视口封顶开关（API 16 新增导出）：开启后适配盒不超过源原生尺寸，缩放值就是绝对的
    ' 屏幕:视频 像素比。内核把这一偏好存在渲染器里，设备重建后不丢，所以不必在恢复路径重发。
    Public Sub 设置原生尺寸封顶(开启 As Boolean)
        检查结果(播放器原生接口.FFF3FP_SetFitLimitToNative(取得句柄(), If(开启, 1UI, 0UI)))
    End Sub

    ''' <summary>图片模式：读取帧数、动画标志、EXIF 旋转、源格式位深、ICC 大小与色彩信息。
    ''' 当前媒体不支持图片信息时返回 Nothing。</summary>
    Friend Function 取图片信息() As 原生图片信息?
        Dim 信息 As New 原生图片信息 With {
            .大小 = CUInt(Marshal.SizeOf(GetType(原生图片信息))),
            .版本 = 20UI
        }
        Dim 结果 = 播放器原生接口.FFF3FP_GetImageInfo(取得句柄(), 信息)
        ' "不支持"是正常回落，不当作错误：调用方只关心"是不是动图"，读不到就按不是处理。
        If 结果 = 原生播放器结果.不支持 Then Return Nothing
        检查结果(结果)
        Return 信息
    End Function
    ' 床渲染插槽的会话侧状态：实例来自色彩侧装载的那一次（同一份模块映射），
    ' 装过一次就不再重复握手——每次重装都会让模块新建一个渲染上下文。
    Private 色彩模块实例 As 色彩变换模块实例
    Private 音频插槽已装 As Boolean
    Private ReadOnly 音频插槽锁 As New Object()
    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Private Delegate Sub 释放音频上下文(上下文 As IntPtr)

    ''' <summary>把模块的外部床渲染函数表交给本会话的内核。任何一步不成立都静默返回：
    ''' 缺席、旧件、未解锁、镜像尺寸不同水、内核拒绝——全部保持矩阵折叠。</summary>
    Private Sub 安装音频渲染插槽()
        SyncLock 音频插槽锁
            安装音频渲染插槽核心()
        End SyncLock
    End Sub

    Private Sub 安装音频渲染插槽核心()
        If Volatile.Read(空间音频已启用) = 0 OrElse 音频插槽已装 OrElse 色彩模块实例 Is Nothing Then Return
        Dim 原因 As String = Nothing
        Dim 表 = 色彩模块实例.尝试取音频渲染表(原因)
        If Not 表.HasValue Then
            If 原因 IsNot Nothing Then Debug.WriteLine("音频渲染插槽：" & 原因)
            Return
        End If
        Dim 本地表 = 表.Value
        Dim 钉住 = GCHandle.Alloc(本地表, GCHandleType.Pinned)
        Dim 已交给内核 As Boolean
        Try
            Dim 结果 = 播放器原生接口.FFF3FP_SetAudioRenderPlug(取得句柄(), 钉住.AddrOfPinnedObject())
            If 结果 = 原生播放器结果.成功 Then
                已交给内核 = True
                音频插槽已装 = True
            Else
                Debug.WriteLine("音频渲染插槽：内核拒绝 " & 结果.ToString())
            End If
        Finally
            钉住.Free()
            ' 只有成功装载才转移上下文所有权；拒绝或异常时由调用方归还。
            If Not 已交给内核 AndAlso 本地表.释放 <> IntPtr.Zero AndAlso 本地表.上下文 <> IntPtr.Zero Then
                Marshal.GetDelegateForFunctionPointer(Of 释放音频上下文)(本地表.释放).Invoke(本地表.上下文)
            End If
        End Try
    End Sub

    Public Sub 设置空间音频(启用 As Boolean)
        SyncLock 音频插槽锁
            Volatile.Write(空间音频已启用, If(启用, 1, 0))
            If 启用 Then
                安装音频渲染插槽核心()
            ElseIf 音频插槽已装 Then
                检查结果(播放器原生接口.FFF3FP_SetAudioRenderPlug(取得句柄(), IntPtr.Zero))
                音频插槽已装 = False
            End If
        End SyncLock
    End Sub

    ''' <summary>插槽读数：0/1 + 已渲染与已拒绝帧数。给信息层与测试用，不抛。</summary>
    Public Function 读取音频渲染插槽状态() As (生效 As Integer, 已渲染帧数 As ULong, 已拒绝帧数 As ULong)
        Dim 生效UI As UInteger
        Dim 已渲染 As ULong, 已拒绝 As ULong
        Try
            播放器原生接口.FFF3FP_GetAudioRenderPlugState(取得句柄(), 生效UI, IntPtr.Zero, 0UI, 已渲染, 已拒绝)
        Catch ex As EntryPointNotFoundException
            Return (0UL, 0UL, 0UL)
        End Try
        Return (CInt(生效UI), 已渲染, 已拒绝)
    End Function

    Public Sub 设置音频端点(端点标识 As String)
        Dim 指针 = IntPtr.Zero
        Try
            If Not String.IsNullOrWhiteSpace(端点标识) Then
                指针 = Marshal.StringToCoTaskMemUTF8(端点标识)
            End If
            ' 空指针是原生 API 的“跟随 Windows 默认输出设备”契约。
            检查结果(播放器原生接口.FFF3FP_SetAudioEndpoint(取得句柄(), 指针))
        Finally
            If 指针 <> IntPtr.Zero Then Marshal.FreeCoTaskMem(指针)
        End Try
    End Sub
    Public Sub 设置WASAPI独占模式(独占 As Boolean)
        检查结果(播放器原生接口.FFF3FP_SetAudioExclusiveMode(取得句柄(), If(独占, 1UI, 0UI)))
    End Sub
    Public Sub 设置音量(音量 As Single, Optional 静音 As Boolean = False)
        If Not Single.IsFinite(音量) OrElse 音量 < 0 OrElse 音量 > 1 Then Throw New ArgumentOutOfRangeException(NameOf(音量))
        检查结果(播放器原生接口.FFF3FP_SetVolume(取得句柄(), 音量, If(静音, 1UI, 0UI)))
    End Sub

    Public Sub 设置定时文字图层(画布大小 As Size, 命令 As IReadOnlyList(Of 定时文字命令),
                           序号 As ULong, 目标帧率 As Single)
        设置定时文字图层核心(画布大小, 命令, 序号, 目标帧率, 原生定时文字图层槽位.字幕)
    End Sub

    Public Sub 设置弹幕图层(画布大小 As Size, 命令 As IReadOnlyList(Of 定时文字命令),
                         序号 As ULong, 目标帧率 As Single)
        设置定时文字图层核心(画布大小, 命令, 序号, 目标帧率, 原生定时文字图层槽位.弹幕)
    End Sub

    Public Sub 设置播放器信息图层(画布大小 As Size, 命令 As IReadOnlyList(Of 定时文字命令),
                              序号 As ULong, 目标帧率 As Single)
        设置定时文字图层核心(画布大小, 命令, 序号, 目标帧率, 原生定时文字图层槽位.播放器信息)
    End Sub

    Public Sub 设置歌词图层(画布大小 As Size, 命令 As IReadOnlyList(Of 定时文字命令),
                       序号 As ULong, 目标帧率 As Single, 呈现设置 As 歌词呈现设置)
        If Not Single.IsFinite(呈现设置.模糊半径) OrElse 呈现设置.模糊半径 < 1.0F OrElse
            呈现设置.模糊半径 > 96.0F OrElse 呈现设置.模糊次数 < 0 OrElse
            呈现设置.模糊次数 > 5 OrElse 呈现设置.下采样倍率 < 1 OrElse
            呈现设置.下采样倍率 > 16 OrElse
            Not Single.IsFinite(呈现设置.封面区域宽度百分比) OrElse
            Not Single.IsFinite(呈现设置.歌词区域宽度百分比) OrElse
            呈现设置.封面区域宽度百分比 <= 0.0F OrElse
            呈现设置.歌词区域宽度百分比 <= 0.0F OrElse
            Not Single.IsFinite(呈现设置.封面左内边距百分比) OrElse
            Not Single.IsFinite(呈现设置.封面右内边距百分比) OrElse
            Not Single.IsFinite(呈现设置.封面垂直内边距百分比) OrElse
            呈现设置.封面左内边距百分比 < 0.0F OrElse
            呈现设置.封面左内边距百分比 >= 50.0F OrElse
            呈现设置.封面右内边距百分比 < 0.0F OrElse
            呈现设置.封面右内边距百分比 >= 50.0F OrElse
            呈现设置.封面垂直内边距百分比 < 0.0F OrElse
            呈现设置.封面垂直内边距百分比 >= 50.0F Then
            Throw New ArgumentOutOfRangeException(NameOf(呈现设置))
        End If
        设置定时文字图层核心(画布大小, 命令, 序号, 目标帧率,
                            原生定时文字图层槽位.歌词, 呈现设置)
    End Sub

    Private Sub 设置定时文字图层核心(画布大小 As Size, 命令 As IReadOnlyList(Of 定时文字命令),
                                序号 As ULong, 目标帧率 As Single,
                                图层槽位 As 原生定时文字图层槽位,
                                Optional 呈现设置 As 歌词呈现设置? = Nothing)
        If 画布大小.Width <= 0 OrElse 画布大小.Height <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(画布大小))
        If Not Single.IsFinite(目标帧率) OrElse 目标帧率 < 1.0F OrElse 目标帧率 > 240.0F Then
            Throw New ArgumentOutOfRangeException(NameOf(目标帧率))
        End If
        ArgumentNullException.ThrowIfNull(命令)
        SyncLock 定时文字提交锁
            ' 此缓冲和 UTF-8 指针只为同步 P/Invoke 提供稳定地址。原生层必须在返回前
            ' 深拷贝命令；不得保留这些地址。缓存有界，避免 60 Hz 弹幕提交反复分配。
            确保定时文字原生容量(命令.Count)
            定时文字临时指针.Clear()
            定时文字位图固定句柄.Clear()
            Try
                For index = 0 To 命令.Count - 1
                    Dim source = 命令(index)
                    ArgumentNullException.ThrowIfNull(source)
                    Dim value As New 原生定时文字命令 With {
                        .大小 = 原生定时文字命令大小, .版本 = 20UI,
                        .类型 = If(source.是位图, 原生定时文字命令类型.位图, 原生定时文字命令类型.文字),
                        .标志 = CType(CUInt(source.样式), 原生定时文字标志),
                        .X = source.X, .Y = source.Y, .宽度 = source.宽度, .高度 = source.高度,
                        .前景色ARGB = source.前景色ARGB, .描边色ARGB = source.描边色ARGB,
                        .字号 = source.字号, .描边宽度 = source.描边宽度,
                        .阴影色ARGB = source.阴影色ARGB,
                        .阴影X偏移 = source.阴影X偏移, .阴影Y偏移 = source.阴影Y偏移,
                        .水平对齐 = CType(CUInt(source.水平对齐), 原生定时文字对齐),
                        .垂直对齐 = CType(CUInt(source.垂直对齐), 原生定时文字对齐),
                        .内容标识 = source.内容标识}
                    If source.是位图 Then
                        ArgumentNullException.ThrowIfNull(source.位图像素BGRA)
                        Dim pinned = GCHandle.Alloc(source.位图像素BGRA, GCHandleType.Pinned)
                        定时文字位图固定句柄.Add(pinned)
                        value.位图BGRA = pinned.AddrOfPinnedObject()
                        value.位图宽度 = CUInt(source.位图宽度)
                        value.位图高度 = CUInt(source.位图高度)
                        value.位图行跨度 = CUInt(source.位图行跨度)
                        value.位图字节数 = CUInt(source.位图像素BGRA.Length)
                    Else
                        value.文本UTF8 = 取得定时文字UTF8指针(If(source.文本, String.Empty))
                        value.字体UTF8 = 取得定时文字UTF8指针(If(source.字体, String.Empty))
                    End If
                    定时文字原生缓冲(index) = value
                Next
                Dim layer As New 原生定时文字图层 With {
                    .大小 = 原生定时文字图层大小, .版本 = 20UI,
                    .画布宽度 = CUInt(画布大小.Width), .画布高度 = CUInt(画布大小.Height),
                    .命令数 = CUInt(命令.Count), .图层槽位 = 图层槽位, .序号 = 序号,
                    .命令 = If(命令.Count = 0, IntPtr.Zero, 定时文字原生缓冲句柄.AddrOfPinnedObject()),
                    .目标帧率 = 目标帧率}
                If 呈现设置.HasValue Then
                    Dim settings = 呈现设置.Value
                    layer.封面毛玻璃半径 = settings.模糊半径
                    layer.封面毛玻璃次数 = CUInt(settings.模糊次数)
                    layer.封面毛玻璃下采样倍率 = CUInt(settings.下采样倍率)
                    layer.封面毛玻璃遮罩颜色ARGB = settings.遮罩颜色ARGB
                    layer.封面区域宽度百分比 = settings.封面区域宽度百分比
                    layer.歌词区域宽度百分比 = settings.歌词区域宽度百分比
                    layer.封面区域左内边距百分比 = settings.封面左内边距百分比
                    layer.封面区域垂直内边距百分比 = settings.封面垂直内边距百分比
                    layer.封面区域右内边距百分比 = settings.封面右内边距百分比
                End If
                检查结果(播放器原生接口.FFF3FP_SetTimedTextLayer(取得句柄(), layer))
            Finally
                For Each pinned In 定时文字位图固定句柄
                    If pinned.IsAllocated Then pinned.Free()
                Next
                For Each pointer In 定时文字临时指针
                    If pointer <> IntPtr.Zero Then Marshal.FreeCoTaskMem(pointer)
                Next
                定时文字位图固定句柄.Clear()
                定时文字临时指针.Clear()
            End Try
        End SyncLock
    End Sub

    Private Sub 确保定时文字原生容量(所需 As Integer)
        If 所需 <= 定时文字原生缓冲.Length Then Return
        Dim capacity = Math.Max(16, 定时文字原生缓冲.Length)
        While capacity < 所需
            capacity *= 2
        End While
        If 定时文字原生缓冲句柄.IsAllocated Then 定时文字原生缓冲句柄.Free()
        ReDim 定时文字原生缓冲(capacity - 1)
        定时文字原生缓冲句柄 = GCHandle.Alloc(定时文字原生缓冲, GCHandleType.Pinned)
    End Sub

    Private Function 取得定时文字UTF8指针(值 As String) As IntPtr
        Dim pointer As IntPtr
        If 定时文字UTF8缓存.TryGetValue(值, pointer) Then Return pointer
        pointer = Marshal.StringToCoTaskMemUTF8(值)
        If 定时文字UTF8缓存.Count < 定时文字UTF8缓存上限 Then
            定时文字UTF8缓存.Add(值, pointer)
        Else
            定时文字临时指针.Add(pointer)
        End If
        Return pointer
    End Function

    Public ReadOnly Property 当前定时文字状态 As 定时文字状态
        Get
            Return 读取定时文字状态(AddressOf 播放器原生接口.FFF3FP_GetTimedTextStatus)
        End Get
    End Property

    Public ReadOnly Property 当前弹幕状态 As 定时文字状态
        Get
            Return 读取定时文字状态(AddressOf 播放器原生接口.FFF3FP_GetDanmakuStatus)
        End Get
    End Property

    Public ReadOnly Property 当前歌词状态 As 定时文字状态
        Get
            Return 读取定时文字状态(AddressOf 播放器原生接口.FFF3FP_GetLyricsStatus)
        End Get
    End Property

    Private Delegate Function 定时文字状态调用(句柄 As 播放器原生句柄, ByRef 值 As 原生定时文字状态) As 原生播放器结果

    Private Function 读取定时文字状态(调用 As 定时文字状态调用) As 定时文字状态
        Dim 值 As New 原生定时文字状态 With {.大小 = 原生定时文字状态大小, .版本 = 20UI}
        检查结果(调用(取得句柄(), 值))
        Return New 定时文字状态(值)
    End Function

    ''' <summary>播放器当下状态的快照 —— 语义就是"每次读都是最新的"，所以必须每次现取，绝不能缓存。</summary>
    ''' <remarks>命中 .NET 11 的 WFO2000（getter 内构造新引用实例），已在 .editorconfig 中按本文件作用域抑制；理由见那里。</remarks>
    Public ReadOnly Property 当前快照 As 播放器快照
        Get
            Dim 值 As New 原生播放器快照 With {.大小 = 原生播放器快照大小, .版本 = 20UI}
            检查结果(播放器原生接口.FFF3FP_GetSnapshot(取得句柄(), 值))
            Return New 播放器快照(值)
        End Get
    End Property

    Public Function 读取视频输出原始像素(X As Integer, Y As Integer) As 视频输出原始像素
        If X < 0 Then Throw New ArgumentOutOfRangeException(NameOf(X))
        If Y < 0 Then Throw New ArgumentOutOfRangeException(NameOf(Y))
        Dim 值 As New 原生视频像素探针 With {
            .大小 = 原生视频像素探针大小,
            .版本 = 20UI, .X = CUInt(X), .Y = CUInt(Y)}
        检查结果(播放器原生接口.FFF3FP_ReadVideoPixel(取得句柄(), 值))
        Return New 视频输出原始像素(值)
    End Function

    Public Function 读取视频输出像素(X As Integer, Y As Integer) As Color
        Dim 值 = 读取视频输出原始像素(X, Y)
        Return Color.FromArgb(通道到字节(值.Alpha), 通道到字节(值.红),
                              通道到字节(值.绿), 通道到字节(值.蓝))
    End Function

    Private Shared Function 通道到字节(值 As Single) As Integer
        Return CInt(Math.Round(Math.Clamp(值, 0.0F, 1.0F) * 255.0F))
    End Function

    Public Function 读取音频峰值() As Single()
        Dim 值 = 读取原生音频峰值()
        Dim 数量 = Math.Min(CInt(值.声道数), 8)
        Return {值.峰值1, 值.峰值2, 值.峰值3, 值.峰值4, 值.峰值5, 值.峰值6, 值.峰值7, 值.峰值8}.Take(数量).ToArray()
    End Function

    Public Function 读取输入音频峰值() As Single()
        Dim 值 = 读取原生音频峰值()
        Dim 数量 = Math.Min(CInt(值.输入声道数), 8)
        Return 值.输入峰值.Take(数量).ToArray()
    End Function

    Private Function 读取原生音频峰值() As 原生音频峰值
        Dim 值 As New 原生音频峰值 With {
            .大小 = 原生音频峰值大小, .版本 = 20UI, .输入峰值 = New Single(7) {}}
        检查结果(播放器原生接口.FFF3FP_GetAudioPeakLevels(取得句柄(), 值))
        Return 值
    End Function

    Public ReadOnly Property 当前媒体信息 As 媒体信息
        Get
            Dim JSON = 读取原生文本(AddressOf 播放器原生接口.FFF3FP_GetMediaInfo)
            If String.IsNullOrWhiteSpace(JSON) Then Return Nothing
            Dim 信息 = JsonSerializer.Deserialize(Of 媒体信息)(JSON)
            Dim 快照 = 当前快照
            Dim 当前视频 = 信息?.流.FirstOrDefault(Function(流) 流.索引 = 快照.当前视频流 AndAlso 流.类型 = "video")
            If 当前视频 IsNot Nothing Then
                ' 原生媒体 JSON 缓存静态信息；逐帧处理结果以实时快照为准。
                ' 外部色彩变换已启用由内核自报（HDR处理路径=外部色彩变换）；外部色彩变换可用
                ' 内核不写（该字段在两侧都是死读位），由宿主按“模块 DLL 在盘上”回填——
                ' 只查文件存在，不装载、不握手，每次调用都是廉价读数。
                当前视频.外部色彩变换已启用 = 快照.HDR处理路径 = HDR处理路径.外部色彩变换
                当前视频.外部色彩变换可用 = 色彩变换模块接口.模块在盘上()
                当前视频.动态HDR元数据 = 快照.动态HDR元数据有效
                当前视频.HDR回退 = 快照.HDR回退有效
            End If
            Return 信息
        End Get
    End Property

    Public ReadOnly Property 最后错误消息 As String
        Get
            Return 读取原生文本(AddressOf 播放器原生接口.FFF3FP_GetLastError)
        End Get
    End Property

    Public ReadOnly Property 当前光盘状态 As 光盘状态
        Get
            Return JsonSerializer.Deserialize(Of 光盘状态)(读取原生文本(AddressOf 播放器原生接口.FFF3FP_GetDiscStatus))
        End Get
    End Property

    Public Sub 光盘导航(命令 As 光盘命令, Optional 参数 As Integer = 0, Optional Y As Integer = 0)
        检查结果(播放器原生接口.FFF3FP_DiscNavigate(取得句柄(), CInt(命令), 参数, Y))
    End Sub

    ''' <summary>离屏回读一帧（截图用）。内核不支持或没有画面时返回 Nothing。
    ''' 8 位返回 Format32bppArgb 位图；16 位返回 Format64bppArgb（线性 scRGB 半浮点，
    ''' 1.0 = 80 nits），由调用方决定如何映射/保存。
    ''' ⚠ 与屏幕抓取不同：这不依赖窗口可见，被遮挡或最小化时同样正确。</summary>
    Public Function 读取原始帧() As Bitmap
        If Not 播放器原生接口.支持帧回读 Then Return Nothing
        Dim 宽 As UInteger = 0UI, 高 As UInteger = 0UI
        If Not 播放器原生接口.取帧尺寸(取得句柄(), 宽, 高) Then Return Nothing
        ' 先按 8 位问；若交换链是 16 位（HDR/广色域 SDR），改按 16 位取，避免量化丢精度。
        Dim 位深 As UInteger = If(当前快照.视频输出位深度 >= 16, 16UI, 8UI)
        Dim 每像素 = If(位深 = 16UI, 8UI, 4UI)
        Dim 字节数 = CULng(宽) * 高 * 每像素
        If 字节数 = 0UL OrElse 字节数 > 268435456UL Then Return Nothing   ' 上限 256 MB
        Dim 缓冲(CInt(字节数) - 1) As Byte
        Dim 固定 = GCHandle.Alloc(缓冲, GCHandleType.Pinned)
        Try
            Dim 实际宽 = 宽, 实际高 = 高, 实际位深 = 位深
            If Not 播放器原生接口.复制帧(取得句柄(), 固定.AddrOfPinnedObject(),
                                          CUInt(缓冲.Length), 实际宽, 实际高, 实际位深) Then Return Nothing
            Dim 像素格式 = If(实际位深 = 16UI, Imaging.PixelFormat.Format64bppArgb,
                                                Imaging.PixelFormat.Format32bppArgb)
            Using 临时 As New Bitmap(CInt(实际宽), CInt(实际高),
                                     CInt(CULng(实际宽) * If(实际位深 = 16UI, 8UI, 4UI)),
                                     像素格式, 固定.AddrOfPinnedObject())
                ' Clone 出来，让位图不再依赖这块会被回收的缓冲。
                Return 临时.Clone(New Rectangle(0, 0, CInt(实际宽), CInt(实际高)), 像素格式)
            End Using
        Finally
            固定.Free()
        End Try
    End Function

    Public Function 读取SDR合成帧(Optional 仅光盘层 As Boolean = False) As Bitmap
        Dim 宽, 高 As UInteger
        Dim 模式 = If(仅光盘层, 1UI, 0UI)
        Dim 结果 = 播放器原生接口.FFF3FP_CopySdrFrame(取得句柄(), IntPtr.Zero, 0, 宽, 高, 模式)
        If 结果 <> 原生播放器结果.缓冲区不足 Then 检查结果(结果)
        If 宽 = 0 OrElse 高 = 0 OrElse CULng(宽) * 高 > 16777216UL Then Throw New InvalidOperationException("帧尺寸无效。")
        Dim 字节(CInt(CULng(宽) * 高 * 4) - 1) As Byte
        Dim 固定 = GCHandle.Alloc(字节, GCHandleType.Pinned)
        Try
            检查结果(播放器原生接口.FFF3FP_CopySdrFrame(取得句柄(), 固定.AddrOfPinnedObject(), CUInt(字节.Length), 宽, 高, 模式))
            Using 临时 As New Bitmap(CInt(宽), CInt(高), CInt(宽 * 4), Imaging.PixelFormat.Format32bppPArgb, 固定.AddrOfPinnedObject())
                Return 临时.Clone(New Rectangle(0, 0, CInt(宽), CInt(高)), Imaging.PixelFormat.Format32bppPArgb)
            End Using
        Finally
            固定.Free()
        End Try
    End Function

    ''' <summary>色彩变换通路的诊断摘要（测试与日志用；通路未启动时答“未启动”）。</summary>
    Friend ReadOnly Property 色彩变换诊断 As String
        Get
            Dim 泵 = 注入泵
            If 泵 Is Nothing Then Return "通路未启动（模块缺席、未装载或内核无环导出）"
            Return 泵.描述计数器() & "；" & 泵.描述模块态()
        End Get
    End Property

    ''' <summary>色彩变换通路已读到的内容分类文本（模块产出；泵未启动 / 未解锁 / 无分类时答空串）。</summary>
    Friend ReadOnly Property 色彩变换内容描述 As String
        Get
            Dim 泵 = 注入泵
            If 泵 Is Nothing Then Return String.Empty
            Return 泵.读取内容描述()
        End Get
    End Property

    Public Sub 释放() Implements IDisposable.Dispose
        If Interlocked.Exchange(已释放, 1) <> 0 Then Return
        释放取消源.Cancel()
        ' 先停注入泵：泵的每次原生调用都要用活句柄，停止（含线程 join）必须在句柄销毁前，
        ' 否则就是拿着正在销毁的句柄进 CopyFrameSideDataAfter（use-after-free）。
        Try
            注入泵?.Dispose()
        Catch ex As Exception
            ' 停泵失败不该阻断会话的其余清理。
        End Try
        SyncLock 定时文字提交锁
            句柄.Dispose()
            If 定时文字原生缓冲句柄.IsAllocated Then 定时文字原生缓冲句柄.Free()
            For Each pointer In 定时文字UTF8缓存.Values
                If pointer <> IntPtr.Zero Then Marshal.FreeCoTaskMem(pointer)
            Next
            定时文字UTF8缓存.Clear()
            定时文字原生缓冲 = Array.Empty(Of 原生定时文字命令)()
        End SyncLock
        释放取消源.Dispose()
        Dim 忽略 As 播放器事件参数 = Nothing
        While 待处理事件.TryDequeue(忽略)
        End While
        GC.SuppressFinalize(Me)
    End Sub

    Private Delegate Function 路径调用(原生句柄 As 播放器原生句柄, 路径UTF8 As IntPtr) As 原生播放器结果
    Private Delegate Function 文本调用(原生句柄 As 播放器原生句柄, 输出 As IntPtr, 输出大小 As UInteger, ByRef 所需大小 As UInteger) As 原生播放器结果

    Private Sub 调用路径(调用 As 路径调用, 值 As String)
        ArgumentException.ThrowIfNullOrWhiteSpace(值)
        Dim 指针 = Marshal.StringToCoTaskMemUTF8(值)
        Try
            检查结果(调用(取得句柄(), 指针))
        Finally
            Marshal.FreeCoTaskMem(指针)
        End Try
    End Sub

    Private Function 读取原生文本(调用 As 文本调用) As String
        Dim 所需 As UInteger
        Dim 首次 = 调用(取得句柄(), IntPtr.Zero, 0UI, 所需)
        If 首次 <> 原生播放器结果.缓冲区不足 AndAlso 首次 <> 原生播放器结果.成功 Then 检查结果(首次)
        If 所需 <= 1 Then Return String.Empty
        Dim 缓冲区 = Marshal.AllocHGlobal(CInt(所需))
        Try
            检查结果(调用(取得句柄(), 缓冲区, 所需, 所需))
            Return Marshal.PtrToStringUTF8(缓冲区)
        Finally
            Marshal.FreeHGlobal(缓冲区)
        End Try
    End Function

    Friend Function 取得句柄() As 播放器原生句柄
        ObjectDisposedException.ThrowIf(Volatile.Read(已释放) <> 0, Me)
        Return 句柄
    End Function

    Private Sub 检查结果(结果 As 原生播放器结果)
        If 结果 = 原生播放器结果.成功 Then Return
        Dim 消息 = $"3FP 操作失败（{CInt(结果)}）。"
        Try
            Dim 原生消息 = 读取原生文本(AddressOf 播放器原生接口.FFF3FP_GetLastError)
            If Not String.IsNullOrWhiteSpace(原生消息) Then 消息 = 原生消息
        Catch
        End Try
        Throw New 播放器异常(CInt(结果), 消息)
    End Sub

    Private Shared Sub 接收原生事件(上下文 As IntPtr, 事件类型 As UInteger, 详情UTF8 As IntPtr)
        Try
            Dim 状态 = TryCast(GCHandle.FromIntPtr(上下文).Target, 回调状态)
            Dim 目标 As 播放器会话 = Nothing
            If 状态?.目标 IsNot Nothing AndAlso 状态.目标.TryGetTarget(目标) Then
                目标.投递事件(New 播放器事件参数(CType(事件类型, 播放器事件类型), Marshal.PtrToStringUTF8(详情UTF8)))
            End If
        Catch
        End Try
    End Sub

    Private Sub 投递事件(参数 As 播放器事件参数)
        If Volatile.Read(已释放) <> 0 Then Return
        If 同步上下文 IsNot Nothing Then
            同步上下文.Post(Sub(state)
                                  If Volatile.Read(已释放) = 0 Then 引发事件(参数)
                              End Sub, Nothing)
            Return
        End If
        待处理事件.Enqueue(参数)
        If Interlocked.CompareExchange(事件排程中, 1, 0) = 0 Then
            ThreadPool.QueueUserWorkItem(Sub(state) 排空事件())
        End If
    End Sub

    Private Sub 排空事件()
        Do
            Dim 参数 As 播放器事件参数 = Nothing
            While Volatile.Read(已释放) = 0 AndAlso 待处理事件.TryDequeue(参数)
                Try
                    引发事件(参数)
                Catch
                End Try
            End While
            Interlocked.Exchange(事件排程中, 0)
            If 待处理事件.IsEmpty OrElse Interlocked.CompareExchange(事件排程中, 1, 0) <> 0 Then Exit Do
        Loop
    End Sub

    Private Sub 引发事件(参数 As 播放器事件参数)
        RaiseEvent 播放器事件(Me, 参数)
        Select Case 参数.类型
            Case 播放器事件类型.状态变化 : RaiseEvent 状态变化(Me, 参数)
            Case 播放器事件类型.打开完成 : RaiseEvent 打开完成(Me, 参数)
            Case 播放器事件类型.操作完成 : RaiseEvent 操作完成(Me, 参数)
            Case 播放器事件类型.播放结束 : RaiseEvent 播放结束(Me, 参数)
            Case 播放器事件类型.错误 : RaiseEvent 错误(Me, 参数)
            Case 播放器事件类型.色彩模式变化 : RaiseEvent 色彩模式变化(Me, 参数)
            Case 播放器事件类型.设备变化 : RaiseEvent 设备变化(Me, 参数)
        End Select
    End Sub

    Private Shared Function 读取事件消息(JSON As String) As String
        Try
            Using 文档 = JsonDocument.Parse(JSON)
                Dim 值 As JsonElement
                If 文档.RootElement.TryGetProperty("message", 值) Then Return 值.GetString()
            End Using
        Catch
        End Try
        Return "打开媒体失败。"
    End Function
End Class
