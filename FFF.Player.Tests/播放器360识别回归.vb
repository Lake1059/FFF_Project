Imports System
Imports FFF.Player

''' <summary>360 全景自动识别的判据回归。
''' 判据只允许来自元数据：① 视频流的球形投影 side data（AV_PKT_DATA_SPHERICAL）；
''' ② 容器级 / 流级 tag。任何宽高几何推断都不允许出现。
''' 跑法：FFF.Player.Tests.exe --360-identification-regression</summary>
Friend Module 播放器360识别回归

    Private NotInheritable Class 用例项
        Public ReadOnly 名称 As String
        Public ReadOnly 信息 As 媒体信息
        Public ReadOnly 快照 As 播放器快照
        Public ReadOnly 期望 As Boolean

        Public Sub New(名称值 As String, 信息值 As 媒体信息, 快照值 As 播放器快照, 期望值 As Boolean)
            名称 = 名称值
            信息 = 信息值
            快照 = 快照值
            期望 = 期望值
        End Sub
    End Class

    Friend Function 运行() As Integer
        Dim 用例 = 构建用例()
        Dim 失败 = 0
        For Each 项 In 用例
            Dim 实际 = 播放器360视角控制器.是360视频(项.信息, 项.快照)
            If 实际 <> 项.期望 Then
                失败 += 1
                Console.WriteLine($"FAIL  {项.名称}    期望={项.期望}  实际={实际}")
            Else
                Console.WriteLine($"ok    {项.名称}")
            End If
        Next
        Console.WriteLine()
        Console.WriteLine($"360 识别回归：{用例.Count} 条，失败 {失败} 条")
        Return If(失败 = 0, 0, 1)
    End Function

    Private Function 构建用例() As List(Of 用例项)
        Dim 用例 As New List(Of 用例项)

        ' ── 基础：没有元数据就一律不认（几何判据已删除）──
        添加(用例, "无元数据 2:1", 1920, 960, Nothing, Nothing, Nothing, False)
        添加(用例, "无元数据 16:9", 3840, 2160, Nothing, Nothing, Nothing, False)
        添加(用例, "无关 tag foo=bar", 1920, 960, Nothing, Nothing, 表("foo", "bar"), False)

        ' ── side data：只有完整等距柱状才算，tiled / half 都画不对 ──
        添加(用例, "投影字段 equirectangular", 1920, 960, "equirectangular", Nothing, Nothing, True)
        添加(用例, "投影字段 tiled equirectangular", 1920, 960, "tiled equirectangular", Nothing, Nothing, False)
        添加(用例, "投影字段 half equirectangular", 1920, 960, "half equirectangular", Nothing, Nothing, False)
        添加(用例, "投影字段 cubemap", 1920, 960, "cubemap", Nothing, Nothing, False)
        添加(用例, "投影字段 fisheye", 1920, 960, "fisheye", Nothing, Nothing, False)

        ' ── 投影类型 tag：键归一化 + 精确值域 ──
        添加(用例, "tag projection=equirectangular", 1920, 960, Nothing, 表("projection", "equirectangular"), Nothing, True)
        添加(用例, "tag projection=half equirectangular", 1920, 960, Nothing, 表("projection", "half equirectangular"), Nothing, False)
        添加(用例, "tag ProjectionType=equirectangular", 1920, 960, Nothing, 表("ProjectionType", "equirectangular"), Nothing, True)
        添加(用例, "tag PROJECTIONTYPE（MKV 大写化）", 1920, 960, Nothing, 表("PROJECTIONTYPE", "equirectangular"), Nothing, True)
        添加(用例, "tag projection_type=equirectangular", 1920, 960, Nothing, 表("projection_type", "equirectangular"), Nothing, True)
        添加(用例, "tag projection=360", 1920, 960, Nothing, 表("projection", "360"), Nothing, False)
        添加(用例, "tag projection=cubemap", 1920, 960, Nothing, 表("projection", "cubemap"), Nothing, False)
        添加(用例, "tag projection=flat", 1920, 960, Nothing, 表("projection", "flat"), Nothing, False)

        ' ── 布尔声明 tag：只认 1 / true ──
        添加(用例, "tag spherical=true", 1920, 960, Nothing, 表("spherical", "true"), Nothing, True)
        添加(用例, "tag spherical=1", 1920, 960, Nothing, 表("spherical", "1"), Nothing, True)
        添加(用例, "tag SPHERICAL=TRUE（MKV 大写化）", 1920, 960, Nothing, 表("SPHERICAL", "TRUE"), Nothing, True)
        添加(用例, "tag spherical_video=1", 1920, 960, Nothing, 表("spherical_video", "1"), Nothing, True)
        添加(用例, "tag spherical=0", 1920, 960, Nothing, 表("spherical", "0"), Nothing, False)

        ' ── 非投影标识不能当证据 ──
        添加(用例, "tag stitching_software=PTGui", 1920, 960, Nothing, 表("stitching_software", "PTGui"), Nothing, False)
        添加(用例, "tag stitching_software=PTGui 360", 1920, 960, Nothing, 表("stitching_software", "PTGui 360"), Nothing, False)

        ' ── side data 与 tag 冲突时，side data 权威 ──
        添加(用例, "冲突：投影=cubemap + tag 说 equirect", 1920, 960, "cubemap", 表("projection", "equirectangular"), Nothing, False)

        ' ── 容器级 tag 只覆盖唯一的视频流 ──
        添加(用例, "双视频流 + 容器级 projection", 1920, 960, Nothing, 表("projection", "equirectangular"), Nothing, False, 1)
        添加(用例, "双视频流 + 流级 projection", 1920, 960, Nothing, Nothing, 表("projection", "equirectangular"), True, 1)
        添加(用例, "单视频流 + 封面图 + 容器级 projection", 1920, 960, Nothing, 表("projection", "equirectangular"), Nothing, True, 0, True)

        ' ── 否定声明与肯定声明并存：结果不能依赖字典遍历顺序 ──
        添加(用例, "同表 projection=cubemap + spherical=true", 1920, 960, Nothing,
             表("projection", "cubemap", "spherical", "true"), Nothing, False)
        添加(用例, "同表逆序 spherical=true + projection=cubemap", 1920, 960, Nothing,
             表("projection", "cubemap", "spherical", "true"), Nothing, False, 0, False, True)

        Return 用例
    End Function

    Private Sub 添加(用例 As List(Of 用例项), 名称 As String, 宽 As Integer, 高 As Integer,
                     投影 As String, 容器标签 As (String, String)(), 流标签 As (String, String)(),
                     期望 As Boolean, Optional 额外视频流 As Integer = 0,
                     Optional 带封面图 As Boolean = False, Optional 逆序 As Boolean = False)
        Dim 信息 = 新建信息(宽, 高, 投影, 建表(容器标签, 逆序), 建表(流标签, 逆序), 额外视频流, 带封面图)
        用例.Add(New 用例项(名称, 信息, 新建快照(0), 期望))
    End Sub

    Private Function 新建信息(宽 As Integer, 高 As Integer, 投影 As String,
                             容器标签 As Dictionary(Of String, String),
                             流标签 As Dictionary(Of String, String),
                             额外视频流 As Integer, 带封面图 As Boolean) As 媒体信息
        Dim 信息 As New 媒体信息()
        Dim 主 As New 媒体流信息 With {
            .索引 = 0, .类型 = "video", .宽度 = 宽, .高度 = 高,
            .投影 = If(投影, String.Empty)
        }
        If 流标签 IsNot Nothing Then
            For Each 项 In 流标签
                主.元数据(项.Key) = 项.Value
            Next
        End If
        信息.流.Add(主)

        For 序号 = 1 To 额外视频流
            Dim 附加 As New 媒体流信息 With {
                .索引 = 序号, .类型 = "video", .宽度 = 宽, .高度 = 高
            }
            信息.流.Add(附加)
        Next

        ' 封面图在核心里是 video 类型但不算画面，不能参与「唯一视频流」计数。
        If 带封面图 Then
            Dim 封面 As New 媒体流信息 With {.索引 = 99, .类型 = "video", .是封面图 = True}
            信息.流.Add(封面)
        End If

        If 容器标签 IsNot Nothing Then
            For Each 项 In 容器标签
                信息.元数据(项.Key) = 项.Value
            Next
        End If
        Return 信息
    End Function

    Private Function 新建快照(流索引 As Integer) As 播放器快照
        Dim 原生 As New 原生播放器快照 With {.当前视频流 = 流索引}
        Return New 播放器快照(原生)
    End Function

    Private Function 建表(项 As (String, String)(), 逆序 As Boolean) As Dictionary(Of String, String)
        If 项 Is Nothing Then Return Nothing
        Dim 表 As New Dictionary(Of String, String)(StringComparer.OrdinalIgnoreCase)
        If 逆序 Then
            For i = 项.Length - 1 To 0 Step -1
                表(项(i).Item1) = 项(i).Item2
            Next
        Else
            For Each 对 In 项
                表(对.Item1) = 对.Item2
            Next
        End If
        Return 表
    End Function

    Private Function 表(ParamArray 键值 As String()) As (String, String)()
        Dim 结果((键值.Length \ 2) - 1) As (String, String)
        For i = 0 To 结果.Length - 1
            结果(i) = (键值(i * 2), 键值(i * 2 + 1))
        Next
        Return 结果
    End Function
End Module
