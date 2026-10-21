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
    Friend Const 表版本 As UInteger = 2UI

    ''' <summary>外部床渲染函数表的导出名（本模块的第二个能力面，中性词汇）。
    ''' 未授权时模块拒交这张表 ⇒ 内核继续走它自己的矩阵折叠，症状与"模块不在盘上"同形。</summary>
    Friend Const 音频能力表入口名 As String = "FFF_GetAudioRenderApi"

    ''' <summary>可选合成面的导出名：只交逐帧参数，不交任何像素代码。
    ''' 旧模块没有这个导出 ⇒ 宿主不请求合成，整条通路与今天逐位一致。</summary>
    Friend Const 合成能力表入口名 As String = "FFF_GetElCompositionApi"

    ''' <summary>可选合成面的版本门（模块 FFFElCompositionVersion）。</summary>
    Friend Const 合成表版本 As UInteger = 1UI

    ''' <summary>音频函数表版本门，与内核 FFF3FP_AUDIO_PLUG_VERSION 同水；数值漂移即握手失败。</summary>
    Friend Const 音频表版本 As UInteger = 1UI

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

    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Function 变换版本原型() As UInteger

    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Function 分析帧原型(输入地址 As IntPtr, 输出地址 As IntPtr) As Integer

    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Sub 复位原型()

    ''' <summary>能力表：size + version + 三个函数指针（32 B，与模块 FFFColorTransformApi 同布局）。</summary>
    <StructLayout(LayoutKind.Sequential)>
    Friend Structure 色彩变换能力表
        Public 大小 As UInteger
        Public 版本 As UInteger
        Public 变换版本 As IntPtr
        Public 分析帧 As IntPtr
        Public 复位 As IntPtr
    End Structure

    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Function 取音频表原型(版本 As UInteger, 表地址 As IntPtr) As Integer

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

            If 入口返回 <> 0 OrElse 表.版本 <> 表版本 OrElse
               表.大小 < CUInt(Marshal.SizeOf(Of 色彩变换能力表)()) OrElse
               表.分析帧 = IntPtr.Zero OrElse 表.复位 = IntPtr.Zero Then
                ' 握手被拒有三种完全不同的原因，画面却一模一样，必须分开留痕：
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
            ' 床渲染函数表的导出地址：旧模块没有它只是"没有这条通路"，不影响色彩侧装载。
            Dim 音频表地址 = 解析导出(句柄, 音频能力表入口名)
            Dim 实例 As New 色彩变换模块实例(句柄, 表, 载荷缓冲区, 载荷句柄, 内容文本地址,
                                          音频表地址, 合成分析地址) With {.布局版本 = 布局版本}
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
    ' 外部床渲染函数表裸导出的地址（装载时解析一次）——0 = 盘上是还没有这一面的旧模块。
    Private ReadOnly _音频表入口地址 As IntPtr
    ' 可选合成面的分析入口（装载时解析一次）。0 = 旧模块/未授权/表不合格 ⇒ 不请求合成。
    Private ReadOnly _合成分析地址 As IntPtr
    ' 表一旦交给内核就必须留着模块：内核持有的是指向本模块代码段的裸函数指针与它自己
    ' 分配的 context，FreeLibrary 之后下一次渲染调用就是踩空。所以交出成功后 Dispose
    ' 跳过卸载（代价是这一份映射陪进程到结束，与"功能可用"相比是可接受的）。
    Private _音频表已交出 As Boolean
    Private _音频表函数 As 取音频表原型
    ' GetDelegateForFunctionPointer 的结果按需缓存一次；字段必须显式写类型。
    Private _内容文本函数 As 内容文本原型
    Private ReadOnly _锁 As New Object()
    Private _已释放 As Boolean

    Friend Sub New(模块 As IntPtr, 表 As 色彩变换能力表, 载荷缓冲区 As Byte(), 载荷句柄 As GCHandle,
                   内容文本地址 As IntPtr, 音频表入口地址 As IntPtr, 合成分析地址 As IntPtr)
        _模块 = 模块
        _表 = 表
        _载荷缓冲区 = 载荷缓冲区
        _载荷句柄 = 载荷句柄
        _内容文本地址 = 内容文本地址
        _音频表入口地址 = 音频表入口地址
        _合成分析地址 = 合成分析地址
    End Sub

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

    Public Sub Dispose() Implements IDisposable.Dispose
        SyncLock _锁
            If _已释放 Then Return
            _已释放 = True
        End SyncLock
        Try
            If _载荷句柄.IsAllocated Then _载荷句柄.Free()
        Catch
            ' 释放失败不应掩盖后续清理
        End Try
        If _模块 <> IntPtr.Zero Then
            If _音频表已交出 Then
                ' 见 _音频表已交出 的说明：内核还拿着这张表里的指针，不能卸载。
                Return
            End If
            Try
                FreeLibrary(_模块)
            Catch ex As DllNotFoundException
            End Try
        End If
    End Sub
End Class
