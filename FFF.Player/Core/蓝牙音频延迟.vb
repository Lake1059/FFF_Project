Imports System.Runtime.InteropServices
Imports System.Text.RegularExpressions
Imports Microsoft.Win32

''' <summary>默认输出端点的设备格式（WAVEFORMATEX / WAVEFORMATEXTENSIBLE）。
''' 只描述 Windows 实际送给蓝牙驱动的 PCM 形态，不代表 A2DP codec。</summary>
Public Class 音频输出格式
    Public Property 已获取 As Boolean = False
    Public Property 采样率 As Integer = 0
    Public Property 声道数 As Integer = 0
    Public Property 位深 As Integer = 0
    Public Property 有效位深 As Integer = 0
    Public Property 格式标签 As Integer = 0
    Public Property 格式名称 As String = "未知"

    Public ReadOnly Property 描述 As String
        Get
            If Not 已获取 Then Return "格式未知"
            Dim 声道文本 As String
            Select Case 声道数
                Case 1 : 声道文本 = "单声道"
                Case 2 : 声道文本 = "立体声"
                Case Else : 声道文本 = 声道数.ToString() & " 声道"
            End Select
            Return (采样率 / 1000.0).ToString("0.#") & " kHz / " &
                声道文本 & " / " & 位深.ToString() & " bit"
        End Get
    End Property
End Class

''' <summary>当前默认输出端点的蓝牙音频画像。探测层只填事实，补偿量由纯算术策略计算。</summary>
Public Class 蓝牙音频信息
    Public Property 是蓝牙 As Boolean = False
    Public Property 已连接 As Boolean = False
    Public Property 协议 As 蓝牙音频协议 = 蓝牙音频协议.无
    Public Property 协议名称 As String = ""
    Public Property 延迟下界毫秒 As Integer = 0
    Public Property 延迟上界毫秒 As Integer = 0
    Public Property 输出格式 As 音频输出格式 = Nothing
    Public Property 设备名称 As String = ""
    Public Property 端点名称 As String = ""
    Public Property 设备地址 As String = ""
    Public Property 端点标识 As String = ""
    Public Property 端点状态 As Integer = 0
    Public Property 说明 As String = ""

    ''' <summary>区间中位；未知协议返回 0，由策略层兜底。</summary>
    Public ReadOnly Property 估计延迟毫秒 As Integer
        Get
            If 延迟下界毫秒 > 0 AndAlso 延迟上界毫秒 > 0 Then
                Return (延迟下界毫秒 + 延迟上界毫秒) \ 2
            End If
            Return 0
        End Get
    End Property

    Public Shared ReadOnly 非蓝牙 As New 蓝牙音频信息 With {
        .说明 = "当前默认输出不是蓝牙端点"
    }
End Class

Public Enum 蓝牙音频协议
    无 = 0
    A2DP = 1
    HFP = 2
    LEAudio = 3
    未知 = 4
End Enum

''' <summary>蓝牙延迟补偿的纯算术层。内核 FFF3FP_SetAudioDelay 正值 = 音频更晚；
''' 蓝牙让声音晚到 L_BT，屏幕让画面晚到 L_disp，所以 d = L_disp - L_BT - 用户额外提前量。</summary>
Public Module 蓝牙音频延迟策略
    ''' <summary>内核允许的音频延迟上下限：±10 分钟（100ns 单位）。</summary>
    Public Const 最大绝对延迟100纳秒 As Long = 6_000_000_000L

    Public Const 未知下界毫秒 As Integer = 150
    Public Const 未知上界毫秒 As Integer = 300

    ''' <summary>端点 PCM 格式带来的主机侧额外开销（不是蓝牙 codec 延迟）。</summary>
    Public Const 多声道格式开销毫秒 As Double = 8.0
    Public Const 高采样率格式开销毫秒 As Double = 6.0
    Public Const 高位深格式开销毫秒 As Double = 3.0
    Public Const 最大格式开销毫秒 As Double = 20.0

    ''' <summary>协议级延迟区间（耳机侧，不含主机缓冲）。</summary>
    Public Function 协议区间(协议 As 蓝牙音频协议) As (下界 As Integer, 上界 As Integer)
        Select Case 协议
            Case 蓝牙音频协议.LEAudio
                Return (30, 100)
            Case 蓝牙音频协议.HFP
                Return (100, 250)
            Case 蓝牙音频协议.A2DP
                Return (150, 300)
            Case 蓝牙音频协议.未知
                Return (未知下界毫秒, 未知上界毫秒)
            Case Else
                Return (0, 0)
        End Select
    End Function

    ''' <summary>按刷新率估计“媒体时钟到期 → 像素发光”的软件侧延迟：
    ''' 2 个刷新周期（DWM 合成 + 扫描）+ 5ms（面板响应/调度余量）。
    ''' 低刷新率必须保留完整两帧（24Hz 约 88ms），所以上限放到 100ms；
    ''' 高刷新率下限仍为 10ms，避免 500Hz 以上把调度余量压没。</summary>
    Public Function 估计屏幕延迟毫秒(刷新率 As Integer) As Double
        Dim 赫兹 = If(刷新率 >= 24 AndAlso 刷新率 <= 500, 刷新率, 60)
        Dim 毫秒 = 2.0 * 1000.0 / 赫兹 + 5.0
        Return Math.Max(10.0, Math.Min(100.0, 毫秒))
    End Function

    ''' <summary>端点 PCM 格式的主机侧开销：多声道 downmix、96k/192k 重采样、
    ''' 24bit 以上转换各占一项。未知格式返回 0，完全退回协议区间。</summary>
    Public Function 格式开销毫秒(格式 As 音频输出格式) As Double
        If 格式 Is Nothing OrElse Not 格式.已获取 Then Return 0.0
        Dim 开销 As Double = 0.0
        If 格式.声道数 > 2 Then 开销 += 多声道格式开销毫秒
        If 格式.采样率 >= 88200 Then 开销 += 高采样率格式开销毫秒
        If Math.Max(格式.位深, 格式.有效位深) >= 24 Then 开销 += 高位深格式开销毫秒
        Return Math.Min(最大格式开销毫秒, 开销)
    End Function

    ''' <summary>计算应下发的 SetAudioDelay 值（100ns）。未启用、非蓝牙、未连接都返回 0。</summary>
    Public Function 计算补偿100纳秒(启用 As Boolean, 信息 As 蓝牙音频信息,
                                    刷新率 As Integer, 额外提前量毫秒 As Integer) As Long
        If Not 启用 OrElse 信息 Is Nothing OrElse Not 信息.是蓝牙 OrElse Not 信息.已连接 Then Return 0

        Dim 下界 = 信息.延迟下界毫秒
        Dim 上界 = 信息.延迟上界毫秒
        If 下界 <= 0 OrElse 上界 <= 0 OrElse 上界 < 下界 Then
            Dim 回退协议 = If(信息.协议 = 蓝牙音频协议.无, 蓝牙音频协议.未知, 信息.协议)
            Dim 区间 = 协议区间(回退协议)
            下界 = 区间.下界
            上界 = 区间.上界
        End If
        If 下界 <= 0 OrElse 上界 <= 0 Then Return 0

        Dim 蓝牙毫秒 = (下界 + 上界) / 2.0 + 格式开销毫秒(信息.输出格式)
        Dim 屏幕毫秒 = 估计屏幕延迟毫秒(刷新率)
        Dim 微调 = Math.Max(-100, Math.Min(100, 额外提前量毫秒))
        Dim 延迟毫秒 = 屏幕毫秒 - 蓝牙毫秒 - 微调
        Dim 延迟100纳秒 = CLng(Math.Round(延迟毫秒 * 10_000.0, MidpointRounding.AwayFromZero))
        Return Math.Max(-最大绝对延迟100纳秒, Math.Min(最大绝对延迟100纳秒, 延迟100纳秒))
    End Function

    Public Function 格式化毫秒(延迟100纳秒 As Long) As String
        Return (延迟100纳秒 / 10_000.0).ToString("+0.0;-0.0;0") & " ms"
    End Function
End Module

''' <summary>默认输出端点的蓝牙 profile 探测（Windows 用户态）。
''' 能拿到：MMDevices 端点 → ContainerID / DeviceState / PKEY_AudioEngine_DeviceFormat
''' → BTHENUM/BTHLE 服务节点 → Service / HardwareID。
''' 拿不到：具体 codec（SBC/AAC/aptX/LDAC）与 AVDTP Delay Reporting；只输出协议级区间，
''' 端点 PCM 格式仅用于主机侧格式开销，不伪造 codec。</summary>
Public Module 蓝牙音频检测器
    Private Const 渲染根 As String = "SOFTWARE\Microsoft\Windows\CurrentVersion\MMDevices\Audio\Render"
    Private Const 枚举根 As String = "SYSTEM\CurrentControlSet\Enum"
    Private Const 蓝牙枚举根 As String = "BTHENUM"
    Private Const 蓝牙低功耗根 As String = "BTHLE"

    Private Const 端点友好名称键 As String = "{a45c254e-df1c-4efd-8020-67d146a850e0},2"
    Private Const 设备描述键 As String = "{b3f8fa53-0004-438e-9003-51a46e139bfc},6"
    Private Const 容器标识键 As String = "{8c7ed206-3f8a-4827-b3ab-ae9e1faefc6c},2"
    Private Const 父设备旧键 As String = "{b3f8fa53-0004-438e-9003-51a46e139bfc},39"
    Private Const 父设备键 As String = "{4340a6c5-93fa-4706-972c-7b648008a5a7},8"
    ' PKEY_AudioEngine_DeviceFormat：MMDevices 端点默认设备格式（VT_BLOB，前 8 字节为
    ' PROPVARIANT 头，后面才是 WAVEFORMATEX）。
    Private Const 设备格式键 As String = "{f19f064d-082c-4e27-bc73-6882a1bb8e4c},0"
    Private Const 扩展格式标签 As Integer = &HFFFE
    Private ReadOnly PCM子格式 As New Guid("00000001-0000-0010-8000-00AA00389B71")
    Private ReadOnly Float子格式 As New Guid("00000003-0000-0010-8000-00AA00389B71")

    Private ReadOnly 服务标识正则 As New Regex(
        "\{([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{4}-[0-9A-Fa-f]{12})\}",
        RegexOptions.Compiled)
    Private ReadOnly 地址正则 As New Regex("&([0-9A-Fa-f]{12})_", RegexOptions.Compiled)
    Private ReadOnly 型号正则 As New Regex("\(([^()]+)\)\s*$", RegexOptions.Compiled)
    Private ReadOnly 本地化前缀正则 As New Regex("@[^;]*;", RegexOptions.Compiled)

    Private Const 标识A2DP As String = "0000110B-0000-1000-8000-00805F9B34FB"
    Private Const 标识HFP As String = "0000111E-0000-1000-8000-00805F9B34FB"
    Private Const 标识HFPAG As String = "00001112-0000-1000-8000-00805F9B34FB"
    Private Const 标识HSP As String = "00001108-0000-1000-8000-00805F9B34FB"

    Private Class 端点记录
        Public 端点标识 As String = ""
        Public 容器标识 As String = ""
        Public 友好名称 As String = ""
        Public 设备描述 As String = ""
        Public 状态 As Integer = 0
        Public 父设备实例 As String = ""
        Public 格式 As 音频输出格式 = Nothing
    End Class

    Private Class 蓝牙节点
        Public 容器标识 As String = ""
        Public 服务 As String = ""
        Public 硬件标识 As String = ""
        Public 友好名称 As String = ""
        Public 实例路径 As String = ""
        Public 地址 As String = ""
    End Class

    ''' <summary>探测当前默认渲染端点。任何失败都返回非蓝牙记录并带说明，不抛异常打断播放。</summary>
    Public Function 检测默认输出() As 蓝牙音频信息
        Try
            Dim 端点标识 = 尝试取默认端点标识()
            If String.IsNullOrWhiteSpace(端点标识) Then
                Return 失败("无法读取系统默认输出端点（IMMDeviceEnumerator 不可用）")
            End If
            Return 检测指定端点(端点标识)
        Catch ex As Exception
            Return 失败("蓝牙探测失败：" & ex.GetType().Name & ": " & ex.Message)
        End Try
    End Function

    ''' <summary>探测指定端点标识（测试/未来多端点选择器入口）。</summary>
    Public Function 检测指定端点(端点标识 As String) As 蓝牙音频信息
        Dim 端点 = 读取端点(端点标识)
        If 端点 Is Nothing Then Return 失败("端点不在 MMDevices 注册表中", 端点标识)

        Dim 节点 = 按容器查找节点(端点.容器标识)
        If 节点 IsNot Nothing Then Return 构建(端点, 节点)

        Dim 父设备 = 端点.父设备实例
        Dim 是蓝牙端点 = 父设备.IndexOf("BTHENUM", StringComparison.OrdinalIgnoreCase) >= 0 OrElse
            父设备.IndexOf("BTHLE", StringComparison.OrdinalIgnoreCase) >= 0
        If Not String.IsNullOrWhiteSpace(父设备) AndAlso 是蓝牙端点 Then
            节点 = 读取节点(父设备)
            If 节点 Is Nothing Then 节点 = 从父设备推断节点(父设备, 端点)
            If 节点 IsNot Nothing Then Return 构建(端点, 节点)
        End If

        Dim 结果 As New 蓝牙音频信息 With {
            .是蓝牙 = 是蓝牙端点,
            .已连接 = 端点.状态 = 1,
            .协议 = If(是蓝牙端点, 蓝牙音频协议.未知, 蓝牙音频协议.无),
            .输出格式 = 端点.格式,
            .端点标识 = 端点标识,
            .端点名称 = 端点.友好名称,
            .端点状态 = 端点.状态,
            .说明 = If(是蓝牙端点,
                "已由父设备识别为蓝牙端点，但服务节点当前不可用；按未知蓝牙协议兜底",
                If(端点.容器标识.Length > 0,
                    "端点容器未匹配到蓝牙服务节点（可能不是蓝牙设备）",
                    "端点没有 ContainerID（无法判定是否为蓝牙）"))
        }
        Return 结果
    End Function

    Private Function 失败(说明 As String, Optional 端点标识 As String = "") As 蓝牙音频信息
        Return New 蓝牙音频信息 With {
            .是蓝牙 = False,
            .已连接 = False,
            .协议 = 蓝牙音频协议.无,
            .端点标识 = 端点标识,
            .说明 = 说明
        }
    End Function

    Private Function 构建(端点 As 端点记录, 节点 As 蓝牙节点) As 蓝牙音频信息
        Dim 协议 = 映射协议(节点.服务, 节点.硬件标识, 节点.实例路径)
        Dim 区间 = 蓝牙音频延迟策略.协议区间(协议)
        Dim 型号 = 友好型号(节点.友好名称)
        If String.IsNullOrWhiteSpace(型号) Then 型号 = 端点.设备描述
        If String.IsNullOrWhiteSpace(型号) Then 型号 = 端点.友好名称

        Dim 状态文本 As String
        Select Case 端点.状态
            Case 1 : 状态文本 = "在位"
            Case 2 : 状态文本 = "已禁用"
            Case 4 : 状态文本 = "未连接"
            Case 8 : 状态文本 = "未插入"
            Case Else : 状态文本 = "未知"
        End Select

        Return New 蓝牙音频信息 With {
            .是蓝牙 = True,
            .已连接 = 端点.状态 = 1,
            .协议 = 协议,
            .协议名称 = 协议名称(协议, 节点.服务),
            .延迟下界毫秒 = 区间.下界,
            .延迟上界毫秒 = 区间.上界,
            .输出格式 = 端点.格式,
            .设备名称 = 型号,
            .端点名称 = 端点.友好名称,
            .设备地址 = 节点.地址,
            .端点标识 = 端点.端点标识,
            .端点状态 = 端点.状态,
            .说明 = 状态文本 & "；Service=" & 节点.服务
        }
    End Function

    Private Function 协议名称(协议 As 蓝牙音频协议, 服务 As String) As String
        If 协议 = 蓝牙音频协议.LEAudio Then Return "LE-Audio"
        If 协议 = 蓝牙音频协议.A2DP Then Return "A2DP"
        If 协议 = 蓝牙音频协议.HFP Then
            Return If(服务.IndexOf("HSP", StringComparison.OrdinalIgnoreCase) >= 0, "HSP", "HFP")
        End If
        Return "Bluetooth"
    End Function

    Private Function 映射协议(服务 As String, 硬件标识 As String, 实例路径 As String) As 蓝牙音频协议
        If 服务.IndexOf("BthLEAudio", StringComparison.OrdinalIgnoreCase) >= 0 OrElse
           实例路径.IndexOf("BTHLE", StringComparison.OrdinalIgnoreCase) >= 0 Then
            Return 蓝牙音频协议.LEAudio
        End If

        Dim 标识 = 提取服务标识(硬件标识)
        If 标识 Is Nothing Then 标识 = 提取服务标识(实例路径)
        If 标识 IsNot Nothing Then
            Dim 大写 = 标识.ToUpperInvariant()
            If 大写 = 标识A2DP Then Return 蓝牙音频协议.A2DP
            If 大写 = 标识HFP OrElse 大写 = 标识HFPAG OrElse 大写 = 标识HSP Then Return 蓝牙音频协议.HFP
        End If

        If 服务.IndexOf("BthA2dp", StringComparison.OrdinalIgnoreCase) >= 0 Then Return 蓝牙音频协议.A2DP
        If 服务.IndexOf("BthHF", StringComparison.OrdinalIgnoreCase) >= 0 OrElse
           服务.IndexOf("BthHsp", StringComparison.OrdinalIgnoreCase) >= 0 Then Return 蓝牙音频协议.HFP
        Return 蓝牙音频协议.未知
    End Function

    Private Function 提取服务标识(文本 As String) As String
        If String.IsNullOrWhiteSpace(文本) Then Return Nothing
        Dim 匹配 = 服务标识正则.Match(文本)
        Return If(匹配.Success, 匹配.Groups(1).Value, Nothing)
    End Function

    ' ── MMDevices 端点 ───────────────────────────────────────────────

    Private Function 读取端点(端点标识 As String) As 端点记录
        Dim 标识 = 最后标识(端点标识)
        If 标识 Is Nothing Then Return Nothing

        Using 根 = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64)
            Using 端点键 = 根.OpenSubKey(渲染根 & "\" & 标识)
                If 端点键 Is Nothing Then Return Nothing
                Dim 状态 = If(读取整数(端点键.GetValue("DeviceState")), 0)
                Using 属性键 = 端点键.OpenSubKey("Properties")
                    If 属性键 Is Nothing Then Return Nothing
                    Dim 容器 = 读取容器标识(属性键.GetValue(容器标识键))
                    Dim 名称 = If(读取字符串(属性键.GetValue(端点友好名称键)), "")
                    Dim 描述 = If(读取字符串(属性键.GetValue(设备描述键)), "")
                    Dim 父设备 = 读取字符串(属性键.GetValue(父设备键))
                    If 父设备 Is Nothing Then 父设备 = 读取字符串(属性键.GetValue(父设备旧键))
                    If 父设备 Is Nothing Then 父设备 = ""
                    Return New 端点记录 With {
                        .端点标识 = 端点标识,
                        .容器标识 = 容器,
                        .友好名称 = 名称,
                        .设备描述 = 描述,
                        .状态 = 状态,
                        .父设备实例 = 父设备,
                        .格式 = 读取音频格式(属性键.GetValue(设备格式键))
                    }
                End Using
            End Using
        End Using
    End Function

    ''' <summary>ContainerId 属性是“8 字节头 + 16 字节 GUID”的 blob；取尾部 16 字节。</summary>
    Private Function 读取容器标识(原始 As Object) As String
        If TypeOf 原始 Is Byte() Then
            Dim 字节 = DirectCast(原始, Byte())
            If 字节.Length >= 16 Then
                Dim 尾部(15) As Byte
                Array.Copy(字节, 字节.Length - 16, 尾部, 0, 16)
                Return New Guid(尾部).ToString("B").ToUpperInvariant()
            End If
        End If
        Dim 文本 = 读取字符串(原始)
        Return If(文本 Is Nothing, "", 文本.Trim().ToUpperInvariant())
    End Function

    ''' <summary>解析 MMDevices 的 PKEY_AudioEngine_DeviceFormat。
    ''' 原始 blob 前 8 字节是 PROPVARIANT 头，之后是 WAVEFORMATEX；
    ''' 扩展格式在 +18 读有效位深、+24 读子格式 GUID。</summary>
    Private Function 读取音频格式(原始 As Object) As 音频输出格式
        If Not TypeOf 原始 Is Byte() Then Return Nothing
        Dim 字节 = DirectCast(原始, Byte())
        Const 头长度 As Integer = 8
        If 字节.Length < 头长度 + 18 Then Return Nothing

        Dim 标签 = BitConverter.ToUInt16(字节, 头长度)
        Dim 声道 = BitConverter.ToUInt16(字节, 头长度 + 2)
        Dim 采样 = BitConverter.ToUInt32(字节, 头长度 + 4)
        Dim 位深 = BitConverter.ToUInt16(字节, 头长度 + 14)
        If 声道 = 0 OrElse 采样 = 0 OrElse 位深 = 0 Then Return Nothing

        Dim 有效位深 = 位深
        Dim 名称 = 格式标签名称(标签)
        If 标签 = 扩展格式标签 AndAlso 字节.Length >= 头长度 + 40 Then
            Dim 扩展有效位 = BitConverter.ToUInt16(字节, 头长度 + 18)
            If 扩展有效位 > 0 Then 有效位深 = 扩展有效位
            Dim 子格式字节(15) As Byte
            Array.Copy(字节, 头长度 + 24, 子格式字节, 0, 16)
            Dim 子格式 As New Guid(子格式字节)
            名称 = 子格式名称(子格式)
        End If

        Return New 音频输出格式 With {
            .已获取 = True,
            .采样率 = CInt(采样),
            .声道数 = CInt(声道),
            .位深 = CInt(位深),
            .有效位深 = CInt(有效位深),
            .格式标签 = CInt(标签),
            .格式名称 = 名称
        }
    End Function

    Private Function 格式标签名称(标签 As Integer) As String
        Select Case 标签
            Case 1 : Return "PCM"
            Case 3 : Return "Float"
            Case 扩展格式标签 : Return "Extensible"
            Case Else : Return "Codec 0x" & 标签.ToString("X4")
        End Select
    End Function

    Private Function 子格式名称(子格式 As Guid) As String
        If 子格式 = PCM子格式 Then Return "PCM"
        If 子格式 = Float子格式 Then Return "Float"
        Return "Extensible"
    End Function

    Private Function 最后标识(文本 As String) As String
        Dim 起点 = 文本.LastIndexOf("{"c)
        If 起点 < 0 Then Return Nothing
        Dim 终点 = 文本.IndexOf("}"c, 起点)
        If 终点 <= 起点 Then Return Nothing
        Dim 标识文本 = 文本.Substring(起点, 终点 - 起点 + 1)
        Dim 解析 As Guid
        If Guid.TryParse(标识文本, 解析) Then Return 解析.ToString("B").ToUpperInvariant()
        Return Nothing
    End Function

    ' ── BTHENUM / BTHLE 服务节点 ─────────────────────────────────────

    Private Function 按容器查找节点(容器标识 As String) As 蓝牙节点
        If String.IsNullOrWhiteSpace(容器标识) Then Return Nothing
        Dim 最佳 As 蓝牙节点 = Nothing
        For Each 根名 In {蓝牙枚举根, 蓝牙低功耗根}
            Using 根 = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64)
                Using 枚举键 = 根.OpenSubKey(枚举根 & "\" & 根名)
                    If 枚举键 Is Nothing Then Continue For
                    For Each 顶层名 In 枚举键.GetSubKeyNames()
                        Using 顶层键 = 枚举键.OpenSubKey(顶层名)
                            If 顶层键 Is Nothing Then Continue For
                            Dim 顶层节点 = 读取节点键(顶层键, 根名 & "\" & 顶层名)
                            If 顶层节点 IsNot Nothing AndAlso 容器相等(顶层节点.容器标识, 容器标识) Then
                                最佳 = 优先(最佳, 顶层节点)
                            End If
                            For Each 子项名 In 顶层键.GetSubKeyNames()
                                Using 子键 = 顶层键.OpenSubKey(子项名)
                                    If 子键 Is Nothing Then Continue For
                                    Dim 节点 = 读取节点键(子键, 根名 & "\" & 顶层名 & "\" & 子项名)
                                    If 节点 IsNot Nothing AndAlso 容器相等(节点.容器标识, 容器标识) Then
                                        最佳 = 优先(最佳, 节点)
                                    End If
                                End Using
                            Next
                        End Using
                    Next
                End Using
            End Using
        Next
        Return 最佳
    End Function

    Private Function 优先(当前 As 蓝牙节点, 候选 As 蓝牙节点) As 蓝牙节点
        If 当前 Is Nothing Then Return 候选
        ' 同一容器可能同时挂 A2DP 与 HFP 服务节点；媒体端点默认选择优先 A2DP。
        If 当前.服务.IndexOf("BthA2dp", StringComparison.OrdinalIgnoreCase) >= 0 Then Return 当前
        If 候选.服务.IndexOf("BthA2dp", StringComparison.OrdinalIgnoreCase) >= 0 Then Return 候选
        If 当前.服务.IndexOf("BthLEAudio", StringComparison.OrdinalIgnoreCase) >= 0 Then Return 当前
        If 候选.服务.IndexOf("BthLEAudio", StringComparison.OrdinalIgnoreCase) >= 0 Then Return 候选
        Return 当前
    End Function

    Private Function 读取节点(实例路径 As String) As 蓝牙节点
        Dim 路径 = 规范化实例路径(实例路径)
        If 路径.Length = 0 Then Return Nothing
        Using 根 = RegistryKey.OpenBaseKey(RegistryHive.LocalMachine, RegistryView.Registry64)
            Using 键 = 根.OpenSubKey(枚举根 & "\" & 路径)
                If 键 Is Nothing Then Return Nothing
                Return 读取节点键(键, 路径)
            End Using
        End Using
    End Function

    ''' <summary>MMDevices 的父设备实例常带 "{1}." 前缀（如 {1}.BTHENUM\...）。
    ''' 去掉前缀，只保留从 BTHENUM\ / BTHLE\ 开始的枚举路径。</summary>
    Private Function 规范化实例路径(实例路径 As String) As String
        If String.IsNullOrWhiteSpace(实例路径) Then Return ""
        Dim 文本 = 实例路径.Trim()
        For Each 根名 In {蓝牙枚举根, 蓝牙低功耗根}
            Dim 索引 = 文本.IndexOf(根名 & "\", StringComparison.OrdinalIgnoreCase)
            If 索引 >= 0 Then Return 文本.Substring(索引)
        Next
        Return 文本
    End Function

    ''' <summary>服务节点在设备未连接时可能暂时不存在；此时从父设备路径的服务 UUID
    ''' 推断协议，至少保留“是蓝牙/未连接”的事实，不让设置页误报非蓝牙。</summary>
    Private Function 从父设备推断节点(父设备 As String, 端点 As 端点记录) As 蓝牙节点
        Dim 路径 = 规范化实例路径(父设备)
        If 路径.Length = 0 Then Return Nothing
        If Not (路径.StartsWith(蓝牙枚举根 & "\", StringComparison.OrdinalIgnoreCase) OrElse
                路径.StartsWith(蓝牙低功耗根 & "\", StringComparison.OrdinalIgnoreCase)) Then Return Nothing

        Dim 服务 = 推断服务(路径)
        If 服务.Length = 0 Then Return Nothing
        Dim 叶节点 = 路径.Substring(路径.LastIndexOf("\"c) + 1)
        Return New 蓝牙节点 With {
            .容器标识 = 端点.容器标识,
            .服务 = 服务,
            .硬件标识 = 路径,
            .友好名称 = 端点.友好名称,
            .实例路径 = 路径,
            .地址 = 格式化地址(叶节点)
        }
    End Function

    Private Function 推断服务(路径 As String) As String
        If 路径.StartsWith(蓝牙低功耗根 & "\", StringComparison.OrdinalIgnoreCase) Then Return "BthLEAudio"
        Dim 标识 = 提取服务标识(路径)
        If 标识 IsNot Nothing Then
            Dim 大写 = 标识.ToUpperInvariant()
            If 大写 = 标识A2DP Then Return "BthA2dp"
            If 大写 = 标识HFP OrElse 大写 = 标识HFPAG OrElse 大写 = 标识HSP Then Return "BthHFEnum"
        End If
        Return ""
    End Function

    Private Function 读取节点键(键 As RegistryKey, 实例路径 As String) As 蓝牙节点
        Dim 服务 = 读取字符串(键.GetValue("Service"))
        If 服务 Is Nothing Then 服务 = ""
        If Not (服务.IndexOf("BthA2dp", StringComparison.OrdinalIgnoreCase) >= 0 OrElse
                服务.IndexOf("BthHF", StringComparison.OrdinalIgnoreCase) >= 0 OrElse
                服务.IndexOf("BthHsp", StringComparison.OrdinalIgnoreCase) >= 0 OrElse
                服务.IndexOf("BthLEAudio", StringComparison.OrdinalIgnoreCase) >= 0) Then Return Nothing

        Dim 硬件 = 首个字符串(键.GetValue("HardwareID"))
        If 硬件 Is Nothing Then 硬件 = ""
        Dim 友好 = 读取字符串(键.GetValue("FriendlyName"))
        If 友好 Is Nothing Then 友好 = ""
        Dim 容器 = 读取字符串(键.GetValue("ContainerID"))
        If 容器 Is Nothing Then 容器 = ""
        Dim 地址 = 格式化地址(实例路径.Substring(实例路径.LastIndexOf("\"c) + 1))
        Return New 蓝牙节点 With {
            .容器标识 = 容器.Trim().ToUpperInvariant(),
            .服务 = 服务,
            .硬件标识 = 硬件,
            .友好名称 = 友好,
            .实例路径 = 实例路径,
            .地址 = 地址
        }
    End Function

    Private Function 容器相等(甲 As String, 乙 As String) As Boolean
        Return 甲.Length > 0 AndAlso 乙.Length > 0 AndAlso
            String.Equals(甲, 乙, StringComparison.OrdinalIgnoreCase)
    End Function

    Private Function 格式化地址(叶节点 As String) As String
        Dim 匹配 = 地址正则.Match(叶节点)
        If Not 匹配.Success Then Return ""
        Dim 十六进制 = 匹配.Groups(1).Value.ToUpperInvariant()
        Dim 分段(5) As String
        For 索引 = 0 To 5
            分段(索引) = 十六进制.Substring(索引 * 2, 2)
        Next
        Return String.Join(":", 分段)
    End Function

    Private Function 友好型号(本地化 As String) As String
        If String.IsNullOrWhiteSpace(本地化) Then Return ""
        Dim 匹配 = 型号正则.Match(本地化)
        If 匹配.Success Then Return 匹配.Groups(1).Value.Trim()
        Return 本地化前缀正则.Replace(本地化, "").Trim()
    End Function

    ' ── 基础读取 ─────────────────────────────────────────────────────

    Private Function 读取字符串(值 As Object) As String
        If 值 Is Nothing Then Return Nothing
        If TypeOf 值 Is String Then Return DirectCast(值, String)
        If TypeOf 值 Is String() Then
            Dim 数组 = DirectCast(值, String())
            If 数组.Length > 0 Then Return 数组(0)
        End If
        Return Nothing
    End Function

    Private Function 首个字符串(值 As Object) As String
        Return 读取字符串(值)
    End Function

    Private Function 读取整数(值 As Object) As Integer?
        If 值 Is Nothing Then Return Nothing
        If TypeOf 值 Is Integer Then Return CInt(值)
        If TypeOf 值 Is Long Then Return CInt(值)
        If TypeOf 值 Is String Then
            Dim 解析 As Integer
            If Integer.TryParse(DirectCast(值, String), 解析) Then Return 解析
        End If
        Return Nothing
    End Function

    ' ── 显示刷新率 ───────────────────────────────────────────────────

    <DllImport("user32.dll", EntryPoint:="GetDC")>
    Private Function 取得设备上下文(窗口 As IntPtr) As IntPtr
    End Function

    <DllImport("user32.dll", EntryPoint:="ReleaseDC")>
    Private Function 释放设备上下文(窗口 As IntPtr, 上下文 As IntPtr) As Integer
    End Function

    <DllImport("gdi32.dll", EntryPoint:="GetDeviceCaps")>
    Private Function 取得设备能力(上下文 As IntPtr, 索引 As Integer) As Integer
    End Function

    ''' <summary>窗口所在显示器的当前刷新率；取不到按 60Hz。</summary>
    Public Function 取刷新率(窗口 As IntPtr) As Integer
        Dim 上下文 As IntPtr = IntPtr.Zero
        Try
            上下文 = 取得设备上下文(窗口)
            If 上下文 = IntPtr.Zero Then Return 60
            Dim 刷新率 = 取得设备能力(上下文, 116) ' VREFRESH
            Return If(刷新率 >= 24 AndAlso 刷新率 <= 500, 刷新率, 60)
        Catch
            Return 60
        Finally
            If 上下文 <> IntPtr.Zero Then 释放设备上下文(窗口, 上下文)
        End Try
    End Function

    ' ── COM：默认渲染端点（裸 vtable，不用 ComImport） ─────────────
    '
    ' 只调用 IMMDeviceEnumerator::GetDefaultAudioEndpoint 与 IMMDevice::GetId：
    ' IUnknown 0..2；IMMDeviceEnumerator 3..7；IMMDevice 3..6。

    Private Const 渲染流 As Integer = 0
    Private Const 控制台角色 As Integer = 0
    Private Const 进程内服务器 As UInteger = 1UI
    Private ReadOnly 枚举器类标识 As New Guid("BCDE0395-E52F-467C-8E3D-C4579291692E")
    Private ReadOnly 枚举器接口标识 As New Guid("A95664D2-9614-4F35-A746-DE8DB63617E6")

    <DllImport("ole32.dll", ExactSpelling:=True, EntryPoint:="CoCreateInstance")>
    Private Function 创建实例(ByRef 类标识 As Guid, 外部 As IntPtr, 上下文 As UInteger,
                              ByRef 接口标识 As Guid, ByRef 实例 As IntPtr) As Integer
    End Function

    <UnmanagedFunctionPointer(CallingConvention.Winapi)>
    Private Delegate Function 取默认端点委托(自身 As IntPtr, 数据流 As Integer,
                                              角色 As Integer, ByRef 端点 As IntPtr) As Integer

    <UnmanagedFunctionPointer(CallingConvention.Winapi)>
    Private Delegate Function 取标识委托(自身 As IntPtr, ByRef 标识 As IntPtr) As Integer

    <UnmanagedFunctionPointer(CallingConvention.Winapi)>
    Private Delegate Function 释放委托(自身 As IntPtr) As Integer

    Private Function 虚表槽(实例 As IntPtr, 槽 As Integer) As IntPtr
        Dim 虚表 = Marshal.ReadIntPtr(实例)
        Return Marshal.ReadIntPtr(虚表, 槽 * IntPtr.Size)
    End Function

    Private Sub 释放实例(实例 As IntPtr)
        If 实例 = IntPtr.Zero Then Return
        Try
            Dim 释放 = Marshal.GetDelegateForFunctionPointer(Of 释放委托)(虚表槽(实例, 2))
            释放(实例)
        Catch
            ' 释放失败不能盖住探测结果。
        End Try
    End Sub

    Private Function 尝试取默认端点标识() As String
        Dim 枚举器 As IntPtr = IntPtr.Zero
        Dim 设备 As IntPtr = IntPtr.Zero
        Try
            Dim 类标识 = 枚举器类标识
            Dim 接口标识 = 枚举器接口标识
            Dim 结果 = 创建实例(类标识, IntPtr.Zero, 进程内服务器, 接口标识, 枚举器)
            If 结果 < 0 OrElse 枚举器 = IntPtr.Zero Then Return Nothing

            Dim 取默认 = Marshal.GetDelegateForFunctionPointer(Of 取默认端点委托)(虚表槽(枚举器, 4))
            结果 = 取默认(枚举器, 渲染流, 控制台角色, 设备)
            If 结果 < 0 OrElse 设备 = IntPtr.Zero Then Return Nothing

            Dim 取标识 = Marshal.GetDelegateForFunctionPointer(Of 取标识委托)(虚表槽(设备, 5))
            Dim 标识指针 As IntPtr = IntPtr.Zero
            结果 = 取标识(设备, 标识指针)
            If 结果 < 0 OrElse 标识指针 = IntPtr.Zero Then Return Nothing
            Try
                Return Marshal.PtrToStringUni(标识指针)
            Finally
                Marshal.FreeCoTaskMem(标识指针)
            End Try
        Catch
            Return Nothing
        Finally
            释放实例(设备)
            释放实例(枚举器)
        End Try
    End Function
End Module
