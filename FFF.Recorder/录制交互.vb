Imports System.Diagnostics
Imports System.IO
Imports System.Threading.Tasks

Public NotInheritable Class 视频源条目
    Public Property 类型 As Integer
    Public Property 捕获模式 As Integer
    Public Property 键 As String = String.Empty
    Public Property 显示文本 As String = String.Empty
    Public Property 显示器 As 显示器信息
    Public Property 窗口 As 窗口信息
    Public Overrides Function ToString() As String
        Return 显示文本
    End Function
End Class

Public NotInheritable Class 音频源条目
    Public Property 默认设备 As Boolean
    Public Property 标识 As String = String.Empty
    Public Property 名称 As String = String.Empty
    Public Overrides Function ToString() As String
        Return 名称
    End Function
End Class

Public Module 录制交互
    Private 当前控制器 As 录制控制器
    Private 当前主窗体 As Form1
    Private 当前录制音频端点 As String = String.Empty
    Private 录制开始计数 As Long
    Private 暂停开始计数 As Long
    Private 累计暂停计数 As Long
    Private 正在停止任务 As Task

    Public ReadOnly Property 是否录制中 As Boolean
        Get
            Return 当前控制器 IsNot Nothing AndAlso 当前控制器.已开始
        End Get
    End Property

    Public ReadOnly Property 是否已暂停 As Boolean
        Get
            Return 当前控制器 IsNot Nothing AndAlso 当前控制器.已暂停
        End Get
    End Property

    Public ReadOnly Property 是否正在停止 As Boolean
        Get
            Return 正在停止任务 IsNot Nothing
        End Get
    End Property

    Public ReadOnly Property 已录制时长 As TimeSpan
        Get
            If 录制开始计数 = 0 Then Return TimeSpan.Zero
            Dim 结束计数 = If(暂停开始计数 > 0, 暂停开始计数, Stopwatch.GetTimestamp())
            Dim 有效计数 = Math.Max(0L, 结束计数 - 录制开始计数 - 累计暂停计数)
            Return TimeSpan.FromSeconds(CDbl(有效计数) / Stopwatch.Frequency)
        End Get
    End Property

    Public ReadOnly Property 当前视频源 As 视频源条目
        Get
            Return Form总控台.当前视频源条目
        End Get
    End Property

    Public Sub 初始化(主窗体 As Form1)
        当前主窗体 = 主窗体
        设置.实例对象.规范化()
        Form总控台.初始化页面()
        Form输出设置.初始化页面()
        Form视频参数.初始化页面()
        Form音频参数.初始化页面()
        Form设置.初始化页面()
    End Sub

    Public Sub 切换录制状态()
        If 当前控制器 Is Nothing Then
            开始录制()
        ElseIf 当前控制器.已暂停 Then
            继续录制()
        Else
            暂停()
        End If
    End Sub

    Public Sub 开始录制()
        If 当前控制器 IsNot Nothing OrElse 是否正在停止 Then Return
        Form总控台.MTB_执行日志.Text = String.Empty
        Form总控台.重置录制统计()
        Try
            Form总控台.预览?.开始录制预览()
            Dim 源 = Form总控台.当前视频源条目
            If 源 Is Nothing Then Throw New InvalidOperationException("请选择视频源。")
            Dim 音频 = Form总控台.当前音频源条目
            Dim 输出目录 = If(String.IsNullOrWhiteSpace(设置.实例对象.输出目录), Application.StartupPath, 设置.实例对象.输出目录)
            Directory.CreateDirectory(输出目录)
            Dim 输出文件 = 生成输出文件(输出目录)

            Dim 音频名称 As String = "aac", 音频模式 As UInteger = 0
            Select Case 设置.实例对象.音频编码器索引
                Case 1 : 音频模式 = 1
                Case 2 : 音频名称 = "libfdk_aac" : 音频模式 = 2
                Case 3 : 音频名称 = "pcm_s24le" : 音频模式 = 3
                Case 4 : 音频名称 = "pcm_s32le" : 音频模式 = 4
                Case 5 : 音频名称 = "flac" : 音频模式 = 5
            End Select
            Dim 声道数 = CUInt(Math.Clamp(设置.实例对象.音频声道 + 1, 1, 8))
            Dim 音频码率 = If(音频模式 <= 1, CLng(设置.实例对象.音频每声道码率) * 1000L * 声道数, 0L)
            Dim 编码器名称 = Form视频参数.取得编码器名称()
            Dim H264不支持十位 = 编码器名称.Contains("264", StringComparison.OrdinalIgnoreCase)
            Dim 音频端点 = If(音频 Is Nothing OrElse 音频.默认设备, 获取默认音频端点(), 音频.标识)
            Dim 录制帧率 = Form视频参数.取得录制帧率()
            Dim 质量控制模式 = CUInt(Math.Clamp(设置.实例对象.质量控制模式, 0, 4))
            Dim 使用十位色 = 设置.实例对象.灰阶位深 > 0 AndAlso Not H264不支持十位
            Dim 视频采样 = If(设置.实例对象.像素采样 = 1, 视频采样格式.YUV四二二,
                If(设置.实例对象.像素采样 = 2, 视频采样格式.YUV四四四, 视频采样格式.YUV四二零))
            Dim 配置 As New 录制配置 With {
                .输出文件 = 输出文件, .编码器名称 = 编码器名称,
                .帧率分子 = 录制帧率.分子, .帧率分母 = 录制帧率.分母,
                .可变帧率 = 设置.实例对象.帧率模式 = 1,
                .速率控制 = 取得速率控制(质量控制模式),
                .质量控制模式 = 质量控制模式,
                .质量值 = 设置.实例对象.质量值,
                .自定义视频参数 = 设置.实例对象.自定义视频参数,
                .使用十位色 = 使用十位色,
                .使用HDR10 = 设置.实例对象.色彩模式 = 1,
                .视频采样 = 视频采样,
                .色彩范围 = 视频色彩范围.完整,
                .编码预设 = Form视频参数.取得编码预设(),
                .编码配置档 = If(Form视频参数.MCB_配置文件.SelectedIndex >= 0, Form视频参数.MCB_配置文件.Text, String.Empty),
                .场景优化 = Form视频参数.取得场景优化值(),
                .系统音频端点标识 = 音频端点,
                .跟随默认系统音频设备 = 音频 Is Nothing OrElse 音频.默认设备,
                .音频编码器名称 = 音频名称, .音频采样率 = CUInt(设置.实例对象.音频采样率),
                .音频声道数 = 声道数, .音频码率 = 音频码率, .音频模式 = 音频模式,
                .保留独立音轨 = True}

            Dim 宽度 As UInteger = 1920, 高度 As UInteger = 1080
            Dim 客户区裁剪 As 窗口裁剪信息 = Nothing
            If 源.显示器 IsNot Nothing Then 宽度 = 源.显示器.宽度 : 高度 = 源.显示器.高度
            If 源.窗口 IsNot Nothing Then
                If 源.捕获模式 = 2 Then
                    客户区裁剪 = 窗口发现.获取客户区裁剪(源.窗口.窗口句柄)
                    宽度 = 客户区裁剪.宽度
                    高度 = 客户区裁剪.高度
                Else
                    宽度 = CUInt(Math.Max(1, 源.窗口.右边 - 源.窗口.左边))
                    高度 = CUInt(Math.Max(1, 源.窗口.底边 - 源.窗口.顶边))
                End If
            End If
            Dim 输出尺寸 = Form视频参数.取得输出尺寸(宽度, 高度)
            Dim 视频配置 As New 视频处理配置 With {
                .输出宽度 = 输出尺寸.宽度, .输出高度 = 输出尺寸.高度,
                .输出HDR10 = 设置.实例对象.色彩模式 = 1,
                .输出十位SDR = 设置.实例对象.色彩模式 = 0 AndAlso 设置.实例对象.灰阶位深 > 0 AndAlso Not H264不支持十位,
                .允许HDR转SDR = 设置.实例对象.色彩模式 = 0,
                .裁剪左边 = If(客户区裁剪 Is Nothing, 0UI, 客户区裁剪.左边),
                .裁剪顶边 = If(客户区裁剪 Is Nothing, 0UI, 客户区裁剪.顶边),
                .裁剪右边 = If(客户区裁剪 Is Nothing, 0UI, 客户区裁剪.右边),
                .裁剪底边 = If(客户区裁剪 Is Nothing, 0UI, 客户区裁剪.底边),
                .目标峰值尼特 = If(设置.实例对象.色彩模式 = 0, 设置.实例对象.SDR亮度, 设置.实例对象.HDR峰值),
                .参考白尼特 = 80.0F}

            If 源.类型 = 0 Then
                当前控制器 = 录制控制器.创建显示器WGC录制(源.显示器, 配置, 视频配置, 设置.实例对象.捕获鼠标)
            Else
                当前控制器 = 录制控制器.创建窗口录制(源.窗口.窗口句柄, 配置, 视频配置, 设置.实例对象.捕获鼠标)
            End If
            当前录制音频端点 = 音频端点
            AddHandler 当前控制器.录制失败, AddressOf 录制失败
            AddHandler 当前控制器.收到诊断事件, AddressOf 诊断事件
            AddHandler 当前控制器.收到处理后帧, AddressOf 处理录制预览帧
            写入录制配置日志(源, 音频, 配置, 录制帧率, 输出文件)
            当前控制器.开始()
            录制开始计数 = Stopwatch.GetTimestamp()
            暂停开始计数 = 0
            累计暂停计数 = 0
            写日志("录制已开始。")
        Catch ex As Exception
            写日志($"开始录制失败：{ex.Message}")
            清理控制器()
        Finally
            Form总控台.更新录制状态()
        End Try
    End Sub

    Public Sub 暂停()
        If 当前控制器 Is Nothing OrElse Not 当前控制器.已开始 OrElse 当前控制器.已暂停 Then Return
        Try
            当前控制器.暂停()
            暂停开始计数 = Stopwatch.GetTimestamp()
            写日志("录制已暂停。")
        Catch ex As Exception
            写日志($"暂停录制失败：{ex.Message}")
        Finally
            Form总控台.更新录制状态()
        End Try
    End Sub

    Public Sub 继续录制()
        If 当前控制器 Is Nothing OrElse Not 当前控制器.已暂停 Then Return
        Try
            当前控制器.恢复()
            Dim 现在 = Stopwatch.GetTimestamp()
            If 暂停开始计数 > 0 Then 累计暂停计数 += 现在 - 暂停开始计数
            暂停开始计数 = 0
            写日志("录制已继续。")
        Catch ex As Exception
            写日志($"继续录制失败：{ex.Message}")
        Finally
            Form总控台.更新录制状态()
        End Try
    End Sub

    Public Function 停止录制异步(Optional 记录正常停止 As Boolean = True) As Task
        If 正在停止任务 IsNot Nothing Then Return 正在停止任务
        Dim 待停止控制器 = 当前控制器
        If 待停止控制器 Is Nothing Then Return Task.CompletedTask
        Dim 完成信号 As New TaskCompletionSource(Of Boolean)(TaskCreationOptions.RunContinuationsAsynchronously)
        ' 对外任务覆盖原生 drain、统计读取、资源释放和 UI 状态复位，关闭窗口和重复点击等待同一事务。
        正在停止任务 = 完成信号.Task
        Dim 收尾计时 = Stopwatch.StartNew()
        写日志("正在停止录制并写入文件尾……")
        Form总控台.更新录制状态()
        执行停止录制异步(待停止控制器, 记录正常停止, 收尾计时, 完成信号)
        Return 完成信号.Task
    End Function

    Private Async Sub 执行停止录制异步(待停止控制器 As 录制控制器, 记录正常停止 As Boolean,
                              收尾计时 As Stopwatch, 完成信号 As TaskCompletionSource(Of Boolean))
        Try
            ' 捕获线程、编码器 drain 和 MKV trailer 都可能等待 CPU 编码器；不得阻塞 UI 消息循环。
            Await Task.Run(Sub() 待停止控制器.停止())
            If 记录正常停止 Then 写日志($"录制已停止，收尾耗时 {收尾计时.Elapsed.TotalSeconds:F2} 秒。")
        Catch ex As Exception
            写日志($"停止录制失败：{ex.Message}")
        Finally
            Try
                清理控制器(待停止控制器)
            Catch ex As Exception
                写日志($"释放录制资源失败：{ex.Message}")
            Finally
                正在停止任务 = Nothing
                Form总控台.更新录制状态()
                完成信号.TrySetResult(True)
            End Try
        End Try
    End Sub

    Public Sub 切分文件()
        If 当前控制器 Is Nothing OrElse Not 当前控制器.已开始 OrElse 当前控制器.已暂停 Then Return
        Try
            Dim 输出目录 = If(String.IsNullOrWhiteSpace(设置.实例对象.输出目录), Application.StartupPath, 设置.实例对象.输出目录)
            Directory.CreateDirectory(输出目录)
            Dim 新文件 = 生成输出文件(输出目录)
            当前控制器.切分(新文件)
            写日志($"已切分到新文件：{新文件}")
        Catch ex As Exception
            写日志($"切分文件失败：{ex.Message}")
        End Try
    End Sub

    Friend Function 尝试读取统计(ByRef 统计 As 录制统计) As Boolean
        If 当前控制器 Is Nothing OrElse Not 当前控制器.已开始 Then Return False
        Try
            统计 = 当前控制器.读取统计()
            Return True
        Catch
            Return False
        End Try
    End Function

    Friend Sub 同步录制音频端点(端点标识 As String)
        If 当前控制器 Is Nothing OrElse Not 当前控制器.已开始 OrElse String.IsNullOrWhiteSpace(端点标识) Then Return
        If String.Equals(当前录制音频端点, 端点标识, StringComparison.Ordinal) Then Return
        Try
            当前控制器.切换系统音频端点(端点标识)
            当前录制音频端点 = 端点标识
            写日志("播放设备已切换，录制时间线保持连续。")
        Catch ex As Exception
            写日志($"切换播放设备失败：{ex.Message}")
        End Try
    End Sub

    Friend Async Function 关闭异步() As Task
        If 当前控制器 IsNot Nothing Then Await 停止录制异步()
        Form总控台.释放页面资源()
    End Function

    Private Sub 清理控制器(Optional 待清理控制器 As 录制控制器 = Nothing)
        If 待清理控制器 IsNot Nothing AndAlso Not ReferenceEquals(当前控制器, 待清理控制器) Then Return
        If 当前控制器 IsNot Nothing Then
            Try
                Form总控台.显示录制统计(当前控制器.读取统计())
            Catch
            End Try
            RemoveHandler 当前控制器.录制失败, AddressOf 录制失败
            RemoveHandler 当前控制器.收到诊断事件, AddressOf 诊断事件
            RemoveHandler 当前控制器.收到处理后帧, AddressOf 处理录制预览帧
            当前控制器.释放()
            当前控制器 = Nothing
        End If
        Form总控台.预览?.结束录制预览()
        当前录制音频端点 = String.Empty
        录制开始计数 = 0
        暂停开始计数 = 0
        累计暂停计数 = 0
    End Sub

    Private Function 获取默认音频端点() As String
        Return 音频响度计.获取默认设备标识()
    End Function

    Private Function 生成输出文件(目录 As String) As String
        Dim 基础 = If(设置.实例对象.自动命名方式 = 0, DateTime.Now.ToString("yyyyMMdd-HHmmss"), "录制")
        Dim 候选 = Path.Combine(目录, 基础 & ".mkv")
        Dim 索引 = 1
        While File.Exists(候选)
            候选 = Path.Combine(目录, $"{基础}-{索引}.mkv")
            索引 += 1
        End While
        Return 候选
    End Function

    Private Function 格式化帧率(分子 As UInteger, 分母 As UInteger) As String
        If 分母 = 0 Then Return "未知"
        Dim 帧率 = CDbl(分子) / 分母
        If Math.Abs(帧率 - Math.Round(帧率)) < 0.001 Then Return Math.Round(帧率).ToString("F0")
        Return 帧率.ToString("F2")
    End Function

    Private Function 取得速率控制(质量控制模式 As UInteger) As 编码速率控制
        ' CQ 和自定义参数都需要允许编码器使用可变码率；其余质量模式由编码器的恒定质量选项控制。
        Return If(质量控制模式 = 2UI OrElse 质量控制模式 = 4UI,
            编码速率控制.可变码率, 编码速率控制.恒定质量)
    End Function

    Private Sub 写入录制配置日志(源 As 视频源条目, 音频 As 音频源条目,
                             配置 As 录制配置, 录制帧率 As (分子 As UInteger, 分母 As UInteger),
                             输出文件 As String)
        Dim 捕获模式 = If(源.捕获模式 = 0, "显示器", If(源.捕获模式 = 2, "窗口客户区", "完整窗口"))
        Dim 质量模式名称 = {"通用 QP 恒定量化", "CRF - CPU", "CQ - NVIDIA", "global_quality - Intel", "自定义参数"}
        Dim 质量模式文本 = If(配置.质量控制模式 < CUInt(质量模式名称.Length),
            质量模式名称(CInt(配置.质量控制模式)), "未知")
        写日志($"录制源：{源.显示文本}", False)
        写日志($"捕获选项：模式={捕获模式}，捕获鼠标={If(设置.实例对象.捕获鼠标, "是", "否")}", False)
        写日志($"视频选项：分辨率={If(String.IsNullOrWhiteSpace(Form视频参数.MCB_视频分辨率.Text), "原生分辨率", Form视频参数.MCB_视频分辨率.Text)}（实际 {配置.宽度} × {配置.高度}），帧率={格式化帧率(录制帧率.分子, 录制帧率.分母)} 帧/秒，帧率模式={If(配置.可变帧率, "VFR", "CFR")}", False)
        写日志($"编码选项：编码器={配置.编码器名称}，预设={If(String.IsNullOrWhiteSpace(配置.编码预设), "默认", 配置.编码预设)}，配置档={If(String.IsNullOrWhiteSpace(配置.编码配置档), "默认", 配置.编码配置档)}，场景优化={If(String.IsNullOrWhiteSpace(配置.场景优化), "默认", 配置.场景优化)}", False)
        写日志($"色彩选项：采样={取得采样描述(配置.视频采样)}，位深选项={Form视频参数.MCB_灰阶位深.Text}（实际 {If(配置.使用十位色, "10bit", "8bit")}），色彩模式={Form视频参数.MCB_色彩模式.Text}，HDR={If(配置.使用HDR10, "是", "否")}", False)
        写日志($"色彩处理：SDR亮度={设置.实例对象.SDR亮度} nit，HDR峰值={设置.实例对象.HDR峰值} nit，色彩范围={配置.色彩范围}", False)
        写日志($"质量控制：模式={质量模式文本}（{配置.质量控制模式}），质量值={配置.质量值}，速率控制={配置.速率控制}", False)
        写日志($"自定义视频参数：{If(String.IsNullOrWhiteSpace(配置.自定义视频参数), "无", 配置.自定义视频参数)}", False)
        写日志($"音频选项：来源={If(音频 Is Nothing, "未选择", 音频.名称)}，跟随默认设备={If(配置.跟随默认系统音频设备, "是", "否")}，编码器={取得音频编码器显示名称(设置.实例对象.音频编码器索引)}，采样率={配置.音频采样率} 赫兹，声道={配置.音频声道数}，码率={If(配置.音频码率 > 0, $"{配置.音频码率 \ 1000} kbps", "无损/由编码器决定")}", False)
        写日志($"输出文件：{输出文件}", False)
    End Sub

    Private Function 取得采样描述(采样 As 视频采样格式) As String
        Select Case 采样
            Case 视频采样格式.YUV四二二 : Return "4:2:2"
            Case 视频采样格式.YUV四四四 : Return "4:4:4"
            Case Else : Return "4:2:0"
        End Select
    End Function

    Private Function 取得音频编码器显示名称(索引 As Integer) As String
        Select Case 索引
            Case 1 : Return "NMR AAC"
            Case 2 : Return "FDK AAC"
            Case 3 : Return "无损 WAV 24 位"
            Case 4 : Return "无损 WAV 32 位"
            Case 5 : Return "无损 FLAC"
            Case Else : Return "AAC"
        End Select
    End Function

    Private Sub 写日志(文本 As String, Optional 是否带时间戳 As Boolean = True)
        If 当前主窗体 Is Nothing OrElse 当前主窗体.IsDisposed Then Return
        If 是否带时间戳 Then
            Form总控台.MTB_执行日志.AppendText($"[{DateTime.Now:HH:mm:ss}] {文本}{vbCrLf}")
        Else
            Form总控台.MTB_执行日志.AppendText($"{文本}{vbCrLf}")
        End If
    End Sub

    Private Sub 录制失败(sender As Object, e As 录制失败事件参数)
        If 当前主窗体 Is Nothing OrElse 当前主窗体.IsDisposed Then Return
        当前主窗体.BeginInvoke(Sub() 处理录制失败(e.异常))
    End Sub

    Private Async Sub 处理录制失败(错误 As Exception)
        写日志($"录制失败：{错误.Message}")
        Await 停止录制异步(False)
    End Sub

    Private Sub 处理录制预览帧(sender As Object, e As 处理后视频帧事件参数)
        Form总控台.预览?.提交GPU帧(e.图形设备, e.帧)
    End Sub

    Private Sub 诊断事件(sender As Object, e As 录制诊断事件参数)
        If 当前主窗体 Is Nothing OrElse 当前主窗体.IsDisposed Then Return
        当前主窗体.BeginInvoke(Sub()
                              Dim 消息 = 读取诊断消息(e.详细信息JSON)
                              Select Case e.事件名称.ToLowerInvariant()
                                  Case "start", "pause", "resume", "stop", "split",
                                       "controller_paused", "controller_resumed",
                                       "system_audio_endpoint_switched", "recording_controller_failed"
                                      Return
                                  Case "encoder_initialization_failed"
                                      写日志($"视频编码器初始化失败：{消息}")
                                  Case "video_failed"
                                      写日志($"视频编码失败：{消息}")
                                  Case "audio_failed"
                                      写日志($"音频编码失败：{消息}")
                                  Case "audio_device_failed"
                                      写日志($"音频设备异常：{消息}")
                                  Case "dxgi_access_lost_rebuilt"
                                      写日志($"显示画面捕获中断后已自动恢复。{格式化可选诊断消息(消息)}")
                                  Case "wgc_resize"
                                      写日志($"目标窗口尺寸已变化：{消息}")
                                  Case "wgc_failed"
                                      写日志($"窗口画面捕获失败：{消息}")
                                  Case "wgc_closed"
                                      写日志(If(String.IsNullOrWhiteSpace(消息), "目标窗口已关闭。", 消息))
                                  Case "abort"
                                      写日志("录制已异常中止。")
                                  Case Else
                                      If Not String.IsNullOrWhiteSpace(消息) Then 写日志($"录制组件提示：{消息}")
                              End Select
                          End Sub)
    End Sub

    Private Function 读取诊断消息(详细信息JSON As String) As String
        If String.IsNullOrWhiteSpace(详细信息JSON) Then Return String.Empty
        Try
            Using 文档 = System.Text.Json.JsonDocument.Parse(详细信息JSON)
                Dim 消息元素 As System.Text.Json.JsonElement
                If 文档.RootElement.TryGetProperty("message", 消息元素) Then Return If(消息元素.GetString(), String.Empty)
            End Using
        Catch
        End Try
        Return String.Empty
    End Function

    Private Function 格式化可选诊断消息(消息 As String) As String
        If String.IsNullOrWhiteSpace(消息) Then Return String.Empty
        Return " " & 消息.Replace("重建次数=", "恢复次数：")
    End Function
End Module
