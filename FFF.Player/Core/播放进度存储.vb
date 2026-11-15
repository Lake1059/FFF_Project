Imports System.Diagnostics
Imports System.IO
Imports System.Linq
Imports System.Text
Imports System.Text.Json

''' <summary>
''' 播放进度记忆：每个媒体文件上次看到哪里，下次打开同一文件时从那里继续。
'''
''' <para><b>为什么是独立文件</b>：进度是"可丢的运行数据"，用户配置是"不可丢的用户资产"。
''' 混进 <c>FFF.Player.Settings.json</c> 会有三个后果：① 进度每十几秒覆写一次，
''' 设置文件被高频重写；② 用户想清进度就得动配置；③ 配置解析失败时进度一起归零。
''' 所以独立成 <c>FFF.Player.PlaybackProgress.json</c>，与设置同目录（便携部署跟着 exe 走），
''' 删掉它只会丢进度，不会影响任何设置。</para>
'''
''' <para><b>为什么按文件指纹校验</b>：路径不是身份。同名文件被重新压制 / 下载到同一路径后，
''' 旧位置对新内容毫无意义。每条记录带文件大小与最后写入时间，任一不符就判为失效，
''' 下次打开从头开始。这样不会出现"新片打开直接跳到片尾"的假恢复。</para>
'''
''' <para><b>写入时机</b>：暂停、跳转、换片、停止、播放结束、退出都立即落盘；
''' 播放中由 Form1 的 15 秒定时器兜底。正常退出再清理一次失效条目。
''' 全部失败都静默（进度不是播放的前提），文件不可写时功能自然关闭。</para>
''' </summary>
Public NotInheritable Class 播放进度存储

    ''' <summary>与 exe 同目录的进度文件名。改名会被视为"没有历史进度"。</summary>
    Public Const 文件名 As String = "FFF.Player.PlaybackProgress.json"

    ''' <summary>格式版本。默认 0 让缺失字段的旧/手工文件可被识别。</summary>
    Public Const 当前版本 As Integer = 1

    ''' <summary>低于这个位置不记：打开看几秒不算"看过"，下次仍从头开始。</summary>
    Public Const 最小记录位置100纳秒 As Long = 5L * TimeSpan.TicksPerSecond

    ''' <summary>距片尾小于余量视为已看完。实际余量按片长自适应，见 <see cref="完成判定余量"/>。</summary>
    Public Const 完成余量100纳秒 As Long = 15L * TimeSpan.TicksPerSecond

    ''' <summary>最多保留的媒体条目数；超出按最后打开时间淘汰。</summary>
    Public Const 最大条目数 As Integer = 128

    ''' <summary>超过这个天数没再打开的记录，退出清理时丢弃。</summary>
    Public Const 过期天数 As Integer = 90

    ''' <summary>播放中两次落盘的最小间隔（毫秒）。暂停/跳转/退出等显式保存不受此限。</summary>
    Public Const 自动保存间隔毫秒 As Long = 10_000L

    Private Shared ReadOnly 锁 As New Object()
    Private Shared ReadOnly 条目 As New Dictionary(Of String, 进度条目)(StringComparer.OrdinalIgnoreCase)
    Private Shared _已加载 As Boolean
    Private Shared _脏 As Boolean
    Private Shared _上次写盘 As Long = Environment.TickCount64
    Private Shared _测试路径 As String
    ''' <summary>进度文件版本高于当前可识别版本：只读不写，避免把未来版本的字段静默收敛成 v1。</summary>
    Private Shared _不兼容版本 As Boolean

    Private Sub New()
    End Sub

    ''' <summary>进度文件完整路径：默认在程序目录（exe 旁），测试可重定向。</summary>
    Public Shared ReadOnly Property 文件路径 As String
        Get
            Dim 路径 = _测试路径
            If String.IsNullOrWhiteSpace(路径) Then 路径 = Path.Combine(程序目录, 文件名)
            Return 路径
        End Get
    End Property

    ''' <summary>落盘结构。字段名保持中文，与设置文件同一可读口径。</summary>
    Public NotInheritable Class 进度文件
        Public Property 版本 As Integer
        Public Property 条目 As List(Of 进度条目) = New List(Of 进度条目)()
    End Class

    ''' <summary>单个媒体的进度记录。位置与时长用 100ns，避免浮点秒的舍入漂移。</summary>
    Public NotInheritable Class 进度条目
        Public Property 路径 As String = String.Empty
        Public Property 位置100纳秒 As Long
        Public Property 时长100纳秒 As Long
        ''' <summary>文件大小（字节）；-1 = 记录时读不到，校验时跳过该维度。</summary>
        Public Property 文件大小 As Long = -1L
        ''' <summary>文件最后写入时间（UTC ticks）；-1 = 读不到。</summary>
        Public Property 文件修改时间UtcTicks As Long = -1L
        ''' <summary>看到过片尾：下次从头开始，不续播。</summary>
        Public Property 已完成 As Boolean
        ''' <summary>最后打开/更新时间（UTC ticks），用于过期淘汰与 LRU 截断。</summary>
        Public Property 最后更新UtcTicks As Long
    End Class

    ''' <summary>查询某文件的续播位置。没有记录 / 已看完 / 文件已变 / 太靠前一律返回 0。</summary>
    Public Shared Function 查询(路径 As String, 时长100纳秒 As Long) As TimeSpan
        If Not 播放进度记忆已启用() Then Return TimeSpan.Zero
        If String.IsNullOrWhiteSpace(路径) OrElse 时长100纳秒 <= 0 Then Return TimeSpan.Zero
        确保已加载()
        If _不兼容版本 Then Return TimeSpan.Zero
        Dim 键 = 规范化路径(路径)
        SyncLock 锁
            Dim 项 As 进度条目 = Nothing
            If Not 条目.TryGetValue(键, 项) OrElse 项 Is Nothing Then Return TimeSpan.Zero

            If 项.已完成 Then
                条目.Remove(键)
                _脏 = True
                Return TimeSpan.Zero
            End If

            If Not 指纹匹配(项) Then
                条目.Remove(键)
                _脏 = True
                Return TimeSpan.Zero
            End If

            Dim 位置 = 项.位置100纳秒
            If 位置 < 最小记录位置100纳秒 Then
                条目.Remove(键)
                _脏 = True
                Return TimeSpan.Zero
            End If

            If 位置 >= 时长100纳秒 - 完成判定余量(时长100纳秒) Then
                ' 记录本身没标"已完成"，但按实际时长已经贴到片尾：视为看完。
                条目.Remove(键)
                _脏 = True
                Return TimeSpan.Zero
            End If

            Return TimeSpan.FromTicks(位置)
        End SyncLock
    End Function

    ''' <summary>设置页显示用：当前已有记录条数。功能关闭时仍可读，方便看清要清理什么。</summary>
    Public Shared Function 统计条目数() As Integer
        确保已加载()
        If _不兼容版本 Then Return 0
        SyncLock 锁
            Return 条目.Count
        End SyncLock
    End Function

    ''' <summary>记录当前位置。位置太靠前会删除旧记录；贴到片尾则记成"已完成"。</summary>
    ''' <param name="立即落盘">True 用于暂停/跳转/换片/停止/退出等显式保存点。</param>
    Public Shared Sub 记录(路径 As String, 位置 As TimeSpan, 时长 As TimeSpan,
                           Optional 已完成 As Boolean = False, Optional 立即落盘 As Boolean = False)
        If Not 播放进度记忆已启用() Then Return
        If String.IsNullOrWhiteSpace(路径) OrElse 时长 <= TimeSpan.Zero Then Return
        ' 光盘/目录没有文件身份，不能当媒体记录；图片总时长为 0，前面已排除。
        If Not File.Exists(路径) Then Return

        Dim 时长100纳秒 = 时长.Ticks
        Dim 位置100纳秒 = Math.Clamp(位置.Ticks, 0L, 时长100纳秒)
        Dim 键 = 规范化路径(路径)
        确保已加载()
        If _不兼容版本 Then Return
        SyncLock 锁
            Dim 完成值 = 已完成 OrElse 位置100纳秒 >= 时长100纳秒 - 完成判定余量(时长100纳秒)

            If Not 完成值 AndAlso 位置100纳秒 < 最小记录位置100纳秒 Then
                If 条目.Remove(键) Then _脏 = True
                If 立即落盘 Then 落盘(强制:=True)
                Return
            End If

            Dim 项 As 进度条目 = Nothing
            If Not 条目.TryGetValue(键, 项) OrElse 项 Is Nothing Then
                项 = New 进度条目 With {.路径 = 键}
                条目(键) = 项
            ElseIf Not 完成值 AndAlso Not 项.已完成 AndAlso
                   Math.Abs(项.位置100纳秒 - 位置100纳秒) < TimeSpan.TicksPerSecond Then
                ' 位置基本没动：别为了每 100ms 的心跳重写文件。
                If 立即落盘 Then 落盘(强制:=True)
                Return
            End If

            Dim 指纹 = 读取指纹(键)
            项.位置100纳秒 = 位置100纳秒
            项.时长100纳秒 = 时长100纳秒
            项.文件大小 = 指纹.大小
            项.文件修改时间UtcTicks = 指纹.修改时间UtcTicks
            项.已完成 = 完成值
            项.最后更新UtcTicks = DateTime.UtcNow.Ticks
            _脏 = True

            If 立即落盘 OrElse Environment.TickCount64 - _上次写盘 >= 自动保存间隔毫秒 Then
                落盘()
            End If
        End SyncLock
    End Sub

    ''' <summary>把内存里的进度原子写入 JSON。失败保留旧文件，下次再试。</summary>
    Public Shared Sub 落盘(Optional 强制 As Boolean = False)
        SyncLock 锁
            If _不兼容版本 Then Return
            If Not _已加载 Then Return
            If Not _脏 AndAlso Not 强制 Then Return

            Dim 路径 = 文件路径
            If 条目.Count = 0 Then
                ' 空表不占目录：删掉文件比留一个空 JSON 干净。删不掉也不影响下次重建。
                Try
                    If File.Exists(路径) Then File.Delete(路径)
                Catch ex As Exception
                    Debug.WriteLine("播放进度：删除空文件失败（已忽略）：" & ex.GetType().Name)
                End Try
                _脏 = False
                _上次写盘 = Environment.TickCount64
                Return
            End If

            Dim 临时路径 As String = Nothing
            Try
                ' 写盘时按最后更新时间截断：内存里可以短暂超限，磁盘上不允许无限增长。
                Dim 文件 As New 进度文件 With {
                    .版本 = 当前版本,
                    .条目 = 条目.Values.
                        OrderByDescending(Function(项) 项.最后更新UtcTicks).
                        Take(最大条目数).
                        ToList()
                }
                Dim JSON = JsonSerializer.Serialize(文件, JsonSO)
                Dim 目录 = Path.GetDirectoryName(路径)
                If Not String.IsNullOrEmpty(目录) Then Directory.CreateDirectory(目录)
                临时路径 = 路径 & ".tmp-" & Guid.NewGuid().ToString("N")
                ' 先写临时文件并 Flush(True) 把内核缓冲刷到盘，再原子改名；
                ' 避免掉电时留下半截 JSON 直接顶掉旧进度。编码仍带 BOM，与历史文件一致。
                Dim 编码 As New UTF8Encoding(encoderShouldEmitUTF8Identifier:=True)
                Using fs As New FileStream(临时路径, FileMode.Create, FileAccess.Write, FileShare.None)
                    Dim 字节 = 编码.GetBytes(JSON)
                    fs.Write(字节, 0, 字节.Length)
                    fs.Flush(True)
                End Using
                File.Move(临时路径, 路径, True)
                _脏 = False
                _上次写盘 = Environment.TickCount64
            Catch ex As Exception
                Try
                    If Not String.IsNullOrWhiteSpace(临时路径) AndAlso File.Exists(临时路径) Then
                        File.Delete(临时路径)
                    End If
                Catch
                End Try
                ' 进度不是播放的前提：不可写目录 / 磁盘满 / 文件被占用一律静默。
                Debug.WriteLine("播放进度：写入失败（已忽略）：" & ex.GetType().Name)
            End Try
        End SyncLock
    End Sub

    ''' <summary>正常退出时清理：删掉源文件已消失/已被替换、已看完、太小、过旧的条目，
    ''' 再按最后打开时间截断到上限。保留的条目继续用于下次续播。</summary>
    Public Shared Sub 退出时清理()
        If Not 播放进度记忆已启用() Then Return
        确保已加载()
        SyncLock 锁
            Dim 现在 = DateTime.UtcNow.Ticks
            Dim 过期Ticks = TimeSpan.FromDays(过期天数).Ticks

            For Each 键 In 条目.Keys.ToArray()
                Dim 项 = 条目(键)
                If 项 Is Nothing OrElse
                   项.已完成 OrElse
                   项.位置100纳秒 < 最小记录位置100纳秒 OrElse
                   (现在 - 项.最后更新UtcTicks) > 过期Ticks OrElse
                   Not 指纹匹配(项) Then
                    条目.Remove(键)
                End If
            Next

            If 条目.Count > 最大条目数 Then
                Dim 保留 = 条目.Values.
                    OrderByDescending(Function(项) 项.最后更新UtcTicks).
                    Take(最大条目数).
                    Select(Function(项) 项.路径).
                    ToArray()
                For Each 键 In 条目.Keys.ToArray()
                    If Not 保留.Contains(键) Then 条目.Remove(键)
                Next
            End If

            _脏 = True
            落盘(强制:=True)
        End SyncLock
    End Sub

    ''' <summary>清空全部进度并删除文件。当前主流程不调用；保留给"退出即清空"或
    ''' 将来的设置开关，也方便测试与排障。</summary>
    Public Shared Sub 清空()
        SyncLock 锁
            _已加载 = True
            _不兼容版本 = False
            条目.Clear()
            _脏 = False
            Dim 路径 = 文件路径
            Try
                If File.Exists(路径) Then File.Delete(路径)
            Catch ex As Exception
                Debug.WriteLine("播放进度：清空失败（已忽略）：" & ex.GetType().Name)
            End Try
            _上次写盘 = Environment.TickCount64
        End SyncLock
    End Sub

    ''' <summary>测试夹具：重定向到临时文件并清空静态缓存。
    ''' 必须每个用例 try/finally 还原，否则会污染后续用例或真实程序目录。</summary>
    Friend Shared Sub 测试用重定向(路径 As String)
        SyncLock 锁
            _测试路径 = 路径
            _已加载 = False
            _脏 = False
            _不兼容版本 = False
            条目.Clear()
            _上次写盘 = Environment.TickCount64
        End SyncLock
    End Sub

    Private Shared Sub 确保已加载()
        SyncLock 锁
            If _已加载 Then Return
            _已加载 = True

            Dim 路径 = 文件路径
            If Not File.Exists(路径) Then Return
            Try
                Dim 文本 = File.ReadAllText(路径, Encoding.UTF8)
                Dim 文件 = JsonSerializer.Deserialize(Of 进度文件)(文本, JsonSO)
                If 文件 Is Nothing OrElse 文件.条目 Is Nothing Then Return
                If 文件.版本 > 当前版本 Then
                    ' 不理解的版本：不把任何条目读进内存，也不允许回写覆盖原文件，
                    ' 避免未来版本新增的字段被静默丢弃。用户仍可显式清空（见 清空）。
                    _不兼容版本 = True
                    Return
                End If

                For Each 项 In 文件.条目
                    If 项 Is Nothing OrElse String.IsNullOrWhiteSpace(项.路径) Then Continue For
                    If 项.时长100纳秒 <= 0 Then Continue For
                    Dim 键 = 规范化路径(项.路径)
                    项.路径 = 键
                    条目(键) = 项
                Next
            Catch ex As Exception
                ' 与 settings 的处理同型：先把坏文件留一份，再走空表，绝不让进度拖垮启动。
                Try
                    File.Copy(路径, 路径 & ".broken-" & DateTime.Now.ToString("yyyyMMddHHmmss"), True)
                Catch
                End Try
                Debug.WriteLine("播放进度：读取失败，按空表继续：" & ex.GetType().Name)
            End Try
        End SyncLock
    End Sub

    ''' <summary>设置页的「记住播放进度」开关。关闭时查询与记录都直接短路；
    ''' 设置对象缺失的极端场景按开启处理，避免功能静默消失。</summary>
    Private Shared Function 播放进度记忆已启用() As Boolean
        Try
            Return 设置.实例对象.播放进度记忆
        Catch
            Return True
        End Try
    End Function

    ''' <summary>片长自适应完成余量：长片 15s；短片至少留 2s 或片长的 1/10，
    ''' 否则 20 秒的短片看了 6 秒就被判成"已看完"。</summary>
    Private Shared Function 完成判定余量(时长100纳秒 As Long) As Long
        Return Math.Min(完成余量100纳秒,
                        Math.Max(2L * TimeSpan.TicksPerSecond, 时长100纳秒 \ 10L))
    End Function

    Private Shared Function 规范化路径(路径 As String) As String
        Try
            Return Path.GetFullPath(路径.Trim())
        Catch
            ' 非法路径不该让进度功能抛异常；退回原串，至少同一进程内还能命中。
            Return 路径.Trim()
        End Try
    End Function

    Private Enum 指纹状态
        ''' <summary>能 stat 到文件，大小/修改时间可信。</summary>
        可用
        ''' <summary>文件现在读不到（权限/锁/网络盘离线/U盘拔出），但不能确认它永久消失。
        ''' 此时保留条目，等下次能访问时再校验。</summary>
        暂不可读
        ''' <summary>文件/父目录确实不存在：可以安全清理。</summary>
        已确认消失
    End Enum

    Private Structure 文件指纹
        Public 状态 As 指纹状态
        Public 大小 As Long
        Public 修改时间UtcTicks As Long
    End Structure

    Private Shared Function 读取指纹(路径 As String) As 文件指纹
        Try
            ' File.GetAttributes 对不存在的路径抛 FileNotFoundException / DirectoryNotFoundException；
            ' 对无权限、文件被占用、网络盘离线、U盘拔出抛 UnauthorizedAccessException / IOException。
            ' 这两类语义完全不同：前者是"可以安全删条目"，后者是"千万别删，下次可能又能看到"。
            File.GetAttributes(路径)
            Dim 信息 As New FileInfo(路径)
            Return New 文件指纹 With {
                .状态 = 指纹状态.可用,
                .大小 = 信息.Length,
                .修改时间UtcTicks = 信息.LastWriteTimeUtc.Ticks
            }
        Catch ex As FileNotFoundException
            ' 文件不存在，但父目录可达：确认消失。
            Return New 文件指纹 With {.状态 = 指纹状态.已确认消失}
        Catch ex As DirectoryNotFoundException
            ' 两种情况都抛这个：① 父目录在可达盘上被删了 → 确认消失；
            ' ② 整个盘符根不可达（U 盘拔出 / 网络盘离线 / 盘符不存在）→ 暂不可读。
            ' 用盘符根是否可达区分。
            If 盘符根可达(路径) Then
                Return New 文件指纹 With {.状态 = 指纹状态.已确认消失}
            End If
            Return New 文件指纹 With {.状态 = 指纹状态.暂不可读}
        Catch
            Return New 文件指纹 With {.状态 = 指纹状态.暂不可读}
        End Try
    End Function

    ''' <summary>判断路径所在的盘符根是否可达。根不可达 = U 盘被拔 / 网络盘离线 / 盘符不存在。</summary>
    Private Shared Function 盘符根可达(路径 As String) As Boolean
        Try
            Dim 根 = Path.GetPathRoot(Path.GetFullPath(路径))
            If String.IsNullOrEmpty(根) Then Return True
            Return Directory.Exists(根)
        Catch
            ' 探测本身就失败：保守按不可达处理，保留条目。
            Return False
        End Try
    End Function

    ''' <summary>记录时的文件是否还是同一份。大小与写入时间都要对得上；
    ''' 记录侧读不到的维度（-1）不参与判定。读不到但不能确认消失（U盘拔出/网络盘离线）
    ''' 时保留条目，避免误删用户下次插回来还要用的续播位置。</summary>
    Private Shared Function 指纹匹配(项 As 进度条目) As Boolean
        If 项 Is Nothing Then Return False
        Dim 指纹 = 读取指纹(项.路径)
        Select Case 指纹.状态
            Case 指纹状态.暂不可读
                Return True
            Case 指纹状态.已确认消失
                Return False
            Case Else ' 可用
                If 项.文件大小 >= 0 AndAlso 项.文件大小 <> 指纹.大小 Then Return False
                If 项.文件修改时间UtcTicks >= 0 AndAlso
                   项.文件修改时间UtcTicks <> 指纹.修改时间UtcTicks Then Return False
                Return True
        End Select
    End Function

End Class
