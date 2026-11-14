Imports System.IO
Imports System.Threading

' 运行期前后预取计划：把「当前媒体及其邻居」按浏览态下发给内核的 PrefetchService。
' 纯事件驱动——只由 媒体已打开 / 列表变化 两个事件推进，不设 Timer、不轮询、不起线程；
' 小助手进程由内核 PrefetchService 自理，宿主不拉起任何进程。
' 旧内核没有这两个可选导出时 设置前后预取计划 直接返回 False，通路静默禁用。
Friend NotInheritable Class 前后预取控制器
    Private Shared ReadOnly 排除扩展名 As New HashSet(Of String)(StringComparer.OrdinalIgnoreCase) From {
        ".yuv", ".rgb"
    }

    Private ReadOnly 列表 As 播放列表
    Private ReadOnly 取当前媒体路径 As Func(Of String)
    Private 代次 As Integer
    Private 上次签名 As String

    Friend Sub New(列表对象 As 播放列表, Optional 取当前媒体路径 As Func(Of String) = Nothing)
        列表 = 列表对象
        Me.取当前媒体路径 = 取当前媒体路径
    End Sub

    ' 媒体已打开 事件：带确切的当前媒体路径。
    Friend Sub 媒体已打开(媒体路径 As String)
        重算并下发(媒体路径)
    End Sub

    ' 列表变化 事件：列表与当前索引已更新，当前路径以列表当前项为准
    '（此刻播放控制器的 当前媒体路径 可能还是上一次打开的媒体，不能拿它当真值）。
    Friend Sub 列表变化()
        Dim 路径 = 列表.当前项目?.路径
        If String.IsNullOrEmpty(路径) AndAlso 取当前媒体路径 IsNot Nothing Then 路径 = 取当前媒体路径()
        重算并下发(路径)
    End Sub

    ' 释放：下发空 plan 取消当前代次全部工作，强制下发不走去重。
    Friend Sub 释放()
        Try
            Dim 新代次 = CUInt(Interlocked.Increment(代次))
            播放器原生接口.设置前后预取计划(Array.Empty(Of String)(), 新代次, 0UI)
        Catch
            ' 预加载不能影响播放。
        End Try
    End Sub

    Private Sub 重算并下发(当前路径 As String)
        Try
            Dim 项目 = 列表.取得项目()
            Dim 当前索引 = 列表.当前索引
            If 项目 Is Nothing OrElse 项目.Count = 0 OrElse
               当前索引 < 0 OrElse 当前索引 >= 项目.Count Then
                下发空计划()
                Return
            End If

            Dim 首项 = If(String.IsNullOrEmpty(当前路径), 项目(当前索引).路径, 当前路径)
            Dim 邻居偏移 As Integer()
            Dim 标志 As UInteger
            If 播放列表.是图片文件(首项) Then
                邻居偏移 = New Integer() {1, -1, 2, -2, 3, -3}
                标志 = 1UI
            Else
                邻居偏移 = New Integer() {1, -1}
                标志 = 2UI
            End If

            Dim 路径列表 As New List(Of String) From {首项}
            For Each 偏移 In 邻居偏移
                Dim 邻居索引 = 当前索引 + 偏移
                If 邻居索引 < 0 OrElse 邻居索引 >= 项目.Count Then Continue For
                Dim 邻居路径 = 项目(邻居索引).路径
                ' RAW 由 LibRaw 整帧解码，邻居预取会同时压 3~6 张高像素图；只保留当前文件，
                ' 不把 RAW 邻居塞进预取计划。打开/切换仍由播放控制器正常解码。
                If 播放列表.是相机RAW文件(邻居路径) Then Continue For
                If 排除扩展名.Contains(Path.GetExtension(邻居路径)) Then Continue For
                If Not 播放列表.是支持的媒体文件(邻居路径) Then Continue For
                路径列表.Add(邻居路径)
            Next

            ' 列表变化 与 媒体已打开 在同一次切换会先后到达；只有最终路径集合
            '（含当前路径、顺序、flags）真正变化才递增代次并下发。
            Dim 签名 = 标志.ToString() & "|" & String.Join(vbLf, 路径列表)
            If String.Equals(签名, 上次签名, StringComparison.Ordinal) Then Return
            上次签名 = 签名

            Dim 新代次 = CUInt(Interlocked.Increment(代次))
            播放器原生接口.设置前后预取计划(路径列表, 新代次, 标志)
        Catch
            ' 预加载不能影响播放。
        End Try
    End Sub

    Private Sub 下发空计划()
        Const 空签名 As String = "<空>"
        If String.Equals(空签名, 上次签名, StringComparison.Ordinal) Then Return
        上次签名 = 空签名
        Dim 新代次 = CUInt(Interlocked.Increment(代次))
        播放器原生接口.设置前后预取计划(Array.Empty(Of String)(), 新代次, 0UI)
    End Sub
End Class
