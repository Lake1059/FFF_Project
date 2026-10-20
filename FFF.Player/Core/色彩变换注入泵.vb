Imports System.Runtime.InteropServices
Imports System.Threading

' ═══════════════════════════════════════════════════════════════════════════
'  色彩变换注入泵 —— “帧边数据环 → 外部模块 → kind=3 注入”这条通路的编排
'
'  ● 一路会话一个泵、一个模块实例：模块的 reset() 是“丢弃逐流状态”，多路共用
'    一个实例时，一路的 seek 会把别的路的模块侧状态也清掉。
'
'  ● 逐帧驱动**不能挂 UI 轮询**：3FP 播放中轮询是 250ms/4Hz，而环只装 120 帧
'    ⇒ 60fps 素材下每秒产出 60 条、每秒只消费 4 条，环一直溢出，游标停在
'    被挤掉的旧帧号上 ⇒ 注入永久落后两秒（“静默停摆”的另一种形状）。
'    所以泵自带一条后台线程，节拍按帧率的一半取、钳到 [2,12]ms。
'
'  ● 注入是**整表替换**语义：内核用这一批把现有注入表整体换掉。3FP 宿主侧
'    当前没有自己的 HDR10+/Vivid 注入表（那条通路在 3FCompare 侧），所以只发
'    kind=3 一条即可；将来宿主若载入伴报表，必须与 kind=3 一并重发。
'
'  ● 复位双保险：宿主信号（seek/帧步进/停止/换文件/换轨）由会话调
'    <see cref="流重启"/>；内核自发起的清环（设备恢复、解码器线程数自适应、
'    硬解回退）宿主无从得知，由 <see cref="检查环回退"/> 用快照帧号回退兜住。
'    漏掉复位的表现不是崩，是“注入静默停摆”。
'
'  ● 模块缺席 / 内核没有环导出 / 本帧无据，三种情况都只是“什么都不做”，
'    与不装模块时的行为逐位一致（既定口径：不装模块 = 静默降级且不提示）。
'
'  ● 配置记录通道：容器码流配置记录是**流级**常量（在 codecpar 上，不在逐帧环里），
'    由内核导出 FFF3FP_CopyStreamSideData 取回、缓存一次、喂给模块 分析帧 的追加字段；
'    模块据此把分类文本（如多层/兼容档）算出来，再由 取内容描述() 提供给 UI。
'    这两个追加通道都按"旧内核/旧模块没有就静默降级"处理。
' ═══════════════════════════════════════════════════════════════════════════

Friend Class 色彩变换注入泵
    Implements IDisposable

    ' ---- 契约常量 ----

    ''' <summary>环上取的边数据类型键：不透明数值，按宿主—模块契约取值（见 边数据契约类型）。</summary>
    Private Const 环条目类型 As UInteger = 播放器原生接口.边数据契约类型

    ''' <summary>动态元数据包版本（内核 FFF3FPDynamicMetadataPacket.version）。</summary>
    Private Const 包版本 As UInteger = 1UI

    ''' <summary>本模块产出的载荷 kind：内核 ExternalColorTransform。</summary>
    Private Const 载荷种类 As UInteger = 3UI

    ''' <summary>载荷种类版本。</summary>
    Private Const 载荷种类版本 As UInteger = 1UI

    ''' <summary>模块“在但锁着”时的补装载间隔（ms）。</summary>
    Private Const 补装载间隔毫秒 As Integer = 2000

    ''' <summary>补装载上限 ≈ 60 秒：给“切出去找解锁码”留够时间；超时后本会话放弃，保持静默。</summary>
    Private Const 最大补装载次数 As Integer = 30

    ''' <summary>环回退自愈的检查间隔（ms）：只在“这一趟没有新条目”时才有意义。</summary>
    Private Const 回退检查间隔毫秒 As Integer = 200

    ''' <summary>容器配置记录通道的失败重试间隔（ms）：流里没有 / 尺寸不合时干净地稍后再试。</summary>
    Private Const 配置记录重试间隔毫秒 As Long = 2000L

    ''' <summary>配置记录的单条上限（字节）。正常记录只有几十字节；更大的异常条目一概不缓存。</summary>
    Private Const 配置记录上限 As UInteger = 64UI

    ' ---- 游标：高 32 位是复位世代，低 32 位是“帧号 ≥”下界 ----
    ' 为什么不直接对帧号做 CAS：复位把下界写回 0，而探测时的下界本来就可能
    ' 是 0（从头播、或刚复位过一次）——那时“值没变”与“被人复位过”在单值 CAS
    ' 上长得一模一样，旧时间线的条目照样会被发出去。世代计数器正是这两者的区分量。
    Private _游标状态 As Long
    Private _缓冲区 As Byte()

    ' ---- 状态 ----
    Private ReadOnly _会话 As 播放器会话
    Private ReadOnly _锁 As New Object
    Private _模块 As 色彩变换模块实例
    Private _线程 As Thread
    Private _唤醒 As ManualResetEventSlim
    Private _停止 As Boolean
    Private _已释放 As Boolean
    Private _错误已记 As Boolean
    Private _上次载荷 As Byte()
    Private _末次注入帧号 As Long = -1
    Private _已消费帧号 As Long = -1
    Private _上次回退检查 As Long = 0
    Private _节拍毫秒 As Integer = 8
    Private _节拍已缓存 As Boolean
    ' 模块答"未授权"之后是否已经通知过内核丢表。只做一次：未授权是逐帧回答，
    ' 每帧都调一次内核既无意义也会把日志刷满。重新注入成功后复位。
    Private _已清注入表 As Boolean

    ' ---- 配置记录通道（容器码流配置记录：流级常量，取首次出现的一条并缓存） ----
    Private _配置记录 As Byte()
    Private _配置记录下次查询 As Long
    ' 模块产出的分类文本（泵线程写、UI 线程读 ⇒ 用 Volatile 发布可见性）。
    Private _内容描述 As String

    ' ---- 计数器（诊断：会话收尾时一句话回答“通路到底跑没跑”） ----
    Public Property 注入帧数 As UInteger
    Public Property 未变帧数 As UInteger
    Public Property 跳过帧数 As UInteger
    Public ReadOnly Property 末次注入帧号 As Long
        Get
            Return _末次注入帧号
        End Get
    End Property

    ''' <summary>计数器摘要。</summary>
    Public Function 描述计数器() As String
        Return $"注入 {注入帧数} 帧，未变 {未变帧数} 帧，跳过 {跳过帧数} 帧，末帧 {_末次注入帧号}"
    End Function

    ''' <summary>最近一次 分析帧（泵线程）产出的分类文本；未解锁 / 无分类 / 模块缺席时为空串。
    ''' <para>只能在泵线程调用过 分析帧 之后读（模块侧导出按线程保存快照）；
    ''' 这里读到的是已拷贝的托管串，跨线程安全。</para></summary>
    Friend Function 读取内容描述() As String
        Dim 值 = Volatile.Read(_内容描述)
        If String.IsNullOrEmpty(值) Then Return String.Empty
        Return 值
    End Function

    ''' <summary>模块采用态（诊断用：把“0 注入”分成“没装载”与“装载了但环空”两种读法）。</summary>
    Public Function 描述模块态() As String
        SyncLock _锁
            If _已释放 Then Return "泵已释放"
            If _模块 Is Nothing Then Return "模块未采用（仍锁着或不在盘上）"
            If Not _模块.可用 Then Return "模块已采用但不可用"
            Return "模块已采用（布局版本 " & _模块.布局版本.ToString() & "）"
        End SyncLock
    End Function

    Public Sub New(会话 As 播放器会话)
        If 会话 Is Nothing Then Throw New ArgumentNullException(NameOf(会话))
        _会话 = 会话
    End Sub

    ''' <summary>采用已装载的模块实例（装载成功后由会话调用一次）。</summary>
    Public Sub 采用模块(模块 As 色彩变换模块实例)
        SyncLock _锁
            If _已释放 OrElse 模块 Is Nothing Then Return
            _模块 = 模块
        End SyncLock
    End Sub

    ''' <summary>这条通路是否可用（模块在、内核有环导出）。</summary>
    Public ReadOnly Property 通路可用 As Boolean
        Get
            SyncLock _锁
                Return Not _已释放 AndAlso _模块 IsNot Nothing AndAlso _模块.可用 AndAlso
                    播放器原生接口.支持帧边数据环 AndAlso 播放器原生接口.支持动态元数据注入
            End SyncLock
        End Get
    End Property

    ''' <summary>启动泵线程。已在跑时是空操作；线程曾退出（如补装载放弃）时可重启动。</summary>
    Public Sub 启动()
        Dim 旧唤醒 As ManualResetEventSlim = Nothing
        SyncLock _锁
            If _已释放 OrElse _停止 Then Return
            If _线程 IsNot Nothing AndAlso _线程.IsAlive Then Return
            旧唤醒 = _唤醒
            _唤醒 = New ManualResetEventSlim(False)
        End SyncLock
        ' 旧线程已经不在跑（上面 IsAlive 已排除），它那条唤醒事件可以收了：
        ' 重启动是真会发生的路径（解锁晚于补装载预算），不释放会一次泄一个句柄。
        If 旧唤醒 IsNot Nothing Then
            Try
                旧唤醒.Dispose()
            Catch ex As ObjectDisposedException
            End Try
        End If
        Dim 新线程 As New Thread(New ThreadStart(AddressOf 泵循环)) With {
            .IsBackground = True,
            .Name = "色彩变换注入泵"
        }
        SyncLock _锁
            If _已释放 OrElse _停止 Then Return
            _线程 = 新线程
        End SyncLock
        _线程.Start()
    End Sub

    ''' <summary>请求停止并等待线程退场（至多 ~2 秒）。Dispose 与换文件时调。</summary>
    Public Sub 停止并等待()
        SyncLock _锁
            If _停止 Then Return
            _停止 = True
        End SyncLock
        Try
            _唤醒?.Set()
        Catch ex As ObjectDisposedException
        End Try
        Dim 线程 = _线程
        If 线程 IsNot Nothing AndAlso 线程.IsAlive Then
            线程.Join(2000)
        End If
    End Sub

    ' ---- 泵主循环 ----

    Private Sub 泵循环()
        Try
            Dim 补装载次数 As Integer = 0
            While Not _停止
                Dim 模块 As 色彩变换模块实例 = Nothing
                SyncLock _锁
                    模块 = _模块
                End SyncLock

                If 模块 Is Nothing OrElse Not 模块.可用 Then
                    ' 还没装载（通常是“锁着、等用户输入解锁码”这一段）：低频补装载。
                    ' 票据一旦落盘，尝试装载就会成功，下一拍转入正常节拍。
                    If 补装载次数 >= 最大补装载次数 Then
                        ' 放弃前把已注入的表丢掉：模块是中途失效（不只是在锁着）时，
                        ' 内核里会一直躺着最后一条 LUT，而显示行早就空了。
                        If 注入帧数 > 0UI Then 清除注入表()
                        Debug.WriteLine("色彩变换注入泵：补装载 " & 最大补装载次数.ToString() &
                                        " 次仍未成功（模块仍锁着或不在）⇒ 本会话放弃，保持静默")
                        Return
                    End If
                    补装载次数 += 1
                    尝试补装载()
                    If Not 睡一拍(补装载间隔毫秒) Then Return
                    Continue While
                End If

                Try
                    跑一趟()
                Catch ex As ObjectDisposedException
                    ' 会话正在销毁：本线程的正常退场路径之一，不留痕。
                    Return
                Catch ex As Exception
                    ' 兜底：泵的任何异常都不能带倒后台线程（线程一死，通路就永久哑了，
                    ' 且没有任何可见症状）。只记一次——逐帧记会把日志刷满。
                    If Not _错误已记 Then
                        _错误已记 = True
                        Debug.WriteLine("色彩变换注入泵：泵异常（已忽略后续同类）：" &
                                        ex.GetType().Name & ": " & ex.Message)
                    End If
                End Try

                If Not 睡一拍(计算节拍毫秒()) Then Return
            End While
        Catch ex As Exception
            ' 线程入口的兜底：任何意外都静默退场，绝不把异常带出线程。
            ' 退场前同样要丢表：否则画面继续套最后一条 LUT，而宿主这边已经没有任何
            ' 供给源了（症状是“解锁了但显示没变”的镜像版）。
            If 注入帧数 > 0UI Then 清除注入表()
            Debug.WriteLine("色彩变换注入泵：线程异常退场：" & ex.GetType().Name)
        End Try
    End Sub

    ''' <summary>睡一拍；停止事件被释放时返回 False 让线程退场。</summary>
    Private Function 睡一拍(毫秒 As Integer) As Boolean
        Dim 唤醒 = _唤醒
        If 唤醒 Is Nothing Then Return False
        Try
            唤醒.Wait(毫秒)
        Catch ex As ObjectDisposedException
            Return False
        End Try
        Return Not _停止
    End Function

    ''' <summary>补装载：只在模块可能已解锁时才做（票据落盘后握手才会过）。</summary>
    Private Sub 尝试补装载()
        Try
            Dim 原因 As String = Nothing
            Dim 形态 = 色彩变换模块接口.探测(原因)
            If 形态 <> 色彩变换模块接口.模块在场形态.就绪 Then Return
            Dim 模块 = 色彩变换模块接口.尝试装载(原因)
            If 模块 IsNot Nothing Then
                采用模块(模块)
                _节拍毫秒 = 计算节拍毫秒()
            End If
        Catch ex As Exception
            ' 补装载失败只是“还没解锁”，不是错误。
        End Try
    End Sub

    ' ---- 一趟：取一条 → 换算 → 注入 ----

    ''' <summary>跑一趟。返回 true 表示本帧注入了新表。</summary>
    Public Function 跑一趟() As Boolean
        Dim 模块 As 色彩变换模块实例 = Nothing
        SyncLock _锁
            模块 = _模块
        End SyncLock
        If 模块 Is Nothing OrElse Not 模块.可用 Then Return False

        Dim 帧序号 As Long = -1
        Dim 数据 As Byte() = Nothing
        If Not 尝试读下一条(帧序号, 数据) Then
            ' 这一趟没有新条目：顺手做“环被内核清空”的自愈检查。
            检查环回退()
            Return False
        End If

        ' 换算前先把容器配置记录拿到手（流级常量；拿不到只是模块走它自己的保守路径）。
        确保配置记录()

        Dim 载荷大小 As Integer = 0
        Dim 返回 = 模块.分析帧(数据, 帧序号, _配置记录, 载荷大小)
        _已消费帧号 = 帧序号

        ' 分类文本必须**紧接 分析帧 之后、在同一线程**读（模块侧按线程保存快照）。
        ' 放在 Select Case 之前：所有结果分支都要发布——包括 NoMetadata（本帧无据但
        ' 流级分类已经算出来）与未授权（模块可能刚失效），漏了会让显示行停在旧值。
        发布内容描述(模块.取内容描述())

        Select Case 返回
            Case 色彩变换模块接口.结果成功
                If 载荷大小 <= 0 Then
                    跳过帧数 += 1UI
                    Return False
                End If
                Dim 载荷 = 模块.取载荷副本()
                If 载荷 Is Nothing OrElse 载荷.Length < 载荷大小 Then
                    跳过帧数 += 1UI
                    Return False
                End If
                ' 内容未变 ⇒ 不重发：内核里已经躺着内容相同的一条 ≤ 当前帧，
                ' 重发在语义上是恒等操作，只会把 431296 B 的封送成本换成同一个画面。
                If _上次载荷 IsNot Nothing AndAlso 载荷相同(载荷, 载荷大小, _上次载荷) Then
                    未变帧数 += 1UI
                    Return False
                End If
                If 尝试注入(载荷, 载荷大小, 帧序号) Then
                    ReDim Preserve _上次载荷(载荷大小 - 1)
                    Array.Copy(载荷, _上次载荷, 载荷大小)
                    注入帧数 += 1UI
                    _末次注入帧号 = 帧序号
                    _已清注入表 = False
                    Return True
                End If
                跳过帧数 += 1UI
                Return False

            Case 色彩变换模块接口.结果无元数据
                ' 本帧无可换算据：正常跳过，不是错误，不计数。
                Return False

            Case 色彩变换模块接口.结果未授权
                ' 解锁失效（票据到期/被删/换机）：模块从此不再供货。宿主这边显示行
                ' 会自己变空（上面刚发布的空内容描述），但内核里还躺着上一条表 ——
                ' 不丢掉的话，画面会继续套 LUT 直到换文件，与显示行自相矛盾。
                跳过帧数 += 1UI
                清除注入表()
                Return False

            Case Else
                ' 参数无效 / 缓冲不足 / 不支持：都记跳过。
                跳过帧数 += 1UI
                Return False
        End Select
    End Function

    ''' <summary>通知内核丢掉所有已注入的动态元数据（种类 0 = 清空所有 kind）。
    ''' 幂等且只做一次；下一次成功注入时复位，让“授权恢复”能重新生效。</summary>
    Private Sub 清除注入表()
        If _已清注入表 Then Return
        _已清注入表 = True
        ' 载荷缓存一起作废：授权恢复后必须重新注入，不能因为“内容没变”而跳过。
        _上次载荷 = Nothing
        Try
            Dim 句柄 = _会话.取得句柄()
            If 句柄 Is Nothing OrElse 句柄.IsInvalid Then Return
            If Not 播放器原生接口.支持动态元数据注入 Then Return
            Dim 结果 = 播放器原生接口.清空动态元数据(句柄, 0UI)
            If 结果 <> 原生播放器结果.成功 Then
                Debug.WriteLine("色彩变换注入泵：清注入表未被内核接受（" & 结果.ToString() & "）")
            End If
        Catch ex As Exception
            ' 句柄可能正在销毁：清不掉也不能把泵带倒。
            Debug.WriteLine("色彩变换注入泵：清注入表异常（已忽略）：" & ex.GetType().Name)
        End Try
    End Sub

    ' ---- 帧边数据环游标 ----

    ''' <summary>取下一条边数据。返回 false 表示这一趟没有可注入的新条目（含正常回答 NotFound）。</summary>
    Private Function 尝试读下一条(ByRef 帧序号 As Long, ByRef 数据 As Byte()) As Boolean
        帧序号 = -1
        数据 = Nothing
        Dim 句柄 = _会话.取得句柄()
        If 句柄 Is Nothing OrElse 句柄.IsInvalid Then Return False
        If Not 播放器原生接口.支持帧边数据环 Then Return False

        ' 一次取回整个状态：它既是这次探测用的下界，也是稍后前进时的合法性凭据。
        Dim 状态0 = Volatile.Read(_游标状态)
        Dim 下界 = 状态0 And &HFFFFFFFFL

        ' 第一趟：缓冲区传 Nothing 只问尺寸。
        Dim 需要 As UInteger = 0UI
        Dim 找到 As Long = 0L
        Dim 探测结果 = 播放器原生接口.复制帧边数据(句柄, 环条目类型, 下界, Nothing, 需要, 找到)
        If 探测结果 <> 原生播放器结果.成功 OrElse 需要 = 0UI OrElse 找到 < 0L Then Return False

        ' 第二趟起可以重试：缓冲区不足时条目未被消费，按报出的尺寸换大缓冲再取一次。
        For 尝试 As Integer = 0 To 1
            确保容量(需要)
            Dim 写入 As UInteger = 0UI
            Dim 取到帧号 As Long = 0L
            Dim 取结果 = 播放器原生接口.复制帧边数据(句柄, 环条目类型, 找到, _缓冲区, 写入, 取到帧号)
            Select Case 取结果
                Case 原生播放器结果.成功
                    If 写入 = 0UI OrElse 取到帧号 < 0L Then Return False
                    ' 探测与取字节之间可能夹进一次流重启（seek/停止/换文件都在别的线程上）。
                    ' 世代+下界一起 CAS：对不上就说明这条条目属于已经作废的时间线 ⇒ 当场丢弃，
                    ' 不把带旧帧号的表交给内核（那会让画面停在旧色调，而且毫无失败痕迹）。
                    Dim 新状态 = (状态0 And &HFFFFFFFF00000000L) Or (CUInt(取到帧号 + 1L) And &HFFFFFFFFL)
                    If Interlocked.CompareExchange(_游标状态, 新状态, 状态0) <> 状态0 Then Return False
                    帧序号 = 取到帧号
                    数据 = _缓冲区   ' 指向内部复用缓冲：调用方必须当次用完
                    Return True

                Case 原生播放器结果.缓冲区不足
                    需要 = 写入
                    Continue For

                Case Else
                    ' NotFound：条目在两次调用之间被挤出环。
                    Return False
            End Select
        Next
        Return False
    End Function

    ''' <summary>环当前复用缓冲的容量，只按内核报过的最大尺寸增长（单块上限 16 KiB）。</summary>
    Private Sub 确保容量(需要 As UInteger)
        If _缓冲区 IsNot Nothing AndAlso _缓冲区.Length >= 需要 Then Return
        _缓冲区 = New Byte(CInt(Math.Max(需要, 16UI * 1024UI)) - 1) {}
    End Sub

    ''' <summary>游标只前进的下界（含）。0 表示从头开始。</summary>
    Public ReadOnly Property 游标下界 As Long
        Get
            Return Volatile.Read(_游标状态) And &HFFFFFFFFL
        End Get
    End Property

    ''' <summary>复位游标：下界归零并且世代 +1。只动游标、不动复用缓冲。</summary>
    Public Sub 游标复位()
        Dim 旧值 As Long, 新值 As Long
        Do
            旧值 = Volatile.Read(_游标状态)
            新值 = (((旧值 And &HFFFFFFFF00000000L) >> 32) + 1L) << 32
        Loop While Interlocked.CompareExchange(_游标状态, 新值, 旧值) <> 旧值
    End Sub

    ' ---- 配置记录通道（流级边数据） ----

    ''' <summary>取回当前视频流的容器配置记录并缓存（流级常量，取一次即可；seek 不清除、
    ''' 换文件/换轨由 <see cref="流重启"/> 清掉重取）。两趟式：先探询尺寸，0 &lt; 尺寸 ≤
    ''' <see cref="配置记录上限"/> 才回取。内核没有该导出、流里还没有记录、或尺寸不合，
    ''' 都只是"这一趟没有"——按 <see cref="配置记录重试间隔毫秒"/> 节流后再试，不报错。</summary>
    Private Sub 确保配置记录()
        If _配置记录 IsNot Nothing Then Return
        If Not 播放器原生接口.支持流边数据 Then Return
        Dim 现在 = Environment.TickCount64
        If 现在 < _配置记录下次查询 Then Return

        Dim 句柄 = _会话.取得句柄()
        If 句柄 Is Nothing OrElse 句柄.IsInvalid Then Return

        Dim 需要 As UInteger = 0UI
        Dim 探询 = 播放器原生接口.复制流边数据(句柄, 播放器原生接口.配置记录契约类型, Nothing, 需要)
        If 探询 = 原生播放器结果.成功 AndAlso 需要 > 0UI AndAlso 需要 <= 配置记录上限 Then
            Dim 缓冲(CInt(需要) - 1) As Byte
            Dim 写入 As UInteger = 0UI
            Dim 取结果 = 播放器原生接口.复制流边数据(句柄, 播放器原生接口.配置记录契约类型, 缓冲, 写入)
            If 取结果 = 原生播放器结果.成功 AndAlso 写入 = 需要 Then
                _配置记录 = 缓冲
                Return
            End If
        End If
        ' 未找到（流里没有这类记录）或尺寸在两次调用之间变了：2 秒后再试。
        _配置记录下次查询 = 现在 + 配置记录重试间隔毫秒
    End Sub

    ''' <summary>与当前值不同才发布（分类文本逐帧不变，避免无谓写入）。泵线程调用。</summary>
    Private Sub 发布内容描述(描述 As String)
        If 描述 Is Nothing Then 描述 = String.Empty
        If String.Equals(描述, _内容描述, StringComparison.Ordinal) Then Return
        Volatile.Write(_内容描述, 描述)
    End Sub

    ' ---- 流重启与环回退自愈 ----

    ''' <summary>流状态重置：seek、帧步进、停止、换文件、换轨之后必须调。
    ''' 两件事缺一不可：游标复位（presentation index 从那里重来，内核同时把环丢了）、
    ''' 模块侧逐流状态丢弃。漏掉的表现不是崩，是“注入静默停摆”。
    ''' 另外必须重新注入一次：内核里留着的是带旧帧号的那一条，presentation index
    ''' 从 0 重来 ⇒ 旧条目的帧号大于当前帧，永远选不中 ⇒ 画面会掉回基底。</summary>
    Public Sub 流重启()
        游标复位()
        Try
            Dim 模块 As 色彩变换模块实例 = Nothing
            SyncLock _锁
                模块 = _模块
            End SyncLock
            模块?.复位()
        Catch ex As Exception
            ' reset 契约：任何时刻都安全；失败也不该把调用方带倒。
        End Try
        _上次载荷 = Nothing
        _末次注入帧号 = -1
        _已消费帧号 = -1
        ' 新时间线还没有任何注入：允许下一次"未授权"再通知一次内核。
        _已清注入表 = False
        ' 换文件后帧率可能变了：清掉缓存让下一拍重读。
        _节拍已缓存 = False
        ' 流重绑才会清内核侧的流级边数据；配置记录与分类文本必须跟着重取。
        _配置记录 = Nothing
        _配置记录下次查询 = 0L
        Volatile.Write(_内容描述, String.Empty)
    End Sub

    ''' <summary>“环被内核清空”的自愈：presentation index 回退即环已重来。
    ''' 清环的点里有三个（设备恢复、解码器线程数自适应、硬解回退）是内核自己发起的，
    ''' 宿主拿不到任何信号；快照的帧号是唯一每帧都到宿主手里的证据。
    ''' 只在“这一趟没有新条目”时按 200ms 节流检查，不给正常通路加内核调用。</summary>
    Private Sub 检查环回退()
        Dim 现在 = Environment.TickCount64
        If 现在 - _上次回退检查 < 回退检查间隔毫秒 Then Return
        _上次回退检查 = 现在
        Try
            Dim 快照 = _会话.当前快照
            Dim 当前帧 = 快照.帧序号
            If _已消费帧号 > 0 AndAlso 当前帧 >= 0 AndAlso 当前帧 < _已消费帧号 Then
                流重启()
            End If
        Catch ex As Exception
            ' 快照读取失败（会话正在关）不该影响泵。
        End Try
    End Sub

    ' ---- 注入 ----

    ''' <summary>把一份载荷钉住、打成一个 kind=3 包，整表替换式注入。
    ''' 载荷字节就是内核 kind=3 载荷结构的字节（模块与内核 static_assert 同水），
    ''' 所以不需要重建结构，直接钉住模块的缓冲即可。</summary>
    Private Function 尝试注入(载荷 As Byte(), 载荷大小 As Integer, 帧序号 As Long) As Boolean
        Dim 句柄 = _会话.取得句柄()
        If 句柄 Is Nothing OrElse 句柄.IsInvalid Then Return False
        If Not 播放器原生接口.支持动态元数据注入 Then Return False
        ' 模块产出的载荷尺寸必须与内核结构尺寸同水，否则每一帧都会被整批拒绝。
        If 载荷大小 <> 播放器原生接口.色彩变换载荷尺寸 Then Return False

        Dim 钉住 = GCHandle.Alloc(载荷, GCHandleType.Pinned)
        Try
            Dim 包 As New 播放器原生接口.原生动态元数据包 With {
                .大小 = CUInt(Marshal.SizeOf(Of 播放器原生接口.原生动态元数据包)()),
                .版本 = 包版本,
                .种类 = 载荷种类,
                .种类版本 = 载荷种类版本,
                .帧序号 = 帧序号,
                .载荷大小 = CUInt(载荷大小),
                .保留 = 0UI,
                .载荷 = 钉住.AddrOfPinnedObject()
            }
            Return 播放器原生接口.设置动态元数据(句柄, {包}) = 原生播放器结果.成功
        Finally
            钉住.Free()
        End Try
    End Function

    ''' <summary>两份载荷的前 <paramref name="大小"/> 字节是否逐位相同。</summary>
    Private Shared Function 载荷相同(左 As Byte(), 大小 As Integer, 右 As Byte()) As Boolean
        If 右 Is Nothing OrElse 右.Length < 大小 Then Return False
        For 索引 As Integer = 0 To 大小 - 1
            If 左(索引) <> 右(索引) Then Return False
        Next
        Return True
    End Function

    ' ---- 节拍 ----

    ''' <summary>半帧节拍，钳到 [2,12]ms。帧率读不到时默认 8ms（125Hz）。
    ''' 半帧 ⇒ 消费速率是产出速率的两倍，环不会在消费前溢出。
    ''' 帧率缓存：读一次就够（媒体信息是 JSON 反序列化，不能每 tick 读）；
    ''' 换文件后由 <see cref="流重启"/> 清缓存重读。</summary>
    Private Function 计算节拍毫秒() As Integer
        If Not _节拍已缓存 Then
            Try
                Dim 信息 = _会话.当前媒体信息
                Dim 视频流 = 信息?.流.FirstOrDefault(Function(流) 流.类型 = "video")
                If 视频流 IsNot Nothing AndAlso 视频流.平均帧率 > 0 Then
                    _节拍毫秒 = CInt(Math.Clamp(500.0 / 视频流.平均帧率, 2.0, 12.0))
                    _节拍已缓存 = True
                End If
            Catch ex As Exception
                ' 读不到帧率就按默认档，不因此停下通路。
            End Try
        End If
        Return _节拍毫秒
    End Function

    ' ---- 释放 ----

    Public Sub Dispose() Implements IDisposable.Dispose
        停止并等待()
        SyncLock _锁
            If _已释放 Then Return
            _已释放 = True
            Dim 模块 = _模块
            _模块 = Nothing
            If 模块 IsNot Nothing Then
                Try
                    模块.Dispose()
                Catch ex As Exception
                    ' 释放失败不该掩盖别的清理。
                End Try
            End If
        End SyncLock
        Try
            _唤醒?.Dispose()
        Catch ex As ObjectDisposedException
        End Try
    End Sub
End Class
