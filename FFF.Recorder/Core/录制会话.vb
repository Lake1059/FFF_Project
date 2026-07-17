Imports System.Diagnostics
Imports System.Runtime.InteropServices

Friend NotInheritable Class 原生配置封送范围
    Implements IDisposable

    Private ReadOnly 路径指针 As IntPtr
    Private ReadOnly 编码器指针 As IntPtr
    Private ReadOnly 系统音频指针 As IntPtr
    Private ReadOnly 麦克风指针 As IntPtr
    Private ReadOnly 预设指针 As IntPtr
    Private ReadOnly 配置档指针 As IntPtr
    Private ReadOnly 诊断日志指针 As IntPtr
    Private ReadOnly 捕获后端指针 As IntPtr
    Private ReadOnly 源说明指针 As IntPtr
    Private ReadOnly 源格式指针 As IntPtr
    Private 已释放 As Boolean

    Friend Sub New(配置 As 录制配置, 设备指针 As IntPtr,
        Optional 诊断回调 As IntPtr = Nothing, Optional 诊断回调上下文 As IntPtr = Nothing)
        路径指针 = Marshal.StringToCoTaskMemUTF8(IO.Path.GetFullPath(配置.输出文件))
        Try
            编码器指针 = Marshal.StringToCoTaskMemUTF8(配置.编码器名称)
            系统音频指针 = If(String.IsNullOrWhiteSpace(配置.系统音频端点标识), IntPtr.Zero,
                Marshal.StringToCoTaskMemUTF8(配置.系统音频端点标识))
            麦克风指针 = If(String.IsNullOrWhiteSpace(配置.麦克风端点标识), IntPtr.Zero,
                Marshal.StringToCoTaskMemUTF8(配置.麦克风端点标识))
            预设指针 = If(String.IsNullOrWhiteSpace(配置.编码预设), IntPtr.Zero,
                Marshal.StringToCoTaskMemUTF8(配置.编码预设))
            配置档指针 = If(String.IsNullOrWhiteSpace(配置.编码配置档), IntPtr.Zero,
                Marshal.StringToCoTaskMemUTF8(配置.编码配置档))
            Dim 诊断路径 = If(String.IsNullOrWhiteSpace(配置.诊断日志文件),
                IO.Path.GetFullPath(配置.输出文件) & ".diagnostic.jsonl", IO.Path.GetFullPath(配置.诊断日志文件))
            诊断日志指针 = Marshal.StringToCoTaskMemUTF8(诊断路径)
            捕获后端指针 = If(String.IsNullOrWhiteSpace(配置.捕获后端), IntPtr.Zero,
                Marshal.StringToCoTaskMemUTF8(配置.捕获后端))
            源说明指针 = If(String.IsNullOrWhiteSpace(配置.捕获源说明), IntPtr.Zero,
                Marshal.StringToCoTaskMemUTF8(配置.捕获源说明))
            源格式指针 = If(String.IsNullOrWhiteSpace(配置.捕获源格式), IntPtr.Zero,
                Marshal.StringToCoTaskMemUTF8(配置.捕获源格式))
        Catch
            释放()
            Throw
        End Try

        值 = New 原生会话配置 With {
            .大小 = CUInt(Marshal.SizeOf(Of 原生会话配置)()), .版本 = 1,
            .D3D11设备 = 设备指针, .输出路径UTF8 = 路径指针, .编码器名称UTF8 = 编码器指针,
            .宽度 = 配置.宽度, .高度 = 配置.高度, .帧率分子 = 配置.帧率分子,
            .帧率分母 = 配置.帧率分母, .视频码率 = 配置.视频码率,
            .关键帧间隔 = 配置.关键帧间隔, .B帧数量 = 配置.B帧数量,
            .十位色 = If(配置.使用十位色, 1UI, 0UI), .HDR10 = If(配置.使用HDR10, 1UI, 0UI),
            .系统音频端点标识UTF8 = 系统音频指针, .麦克风端点标识UTF8 = 麦克风指针,
            .保留独立音轨 = If(配置.保留独立音轨, 1UI, 0UI),
            .输入纹理格式 = CUInt(配置.输入纹理格式), .视频采样 = CUInt(配置.视频采样),
            .速率控制 = CUInt(配置.速率控制), .质量值 = 配置.质量值, .最大码率 = 配置.最大码率,
            .前瞻帧数 = 配置.前瞻帧数, .预设UTF8 = 预设指针, .配置档UTF8 = 配置档指针,
            .多遍模式 = CUInt(配置.多遍模式), .色彩范围 = CUInt(配置.色彩范围),
            .系统音频增益 = 配置.系统音频增益, .麦克风增益 = 配置.麦克风增益,
            .静音系统音频 = If(配置.静音系统音频, 1UI, 0UI),
            .静音麦克风 = If(配置.静音麦克风, 1UI, 0UI), .诊断日志路径UTF8 = 诊断日志指针,
            .捕获后端UTF8 = 捕获后端指针, .源说明UTF8 = 源说明指针, .源格式UTF8 = 源格式指针,
            .诊断回调 = 诊断回调, .诊断回调上下文 = 诊断回调上下文
        }
    End Sub

    Friend 值 As 原生会话配置

    Public Sub 释放() Implements IDisposable.Dispose
        If 已释放 Then Return
        If 路径指针 <> IntPtr.Zero Then Marshal.FreeCoTaskMem(路径指针)
        If 编码器指针 <> IntPtr.Zero Then Marshal.FreeCoTaskMem(编码器指针)
        If 系统音频指针 <> IntPtr.Zero Then Marshal.FreeCoTaskMem(系统音频指针)
        If 麦克风指针 <> IntPtr.Zero Then Marshal.FreeCoTaskMem(麦克风指针)
        If 预设指针 <> IntPtr.Zero Then Marshal.FreeCoTaskMem(预设指针)
        If 配置档指针 <> IntPtr.Zero Then Marshal.FreeCoTaskMem(配置档指针)
        If 诊断日志指针 <> IntPtr.Zero Then Marshal.FreeCoTaskMem(诊断日志指针)
        If 捕获后端指针 <> IntPtr.Zero Then Marshal.FreeCoTaskMem(捕获后端指针)
        If 源说明指针 <> IntPtr.Zero Then Marshal.FreeCoTaskMem(源说明指针)
        If 源格式指针 <> IntPtr.Zero Then Marshal.FreeCoTaskMem(源格式指针)
        已释放 = True
    End Sub
End Class

Friend NotInheritable Class 原生诊断回调桥
    Friend Property 会话 As 录制会话

    Friend Sub 处理(事件名称 As String, 详细信息JSON As String)
        会话?.接收诊断事件(事件名称, 详细信息JSON)
    End Sub
End Class

Public NotInheritable Class 录制会话
    Implements IDisposable

    Private ReadOnly 句柄 As 原生会话句柄
    Private ReadOnly 回调委托 As 原生诊断回调
    Private 回调GC句柄 As GCHandle
    Private 已分配回调句柄 As Boolean
    Private 已释放 As Boolean

    Private Sub New(原生句柄 As 原生会话句柄, 诊断回调 As 原生诊断回调,
        诊断回调句柄 As GCHandle)
        句柄 = 原生句柄
        回调委托 = 诊断回调
        回调GC句柄 = 诊断回调句柄
        已分配回调句柄 = True
    End Sub

    Public Event 收到诊断事件 As EventHandler(Of 录制诊断事件参数)

    Friend Shared Function 创建(配置 As 录制配置, 设备指针 As IntPtr) As 录制会话
        Dim 回调桥 As New 原生诊断回调桥()
        Dim 回调 As New 原生诊断回调(AddressOf 处理原生诊断回调)
        Dim 回调句柄 = GCHandle.Alloc(回调桥)
        Try
            Using 封送范围 As New 原生配置封送范围(配置, 设备指针,
                Marshal.GetFunctionPointerForDelegate(回调), GCHandle.ToIntPtr(回调句柄))
            Dim 原生指针 As IntPtr
            录制引擎.检查结果(原生接口.FFF_CreateSession(封送范围.值, 原生指针), "创建录制会话失败。")
                Dim 结果 = New 录制会话(New 原生会话句柄(原生指针), 回调, 回调句柄)
                回调桥.会话 = 结果
                Return 结果
            End Using
        Catch
            回调句柄.Free()
            Throw
        End Try
    End Function

    Public Sub 开始()
        确保未释放()
        录制引擎.检查结果(原生接口.FFF_StartSession(句柄), 读取最后错误("开始录制失败。"))
    End Sub

    Public Sub 提交视频纹理(D3D11纹理指针 As IntPtr, QPC时间戳 As Long,
        Optional 纹理数组索引 As UInteger = 0, Optional 是重复帧 As Boolean = False)
        确保未释放()
        If D3D11纹理指针 = IntPtr.Zero Then Throw New ArgumentException("纹理指针不能为空。", NameOf(D3D11纹理指针))
        Dim 提交标志 = If(是重复帧, 1UI, 0UI)
        录制引擎.检查结果(原生接口.FFF_SubmitVideoTexture(句柄, D3D11纹理指针, 纹理数组索引,
            QPC时间戳, 提交标志),
            读取最后错误("提交视频纹理失败。"))
    End Sub

    Public Sub 报告丢弃视频帧(帧数 As UInteger)
        确保未释放()
        If 帧数 = 0 Then Return
        录制引擎.检查结果(原生接口.FFF_ReportDroppedVideoFrames(句柄, 帧数), "报告丢弃视频帧失败。")
    End Sub

    Public Sub 记录诊断事件(事件名称 As String, Optional 消息 As String = "")
        确保未释放()
        If String.IsNullOrWhiteSpace(事件名称) Then Throw New ArgumentException("事件名称不能为空。", NameOf(事件名称))
        Dim 名称指针 = Marshal.StringToCoTaskMemUTF8(事件名称)
        Dim 消息指针 = Marshal.StringToCoTaskMemUTF8(If(消息, String.Empty))
        Try
            录制引擎.检查结果(原生接口.FFF_ReportDiagnosticEvent(句柄, 名称指针, 消息指针),
                "记录诊断事件失败。")
        Finally
            Marshal.FreeCoTaskMem(名称指针)
            Marshal.FreeCoTaskMem(消息指针)
        End Try
    End Sub

    Public Sub 暂停(Optional QPC时间戳 As Long = 0)
        确保未释放()
        If QPC时间戳 = 0 Then QPC时间戳 = Stopwatch.GetTimestamp()
        录制引擎.检查结果(原生接口.FFF_PauseSession(句柄, QPC时间戳), "暂停录制失败。")
    End Sub

    Public Sub 恢复(Optional QPC时间戳 As Long = 0)
        确保未释放()
        If QPC时间戳 = 0 Then QPC时间戳 = Stopwatch.GetTimestamp()
        录制引擎.检查结果(原生接口.FFF_ResumeSession(句柄, QPC时间戳), "恢复录制失败。")
    End Sub

    Public Sub 停止()
        确保未释放()
        录制引擎.检查结果(原生接口.FFF_StopSession(句柄), 读取最后错误("停止录制失败。"))
    End Sub

    Public Sub 强制中止()
        If 已释放 Then Return
        录制引擎.检查结果(原生接口.FFF_AbortSession(句柄), "强制中止录制失败。")
    End Sub

    Public Function 读取统计() As 录制统计
        确保未释放()
        Dim 原生统计 As New 原生会话统计 With {.大小 = CUInt(Marshal.SizeOf(Of 原生会话统计)()), .版本 = 1}
        录制引擎.检查结果(原生接口.FFF_GetSessionStatistics(句柄, 原生统计), "读取录制统计失败。")
        Return New 录制统计 With {
            .状态 = CType(原生统计.状态, 录制会话状态), .已提交帧数 = 原生统计.已提交帧数,
            .已丢弃帧数 = 原生统计.已丢弃帧数, .已重复帧数 = 原生统计.已重复帧数,
            .最后视频时间戳 = 原生统计.最后视频QPC, .累计暂停计数 = 原生统计.暂停QPC,
            .最后错误码 = 原生统计.最后错误码, .队列深度 = 原生统计.队列深度,
            .最后编码微秒 = 原生统计.最后编码微秒, .峰值编码微秒 = 原生统计.峰值编码微秒,
            .系统音频不连续次数 = 原生统计.系统音频不连续次数,
            .麦克风不连续次数 = 原生统计.麦克风不连续次数,
            .系统音频时间戳错误次数 = 原生统计.系统音频时间戳错误次数,
            .麦克风时间戳错误次数 = 原生统计.麦克风时间戳错误次数,
            .系统音频漂移微秒 = 原生统计.系统音频漂移微秒,
            .麦克风漂移微秒 = 原生统计.麦克风漂移微秒,
            .已写正常文件尾 = 原生统计.已写正常文件尾 <> 0,
            .峰值队列深度 = 原生统计.峰值队列深度,
            .最后写入微秒 = 原生统计.最后写入微秒, .峰值写入微秒 = 原生统计.峰值写入微秒,
            .系统音频时间线误差微秒 = 原生统计.系统音频时间线误差微秒,
            .麦克风时间线误差微秒 = 原生统计.麦克风时间线误差微秒,
            .系统音频补偿PPM = 原生统计.系统音频补偿PPM,
            .麦克风补偿PPM = 原生统计.麦克风补偿PPM
        }
    End Function

    Public Sub 释放() Implements IDisposable.Dispose
        If 已释放 Then Return
        句柄.Dispose()
        If 已分配回调句柄 Then
            回调GC句柄.Free()
            已分配回调句柄 = False
        End If
        已释放 = True
        GC.SuppressFinalize(Me)
    End Sub

    Private Function 读取最后错误(后备消息 As String) As String
        Dim 所需大小 As UInteger
        Dim 结果码 = 原生接口.FFF_GetLastError(句柄, IntPtr.Zero, 0, 所需大小)
        If 结果码 <> 原生结果.缓冲区不足 OrElse 所需大小 <= 1 Then Return 后备消息
        Dim 缓冲区 = Marshal.AllocHGlobal(CInt(所需大小))
        Try
            If 原生接口.FFF_GetLastError(句柄, 缓冲区, 所需大小, 所需大小) <> 原生结果.成功 Then Return 后备消息
            Dim 消息 = Marshal.PtrToStringUTF8(缓冲区)
            Return If(String.IsNullOrWhiteSpace(消息), 后备消息, 消息)
        Finally
            Marshal.FreeHGlobal(缓冲区)
        End Try
    End Function

    Private Sub 确保未释放()
        ObjectDisposedException.ThrowIf(已释放, Me)
    End Sub

    Friend Sub 接收诊断事件(事件名称 As String, 详细信息JSON As String)
        RaiseEvent 收到诊断事件(Me, New 录制诊断事件参数(事件名称, 详细信息JSON))
    End Sub

    Private Shared Sub 处理原生诊断回调(上下文 As IntPtr, 事件名称UTF8 As IntPtr,
        详细信息UTF8 As IntPtr)
        Try
            Dim 回调桥 = TryCast(GCHandle.FromIntPtr(上下文).Target, 原生诊断回调桥)
            回调桥?.处理(If(事件名称UTF8 = IntPtr.Zero, String.Empty, Marshal.PtrToStringUTF8(事件名称UTF8)),
                If(详细信息UTF8 = IntPtr.Zero, String.Empty, Marshal.PtrToStringUTF8(详细信息UTF8)))
        Catch
        End Try
    End Sub
End Class
