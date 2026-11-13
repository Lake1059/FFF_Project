Imports System.Runtime.InteropServices

' 外部模块门面：只保留中性入口，模块实现与注入在外部 DLL。

Friend Enum 外部授权状态 As Integer
    不可用 = 0
    已锁定 = 1
    就绪 = 2
End Enum

Friend Enum 外部解锁结果 As Integer
    被拒 = 0
    已接受 = 1
    已接受未落盘 = 2
    ''' <summary>模块缺席、没有 Authorization 能力或返回未知值：不是“码不对”，不能再让用户重试。</summary>
    无法提交 = 3
End Enum

Friend Enum 外部授权文本用途 As UInteger
    未授权解释 = 1UI
    对话框标签 = 2UI
    票据存不下 = 3UI
End Enum

''' <summary>descriptor.capabilities 的位掩码。</summary>
Friend Enum 外部模块能力位 As UInteger
    授权 = 1UI << 0
    音频渲染 = 1UI << 1
    音频直通 = 1UI << 2
    处理器 = 1UI << 3
    色彩分析 = 1UI << 4
End Enum

''' <summary>getCapability 的能力编号。</summary>
Friend Enum 外部模块能力编号 As UInteger
    授权 = 1UI
    音频渲染 = 2UI
    音频直通 = 3UI
    处理器 = 4UI
    色彩分析 = 5UI
End Enum

''' <summary>注入泵快照（184 B）。</summary>
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

<StructLayout(LayoutKind.Sequential)>
Friend Structure 外部模块描述符原生
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public Id As IntPtr
    Public 显示名称 As IntPtr
    Public 能力 As UInteger
    Public 需求 As UInteger
    Public 标志 As UInteger
End Structure

''' <summary>托管侧描述符：字符串已从模块内存立即拷贝，不保留跨调用指针。</summary>
Friend Structure 外部模块描述符
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public Id As String
    Public 显示名称 As String
    Public 能力 As UInteger
    Public 需求 As UInteger
    Public 标志 As UInteger
End Structure

<StructLayout(LayoutKind.Sequential)>
Friend Structure 外部模块Api
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public 上下文 As IntPtr
    Public 取描述符 As IntPtr
    Public 取能力 As IntPtr
End Structure

<StructLayout(LayoutKind.Sequential)>
Friend Structure 外部授权Api
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public 上下文 As IntPtr
    Public 取状态 As IntPtr
    Public 取文本 As IntPtr
    Public 提交 As IntPtr
End Structure

<StructLayout(LayoutKind.Sequential)>
Friend Structure 外部处理器状态
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public 运行 As UInteger
    Public 已处理帧数 As ULong
    Public 未变帧数 As ULong
    Public 跳过帧数 As ULong
    Public 无据帧数 As ULong
    Public 末帧帧号 As Long
    Public 末帧峰值尼特 As Single
    <MarshalAs(UnmanagedType.ByValArray, SizeConst:=64)> Public 状态文本字节 As Byte()
    <MarshalAs(UnmanagedType.ByValArray, SizeConst:=64)> Public 详情文本字节 As Byte()
End Structure

<StructLayout(LayoutKind.Sequential)>
Friend Structure 外部处理器Api
    Public 大小 As UInteger
    Public 版本 As UInteger
    Public 上下文 As IntPtr
    Public 启动 As IntPtr
    Public 停止 As IntPtr
    Public 取状态 As IntPtr
End Structure

''' <summary>外部床渲染表镜像（56 B）。</summary>
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

''' <summary>外部音频直通表镜像（48 B）。</summary>
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

<UnmanagedFunctionPointer(CallingConvention.Cdecl)>
Friend Delegate Function 取外部模块Api原型(abiVersion As UInteger, ByRef outApi As 外部模块Api) As Integer

<UnmanagedFunctionPointer(CallingConvention.Cdecl)>
Friend Delegate Function 取外部描述符原型(context As IntPtr, ByRef outDescriptor As 外部模块描述符原生) As Integer

<UnmanagedFunctionPointer(CallingConvention.Cdecl)>
Friend Delegate Function 取外部能力原型(context As IntPtr, capability As UInteger,
                                           version As UInteger, ByRef outTable As IntPtr) As Integer

<UnmanagedFunctionPointer(CallingConvention.Cdecl)>
Friend Delegate Function 外部授权取状态原型(context As IntPtr) As Integer

<UnmanagedFunctionPointer(CallingConvention.Cdecl)>
Friend Delegate Function 外部授权取文本原型(context As IntPtr, 用途 As UInteger) As IntPtr

<UnmanagedFunctionPointer(CallingConvention.Cdecl)>
Friend Delegate Function 外部授权提交原型(context As IntPtr, 代码UTF8 As IntPtr) As Integer

<UnmanagedFunctionPointer(CallingConvention.Cdecl)>
Friend Delegate Function 外部处理器启动原型(context As IntPtr, 会话句柄 As IntPtr) As Integer

<UnmanagedFunctionPointer(CallingConvention.Cdecl)>
Friend Delegate Sub 外部处理器停止原型(context As IntPtr)

<UnmanagedFunctionPointer(CallingConvention.Cdecl)>
Friend Delegate Sub 外部处理器取状态原型(context As IntPtr, ByRef 状态 As 外部处理器状态)

''' <summary>中性外部模块实例的统一接口。</summary>
Friend Interface 外部模块实例
    Inherits IDisposable

    ReadOnly Property 描述符 As 外部模块描述符
    ReadOnly Property 处理器可用 As Boolean
    ReadOnly Property 音频表所有权已转移 As Boolean
    ReadOnly Property 直通表所有权已转移 As Boolean
    Sub 标记音频表所有权已转移()
    Sub 标记直通表所有权已转移()
    Function 尝试取音频渲染表(ByRef 原因 As String) As Nullable(Of 音频渲染能力表)
    Function 尝试取音频直通表(ByRef 原因 As String) As Nullable(Of 音频直通能力表)
    Function 启动处理器(会话句柄 As IntPtr) As Boolean
    Sub 停止处理器(Optional 会话句柄 As IntPtr = Nothing)
    Function 读取注入状态(ByRef 状态 As 注入状态) As Boolean
    Function 授权状态() As 外部授权状态
    Function 授权文本(用途 As 外部授权文本用途, Optional 目录覆盖 As String = Nothing) As String
    Function 提交解锁码(码 As String, ByRef 详情 As String) As 外部解锁结果
End Interface

''' <summary>中性入口模块实现：只认 descriptor + getCapability，不认旧导出名。</summary>
Friend Class NeutralExternalModule
    Implements 外部模块实例, IDisposable

    Private ReadOnly _模块 As IntPtr
    Private ReadOnly _api As 外部模块Api
    Private ReadOnly _描述符 As 外部模块描述符
    Private ReadOnly _取描述符 As 取外部描述符原型
    Private ReadOnly _取能力 As 取外部能力原型

    Private _授权 As Nullable(Of 外部授权Api)
    Private _授权取状态 As 外部授权取状态原型
    Private _授权取文本 As 外部授权取文本原型
    Private _授权提交 As 外部授权提交原型

    Private _处理器 As Nullable(Of 外部处理器Api)
    Private _处理器启动 As 外部处理器启动原型
    Private _处理器停止 As 外部处理器停止原型
    Private _处理器取状态 As 外部处理器取状态原型

    Private _音频表 As Nullable(Of 音频渲染能力表)
    Private _直通表 As Nullable(Of 音频直通能力表)

    Private _音频所有权 As Boolean
    Private _直通所有权 As Boolean
    Private ReadOnly _锁 As New Object()
    Private _已释放 As Boolean

    Friend Sub New(模块 As IntPtr, api As 外部模块Api, 描述符 As 外部模块描述符)
        _模块 = 模块
        _api = api
        _描述符 = 描述符
        _取描述符 = CType(Marshal.GetDelegateForFunctionPointer(api.取描述符, GetType(取外部描述符原型)), 取外部描述符原型)
        _取能力 = CType(Marshal.GetDelegateForFunctionPointer(api.取能力, GetType(取外部能力原型)), 取外部能力原型)
        尝试绑定授权()
        尝试绑定处理器()
    End Sub

    Public ReadOnly Property 描述符 As 外部模块描述符 Implements 外部模块实例.描述符
        Get
            Return _描述符
        End Get
    End Property

    Public ReadOnly Property 处理器可用 As Boolean Implements 外部模块实例.处理器可用
        Get
            Return _处理器.HasValue
        End Get
    End Property

    Public ReadOnly Property 音频表所有权已转移 As Boolean Implements 外部模块实例.音频表所有权已转移
        Get
            Return _音频所有权
        End Get
    End Property

    Public ReadOnly Property 直通表所有权已转移 As Boolean Implements 外部模块实例.直通表所有权已转移
        Get
            Return _直通所有权
        End Get
    End Property

    Public Sub 标记音频表所有权已转移() Implements 外部模块实例.标记音频表所有权已转移
        SyncLock _锁
            _音频所有权 = True
        End SyncLock
    End Sub

    Public Sub 标记直通表所有权已转移() Implements 外部模块实例.标记直通表所有权已转移
        SyncLock _锁
            _直通所有权 = True
        End SyncLock
    End Sub

    Private Sub 尝试绑定授权()
        If (_描述符.能力 And CUInt(外部模块能力位.授权)) = 0UI Then Return
        Dim 指针 As IntPtr = IntPtr.Zero
        If Not Try取能力(CUInt(外部模块能力编号.授权), 1UI, 指针) Then Return
        Dim 表 = CType(Marshal.PtrToStructure(指针, GetType(外部授权Api)), 外部授权Api)
        If 表.大小 < CUInt(Marshal.SizeOf(Of 外部授权Api)()) OrElse 表.版本 <> 1UI OrElse
           表.上下文 = IntPtr.Zero OrElse 表.取状态 = IntPtr.Zero Then Return
        _授权 = 表
        _授权取状态 = CType(Marshal.GetDelegateForFunctionPointer(表.取状态, GetType(外部授权取状态原型)), 外部授权取状态原型)
        If 表.取文本 <> IntPtr.Zero Then
            _授权取文本 = CType(Marshal.GetDelegateForFunctionPointer(表.取文本, GetType(外部授权取文本原型)), 外部授权取文本原型)
        End If
        If 表.提交 <> IntPtr.Zero Then
            _授权提交 = CType(Marshal.GetDelegateForFunctionPointer(表.提交, GetType(外部授权提交原型)), 外部授权提交原型)
        End If
    End Sub

    Private Sub 尝试绑定处理器()
        If (_描述符.能力 And CUInt(外部模块能力位.处理器)) = 0UI Then Return
        Dim 指针 As IntPtr = IntPtr.Zero
        If Not Try取能力(CUInt(外部模块能力编号.处理器), 1UI, 指针) Then Return
        Dim 表 = CType(Marshal.PtrToStructure(指针, GetType(外部处理器Api)), 外部处理器Api)
        If 表.大小 < CUInt(Marshal.SizeOf(Of 外部处理器Api)()) OrElse 表.版本 <> 1UI OrElse
           表.上下文 = IntPtr.Zero OrElse 表.启动 = IntPtr.Zero OrElse
           表.停止 = IntPtr.Zero OrElse 表.取状态 = IntPtr.Zero Then Return
        _处理器 = 表
        _处理器启动 = CType(Marshal.GetDelegateForFunctionPointer(表.启动, GetType(外部处理器启动原型)), 外部处理器启动原型)
        _处理器停止 = CType(Marshal.GetDelegateForFunctionPointer(表.停止, GetType(外部处理器停止原型)), 外部处理器停止原型)
        _处理器取状态 = CType(Marshal.GetDelegateForFunctionPointer(表.取状态, GetType(外部处理器取状态原型)), 外部处理器取状态原型)
    End Sub

    Private Function Try取能力(能力 As UInteger, 版本 As UInteger, ByRef 表指针 As IntPtr) As Boolean
        表指针 = IntPtr.Zero
        Try
            Return _取能力.Invoke(_api.上下文, 能力, 版本, 表指针) = 0 AndAlso 表指针 <> IntPtr.Zero
        Catch
            Return False
        End Try
    End Function

    Public Function 尝试取音频渲染表(ByRef 原因 As String) As Nullable(Of 音频渲染能力表) Implements 外部模块实例.尝试取音频渲染表
        原因 = Nothing
        SyncLock _锁
            If _已释放 Then Return Nothing
            If (_描述符.能力 And CUInt(外部模块能力位.音频渲染)) = 0UI Then
                原因 = "descriptor 未声明 AudioRender 能力"
                Return Nothing
            End If
            Dim 指针 As IntPtr = IntPtr.Zero
            If Not Try取能力(CUInt(外部模块能力编号.音频渲染), 1UI, 指针) Then
                原因 = "getCapability(AudioRender) 失败"
                Return Nothing
            End If
            Dim 表 As 音频渲染能力表
            Try
                表 = CType(Marshal.PtrToStructure(指针, GetType(音频渲染能力表)), 音频渲染能力表)
            Catch ex As Exception
                原因 = "AudioRender 表读取异常 " & ex.GetType().Name
                Return Nothing
            End Try
            If 表.大小 < 56UI OrElse 表.版本 <> 1UI OrElse 表.上下文 = IntPtr.Zero OrElse
               表.支持 = IntPtr.Zero OrElse 表.渲染 = IntPtr.Zero OrElse 表.复位 = IntPtr.Zero Then
                原因 = "AudioRender 表不合格（size=" & 表.大小.ToString() & " version=" & 表.版本.ToString() & "）"
                Return Nothing
            End If
            ' 内核撤表会销毁 context，每次交付必须重新握手。
            _音频表 = 表
            Return _音频表
        End SyncLock
    End Function

    Public Function 尝试取音频直通表(ByRef 原因 As String) As Nullable(Of 音频直通能力表) Implements 外部模块实例.尝试取音频直通表
        原因 = Nothing
        SyncLock _锁
            If _已释放 Then Return Nothing
            If (_描述符.能力 And CUInt(外部模块能力位.音频直通)) = 0UI Then
                原因 = "descriptor 未声明 AudioPassthrough 能力"
                Return Nothing
            End If
            Dim 指针 As IntPtr = IntPtr.Zero
            If Not Try取能力(CUInt(外部模块能力编号.音频直通), 1UI, 指针) Then
                原因 = "getCapability(AudioPassthrough) 失败"
                Return Nothing
            End If
            Dim 表 As 音频直通能力表
            Try
                表 = CType(Marshal.PtrToStructure(指针, GetType(音频直通能力表)), 音频直通能力表)
            Catch ex As Exception
                原因 = "AudioPassthrough 表读取异常 " & ex.GetType().Name
                Return Nothing
            End Try
            If 表.大小 < 48UI OrElse 表.版本 <> 1UI OrElse 表.上下文 = IntPtr.Zero OrElse
               表.描述 = IntPtr.Zero OrElse 表.打包 = IntPtr.Zero OrElse 表.复位 = IntPtr.Zero Then
                原因 = "AudioPassthrough 表不合格（size=" & 表.大小.ToString() & " version=" & 表.版本.ToString() & "）"
                Return Nothing
            End If
            _直通表 = 表
            Return _直通表
        End SyncLock
    End Function

    Public Function 启动处理器(会话句柄 As IntPtr) As Boolean Implements 外部模块实例.启动处理器
        If 会话句柄 = IntPtr.Zero Then Return False
        If Not _处理器.HasValue Then Return False
        Dim 处理器 = _处理器
        SyncLock _锁
            If _已释放 Then Return False
            Try
                If _处理器启动.Invoke(处理器.Value.上下文, 会话句柄) = 0 Then Return True
                ' 重复 start 可能被模块拒绝；只有快照明确说运行中才按成功算。
                Dim 状态 As New 外部处理器状态()
                If 读取中性状态(状态) AndAlso 状态.运行 <> 0UI Then Return True
            Catch
            End Try
            Return False
        End SyncLock
    End Function

    Public Sub 停止处理器(Optional 会话句柄 As IntPtr = Nothing) Implements 外部模块实例.停止处理器
        If Not _处理器.HasValue Then Return
        Dim 处理器 = _处理器
        SyncLock _锁
            If _已释放 Then Return
            Try
                _处理器停止.Invoke(处理器.Value.上下文)
            Catch
            End Try
        End SyncLock
    End Sub

    Private Function 读取中性状态(ByRef 状态 As 外部处理器状态) As Boolean
        状态.大小 = CUInt(Marshal.SizeOf(Of 外部处理器状态)())
        状态.版本 = 1UI
        If Not _处理器.HasValue Then Return False
        Dim 处理器 = _处理器
        Try
            _处理器取状态.Invoke(处理器.Value.上下文, 状态)
            Return True
        Catch
            Return False
        End Try
    End Function

    Public Function 读取注入状态(ByRef 状态 As 注入状态) As Boolean Implements 外部模块实例.读取注入状态
        状态 = New 注入状态 With {.末帧帧号 = -1L}
        SyncLock _锁
            If _已释放 Then Return False
            Dim 中性 As New 外部处理器状态()
            If Not 读取中性状态(中性) Then Return False
            状态.大小 = CUInt(Marshal.SizeOf(Of 注入状态)())
            状态.运行 = 中性.运行
            状态.注入帧数 = 中性.已处理帧数
            状态.未变帧数 = 中性.未变帧数
            状态.跳过帧数 = 中性.跳过帧数
            状态.无据帧数 = 中性.无据帧数
            状态.末帧帧号 = 中性.末帧帧号
            状态.末帧峰值尼特 = 中性.末帧峰值尼特
            If 中性.状态文本字节 IsNot Nothing Then
                状态.内容文本字节 = DirectCast(中性.状态文本字节.Clone(), Byte())
            End If
            If 中性.详情文本字节 IsNot Nothing Then
                状态.合成文本字节 = DirectCast(中性.详情文本字节.Clone(), Byte())
            End If
            Return True
        End SyncLock
    End Function

    Public Function 授权状态() As 外部授权状态 Implements 外部模块实例.授权状态
        SyncLock _锁
            If _已释放 Then Return 外部授权状态.不可用
            If Not _授权.HasValue Then
                ' 没有 Authorization 能力 = 不需要激活。
                Return If(_处理器.HasValue OrElse (_描述符.能力 And CUInt(外部模块能力位.音频渲染)) <> 0UI,
                          外部授权状态.就绪, 外部授权状态.不可用)
            End If
            Try
                Select Case _授权取状态.Invoke(_授权.Value.上下文)
                    Case 1
                        Return 外部授权状态.已锁定
                    Case 2
                        Return 外部授权状态.就绪
                    Case Else
                        Return 外部授权状态.不可用
                End Select
            Catch
                Return 外部授权状态.不可用
            End Try
        End SyncLock
    End Function

    Public Function 授权文本(用途 As 外部授权文本用途, Optional 目录覆盖 As String = Nothing) As String Implements 外部模块实例.授权文本
        SyncLock _锁
            If _已释放 Then Return String.Empty
            If Not _授权.HasValue OrElse _授权取文本 Is Nothing Then Return String.Empty
            Try
                Dim 指针 = _授权取文本.Invoke(_授权.Value.上下文, CUInt(用途))
                If 指针 = IntPtr.Zero Then Return String.Empty
                Return Marshal.PtrToStringUTF8(指针)
            Catch
                Return String.Empty
            End Try
        End SyncLock
    End Function

    Public Function 提交解锁码(码 As String, ByRef 详情 As String) As 外部解锁结果 Implements 外部模块实例.提交解锁码
        详情 = Nothing
        SyncLock _锁
            If _已释放 Then
                详情 = "模块实例已释放"
                Return 外部解锁结果.无法提交
            End If
            If Not _授权.HasValue OrElse _授权提交 Is Nothing Then
                详情 = "模块没有可用的 Authorization 能力"
                Return 外部解锁结果.无法提交
            End If
            Dim 文本句柄 As IntPtr = IntPtr.Zero
            Try
                文本句柄 = Marshal.StringToCoTaskMemUTF8(If(码, String.Empty))
                Select Case _授权提交.Invoke(_授权.Value.上下文, 文本句柄)
                    Case 1
                        Return 外部解锁结果.已接受
                    Case 2
                        Return 外部解锁结果.已接受未落盘
                    Case 0
                        Return 外部解锁结果.被拒
                    Case Else
                        详情 = "模块返回未知解锁结果"
                        Return 外部解锁结果.无法提交
                End Select
            Catch ex As Exception
                详情 = "提交异常 " & ex.GetType().Name
                Return 外部解锁结果.无法提交
            Finally
                If 文本句柄 <> IntPtr.Zero Then Marshal.FreeCoTaskMem(文本句柄)
            End Try
        End SyncLock
    End Function

    Public Sub Dispose() Implements IDisposable.Dispose
        Dim 需要保留 As Boolean
        SyncLock _锁
            If _已释放 Then Return
            _已释放 = True
            需要保留 = _音频所有权 OrElse _直通所有权
        End SyncLock
        If 需要保留 Then Return
        If _模块 <> IntPtr.Zero Then
            Try
                FreeLibrary(_模块)
            Catch ex As DllNotFoundException
            End Try
        End If
    End Sub
End Class

' 中性入口装载与通用目录发现

Friend Module 外部模块接口

    ''' <summary>把状态里一段 NUL 结尾的 UTF-8 文本拷成托管串（空段/缺省答空串）。</summary>
    Friend Function 注入文本(字节 As Byte()) As String
        If 字节 Is Nothing Then Return String.Empty
        Dim 结束 = Array.IndexOf(字节, CByte(0))
        If 结束 < 0 Then 结束 = 字节.Length
        If 结束 <= 0 Then Return String.Empty
        Return System.Text.Encoding.UTF8.GetString(字节, 0, 结束)
    End Function

    Friend Const 中性入口名 As String = "FFF3FP_ExternalModuleGetApi"
    Friend Const 中性Abi版本 As UInteger = 1UI
    Friend Const 模块文件名环境变量 As String = "FFF3FP_EXTERNAL_MODULE_FILE"
    Friend Const 模块目录环境变量 As String = "FFF3FP_MODULE_DIR"

    Friend Enum 模块在场形态
        缺席 = 0
        已锁定 = 2
        就绪 = 3
    End Enum

    Friend Enum 解锁结果
        被拒 = 0
        已接受 = 1
        已接受未落盘 = 2
        ''' <summary>没提交到模块 / 没有解锁入口 / 模块返回未知值：与“码不对”分开。</summary>
        无法提交 = 3
    End Enum

    Private Const 改变搜索路径旗标 As UInteger = &H8UI

    <DllImport("kernel32.dll", CharSet:=CharSet.Unicode, SetLastError:=True)>
    Private Function LoadLibraryExW(文件名 As String, 文件 As IntPtr, 旗标 As UInteger) As IntPtr
    End Function

    <DllImport("kernel32.dll", SetLastError:=True)>
    Private Function GetProcAddress(模块 As IntPtr, 名称 As String) As IntPtr
    End Function

    <DllImport("kernel32.dll", SetLastError:=True)>
    Friend Function FreeLibrary(模块 As IntPtr) As Boolean
    End Function

    Private ReadOnly 中性保留锁 As New Object()
    Private ReadOnly 保留的中性模块 As New List(Of NeutralExternalModule)

    Friend Function 探测(ByRef 原因 As String, Optional 目录覆盖 As String = Nothing) As 模块在场形态
        原因 = Nothing
        Dim 中性原因 As String = Nothing
        Dim 中性 = 尝试装载中性模块(中性原因, 目录覆盖)
        If 中性 Is Nothing Then
            原因 = 中性原因
            Return 模块在场形态.缺席
        End If
        Try
            Select Case 中性.授权状态()
                Case 外部授权状态.已锁定
                    Return 模块在场形态.已锁定
                Case 外部授权状态.就绪
                    Return 模块在场形态.就绪
                Case Else
                    If 中性.处理器可用 Then Return 模块在场形态.就绪
                    Return 模块在场形态.缺席
            End Select
        Finally
            中性.Dispose()
        End Try
    End Function

    Friend Function 尝试装载(ByRef 原因 As String, Optional 目录覆盖 As String = Nothing) As 外部模块实例
        原因 = Nothing
        Dim 中性原因 As String = Nothing
        Dim 中性 = 尝试装载中性模块(中性原因, 目录覆盖)
        If 中性 Is Nothing Then 原因 = 中性原因
        Return 中性
    End Function

    Friend Function 模块在盘上(Optional 目录覆盖 As String = Nothing) As Boolean
        Dim 原因 As String = Nothing
        Dim 中性 = 尝试装载中性模块(原因, 目录覆盖)
        If 中性 Is Nothing Then Return False
        中性.Dispose()
        Return True
    End Function

    Friend Function 显示名称(Optional 目录覆盖 As String = Nothing) As String
        Dim 原因 As String = Nothing
        Dim 中性 = 尝试装载中性模块(原因, 目录覆盖)
        If 中性 Is Nothing Then Return "可选扩展模块"
        Try
            If Not String.IsNullOrWhiteSpace(中性.描述符.显示名称) Then Return 中性.描述符.显示名称
        Finally
            中性.Dispose()
        End Try
        Return "可选扩展模块"
    End Function

    Friend Function 提交解锁码(码 As String, ByRef 详情 As String,
                              Optional 目录覆盖 As String = Nothing) As 解锁结果
        详情 = Nothing
        Dim 原因 As String = Nothing
        Dim 中性 = 尝试装载中性模块(原因, 目录覆盖)
        If 中性 Is Nothing Then
            详情 = "外部模块未装载，无法提交解锁码"
            Return 解锁结果.无法提交
        End If
        Dim 保留实例 As Boolean = False
        Try
            Select Case 中性.提交解锁码(码, 详情)
                Case 外部解锁结果.已接受
                    Return 解锁结果.已接受
                Case 外部解锁结果.已接受未落盘
                    保留实例 = True
                    SyncLock 中性保留锁
                        保留的中性模块.Add(中性)
                    End SyncLock
                    Return 解锁结果.已接受未落盘
                Case 外部解锁结果.无法提交
                    Return 解锁结果.无法提交
                Case Else
                    Return 解锁结果.被拒
            End Select
        Finally
            If Not 保留实例 Then 中性.Dispose()
        End Try
    End Function

    Friend Function 取状态文本(用途 As 外部授权文本用途, Optional 目录覆盖 As String = Nothing) As String
        Dim 原因 As String = Nothing
        Dim 中性 = 尝试装载中性模块(原因, 目录覆盖)
        If 中性 Is Nothing Then Return String.Empty
        Try
            Return 中性.授权文本(用途, 目录覆盖)
        Finally
            中性.Dispose()
        End Try
    End Function
    ''' <summary>装载中性入口。失败返回 Nothing，原因只进日志。</summary>
    Friend Function 尝试装载中性模块(ByRef 原因 As String,
                                      Optional 目录覆盖 As String = Nothing) As NeutralExternalModule
        原因 = Nothing
        For Each 候选 In 中性候选路径(目录覆盖)
            Dim 句柄 As IntPtr = IntPtr.Zero
            Try
                句柄 = LoadLibraryExW(候选, IntPtr.Zero, 改变搜索路径旗标)
                If 句柄 = IntPtr.Zero Then
                    原因 = 候选 & " LoadLibrary 失败"
                    Continue For
                End If
                Dim 入口 = GetProcAddress(句柄, 中性入口名)
                If 入口 = IntPtr.Zero Then
                    原因 = 候选 & " 不含 " & 中性入口名
                    Continue For
                End If
                Dim 取Api = CType(Marshal.GetDelegateForFunctionPointer(入口, GetType(取外部模块Api原型)), 取外部模块Api原型)
                Dim api As New 外部模块Api With {
                    .大小 = CUInt(Marshal.SizeOf(Of 外部模块Api)()),
                    .版本 = 0UI
                }
                If 取Api.Invoke(中性Abi版本, api) <> 0 Then
                    原因 = 候选 & " 的 " & 中性入口名 & " 拒绝 ABI " & 中性Abi版本.ToString()
                    Continue For
                End If
                If api.大小 < CUInt(Marshal.SizeOf(Of 外部模块Api)()) OrElse api.版本 <> 中性Abi版本 OrElse
                   api.上下文 = IntPtr.Zero OrElse api.取描述符 = IntPtr.Zero OrElse api.取能力 = IntPtr.Zero Then
                    原因 = 候选 & " 的模块 Api 不合格"
                    Continue For
                End If

                Dim 取描述符 = CType(Marshal.GetDelegateForFunctionPointer(api.取描述符, GetType(取外部描述符原型)), 取外部描述符原型)
                Dim 原生 As New 外部模块描述符原生 With {
                    .大小 = CUInt(Marshal.SizeOf(Of 外部模块描述符原生)()),
                    .版本 = 0UI
                }
                If 取描述符.Invoke(api.上下文, 原生) <> 0 Then
                    原因 = 候选 & " 的 getDescriptor 失败"
                    Continue For
                End If
                If 原生.大小 < CUInt(Marshal.SizeOf(Of 外部模块描述符原生)()) OrElse 原生.版本 <> 1UI Then
                    原因 = 候选 & " 的 descriptor 尺寸/版本不合格"
                    Continue For
                End If
                Dim 描述符 As New 外部模块描述符 With {
                    .大小 = 原生.大小,
                    .版本 = 原生.版本,
                    .Id = If(原生.Id = IntPtr.Zero, "", Marshal.PtrToStringUTF8(原生.Id)),
                    .显示名称 = If(原生.显示名称 = IntPtr.Zero, "", Marshal.PtrToStringUTF8(原生.显示名称)),
                    .能力 = 原生.能力,
                    .需求 = 原生.需求,
                    .标志 = 原生.标志
                }
                Dim 模块 = New NeutralExternalModule(句柄, api, 描述符)
                ' 句柄所有权移交给实例；成功返回后不再走 Finally 的释放。
                句柄 = IntPtr.Zero
                Return 模块
            Catch ex As Exception
                原因 = "中性模块装载异常 " & ex.GetType().Name & ": " & ex.Message
            Finally
                If 句柄 <> IntPtr.Zero Then FreeLibrary(句柄)
            End Try
        Next
        If String.IsNullOrEmpty(原因) Then 原因 = "未找到中性外部模块"
        Return Nothing
    End Function

    Private Iterator Function 中性候选路径(目录覆盖 As String) As IEnumerable(Of String)
        Dim 已见 As New HashSet(Of String)(StringComparer.OrdinalIgnoreCase)
        Dim 目录列表 = 模块目录列表(目录覆盖)
        Dim 文件名 = Environment.GetEnvironmentVariable(模块文件名环境变量)

        If Not String.IsNullOrWhiteSpace(文件名) Then
            If Not 文件名.EndsWith(".dll", StringComparison.OrdinalIgnoreCase) Then 文件名 &= ".dll"
            For Each 目录 In 目录列表
                Dim 完整 = 尝试完整路径(目录, 文件名)
                If 完整 IsNot Nothing AndAlso 已见.Add(完整) Then Yield 完整
            Next
            Return
        End If

        For Each 目录 In 目录列表
            If Not IO.Directory.Exists(目录) Then Continue For
            Dim 文件列表 As String()
            Try
                文件列表 = IO.Directory.GetFiles(目录, "*.dll", IO.SearchOption.TopDirectoryOnly)
            Catch
                Continue For
            End Try
            For Each 文件 In 文件列表
                Dim 完整 = 尝试完整路径(目录, IO.Path.GetFileName(文件))
                If 完整 IsNot Nothing AndAlso 已见.Add(完整) Then Yield 完整
            Next
        Next
    End Function

    Private Function 模块目录列表(目录覆盖 As String) As List(Of String)
        Dim 列表 As New List(Of String)
        添加目录(列表, 目录覆盖)
        添加目录(列表, Environment.GetEnvironmentVariable(模块目录环境变量))
        Dim 进程目录 = IO.Path.GetDirectoryName(Environment.ProcessPath)
        If Not String.IsNullOrWhiteSpace(进程目录) Then 添加目录(列表, IO.Path.Combine(进程目录, "Modules"))
        添加目录(列表, IO.Path.Combine(AppContext.BaseDirectory, "Modules"))
        Return 列表
    End Function

    Private Sub 添加目录(列表 As List(Of String), 目录 As String)
        If String.IsNullOrWhiteSpace(目录) Then Return
        Try
            列表.Add(IO.Path.GetFullPath(目录))
        Catch
        End Try
    End Sub

    Private Function 尝试完整路径(目录 As String, 文件名 As String) As String
        Try
            Dim 完整 = IO.Path.GetFullPath(IO.Path.Combine(目录, 文件名))
            If IO.File.Exists(完整) Then Return 完整
        Catch
        End Try
        Return Nothing
    End Function
End Module
