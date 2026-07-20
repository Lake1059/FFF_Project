Imports System.Diagnostics
Imports System.IO

Public NotInheritable Class 视频源条目
    Public Property 类型 As Integer
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
        If 是否录制中 Then Return
        Form总控台.MTB_执行日志.Text = String.Empty
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
            Dim 配置 As New 录制配置 With {
                .输出文件 = 输出文件, .编码器名称 = 编码器名称,
                .帧率分子 = 录制帧率.分子, .帧率分母 = 录制帧率.分母,
                .可变帧率 = 设置.实例对象.帧率模式 = 1,
                .速率控制 = If(设置.实例对象.质量控制模式 = 4, 编码速率控制.可变码率, 编码速率控制.恒定质量),
                .质量控制模式 = CUInt(设置.实例对象.质量控制模式),
                .质量值 = 设置.实例对象.质量值,
                .自定义视频参数 = 设置.实例对象.自定义视频参数,
                .使用十位色 = 设置.实例对象.灰阶位深 > 0 AndAlso Not H264不支持十位,
                .使用HDR10 = 设置.实例对象.色彩模式 = 1,
                .视频采样 = If(设置.实例对象.像素采样 = 2, 视频采样格式.YUV四四四, 视频采样格式.YUV四二零),
                .编码预设 = Form视频参数.取得编码预设(),
                .编码配置档 = If(Form视频参数.MCB_配置文件.SelectedIndex >= 0, Form视频参数.MCB_配置文件.Text, String.Empty),
                .场景优化 = If(Form视频参数.MCB_场景优化.SelectedIndex > 0, Form视频参数.MCB_场景优化.Text, String.Empty),
                .系统音频端点标识 = 音频端点,
                .跟随默认系统音频设备 = 音频 Is Nothing OrElse 音频.默认设备,
                .音频编码器名称 = 音频名称, .音频采样率 = CUInt(设置.实例对象.音频采样率),
                .音频声道数 = 声道数, .音频码率 = 音频码率, .音频模式 = 音频模式,
                .保留独立音轨 = True}

            Dim 宽度 As UInteger = 1920, 高度 As UInteger = 1080
            If 源.显示器 IsNot Nothing Then 宽度 = 源.显示器.宽度 : 高度 = 源.显示器.高度
            If 源.窗口 IsNot Nothing Then
                宽度 = CUInt(Math.Max(1, 源.窗口.右边 - 源.窗口.左边))
                高度 = CUInt(Math.Max(1, 源.窗口.底边 - 源.窗口.顶边))
            End If
            Dim 输出尺寸 = Form视频参数.取得输出尺寸(宽度, 高度)
            Dim 视频配置 As New 视频处理配置 With {
                .输出宽度 = 输出尺寸.宽度, .输出高度 = 输出尺寸.高度,
                .输出HDR10 = 设置.实例对象.色彩模式 = 1,
                .输出十位SDR = 设置.实例对象.色彩模式 = 0 AndAlso 设置.实例对象.灰阶位深 > 0 AndAlso Not H264不支持十位,
                .允许HDR转SDR = 设置.实例对象.色彩模式 = 0,
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
            当前控制器.开始()
            录制开始计数 = Stopwatch.GetTimestamp()
            暂停开始计数 = 0
            累计暂停计数 = 0
            写日志($"已开始录制：{输出文件}")
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

    Public Sub 停止录制()
        If 当前控制器 Is Nothing Then Return
        Try
            当前控制器.停止()
            写日志("录制已停止。")
        Catch ex As Exception
            写日志($"停止录制失败：{ex.Message}")
        Finally
            清理控制器()
            Form总控台.更新录制状态()
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

    Friend Sub 关闭()
        If 当前控制器 IsNot Nothing Then 停止录制()
        Form总控台.释放页面资源()
    End Sub

    Private Sub 清理控制器()
        If 当前控制器 IsNot Nothing Then
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

    Private Sub 写日志(文本 As String)
        If 当前主窗体 Is Nothing OrElse 当前主窗体.IsDisposed Then Return
        Form总控台.MTB_执行日志.AppendText($"[{DateTime.Now:HH:mm:ss}] {文本}{vbCrLf}")
    End Sub

    Private Sub 录制失败(sender As Object, e As 录制失败事件参数)
        If 当前主窗体 Is Nothing OrElse 当前主窗体.IsDisposed Then Return
        当前主窗体.BeginInvoke(Sub()
                              写日志($"录制失败：{e.异常.Message}")
                              清理控制器()
                              Form总控台.更新录制状态()
                          End Sub)
    End Sub

    Private Sub 处理录制预览帧(sender As Object, e As 处理后视频帧事件参数)
        Form总控台.预览?.提交GPU帧(e.图形设备, e.帧)
    End Sub

    Private Sub 诊断事件(sender As Object, e As 录制诊断事件参数)
        If 当前主窗体 Is Nothing OrElse 当前主窗体.IsDisposed Then Return
        当前主窗体.BeginInvoke(Sub()
                              If String.Equals(e.事件名称, "stop", StringComparison.OrdinalIgnoreCase) Then
                                  写日志("stop")
                              Else
                                  写日志($"{e.事件名称} {e.详细信息JSON}")
                              End If
                          End Sub)
    End Sub
End Module
