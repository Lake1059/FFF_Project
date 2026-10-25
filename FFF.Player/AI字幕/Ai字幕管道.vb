Option Strict On
Option Explicit On

Imports System.Collections.Concurrent
Imports System.Diagnostics
Imports System.Text
Imports System.Text.Json
Imports System.Threading

''' <summary>
''' AI 字幕管道：宿主侧 worker 进程管理 + tap 取数搬运（stdin/stdout 协议）。
''' 协议与 worker 内部逻辑见 docs/ai-subtitles/IMPLEMENTATION_ANALYSIS.md §6；
''' 本类型只负责"帧从内核环流到 worker、结果从 worker 流到状态机"。
'''
''' 三条线程边界（§8）：
'''   * pump 线程：ReadAudioTap → 写 stdin。禁碰 UI、禁碰状态机；
'''   * result 线程：读 stdout → 解析 → 状态机.提交结果。禁碰 UI；
'''   * stderr 线程：worker 诊断日志进环形缓冲（信息图层/日志用）。
''' worker 进程自身只做转换与推理，没有任何反压播放器的能力——播放侧唯一的
''' 代价是 ring 满时 drop-oldest（内核行为，见 FFF.Player.AudioTap.h）。
'''
''' 生命周期铁律：<see cref="停止"/> 必须在播放器句柄销毁前调用（控制器关媒体/
''' 停播/切文件/退出四条路径都走它）；pump 每轮都重新向 <see cref="句柄提供器"/>
''' 要句柄，销毁后自然退出，不存在悬空解引用。
'''
''' Python 运行时：实验分支不内置 Python，要求独立 venv（README 写明依赖组合）。
''' worker 缺失/启动失败/中途死亡 → 字幕功能关闭，**播放不受任何影响**。
''' </summary>
Friend NotInheritable Class Ai字幕管道
    Implements IDisposable

    Private Const 停止等待毫秒 As Integer = 2000
    Private Const 初始缓冲字节 As Integer = 1 << 20        ' 1 MiB：5.1×1024 帧块仅 24 KiB
    Private Const 空转睡眠毫秒 As Integer = 1

    Private ReadOnly 句柄提供器 As Func(Of 播放器原生句柄)
    Private ReadOnly 状态机 As Ai字幕状态机
    Private ReadOnly 日志 As Action(Of String)

    Private ReadOnly 锁 As New Object()
    Private _进程 As Process = Nothing
    Private _pump线程 As Thread = Nothing
    Private _result线程 As Thread = Nothing
    Private _stderr线程 As Thread = Nothing
    ' 结果线程代次：每停一次/每启一次 worker 就 +1。旧结果线程的收尾（Finally 里
    ' 置"worker 已退出"）只允许在代次未变时生效——重启时旧线程可能在进程退场后
    ' 很久才 unwind，迟到的置位会把新 worker 的每一条结果全部丢掉，字幕从此永久
    ' 沉默且日志无任何异常（停止再播 / 改设置重启的现场，2026-10-25 实机复盘）。
    Private _结果线程代 As Integer = 0
    Private _停止事件 As New ManualResetEventSlim(False)
    Private _已释放 As Boolean = False

    ' worker 最新诊断（信息图层展示用；纯内存，无线程安全问题）。
    Private ReadOnly 诊断锁 As New Object()
    Private _诊断文本 As String = String.Empty
    Private _统计文本 As String = String.Empty

    ' 语言闸门丢弃计数：只有 result 线程读写，不需要锁。
    ' 限流是为了"整片语言都不符"时别每 5s 刷一行日志（2MB 上限撑不住长期挂机）。
    Private _语言不符丢弃 As Integer = 0
    Private _上次语言日志 As Long = 0L

    ' worker warning（模型不认音频端点、设备回落）的用户可见通道：result 线程
    ' 触发，按文案去重后交宿主提示（信息图层）。每条不同的 warning 首见必显——
    ' 启动批里"设备回落"之后紧跟着"音频端点探测失败"（换错模型的核心场景），
    ' 一刀切 30s 限流会把第二条吞掉；同一条重复才按 30s 收，防持续 warning
    ' 把提示通道刷爆（与语言闸门同一套路）。
    Private ReadOnly _提示 As Action(Of String)
    Private _上次警告文本 As String = String.Empty
    Private _上次警告计时 As Long = 0L

    Public Sub New(句柄提供器 As Func(Of 播放器原生句柄), 状态机 As Ai字幕状态机,
                   Optional 日志 As Action(Of String) = Nothing,
                   Optional 提示 As Action(Of String) = Nothing)
        Me.句柄提供器 =句柄提供器
        Me.状态机 = 状态机
        Me.日志 = 日志
        Me._提示 = 提示
    End Sub

    ''' <summary>worker 是否在运行。</summary>
    Public ReadOnly Property 运行中 As Boolean
        Get
            SyncLock 锁
                Return _进程 IsNot Nothing AndAlso Not _进程.HasExited
            End SyncLock
        End Get
    End Property

    ''' <summary>worker 最新状态行（loading/ready/error）。</summary>
    Public ReadOnly Property 诊断文本 As String
        Get
            SyncLock 诊断锁
                Return _诊断文本
            End SyncLock
        End Get
    End Property

    ''' <summary>worker 最新统计行（rtf/windows/dropped）。</summary>
    Public ReadOnly Property 统计文本 As String
        Get
            SyncLock 诊断锁
                Return _统计文本
            End SyncLock
        End Get
    End Property

    ''' <summary>
    ''' 启动 worker 并开泵。已在运行则直接返回 True。
    ''' </summary>
    ''' <param name="配置">已校验通过的扩展包；worker / python / 模型 / llama-server 全在里面。</param>
    ''' <param name="窗口覆盖">大于 0 时覆盖包清单的窗口秒；否则跟随清单。</param>
    ''' <param name="步长覆盖">大于 0 时覆盖包清单的步长秒；否则跟随清单（仍会压到不超过窗口）。</param>
    ''' <param name="后端">推理后端（Ai字幕后端 的标识）。设备探测与回落由 worker 问
    ''' llama-server 的 --list-devices 完成，本体不猜设备名。</param>
    ''' <param name="序号">同一后端下的第几块设备；负数 = 不指定。</param>
    ''' <param name="Gpu层数">卸载到显卡的层数；负数 = 自动（GPU 后端全卸、CPU 不卸）。
    ''' 显存吃紧的机器填小数（模型 28 层，填 16 = 一半权重在显存）。</param>
    ''' <param name="语言">字幕语言标识（zh/en/…）或 auto。串到 worker 的 --language，
    ''' 由它换成模型认识的英文名 —— 实测 llama-server 只认英文名，ISO 代码会被忽略。</param>
    ''' <param name="用户附加参数">设置页高级框里用户填的 llama-server 附加参数（每行一个
    ''' token，# 开头是注释）。与清单 llamaArgs 合并后排后、同名 flag 覆盖清单的
    ''' （见 <see cref="合并附加参数"/>）——换模型调 ctx-size / temp 时用户不必改包。</param>
    Public Function 启动(配置 As Ai字幕包配置, 窗口覆盖 As Double, 步长覆盖 As Double,
                        后端 As String, 序号 As Integer, Gpu层数 As Integer,
                        Optional 语言 As String = "",
                        Optional 模型档 As String = "",
                        Optional 用户附加参数 As String = "") As Boolean
        If _已释放 Then Return False
        If 配置 Is Nothing Then Return False
        If String.IsNullOrWhiteSpace(配置.Worker脚本) OrElse Not IO.File.Exists(配置.Worker脚本) Then
            记录($"AI 字幕：worker 脚本不存在（{配置.Worker脚本}）")
            Return False
        End If
        Dim 窗口秒 = If(窗口覆盖 > 0, 窗口覆盖, 配置.窗口秒)
        Dim 步长秒 = If(步长覆盖 > 0, 步长覆盖, 配置.步长秒)
        ' 步长大于窗口会在窗口之间留下音频空洞，这里再压一次（包与覆盖各自都已校过，但两者组合时可能出现）。
        If 步长秒 > 窗口秒 Then 步长秒 = 窗口秒
        ' worker 的 --model-dir 仍是必填项：包没写 model 时给它包目录（它会按默认量化档自己拼）；
        ' 写了就给模型所在目录，再由 --model 精确指定文件。
        ' 模型档：档名命中就用那一档的权重；未命中（老设置里的档名随换包消失）回落到默认档。
        ' 绝不因为档名对不上就拒绝启动 —— 那会让用户以为整个功能坏了。
        Dim 生效模型 = 配置.模型文件
        Dim 生效投影 = 配置.投影文件
        Dim 档说明 = "默认档"
        If Not String.IsNullOrWhiteSpace(模型档) Then
            Dim 档命中 As Boolean
            Dim 档 = 配置.取模型档(模型档, 档命中)
            If 档命中 Then
                生效模型 = 档.模型文件
                生效投影 = 档.投影文件
                档说明 = 档.档名
            Else
                档说明 = $"默认档（{模型档} 不在本包内，已回落）"
            End If
        End If
        Dim 模型目录 = 配置.包目录
        If Not String.IsNullOrWhiteSpace(生效模型) Then
            Try
                模型目录 = IO.Path.GetDirectoryName(配置.模型文件)
            Catch
            End Try
        End If
        If String.IsNullOrWhiteSpace(模型目录) Then 模型目录 = 配置.包目录
        SyncLock 锁
            If _进程 IsNot Nothing AndAlso Not _进程.HasExited Then Return True
            ' 临时目录挑选必须在存活检查之后：媒体已打开会让 启动() 重入一次，
            ' worker 活着时根本用不到新临时目录，却要白扫一遍候选目录。2026-10-25
            ' 实测 E 盘扩展包目录的探测被文件过滤驱动拒绝（UnauthorizedAccessException），
            ' 于是每次切文件/切解码器都刷一条告警——挪下来就只在真正要起 worker 时探测。
            ' 候选与回退语义不变：扩展包目录下的 tmp → 程序目录下的 tmp，都不可写
            ' 就不传 --tmp-dir，worker 有自己的回退链兜底（脚本旁 tmp → cwd → 系统临时目录）。
            Dim 临时根 = 挑选临时目录(IO.Path.Combine(配置.包目录, "tmp"))
            If String.IsNullOrEmpty(临时根) Then
                临时根 = 挑选临时目录(IO.Path.Combine(程序目录, "tmp"))
            End If
            _停止事件.Reset()
            Dim 启动信息 As New ProcessStartInfo With {
                .FileName = If(String.IsNullOrWhiteSpace(配置.Python), "python", 配置.Python),
                .UseShellExecute = False,
                .CreateNoWindow = True,
                .RedirectStandardInput = True,
                .RedirectStandardOutput = True,
                .RedirectStandardError = True,
                .StandardOutputEncoding = Encoding.UTF8,
                .StandardErrorEncoding = Encoding.UTF8,
                .WindowStyle = ProcessWindowStyle.Hidden
            }
            启动信息.ArgumentList.Add(配置.Worker脚本)
            启动信息.ArgumentList.Add("--model-dir")
            启动信息.ArgumentList.Add(模型目录)
            If Not String.IsNullOrWhiteSpace(生效模型) Then
                启动信息.ArgumentList.Add("--model")
                启动信息.ArgumentList.Add(生效模型)
            End If
            If Not String.IsNullOrWhiteSpace(生效投影) Then
                启动信息.ArgumentList.Add("--mmproj")
                启动信息.ArgumentList.Add(生效投影)
            End If
            启动信息.ArgumentList.Add("--window")
            启动信息.ArgumentList.Add(窗口秒.ToString("0.###", Globalization.CultureInfo.InvariantCulture))
            启动信息.ArgumentList.Add("--step")
            启动信息.ArgumentList.Add(步长秒.ToString("0.###", Globalization.CultureInfo.InvariantCulture))
            If Not String.IsNullOrWhiteSpace(配置.LlamaServer) Then
                启动信息.ArgumentList.Add("--llama-server")
                启动信息.ArgumentList.Add(配置.LlamaServer)
            End If
            启动信息.ArgumentList.Add("--device-backend")
            启动信息.ArgumentList.Add(Ai字幕后端.规整(后端))
            ' auto 也显式传：让 worker 日志里能看出这一路到底有没有语言提示。
            启动信息.ArgumentList.Add("--language")
            启动信息.ArgumentList.Add(Ai字幕语言.传参值(语言))
            ' 0 = 让 worker 自己挑空闲端口。固定端口在多实例下必冲突（同时开两个
            ' 播放器窗口、或双路对比时各起一份 worker），而端口本身对宿主不可见。
            启动信息.ArgumentList.Add("--port")
            启动信息.ArgumentList.Add("0")
            If 序号 >= 0 Then
                启动信息.ArgumentList.Add("--device-index")
                启动信息.ArgumentList.Add(序号.ToString(Globalization.CultureInfo.InvariantCulture))
            End If
            ' 层数只在用户显式指定时传：不传就由 worker 按后端自动决策
            '（GPU 全卸 / CPU 不卸），默认行为完全不变。
            If Gpu层数 >= 0 Then
                启动信息.ArgumentList.Add("--ngl")
                启动信息.ArgumentList.Add(Gpu层数.ToString(Globalization.CultureInfo.InvariantCulture))
            End If
            If 配置.服务器映射.Count > 0 Then
                ' 换后端 = 换二进制，所以把整张"后端 → llama-server"表交给 worker。
                启动信息.ArgumentList.Add("--llama-servers")
                启动信息.ArgumentList.Add(JsonSerializer.Serialize(配置.服务器映射))
            End If
            ' 扩展包逃生门：清单 llamaArgs + 设置页用户框（Ai字幕附加参数）两个来源，
            ' 合并后逐 token 映射为 --llama-arg，worker 的固定默认值遇同名 flag 让位。
            ' 换模型要调 ctx-size / temp / 后端专属开关时走这里，本体与 worker 都不用改。
            ' 等号形式不是风格选择：argparse 的 append action 不收以 -- 开头的值，
            ' 拆成两个 token 传（"--llama-arg" "--ctx-size"）会被判 expected one
            ' argument 直接 exit 2——文档的标准示例 ["--ctx-size","8192"] 恰恰
            ' 每个值都以 -- 开头。"--llama-arg=--ctx-size" 才收集得到（2026-10-25
            ' 审查实测：拆分形式 SystemExit(2)，等号形式 Namespace 正常）。
            Dim 用户参数 = 解析附加参数(用户附加参数)
            Dim 合并参数 = 合并附加参数(配置.额外参数, 用户参数)
            For Each 附加 In 合并参数
                启动信息.ArgumentList.Add("--llama-arg=" & 附加)
            Next
            If 合并参数.Count > 0 Then
                记录($"AI 字幕：llama-server 附加参数 {合并参数.Count} 条" &
                     $"（清单 {配置.额外参数.Count} + 用户 {用户参数.Count}，同名 flag 以用户为准）")
            End If
            If Not String.IsNullOrEmpty(临时根) Then
                ' 显式指定 + 把子进程的 TEMP/TMP 一并指过去：llama-server 是 worker
                ' 的子进程，环境变量整棵树继承，哪一层要写临时文件都有着落。
                启动信息.ArgumentList.Add("--tmp-dir")
                启动信息.ArgumentList.Add(临时根)
                启动信息.EnvironmentVariables("TEMP") = 临时根
                启动信息.EnvironmentVariables("TMP") = 临时根
            End If
            记录($"AI 字幕：模型 {档说明}" & If(模型目录 <> 配置.包目录, $"（{模型目录}）", ""))
            记录($"AI 字幕：临时目录 {If(String.IsNullOrEmpty(临时根), "系统默认（worker 自选）", 临时根)}")
            记录($"AI 字幕：推理后端 {Ai字幕后端.规整(后端)}" &
                 If(序号 >= 0, $"，设备序号 {序号}", "") &
                 If(配置.服务器映射.Count > 0, $"（包内专用二进制 {配置.服务器映射.Count} 份）", "（用包默认二进制）"))
            记录($"AI 字幕：字幕语言 {Ai字幕语言.显示名(语言)}" &
                 If(设置.实例对象.Ai字幕仅所选语言, "（只显示该语言）", "（提示，不筛除）"))
            Try
                _进程 = Process.Start(启动信息)
            Catch ex As Exception
                记录($"AI 字幕：worker 启动失败 {ex.Message}")
                _进程 = Nothing
                Return False
            End Try
            Try
                _进程.PriorityClass = ProcessPriorityClass.BelowNormal
            Catch
                ' 提权不足等场景不影响功能。
            End Try
            _pump线程 = New Thread(AddressOf Pump循环) With {.IsBackground = True, .Name = "AI字幕-取数"}
            Dim 代 As Integer = Interlocked.Increment(_结果线程代)
            _result线程 = New Thread(Sub() Result循环(代)) With {.IsBackground = True, .Name = "AI字幕-结果"}
            _stderr线程 = New Thread(AddressOf Stderr循环) With {.IsBackground = True, .Name = "AI字幕-诊断"}
            _pump线程.Start()
            _result线程.Start()
            _stderr线程.Start()
            记录("AI 字幕：worker 已启动")
            Return True
        End SyncLock
    End Function

    ''' <summary>
    ''' 停止 worker 并回收线程。关 stdin 给 worker 优雅退出机会，超时再 Kill。
    ''' 播放路径不受影响：本方法只动管道自己的线程。
    ''' </summary>
    Public Sub 停止()
        Dim 进程本地 As Process = Nothing
        SyncLock 锁
            ' 先作废这一代结果线程的收尾通知（见 _结果线程代）：本方法随后那句
            ' 同步的通知已经覆盖了"worker 停了"的语义，旧线程迟到的 Finally 不许
            ' 再把标志置回去——重启路径紧接着就是 状态机.重置()，谁后到谁害人。
            Interlocked.Increment(_结果线程代)
            _停止事件.Set()
            进程本地 = _进程
            _进程 = Nothing
        End SyncLock
        If 进程本地 Is Nothing Then Return
        Try
            If Not 进程本地.HasExited Then
                ' 关 stdin：worker 的读帧循环见到 EOF 即优雅退出。
                进程本地.StandardInput.Close()
                If Not 进程本地.WaitForExit(停止等待毫秒) Then
                    记录("AI 字幕：worker 未在 2s 内退出，Kill")
                    进程本地.Kill(entireProcessTree:=True)
                End If
            End If
        Catch ex As Exception
            记录($"AI 字幕：停止时异常 {ex.Message}")
        End Try
        状态机?.通知Worker已退出()
        记录("AI 字幕：worker 已停止")
    End Sub

    ' ---------------- pump 线程：内核环 → worker stdin ----------------

    Private Sub Pump循环()
        Dim 缓冲 = New Byte(初始缓冲字节 - 1) {}
        ' 异常日志限流：持续失败（例如内核没有 tap 导出）不能每轮都写一行，
        ' 否则两分钟就能把日志文件撑到几 MB。首次必记，之后每 30s 汇总一次。
        Dim 异常计数 As Integer = 0
        Dim 上次异常计时 As Long = Stopwatch.GetTimestamp()
        Try
            While Not _停止事件.IsSet
                ' 逐轮容错：切文件/停止时会话可能在"取句柄"与"读环"之间被释放
                ' （SafeHandle 抛 ObjectDisposedException）。这必须只废掉一轮——
                ' 若让异常杀死取数线程，切到下一个文件时 worker 明明在运行却
                ' 再也收不到帧，而 启动() 又会因"已在运行"直接返回 True。
                Try
                    Dim 句柄 = 句柄提供器()
                    If 句柄 Is Nothing OrElse 句柄.IsInvalid Then
                        If _停止事件.Wait(200) Then Return
                        Continue While
                    End If
                    Dim 块 As 原生音频Tap块 = Nothing
                    Dim 结果 = 读音频Tap(句柄, 缓冲, 块)
                    Select Case 结果
                        Case 原生播放器结果.成功
                            If 块.已写字节数 = 0UI Then
                                ' 空环：tap 未装（会话未开）或已排空。
                                If _停止事件.Wait(空转睡眠毫秒) Then Return
                                Continue While
                            End If
                            If Not 写帧(块, 缓冲) Then
                                记录("AI 字幕：取数线程因写帧失败退出")
                                Return
                            End If
                        Case 原生播放器结果.缓冲区不足
                            ' 块不消费：换大缓冲重取（块 PCM 上限由内核环容量决定）。
                            Dim 需要 = If(块.需要字节数 > 0UI, CInt(块.需要字节数), 缓冲.Length * 2)
                            If 需要 <= 缓冲.Length Then Return   ' 内核守约前提下不可达
                            缓冲 = New Byte(需要 - 1) {}
                        Case 原生播放器结果.状态无效, 原生播放器结果.参数无效, 原生播放器结果.不支持
                            ' 会话尚未就绪或 tap 未装：轻负载空转。
                            If _停止事件.Wait(空转睡眠毫秒) Then Return
                        Case Else
                            If _停止事件.Wait(5) Then Return
                    End Select
                Catch ex As ObjectDisposedException
                    If _停止事件.Wait(空转睡眠毫秒) Then Return
                Catch ex As Exception
                    异常计数 += 1
                    Dim 现在 As Long = Stopwatch.GetTimestamp()
                    If 异常计数 = 1 OrElse
                       (现在 - 上次异常计时) / CDbl(Stopwatch.Frequency) >= 30.0 Then
                        记录($"AI 字幕：取数轮次异常（累计 {异常计数} 次）{ex.GetType().Name} {ex.Message}")
                        上次异常计时 = 现在
                    End If
                    If _停止事件.Wait(50) Then Return
                End Try
            End While
            记录("AI 字幕：取数线程收到停止信号，正常退出")
        Catch ex As Exception
            记录($"AI 字幕：取数线程退出 {ex.GetType().Name} {ex.Message}")
        End Try
    End Sub

    ''' <summary>组一帧 [u32 LE metaLen][meta JSON][PCM] 写进 worker stdin。</summary>
    Private Function 写帧(块 As 原生音频Tap块, 缓冲 As Byte()) As Boolean
        Try
            Dim 元数据 = New StringBuilder(192)
            元数据.Append("{""generation"":").Append(块.代次.ToString(Globalization.CultureInfo.InvariantCulture))
            元数据.Append(",""position100ns"":").Append(块.位置100纳秒.ToString(Globalization.CultureInfo.InvariantCulture))
            元数据.Append(",""frames"":").Append(块.帧数.ToString(Globalization.CultureInfo.InvariantCulture))
            元数据.Append(",""sampleRate"":").Append(块.采样率.ToString(Globalization.CultureInfo.InvariantCulture))
            元数据.Append(",""channels"":").Append(块.声道数.ToString(Globalization.CultureInfo.InvariantCulture))
            元数据.Append(",""positions"":[")
            For i = 0 To 块.声道位置.Length - 1
                If i > 0 Then 元数据.Append(","c)
                元数据.Append(块.声道位置(i).ToString(Globalization.CultureInfo.InvariantCulture))
            Next
            元数据.Append("],""sampleFormat"":").Append(块.采样格式.ToString(Globalization.CultureInfo.InvariantCulture))
            元数据.Append(",""bytes"":").Append(块.已写字节数.ToString(Globalization.CultureInfo.InvariantCulture))
            元数据.Append("}")
            Dim 元数据字节 = Encoding.UTF8.GetBytes(元数据.ToString())
            Dim 长度前缀 = New Byte(3) {}
            Dim 元数据长度 = CInt(元数据字节.Length)
            长度前缀(0) = CByte(元数据长度 And &HFF)
            长度前缀(1) = CByte((元数据长度 >> 8) And &HFF)
            长度前缀(2) = CByte((元数据长度 >> 16) And &HFF)
            长度前缀(3) = CByte((元数据长度 >> 24) And &HFF)

            Dim 进程本地 As Process = Nothing
            SyncLock 锁
                进程本地 = _进程
            End SyncLock
            If 进程本地 Is Nothing OrElse 进程本地.HasExited Then Return False
            Dim 输入 = 进程本地.StandardInput.BaseStream
            输入.Write(长度前缀, 0, 4)
            输入.Write(元数据字节, 0, 元数据字节.Length)
            输入.Write(缓冲, 0, CInt(块.已写字节数))
            输入.Flush()
            Return True
        Catch ex As Exception
            ' worker 消失/管道断：安静退出取数线程，播放不受影响。
            记录($"AI 字幕：写帧失败 {ex.GetType().Name} {ex.Message}")
            Return False
        End Try
    End Function

    ' ---------------- result 线程：worker stdout → 状态机 ----------------

    Private Sub Result循环(代 As Integer)
        Try
            Dim 进程本地 As Process = Nothing
            SyncLock 锁
                进程本地 = _进程
            End SyncLock
            If 进程本地 Is Nothing Then Return
            Dim 读取器 = 进程本地.StandardOutput
            While Not _停止事件.IsSet
                Dim 行 = 读取器.ReadLine()
                If 行 Is Nothing Then Exit While     ' EOF：worker 退出
                If 行.Length = 0 Then Continue While
                处理结果行(行)
            End While
            记录("AI 字幕：worker 输出流结束")
        Catch ex As Exception
            记录($"AI 字幕：结果线程退出 {ex.GetType().Name} {ex.Message}")
        Finally
            ' 代次 gate：只有"当前这一代"结果线程的退场才算 worker 真的死了。
            ' 重启时旧线程可能在新线程已开始收结果之后才 unwinding（进程退场≠
            ' 线程退场），迟到的置位会让 状态机 把新 worker 的每条结果都当噪声
            ' 丢掉——字幕永久沉默，且没有任何一行日志提示（停止再播/改设置
            ' 重启现场，2026-10-25 实机复盘定位）。
            If Volatile.Read(_结果线程代) = 代 Then 状态机?.通知Worker已退出()
        End Try
    End Sub

    Private Sub 处理结果行(行 As String)
        Try
            Using 文档 = JsonDocument.Parse(行)
                Dim 根 = 文档.RootElement
                Dim 类型 As String = 取字符串(根, "type")
                If 类型 <> String.Empty Then
                    Select Case 类型
                        Case "final"
                            Dim 语言值 = 取字符串(根, "language")
                            ' matched 缺省 True：旧 worker 没这个字段，不能被当"不符"杀掉。
                            ' 只有用户显式要求"只显示所选语言"时才丢——默认那句提示压不住
                            ' 模型，但丢结果会让整片空白，那是更坏的默认。
                            Dim 匹配 = 取布尔(根, "matched", True)
                            If Ai字幕语言.应丢弃(语言值, 匹配, 设置.实例对象.Ai字幕语言,
                                                  设置.实例对象.Ai字幕仅所选语言) Then
                                _语言不符丢弃 += 1
                                记语言不符(语言值)
                            Else
                                Dim 条目 As New Ai字幕状态机.Ai字幕条目(
                                    取ULong(根, "generation"),
                                    取Long(根, "start100ns"),
                                    取Long(根, "end100ns"),
                                    取字符串(根, "text"),
                                    语言值)
                                状态机?.提交结果(条目)
                            End If
                        Case "status"
                            Dim 状态 = 取字符串(根, "state")
                            Dim 详情 = 取字符串(根, "detail")
                            SyncLock 诊断锁
                                _诊断文本 = If(String.IsNullOrEmpty(详情), 状态, $"{状态}: {详情}")
                            End SyncLock
                            记录($"AI 字幕：worker 状态 {_诊断文本}")
                            ' warning（模型不认音频端点、设备回落）不是死亡，但必须
                            ' 让人看见：只进日志的话，用户面对的是空白字幕+无解释。
                            If 状态 = "warning" Then 记警告(详情)
                            ' 只有 worker 自述"我不干了"才当死亡。缺省按致命处理（旧 worker 没这个字段），
                            ' 但必须让"这一窗推理失败、我继续重试"（fatal=false）走开——否则本地 HTTP
                            ' 抖一次就把字幕永久关掉，而 worker 其实活得好好的。
                            If 状态 = "error" AndAlso 取布尔(根, "fatal", True) Then
                                状态机?.通知Worker已退出()
                            End If
                        Case "stats"
                            SyncLock 诊断锁
                                _统计文本 = $"rtf={取双精度(根, "rtf"):0.000} 窗口={取Long(根, "windows")} 丢弃={取Long(根, "dropped")}" &
                                            If(_语言不符丢弃 > 0, $" 语言不符={_语言不符丢弃}", "")
                            End SyncLock
                    End Select
                End If
            End Using
        Catch ex As Exception
            ' 单行解析失败不影响后续行。
            记录($"AI 字幕：结果行解析失败 {ex.Message}")
        End Try
    End Sub

    ' ---------------- stderr 线程：worker 诊断 ----------------

    Private Sub Stderr循环()
        Try
            Dim 进程本地 As Process = Nothing
            SyncLock 锁
                进程本地 = _进程
            End SyncLock
            If 进程本地 Is Nothing Then Return
            Dim 读取器 = 进程本地.StandardError
            While Not _停止事件.IsSet
                Dim 行 = 读取器.ReadLine()
                If 行 Is Nothing Then Exit While
                If 行.Length > 0 Then 记录($"AI 字幕(worker)：{行}")
            End While
        Catch
            ' 诊断流无关紧要。
        End Try
    End Sub

    ''' <summary>语言闸门丢弃一条：首条必记，之后每 30s 汇总一行。
    ''' 整片语言都不符时（例如选了中文看英语片），这是用户唯一能看到的线索。</summary>
    Private Sub 记语言不符(实际语言 As String)
        Dim 现在 As Long = Stopwatch.GetTimestamp()
        If _语言不符丢弃 > 1 AndAlso
           (现在 - _上次语言日志) / CDbl(Stopwatch.Frequency) < 30.0 Then Return
        _上次语言日志 = 现在
        记录($"AI 字幕：语言不符已丢弃 {_语言不符丢弃} 条（实际 {If(实际语言, "未知")}，" &
             $"只显示 {Ai字幕语言.显示名(设置.实例对象.Ai字幕语言)}）")
    End Sub

    ' ---------------- worker warning → 用户可见 ----------------

    ''' <summary>worker 的 warning 必须让人看见，不能只躺日志文件：换了个不认
    ''' 音频端点的模型时，用户看到的是"字幕空白但没解释"，而原因 worker 早就
    ''' 说出来了。节流按文案去重：不同 warning 首见必显（启动批里设备回落与
    ''' 音频端点探测失败是两条不同的话，都得让人看见），同一条重复才 30s 收。</summary>
    Private Sub 记警告(详情 As String)
        If _提示 Is Nothing Then Return
        Dim 文本 = 警告摘要(详情)
        Dim 现在 As Long = Stopwatch.GetTimestamp()
        If 文本 = _上次警告文本 AndAlso
           (现在 - _上次警告计时) / CDbl(Stopwatch.Frequency) < 30.0 Then Return
        _上次警告文本 = 文本
        _上次警告计时 = 现在
        Try
            _提示(文本)
        Catch
            ' 提示通道失败不影响字幕本体。
        End Try
    End Sub

    ''' <summary>warning 详情是写给日志的长文，提示通道只留能读完的第一句。
    ''' WARNING 前缀是 worker 的设备回落约定，剥掉免得和 "AI 字幕：" 叠成两层
    ''' 警告样。分号串的是并列子句（"只有 X；已回落到它的默认设备；用户指定层数
    ''' 16"）——整串短就全留着；按第一个分号一刀切曾把"已回落到它的默认设备"
    ''' 这种结果说明切掉，用户只看到半句话。长了才从尾部收，收口补省略号。</summary>
    Private Shared Function 警告摘要(详情 As String) As String
        Dim 文本 = If(详情, String.Empty).Trim()
        If 文本.StartsWith("WARNING", StringComparison.Ordinal) Then
            文本 = 文本.Substring(7).TrimStart("："c, ":"c, " "c)
        End If
        If 文本 = String.Empty Then Return "AI 字幕：worker 报告异常，详见日志"
        If 文本.Length <= 50 Then Return $"AI 字幕：{文本}"
        Dim 段 = 文本.Split("；"c)
        Dim 收编 As String = 段(0)
        For i As Integer = 1 To 段.Length - 1
            If 收编.Length + 1 + 段(i).Length > 50 Then Exit For
            收编 &= "；" & 段(i)
        Next
        Return $"AI 字幕：{收编}…（详见日志）"
    End Function

    ' ---------------- JSON 取值助手（缺字段走默认，绝不抛） ----------------

    Private Shared Function 取字符串(根 As JsonElement, 名 As String) As String
        Dim 值 As JsonElement = Nothing
        If 根.TryGetProperty(名, 值) AndAlso 值.ValueKind = JsonValueKind.String Then
            Return 值.GetString()
        End If
        Return String.Empty
    End Function

    Private Shared Function 取Long(根 As JsonElement, 名 As String) As Long
        Dim 值 As JsonElement = Nothing
        If 根.TryGetProperty(名, 值) Then
            If 值.ValueKind = JsonValueKind.Number Then
                Dim n As Long
                If 值.TryGetInt64(n) Then Return n
            End If
        End If
        Return 0L
    End Function

    Private Shared Function 取ULong(根 As JsonElement, 名 As String) As ULong
        Dim 值 As JsonElement = Nothing
        If 根.TryGetProperty(名, 值) Then
            If 值.ValueKind = JsonValueKind.Number Then
                Dim n As ULong
                If 值.TryGetUInt64(n) Then Return n
            End If
        End If
        Return 0UL
    End Function

    Private Shared Function 取双精度(根 As JsonElement, 名 As String) As Double
        Dim 值 As JsonElement = Nothing
        If 根.TryGetProperty(名, 值) Then
            If 值.ValueKind = JsonValueKind.Number Then
                Dim n As Double
                If 值.TryGetDouble(n) Then Return n
            End If
        End If
        Return 0.0
    End Function

    ''' <summary>取布尔；缺字段或类型不符时返回 <paramref name="缺省"/>（向后兼容旧 worker）。</summary>
    Private Shared Function 取布尔(根 As JsonElement, 名 As String, 缺省 As Boolean) As Boolean
        Dim 值 As JsonElement = Nothing
        If 根.TryGetProperty(名, 值) Then
            Select Case 值.ValueKind
                Case JsonValueKind.True : Return True
                Case JsonValueKind.False : Return False
            End Select
        End If
        Return 缺省
    End Function

    ''' <summary>
    ''' 挑一个可写的临时目录根（不存在则创建；以"写探针文件再删掉"验证真的可写——
    ''' 只建目录不够：只读媒体/权限异常的盘上 CreateDirectory 也可能失败或建出来也写不了）。
    ''' 任一候选不可写就换下一个，全不可写返回空串：调用方据此不传 --tmp-dir，
    ''' 由 worker 的回退链（系统临时目录 → 脚本旁 tmp → cwd）自己兜底。
    ''' </summary>
    Private Function 挑选临时目录(ParamArray 候选 As String()) As String
        For Each 目录 In 候选
            If String.IsNullOrWhiteSpace(目录) Then Continue For
            Try
                IO.Directory.CreateDirectory(目录)
                Dim 探针 = IO.Path.Combine(目录, ".writable-probe")
                IO.File.WriteAllText(探针, "ok")
                IO.File.Delete(探针)
                Return 目录
            Catch ex As Exception
                记录($"AI 字幕：临时目录候选不可写（{目录}）{ex.GetType().Name} {ex.Message}")
            End Try
        Next
        Return String.Empty
    End Function

    ''' <summary>解析用户附加参数文本（设置页高级框）：每行一个 token。空行忽略；
    ''' # 开头是注释行 —— 写给用户自己看的说明，不进 llama-server 命令行；
    ''' 其余 Trim 后原样收取（"--ctx-size" 与 "8192" 各一行，或单行 "--ctx-size=8192"）。
    ''' Friend：设置页状态行要数一眼用户填了几条（不解析就得在两处各写一遍）。</summary>
    Friend Shared Function 解析附加参数(文本 As String) As List(Of String)
        Dim 结果 As New List(Of String)
        If String.IsNullOrWhiteSpace(文本) Then Return 结果
        For Each 行 In 文本.Split(New String() {vbCrLf, vbLf, vbCr}, StringSplitOptions.RemoveEmptyEntries)
            Dim 值 = 行.Trim()
            ' RemoveEmptyEntries 只挡"真空行"，挡不住纯空格行——Trim 后是空串的
            ' 一律跳过：塞个空 token 进 --llama-arg=，llama-server 会当位置参数报错。
            If 值.Length = 0 Then Continue For
            If 值.StartsWith("#"c, StringComparison.Ordinal) Then Continue For
            结果.Add(值)
        Next
        Return 结果
    End Function

    ''' <summary>合并包清单与用户的附加参数：用户参数排后（llama-server 侧后者生效），
    ''' 且用户点名过的 flag 连同它在包参数里的值一起剔除 —— 即"用户覆盖包"。
    ''' 与 worker 的 <c>_user_first</c> 同一套语义（值 = 下一个不以 -- 开头的 token），
    ''' 两边一致才不会出现"清单给 4096、用户给 8192，命令行两个都在"——某些 llama-server
    ''' 版本对重复 --ctx-size 直接报错，那会让用户以为整个功能坏了。</summary>
    Private Shared Function 合并附加参数(包参数 As IList(Of String), 用户参数 As IList(Of String)) As List(Of String)
        If 用户参数.Count = 0 Then Return New List(Of String)(包参数)
        Dim 点名 As New HashSet(Of String)(StringComparer.Ordinal)
        For Each 项 In 用户参数
            If 项.StartsWith("--", StringComparison.Ordinal) Then 点名.Add(取Flag名(项))
        Next
        Dim 结果 As New List(Of String)
        Dim i As Integer = 0
        While i < 包参数.Count
            Dim 令牌 = 包参数(i)
            If 令牌.StartsWith("--", StringComparison.Ordinal) AndAlso 点名.Contains(取Flag名(令牌)) Then
                ' 删 flag 顺带删它的值：只删 flag 会把包里的旧值留给 llama-server 当位置
                ' 参数，报出来的错比"重复参数"更难懂。等号形态（--flag=value）是单 token，
                ' 下一个 token 必是别的 flag，不会多吃。
                i += If(i + 1 < 包参数.Count AndAlso
                        Not 包参数(i + 1).StartsWith("--", StringComparison.Ordinal), 2, 1)
                Continue While
            End If
            结果.Add(令牌)
            i += 1
        End While
        结果.AddRange(用户参数)
        Return 结果
    End Function

    ''' <summary>token 的 flag 名：--flag=value 取 --flag 部分（等号形态在用户框里很常见）。</summary>
    Private Shared Function 取Flag名(令牌 As String) As String
        Dim 等号 = 令牌.IndexOf("="c)
        Return If(等号 > 0, 令牌.Substring(0, 等号), 令牌)
    End Function

    Private Sub 记录(消息 As String)
        Try
            日志?.Invoke(消息)
        Catch
            ' 日志本身失败也必须安静。
        End Try
    End Sub

    Public Sub Dispose() Implements IDisposable.Dispose
        If _已释放 Then Return
        _已释放 = True
        停止()
    End Sub
End Class
