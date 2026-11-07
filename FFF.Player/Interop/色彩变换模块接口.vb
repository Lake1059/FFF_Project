Imports System.Runtime.InteropServices

' ═══════════════════════════════════════════════════════════════════════════
'  色彩变换模块接口 —— 外部色彩变换模块（FFF.HDRdp.Test.dll）的 ABI 镜像与装载入口
'
'  ● 真源：与本模块同水的那份 C ABI 契约头（库外独立仓，不随本仓发布）。
'    形状 = 版本门 + 函数表 + 一小把始终可达的授权面裸导出；没有 C++ 对象、
'    没有跨边界保留指针、没有分配器所有权——输入借用一次调用，输出写调用方缓冲区。
'
'  ● 为什么宿主自己装载（而不是走内核的 ColorExtension 导出）：
'    内核那四个导出是永久惰性桩（PlayerApi.cpp:51-70：status 恒 0、statusText 恒
'    nullptr、prompt setter 丢弃、Authenticate 恒 NotSupported）。授权面在模块自己
'    身上，只能由宿主按 exe 目录装载后直接绑它的裸导出。
'    装载目录口径：绝不指望 .NET 单文件解压缓存目录——已实测（2026-10-17 五臂）没有任何
'    代码去那里找模块；候选序 = FC_COLORTRANSFORM_DIR 覆盖 → 应用目录 → 裸文件名。
'
'  ● 静默降级是既定口径：模块不在盘上 / 装载失败 / 不含授权层 ⇒ 与"没这功能"同形，
'    不提示、不报错、不改默认行为。
'
'  ● 商标合规：本文件（及整个 3FP 本体）只出现中性名 FFF.HDRdp.Test.dll 与中性词汇，
'    不含任何专有算法、常量或文案——专有内容全部封装在模块二进制内部。
'    发布护栏（FFF.Player.vbproj 的“禁止私有模块发布”目标 + 发布脚本同款检查）以某个
'    历史文件名作为匹配键，那是发布链路的既有资产：不得改名，也不在本文件复述全名。
'    ⇒ 宿主源码一律用中性名；需要引用护栏时给出 vbproj 目标名，不抄文件名。
' ═══════════════════════════════════════════════════════════════════════════

Friend Module 色彩变换模块接口

    ' ---- ABI 常量（与模块契约头同水；头改数值时这里必须同步改） ----

    ''' <summary>盘上的模块文件名。缺席是常态而非错误。</summary>
    Friend Const 模块文件名 As String = "FFF.HDRdp.Test.dll"

    ''' <summary>能力表入口（握手用）。未授权时它拒交函数表。</summary>
    Friend Const 能力表入口名 As String = "FFF_GetColorTransformApi"

    ''' <summary>授权状态裸导出。握手被拒后仍要可达，否则分不清"模块不在"与"锁着"。</summary>
    Friend Const 授权状态入口名 As String = "FFF_ColorTransformAuthorizationStatus"

    ''' <summary>状态文本裸导出（解锁框的文案来源）。</summary>
    Friend Const 状态文本入口名 As String = "FFF_ColorTransformStatusText"

    ''' <summary>解锁码提交裸导出。</summary>
    Friend Const 解锁入口名 As String = "FFF_ColorTransformAuthenticate"

    ''' <summary>分类文本裸导出：调用线程上最近一次 分析帧 的分类结果（UTF-8）。
    ''' 旧模块没有它 ⇒ 解析不到即答空串，静默降级。</summary>
    Friend Const 内容文本入口名 As String = "FFF_ColorTransformContentText"

    ''' <summary>最近帧峰值裸导出：调用线程上最近一次 分析帧 的 DV L1 逐帧峰值（尼特）。
    ''' 旧模块没有它 ⇒ 解析不到即答 0（= 无），静默降级。</summary>
    Friend Const 峰值入口名 As String = "FFF_ColorTransformLastFramePeakNits"

    ''' <summary>Authenticate 的三态返回（与模块 AuthenticateResult 同水）。</summary>
    Friend Const 解锁被拒 As Integer = 0
    Friend Const 解锁已落盘 As Integer = 1
    Friend Const 解锁未落盘 As Integer = 2

    ''' <summary>状态文本用途码：1=未授权解释，2=对话框标签，3=票据存不下。</summary>
    Friend Const 文本用途未授权解释 As UInteger = 1UI
    Friend Const 文本用途对话框标签 As UInteger = 2UI
    Friend Const 文本用途票据存不下 As UInteger = 3UI

    ''' <summary>函数表版本门。2 = 握手新增"未授权即拒"（表形状未变）。
    ''' 停在 1 会被模块拒，于是整链按"模块缺席"静默降级——症状与不装模块一模一样。</summary>
    Friend Const 表版本 As UInteger = 3UI

    ''' <summary>外部床渲染函数表的导出名（本模块的第二个能力面，中性词汇）。
    ''' 未授权时模块拒交这张表 ⇒ 内核继续走它自己的矩阵折叠，症状与"模块不在盘上"同形。</summary>
    Friend Const 音频能力表入口名 As String = "FFF_GetAudioRenderApi"

    ''' <summary>音频源码直通函数表的导出名（第三张能力面）：帧解析 + IEC 61937 打包 +
    ''' 格式语义仲裁全在模块，内核只做字节搬运与设备协商。旧模块没有这个导出 ⇒
    ''' 宿主不钉表，内核整体回退解码路径——与"模块不在盘上"同形静默。</summary>
    Friend Const 音频直通表入口名 As String = "FFF_GetAudioPassthroughApi"

    ''' <summary>音频直通函数表版本门（FFF3FP_AUDIO_PASSTHROUGH_VERSION）。</summary>
    Friend Const 音频直通表版本 As UInteger = 1UI

    ''' <summary>音频直通函数表字节数 = sizeof(FFF3FPAudioPassthroughApi)：
    ''' 两枚 UInteger + 五枚指针（上下文/描述/打包/复位/释放，x64）= 48。
    ''' 装载点拿 镜像尺寸 当场对撞，不等就不把表交给内核（与 :90-91 的床表护栏同型）。</summary>
    Friend Const 音频直通表大小 As UInteger = 48UI

    ''' <summary>可选合成面的导出名：只交逐帧参数，不交任何像素代码。
    ''' 旧模块没有这个导出 ⇒ 宿主不请求合成，整条通路与今天逐位一致。</summary>
    Friend Const 合成能力表入口名 As String = "FFF_GetElCompositionApi"

    ''' <summary>可选合成面的版本门（模块 FFFElCompositionVersion）。</summary>
    Friend Const 合成表版本 As UInteger = 1UI

    ''' <summary>音频函数表版本门，与内核 FFF3FP_AUDIO_PLUG_VERSION 同水；数值漂移即握手失败。</summary>
    Friend Const 音频表版本 As UInteger = 1UI

    ''' <summary>可选自驱动注入面的导出名：逐帧注入泵（泵线程、游标、回退探测全部在
    ''' 模块内部，宿主只交一次内核会话句柄）。旧模块没有这个导出 ⇒ 宿主不启动注入，
    ''' 与"未解锁"同形静默。</summary>
    Friend Const 注入面入口名 As String = "FFF_GetInjectionApi"

    ''' <summary>可选注入面的版本门（模块 FFFInjectionVersion）。</summary>
    Friend Const 注入面版本 As UInteger = 1UI

    ''' <summary>音频函数表字节数 = sizeof(FFF3FPAudioRenderPlug)：两枚 UInteger + 六枚指针（x64）。
    ''' 装载点会拿 音频渲染能力表 的 Marshal.SizeOf 当场对撞，不等就不把表交给内核。</summary>
    Friend Const 音频表大小 As UInteger = 56UI

    ''' <summary>本模块产出的载荷 kind（内核 ExternalColorTransform）。</summary>
    Friend Const 载荷种类 As UInteger = 3UI

    ''' <summary>载荷种类版本。</summary>
    Friend Const 载荷种类版本 As UInteger = 1UI

    ''' <summary>一次调用可能产出的最大字节数（= sizeof(FFF3FPColorTransformPayload)）。</summary>
    Friend Const 载荷容量 As UInteger = 431296UI

    ''' <summary>载荷输入结构扩展布局字节数（配置记录通道 + EL 合同字）。</summary>
    Friend Const 输入结构大小 As UInteger = 56UI

    ''' <summary>输入标志：宿主已把增强层残差合成进基底层采样。
    ''' <para>置位条件只有一个：<see cref="色彩变换模块实例.分析增强层"/> 对**同一帧**返回了
    ''' <see cref="原生合成参数.是否启用"/> 非零，宿主随即将同一组参数交给内核
    ''' （FFF3FP_SetAuxiliaryComposition）；内核在没能配到辅帧的那一帧会**扣下** kind=3 表，
    ''' 于是置位永远不会让表落在未合成的画面上。旧模块/旧内核没有这条通路 ⇒ 恒 0。</para>
    ''' 48 B 旧布局没有这个字段，模块按 0 处理；56 B 布局里模块只认这一位，
    ''' 其余位或非零保留字会被拒（<see cref="结果参数无效"/>）。</summary>
    Friend Const 输入标志增强层已预合成 As UInteger = 1UI

    ''' <summary>analyzeFrame 返回码（FFFColorTransformResult）。</summary>
    Friend Const 结果成功 As Integer = 0
    Friend Const 结果无元数据 As Integer = 1
    Friend Const 结果参数无效 As Integer = 2
    Friend Const 结果缓冲不足 As Integer = 3
    Friend Const 结果不支持 As Integer = 4
    ''' <summary>版本 2 新增：表已在手但授权中途失效 ⇒ 停止注入，回落基底。</summary>
    Friend Const 结果未授权 As Integer = 5

    ''' <summary>模块在场形态。探测只回答"要不要打扰用户"。</summary>
    Public Enum 模块在场形态
        ''' <summary>盘上没有、或装载/解析失败 ⇒ 静默，绝不提示。</summary>
        缺席 = 0
        ''' <summary>能装载但不含授权层（早于 ABI 版本 2 的旧件）⇒ 按缺席处理。</summary>
        旧件 = 1
        ''' <summary>装载成功、授权层可达、AuthorizationStatus()=1 ⇒ 唯一需要提示的态。</summary>
        已锁定 = 2
        ''' <summary>AuthorizationStatus()=2 ⇒ 已解锁，直接走装载。</summary>
        就绪 = 3
    End Enum

    ''' <summary>解锁码送进模块后的结果（对应模块 AuthenticateResult 加一层"够不到"）。</summary>
    Public Enum 解锁结果
        ''' <summary>没能调用到模块 ⇒ 与"缺席"同义，不提示。</summary>
        够不到 = 0
        ''' <summary>码不对（模块返回 0）：可重试。</summary>
        被拒 = 1
        ''' <summary>通过，且票据已落盘（模块返回 1）。</summary>
        已接受 = 2
        ''' <summary>通过，但票据写不进目标目录（模块返回 2）⇒ 仅本次进程有效。</summary>
        已接受未落盘 = 3
    End Enum

    ' ---- kernel32 装载原语：只负责"按文件名找到并映射"，失败一律返回 0，不抛 ----

    Private Const 改变搜索路径旗标 As UInteger = &H8UI   ' LOAD_WITH_ALTERED_SEARCH_PATH

    <DllImport("kernel32", SetLastError:=True, CharSet:=CharSet.Unicode, ExactSpelling:=True)>
    Private Function LoadLibraryExW(路径 As String, 保留 As IntPtr, 旗标 As UInteger) As IntPtr
    End Function

    <DllImport("kernel32", SetLastError:=True, CharSet:=CharSet.Unicode, ExactSpelling:=True)>
    Private Function LoadLibraryW(路径 As String) As IntPtr
    End Function

    <DllImport("kernel32", SetLastError:=True, CharSet:=CharSet.Ansi, ExactSpelling:=True)>
    Private Function GetProcAddress(模块 As IntPtr, 名称 As String) As IntPtr
    End Function

    <DllImport("kernel32", SetLastError:=True, ExactSpelling:=True)>
    Friend Function FreeLibrary(模块 As IntPtr) As Boolean
    End Function

    ''' <summary>映射模块。失败返回 IntPtr.Zero（不抛）。
    ''' 带目录分隔符的走 LoadLibraryExW + 改变搜索路径旗标（让模块自己的依赖先在它所在
    ''' 目录解析）；裸文件名走普通 LoadLibraryW——对该形态带旗标是未定义行为。</summary>
    Private Function 装载候选(名称或路径 As String) As IntPtr
        If String.IsNullOrEmpty(名称或路径) Then Return IntPtr.Zero
        Try
            If 名称或路径.Contains("/"c) OrElse 名称或路径.Contains("\"c) Then
                Return LoadLibraryExW(名称或路径, IntPtr.Zero, 改变搜索路径旗标)
            End If
            Return LoadLibraryW(名称或路径)
        Catch ex As BadImageFormatException
            Return IntPtr.Zero
        Catch ex As DllNotFoundException
            Return IntPtr.Zero
        End Try
    End Function

    ''' <summary>取导出函数地址。失败返回 IntPtr.Zero（不抛）。</summary>
    Private Function 解析导出(模块 As IntPtr, 名称 As String) As IntPtr
        If 模块 = IntPtr.Zero Then Return IntPtr.Zero
        Try
            Return GetProcAddress(模块, 名称)
        Catch ex As EntryPointNotFoundException
            Return IntPtr.Zero
        End Try
    End Function

    Private Sub 释放模块(模块 As IntPtr)
        If 模块 = IntPtr.Zero Then Return
        Try
            FreeLibrary(模块)
        Catch ex As DllNotFoundException
            ' 连 kernel32 都解析不了：无从释放，静默。
        End Try
    End Sub

    ' ---- 委托与结构（全部 cdecl；形参是指针/blittable，不给封送留歧义） ----

    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Function 取能力表原型(版本 As UInteger, 表地址 As IntPtr) As Integer

    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Function 授权状态原型() As UInteger

    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Function 状态文本原型(用途 As UInteger) As IntPtr

    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Function 解锁原型(代码UTF8 As IntPtr) As Integer

    ''' <summary>分类文本裸导出（无参数，答 UTF-8 常量串；无分类时空串，非空指针）。
    ''' ⚠ 返回指针只在**调用线程**上、直到该线程下次 分析帧 前有效 ⇒ 调用方必须立即拷贝。</summary>
    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Function 内容文本原型() As IntPtr

    ''' <summary>最近帧峰值裸导出（无参数，答尼特值；0 = 无）。
    ''' 与分类文本同一线程契约：只在刚调用过 分析帧 的线程上读。</summary>
    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Function 峰值原型() As Single

    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Function 变换版本原型() As UInteger

    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Function 分析帧原型(输入地址 As IntPtr, 输出地址 As IntPtr) As Integer

    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Sub 复位原型()

    ''' <summary>能力表：size + version + 函数指针（32 B v2 布局；40 B v3 尾加
    ''' 设置显示目标 列）。旧模块 32 B 表 + 新宿主 40 B 镜像 ⇒ 绑定处按 size 归一，
    ''' 缺列读作零指针 = 通道缺席（bake 回落经典 100-nit SDR 目标）。</summary>
    <StructLayout(LayoutKind.Sequential)>
    Friend Structure 色彩变换能力表
        Public 大小 As UInteger
        Public 版本 As UInteger
        Public 变换版本 As IntPtr
        Public 分析帧 As IntPtr
        Public 复位 As IntPtr
        Public 设置显示目标 As IntPtr
    End Structure

    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Sub 设置显示目标原型(尼特 As Single)

    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Function 取音频表原型(版本 As UInteger, 表地址 As IntPtr) As Integer

    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Function 取直通表原型(版本 As UInteger, 表地址 As IntPtr) As Integer

    ''' <summary>音频直通函数表的宿主镜像（48 B，全 blittable，与
    ''' FFF3FPAudioPassthroughApi 同布局同序）。与床表同一纪律：宿主只把整张表
    ''' 钉住转交给内核，自己不逐枚调用任何一列，因此不为它们声明委托原型——
    ''' 唯一的例外是 释放（内核拒收时归还模块上下文用）。</summary>
    <StructLayout(LayoutKind.Sequential)>
    Friend Structure 音频直通能力表
        Public 大小 As UInteger
        Public 版本 As UInteger
        Public 上下文 As IntPtr
        Public 描述 As IntPtr
        Public 打包 As IntPtr
        Public 复位 As IntPtr
        Public 释放 As IntPtr
    End Structure

    ''' <summary>外部床渲染函数表的宿主镜像（56 B，全 blittable）。字段次序就是 ABI：
    ''' 宿主只把整张表转交给内核（内核在装载点拷贝一份，所以调用期间钉住即可），
    ''' 自己不逐枚调用任何一列，因此本文件不需要为它们声明委托原型。</summary>
    <StructLayout(LayoutKind.Sequential)>
    Friend Structure 音频渲染能力表
        Public 大小 As UInteger
        Public 版本 As UInteger
        Public 上下文 As IntPtr
        Public 名称 As IntPtr
        Public 支持 As IntPtr
        Public 渲染 As IntPtr
        Public 复位 As IntPtr
        Public 释放 As IntPtr
    End Structure

    ''' <summary>一帧的输入（56 B 扩展布局：头 + 两个借用通道 + 演示帧号 + 合同字）。
    ''' 帧号模块不读也不抄进载荷——"取最后一条 ≤ 当前帧"的匹配是宿主的职责。
    ''' 48 B 旧模块会忽略末尾的 <see cref="标志"/>/<see cref="保留"/>；当前模块只认
    ''' <see cref="输入标志增强层已预合成"/>，由泵按合成面的同帧分析结果置位。</summary>
    <StructLayout(LayoutKind.Sequential)>
    Friend Structure 色彩变换输入
        Public 大小 As UInteger
        Public 版本 As UInteger
        Public 元数据 As IntPtr
        Public 元数据大小 As ULong
        Public 帧序号 As ULong
        Public 配置记录 As IntPtr
        Public 配置记录大小 As ULong
        Public 标志 As UInteger
        Public 保留 As UInteger
    End Structure

    ''' <summary>一次分析的结果（32 B）。载荷写调用方拥有的缓冲区。</summary>
    <StructLayout(LayoutKind.Sequential)>
    Friend Structure 色彩变换输出
        Public 大小 As UInteger
        Public 载荷种类 As UInteger
        Public 载荷种类版本 As UInteger
        Public 载荷大小 As UInteger
        Public 载荷 As IntPtr
        Public 载荷容量 As UInteger
        Public 保留 As UInteger
    End Structure

    ''' <summary>可选合成面的逐帧参数（32 B，与模块 FFFElCompositionParams 同宽同序）。
    ''' 宿主只把它原样转交给内核 FFF3FP_SetAuxiliaryComposition，自己不解释任何字段。</summary>
    <StructLayout(LayoutKind.Sequential)>
    Friend Structure 原生合成参数
        Public 是否启用 As UInteger
        Public 是否禁用残差 As UInteger
        Public 基底层位深 As UInteger
        Public 增强层位深 As UInteger
        Public 视频位深 As UInteger
        Public 残差中心 As UInteger
        Public 残差移位 As Integer
        Public 最大值 As UInteger
    End Structure

    ''' <summary>合成面输入（40 B）：与色彩变换输入同一份借用通道，没有标志字。</summary>
    <StructLayout(LayoutKind.Sequential)>
    Friend Structure 原生合成输入
        Public 大小 As UInteger
        Public 版本 As UInteger
        Public 元数据 As IntPtr
        Public 元数据大小 As ULong
        Public 配置记录 As IntPtr
        Public 配置记录大小 As ULong
    End Structure

    ''' <summary>合成面输出（40 B）：头 + 参数，没有缓冲区。</summary>
    <StructLayout(LayoutKind.Sequential)>
    Friend Structure 原生合成输出
        Public 大小 As UInteger
        Public 保留 As UInteger
        Public 参数 As 原生合成参数
    End Structure

    ''' <summary>合成面函数表（16 B，与模块 FFFElCompositionApi 同布局）。</summary>
    <StructLayout(LayoutKind.Sequential)>
    Friend Structure 合成能力表
        Public 大小 As UInteger
        Public 版本 As UInteger
        Public 分析 As IntPtr
    End Structure

    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Function 取合成表原型(版本 As UInteger, 表地址 As IntPtr) As Integer

    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Function 合成分析原型(输入地址 As IntPtr, 输出地址 As IntPtr) As Integer

    ''' <summary>FFFInjectionStatus 镜像（184 B）：注入泵自己的连贯快照——计数、峰值、
    ''' 两段文本由泵线程一次写好，宿主读到的是同一拍的值。文本是 UTF-8 NUL 结尾、
    ''' 模块自己的措辞，宿主原样显示、不解释；泵未跑时为空。</summary>
    <StructLayout(LayoutKind.Sequential)>
    Friend Structure 注入状态
        Public 大小 As UInteger
        Public 运行 As UInteger
        Public 注入帧数 As ULong
        Public 未变帧数 As ULong
        Public 跳过帧数 As ULong
        Public 无据帧数 As ULong
        Public 末帧帧号 As Long
        Public 末帧峰值尼特 As Single
        <MarshalAs(UnmanagedType.ByValArray, SizeConst:=64)> Public 内容文本字节 As Byte()
        <MarshalAs(UnmanagedType.ByValArray, SizeConst:=64)> Public 合成文本字节 As Byte()
    End Structure

    ''' <summary>FFFInjectionApi 镜像：启动 / 停止 / 读快照。内核会话句柄对宿主与模块都不透明。</summary>
    <StructLayout(LayoutKind.Sequential)>
    Friend Structure 注入能力表
        Public 大小 As UInteger
        Public 版本 As UInteger
        Public 启动 As IntPtr
        Public 停止 As IntPtr
        Public 读状态 As IntPtr
    End Structure

    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Function 取注入表原型(版本 As UInteger, 表地址 As IntPtr) As Integer

    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Function 注入启动原型(内核句柄 As IntPtr) As Integer

    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Sub 注入停止原型()

    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Sub 注入读状态原型(ByRef 状态 As 注入状态)

    ''' <summary>把状态里一段 NUL 结尾的 UTF-8 文本拷成托管串（空段/缺省答空串）。</summary>
    Friend Function 注入文本(字节 As Byte()) As String
        If 字节 Is Nothing Then Return String.Empty
        Dim 结束 = Array.IndexOf(字节, CByte(0))
        If 结束 < 0 Then 结束 = 字节.Length
        If 结束 <= 0 Then Return String.Empty
        Return System.Text.Encoding.UTF8.GetString(字节, 0, 结束)
    End Function

    ' ---- 搜索目录：三个入口必须同一份口径，否则"探测说在、解锁说够不到"自相矛盾 ----

    ''' <summary>搜索序：显式覆盖（环境变量 FC_COLORTRANSFORM_DIR）→ **真实 exe 目录** →
    ''' 应用目录 → 裸文件名。每个候选都是"存在才试"，绝不写死绝对路径。
    ''' <para>为什么必须有"真实 exe 目录"这一候选：单文件发布形态下
    ''' <see cref="AppContext.BaseDirectory"/> 是 .NET 解压缓存目录（%TEMP%\.net\...）而不是
    ''' exe 目录——已实测（2026-10-17 五臂）没有任何代码去那里找模块。用户口径是"按 exe
    ''' 目录绝对路径装载"，所以这里用 <see cref="Environment.ProcessPath"/> 的目录
    ''' （进程映像的真实位置，两种发布形态下都正确）。</para></summary>
    Private Iterator Function 候选路径(目录覆盖 As String) As IEnumerable(Of String)
        Dim 已见 As New HashSet(Of String)(StringComparer.OrdinalIgnoreCase)
        If Not String.IsNullOrWhiteSpace(目录覆盖) Then
            Dim 完整 As String = Nothing
            Try
                完整 = IO.Path.Combine(目录覆盖, 模块文件名)
            Catch ex As ArgumentException
                完整 = Nothing
            End Try
            If 完整 IsNot Nothing AndAlso 已见.Add(完整) AndAlso IO.File.Exists(完整) Then
                Yield 完整
            End If
        End If

        ' 真实 exe 目录（用户口径；单文件形态下 AppContext.BaseDirectory 不是它）。
        Dim 进程路径 As String = Nothing
        Try
            进程路径 = Environment.ProcessPath
        Catch ex As Exception
            进程路径 = Nothing
        End Try
        If Not String.IsNullOrWhiteSpace(进程路径) Then
            Dim 进程目录路径 As String = Nothing
            Try
                进程目录路径 = IO.Path.Combine(IO.Path.GetDirectoryName(IO.Path.GetFullPath(进程路径)), 模块文件名)
            Catch ex As Exception
                进程目录路径 = Nothing
            End Try
            If 进程目录路径 IsNot Nothing AndAlso 已见.Add(进程目录路径) AndAlso IO.File.Exists(进程目录路径) Then
                Yield 进程目录路径
            End If
        End If

        Dim 应用目录路径 As String = Nothing
        Try
            应用目录路径 = IO.Path.Combine(AppContext.BaseDirectory, 模块文件名)
        Catch ex As ArgumentException
            应用目录路径 = Nothing
        End Try
        If 应用目录路径 IsNot Nothing AndAlso 已见.Add(应用目录路径) AndAlso IO.File.Exists(应用目录路径) Then
            Yield 应用目录路径
        End If
        已见.Add(模块文件名)
        Yield 模块文件名
    End Function

    ''' <summary>环境变量覆盖（对照实验用：同一份 exe 只改模块的有无与新旧）。</summary>
    Public Function 取目录覆盖() As String
        Try
            Dim 目录 = Environment.GetEnvironmentVariable("FC_COLORTRANSFORM_DIR")
            If String.IsNullOrWhiteSpace(目录) Then Return Nothing
            Return 目录
        Catch
            Return Nothing
        End Try
    End Function

    ''' <summary>盘上是否存在模块文件。不装载、不握手指、不探测授权面——
    ''' 给 UI/诊断用的“可用性”读数（快照的 externalColorExtensionAvailable 位）。
    ''' 只认“带目录分隔符且文件真实存在”的候选：候选路径的最后一项是裸文件名
    ''' （交给系统搜索顺序的装载尝试，不是磁盘存在性证据）。</summary>
    Public Function 模块在盘上(Optional 目录覆盖 As String = Nothing) As Boolean
        For Each 候选 In 候选路径(If(目录覆盖, 取目录覆盖()))
            If 候选.Contains("/"c) OrElse 候选.Contains("\"c) Then Return True
        Next
        Return False
    End Function

    Private Function 逐个装载(目录覆盖 As String, ByRef 句柄 As IntPtr) As Boolean
        句柄 = IntPtr.Zero
        For Each 候选 In 候选路径(目录覆盖)
            句柄 = 装载候选(候选)
            If 句柄 <> IntPtr.Zero Then Return True
        Next
        Return False
    End Function

    ' ---- 探测与解锁：不握手、不建实例、不抛 ----

    ''' <summary>探测模块在场形态。只做三件事：映射模块 → 找授权层裸导出 → 读状态。
    ''' 它是"要不要打扰用户"的判据；"能不能真的用"仍由 <see cref="尝试装载"/> 回答。</summary>
    Public Function 探测(<Out> ByRef 原因 As String, Optional 目录覆盖 As String = Nothing) As 模块在场形态
        原因 = Nothing
        Dim 句柄 As IntPtr = IntPtr.Zero
        Try
            If Not 逐个装载(If(目录覆盖, 取目录覆盖()), 句柄) Then
                原因 = 模块文件名 & " 未装载（Win32 错误码 " & Marshal.GetLastPInvokeError().ToString() & "）"
                Return 模块在场形态.缺席
            End If

            Dim 状态地址 = 解析导出(句柄, 授权状态入口名)
            If 状态地址 = IntPtr.Zero Then
                原因 = "模块不含授权层（早于 ABI 版本 2 的旧件）：没有可解锁的路径"
                Return 模块在场形态.旧件
            End If

            Dim 状态函数 = CType(Marshal.GetDelegateForFunctionPointer(状态地址, GetType(授权状态原型)), 授权状态原型)
            Dim 状态 = 状态函数()
            ' 只有 1/2 是模块声明过的取值；别的按缺席处理——宁可不提示，也不在看不懂的返回值上弹框。
            Select Case 状态
                Case 2UI
                    原因 = "已解锁"
                    Return 模块在场形态.就绪
                Case 1UI
                    原因 = "模块已装载但未解锁：这是唯一需要提示用户的一格"
                    Return 模块在场形态.已锁定
                Case Else
                    原因 = "授权状态返回未知值 " & 状态.ToString()
                    Return 模块在场形态.缺席
            End Select
        Catch ex As Exception
            ' 探测是"要不要打扰用户"的判据：宁可判缺席（不提示），也不能抛。
            原因 = "探测异常 " & ex.GetType().Name & ": " & ex.Message
            Return 模块在场形态.缺席
        Finally
            If 句柄 <> IntPtr.Zero Then 释放模块(句柄)
        End Try
    End Function

    ''' <summary>把一枚解锁码送进模块铸票。不抛；够不到模块一律答 <see cref="解锁结果.够不到"/>。
    ''' 码本身绝不进日志（调用方只记结果与原因）。</summary>
    Public Function 提交解锁码(码 As String, <Out> ByRef 详情 As String,
                               Optional 目录覆盖 As String = Nothing) As 解锁结果
        详情 = Nothing
        If String.IsNullOrWhiteSpace(码) Then Return 解锁结果.被拒
        Dim 句柄 As IntPtr = IntPtr.Zero
        ' 票据写不进盘时，模块的授权只活在本进程的内存里（授予是模块全局态）。
        ' 这一份句柄绝不能再卸掉：FreeLibrary 会把那份授予连同模块一起抹掉，
        ' 之后每次探测/装载都重新读盘 ⇒ 全程未授权，而用户刚看到"已通过"。
        ' 代价是一个进程生命期的模块引用，与"本次运行可用"的承诺正好等长。
        Dim 保留句柄 As Boolean = False
        Try
            ' 搜索口径必须与探测/装载同一份：否则探测说"在且锁着"、弹了框，
            ' 而这里去解锁的是另一枚拷贝（或压根找不到），框白弹一次。
            If Not 逐个装载(If(目录覆盖, 取目录覆盖()), 句柄) Then
                详情 = 模块文件名 & " 未装载，无法提交解锁码"
                Return 解锁结果.够不到
            End If

            Dim 地址 = 解析导出(句柄, 解锁入口名)
            If 地址 = IntPtr.Zero Then
                详情 = "模块不含解锁入口（旧件）"
                Return 解锁结果.够不到
            End If

            Dim 解锁函数 = CType(Marshal.GetDelegateForFunctionPointer(地址, GetType(解锁原型)), 解锁原型)
            Dim 码指针 = IntPtr.Zero
            Dim 返回值 As Integer
            Try
                码指针 = Marshal.StringToCoTaskMemUTF8(码)
                返回值 = 解锁函数(码指针)
            Finally
                If 码指针 <> IntPtr.Zero Then Marshal.FreeCoTaskMem(码指针)
            End Try
            Select Case 返回值
                Case 解锁已落盘
                    Return 解锁结果.已接受
                Case 解锁未落盘
                    保留句柄 = True
                    Return 解锁结果.已接受未落盘
                Case 解锁被拒
                    Return 解锁结果.被拒
                Case Else
                    ' 读不懂的返回码不能当成"码不对"：那会让对话框再要一次输入，
                    ' 而真相是与模块的答复形状对不上了（多半是版本不同水）。
                    详情 = "模块返回未知值 " & 返回值.ToString()
                    Return 解锁结果.够不到
            End Select
        Catch ex As Exception
            详情 = "提交解锁码异常 " & ex.GetType().Name & ": " & ex.Message
            Return 解锁结果.够不到
        Finally
            If 句柄 <> IntPtr.Zero AndAlso Not 保留句柄 Then 释放模块(句柄)
        End Try
    End Function

    ''' <summary>取模块的状态文本（UTF-8）。未知用途或模块缺席返回 Nothing。</summary>
    Public Function 取状态文本(用途 As UInteger, Optional 目录覆盖 As String = Nothing) As String
        Dim 句柄 As IntPtr = IntPtr.Zero
        Try
            If Not 逐个装载(If(目录覆盖, 取目录覆盖()), 句柄) Then Return Nothing
            Dim 地址 = 解析导出(句柄, 状态文本入口名)
            If 地址 = IntPtr.Zero Then Return Nothing
            Dim 文本函数 = CType(Marshal.GetDelegateForFunctionPointer(地址, GetType(状态文本原型)), 状态文本原型)
            Dim 指针 = 文本函数(用途)
            If 指针 = IntPtr.Zero Then Return Nothing
            Return Marshal.PtrToStringUTF8(指针)
        Catch
            Return Nothing
        Finally
            If 句柄 <> IntPtr.Zero Then 释放模块(句柄)
        End Try
    End Function

    ''' <summary>尝试装载模块并交出函数表。任何失败都返回 Nothing 并给出只进日志的原因。
    ''' 版本门不过 / 表不合格 / 载荷容量与内核结构不同水 ⇒ 与"模块不在盘上"同等处理。</summary>
    Public Function 尝试装载(<Out> ByRef 原因 As String, Optional 目录覆盖 As String = Nothing) As 色彩变换模块实例
        原因 = Nothing
        Dim 句柄 As IntPtr = IntPtr.Zero
        Dim 载荷句柄 As GCHandle = Nothing
        Try
            If Not 逐个装载(If(目录覆盖, 取目录覆盖()), 句柄) Then
                原因 = 模块文件名 & " 未装载（Win32 错误码 " & Marshal.GetLastPInvokeError().ToString() & "）——按既定口径静默降级"
                Return Nothing
            End If

            Dim 入口 = 解析导出(句柄, 能力表入口名)
            If 入口 = IntPtr.Zero Then
                原因 = 模块文件名 & " 里没有导出 " & 能力表入口名
                释放模块(句柄)
                Return Nothing
            End If
            Dim 取表 = CType(Marshal.GetDelegateForFunctionPointer(入口, GetType(取能力表原型)), 取能力表原型)

            ' 表按原生布局读写：全标量/指针的 blittable 镜像，钉住后直接读写，不给
            ' "按值传 32 字节表"留歧义。
            Dim 表 As New 色彩变换能力表 With {.大小 = CUInt(Marshal.SizeOf(Of 色彩变换能力表)()), .版本 = 0UI}
            Dim 表句柄 = GCHandle.Alloc(表, GCHandleType.Pinned)
            Dim 入口返回 As Integer
            Try
                入口返回 = 取表(表版本, 表句柄.AddrOfPinnedObject())
                表 = CType(Marshal.PtrToStructure(表句柄.AddrOfPinnedObject(), GetType(色彩变换能力表)), 色彩变换能力表)
            Finally
                表句柄.Free()
            End Try

            ' 旧模块的 32 B 表不含尾列：40 B 镜像的 PtrToStructure 会把表外的堆内存
            ' 读进尾列 —— 按 size 归一，缺列读作零指针 = 通道缺席（bake 回落 100-nit）。
            If 表.大小 < CUInt(Marshal.SizeOf(Of 色彩变换能力表)()) Then
                表.设置显示目标 = IntPtr.Zero
            End If

            If 入口返回 <> 0 OrElse 表.版本 <> 表版本 OrElse
               表.大小 < CUInt(Marshal.SizeOf(Of 色彩变换能力表)()) OrElse
               表.分析帧 = IntPtr.Zero OrElse 表.复位 = IntPtr.Zero Then                ' 握手被拒有三种完全不同的原因，画面却一模一样，必须分开留痕：
                '   1) 盘上是早于 ABI 版本 2 的旧件（不含授权层，也不认这个版本门）
                '   2) 模块在、但未解锁——授权门的设计结果，不是故障
                '   3) 模块自称已解锁却仍被拒——真故障（两侧 ABI 版本没对上）
                原因 = 能力表入口名 & " 返回 " & 入口返回.ToString() & "，或函数表不合格" &
                   "（version=" & 表.版本.ToString() & " size=" & 表.大小.ToString() &
                   " analyze=" & 表.分析帧.ToString() & " reset=" & 表.复位.ToString() & "）；" & 握手被拒补充说明(句柄)
                释放模块(句柄)
                Return Nothing
            End If

            Dim 布局版本 As UInteger = 0UI
            If 表.变换版本 <> IntPtr.Zero Then
                Dim 版本函数 = CType(Marshal.GetDelegateForFunctionPointer(表.变换版本, GetType(变换版本原型)), 变换版本原型)
                布局版本 = 版本函数()
            End If

            ' 可选合成面：导出缺席、版本不符、未授权或表不合格，都只让这条通路缺席，
            ' 不影响已经成功的主握手。少一面 = 不请求合成 = 与今天逐位一致。
            Dim 合成分析地址 As IntPtr = IntPtr.Zero
            Dim 合成入口 = 解析导出(句柄, 合成能力表入口名)
            If 合成入口 <> IntPtr.Zero Then
                Try
                    Dim 取合成表 = CType(Marshal.GetDelegateForFunctionPointer(合成入口, GetType(取合成表原型)), 取合成表原型)
                    Dim 合成表 As New 合成能力表 With {.大小 = CUInt(Marshal.SizeOf(Of 合成能力表)()), .版本 = 0UI}
                    Dim 合成表句柄 = GCHandle.Alloc(合成表, GCHandleType.Pinned)
                    Dim 合成返回 As Integer
                    Try
                        合成返回 = 取合成表(合成表版本, 合成表句柄.AddrOfPinnedObject())
                        合成表 = CType(Marshal.PtrToStructure(合成表句柄.AddrOfPinnedObject(), GetType(合成能力表)), 合成能力表)
                    Finally
                        合成表句柄.Free()
                    End Try
                    If 合成返回 = 0 AndAlso 合成表.版本 = 合成表版本 AndAlso
                       合成表.大小 >= CUInt(Marshal.SizeOf(Of 合成能力表)()) AndAlso
                       合成表.分析 <> IntPtr.Zero Then
                        合成分析地址 = 合成表.分析
                    End If
                Catch ex As Exception
                    ' 合成面坏掉只是"没有这条通路"：主表已就绪，按不请求合成继续。
                End Try
            End If

            Dim 载荷缓冲区 = New Byte(CInt(载荷容量) - 1) {}
            载荷句柄 = GCHandle.Alloc(载荷缓冲区, GCHandleType.Pinned)
            ' 分类文本裸导出：缺了（旧模块）只是"没有内容可显示"，不影响换算本身。
            Dim 内容文本地址 = 解析导出(句柄, 内容文本入口名)
            ' 逐帧峰值裸导出：缺了（旧模块）只是"没有逐帧读数"，同上静默降级。
            Dim 峰值地址 = 解析导出(句柄, 峰值入口名)
            ' 床渲染函数表的导出地址：旧模块没有它只是"没有这条通路"，不影响色彩侧装载。
            Dim 音频表地址 = 解析导出(句柄, 音频能力表入口名)
            ' 音频直通函数表的导出地址：旧模块没有它 ⇒ 内核整体回退解码，同上静默。
            Dim 直通表地址 = 解析导出(句柄, 音频直通表入口名)
            ' 自驱动注入面的导出地址：旧模块没有它 ⇒ 宿主不启动注入，通路只剩显示。
            Dim 注入面地址 = 解析导出(句柄, 注入面入口名)
            Dim 实例 As New 色彩变换模块实例(句柄, 表, 载荷缓冲区, 载荷句柄, 内容文本地址,
                                          峰值地址, 音频表地址, 直通表地址, 合成分析地址, 注入面地址) With {.布局版本 = 布局版本}
            ' 装载成功：模块句柄的所有权已移交给实例（Dispose 时释放），别再走 Finally 的释放。
            句柄 = IntPtr.Zero
            载荷句柄 = Nothing
            Return 实例
        Catch ex As Exception
            原因 = "装载异常 " & ex.GetType().Name & ": " & ex.Message
            If 载荷句柄.IsAllocated Then 载荷句柄.Free()
            Return Nothing
        Finally
            If 句柄 <> IntPtr.Zero Then 释放模块(句柄)
        End Try
    End Function

    ''' <summary>握手被拒时补一段授权态说明，让"没装模块"与"装了没解锁"在日志里不同形。</summary>
    Private Function 握手被拒补充说明(句柄 As IntPtr) As String
        Dim 状态地址 = 解析导出(句柄, 授权状态入口名)
        If 状态地址 = IntPtr.Zero Then Return "模块不含授权层（早于 ABI 版本 2 的旧件）"
        Dim 状态函数 = CType(Marshal.GetDelegateForFunctionPointer(状态地址, GetType(授权状态原型)), 授权状态原型)
        Dim 说明 As String
        Select Case 状态函数()
            Case 1UI
                说明 = "模块已装载但未解锁：按基底出画，属授权门的既定结果而非故障"
            Case 2UI
                说明 = "模块自称已解锁却被握手拒绝 ⇒ 真故障，先查两侧 ABI 版本是否同水"
            Case Else
                说明 = "授权状态返回未知值"
        End Select
        Dim 文本 = 取状态文本(文本用途未授权解释)
        If Not String.IsNullOrEmpty(文本) Then 说明 &= "；模块原话：" & 文本
        Return 说明
    End Function

End Module

' ═══════════════════════════════════════════════════════════════════════════
'  色彩变换模块实例 —— 握手成功后的一个可用模块（一路会话一个实例）
'
'  ● reset() 是"丢弃逐流状态"：多路共用一个实例时，一路的 seek 会把别的路的模块侧
'    状态也清掉 ⇒ 实例级隔离是必需的。
'  ● 载荷缓冲区按实例持有并钉住：431296 B 的按帧重开是大对象分配灾难，
'    内容未变时由调用方去重，不走这里。
' ═══════════════════════════════════════════════════════════════════════════

Friend Class 色彩变换模块实例
    Implements IDisposable

    Private ReadOnly _模块 As IntPtr
    Private ReadOnly _表 As 色彩变换能力表
    Private ReadOnly _载荷缓冲区 As Byte()
    Private ReadOnly _载荷句柄 As GCHandle
    ' 分类文本裸导出的地址（模块函数表之外；装载时解析一次）。
    Private ReadOnly _内容文本地址 As IntPtr
    ' 最近帧峰值裸导出的地址（装载时解析一次）——0 = 盘上是还没有这一导出的旧模块。
    Private ReadOnly _峰值地址 As IntPtr
    ' 外部床渲染函数表裸导出的地址（装载时解析一次）——0 = 盘上是还没有这一面的旧模块。
    Private ReadOnly _音频表入口地址 As IntPtr
    ' 音频直通函数表裸导出的地址（装载时解析一次）——0 = 旧模块 ⇒ 内核整体回退解码。
    Private ReadOnly _直通表入口地址 As IntPtr
    ' 可选合成面的分析入口（装载时解析一次）。0 = 旧模块/未授权/表不合格 ⇒ 不请求合成。
    Private ReadOnly _合成分析地址 As IntPtr
    ' 可选自驱动注入面的导出地址（装载时解析一次）。0 = 旧模块 ⇒ 宿主不启动注入。
    Private ReadOnly _注入表入口地址 As IntPtr
    ' 表一旦交给内核就必须留着模块：内核持有的是指向本模块代码段的裸函数指针与它自己
    ' 分配的 context，FreeLibrary 之后下一次渲染调用就是踩空。所以交出成功后 Dispose
    ' 跳过卸载（代价是这一份映射陪进程到结束，与"功能可用"相比是可接受的）。
    Private _音频表已交出 As Boolean
    Private _音频表函数 As 取音频表原型
    Private _直通表已交出 As Boolean
    Private _直通表函数 As 取直通表原型
    ' GetDelegateForFunctionPointer 的结果按需缓存一次；字段必须显式写类型。
    Private _内容文本函数 As 内容文本原型
    Private _峰值函数 As 峰值原型
    Private _注入表函数 As 取注入表原型
    Private _注入启动 As 注入启动原型
    Private _注入停止 As 注入停止原型
    Private _注入读状态 As 注入读状态原型
    ' 自驱动泵是**进程级单例**（模块内部一个全局运行标志、停止无参），而宿主按会话持有
    ' 一份实例。这两个字段把"泵此刻绑的是哪个内核句柄"记在进程级（Shared），否则换会话
    ' 时会出现：新会话 start 被"已在跑"拒掉、宿主按快照判成功，紧接着旧会话释放时
    ' 无参停止把泵停掉 —— 新会话再也没有泵，且没有任何一处会报错。
    Private Shared ReadOnly _泵锁 As New Object()
    Private Shared _泵当前句柄 As IntPtr
    ' 本实例启动过的那一份（Dispose 只能停自己起的，不能停别人的）。
    Private _本实例泵句柄 As IntPtr
    Private ReadOnly _锁 As New Object()
    Private _已释放 As Boolean

    Friend Sub New(模块 As IntPtr, 表 As 色彩变换能力表, 载荷缓冲区 As Byte(), 载荷句柄 As GCHandle,
                   内容文本地址 As IntPtr, 峰值地址 As IntPtr, 音频表入口地址 As IntPtr,
                   直通表入口地址 As IntPtr, 合成分析地址 As IntPtr, 注入表入口地址 As IntPtr)
        _模块 = 模块
        _表 = 表
        _载荷缓冲区 = 载荷缓冲区
        _载荷句柄 = 载荷句柄
        _内容文本地址 = 内容文本地址
        _峰值地址 = 峰值地址
        _音频表入口地址 = 音频表入口地址
        _直通表入口地址 = 直通表入口地址
        _合成分析地址 = 合成分析地址
        _注入表入口地址 = 注入表入口地址
    End Sub

    ''' <summary>模块提供了音频直通函数表（旧模块没有这个导出 ⇒ 内核整体回退解码，
    ''' 与"模块不在盘上"同形）。</summary>
    Public ReadOnly Property 支持音频直通 As Boolean
        Get
            Return _直通表入口地址 <> IntPtr.Zero
        End Get
    End Property

    ''' <summary>模块提供了可选合成面（它解释自己的逐帧参数；像素合成仍由内核完成）。
    ''' 缺席时宿主不请求合成，整条通路与今天逐位一致。</summary>
    Public ReadOnly Property 支持增强层合成 As Boolean
        Get
            Return _合成分析地址 <> IntPtr.Zero
        End Get
    End Property

    ''' <summary>模块自报的载荷布局版本（仅日志归因用）。</summary>
    Public Property 布局版本 As UInteger

    ''' <summary>模块已装载且能换算一帧。</summary>
    Public ReadOnly Property 可用 As Boolean
        Get
            SyncLock _锁
                Return Not _已释放 AndAlso _表.分析帧 <> IntPtr.Zero
            End SyncLock
        End Get
    End Property

    ''' <summary>丢弃模块侧逐流状态（seek / 换文件 / 解码器重建后必须调）。</summary>
    Public Sub 复位()
        SyncLock _锁
            If _已释放 OrElse _表.复位 = IntPtr.Zero Then Return
            Dim 复位函数 = CType(Marshal.GetDelegateForFunctionPointer(_表.复位, GetType(复位原型)), 复位原型)
            复位函数()
        End SyncLock
    End Sub

    ''' <summary>会话级显示目标（尼特）：bake 尾按它编码映射表（"跟随显示器最大亮度"
    ''' 读数），渲染端用同一个数解码。旧模块缺列时是 no-op（bake 回落经典
    ''' 100-nit SDR 目标）；零值同样回落。</summary>
    Public Sub 设置显示目标尼特(尼特 As Single)
        SyncLock _锁
            If _已释放 OrElse _表.设置显示目标 = IntPtr.Zero Then Return
            Dim 设置函数 = CType(Marshal.GetDelegateForFunctionPointer(_表.设置显示目标,
                GetType(设置显示目标原型)), 设置显示目标原型)
            设置函数(尼特)
        End SyncLock
    End Sub

    ''' <summary>换算一帧的帧边数据。借用输入仅本次调用有效；输出写内部钉住的载荷缓冲区。
    ''' <paramref name="配置记录"/> 是当前流的容器码流配置记录（流级常量；没有就传 Nothing）。
    ''' <paramref name="标志"/> 只应由模块自己的合成分析结果决定（见 <see cref="分析增强层"/>）。
    ''' 返回 <see cref="结果无元数据"/> 表示本帧无可换算据（正常跳过，不是错误）。</summary>
    Public Function 分析帧(边数据 As Byte(), 帧序号 As Long, 配置记录 As Byte(),
                          <Out> ByRef 载荷大小 As Integer,
                          Optional 标志 As UInteger = 0UI) As Integer
        载荷大小 = 0
        Dim 边数据句柄 As GCHandle = Nothing
        Dim 配置记录句柄 As GCHandle = Nothing
        Dim 输入结构句柄 As GCHandle = Nothing
        Dim 输出结构句柄 As GCHandle = Nothing
        Try
            SyncLock _锁
                If _已释放 OrElse _表.分析帧 = IntPtr.Zero Then Return 结果未授权
            End SyncLock

            Dim 输入 As New 色彩变换输入 With {
                .大小 = 输入结构大小,
                .版本 = 表版本,
                .元数据 = IntPtr.Zero,
                .元数据大小 = 0UI,
                .帧序号 = CULng(帧序号),
                .配置记录 = IntPtr.Zero,
                .配置记录大小 = 0UI,
                .标志 = 标志,
                .保留 = 0UI
            }
            If 边数据 IsNot Nothing AndAlso 边数据.Length > 0 Then
                边数据句柄 = GCHandle.Alloc(边数据, GCHandleType.Pinned)
                输入.元数据 = 边数据句柄.AddrOfPinnedObject()
                输入.元数据大小 = CULng(边数据.Length)
            End If
            If 配置记录 IsNot Nothing AndAlso 配置记录.Length > 0 Then
                配置记录句柄 = GCHandle.Alloc(配置记录, GCHandleType.Pinned)
                输入.配置记录 = 配置记录句柄.AddrOfPinnedObject()
                输入.配置记录大小 = CULng(配置记录.Length)
            End If

            Dim 输出 As New 色彩变换输出 With {
                .大小 = CUInt(Marshal.SizeOf(Of 色彩变换输出)()),
                .载荷种类 = 0UI,
                .载荷种类版本 = 0UI,
                .载荷大小 = 0UI,
                .载荷 = _载荷句柄.AddrOfPinnedObject(),
                .载荷容量 = 载荷容量,
                .保留 = 0UI
            }

            输入结构句柄 = GCHandle.Alloc(输入, GCHandleType.Pinned)
            输出结构句柄 = GCHandle.Alloc(输出, GCHandleType.Pinned)
            Try
                Dim 分析函数 = CType(Marshal.GetDelegateForFunctionPointer(_表.分析帧, GetType(分析帧原型)), 分析帧原型)
                Dim 返回 = 分析函数(输入结构句柄.AddrOfPinnedObject(), 输出结构句柄.AddrOfPinnedObject())
                输出 = CType(Marshal.PtrToStructure(输出结构句柄.AddrOfPinnedObject(), GetType(色彩变换输出)), 色彩变换输出)
                If 返回 = 结果成功 AndAlso 输出.载荷大小 > 0UI Then
                    载荷大小 = CInt(输出.载荷大小)
                End If
                Return 返回
            Finally
                输入结构句柄.Free()
                输出结构句柄.Free()
            End Try
        Catch ex As Exception
            Return 结果参数无效
        Finally
            If 边数据句柄.IsAllocated Then 边数据句柄.Free()
            If 配置记录句柄.IsAllocated Then 配置记录句柄.Free()
        End Try
    End Function

    ''' <summary>可选合成面：让模块从同一份帧边数据算出"这一帧要不要合成、按什么参数合成"。
    ''' 宿主把参数原样转交内核，自己不解释任何字段（红线 B）。
    ''' <para>返回 <see cref="结果成功"/> 且 <see cref="原生合成参数.是否启用"/> 非零时，宿主才
    ''' 在 <see cref="分析帧"/> 的输入里置 <see cref="输入标志增强层已预合成"/>。模块缺席这一面时
    ''' 返回 <see cref="结果不支持"/>，调用方按"不合成、不置标志"处理。</para></summary>
    Public Function 分析增强层(边数据 As Byte(), 帧序号 As Long, 配置记录 As Byte(),
                              <Out> ByRef 参数 As 原生合成参数) As Integer
        ' 帧号本面不消费（内核按自己的呈现序号配对），保留形参让两侧调用形状一致。
        参数 = New 原生合成参数()
        If _合成分析地址 = IntPtr.Zero Then Return 结果不支持
        Dim 边数据句柄 As GCHandle = Nothing
        Dim 配置记录句柄 As GCHandle = Nothing
        Dim 输入结构句柄 As GCHandle = Nothing
        Dim 输出结构句柄 As GCHandle = Nothing
        Try
            SyncLock _锁
                If _已释放 Then Return 结果未授权
            End SyncLock
            Dim 输入 As New 原生合成输入 With {
                .大小 = CUInt(Marshal.SizeOf(Of 原生合成输入)()),
                .版本 = 合成表版本,
                .元数据 = IntPtr.Zero,
                .元数据大小 = 0UL,
                .配置记录 = IntPtr.Zero,
                .配置记录大小 = 0UL
            }
            If 边数据 IsNot Nothing AndAlso 边数据.Length > 0 Then
                边数据句柄 = GCHandle.Alloc(边数据, GCHandleType.Pinned)
                输入.元数据 = 边数据句柄.AddrOfPinnedObject()
                输入.元数据大小 = CULng(边数据.Length)
            End If
            If 配置记录 IsNot Nothing AndAlso 配置记录.Length > 0 Then
                配置记录句柄 = GCHandle.Alloc(配置记录, GCHandleType.Pinned)
                输入.配置记录 = 配置记录句柄.AddrOfPinnedObject()
                输入.配置记录大小 = CULng(配置记录.Length)
            End If
            Dim 输出 As New 原生合成输出 With {
                .大小 = CUInt(Marshal.SizeOf(Of 原生合成输出)()),
                .保留 = 0UI
            }
            输入结构句柄 = GCHandle.Alloc(输入, GCHandleType.Pinned)
            输出结构句柄 = GCHandle.Alloc(输出, GCHandleType.Pinned)
            Try
                Dim 分析函数 = CType(Marshal.GetDelegateForFunctionPointer(_合成分析地址, GetType(合成分析原型)), 合成分析原型)
                Dim 返回 = 分析函数(输入结构句柄.AddrOfPinnedObject(), 输出结构句柄.AddrOfPinnedObject())
                输出 = CType(Marshal.PtrToStructure(输出结构句柄.AddrOfPinnedObject(), GetType(原生合成输出)), 原生合成输出)
                If 返回 = 结果成功 Then 参数 = 输出.参数
                Return 返回
            Finally
                输入结构句柄.Free()
                输出结构句柄.Free()
            End Try
        Catch ex As Exception
            Return 结果参数无效
        Finally
            If 边数据句柄.IsAllocated Then 边数据句柄.Free()
            If 配置记录句柄.IsAllocated Then 配置记录句柄.Free()
        End Try
    End Function

    ''' <summary>取内部载荷缓冲区当前内容的副本（调用方拿到即拥有；缓冲区按帧复用）。</summary>
    Public Function 取载荷副本() As Byte()
        SyncLock _锁
            If _已释放 Then Return Nothing
            Return DirectCast(_载荷缓冲区.Clone(), Byte())
        End SyncLock
    End Function

    ''' <summary>调用线程上最近一次 <see cref="分析帧"/> 产出的分类文本（UTF-8，立即拷贝成托管串）。
    ''' <para>⚠ 只能在**刚调用过 分析帧 的同一线程**（泵线程）上读：导出按线程保存快照，
    ''' 指针只在该线程下次 分析帧 前有效；这里拿到就马上拷贝，不留任何跨调用指针。</para>
    ''' <para>导出缺失（早于该 ABI 的模块）或本帧没有分类时答空串；不抛。</para></summary>
    Public Function 取内容描述() As String
        If _内容文本地址 = IntPtr.Zero Then Return String.Empty
        Try
            Dim 函数 = _内容文本函数
            If 函数 Is Nothing Then
                函数 = CType(Marshal.GetDelegateForFunctionPointer(_内容文本地址, GetType(内容文本原型)), 内容文本原型)
                _内容文本函数 = 函数
            End If
            Dim 指针 = 函数()
            If 指针 = IntPtr.Zero Then Return String.Empty
            Return Marshal.PtrToStringUTF8(指针)
        Catch ex As Exception
            Return String.Empty
        End Try
    End Function

    ''' <summary>调用线程上最近一次 <see cref="分析帧"/> 读到的 DV L1 逐帧峰值（尼特）。
    ''' <para>⚠ 与 取内容描述 同一线程契约：只能在**刚调用过 分析帧 的同一线程**（泵线程）上读
    ''' （模块侧按线程保存快照）。</para>
    ''' <para>导出缺失（早于该 ABI 的模块）、未解锁、本帧无 blob 或无 L1 块时答 0；不抛。</para></summary>
    Public Function 取最近帧峰值() As Single
        If _峰值地址 = IntPtr.Zero Then Return 0F
        Try
            Dim 函数 = _峰值函数
            If 函数 Is Nothing Then
                函数 = CType(Marshal.GetDelegateForFunctionPointer(_峰值地址, GetType(峰值原型)), 峰值原型)
                _峰值函数 = 函数
            End If
            Return 函数()
        Catch ex As Exception
            Return 0F
        End Try
    End Function

    ''' <summary>模块提供了自驱动注入面（逐帧注入泵整只活在模块内部）。缺席时宿主
    ''' 不启动注入，内容/峰值/合成三段显示随之没来源，按空串静默。</summary>
    Public ReadOnly Property 支持注入 As Boolean
        Get
            Return _注入表入口地址 <> IntPtr.Zero
        End Get
    End Property

    ''' <summary>启动自驱动注入泵：交给它内核会话句柄（对双方都不透明），泵自己解析
    ''' 内核导出、自己追环、自己判回退。<para>幂等口径：泵已在跑时 start 被模块拒绝，
    ''' 此时若快照说"运行中"按成功算（重复 start 不是故障）。未解锁也算成功——泵在
    ''' 模块内部按 250ms 重试解析自己的分析面，票据落盘后自恢复。</para>
    ''' <para>答 False 的仅有的情形：模块没有注入面 / 握手不合格 / 泵拒绝且确实没在跑。
    ''' 不抛。</para></summary>
    Public Function 启动注入(内核句柄 As IntPtr) As Boolean
        If _注入表入口地址 = IntPtr.Zero OrElse 内核句柄 = IntPtr.Zero Then Return False
        If _已释放 Then Return False
        Try
            SyncLock _泵锁
                ' 已经在给这个句柄跑。重复 start 不是故障 —— 但也绝不能顺手 stop 一次，
                ' 那会把自己刚接管的泵停掉。
                If _泵当前句柄 = 内核句柄 Then
                    _本实例泵句柄 = 内核句柄
                    Return True
                End If
                If Not 确保注入委托() Then Return False
                ' 泵归别的会话（模块是进程级单例，两个会话没法并存）：先真停掉它
                ' （模块的 stop 含线程 join，同步返回），再交到自己的句柄上。
                ' 不这么做就会留下"新会话 start 被拒、宿主按快照判成功、泵其实还绑在
                ' 旧句柄上"的局面 —— 旧会话一释放把泵停掉，新会话从此静默无泵。
                If _泵当前句柄 <> IntPtr.Zero Then 真停泵()
                If _注入启动(内核句柄) = 0 Then
                    _泵当前句柄 = 内核句柄
                    _本实例泵句柄 = 内核句柄
                    Return True
                End If
                ' 拒绝。唯一非故障的含义是"已在跑"，可我们刚停过、停是同步的，此刻仍在跑
                ' 只能是别处并发起的 —— 归属不可确认，按未生效答 False：宁可信息图层不显示，
                ' 也不把一份不属于本会话的读数显示成生效。
                Return False
            End SyncLock
        Catch ex As Exception
            Return False
        End Try
    End Function

    ''' <summary>把注入表的三支函数指针解析成委托（幂等；表只取一次，委托只解一次）。</summary>
    Private Function 确保注入委托() As Boolean
        If _注入表入口地址 = IntPtr.Zero Then Return False
        If _注入启动 IsNot Nothing AndAlso _注入停止 IsNot Nothing AndAlso _注入读状态 IsNot Nothing Then
            Return True
        End If
        If _注入表函数 Is Nothing Then
            _注入表函数 = CType(Marshal.GetDelegateForFunctionPointer(
                _注入表入口地址, GetType(取注入表原型)), 取注入表原型)
        End If
        Dim 表 As New 注入能力表 With {.大小 = CUInt(Marshal.SizeOf(Of 注入能力表)()), .版本 = 0UI}
        Dim 钉住 = GCHandle.Alloc(表, GCHandleType.Pinned)
        Dim 返回 As Integer
        Try
            返回 = _注入表函数(注入面版本, 钉住.AddrOfPinnedObject())
            表 = CType(Marshal.PtrToStructure(钉住.AddrOfPinnedObject(), GetType(注入能力表)), 注入能力表)
        Finally
            钉住.Free()
        End Try
        If 返回 <> 0 OrElse 表.版本 <> 注入面版本 OrElse
           表.大小 < CUInt(Marshal.SizeOf(Of 注入能力表)()) OrElse
           表.启动 = IntPtr.Zero OrElse 表.停止 = IntPtr.Zero OrElse 表.读状态 = IntPtr.Zero Then
            Return False
        End If
        _注入启动 = CType(Marshal.GetDelegateForFunctionPointer(表.启动, GetType(注入启动原型)), 注入启动原型)
        _注入停止 = CType(Marshal.GetDelegateForFunctionPointer(表.停止, GetType(注入停止原型)), 注入停止原型)
        _注入读状态 = CType(Marshal.GetDelegateForFunctionPointer(表.读状态, GetType(注入读状态原型)), 注入读状态原型)
        Return True
    End Function

    ''' <summary>停掉进程级那一份泵并清空归属。调用方必须已持 <c>_泵锁</c>。</summary>
    Private Sub 真停泵()
        Try
            If _注入停止 IsNot Nothing Then _注入停止()
        Catch ex As Exception
            ' 停泵失败不该把调用方拖死：模块 PROCESS_DETACH 兜底还会再停一次。
        End Try
        _泵当前句柄 = IntPtr.Zero
        _本实例泵句柄 = IntPtr.Zero
    End Sub

    ''' <summary>停泵（幂等、不抛）。必须在内核会话句柄销毁**之前**调用——泵的每次
    ''' 原生调用都要用活句柄，句柄销毁后泵线程再进内核就是 use-after-free。</summary>
    ''' <param name="内核句柄">本会话的句柄。只有泵正绑在它上面时才真停；泵归别的会话时
    ''' 放手不动 —— 否则"换文件"里旧会话的释放会把新会话刚接管的泵停死。传 Nothing 表示
    ''' 无条件停（进程级复位用；常规路径一律传句柄）。</param>
    Public Sub 停止注入(Optional 内核句柄 As IntPtr = Nothing)
        Try
            SyncLock _泵锁
                If _泵当前句柄 = IntPtr.Zero Then
                    ' 泵已经不在（进程级那份没了）：本实例的记录一并作废，
                    ' 免得 Dispose 拿它去停一个不存在的泵。
                    _本实例泵句柄 = IntPtr.Zero
                    Return
                End If
                If 内核句柄 <> IntPtr.Zero AndAlso _泵当前句柄 <> 内核句柄 Then Return
                If Not 确保注入委托() Then
                    _泵当前句柄 = IntPtr.Zero
                    _本实例泵句柄 = IntPtr.Zero
                    Return
                End If
                真停泵()
            End SyncLock
        Catch ex As Exception
            ' 停泵失败不阻断会话清理；模块自身的 PROCESS_DETACH 兜底还会再停一次。
        End Try
    End Sub

    ''' <summary>读泵的连贯快照（同拍计数 + 文本 + 峰值）。泵没启动 / 无注入面 / 异常时
    ''' 答 False 且把状态清零（末帧帧号 = -1）。</summary>
    Public Function 读取注入状态(ByRef 状态 As 注入状态) As Boolean
        状态 = New 注入状态 With {.末帧帧号 = -1L}
        ' 卸载之后绝不进模块代码：FreeLibrary 已回收它的代码段，委托指针指向的
        ' 就是别的模块的地址了。释放与界面读快照不在同一线程时，这条是唯一的护栏。
        If _已释放 Then Return False
        If _注入读状态 Is Nothing Then Return False
        Try
            Dim 本地 As 注入状态 = 状态
            本地.大小 = CUInt(Marshal.SizeOf(Of 注入状态)())
            _注入读状态(本地)
            状态 = 本地
            Return True
        Catch ex As Exception
            状态 = New 注入状态 With {.末帧帧号 = -1L}
            Return False
        End Try
    End Function

    ''' <summary>握手取外部床渲染函数表。旧模块没有这一面 / 未授权 / 表不合格 ⇒ 返回 Nothing
    ''' 并给出只进日志的原因，按既定口径与"模块不在盘上"同等处理：不提示、不报错、
    ''' 床继续由内核自己的矩阵折叠处理（那是现网行为，不是新的失败分支）。</summary>
    Public Function 尝试取音频渲染表(<Out> ByRef 原因 As String) As Nullable(Of 音频渲染能力表)
        原因 = Nothing
        If _音频表入口地址 = IntPtr.Zero Then
            原因 = 模块文件名 & " 里没有导出 " & 音频能力表入口名 & "——床保持矩阵折叠，静默降级"
            Return Nothing
        End If
        Try
            If CUInt(Marshal.SizeOf(Of 音频渲染能力表)()) <> 音频表大小 Then
                ' 镜像与内核契约不同水：把这张表交给内核就是让内核按错的偏移读指针。
                原因 = "音频函数表镜像尺寸 " & Marshal.SizeOf(Of 音频渲染能力表)().ToString() &
                        " 与契约 " & 音频表大小.ToString() & " 不同水——不交出表"
                Return Nothing
            End If
            If _音频表函数 Is Nothing Then
                _音频表函数 = CType(Marshal.GetDelegateForFunctionPointer(
                    _音频表入口地址, GetType(取音频表原型)), 取音频表原型)
            End If
            Dim 表 As New 音频渲染能力表 With {.大小 = 音频表大小, .版本 = 0UI}
            Dim 钉住 = GCHandle.Alloc(表, GCHandleType.Pinned)
            Dim 返回 As Integer
            Try
                返回 = _音频表函数(音频表版本, 钉住.AddrOfPinnedObject())
                表 = CType(Marshal.PtrToStructure(钉住.AddrOfPinnedObject(),
                        GetType(音频渲染能力表)), 音频渲染能力表)
            Finally
                钉住.Free()
            End Try
            If 返回 <> 0 OrElse 表.版本 <> 音频表版本 OrElse 表.大小 < 音频表大小 OrElse
               表.上下文 = IntPtr.Zero OrElse 表.支持 = IntPtr.Zero OrElse
               表.渲染 = IntPtr.Zero OrElse 表.复位 = IntPtr.Zero Then
                ' 与色彩侧同一套三分法：旧件没有这一面、模块在但未解锁（设计结果）、
                ' 自称已解锁却仍被拒（真故障）。日志必须分得开。
                原因 = 音频能力表入口名 & " 返回 " & 返回.ToString() &
                        "（version=" & 表.版本.ToString() & " size=" & 表.大小.ToString() &
                        " context=" & 表.上下文.ToString() & "）——床保持矩阵折叠"
                Return Nothing
            End If
            _音频表已交出 = True
            Return 表
        Catch ex As Exception
            原因 = "音频握手异常 " & ex.GetType().Name & ": " & ex.Message
            Return Nothing
        End Try
    End Function

    ''' <summary>模块已被交出的函数表绑住时，本属性为真；只给日志与测试用。</summary>
    Public ReadOnly Property 音频表已交出 As Boolean
        Get
            Return _音频表已交出
        End Get
    End Property

    ''' <summary>模块已被交出的直通函数表绑住时，本属性为真；只给日志与测试用。</summary>
    Public ReadOnly Property 直通表已交出 As Boolean
        Get
            Return _直通表已交出
        End Get
    End Property

    ''' <summary>握手取音频直通函数表。旧模块没有这一面 / 未授权 / 表不合格 ⇒ 返回
    ''' Nothing 并给出只进日志的原因，按既定口径与"模块不在盘上"同等处理：
    ''' 不提示、不报错、内核整体回退解码路径（那是现网行为，不是新的失败分支）。</summary>
    Public Function 尝试取音频直通表(<Out> ByRef 原因 As String) As Nullable(Of 音频直通能力表)
        原因 = Nothing
        If _直通表入口地址 = IntPtr.Zero Then
            原因 = 模块文件名 & " 里没有导出 " & 音频直通表入口名 & "——直通整体回退解码，静默降级"
            Return Nothing
        End If
        Try
            If CUInt(Marshal.SizeOf(Of 音频直通能力表)()) <> 音频直通表大小 Then
                ' 镜像与内核契约不同水：把这张表交给内核就是让内核按错的偏移读指针。
                原因 = "音频直通函数表镜像尺寸 " & Marshal.SizeOf(Of 音频直通能力表)().ToString() &
                        " 与契约 " & 音频直通表大小.ToString() & " 不同水——不交出表"
                Return Nothing
            End If
            If _直通表函数 Is Nothing Then
                _直通表函数 = CType(Marshal.GetDelegateForFunctionPointer(
                    _直通表入口地址, GetType(取直通表原型)), 取直通表原型)
            End If
            Dim 表 As New 音频直通能力表 With {.大小 = 音频直通表大小, .版本 = 0UI}
            Dim 钉住 = GCHandle.Alloc(表, GCHandleType.Pinned)
            Dim 返回 As Integer
            Try
                返回 = _直通表函数(音频直通表版本, 钉住.AddrOfPinnedObject())
                表 = CType(Marshal.PtrToStructure(钉住.AddrOfPinnedObject(),
                        GetType(音频直通能力表)), 音频直通能力表)
            Finally
                钉住.Free()
            End Try
            If 返回 <> 0 OrElse 表.版本 <> 音频直通表版本 OrElse 表.大小 < 音频直通表大小 OrElse
               表.上下文 = IntPtr.Zero OrElse 表.描述 = IntPtr.Zero OrElse
               表.打包 = IntPtr.Zero OrElse 表.复位 = IntPtr.Zero Then
                ' 与色彩侧同一套三分法：旧件没有这一面、模块在但未解锁（设计结果）、
                ' 自称已解锁却仍被拒（真故障）。日志必须分得开。
                原因 = 音频直通表入口名 & " 返回 " & 返回.ToString() &
                        "（version=" & 表.版本.ToString() & " size=" & 表.大小.ToString() &
                        " context=" & 表.上下文.ToString() & "）——直通整体回退解码"
                Return Nothing
            End If
            _直通表已交出 = True
            Return 表
        Catch ex As Exception
            原因 = "音频直通握手异常 " & ex.GetType().Name & ": " & ex.Message
            Return Nothing
        End Try
    End Function

    Public Sub Dispose() Implements IDisposable.Dispose
        SyncLock _锁
            If _已释放 Then Return
            _已释放 = True
        End SyncLock
        ' 先停自驱动注入泵：泵线程拿着内核会话句柄与模块导出地址，会话句柄销毁或
        ' FreeLibrary 之后泵再进循环就是踩空。只停本实例起的那一份 —— 泵是进程级
        ' 单例，已经交给别的会话的泵不归这里管。幂等、不抛。
        If _本实例泵句柄 <> IntPtr.Zero Then 停止注入(_本实例泵句柄)
        ' 音频表一旦交给内核，这条映射与载荷缓冲都还活在内核的渲染路径上：内核持有
        ' 的是表里的裸函数指针和它自己那份指向载荷缓冲的地址。此时回收任何一样都会
        ' 让下一次渲染调用踩空，所以整份实例搁置到进程结束（_已释放 已置位，读取
        ' 路径不再进模块）。这段早退必须排在 Free 载荷句柄之前。
        If _音频表已交出 Then Return
        Try
            If _载荷句柄.IsAllocated Then _载荷句柄.Free()
        Catch
            ' 释放失败不应掩盖后续清理
        End Try
        If _模块 <> IntPtr.Zero Then
            Try
                FreeLibrary(_模块)
            Catch ex As DllNotFoundException
            End Try
        End If
    End Sub
End Class
