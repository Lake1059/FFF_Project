Option Strict On
Option Explicit On

Imports System.IO
Imports System.Runtime.InteropServices

''' <summary>
''' AI 实时字幕控制器：标题栏菜单开关 + 扩展包生命周期 + 字幕图层唯一接口。
''' 设计对齐 播放器视图旋转控制器 / 播放器360视角控制器：
'''   · 只加不改：不碰任何既有播放/字幕分支，本控制器的逻辑全部由自身状态门控；
'''   · 失败静默降级：包没选 / 包坏了 / 内核没 tap / worker 起不来 → 只关 AI 字幕，
'''     播放不受影响；
'''   · 显示走既有字幕图层的 <c>绘制扩展定时文字</c> 事件（Form1 订阅后回调
'''     <see cref="取当前条目"/>），AI 与外部 SRT 同底并存、AI 上移一行（决策 D4）。
'''
''' 本体不认识任何具体模型或路径：一切来自扩展包（一个含 ai-subtitles.json 的目录，
''' 见 <see cref="Ai字幕包"/>）。设置里选到有效包即启用；包被删或清单失效则自动停用。
'''
''' 生命周期（docs/ai-subtitles/IMPLEMENTATION_ANALYSIS.md §5）：
'''   * 打开媒体（<c>媒体已打开</c>）且已启用 → 装 tap + 启 worker（幂等，
'''     切文件时 worker 不重启，只在新会话上重装 tap）；
'''   * 停止播放 / 关窗体 / 拨到关 → 停 worker（宿主侧铁律：停管道必须在
'''     播放器句柄销毁前，即 <c>播放控制器.停止()</c> 之前调用 <see cref="停止"/>）；
'''   * seek 由状态机的代次守卫 + worker 的 generation 变化各自清缓冲，无需宿主插手。
''' </summary>
Friend NotInheritable Class 播放器AI字幕控制器
    Implements IDisposable

    Private Const 开关文字 As String = "启用 AI 实时字幕"
    Private Const 设置文字 As String = "AI 字幕设置…"

    Private ReadOnly AI菜单 As LakeUI.ModernContextMenu
    Private ReadOnly 句柄提供器 As Func(Of 播放器原生句柄)
    Private ReadOnly 媒体信息提供器 As Func(Of 媒体信息)
    Private ReadOnly 操作提示 As Action(Of String)
    Private ReadOnly 保存停用偏好 As Action(Of Boolean)
    Private ReadOnly 打开设置页 As Action
    Private ReadOnly 开关菜单项 As LakeUI.ModernContextMenu.ModernMenuItem
    Private ReadOnly 设置菜单项 As LakeUI.ModernContextMenu.ModernMenuItem

    ' 状态机与管道都是本控制器私有：Form1 只经由 取当前条目 看见字幕文本。
    Private ReadOnly 状态机 As New Ai字幕状态机()
    Private 管道 As Ai字幕管道 = Nothing
    ' 当前生效的扩展包；Nothing = 没装包或包不可用（此时一切静默关闭）。
    Private 包 As Ai字幕包配置 = Nothing
    Private 停用 As Boolean
    Private 已报告跳过 As Boolean
    Private 已释放 As Boolean
    ' 已上屏的文本（仅变化时落一行日志：P5 端到端验收的机器可验证据，
    ' 也是"字幕确实走到了显示路径"的唯一自动探针）。
    Private 已上屏文本 As String = String.Empty
    ' 内核侧"字幕图层真的被呈现了几次"的采样节流（每 10 秒一次）。宿主侧日志只能证明
    ' 文本交给了绘制事件；这一条是像素真到屏幕的证据（纯音频无视频轨时尤其关键）。
    Private 上次图层状态时刻 As DateTime = DateTime.MinValue
    ' 当前管道是用哪套设备选项起起来的。播放中改设置时，管道会因为"已在运行"直接
    ' 返回成功，必须先比对这两项决定是否重启（否则改了后端照样跑旧的）。
    Private 已启动后端 As String = Ai字幕后端.自动
    Private 已启动序号 As Integer = -1
    ' 语言也是启动期参数（worker 只在 --language 里拿到一次），中途改必须重启，
    ' 否则用户改了"字幕语言"却发现输出一点没变。
    Private 已启动语言 As String = Ai字幕语言.自动
    ' 模型档同理：换档就是换权重文件，只在启动期生效。
    Private 已启动模型档 As String = String.Empty
    ' 用户附加参数（设置页高级框原文）同理：改内容就要重启 worker 才会进命令行。
    Private 已启动附加参数 As String = String.Empty
    Private 已启动层数 As Integer = -1
    ' 日志滚动：-1 = 还没量过现有文件长度（首次写时取实际大小，这样跨进程追加也准）。
    Private Const 日志上限字节 As Long = 2L * 1024L * 1024L
    Private Shared 日志字节 As Long = -1L
    ' 四个线程都会记日志（pump / result / stderr / UI）。File.AppendAllText 用
    ' FileShare.Read 打开，并发写必抛 IOException 并被下面的 Catch 吞掉——也就是
    ' 丢行，而且专挑 llama-server 启动那种"一次吐几十行"的时刻丢。
    Private Shared ReadOnly 日志锁 As New Object()

    Public Sub New(AI菜单 As LakeUI.ModernContextMenu,
                   句柄提供器值 As Func(Of 播放器原生句柄),
                   媒体信息提供器值 As Func(Of 媒体信息),
                   操作提示值 As Action(Of String),
                   保存停用偏好值 As Action(Of Boolean),
                   打开设置页值 As Action)
        ArgumentNullException.ThrowIfNull(AI菜单)
        ArgumentNullException.ThrowIfNull(句柄提供器值)
        ArgumentNullException.ThrowIfNull(媒体信息提供器值)
        ArgumentNullException.ThrowIfNull(操作提示值)
        ArgumentNullException.ThrowIfNull(保存停用偏好值)
        ArgumentNullException.ThrowIfNull(打开设置页值)
        ' 字段与参数同名，必须 Me. 限定：否则是参数自我赋值，字段恒为 Nothing。
        Me.AI菜单 = AI菜单
        句柄提供器 = 句柄提供器值
        媒体信息提供器 = 媒体信息提供器值
        操作提示 = 操作提示值
        保存停用偏好 = 保存停用偏好值
        打开设置页 = 打开设置页值
        停用 = 设置.实例对象.Ai字幕已停用

        开关菜单项 = New LakeUI.ModernContextMenu.ModernMenuItem With {
            .Text = 开关文字, .ToggleCheckOnClick = False, .CloseOnClick = True
        }
        Me.AI菜单.Items.Add(开关菜单项)
        AddHandler 开关菜单项.Click, AddressOf 开关_Click
        ' 包在设置窗口里有独立一页（分类列表末尾），这里给一条直达路径，
        ' 免得用户在十几个分类里翻。
        设置菜单项 = New LakeUI.ModernContextMenu.ModernMenuItem With {
            .Text = 设置文字, .ToggleCheckOnClick = False, .CloseOnClick = True
        }
        Me.AI菜单.Items.Add(设置菜单项)
        AddHandler 设置菜单项.Click, AddressOf 设置_Click
    End Sub

    ''' <summary>设置变化后重新读包：装了有效包就起，包没了/坏了就停。
    ''' Form1 的 应用Ai字幕设置 与启动初始化都走这里。</summary>
    Friend Sub 应用设置()
        If 已释放 Then Return
        停用 = 设置.实例对象.Ai字幕已停用
        Dim 错误 As String = String.Empty
        Dim 新包 = Ai字幕包.载入(设置.实例对象.Ai字幕包目录, 错误)
        包 = 新包
        If 新包 Is Nothing Then
            ' 没装包是完全正常的默认状态，不提示、只记一次日志。
            If Not String.IsNullOrWhiteSpace(设置.实例对象.Ai字幕包目录) AndAlso Not 已报告跳过 Then
                已报告跳过 = True
                记日志($"AI 字幕：扩展包不可用（{错误}）")
            End If
            停止()
            开关菜单项.Checked = False
            Return
        End If
        已报告跳过 = False
        开关菜单项.Checked = Not 停用
        Dim 设置对象 = 设置.实例对象
        Dim 新后端 = 生效后端(包)
        Dim 新语言 = Ai字幕语言.规整(设置对象.Ai字幕语言)
        ' 管道还活着时 启动() 会直接返回"已在运行"，所以换设备（或换包、换字幕语言）
        ' 必须显式先停：否则用户改了设置，跑着的还是旧二进制/旧后端/旧语言提示。
        Dim 新模型档 = If(设置对象.Ai字幕模型档, String.Empty).Trim()
        If 管道 IsNot Nothing AndAlso (已启动后端 <> 新后端 OrElse
                                       已启动序号 <> 设置对象.Ai字幕设备序号 OrElse
                                       已启动层数 <> 设置对象.Ai字幕Gpu层数 OrElse
                                       已启动语言 <> 新语言 OrElse
                                       已启动模型档 <> 新模型档 OrElse
                                       已启动附加参数 <> 设置对象.Ai字幕附加参数) Then
            停止()
        End If
        ' 驻留偏移是纯显示侧（状态机守卫判定用）：不触发重启，这里同步即可，
        ' 播放中改设置也立刻见效。字幕"何时出现"由推理窗口决定，偏移只调驻留。
        状态机.时间偏移100纳秒 = CLng(设置对象.Ai字幕字幕偏移秒 * 10_000_000L)
        If 停用 Then
            停止()
        Else
            尝试启动(Nothing)
        End If
    End Sub

    ''' <summary>这台机器上真正要用的后端：用户没选（auto）就跟随包清单的 device。</summary>
    Private Shared Function 生效后端(包 As Ai字幕包配置) As String
        If 包 Is Nothing Then Return Ai字幕后端.自动
        Dim id = Ai字幕后端.规整(设置.实例对象.Ai字幕设备后端)
        If id = Ai字幕后端.自动 Then id = Ai字幕后端.规整(包.推荐后端)
        Return id
    End Function

    ''' <summary>AI 字幕是否在工作（包有效且未停用）。</summary>
    Friend ReadOnly Property 启用 As Boolean
        Get
            Return 包 IsNot Nothing AndAlso Not 停用 AndAlso Not 已释放
        End Get
    End Property

    ''' <summary>此刻应显示的 AI 字幕文本；未启用/无有效条目返回空串。
    ''' 只在 PrecisionTimer 线程（字幕图层绘制事件）调用。</summary>
    Friend Function 取当前条目(播放位置 As TimeSpan, 时间轴代次 As ULong) As String
        If 已释放 OrElse Not 启用 Then Return String.Empty
        Dim 文本 = 状态机.取当前条目(播放位置, 时间轴代次)
        If Not String.Equals(文本, 已上屏文本, StringComparison.Ordinal) Then
            已上屏文本 = 文本
            If Not String.IsNullOrEmpty(文本) Then
                记日志($"字幕[代次 {时间轴代次}] {文本}")
            End If
        End If
        ' 诊断采样与"文本变化"解耦：整段只有一句话时文本不变，采样却必须继续，
        ' 否则丢块/呈现计数会整段空白（旧实现挂在文本变化分支上，正是这个毛病）。
        采样诊断()
        Return 文本
    End Function

    ''' <summary>每 5 秒读一次内核侧的字幕图层状态与音频 tap 计数，落一行日志。
    ''' 失败静默：这只是验收探针，读不到不代表字幕没上屏。
    '''
    ''' 两层含义：图层计数证明"画了并且 present 了"；<c>已丢块数</c> 证明"取数端跟得上"。
    ''' 后者是唯一能发现"推理慢到音频环溢出"的指标（CPU 臂 rtf 0.3 时尤其要看）。</summary>
    Private Sub 采样诊断(Optional 强制 As Boolean = False)
        Dim 现在 = DateTime.Now
        If Not 强制 AndAlso (现在 - 上次图层状态时刻).TotalSeconds < 5.0R Then Return
        上次图层状态时刻 = 现在
        Try
            Dim 句柄 = 句柄提供器()
            If 句柄 Is Nothing OrElse 句柄.IsInvalid Then Return
            Dim 值 As New 原生定时文字状态 With {
                .大小 = CUInt(Marshal.SizeOf(Of 原生定时文字状态)()), .版本 = 20UI}
            If 播放器原生接口.FFF3FP_GetTimedTextStatus(句柄, 值) <> 原生播放器结果.成功 Then Return
            Dim 统计 As New 原生音频Tap统计
            Dim 采集文本 = String.Empty
            If 播放器原生接口.取音频Tap统计(句柄, 统计) = 原生播放器结果.成功 Then
                采集文本 = $" 采集块={统计.已产块数} 丢块={统计.已丢块数} 积压={统计.缓冲字节数}B"
            End If
            记日志($"字幕图层状态 呈现帧数={值.图层呈现帧数} 已绘制序号={值.已绘制序号} " &
                   $"命令数={值.命令数} 画布={值.画布宽度}x{值.画布高度}{采集文本}")
        Catch
            ' 探针失败必须安静。
        End Try
    End Sub

    ''' <summary>worker 是否在运行（诊断/测试用）。</summary>
    Friend ReadOnly Property Worker运行中 As Boolean
        Get
            Return 管道 IsNot Nothing AndAlso 管道.运行中
        End Get
    End Property

    ''' <summary>打开媒体后调用：装 tap 并确保 worker 在跑。
    ''' 签名匹配 <c>播放器控制器.媒体已打开</c>，可直接挂到该事件上。</summary>
    Friend Sub 媒体已打开(sender As Object, e As 播放器媒体事件参数)
        If 已释放 OrElse Not 启用 Then Return
        尝试启动(e)
    End Sub

    ''' <summary>停 worker 并清字幕显示。停止播放/关窗体/切文件前调用；
    ''' 幂等，重复调用安全。</summary>
    Friend Sub 停止()
        ' 收尾前读一次内核侧的图层计数与采集计数：这是"字幕真的被呈现过、取数端
        ' 跟得上"的终值证据。必须在卸 tap 之前读，否则统计已被清零。
        If 管道 IsNot Nothing Then 采样诊断(True)
        Dim 管道本地 = 管道
        管道 = Nothing
        If 管道本地 IsNot Nothing Then
            Try
                管道本地.停止()
                管道本地.Dispose()
            Catch
                ' 停不干净也要继续：worker 是独立进程，宿主退出时随作业一起没。
            End Try
        End If
        ' 卸 tap：不停的话内核会继续把每个音频块 memcpy 进一个没人消费的 32 MiB 环，
        ' 整段播放白烧音频线程、白占内存，环满后转成无限丢块。用户关掉 AI 字幕
        ' 就应当把这份代价一起关掉。此时句柄仍然有效（调用方保证在 播放控制器.停止
        ' 之前进来）；无会话/未装 tap 都安全。
        Try
            Dim 句柄 = 句柄提供器()
            If 句柄 IsNot Nothing AndAlso Not 句柄.IsInvalid Then
                If 播放器原生接口.停用音频Tap(句柄) = 原生播放器结果.成功 Then
                    记日志("AI 字幕：音频 tap 已卸载")
                End If
            End If
        Catch
            ' 卸 tap 失败不影响播放；下次打开媒体会重装。
        End Try
        Try
            状态机.重置()
            已上屏文本 = String.Empty
        Catch
        End Try
    End Sub

    ' ---------------- 内部 ----------------

    Private Sub 尝试启动(e As 播放器媒体事件参数)
        If 已释放 OrElse Not 启用 OrElse 包 Is Nothing Then Return
        Try
            If Not 播放器原生接口.支持音频Tap Then
                If Not 已报告跳过 Then
                    已报告跳过 = True
                    记日志("AI 字幕：当前内核没有音频 tap 导出（FFF3FP_EnableAudioTap 缺席），功能不可用")
                    操作提示("AI 字幕：当前内核不支持，功能已跳过")
                End If
                Return
            End If

            Dim 句柄 = 句柄提供器()
            If 句柄 Is Nothing OrElse 句柄.IsInvalid Then
                ' 没有会话（尚未打开媒体）：等 媒体已打开 再来。
                Return
            End If

            ' 有音轨才值得拉一个 1GB 模型起来（图片/无音轨视频直接跳过）。
            Dim 信息 = If(e IsNot Nothing, e.媒体信息, 媒体信息提供器())
            If 信息 Is Nothing OrElse Not 信息.流.Any(Function(x) x.类型 = "audio") Then
                If Not 已报告跳过 Then
                    已报告跳过 = True
                    记日志("AI 字幕：当前媒体没有音轨，跳过")
                End If
                Return
            End If

            Dim 结果 = 播放器原生接口.启用音频Tap(句柄, 0)
            If 结果 <> 原生播放器结果.成功 Then
                记日志($"AI 字幕：tap 安装失败（{结果}），本次不启动")
                Return
            End If

            If 管道 Is Nothing Then
                ' 提示通道随管道一起给：worker 的 warning（模型不认音频端点等）
                ' 由 result 线程节流后经它上屏，只进日志的话用户面对的是无解释空白。
                管道 = New Ai字幕管道(句柄提供器, 状态机, AddressOf 记日志, 操作提示)
                ' 首启提示：从起到首句字幕有十几秒空窗（权重首次搬进显存 + 音频图
                ' 一次性构建，实测冷缓存 16.3s / 热缓存约 1s），不说的话用户会以为
                ' "开了没反应"。只在新建管道时说一次——切文件不重启 worker，不反复刷。
                操作提示("AI 字幕：模型加载中，首句字幕约需十几秒")
            End If
            状态机.重置()
            Dim 设置对象 = 设置.实例对象
            ' 生效后端 已经把"用户没选 → 跟随包清单 device → 还是空则 auto"处理完。
            Dim 后端 = 生效后端(包)
            Dim 语言 = Ai字幕语言.规整(设置对象.Ai字幕语言)
            Dim 新模型档 = If(设置对象.Ai字幕模型档, String.Empty).Trim()
            Dim 新附加参数 = 设置对象.Ai字幕附加参数
            If Not 管道.启动(包, 设置对象.Ai字幕窗口秒覆盖, 设置对象.Ai字幕步长秒覆盖,
                             后端, 设置对象.Ai字幕设备序号, 设置对象.Ai字幕Gpu层数,
                             语言, 新模型档, 新附加参数) Then
                管道 = Nothing
                操作提示("AI 字幕：worker 启动失败，详见日志")
            Else
                已启动后端 = 后端
                已启动序号 = 设置对象.Ai字幕设备序号
                已启动层数 = 设置对象.Ai字幕Gpu层数
                已启动语言 = 语言
                已启动模型档 = 新模型档
                已启动附加参数 = 新附加参数
            End If
        Catch ex As Exception
            记日志($"AI 字幕：启动路径异常 {ex.GetType().Name} {ex.Message}")
        End Try
    End Sub

    ''' <summary>诊断日志：落 <程序目录>\ai-subtitles.log（追加， best-effort，失败即丢）。
    ''' 实验分支的排障主线；不进信息图层，避免刷屏。
    ''' 有上限：到 2 MB 换成 .log.1（覆盖旧的 .1）。正常态一窗一行，长期挂机也不会
    ''' 无限涨；只留一份历史，排障够用。</summary>
    Private Shared Sub 记日志(消息 As String)
        Try
            Dim 行 = $"[{DateTime.Now:HH:mm:ss}] {消息}"
            Debug.WriteLine(行)
            SyncLock 日志锁
                Dim 路径 = Path.Combine(程序目录, "ai-subtitles.log")
                Dim 字节 = System.Text.Encoding.UTF8.GetByteCount(行) + 2
                If 日志字节 < 0L Then
                    Try
                        日志字节 = If(File.Exists(路径), New FileInfo(路径).Length, 0L)
                    Catch
                        日志字节 = 0L
                    End Try
                End If
                If 日志字节 + 字节 > 日志上限字节 Then
                    滚动日志(路径)
                    日志字节 = 0L
                End If
                File.AppendAllText(路径, 行 & vbCrLf, System.Text.Encoding.UTF8)
                日志字节 += 字节
            End SyncLock
        Catch
            ' 日志失败必须安静。
        End Try
    End Sub

    ''' <summary>只留一份历史：删掉旧的 .1，把当前文件改名过去。</summary>
    Private Shared Sub 滚动日志(路径 As String)
        Try
            Dim 旧 = 路径 & ".1"
            If File.Exists(旧) Then File.Delete(旧)
            If File.Exists(路径) Then File.Move(路径, 旧)
        Catch
        End Try
    End Sub

    Private Sub 开关_Click(sender As Object, e As EventArgs)
        If 已释放 Then Return
        If 包 Is Nothing Then
            ' 没装包：开关点了也没意义，直接把人送到设置页去选包。
            操作提示("AI 字幕：请先在设置里选择扩展包")
            Try
                打开设置页()
            Catch
            End Try
            Return
        End If
        停用 = 启用   ' 当前启用 → 拨成停用；当前停用 → 拨回启用
        保存停用偏好(停用)
        开关菜单项.Checked = Not 停用
        If 停用 Then
            停止()
            操作提示("已关闭 AI 实时字幕")
        Else
            操作提示("已启用 AI 实时字幕")
            尝试启动(Nothing)
        End If
    End Sub

    Private Sub 设置_Click(sender As Object, e As EventArgs)
        If 已释放 Then Return
        Try
            打开设置页()
        Catch
            ' 打开设置失败不影响播放，安静吞掉。
        End Try
    End Sub

    Public Sub Dispose() Implements IDisposable.Dispose
        If 已释放 Then Return
        已释放 = True
        RemoveHandler 开关菜单项.Click, AddressOf 开关_Click
        RemoveHandler 设置菜单项.Click, AddressOf 设置_Click
        停止()
        Try
            状态机.Dispose()
        Catch
        End Try
        GC.SuppressFinalize(Me)
    End Sub
End Class
