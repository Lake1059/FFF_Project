Imports System
Imports System.Collections.Generic
Imports System.Diagnostics
Imports System.Runtime.InteropServices
Imports System.Threading

''' <summary>
''' 系统媒体面板（Windows SMTC）的宿主封装。
'''
''' <para>本类不再直接引用 WinRT 投影（Windows.Foundation / Windows.Media /
''' Windows.Storage.Streams）——那些类型会把 Microsoft.Windows.SDK.NET.dll 与
''' WinRT.Runtime.dll 拖进单文件本体。真正的 SMTC 控件由内核 FFF.Native.dll
''' 通过 Win32 COM ABI（RoGetActivationFactory + ISystemMediaTransportControlsInterop）
''' 创建并持有；本类只做 P/Invoke 到 FFF3FP_Smtc* 这一组 C 导出。</para>
'''
''' <para><b>线程</b>：内核侧 COM 控件只能在创建它的 UI 线程上访问。本类的
''' <see cref="发布卡片"/> / <see cref="发布状态"/> / <see cref="发布时间轴"/> 可被任意线程调用，
''' 内部统一用构造时传入的 <see cref="SynchronizationContext"/> Post 回 UI 线程；
''' <see cref="初始化(IntPtr)"/>、<see cref="关闭"/>、<see cref="设置按钮可用性"/>、
''' <see cref="清空卡片"/> 必须由 UI 线程调用（Form1 的接线保证）。</para>
'''
''' <para><b>失败策略</b>：SMTC 是"锦上添花"，任何一步失败只记录 Debug 输出并降级，
''' 绝不向播放路径抛异常。初始化失败后 <see cref="已启用面板"/> 保持 False，不注册卡片。</para>
''' </summary>
Public NotInheritable Class 系统媒体会话
    Implements IDisposable

    ' ---- P/Invoke 到 FFF.Native.dll 的 FFF3FP_Smtc* 导出 ----
    ' 这些导出是进程级（不挂在 FFF3FPHandle 上），与 FFF.Player.Api.h 同一套
    ' extern "C" __declspec(dllexport) + cdecl 约定。旧内核没有这些导出时，
    ' 静态 DllImport 会在 JIT 时抛 EntryPointNotFoundException——所以我们像
    ' 动态元数据那组导出一样，用 LoadLibrary/GetProcAddress 按需解析，
    ' 解析失败就把 已启用 永久置 False，整条 SMTC 通路静默降级。
    Private Const 动态库名称 As String = "FFF.Native.dll"

    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Function SmtcButtonCallback(用户 As IntPtr, 按钮序号 As Integer) As Integer
    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Function SmtcDoubleCallback(用户 As IntPtr, 值 As Double) As Integer

    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Function FFF3FP_SmtcInitializeDelegate(窗口句柄 As IntPtr) As Integer
    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Sub FFF3FP_SmtcShutdownDelegate()
    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Sub FFF3FP_SmtcSetPlaybackStatusDelegate(状态 As Integer)
    ' 字符串参数必须用 Unicode：内核侧是 const wchar_t*（UTF-16）。
    <UnmanagedFunctionPointer(CallingConvention.Cdecl, CharSet:=CharSet.Unicode)>
    Friend Delegate Sub FFF3FP_SmtcPublishCardDelegate(媒体标识 As String, 标题 As String,
                                                        艺术家 As String, 专辑 As String,
                                                        专辑艺术家 As String, 轨道号 As UInteger,
                                                        媒体类型 As Integer)
    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Function FFF3FP_SmtcPublishThumbnailDelegate(png字节 As Byte(),
                                                                  字节数 As UInteger) As Integer
    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Sub FFF3FP_SmtcClearCardDelegate()
    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Sub FFF3FP_SmtcPublishTimelineDelegate(起始秒 As Double, 结束秒 As Double,
                                                            位置秒 As Double, 倍速 As Double,
                                                            可跳转 As Integer)
    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Sub FFF3FP_SmtcSetButtonEnabledDelegate(按钮序号 As Integer, 启用 As Integer)
    <UnmanagedFunctionPointer(CallingConvention.Cdecl)>
    Friend Delegate Sub FFF3FP_SmtcSetCallbacksDelegate(按钮回调 As IntPtr, 跳转回调 As IntPtr,
                                                         倍速回调 As IntPtr, 用户 As IntPtr)

    ' 函数指针缓存（LoadLibrary 句柄 + 委托实例）。委托实例必须保持引用，
    ' 否则 GC 会回收它们，内核回调时跳到已释放的代码。
    Private 库句柄 As IntPtr = IntPtr.Zero
    Private 初始化Fn As FFF3FP_SmtcInitializeDelegate
    Private 关闭Fn As FFF3FP_SmtcShutdownDelegate
    Private 设状态Fn As FFF3FP_SmtcSetPlaybackStatusDelegate
    Private 发卡片Fn As FFF3FP_SmtcPublishCardDelegate
    Private 发缩略图Fn As FFF3FP_SmtcPublishThumbnailDelegate
    Private 清卡片Fn As FFF3FP_SmtcClearCardDelegate
    Private 发时间轴Fn As FFF3FP_SmtcPublishTimelineDelegate
    Private 设按钮Fn As FFF3FP_SmtcSetButtonEnabledDelegate
    Private 设回调Fn As FFF3FP_SmtcSetCallbacksDelegate

    ' 三个回调委托实例必须作为字段长期持有（不能用局部变量）：
    ' Marshal.GetFunctionPointerForDelegate 取出的函数指针是临时的，GC 一旦回收
    ' 委托实例，内核事件触发时会跳到已释放的代码。这里 ReadOnly 在构造函数里
    ' 绑定，与会话同生命周期；Shutdown 之后内核不再回调，GC 回收才安全。
    Private ReadOnly 按钮回调实例 As SmtcButtonCallback
    Private ReadOnly 跳转回调实例 As SmtcDoubleCallback
    Private ReadOnly 倍速回调实例 As SmtcDoubleCallback

    Private ReadOnly 同步上下文 As SynchronizationContext
    Private ReadOnly 命令接收器 As Func(Of 系统媒体按钮, Boolean)
    Private ReadOnly 跳转请求接收器 As Func(Of TimeSpan, Boolean)
    Private ReadOnly 倍速请求接收器 As Func(Of Double, Boolean)

    Private 已启用 As Boolean
    Private 已关闭 As Boolean
    Private 已释放 As Boolean

    Private 上次播放状态 As 系统媒体播放状态 = 系统媒体播放状态.已关闭
    Private 上次显示类型 As 系统媒体显示类型?
    Private 上次倍速 As Double = 1.0R
    Private 上次时间轴 As 系统媒体时间轴?

    Private ReadOnly 待发布 As New Queue(Of 系统媒体更新)()
    Private 发布已排队 As Integer

    ''' <summary>
    ''' 创建一个可回收的 SMTC 会话壳；此时不碰内核，真正创建要等 <see cref="初始化(IntPtr)"/>。
    ''' </summary>
    Public Sub New(同步上下文 As SynchronizationContext,
                   命令接收器 As Func(Of 系统媒体按钮, Boolean),
                   跳转请求接收器 As Func(Of TimeSpan, Boolean),
                   倍速请求接收器 As Func(Of Double, Boolean))
        Me.同步上下文 = 同步上下文
        Me.命令接收器 = 命令接收器
        Me.跳转请求接收器 = 跳转请求接收器
        Me.倍速请求接收器 = 倍速请求接收器

        ' 提前绑定回调委托实例（不依赖内核是否加载成功）。
        按钮回调实例 = New SmtcButtonCallback(AddressOf OnNativeButton)
        跳转回调实例 = New SmtcDoubleCallback(AddressOf OnNativeSeek)
        倍速回调实例 = New SmtcDoubleCallback(AddressOf OnNativeRate)
    End Sub

    Public ReadOnly Property 已启用面板 As Boolean
        Get
            Return 已启用
        End Get
    End Property

    ''' <summary>
    ''' 惰性创建系统媒体会话并挂事件；必须由 UI 线程、在窗口句柄已创建后调用。
    ''' 返回 False 表示当前环境不支持或创建失败，调用方应保持"无系统媒体卡"并继续播放。
    ''' </summary>
    Public Function 初始化(窗口句柄 As IntPtr) As Boolean
        If 已释放 OrElse 已关闭 Then Return False
        If 已启用 Then Return True
        If 窗口句柄 = IntPtr.Zero Then Return False

        Try
            If Not 解析内核导出() Then Return False
            If 初始化Fn Is Nothing Then Return False

            ' 先注册回调（内核在 Initialize 之前就要看到函数指针，事件触发时才不会空跳）。
            Dim 按钮Ptr = Marshal.GetFunctionPointerForDelegate(按钮回调实例)
            Dim 跳转Ptr = Marshal.GetFunctionPointerForDelegate(跳转回调实例)
            Dim 倍速Ptr = Marshal.GetFunctionPointerForDelegate(倍速回调实例)
            设回调Fn(按钮Ptr, 跳转Ptr, 倍速Ptr, IntPtr.Zero)

            If 初始化Fn(窗口句柄) <> 0 Then
                Debug.WriteLine("SMTC：内核 FFF3FP_SmtcInitialize 返回非零，降级为无系统媒体卡")
                Return False
            End If

            已启用 = True
            Return True
        Catch ex As Exception
            Debug.WriteLine("SMTC：初始化失败，降级为无系统媒体卡：" & ex.GetType().Name & " - " & ex.Message)
            已启用 = False
            Return False
        End Try
    End Function

    ''' <summary>
    ''' 清空卡片并关闭面板，但保留本对象（同一窗口后续打开媒体可再次 <see cref="初始化(IntPtr)"/>）。
    ''' 必须由 UI 线程调用，且在 Form1 句柄销毁之前。
    ''' </summary>
    Public Sub 清空卡片()
        If Not 已启用 Then Return
        ' 清空必须同时丢掉尚未执行的旧发布项；否则"停止/切图片"先把卡片关掉，
        ' 排队的旧卡片随后又把面板点亮，形成无法控制的幽灵卡片。
        SyncLock 待发布
            待发布.Clear()
        End SyncLock
        Try
            If 清卡片Fn IsNot Nothing Then 清卡片Fn()
        Catch ex As Exception
            Debug.WriteLine("SMTC：清空卡片失败（已忽略）：" & ex.GetType().Name)
        End Try
        上次播放状态 = 系统媒体播放状态.已关闭
        上次显示类型 = Nothing
        上次时间轴 = Nothing
    End Sub

    ''' <summary>关闭整个面板并摘除事件。可重复调用；关闭后本对象不再可用。</summary>
    Public Sub 关闭()
        If 已关闭 Then Return
        已关闭 = True
        已启用 = False
        清空卡片()
        Try
            If 关闭Fn IsNot Nothing Then 关闭Fn()
        Catch ex As Exception
            Debug.WriteLine("SMTC：摘事件失败（已忽略）：" & ex.GetType().Name)
        End Try
        SyncLock 待发布
            待发布.Clear()
        End SyncLock
    End Sub

    Public Sub Dispose() Implements IDisposable.Dispose
        If 已释放 Then Return
        关闭()
        已释放 = True
    End Sub

    ''' <summary>
    ''' 设置系统媒体卡缩略图。入参是已编码的 PNG 字节；空数组表示清除。
    ''' 必须由 UI 线程调用（与其它 DisplayUpdater 写操作同一线程）。
    ''' 失败只记 Debug，不影响播放或卡片文字信息。
    ''' </summary>
    Public Sub 设置缩略图(png字节 As Byte())
        If 已释放 OrElse 已关闭 OrElse Not 已启用 Then Return
        Dim rc = 测试用设置缩略图(png字节)
        If rc <> 0 Then Debug.WriteLine("SMTC：内核设置缩略图返回非零（已忽略）：" & rc)
    End Sub

    ''' <summary>测试钩子：返回内核 FFF3FP_SmtcPublishThumbnail 的真实返回码，
    ''' 让冒烟测试能断言 PNG 字节 → 内存流 → RandomAccessStreamReference 通路成功，
    ''' 而不是只看“没抛异常”。</summary>
    Friend Function 测试用设置缩略图(png字节 As Byte()) As Integer
        If 已释放 OrElse 已关闭 OrElse Not 已启用 OrElse 发缩略图Fn Is Nothing Then Return -1
        Try
            Dim 数据 As Byte() = If(png字节, Array.Empty(Of Byte)())
            Return 发缩略图Fn(数据, CUInt(数据.Length))
        Catch ex As Exception
            Debug.WriteLine("SMTC：设置缩略图失败（已忽略）：" & ex.GetType().Name)
            Return -2
        End Try
    End Function

    ''' <summary>
    ''' 发布一张完整的媒体卡片。Form1 负责去重（同一媒体标识 + 标题 + 艺术家）。
    ''' 媒体标识（AppMediaId）、专辑艺术家、轨道号都透传到内核
    ''' FFF3FP_SmtcPublishCard；缩略图走独立的 设置缩略图 PNG 字节入口。
    ''' </summary>
    Public Sub 发布卡片(媒体标识 As String, 标题 As String, 艺术家 As String,
                        专辑 As String, 专辑艺术家 As String, 轨道号 As UInteger,
                        显示类型 As 系统媒体显示类型, 倍速 As Double)
        If 已释放 OrElse 已关闭 Then Return
        Dim 项 As New 系统媒体更新()
        项.媒体标识 = If(媒体标识, String.Empty)
        项.标题 = If(标题, String.Empty)
        项.艺术家 = If(艺术家, String.Empty)
        项.专辑 = If(专辑, String.Empty)
        项.专辑艺术家 = If(专辑艺术家, String.Empty)
        项.轨道号 = 轨道号
        项.显示类型 = 显示类型
        项.倍速 = 倍速
        排队发布(项)
    End Sub

    ''' <summary>发布播放状态；传入本地中立枚举，内部转成内核约定的整数。</summary>
    Public Sub 发布状态(状态 As 系统媒体播放状态)
        If 已释放 OrElse 已关闭 Then Return
        Dim 项 As New 系统媒体更新()
        项.状态 = 状态
        排队发布(项)
    End Sub

    ''' <summary>发布时间轴；只在播放中由 Form1 的 1 Hz UI 计时器调用。</summary>
    Public Sub 发布时间轴(时间轴 As 系统媒体时间轴)
        If 已释放 OrElse 已关闭 Then Return
        Dim 项 As New 系统媒体更新()
        项.时间轴 = 时间轴
        排队发布(项)
    End Sub

    ''' <summary>单独发布倍速；用户改倍速时调用，避免等待下一次时间轴 tick。</summary>
    Public Sub 发布倍速(倍速 As Double)
        If 已释放 OrElse 已关闭 Then Return
        Dim 项 As New 系统媒体更新()
        项.倍速 = 倍速
        排队发布(项)
    End Sub

    ''' <summary>时间轴与倍速合并发布，减少跨线程队列项。</summary>
    Public Sub 发布时间轴和倍速(时间轴 As 系统媒体时间轴, 倍速 As Double)
        If 已释放 OrElse 已关闭 Then Return
        Dim 项 As New 系统媒体更新()
        项.时间轴 = 时间轴
        项.倍速 = 倍速
        排队发布(项)
    End Sub

    ''' <summary>
    ''' 设置系统按钮可用性。由 UI 线程调用；Form1 每次状态/播放列表变化时按快照算一遍。
    ''' 参数语义直接对应用户看到的按钮，内部不再从播放状态猜。
    ''' </summary>
    Public Sub 设置按钮可用性(可播放 As Boolean, 可暂停 As Boolean, 可停止 As Boolean,
                              可跳转 As Boolean, 可下一曲 As Boolean, 可上一曲 As Boolean)
        If Not 已启用 OrElse 设按钮Fn Is Nothing Then Return
        Try
            ' 按钮序号与 系统媒体按钮 枚举逐值对齐：0=Play 1=Pause 2=Stop
            ' 4=FastForward 5=Rewind 6=Next 7=Previous（3=Record 内核恒关）。
            设按钮Fn(0, If(可播放, 1, 0))
            设按钮Fn(1, If(可暂停, 1, 0))
            设按钮Fn(2, If(可停止, 1, 0))
            设按钮Fn(4, If(可跳转, 1, 0))
            设按钮Fn(5, If(可跳转, 1, 0))
            设按钮Fn(6, If(可下一曲, 1, 0))
            设按钮Fn(7, If(可上一曲, 1, 0))
        Catch ex As Exception
            Debug.WriteLine("SMTC：设置按钮可用性失败（已忽略）：" & ex.GetType().Name)
        End Try
    End Sub

    ' ---- 原生事件回调：在内核事件线程上被调用，必须尽快返回 ----

    Private Function OnNativeButton(用户 As IntPtr, 按钮序号 As Integer) As Integer
        Try
            If 命令接收器 IsNot Nothing Then
                Return If(命令接收器(CType(按钮序号, 系统媒体按钮)), 1, 0)
            End If
        Catch ex As Exception
            Debug.WriteLine("SMTC：按钮回调失败（已忽略）：" & ex.GetType().Name)
        End Try
        Return 0
    End Function

    Private Function OnNativeSeek(用户 As IntPtr, 位置秒 As Double) As Integer
        Try
            If 跳转请求接收器 IsNot Nothing Then
                Return If(跳转请求接收器(TimeSpan.FromSeconds(位置秒)), 1, 0)
            End If
        Catch ex As Exception
            Debug.WriteLine("SMTC：seek 回调失败（已忽略）：" & ex.GetType().Name)
        End Try
        Return 0
    End Function

    Private Function OnNativeRate(用户 As IntPtr, 倍速 As Double) As Integer
        Try
            If 倍速请求接收器 IsNot Nothing Then
                Return If(倍速请求接收器(倍速), 1, 0)
            End If
        Catch ex As Exception
            Debug.WriteLine("SMTC：倍速回调失败（已忽略）：" & ex.GetType().Name)
        End Try
        Return 0
    End Function

    ''' <summary>测试钩子：不走真实内核事件，直接把按钮投给已注入的
    ''' <see cref="命令接收器"/>。FFF.Player.Tests 借此断言"按钮 → 接收器(=播放器方法)"
    ''' 的分派确实发生，而不是只看能不能建会话。生产路径仍由 OnNativeButton 触发。</summary>
    Friend Sub 测试用分派按钮(按钮 As 系统媒体按钮)
        Try
            If 命令接收器 IsNot Nothing Then
                命令接收器(按钮)
            End If
        Catch ex As Exception
            Debug.WriteLine("SMTC：测试分派按钮回调失败（已忽略）：" & ex.GetType().Name)
        End Try
    End Sub

    ' ---- 发布队列：所有内核写操作都在 UI 线程 ----

    Private Sub 排队发布(项 As 系统媒体更新)
        If 已释放 OrElse 已关闭 Then Return
        SyncLock 待发布
            待发布.Enqueue(项)
        End SyncLock
        If Interlocked.Exchange(发布已排队, 1) = 0 Then
            If 同步上下文 IsNot Nothing Then
                Try
                    同步上下文.Post(AddressOf 发布队列回调, Nothing)
                Catch ex As Exception
                    Debug.WriteLine("SMTC：Post 发布队列失败，改为同步发布：" & ex.GetType().Name)
                    发布队列回调(Nothing)
                End Try
            Else
                发布队列回调(Nothing)
            End If
        End If
    End Sub

    Private Sub 发布队列回调(状态 As Object)
        Interlocked.Exchange(发布已排队, 0)
        If 已释放 OrElse 已关闭 Then Return
        Do
            Dim 项 As 系统媒体更新
            SyncLock 待发布
                If 待发布.Count = 0 Then Exit Do
                项 = 待发布.Dequeue()
            End SyncLock
            Try
                应用更新(项)
            Catch ex As Exception
                Debug.WriteLine("SMTC：应用发布项失败（已忽略）：" & ex.GetType().Name)
            End Try
        Loop
    End Sub

    Private Sub 应用更新(项 As 系统媒体更新)
        If Not 已启用 Then Return

        If 项.状态.HasValue Then
            If 项.状态.Value <> 上次播放状态 AndAlso 设状态Fn IsNot Nothing Then
                设状态Fn(CInt(项.状态.Value))
                上次播放状态 = 项.状态.Value
            End If
        End If

        If 项.媒体标识 IsNot Nothing OrElse 项.标题 IsNot Nothing OrElse
           项.艺术家 IsNot Nothing OrElse 项.专辑 IsNot Nothing OrElse
           项.专辑艺术家 IsNot Nothing OrElse 项.轨道号.HasValue OrElse
           项.显示类型.HasValue Then
            If 发卡片Fn IsNot Nothing Then
                Dim 类型 = If(项.显示类型.HasValue, 项.显示类型.Value,
                              If(上次显示类型.HasValue, 上次显示类型.Value, 系统媒体显示类型.音乐))
                发卡片Fn(If(项.媒体标识, String.Empty),
                         If(项.标题, String.Empty),
                         If(项.艺术家, String.Empty),
                         If(项.专辑, String.Empty),
                         If(项.专辑艺术家, String.Empty),
                         If(项.轨道号.HasValue, 项.轨道号.Value, 0UI),
                         CInt(类型))
                上次显示类型 = 类型
            End If
        End If

        If 项.时间轴.HasValue Then
            上次时间轴 = 项.时间轴.Value
        End If
        If 项.倍速.HasValue Then
            上次倍速 = 项.倍速.Value
        End If

        ' 时间轴和倍速合一次发布（内核 PublishTimeline 同时写 PlaybackRate）。
        If 项.时间轴.HasValue OrElse 项.倍速.HasValue Then
            If 发时间轴Fn IsNot Nothing AndAlso 上次时间轴.HasValue Then
                Dim t = 上次时间轴.Value
                Dim 可跳转 = If(t.MaxSeekTime > TimeSpan.Zero, 1, 0)
                发时间轴Fn(t.StartTime.TotalSeconds, t.EndTime.TotalSeconds,
                           t.Position.TotalSeconds, 上次倍速, 可跳转)
            End If
        End If
    End Sub

    ' ---- 内核导出解析：旧内核没有 Smtc 导出时静默降级 ----

    Private Function 解析内核导出() As Boolean
        If 库句柄 <> IntPtr.Zero Then Return True
        Try
            ' 先看内核是否已被 播放器会话/测试加载进进程；已加载就借句柄，
            ' 不增加 LoadLibrary 计数。
            库句柄 = GetModuleHandle(动态库名称)
            If 库句柄 <> IntPtr.Zero Then
                Debug.WriteLine("SMTC：内核模块已在进程中（GetModuleHandle 命中）")
            Else
                ' 测试进程（--smtc-runtime-smoke）不经过 播放器会话，FFF.Native.dll
                ' 还没被加载。按需 Load：NativeLibrary.TryLoad 走与单文件自解压
                ' 同一套 AssemblyLoadContext 解析路径，不会绕过宿主的 DLL 查找逻辑。
                If NativeLibrary.TryLoad(动态库名称, GetType(系统媒体会话).Assembly, Nothing, 库句柄) Then
                    Debug.WriteLine("SMTC：内核模块按需 NativeLibrary.TryLoad 成功")
                Else
                    Debug.WriteLine("SMTC：内核模块既未加载也无法按需加载（降级为无系统媒体卡）")
                    Return False
                End If
            End If

            初始化Fn = 取委托(Of FFF3FP_SmtcInitializeDelegate)("FFF3FP_SmtcInitialize")
            关闭Fn = 取委托(Of FFF3FP_SmtcShutdownDelegate)("FFF3FP_SmtcShutdown")
            设状态Fn = 取委托(Of FFF3FP_SmtcSetPlaybackStatusDelegate)("FFF3FP_SmtcSetPlaybackStatus")
            发卡片Fn = 取委托(Of FFF3FP_SmtcPublishCardDelegate)("FFF3FP_SmtcPublishCard")
            发缩略图Fn = 取委托(Of FFF3FP_SmtcPublishThumbnailDelegate)("FFF3FP_SmtcPublishThumbnail")
            清卡片Fn = 取委托(Of FFF3FP_SmtcClearCardDelegate)("FFF3FP_SmtcClearCard")
            发时间轴Fn = 取委托(Of FFF3FP_SmtcPublishTimelineDelegate)("FFF3FP_SmtcPublishTimeline")
            设按钮Fn = 取委托(Of FFF3FP_SmtcSetButtonEnabledDelegate)("FFF3FP_SmtcSetButtonEnabled")
            设回调Fn = 取委托(Of FFF3FP_SmtcSetCallbacksDelegate)("FFF3FP_SmtcSetCallbacks")

            ' 任一关键导出缺失就视为内核不支持 SMTC。
            If 初始化Fn Is Nothing OrElse 设回调Fn Is Nothing OrElse
               发卡片Fn Is Nothing OrElse 发时间轴Fn Is Nothing Then
                Debug.WriteLine("SMTC：内核模块已加载，但 FFF3FP_Smtc* 关键导出缺失（旧内核？降级）")
                Return False
            End If
            Return True
        Catch ex As Exception
            Debug.WriteLine("SMTC：解析内核导出失败（已忽略，降级）：" & ex.GetType().Name & " - " & ex.Message)
            Return False
        End Try
    End Function

    Private Function 取委托(Of T As Class)(符号 As String) As T
        If 库句柄 = IntPtr.Zero Then Return Nothing
        Try
            Dim 地址 As IntPtr
            ' 用 NativeLibrary.TryGetExport 而不是 GetProcAddress：前者在
            ' NativeLibrary.TryLoad 加载的库上同样可用，且不依赖 ANSI 串约定。
            If Not NativeLibrary.TryGetExport(库句柄, 符号, 地址) Then Return Nothing
            If 地址 = IntPtr.Zero Then Return Nothing
            ' 用泛型重载 GetDelegateForFunctionPointer(Of T)，避免 VB 不允许的
            ' CType(System.Delegate, T) 反射写法（BC30311）。
            Return Marshal.GetDelegateForFunctionPointer(Of T)(地址)
        Catch
            Return Nothing
        End Try
    End Function

    <DllImport("kernel32.dll", CharSet:=CharSet.Unicode, SetLastError:=True)>
    Private Shared Function GetModuleHandle(模块名 As String) As IntPtr
    End Function

    ''' <summary>内部发布项；字段为 Nothing 表示本次不改。</summary>
    Private Structure 系统媒体更新
        Public 状态 As 系统媒体播放状态?
        Public 媒体标识 As String
        Public 标题 As String
        Public 艺术家 As String
        Public 专辑 As String
        Public 专辑艺术家 As String
        Public 轨道号 As UInteger?
        Public 显示类型 As 系统媒体显示类型?
        Public 时间轴 As 系统媒体时间轴?
        Public 倍速 As Double?
    End Structure

End Class

''' <summary>时间轴发布值；全部用 TimeSpan，避免秒/毫秒单位在宿主与内核之间漂移。</summary>
Public Structure 系统媒体时间轴
    Public StartTime As TimeSpan
    Public EndTime As TimeSpan
    Public Position As TimeSpan
    Public MinSeekTime As TimeSpan
    Public MaxSeekTime As TimeSpan
End Structure
