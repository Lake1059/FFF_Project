Imports System
Imports System.Collections.Generic
Imports System.Diagnostics
Imports System.Runtime.InteropServices
Imports FFF.Player

''' <summary>中性外部模块 ABI 的布局、描述符、授权、处理器与音频能力回归。</summary>
Friend Module 外部模块ABI回归

    Private 失败数 As Integer
    Private 用例数 As Integer

    Friend Function 运行() As Integer
        失败数 = 0
        用例数 = 0
        测结构布局()
        测模块缺席()
        测中性模块()
        测假模块分支()
        Console.WriteLine()
        Console.WriteLine($"外部模块 ABI 回归：{用例数} 条，失败 {失败数} 条")
        Return If(失败数 = 0, 0, 1)
    End Function

    Private Sub 测结构布局()
        检查整数("descriptor 原生 40 B", Marshal.SizeOf(Of 外部模块描述符原生)(), 40)
        检查整数("module api 32 B", Marshal.SizeOf(Of 外部模块Api)(), 32)
        检查整数("auth api 40 B", Marshal.SizeOf(Of 外部授权Api)(), 40)
        检查整数("processor api 40 B", Marshal.SizeOf(Of 外部处理器Api)(), 40)
        检查整数("processor status 192 B", Marshal.SizeOf(Of 外部处理器状态)(), 192)
        检查整数("audio render 56 B", Marshal.SizeOf(Of 音频渲染能力表)(), 56)
        检查整数("audio passthrough 48 B", Marshal.SizeOf(Of 音频直通能力表)(), 48)
        检查整数("injection status 184 B", Marshal.SizeOf(Of 注入状态)(), 184)
        检查整数("auxiliary composition 32 B", Marshal.SizeOf(Of 原生辅层合成)(), 32)
        检查整数("status.processedFrames offset", CInt(Marshal.OffsetOf(Of 外部处理器状态)(NameOf(外部处理器状态.已处理帧数))), 16)
        检查整数("status.lastFrameIndex offset", CInt(Marshal.OffsetOf(Of 外部处理器状态)(NameOf(外部处理器状态.末帧帧号))), 48)
        检查整数("status.lastPeakNits offset", CInt(Marshal.OffsetOf(Of 外部处理器状态)(NameOf(外部处理器状态.末帧峰值尼特))), 56)
        检查整数("status.statusText offset", CInt(Marshal.OffsetOf(Of 外部处理器状态)(NameOf(外部处理器状态.状态文本字节))), 60)
        检查整数("status.detailText offset", CInt(Marshal.OffsetOf(Of 外部处理器状态)(NameOf(外部处理器状态.详情文本字节))), 124)
        检查整数("audioRender.context offset", CInt(Marshal.OffsetOf(Of 音频渲染能力表)(NameOf(音频渲染能力表.上下文))), 8)
        检查整数("audioRender.name offset", CInt(Marshal.OffsetOf(Of 音频渲染能力表)(NameOf(音频渲染能力表.名称))), 16)
        检查整数("audioRender.support offset", CInt(Marshal.OffsetOf(Of 音频渲染能力表)(NameOf(音频渲染能力表.支持))), 24)
        检查整数("audioRender.render offset", CInt(Marshal.OffsetOf(Of 音频渲染能力表)(NameOf(音频渲染能力表.渲染))), 32)
        检查整数("audioRender.reset offset", CInt(Marshal.OffsetOf(Of 音频渲染能力表)(NameOf(音频渲染能力表.复位))), 40)
        检查整数("audioRender.free offset", CInt(Marshal.OffsetOf(Of 音频渲染能力表)(NameOf(音频渲染能力表.释放))) , 48)
        检查整数("audioPass.context offset", CInt(Marshal.OffsetOf(Of 音频直通能力表)(NameOf(音频直通能力表.上下文))), 8)
        检查整数("audioPass.describe offset", CInt(Marshal.OffsetOf(Of 音频直通能力表)(NameOf(音频直通能力表.描述))), 16)
        检查整数("audioPass.pack offset", CInt(Marshal.OffsetOf(Of 音频直通能力表)(NameOf(音频直通能力表.打包))), 24)
        检查整数("audioPass.reset offset", CInt(Marshal.OffsetOf(Of 音频直通能力表)(NameOf(音频直通能力表.复位))), 32)
        检查整数("audioPass.free offset", CInt(Marshal.OffsetOf(Of 音频直通能力表)(NameOf(音频直通能力表.释放))), 40)
        检查整数("aux.frame offset", CInt(Marshal.OffsetOf(Of 原生辅层合成)(NameOf(原生辅层合成.帧序号))), 8)
        检查整数("aux.enabled offset", CInt(Marshal.OffsetOf(Of 原生辅层合成)(NameOf(原生辅层合成.是否启用))), 16)
        检查整数("aux.max offset", CInt(Marshal.OffsetOf(Of 原生辅层合成)(NameOf(原生辅层合成.最大值))), 28)
    End Sub

    Private Sub 测模块缺席()
        Dim 旧目录 = Environment.GetEnvironmentVariable(外部模块接口.模块目录环境变量)
        Dim 旧文件 = Environment.GetEnvironmentVariable(外部模块接口.模块文件名环境变量)
        Dim 空目录 = IO.Path.Combine(IO.Path.GetTempPath(), "FFF3FP-empty-" & Guid.NewGuid().ToString("N"))
        Try
            Environment.SetEnvironmentVariable(外部模块接口.模块目录环境变量, 空目录)
            Environment.SetEnvironmentVariable(外部模块接口.模块文件名环境变量, Nothing)
            Dim 原因 As String = Nothing
            Dim 形态 = 外部模块接口.探测(原因)
            检查文本("模块缺席：形态", 形态.ToString(), 外部模块接口.模块在场形态.缺席.ToString())
            检查布尔("模块缺席：给出原因", String.IsNullOrWhiteSpace(原因), False)
        Finally
            Environment.SetEnvironmentVariable(外部模块接口.模块目录环境变量, 旧目录)
            Environment.SetEnvironmentVariable(外部模块接口.模块文件名环境变量, 旧文件)
        End Try
    End Sub

    Private Sub 测中性模块()
        Using 假 As New 假中性模块()
            检查文本("descriptor 显示名", 假.模块.描述符.显示名称, "测试扩展模块")
            检查文本("descriptor id", 假.模块.描述符.Id, "test.external.module")
            检查整数("能力位", CInt(假.模块.描述符.能力), 1 Or 2 Or 4 Or 8 Or 16)
            检查整数("锁定状态", CInt(假.模块.授权状态()), CInt(外部授权状态.已锁定))
            假.授权状态值 = 2
            检查整数("就绪状态", CInt(假.模块.授权状态()), CInt(外部授权状态.就绪))
            检查文本("授权文本", 假.模块.授权文本(外部授权文本用途.未授权解释), "模块原话")

            Dim 详情 As String = Nothing
            检查整数("解锁接受", CInt(假.模块.提交解锁码("good", 详情)), CInt(外部解锁结果.已接受))
            检查整数("解锁拒绝", CInt(假.模块.提交解锁码("bad", 详情)), CInt(外部解锁结果.被拒))

            检查布尔("处理器可用", 假.模块.处理器可用, True)
            检查布尔("启动处理器", 假.模块.启动处理器(New IntPtr(42)), True)
            Dim 状态 As New 注入状态()
            检查布尔("读取处理器状态", 假.模块.读取注入状态(状态), True)
            检查整数("运行标志", CInt(状态.运行), 1)
            检查无符号("已处理帧数", 状态.注入帧数, 7UL)
            检查长整数("末帧帧号", 状态.末帧帧号, 12L)
            检查文本("状态文本", 注入文本(状态.内容文本字节), "处理中")
            检查文本("详情文本", 注入文本(状态.合成文本字节), "已匹配")

            Dim 渲染 = 假.模块.尝试取音频渲染表(Nothing)
            检查布尔("渲染表存在", 渲染.HasValue, True)
            If 渲染.HasValue Then 检查整数("渲染表大小", CInt(渲染.Value.大小), 56)
            Dim 渲染2 = 假.模块.尝试取音频渲染表(Nothing)
            检查布尔("渲染表可重复取", 渲染2.HasValue, True)
            If 渲染2.HasValue Then
                检查整数("缓存表大小", CInt(渲染2.Value.大小), 56)
                检查整数("每次取表重新握手", If(渲染2.Value.上下文 <> 渲染.Value.上下文, 1, 0), 1)
            End If

            Dim 直通 = 假.模块.尝试取音频直通表(Nothing)
            检查布尔("直通表存在", 直通.HasValue, True)
            If 直通.HasValue Then 检查整数("直通表大小", CInt(直通.Value.大小), 48)
        End Using
    End Sub


    Private 原分支 As String
    Private 原文件名 As String

    Private Function 假模块目录() As String
        Dim 目录 = IO.Path.Combine(IO.Path.GetTempPath(), "FFF3FP-FakeModule")
        Dim 模块 = IO.Path.Combine(目录, "FFF.HDRdp.Test.dll")
        If IO.File.Exists(模块) Then Return 目录
        Return Nothing
    End Function

    Private Sub 设假模块环境(分支 As Integer, Optional 文件名 As String = "FFF.HDRdp.Test.dll")
        原分支 = Environment.GetEnvironmentVariable("FFF_TEST_MODULE_BRANCH")
        原文件名 = Environment.GetEnvironmentVariable(外部模块接口.模块文件名环境变量)
        Environment.SetEnvironmentVariable("FFF_TEST_MODULE_BRANCH", 分支.ToString())
        Environment.SetEnvironmentVariable(外部模块接口.模块文件名环境变量, 文件名)
    End Sub

    Private Sub 恢复假模块环境()
        Environment.SetEnvironmentVariable("FFF_TEST_MODULE_BRANCH", 原分支)
        Environment.SetEnvironmentVariable(外部模块接口.模块文件名环境变量, 原文件名)
    End Sub

    Private Function 装载假模块(分支 As Integer, ByRef 原因 As String,
                                Optional 文件名 As String = "FFF.HDRdp.Test.dll") As 外部模块实例
        设假模块环境(分支, 文件名)
        Try
            Return 外部模块接口.尝试装载(原因, 假模块目录())
        Finally
            恢复假模块环境()
        End Try
    End Function

    Private Sub 测假模块分支()
        Dim 目录 = 假模块目录()
        检查布尔("假模块已构建", 目录 IsNot Nothing, True)
        If 目录 Is Nothing Then Return

        Dim 原因 As String = Nothing
        Using 件 = 装载假模块(7, 原因)
            检查布尔("中性 Processor：装载", 件 IsNot Nothing, True)
            If 件 IsNot Nothing Then
                检查文本("中性 Processor：显示名", 件.描述符.显示名称, "假外部模块")
                检查布尔("中性 Processor：处理器可用", 件.处理器可用, True)
                检查布尔("中性 Processor：无渲染表", 件.尝试取音频渲染表(原因).HasValue, False)
                检查布尔("音频所有权初始未转移", 件.音频表所有权已转移, False)
            End If
        End Using

        原因 = Nothing
        Using 件 = 装载假模块(8, 原因)
            检查布尔("中性 Processor+Render：装载", 件 IsNot Nothing, True)
            If 件 IsNot Nothing Then
                检查布尔("中性 Processor+Render：处理器可用", 件.处理器可用, True)
                检查布尔("中性 Processor+Render：有渲染表", 件.尝试取音频渲染表(原因).HasValue, True)
                检查布尔("取表后所有权仍未转移", 件.音频表所有权已转移, False)
                件.标记音频表所有权已转移()
                检查布尔("标记后所有权已转移", 件.音频表所有权已转移, True)
            End If
        End Using

        Dim 旧目录 = Environment.GetEnvironmentVariable(外部模块接口.模块目录环境变量)
        Dim 旧文件 = Environment.GetEnvironmentVariable(外部模块接口.模块文件名环境变量)
        Dim 旧分支 = Environment.GetEnvironmentVariable("FFF_TEST_MODULE_BRANCH")
        Try
            Environment.SetEnvironmentVariable(外部模块接口.模块目录环境变量, 目录)
            Environment.SetEnvironmentVariable(外部模块接口.模块文件名环境变量, "FFF.HDRdp.Test.dll")
            Environment.SetEnvironmentVariable("FFF_TEST_MODULE_BRANCH", "9")
            原因 = Nothing
            检查文本("未落盘解锁：初始锁定", 外部模块接口.探测(原因).ToString(), 外部模块接口.模块在场形态.已锁定.ToString())
            Dim 详情 As String = Nothing
            检查整数("未落盘解锁：接受未落盘", CInt(外部模块接口.提交解锁码("good", 详情, 目录)), CInt(外部模块接口.解锁结果.已接受未落盘))
            原因 = Nothing
            检查文本("未落盘解锁：随后就绪", 外部模块接口.探测(原因).ToString(), 外部模块接口.模块在场形态.就绪.ToString())
        Finally
            Environment.SetEnvironmentVariable(外部模块接口.模块目录环境变量, 旧目录)
            Environment.SetEnvironmentVariable(外部模块接口.模块文件名环境变量, 旧文件)
            Environment.SetEnvironmentVariable("FFF_TEST_MODULE_BRANCH", 旧分支)
        End Try

        Dim 空目录 = IO.Path.Combine(AppContext.BaseDirectory, "_FFF3FP-MissingModuleDir")
        Try
            IO.Directory.CreateDirectory(空目录)
            Environment.SetEnvironmentVariable(外部模块接口.模块目录环境变量, 空目录)
            Environment.SetEnvironmentVariable(外部模块接口.模块文件名环境变量, "NoSuch.dll")
            Environment.SetEnvironmentVariable("FFF_TEST_MODULE_BRANCH", "1")
            Dim 详情 As String = Nothing
            检查整数("模块缺席提交返回无法提交", CInt(外部模块接口.提交解锁码("good", 详情, 空目录)),
                     CInt(外部模块接口.解锁结果.无法提交))
        Finally
            Try
                If IO.Directory.Exists(空目录) AndAlso IO.Directory.GetFiles(空目录).Length = 0 Then IO.Directory.Delete(空目录)
            Catch
            End Try
            Environment.SetEnvironmentVariable(外部模块接口.模块目录环境变量, 旧目录)
            Environment.SetEnvironmentVariable(外部模块接口.模块文件名环境变量, 旧文件)
            Environment.SetEnvironmentVariable("FFF_TEST_MODULE_BRANCH", 旧分支)
        End Try

        原因 = Nothing
        Using 件 = 装载假模块(7, 原因)
            检查文本("显式文件名：装载", If(件 Is Nothing, "", 件.描述符.Id), "test.fake.external")
        End Using

        原因 = Nothing
        Environment.SetEnvironmentVariable(外部模块接口.模块目录环境变量, 目录)
        Environment.SetEnvironmentVariable(外部模块接口.模块文件名环境变量, Nothing)
        Environment.SetEnvironmentVariable("FFF_TEST_MODULE_BRANCH", "7")
        Using 件 = 外部模块接口.尝试装载(原因, 目录)
            检查文本("目录枚举：装载", If(件 Is Nothing, "", 件.描述符.Id), "test.fake.external")
        End Using
        Environment.SetEnvironmentVariable(外部模块接口.模块目录环境变量, 旧目录)
        Environment.SetEnvironmentVariable(外部模块接口.模块文件名环境变量, 旧文件)
        Environment.SetEnvironmentVariable("FFF_TEST_MODULE_BRANCH", 旧分支)

        原因 = Nothing
        Environment.SetEnvironmentVariable(外部模块接口.模块目录环境变量, 目录)
        Environment.SetEnvironmentVariable(外部模块接口.模块文件名环境变量, "NoSuch.dll")
        Using 件 = 外部模块接口.尝试装载(原因, 目录)
            检查布尔("不存在的文件名：不装载", 件 Is Nothing, True)
        End Using
        Environment.SetEnvironmentVariable(外部模块接口.模块目录环境变量, 旧目录)
        Environment.SetEnvironmentVariable(外部模块接口.模块文件名环境变量, 旧文件)

        Environment.SetEnvironmentVariable("FFF_TEST_MODULE_BRANCH", "7")
        检查布尔("模块在盘上", 外部模块接口.模块在盘上(目录), True)
        检查文本("显示名称", 外部模块接口.显示名称(目录), "假外部模块")
        Environment.SetEnvironmentVariable("FFF_TEST_MODULE_BRANCH", 旧分支)
        测假模块生命周期(7)
    End Sub

    Private Sub 测假模块生命周期(分支 As Integer)
        Dim 次数 = 100
        Dim 成绩 = 0
        For i = 1 To 次数
            Dim 原因 As String = Nothing
            Dim 件 = 装载假模块(分支, 原因)
            If 件 IsNot Nothing Then
                件.Dispose()
                成绩 += 1
            End If
        Next
        GC.Collect()
        GC.WaitForPendingFinalizers()
        GC.Collect()
        检查整数($"中性生命周期 {次数} 次装载", 成绩, 次数)
    End Sub
    Private Sub 检查整数(名称 As String, 实际 As Integer, 期望 As Integer)
        用例数 += 1
        If 实际 = 期望 Then
            Console.WriteLine("ok    " & 名称)
        Else
            失败数 += 1
            Console.WriteLine("FAIL  " & 名称 & "：期望 " & 期望.ToString() & "，实际 " & 实际.ToString())
        End If
    End Sub

    Private Sub 检查无符号(名称 As String, 实际 As ULong, 期望 As ULong)
        用例数 += 1
        If 实际 = 期望 Then
            Console.WriteLine("ok    " & 名称)
        Else
            失败数 += 1
            Console.WriteLine("FAIL  " & 名称 & "：期望 " & 期望.ToString() & "，实际 " & 实际.ToString())
        End If
    End Sub

    Private Sub 检查长整数(名称 As String, 实际 As Long, 期望 As Long)
        用例数 += 1
        If 实际 = 期望 Then
            Console.WriteLine("ok    " & 名称)
        Else
            失败数 += 1
            Console.WriteLine("FAIL  " & 名称 & "：期望 " & 期望.ToString() & "，实际 " & 实际.ToString())
        End If
    End Sub

    Private Sub 检查布尔(名称 As String, 实际 As Boolean, 期望 As Boolean)
        用例数 += 1
        If 实际 = 期望 Then
            Console.WriteLine("ok    " & 名称)
        Else
            失败数 += 1
            Console.WriteLine("FAIL  " & 名称 & "：期望 " & 期望.ToString() & "，实际 " & 实际.ToString())
        End If
    End Sub

    Private Sub 检查文本(名称 As String, 实际 As String, 期望 As String)
        用例数 += 1
        If String.Equals(实际, 期望, StringComparison.Ordinal) Then
            Console.WriteLine("ok    " & 名称)
        Else
            失败数 += 1
            Console.WriteLine("FAIL  " & 名称 & "：期望『" & 期望 & "』，实际『" & If(实际, "<Nothing>") & "』")
        End If
    End Sub
End Module

''' <summary>用托管委托模拟一个中性模块：不依赖编译原生 DLL，只验证宿主侧绑定与映射。</summary>
Friend Class 假中性模块
    Implements IDisposable

    Friend Property 授权状态值 As Integer = 1

    Friend ReadOnly Property 模块 As NeutralExternalModule

    Private ReadOnly _保留结构 As New List(Of IntPtr)
    Private ReadOnly _保留文本 As New List(Of IntPtr)
    Private ReadOnly _取描述符 As 取外部描述符原型
    Private ReadOnly _取能力 As 取外部能力原型
    Private ReadOnly _取授权状态 As 外部授权取状态原型
    Private ReadOnly _取授权文本 As 外部授权取文本原型
    Private ReadOnly _提交授权 As 外部授权提交原型
    Private ReadOnly _启动处理器 As 外部处理器启动原型
    Private ReadOnly _停止处理器 As 外部处理器停止原型
    Private ReadOnly _取处理器状态 As 外部处理器取状态原型
    Private ReadOnly _授权表 As IntPtr
    Private ReadOnly _处理器表 As IntPtr
    Private ReadOnly _直通表 As IntPtr
    Private ReadOnly _文本指针 As IntPtr
    Private ReadOnly _状态文本 As Byte()
    Private ReadOnly _详情文本 As Byte()
    Private _处理器运行 As UInteger
    Private _渲染序号 As Integer

    Friend Sub New()
        _取描述符 = AddressOf 模拟取描述符
        _取能力 = AddressOf 模拟取能力
        _取授权状态 = AddressOf 模拟取授权状态
        _取授权文本 = AddressOf 模拟取授权文本
        _提交授权 = AddressOf 模拟提交授权
        _启动处理器 = AddressOf 模拟启动处理器
        _停止处理器 = AddressOf 模拟停止处理器
        _取处理器状态 = AddressOf 模拟取处理器状态

        _文本指针 = Marshal.StringToCoTaskMemUTF8("模块原话")
        _保留文本.Add(_文本指针)
        _状态文本 = 文本缓冲("处理中")
        _详情文本 = 文本缓冲("已匹配")

        Dim 授权 As New 外部授权Api With {
            .大小 = CUInt(Marshal.SizeOf(Of 外部授权Api)()),
            .版本 = 1UI,
            .上下文 = New IntPtr(42),
            .取状态 = Marshal.GetFunctionPointerForDelegate(_取授权状态),
            .取文本 = Marshal.GetFunctionPointerForDelegate(_取授权文本),
            .提交 = Marshal.GetFunctionPointerForDelegate(_提交授权)
        }
        _授权表 = 分配结构(授权)

        Dim 处理器 As New 外部处理器Api With {
            .大小 = CUInt(Marshal.SizeOf(Of 外部处理器Api)()),
            .版本 = 1UI,
            .上下文 = New IntPtr(43),
            .启动 = Marshal.GetFunctionPointerForDelegate(_启动处理器),
            .停止 = Marshal.GetFunctionPointerForDelegate(_停止处理器),
            .取状态 = Marshal.GetFunctionPointerForDelegate(_取处理器状态)
        }
        _处理器表 = 分配结构(处理器)

        Dim 直通 As New 音频直通能力表 With {
            .大小 = 48UI, .版本 = 1UI, .上下文 = New IntPtr(45),
            .描述 = New IntPtr(1), .打包 = New IntPtr(1), .复位 = New IntPtr(1),
            .释放 = New IntPtr(1)
        }
        _直通表 = 分配结构(直通)

        Dim 描述符 As New 外部模块描述符 With {
            .大小 = CUInt(Marshal.SizeOf(Of 外部模块描述符原生)()),
            .版本 = 1UI,
            .Id = "test.external.module",
            .显示名称 = "测试扩展模块",
            .能力 = CUInt(外部模块能力位.授权 Or 外部模块能力位.音频渲染 Or
                        外部模块能力位.音频直通 Or 外部模块能力位.处理器 Or
                        外部模块能力位.色彩分析),
            .需求 = 0UI,
            .标志 = 0UI
        }
        Dim api As New 外部模块Api With {
            .大小 = CUInt(Marshal.SizeOf(Of 外部模块Api)()),
            .版本 = 1UI,
            .上下文 = New IntPtr(41),
            .取描述符 = Marshal.GetFunctionPointerForDelegate(_取描述符),
            .取能力 = Marshal.GetFunctionPointerForDelegate(_取能力)
        }
        模块 = New NeutralExternalModule(IntPtr.Zero, api, 描述符)
    End Sub

    Private Function 分配结构(Of T)(值 As T) As IntPtr
        Dim 指针 = Marshal.AllocHGlobal(Marshal.SizeOf(Of T)())
        Marshal.StructureToPtr(值, 指针, False)
        _保留结构.Add(指针)
        Return 指针
    End Function

    Private Function 模拟取描述符(context As IntPtr, ByRef outDescriptor As 外部模块描述符原生) As Integer
        outDescriptor.大小 = CUInt(Marshal.SizeOf(Of 外部模块描述符原生)())
        outDescriptor.版本 = 1UI
        Return 0
    End Function

    Private Function 模拟取能力(context As IntPtr, capability As UInteger,
                                version As UInteger, ByRef outTable As IntPtr) As Integer
        Select Case capability
            Case 1UI
                outTable = _授权表
            Case 2UI
                _渲染序号 += 1
                Dim 渲染 As New 音频渲染能力表 With {
                    .大小 = 56UI, .版本 = 1UI, .上下文 = New IntPtr(44 + _渲染序号),
                    .名称 = New IntPtr(1), .支持 = New IntPtr(1), .渲染 = New IntPtr(1),
                    .复位 = New IntPtr(1), .释放 = New IntPtr(1)
                }
                outTable = 分配结构(渲染)
            Case 3UI
                outTable = _直通表
            Case 4UI
                outTable = _处理器表
            Case Else
                outTable = IntPtr.Zero
                Return 1
        End Select
        Return 0
    End Function

    Private Function 模拟取授权状态(context As IntPtr) As Integer
        Return 授权状态值
    End Function

    Private Function 模拟取授权文本(context As IntPtr, 用途 As UInteger) As IntPtr
        Return If(用途 = 1UI, _文本指针, IntPtr.Zero)
    End Function

    Private Function 模拟提交授权(context As IntPtr, 代码UTF8 As IntPtr) As Integer
        If 代码UTF8 = IntPtr.Zero Then Return 0
        Return If(String.Equals(Marshal.PtrToStringUTF8(代码UTF8), "good", StringComparison.Ordinal), 1, 0)
    End Function

    Private Function 模拟启动处理器(context As IntPtr, 会话句柄 As IntPtr) As Integer
        _处理器运行 = 1UI
        Return 0
    End Function

    Private Sub 模拟停止处理器(context As IntPtr)
        _处理器运行 = 0UI
    End Sub

    Private Sub 模拟取处理器状态(context As IntPtr, ByRef 状态 As 外部处理器状态)
        状态 = New 外部处理器状态 With {
            .大小 = CUInt(Marshal.SizeOf(Of 外部处理器状态)()),
            .版本 = 1UI,
            .运行 = _处理器运行,
            .已处理帧数 = 7UL,
            .未变帧数 = 2UL,
            .跳过帧数 = 1UL,
            .无据帧数 = 3UL,
            .末帧帧号 = 12L,
            .末帧峰值尼特 = 123.5F,
            .状态文本字节 = DirectCast(_状态文本.Clone(), Byte()),
            .详情文本字节 = DirectCast(_详情文本.Clone(), Byte())
        }
    End Sub

    Private Shared Function 文本缓冲(文本 As String) As Byte()
        Dim 结果(63) As Byte
        Dim 字节 = System.Text.Encoding.UTF8.GetBytes(文本)
        Array.Copy(字节, 结果, Math.Min(字节.Length, 63))
        Return 结果
    End Function

    Public Sub Dispose() Implements IDisposable.Dispose
        模块.Dispose()
        For Each 指针 In _保留结构
            If 指针 <> IntPtr.Zero Then Marshal.FreeHGlobal(指针)
        Next
        For Each 指针 In _保留文本
            If 指针 <> IntPtr.Zero Then Marshal.FreeCoTaskMem(指针)
        Next
        _保留结构.Clear()
        _保留文本.Clear()
    End Sub
End Class
