Imports System.Runtime.InteropServices
Imports System.Text.Json
Imports System.Collections.Concurrent

Public NotInheritable Class 录制引擎
    Private Const 期望接口版本 As UInteger = 3
    Private Shared ReadOnly 编码器能力缓存 As New ConcurrentDictionary(Of String, 编码器探测结果)()
    Private Shared ReadOnly 接口兼容 As New Lazy(Of Boolean)(AddressOf 检查接口兼容)

    Private Sub New()
    End Sub

    Public Shared Function 获取接口版本() As UInteger
        Try
            Return 原生接口.FFF_GetApiVersion()
        Catch 错误 As DllNotFoundException
            Throw New InvalidOperationException("无法加载 FFF.Native.dll 或它依赖的 FFmpeg shared DLL。", 错误)
        Catch 错误 As BadImageFormatException
            Throw New InvalidOperationException("FFF.Native.dll 或其依赖项不是兼容的 Windows x64 二进制文件。", 错误)
        Catch 错误 As EntryPointNotFoundException
            Throw New InvalidOperationException("FFF.Native.dll 缺少版本查询入口，文件版本不兼容。", 错误)
        End Try
    End Function

    Public Shared Sub 验证原生接口()
        Dim 忽略 = 接口兼容.Value
    End Sub

    Public Shared Function 运行基础自检() As String
        Return 读取原生文本(AddressOf 原生接口.FFF_RunSelfTest)
    End Function

    Public Shared Function 获取运行时信息() As String
        Return 读取原生文本(AddressOf 原生接口.FFF_GetRuntimeInfo)
    End Function

    Public Shared Function 枚举音频端点() As IReadOnlyList(Of 音频端点信息)
        Dim 文档文本 = 读取原生文本(AddressOf 原生接口.FFF_EnumerateAudioEndpoints)
        Dim 结果 As New List(Of 音频端点信息)
        Using 文档 = JsonDocument.Parse(文档文本)
            For Each 项目 In 文档.RootElement.EnumerateArray()
                结果.Add(New 音频端点信息 With {
                    .类型 = 项目.GetProperty("type").GetString(),
                    .标识 = 项目.GetProperty("id").GetString(),
                    .名称 = 项目.GetProperty("name").GetString()
                })
            Next
        End Using
        Return 结果
    End Function

    Public Shared Function 探测编码器(编码器名称 As String, 宽度 As UInteger, 高度 As UInteger,
        帧率分子 As UInteger, 帧率分母 As UInteger) As 编码器探测结果
        验证原生接口()
        If String.IsNullOrWhiteSpace(编码器名称) Then Throw New ArgumentException("编码器名称不能为空。", NameOf(编码器名称))
        Dim 名称指针 = Marshal.StringToCoTaskMemUTF8(编码器名称)
        Try
            Dim 所需大小 As UInteger
            Dim 首次结果 = 原生接口.FFF_ProbeEncoder(名称指针, 宽度, 高度, 帧率分子, 帧率分母,
                IntPtr.Zero, 0, 所需大小)
            If 首次结果 <> 原生结果.缓冲区不足 Then 检查结果(首次结果, "编码器探测容量查询失败。")
            Dim 缓冲区 = Marshal.AllocHGlobal(CInt(所需大小))
            Try
                Dim 结果码 = 原生接口.FFF_ProbeEncoder(名称指针, 宽度, 高度, 帧率分子, 帧率分母,
                    缓冲区, 所需大小, 所需大小)
                Dim 文本 = Marshal.PtrToStringUTF8(缓冲区)
                Using 文档 = JsonDocument.Parse(文本)
                    Dim 根 = 文档.RootElement
                    Dim 结果 As New 编码器探测结果 With {.编码器名称 = 编码器名称}
                    结果.支持 = 根.GetProperty("supported").GetBoolean()
                    Dim 值 As JsonElement
                    If 根.TryGetProperty("reason", 值) Then 结果.原因 = 值.GetString()
                    If 结果码 <> 原生结果.成功 AndAlso 结果码 <> 原生结果.不支持 Then 检查结果(结果码, 结果.原因)
                    Return 结果
                End Using
            Finally
                Marshal.FreeHGlobal(缓冲区)
            End Try
        Finally
            Marshal.FreeCoTaskMem(名称指针)
        End Try
    End Function

    Public Shared Function 探测D3D11编码器(图形设备 As 图形设备, 配置 As 录制配置,
        Optional 使用缓存 As Boolean = True) As 编码器探测结果
        验证原生接口()
        ArgumentNullException.ThrowIfNull(图形设备)
        ArgumentNullException.ThrowIfNull(配置)
        配置.验证()

        Dim 缓存键 = String.Join("|", 图形设备.适配器标识, 配置.编码器名称, 配置.宽度, 配置.高度,
            配置.帧率分子, 配置.帧率分母, 配置.视频码率, 配置.最大码率, 配置.关键帧间隔,
            配置.B帧数量, 配置.使用十位色, 配置.使用HDR10, CUInt(配置.输入纹理格式),
            CUInt(配置.视频采样), CUInt(配置.速率控制), 配置.质量值, 配置.前瞻帧数,
            配置.编码预设, 配置.编码配置档, CUInt(配置.多遍模式), CUInt(配置.色彩范围))
        If 使用缓存 AndAlso 编码器能力缓存.TryGetValue(缓存键, Nothing) Then
            Return 复制探测结果(编码器能力缓存(缓存键))
        End If

        Dim 结果 = 执行D3D11编码器探测(图形设备, 配置)
        If 使用缓存 Then 编码器能力缓存(缓存键) = 复制探测结果(结果)
        Return 结果
    End Function

    Public Shared Function 探测D3D11编码器组合(图形设备 As 图形设备,
        配置集合 As IEnumerable(Of 录制配置), Optional 使用缓存 As Boolean = True) As IReadOnlyList(Of 编码器探测结果)
        ArgumentNullException.ThrowIfNull(图形设备)
        ArgumentNullException.ThrowIfNull(配置集合)
        Dim 结果 As New List(Of 编码器探测结果)
        For Each 配置 In 配置集合
            ArgumentNullException.ThrowIfNull(配置)
            结果.Add(探测D3D11编码器(图形设备, 配置, 使用缓存))
        Next
        Return 结果
    End Function

    Public Shared Sub 清空编码器能力缓存()
        编码器能力缓存.Clear()
    End Sub

    Private Shared Function 执行D3D11编码器探测(图形设备 As 图形设备,
        配置 As 录制配置) As 编码器探测结果

        Using 封送范围 As New 原生配置封送范围(配置, 图形设备.原生设备指针)
            Dim 缓冲区大小 As UInteger = 4096
            Dim 缓冲区 = Marshal.AllocHGlobal(CInt(缓冲区大小))
            Try
                Dim 所需大小 As UInteger
                Dim 结果码 = 原生接口.FFF_ProbeD3D11Encoder(封送范围.值, 缓冲区, 缓冲区大小, 所需大小)
                If 结果码 = 原生结果.缓冲区不足 AndAlso 所需大小 > 缓冲区大小 Then
                    Marshal.FreeHGlobal(缓冲区)
                    缓冲区大小 = 所需大小
                    缓冲区 = Marshal.AllocHGlobal(CInt(缓冲区大小))
                    结果码 = 原生接口.FFF_ProbeD3D11Encoder(封送范围.值, 缓冲区, 缓冲区大小, 所需大小)
                End If

                Dim 文本 = Marshal.PtrToStringUTF8(缓冲区)
                If String.IsNullOrWhiteSpace(文本) Then 检查结果(结果码, "D3D11 编码器探测失败。")
                Using 文档 = JsonDocument.Parse(文本)
                    Dim 根 = 文档.RootElement
                    Dim 结果 As New 编码器探测结果 With {
                        .编码器名称 = 配置.编码器名称,
                        .支持 = 根.GetProperty("supported").GetBoolean(),
                        .适配器标识 = 图形设备.适配器标识, .使用十位色 = 配置.使用十位色,
                        .视频采样 = 配置.视频采样, .速率控制 = 配置.速率控制,
                        .编码预设 = 配置.编码预设, .编码配置档 = 配置.编码配置档,
                        .多遍模式 = 配置.多遍模式
                    }
                    Dim 原因 As JsonElement
                    If 根.TryGetProperty("reason", 原因) Then 结果.原因 = 原因.GetString()
                    If 结果码 <> 原生结果.成功 AndAlso 结果码 <> 原生结果.不支持 Then
                        检查结果(结果码, If(String.IsNullOrWhiteSpace(结果.原因), "D3D11 编码器探测失败。", 结果.原因))
                    End If
                    Return 结果
                End Using
            Finally
                Marshal.FreeHGlobal(缓冲区)
            End Try
        End Using
    End Function

    Private Shared Function 复制探测结果(来源 As 编码器探测结果) As 编码器探测结果
        Return New 编码器探测结果 With {
            .支持 = 来源.支持, .编码器名称 = 来源.编码器名称, .原因 = 来源.原因,
            .适配器标识 = 来源.适配器标识, .使用十位色 = 来源.使用十位色,
            .视频采样 = 来源.视频采样, .速率控制 = 来源.速率控制,
            .编码预设 = 来源.编码预设, .编码配置档 = 来源.编码配置档,
            .多遍模式 = 来源.多遍模式
        }
    End Function

    Public Shared Function 测试音频端点(端点标识 As String, 是否回环 As Boolean,
        Optional 测试毫秒 As UInteger = 500) As 音频端点测试结果
        验证原生接口()
        If String.IsNullOrWhiteSpace(端点标识) Then Throw New ArgumentException("端点标识不能为空。", NameOf(端点标识))
        Dim 标识指针 = Marshal.StringToCoTaskMemUTF8(端点标识)
        Try
            Dim 所需大小 As UInteger
            Dim 结果码 = 原生接口.FFF_TestAudioEndpoint(标识指针, If(是否回环, 1UI, 0UI), 测试毫秒,
                IntPtr.Zero, 0, 所需大小)
            If 结果码 <> 原生结果.缓冲区不足 Then 检查结果(结果码, "音频端点测试容量查询失败。")
            Dim 缓冲区 = Marshal.AllocHGlobal(CInt(所需大小))
            Try
                结果码 = 原生接口.FFF_TestAudioEndpoint(标识指针, If(是否回环, 1UI, 0UI), 测试毫秒,
                    缓冲区, 所需大小, 所需大小)
                Dim 文本 = Marshal.PtrToStringUTF8(缓冲区)
                If 结果码 <> 原生结果.成功 AndAlso String.IsNullOrWhiteSpace(文本) Then 检查结果(结果码, "音频端点测试失败。")
                Using 文档 = JsonDocument.Parse(文本)
                    Dim 根 = 文档.RootElement
                    Return New 音频端点测试结果 With {
                        .通过 = 根.GetProperty("passed").GetBoolean(),
                        .数据包数 = 读取无符号长整数(根, "packets"), .采样帧数 = 读取无符号长整数(根, "frames"),
                        .静音包数 = 读取无符号长整数(根, "silentPackets"),
                        .不连续次数 = 读取无符号长整数(根, "discontinuities"),
                        .时间戳错误次数 = 读取无符号长整数(根, "timestampErrors"),
                        .首设备位置 = 读取无符号长整数(根, "firstDevicePosition"),
                        .末设备位置 = 读取无符号长整数(根, "lastDevicePosition"),
                        .首QPC位置 = 读取无符号长整数(根, "firstQpc100ns"),
                        .末QPC位置 = 读取无符号长整数(根, "lastQpc100ns"),
                        .音频时钟频率 = 读取无符号长整数(根, "audioClockFrequency"),
                        .采样率 = CUInt(读取无符号长整数(根, "sampleRate")),
                        .声道数 = CUInt(读取无符号长整数(根, "channels")),
                        .位深 = CUInt(读取无符号长整数(根, "bitsPerSample")),
                        .错误 = If(根.TryGetProperty("error", Nothing), 根.GetProperty("error").GetString(), String.Empty)
                    }
                End Using
            Finally
                Marshal.FreeHGlobal(缓冲区)
            End Try
        Finally
            Marshal.FreeCoTaskMem(标识指针)
        End Try
    End Function

    Private Shared Function 读取无符号长整数(元素 As JsonElement, 名称 As String) As ULong
        Dim 值 As JsonElement
        Return If(元素.TryGetProperty(名称, 值), 值.GetUInt64(), 0UL)
    End Function

    Public Shared Function 创建会话(配置 As 录制配置, D3D11设备指针 As IntPtr) As 录制会话
        验证原生接口()
        ArgumentNullException.ThrowIfNull(配置)
        配置.验证()
        If D3D11设备指针 = IntPtr.Zero Then Throw New ArgumentException("D3D11 设备指针不能为空。", NameOf(D3D11设备指针))
        Return 录制会话.创建(配置, D3D11设备指针)
    End Function

    Private Delegate Function 原生文本查询(输出 As IntPtr, 输出大小 As UInteger, ByRef 所需大小 As UInteger) As 原生结果

    Private Shared Function 读取原生文本(查询 As 原生文本查询) As String
        验证原生接口()
        Dim 所需大小 As UInteger
        Dim 结果码 = 查询(IntPtr.Zero, 0, 所需大小)
        If 结果码 <> 原生结果.缓冲区不足 Then 检查结果(结果码, "原生文本容量查询失败。")
        Dim 缓冲区 = Marshal.AllocHGlobal(CInt(所需大小))
        Try
            结果码 = 查询(缓冲区, 所需大小, 所需大小)
            检查结果(结果码, "原生文本读取失败。")
            Return Marshal.PtrToStringUTF8(缓冲区)
        Finally
            Marshal.FreeHGlobal(缓冲区)
        End Try
    End Function

    Friend Shared Sub 检查结果(结果码 As 原生结果, 消息 As String)
        If 结果码 <> 原生结果.成功 Then Throw New 原生调用异常(CInt(结果码), 消息)
    End Sub

    Private Shared Function 检查接口兼容() As Boolean
        Dim 实际版本 = 获取接口版本()
        If 实际版本 <> 期望接口版本 Then
            Throw New InvalidOperationException($"FFF.Native C ABI 版本不兼容：托管层需要 {期望接口版本}，实际为 {实际版本}。")
        End If
        Return True
    End Function
End Class
