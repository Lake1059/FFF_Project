Imports System.Runtime.InteropServices
Imports Microsoft.Win32.SafeHandles

Friend Enum 原生播放器结果 As Integer
    成功 = 0
    参数无效 = -1
    状态无效 = -2
    缓冲区不足 = -3
    原生失败 = -4
    FFmpeg失败 = -5
    设备失败 = -6
    不支持 = -7
End Enum

<UnmanagedFunctionPointer(CallingConvention.Cdecl)>
Friend Delegate Sub 原生播放器回调(上下文 As IntPtr, 事件类型 As UInteger, 详情UTF8 As IntPtr)

<StructLayout(LayoutKind.Sequential)>
Friend Structure 原生播放器配置
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public 输出窗口 As IntPtr
    Public 解码器 As UInteger
    Public 色彩模式 As UInteger
    Public SDR峰值 As Single
    Public HDR峰值 As Single
    Public SDR纸白 As Single
    Public 音频端点UTF8 As IntPtr
    Public 回调 As IntPtr
    Public 回调上下文 As IntPtr
    Public 视频缩放质量 As UInteger
    Public 强制HDR输出 As UInteger
    ' API 15 新增：指定由哪个 DXGI 适配器创建 D3D11 设备；-1 保持原有策略。
    ' 必须追加在结构体末尾，否则字段偏移与内核不一致（结构体 72 -> 80 字节）。
    Public 首选适配器索引 As Integer
    ' API 16 新增：SDR 源的 scRGB 呈现策略。0 = 从不（历史行为），
    ' 1 = 自动（显示器开 Advanced Color 时，位深 >8 或广色域的 SDR 源走 16bit scRGB 链）。
    ' 同样必须追加在末尾：内核按 size >= sizeof(FFF3FPConfiguration) 校验，
    ' 少这一个字段就是 80 < 84 ⇒ FFF3FP_Create 直接 InvalidArgument（会话根本起不来）。
    Public SDRscRGB模式 As UInteger
    ' API 17/18 新增：自适应 CPU 预缩放 与 软解线程档位。
    '
    ' ⚠ 这些字段**必须存在**，即使宿主不使用：内核按
    ' size >= sizeof(FFF3FPConfiguration) 校验，缺一个字段就会被
    ' FFF3FP_Create 直接判为 InvalidArgument（result=-1），**会话根本起不来**。
    ' 实测：宿主停在 API 16（80 字节）而内核为 API 18（112 字节）时，
    ' Create 返回 -1，播放器完全无法启动。
    '
    ' 默认值与内核保持一致；宿主未提供 UI 时用这些默认即可。
    ' 0 = 关闭（保持固定线程策略），非 0 = 启用自适应。
    Public 自适应CPU预缩放 As UInteger
    Public 自适应CPU预缩放丢帧百分比 As UInteger
    Public 自适应解码线程 As UInteger
    Public 软解最小线程数 As UInteger
    Public 软解最大线程数 As UInteger
    Public 解码升档丢帧百分比 As UInteger
    Public 解码降档丢帧百分比 As UInteger
End Structure

<StructLayout(LayoutKind.Sequential)>
Friend Structure 原生播放器快照
    ' ⚠ 字段顺序**必须与 C 侧 FFF3FPSnapshot 逐字段一致**。顺序错了不会报错，
    ' 只会静默读出错位的值 —— 曾经发生过：IAMF 三字段被追加到末尾（C 侧在第 26-28 位），
    ' 于是其后约 30 个字段全部偏移，UI 显示的是别的字段内容。
    ' 改动本结构体后，用 tools 里的快照布局校验确认与 C 侧一致。
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public 状态 As UInteger
    Public 解码器 As UInteger
    Public 请求色彩模式 As UInteger
    Public 实际色彩模式 As UInteger
    Public 位置100纳秒 As Long
    Public 时长100纳秒 As Long
    Public 帧序号 As Long
    Public 原始帧PTS As Long
    Public 帧时间基分子 As Integer
    Public 帧时间基分母 As Integer
    Public 当前视频流 As Integer
    Public 当前音频流 As Integer
    Public 视频宽度 As UInteger
    Public 视频高度 As UInteger
    Public 是HDR源 As UInteger
    Public 正在使用外部音轨 As UInteger
    Public 外部音轨偏移100纳秒 As Long
    Public 已解码视频帧数 As ULong
    Public 已呈现视频帧数 As ULong
    Public 已丢弃视频帧数 As ULong
    Public 视频队列帧数 As UInteger
    Public 源峰值尼特 As UInteger
    Public 已解码音频帧数 As ULong
    '（IAMF 三个字段已挪到本结构末尾，与 C 侧一致。）
    Public 音频位置100纳秒 As Long
    Public 音频缓冲100纳秒 As Long
    Public 音频欠载次数 As ULong
    Public 音频时间戳抖动帧数 As ULong
    Public 音频不连续次数 As ULong
    Public 音频插入静音帧数 As ULong
    Public 音频丢弃重叠帧数 As ULong
    Public 已合并视频帧数 As ULong
    Public 音频拒绝帧数 As ULong
    Public 交换链呈现次数 As ULong
    Public 呈现等待100纳秒 As ULong
    Public 设备锁等待100纳秒 As ULong
    Public 硬件传输100纳秒 As ULong
    Public 软件转换100纳秒 As ULong
    Public 视频实时比特率 As ULong
    Public 音频实时比特率 As ULong
    Public 视频输出位深度 As UInteger
    Public 视频缩放模式 As UInteger
    Public 时间轴代次 As ULong
    Public HDR格式 As UInteger
    Public 兼容HDR格式 As UInteger
    Public HDR处理路径 As UInteger
    Public 杜比视界配置档次 As UInteger
    Public 杜比视界级别 As UInteger
    Public 有杜比视界RPU As UInteger
    Public 有杜比视界增强层 As UInteger
    Public 杜比视界增强层类型 As UInteger
    Public 动态HDR元数据有效 As UInteger
    Public HDR回退有效 As UInteger
    Public 显示器最小亮度毫尼特 As UInteger
    Public 显示器峰值尼特 As UInteger
    Public 显示器全屏峰值尼特 As UInteger
    Public HDR有效目标峰值尼特 As UInteger
    ' ST 2094-40 dynamic metadata diagnostics; appended to FFF3FPSnapshot's tail.
    ' The kernel rejects a caller whose declared size is smaller than its struct, so
    ' these have to be mirrored here or FFF3FP_GetSnapshot fails for the whole host.
    Public 动态HDR元数据窗口数 As UInteger
    Public 动态HDR元数据回退 As UInteger
    Public 动态HDR元数据序号 As ULong
    Public 动态HDR元数据保持帧数 As ULong
    Public 动态HDR元数据目标尼特 As UInteger
    ' IAMF 沉浸式音频（与 C 侧一致，一律追加在末尾：字段偏移必须保持稳定）。
    ' IAMF已接管=1 表示该音轨由 AOM 参考解码器渲染；IAMF声道数 是**渲染目标**声道数
    ' （固定 7.1.4 = 12），而 IAMF内容声道数 才是文件里实际声明的声道数。
    Public IAMF已接管 As UInteger
    Public IAMF声道数 As UInteger
    Public IAMF声场系统 As Integer

    ' 上传耗时（追加在末尾）。
    Public 视频上传100纳秒 As ULong
    ' IAMF 内容声道数（最后追加）：文件声明的声道数，可能小于 IAMF声道数。
    ' 例：7.1 内容按 7.1.4 渲染，libiamf 会把多出的 4 个声道填成**数字静音**
    ' （实测峰值 -inf），所以这不是"上混出假内容"，但 UI 应显示内容而非渲染目标 ——
    ' 否则 7.1 素材会显示成 7.1.4，而实际有 4 只音箱不出声。
    Public IAMF内容声道数 As UInteger
End Structure

<StructLayout(LayoutKind.Sequential)>
Friend Structure 原生图片信息
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public 帧数 As Integer              ' <0 未知
    Public 循环次数 As Integer          ' 0=无限, -1=不循环, >0=次数; <0 未知
    Public 标志 As UInteger             ' 原生图片标志.*
    Public 旋转四分之一圈 As UInteger   ' 0..3；&HFFFFFFFF 未知
    Public 源像素格式 As Integer
    Public 源位深 As UInteger
    Public ICC配置字节数 As UInteger
    Public 色彩原色 As Integer
    Public 色彩空间 As Integer
    Public 色彩传递 As Integer
    ' 与 C 侧 `std::uint32_t reserved[4]` 对齐。用四个独立字段而不是 ByValArray，
    ' 免去数组封送与初始化要求，布局仍是连续的 16 字节。
    Public 保留1 As UInteger
    Public 保留2 As UInteger
    Public 保留3 As UInteger
    Public 保留4 As UInteger
End Structure

''' <summary>IAMF 声场系统（对应 C 侧 IAMF_SoundSystem / 原生解码器的解码输出布局）。
''' 名称按 AOM IAMF 规范的 "Upper+Middle+Bottom" 记法写出，括号内为声道数。</summary>
Friend Module 原生IAMF声场系统
    Public Const 立体声 As Integer = 0            ' 0+2+0, 2
    Public Const 五点一 As Integer = 1            ' 0+5+0, 6
    Public Const 七点一 As Integer = 2            ' 2+5+0, 8
    Public Const 四点零五点一 As Integer = 3      ' 4+5+0, 10
    Public Const 七点一加一 As Integer = 4        ' 4+7+0, 11
    Public Const 七点一点四 As Integer = 5        ' 3+7+0, 12
    Public Const 九点一点四 As Integer = 6        ' 4+9+0, 14
    Public Const 九点一点六 As Integer = 7        ' 9+10+... 24（库实测 24）
    Public Const 五点一点二 As Integer = 8        ' 2+5+0, 8（扩展）
    Public Const 七点一点二 As Integer = 9        ' 2+7+0, 12（扩展）
    Public Const 七点一点二可替 As Integer = 10   ' 3+7+0, 10（扩展）
    Public Const 三点一点二 As Integer = 11       ' 3+1+2, 6（扩展）
    Public Const 单声道 As Integer = 12           ' 1+0+0, 1
    Public Const 九点一点六可替 As Integer = 13   ' 9+1+6, 16（扩展）
    Public Const 七点一点五点四 As Integer = 14   ' 7+1+6, 16（扩展）

    ''' <summary>取便于显示的布局名，如 "7.1.4"。未知索引返回空串。</summary>
    Friend Function 布局名(索引 As Integer) As String
        Select Case 索引
            Case 立体声 : Return "2.0"
            Case 五点一 : Return "5.1"
            Case 七点一 : Return "7.1"
            Case 四点零五点一 : Return "5.1.4"
            Case 七点一加一 : Return "7.1.1"
            Case 七点一点四 : Return "7.1.4"
            Case 九点一点四 : Return "9.1.4"
            Case 九点一点六 : Return "9.1.6"
            Case 五点一点二 : Return "5.1.2"
            Case 七点一点二 : Return "7.1.2"
            Case 七点一点二可替 : Return "7.1.2"
            Case 三点一点二 : Return "3.1.2"
            Case 单声道 : Return "1.0"
            Case 九点一点六可替 : Return "9.1.6"
            Case 七点一点五点四 : Return "7.1.5.4"
            Case Else : Return String.Empty
        End Select
    End Function
End Module
Friend Module 原生图片标志
    Public Const 静态 As UInteger = &H1UI
    Public Const 动画 As UInteger = &H2UI
    Public Const 多帧 As UInteger = &H4UI
    Public Const 含透明 As UInteger = &H8UI
    Public Const 含ICC As UInteger = &H10UI
    Public Const 含旋转 As UInteger = &H20UI
    Public Const 广色域 As UInteger = &H40UI
End Module

<StructLayout(LayoutKind.Sequential)>
Friend Structure 原生视频像素探针
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public X As UInteger
    Public Y As UInteger
    Public 红 As Single
    Public 绿 As Single
    Public 蓝 As Single
    Public Alpha As Single
    Public 视频缩放模式 As UInteger
    Public 输出位深度 As UInteger
    Public 色彩模式 As UInteger
    Public 保留 As UInteger
End Structure

<StructLayout(LayoutKind.Sequential)>
Friend Structure 原生音频峰值
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public 声道数 As UInteger
    Public 输入声道数 As UInteger
    Public 峰值1 As Single
    Public 峰值2 As Single
    Public 峰值3 As Single
    Public 峰值4 As Single
    Public 峰值5 As Single
    Public 峰值6 As Single
    Public 峰值7 As Single
    Public 峰值8 As Single
    <MarshalAs(UnmanagedType.ByValArray, SizeConst:=8)>
    Public 输入峰值 As Single()
End Structure

<Flags>
Friend Enum 原生位图字幕标志 As UInteger
    无 = 0
    清除 = 1
    流结束 = 2
    强制 = 4
    仍需读取 = 8
    未变化 = 16
End Enum

<StructLayout(LayoutKind.Sequential)>
Friend Structure 原生位图字幕帧
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public 标志 As 原生位图字幕标志
    Public 保留 As UInteger
    Public 开始100纳秒 As Long
    Public 结束100纳秒 As Long
    Public 画布宽度 As Integer
    Public 画布高度 As Integer
    Public X As Integer
    Public Y As Integer
    Public 宽度 As Integer
    Public 高度 As Integer
    Public 行跨度 As Integer
    Public 像素字节数 As UInteger
    Public 序号 As Long
End Structure

Friend Enum 原生定时文字命令类型 As UInteger
    文字 = 1
    位图 = 2
End Enum

<Flags>
Friend Enum 原生定时文字标志 As UInteger
    无 = 0
    粗体 = 1
    斜体 = 2
    下划线 = 4
    删除线 = 8
    HDR高亮位图 = 16
    软阴影 = 32
End Enum

Friend Enum 原生定时文字对齐 As UInteger
    靠前 = 0
    居中 = 1
    靠后 = 2
End Enum

Friend Enum 原生定时文字图层槽位 As UInteger
    字幕 = 0
    弹幕 = 1
    播放器信息 = 2
    歌词 = 3
End Enum

<StructLayout(LayoutKind.Sequential)>
Friend Structure 原生定时文字命令
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public 类型 As 原生定时文字命令类型
    Public 标志 As 原生定时文字标志
    Public X As Single
    Public Y As Single
    Public 宽度 As Single
    Public 高度 As Single
    Public 前景色ARGB As UInteger
    Public 描边色ARGB As UInteger
    Public 字号 As Single
    Public 描边宽度 As Single
    Public 水平对齐 As 原生定时文字对齐
    Public 垂直对齐 As 原生定时文字对齐
    Public 文本UTF8 As IntPtr
    Public 字体UTF8 As IntPtr
    Public 位图BGRA As IntPtr
    Public 位图宽度 As UInteger
    Public 位图高度 As UInteger
    Public 位图行跨度 As UInteger
    Public 位图字节数 As UInteger
    Public 内容标识 As ULong
    Public 阴影色ARGB As UInteger
    Public 阴影X偏移 As Single
    Public 阴影Y偏移 As Single
    Public 保留 As UInteger
End Structure

<StructLayout(LayoutKind.Sequential)>
Friend Structure 原生定时文字测量
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public 布局高度 As Single
    Public 可见顶部 As Single
    Public 可见底部 As Single
End Structure

<StructLayout(LayoutKind.Sequential)>
Friend Structure 原生定时文字图层
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public 画布宽度 As UInteger
    Public 画布高度 As UInteger
    Public 命令数 As UInteger
    Public 图层槽位 As 原生定时文字图层槽位
    Public 序号 As ULong
    Public 命令 As IntPtr
    Public 目标帧率 As Single
    Public 保留2 As UInteger
    Public 封面毛玻璃半径 As Single
    Public 封面毛玻璃次数 As UInteger
    Public 封面毛玻璃下采样倍率 As UInteger
    Public 封面毛玻璃遮罩颜色ARGB As UInteger
    Public 封面区域宽度百分比 As Single
    Public 歌词区域宽度百分比 As Single
    Public 封面区域左内边距百分比 As Single
    Public 封面区域垂直内边距百分比 As Single
    Public 封面区域右内边距百分比 As Single
End Structure

<StructLayout(LayoutKind.Sequential)>
Friend Structure 原生定时文字状态
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public 已提交序号 As ULong
    Public 已绘制序号 As ULong
    Public 命令数 As UInteger
    Public 画布宽度 As UInteger
    Public 画布高度 As UInteger
    Public 图层呈现帧数 As UInteger
    Public 可见像素数 As ULong
    Public 精灵缓存命中次数 As ULong
    Public 精灵缓存未命中次数 As ULong
    Public 后备缓冲获取次数 As ULong
    Public 合成像素着色器调用次数 As ULong
End Structure

Friend NotInheritable Class 播放器原生句柄
    Inherits SafeHandleZeroOrMinusOneIsInvalid
    Private 回调句柄 As GCHandle

    Friend Sub New(原生指针 As IntPtr, 持有回调 As GCHandle)
        MyBase.New(True)
        SetHandle(原生指针)
        回调句柄 = 持有回调
    End Sub

    Protected Overrides Function ReleaseHandle() As Boolean
        播放器原生接口.FFF3FP_Destroy(handle)
        If 回调句柄.IsAllocated Then 回调句柄.Free()
        Return True
    End Function
End Class

Friend NotInheritable Class 位图字幕原生句柄
    Inherits SafeHandleZeroOrMinusOneIsInvalid

    Friend Sub New(原生指针 As IntPtr)
        MyBase.New(True)
        SetHandle(原生指针)
    End Sub

    Protected Overrides Function ReleaseHandle() As Boolean
        播放器原生接口.FFF3FP_DestroyBitmapSubtitle(handle)
        Return True
    End Function
End Class

Friend NotInheritable Class ASS字幕原生句柄
    Inherits SafeHandleZeroOrMinusOneIsInvalid

    Friend Sub New(原生指针 As IntPtr)
        MyBase.New(True)
        SetHandle(原生指针)
    End Sub

    Protected Overrides Function ReleaseHandle() As Boolean
        播放器原生接口.FFF3FP_DestroyAssSubtitle(handle)
        Return True
    End Function
End Class

Friend Module 播放器原生接口
    Friend Const 动态库名称 As String = "FFF.Native.dll"

    ' MKS 字幕容器探测：FFF3FP_ProbeSubtitleStreams 是纯新增导出（它的加入不递增版本；
    ' 当前版本 16 来自 FFF3FPConfiguration 的 sdrScRgbMode 字段），
    ' 旧内核没有该入口点，因此不做静态导入，而是运行时解析并缓存；解析失败按
    ' “无探测能力”降级（MKS 不自动加载，但绝不崩）。
    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Private Delegate Function 探测字幕流原型(路径UTF8 As IntPtr, 输出UTF8 As IntPtr,
        输出大小 As UInteger, ByRef 必需大小 As UInteger) As 原生播放器结果

    Private ReadOnly 探测字幕流函数 As 探测字幕流原型 = 解析探测字幕流()

    Private Function 解析探测字幕流() As 探测字幕流原型
        Dim 句柄 = IntPtr.Zero
        Try
            ' TryLoad 成功后不再 Free：进程持有内核库，保证函数指针生命周期。
            If Not NativeLibrary.TryLoad(动态库名称, GetType(播放器原生接口).Assembly, Nothing, 句柄) OrElse
                句柄 = IntPtr.Zero Then Return Nothing
            Dim 地址 = NativeLibrary.GetExport(句柄, "FFF3FP_ProbeSubtitleStreams")
            Return Marshal.GetDelegateForFunctionPointer(Of 探测字幕流原型)(地址)
        Catch ex As EntryPointNotFoundException
            Return Nothing
        Catch ex As Exception
            Return Nothing
        End Try
    End Function

    ''' <summary>当前内核是否提供字幕流探测能力（旧内核返回 False）。</summary>
    Friend ReadOnly Property 支持字幕流探测 As Boolean
        Get
            Return 探测字幕流函数 IsNot Nothing
        End Get
    End Property

    ''' <summary>按路径探测字幕流，返回顶层 JSON；无探测能力或调用失败返回 Nothing。</summary>
    Friend Function 探测字幕流JSON(本地路径 As String) As String
        If String.IsNullOrWhiteSpace(本地路径) Then Return Nothing
        Dim 函数 = 探测字幕流函数
        If 函数 Is Nothing Then Return Nothing
        Dim 路径指针 = IntPtr.Zero
        Dim 输出指针 = IntPtr.Zero
        Try
            路径指针 = Marshal.StringToCoTaskMemUTF8(本地路径)
            Dim 必需大小 As UInteger = 0
            Dim 首次 = 函数(路径指针, IntPtr.Zero, 0UI, 必需大小)
            If 首次 <> 原生播放器结果.成功 AndAlso 首次 <> 原生播放器结果.缓冲区不足 Then Return Nothing
            ' 内核契约保证至少返回终止 NUL（required >= 1）；对违反契约的返回
            ' 直接放弃，避免 AllocCoTaskMem 的未清零内存被当作字符串读越界
            ' （与 播放器会话.读取原生文本 的 required <= 1 守卫同形）。
            If 必需大小 <= 1UI Then Return Nothing
            输出指针 = Marshal.AllocCoTaskMem(CInt(必需大小))
            Dim 结果 = 函数(路径指针, 输出指针, 必需大小, 必需大小)
            If 结果 <> 原生播放器结果.成功 Then Return Nothing
            Return Marshal.PtrToStringUTF8(输出指针)
        Catch ex As Exception
            Return Nothing
        Finally
            If 输出指针 <> IntPtr.Zero Then Marshal.FreeCoTaskMem(输出指针)
            If 路径指针 <> IntPtr.Zero Then Marshal.FreeCoTaskMem(路径指针)
        End Try
    End Function

    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Function 原生授权对话框回调(代码UTF8 As IntPtr, 容量 As UInteger) As Integer

    Friend Const 扩展授权文本用途 As UInteger = 3UI

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_GetApiVersion() As UInteger
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_GetColorExtensionStatusText(statusCode As UInteger, textKind As UInteger) As IntPtr
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Sub FFF3FP_SetColorExtensionAuthorizationPrompt(callback As 原生授权对话框回调)
    End Sub
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_Create(ByRef 配置 As 原生播放器配置, ByRef 播放器 As IntPtr) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_Open(播放器 As 播放器原生句柄, 路径UTF8 As IntPtr) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_Play(播放器 As 播放器原生句柄) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_Pause(播放器 As 播放器原生句柄) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_DiscardAudioOutput(播放器 As 播放器原生句柄) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_Stop(播放器 As 播放器原生句柄) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_Close(播放器 As 播放器原生句柄) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_Seek(播放器 As 播放器原生句柄, 位置100纳秒 As Long) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SeekKeyframe(播放器 As 播放器原生句柄, 位置100纳秒 As Long) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SeekFrame(播放器 As 播放器原生句柄, 帧序号 As Long) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_StepFrame(播放器 As 播放器原生句柄, 方向 As Integer) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_StepKeyframe(播放器 As 播放器原生句柄, 方向 As Integer) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SelectVideoStream(播放器 As 播放器原生句柄, 流索引 As Integer) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SelectAudioStream(播放器 As 播放器原生句柄, 流索引 As Integer) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_LoadExternalAudio(播放器 As 播放器原生句柄, 路径UTF8 As IntPtr, 流索引 As Integer, 偏移100纳秒 As Long) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_ClearExternalAudio(播放器 As 播放器原生句柄) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SetExternalAudioOffset(播放器 As 播放器原生句柄, 偏移100纳秒 As Long) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SetColorMode(播放器 As 播放器原生句柄, 模式 As UInteger, SDR峰值 As Single, HDR峰值 As Single, SDR纸白 As Single, 强制HDR输出 As UInteger) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SetOutputWindow(播放器 As 播放器原生句柄, 窗口 As IntPtr) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SetInteractiveMove(播放器 As 播放器原生句柄, 启用 As UInteger) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_Set360View(播放器 As 播放器原生句柄, 启用 As UInteger,
                                      水平角度 As Single, 垂直角度 As Single,
                                      视场角 As Single) As 原生播放器结果
    End Function
    ' 截图帧回读：离屏渲染，不受遮挡/最小化影响，且能带 HDR/广色域的高精度帧。
    '
    ' ⚠ 同样必须动态解析（理由与下面的视图旋转一致）：这是"同版本号（16）内的新增导出"，
    '   静态导入会让未含该导出的内核抛 EntryPointNotFoundException。
    ' 两次调用契约：先传 像素=空 问尺寸（内核回"缓冲区不足"属正常），再按
    '   宽×高×每像素字节 分配后二调。布局固定取"源分辨率"，格式由请求的位深决定。
    Private Delegate Function 复制帧原型(播放器 As 播放器原生句柄, 像素 As IntPtr,
        容量 As UInteger, ByRef 宽 As UInteger, ByRef 高 As UInteger,
        布局 As UInteger, 格式 As UInteger) As 原生播放器结果

    Private Delegate Function 取末次帧位深原型(播放器 As 播放器原生句柄,
        ByRef 位深 As UInteger) As 原生播放器结果

    Private ReadOnly 复制帧函数 As 复制帧原型 = 解析帧回读导出(Of 复制帧原型)("FFF3FP_CopyFrame")
    Private ReadOnly 取末次帧位深函数 As 取末次帧位深原型 =
        解析帧回读导出(Of 取末次帧位深原型)("FFF3FP_GetLastCopyFrameBitDepth")

    Private Function 解析帧回读导出(Of T)(名称 As String) As T
        Dim 句柄 = IntPtr.Zero
        Try
            ' TryLoad 成功后不再 Free：进程持有内核库，保证函数指针生命周期。
            If Not NativeLibrary.TryLoad(动态库名称, GetType(播放器原生接口).Assembly, Nothing, 句柄) OrElse
                句柄 = IntPtr.Zero Then Return Nothing
            Dim 地址 = NativeLibrary.GetExport(句柄, 名称)
            Return Marshal.GetDelegateForFunctionPointer(Of T)(地址)
        Catch ex As EntryPointNotFoundException
            Return Nothing
        Catch ex As Exception
            Return Nothing
        End Try
    End Function

    ''' <summary>当前内核是否支持离屏帧回读（旧内核返回 False）。</summary>
    Friend ReadOnly Property 支持帧回读 As Boolean
        Get
            Return 复制帧函数 IsNot Nothing
        End Get
    End Property

    ''' <summary>问一次源分辨率帧尺寸（不取像素）。旧内核或无画面时返回 False。</summary>
    Friend Function 取帧尺寸(播放器 As 播放器原生句柄, ByRef 宽 As UInteger,
                             ByRef 高 As UInteger) As Boolean
        Dim 函数 = 复制帧函数
        If 函数 Is Nothing Then Return False
        Try
            ' 契约：像素为空指针时只回尺寸并返回"缓冲区不足"，这是正常路径不是错误。
            Dim 结果 = 函数(播放器, IntPtr.Zero, 0UI, 宽, 高, 0UI, 0UI)
            Return (结果 = 原生播放器结果.缓冲区不足 OrElse 结果 = 原生播放器结果.成功) AndAlso
                宽 > 0UI AndAlso 高 > 0UI
        Catch ex As Exception
            Return False
        End Try
    End Function

    ''' <summary>把源分辨率的一帧读进调用方缓冲。容量按字节计；16 位时每像素 8 字节。</summary>
    Friend Function 复制帧(播放器 As 播放器原生句柄, 缓冲 As IntPtr, 容量 As UInteger,
                           ByRef 宽 As UInteger, ByRef 高 As UInteger,
                           ByRef 位深 As UInteger) As Boolean
        Dim 函数 = 复制帧函数
        If 函数 Is Nothing Then Return False
        Try
            Dim 格式 = If(位深 = 16UI, 1UI, 0UI)
            Dim 结果 = 函数(播放器, 缓冲, 容量, 宽, 高, 0UI, 格式)
            If 结果 <> 原生播放器结果.成功 Then Return False
            Dim 实际位深 As UInteger = 0UI
            If 取末次帧位深函数 IsNot Nothing AndAlso
               取末次帧位深函数(播放器, 实际位深) = 原生播放器结果.成功 AndAlso 实际位深 > 0UI Then
                位深 = 实际位深
            End If
            Return True
        Catch ex As Exception
            Return False
        End Try
    End Function

    ' 视图旋转（四分一转，顺时针 0..3）。
    '
    ' ⚠ 必须动态解析，不能像 FFF3FP_SetFitLimitToNative 那样静态导入：
    '   旋转是"同版本号内的新增导出"，而构造函数只校验版本号（仍是 16）。
    '   若静态导入，一个 16 版但**尚未含旋转**的内核会在首次调用时抛
    '   EntryPointNotFoundException（甚至加载期失败），把"旧内核无此能力"
    '   变成"宿主崩溃"。动态解析让它退化为"旋转不可用"，与字幕流探测同款处理。
    Private Delegate Function 设置视图旋转原型(播放器 As 播放器原生句柄,
        四分一转 As UInteger) As 原生播放器结果

    Private ReadOnly 设置视图旋转函数 As 设置视图旋转原型 = 解析视图旋转()

    Private Function 解析视图旋转() As 设置视图旋转原型
        Dim 句柄 = IntPtr.Zero
        Try
            ' TryLoad 成功后不再 Free：进程持有内核库，保证函数指针生命周期。
            If Not NativeLibrary.TryLoad(动态库名称, GetType(播放器原生接口).Assembly, Nothing, 句柄) OrElse
                句柄 = IntPtr.Zero Then Return Nothing
            Dim 地址 = NativeLibrary.GetExport(句柄, "FFF3FP_SetViewRotation")
            Return Marshal.GetDelegateForFunctionPointer(Of 设置视图旋转原型)(地址)
        Catch ex As EntryPointNotFoundException
            Return Nothing
        Catch ex As Exception
            Return Nothing
        End Try
    End Function

    ''' <summary>当前内核是否支持视图旋转（旧内核返回 False）。</summary>
    Friend ReadOnly Property 支持视图旋转 As Boolean
        Get
            Return 设置视图旋转函数 IsNot Nothing
        End Get
    End Property

    ''' <summary>设置视图旋转（四分一转，顺时针 0..3）。内核不支持时返回 False。
    ''' 失败原因不在此层抛异常：与 探测字幕流JSON 同款——本模块只负责转发与
    ''' 能力探测，错误语义由 播放器会话 决定。</summary>
    Friend Function 设置视图旋转(播放器 As 播放器原生句柄, 四分一转 As UInteger) As Boolean
        Dim 函数 = 设置视图旋转函数
        If 函数 Is Nothing Then Return False
        Try
            Return 函数(播放器, 四分一转) = 原生播放器结果.成功
        Catch ex As Exception
            Return False
        End Try
    End Function

    ' 图片模式：缩放 + 平移。zoom=1 为适应窗口，pan 为相对未缩放画面的归一化偏移 [-1,1]。
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SetViewTransform(播放器 As 播放器原生句柄,
                                            缩放 As Single, 水平平移 As Single,
                                            垂直平移 As Single) As 原生播放器结果
    End Function
    ' 光标锚定缩放：当前缩放 × 倍数，并保持锚点下的画面内容不动。
    ' 锚点按客户区归一化到 [0,1]，宿主直接传鼠标位置比例。
    ' 属**追加导出**：更早的内核没有该符号。这里刻意**不做静态导入** —— 静态导入会在
    ' 载入旧内核时直接抛 EntryPointNotFoundException，连视频播放都起不来。改为声明一个
    ' 委托并按需解析（见 取光标锚定缩放委托），缺失时返回 Nothing 由调用方回退。
    Friend Delegate Function 光标锚定缩放委托(播放器 As 播放器原生句柄,
                                              倍数 As Single, 锚点水平 As Single,
                                              锚点垂直 As Single,
                                              结果缩放指针 As IntPtr) As 原生播放器结果

    ' Module 的成员隐式 Shared，再写 Shared 会被拒（BC30593 / BC30433）。
    Private 光标锚定缩放缓存 As 光标锚定缩放委托
    Private 光标锚定缩放已查询 As Boolean

    ''' <summary>解析 FFF3FP_ZoomViewAt；旧内核没有该导出时返回 Nothing。</summary>
    Friend Function 取光标锚定缩放委托() As 光标锚定缩放委托
        If 光标锚定缩放已查询 Then Return 光标锚定缩放缓存
        光标锚定缩放已查询 = True
        Try
            Dim 地址 = NativeLibrary.GetExport(NativeLibrary.Load(动态库名称), "FFF3FP_ZoomViewAt")
            光标锚定缩放缓存 = Marshal.GetDelegateForFunctionPointer(Of 光标锚定缩放委托)(地址)
        Catch ex As Exception
            光标锚定缩放缓存 = Nothing
        End Try
        Return 光标锚定缩放缓存
    End Function
    ' 视口封顶：非 0 时适配盒不超过源原生尺寸，于是"缩放"变成绝对的 屏幕:视频 像素比
    ' （缩放=1 即逐像素 1:1，窗口比源大时四周留黑边）。0 = 沿用"铺满窗口"的历史行为。
    ' 静态导入即可：构造函数已硬拒 API 版本 ≠ 16，能建会话的内核必然带这个导出。
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SetFitLimitToNative(播放器 As 播放器原生句柄,
                                               开启 As UInteger) As 原生播放器结果
    End Function
    ' 图片信息（帧数/动画/EXIF 旋转/格式位深/ICC/alpha/色彩原色）。调用前必须填好 大小 与 版本。
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_GetImageInfo(播放器 As 播放器原生句柄,
                                        ByRef 信息 As 原生图片信息) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SetAudioEndpoint(播放器 As 播放器原生句柄, 端点UTF8 As IntPtr) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SetAudioExclusiveMode(播放器 As 播放器原生句柄, 独占 As UInteger) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SetVolume(播放器 As 播放器原生句柄, 音量 As Single, 静音 As UInteger) As 原生播放器结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl)>
    Friend Function FFF3FP_SetTimedTextLayer(播放器 As 播放器原生句柄, ByRef 图层 As 原生定时文字图层) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_GetSnapshot(播放器 As 播放器原生句柄, ByRef 快照 As 原生播放器快照) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_ReadVideoPixel(播放器 As 播放器原生句柄, ByRef 探针 As 原生视频像素探针) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_GetAudioPeakLevels(播放器 As 播放器原生句柄, ByRef 峰值 As 原生音频峰值) As 原生播放器结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl)>
    Friend Function FFF3FP_GetTimedTextStatus(播放器 As 播放器原生句柄, ByRef 状态 As 原生定时文字状态) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl)>
    Friend Function FFF3FP_GetDanmakuStatus(播放器 As 播放器原生句柄, ByRef 状态 As 原生定时文字状态) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl)>
    Friend Function FFF3FP_GetLyricsStatus(播放器 As 播放器原生句柄, ByRef 状态 As 原生定时文字状态) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_MeasureTimedText(
        <MarshalAs(UnmanagedType.LPUTF8Str)> 文本UTF8 As String,
        <MarshalAs(UnmanagedType.LPUTF8Str)> 字体UTF8 As String,
        字号 As Single, 标志 As 原生定时文字标志, 最大宽度 As Single,
        描边宽度 As Single, 阴影X偏移 As Single, 阴影Y偏移 As Single,
        阴影有效 As UInteger, ByRef 测量 As 原生定时文字测量) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_MeasureTimedTextWidth(
        <MarshalAs(UnmanagedType.LPUTF8Str)> 文本UTF8 As String,
        <MarshalAs(UnmanagedType.LPUTF8Str)> 字体UTF8 As String,
        字号 As Single, 标志 As 原生定时文字标志, ByRef 宽度 As Single) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_GetMediaInfo(播放器 As 播放器原生句柄, 输出UTF8 As IntPtr, 输出大小 As UInteger, ByRef 所需大小 As UInteger) As 原生播放器结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_GetDiscStatus(播放器 As 播放器原生句柄, 输出UTF8 As IntPtr, 输出大小 As UInteger, ByRef 所需大小 As UInteger) As 原生播放器结果
    End Function

    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_DiscNavigate(播放器 As 播放器原生句柄, 命令 As Integer, 参数 As Integer, Y As Integer) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_CopySdrFrame(播放器 As 播放器原生句柄, 像素 As IntPtr, 容量 As UInteger, ByRef 宽 As UInteger, ByRef 高 As UInteger, 仅光盘层 As UInteger) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_GetLastError(播放器 As 播放器原生句柄, 输出UTF8 As IntPtr, 输出大小 As UInteger, ByRef 所需大小 As UInteger) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Sub FFF3FP_Destroy(播放器 As IntPtr)
    End Sub
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_OpenBitmapSubtitle(路径UTF8 As IntPtr, 流索引 As Integer, ByRef 解码器 As IntPtr) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_ReadBitmapSubtitle(解码器 As 位图字幕原生句柄, ByRef 帧 As 原生位图字幕帧) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_CopyBitmapSubtitlePixels(解码器 As 位图字幕原生句柄, 输出 As IntPtr, 输出大小 As UInteger) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_SeekBitmapSubtitle(解码器 As 位图字幕原生句柄, 位置100纳秒 As Long) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_GetBitmapSubtitleLastError(解码器 As 位图字幕原生句柄, 输出UTF8 As IntPtr, 输出大小 As UInteger, ByRef 所需大小 As UInteger) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Sub FFF3FP_DestroyBitmapSubtitle(解码器 As IntPtr)
    End Sub
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_OpenAssSubtitle(路径UTF8 As IntPtr, 字体目录UTF8 As IntPtr,
                                            流索引 As Integer, ByRef 渲染器 As IntPtr) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_RenderAssSubtitle(渲染器 As ASS字幕原生句柄, 位置100纳秒 As Long,
                                             画布宽度 As Integer, 画布高度 As Integer,
                                             ByRef 帧 As 原生位图字幕帧) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_CopyAssSubtitlePixels(渲染器 As ASS字幕原生句柄, 输出 As IntPtr,
                                                 输出大小 As UInteger) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Function FFF3FP_GetAssSubtitleLastError(渲染器 As ASS字幕原生句柄, 输出UTF8 As IntPtr,
                                                   输出大小 As UInteger, ByRef 所需大小 As UInteger) As 原生播放器结果
    End Function
    <DllImport(动态库名称, CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Friend Sub FFF3FP_DestroyAssSubtitle(渲染器 As IntPtr)
    End Sub
End Module
