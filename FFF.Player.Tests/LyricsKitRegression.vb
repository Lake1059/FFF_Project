' 内嵌歌词提取器 —— 测试包回归
'
' 素材是 16 件真实载体样本（本地素材包，不随仓库分发），用来验证提取器对三根轴的容忍度：
'   ① 键名/大小写（LYRICS / lyrics / syncedlyrics / COMMENT）
'   ② 挂载层级（.opus 流级 vs .mka/.flac/.mp3/.m4a 容器级）
'   ③ 有无时间轴（SYNCEDLYRICS 有，UNSYNCEDLYRICS 无）
'
' 复用宿主真实的 内嵌歌词提取器 + 媒体信息模型，不重新实现一套——
' 否则测的是"我以为的逻辑"而不是"真正跑的逻辑"。
'
' 用法：FFF.Player.Tests --lyrics-kit-regression [kit目录]

Imports System.IO
Imports System.Threading
Imports FFF.Player

Friend Module LyricsKitRegression
    ' 素材目录不写死在任何一台机器上：命令行第一个参数优先，其次环境变量。
    Private Const 套件目录环境变量 As String = "LYRICS_KIT_DIR"

    ' 条目数 = -1 表示"期望读不到可同步歌词"。
    ' 用普通字段结构：VB 的数组初始化器对带自定义构造器的 Structure 支持有限。
    Private Structure 预期项
        Friend 文件 As String
        Friend 期望条目 As Integer
        Friend 期望键 As String
        Friend 说明 As String
    End Structure

    Private Function 期望(文件 As String, 条目 As Integer, 键 As String, 说明 As String) As 预期项
        Dim 值 As 预期项
        值.文件 = 文件
        值.期望条目 = 条目
        值.期望键 = 键
        值.说明 = 说明
        Return 值
    End Function

    Private ReadOnly 用例 As 预期项() = {
        _期望("A_opus_LYRICS_only.opus", 12, "LYRICS", "流级大写 LYRICS"),
        _期望("B_opus_three_keys.opus", 12, "LYRICS", "三键同存，取优先键"),
        _期望("C_opus_three_keys_plus_lrc.opus", 12, "LYRICS", "三键+边车"),
        _期望("D_opus_plain_tag_plus_lrc.opus", -1, "LYRICS", "标签仅纯文本，不可同步"),
        _期望("E_opus_lrc_with_BOM.opus", 12, "LYRICS", "BOM 不影响（我们自己剥）"),
        _期望("F_opus_lowercase_key.opus", 12, "lyrics", "小写键也必须认"),
        _期望("G_opus_in_COMMENT.opus", 12, "COMMENT", "歌词塞 COMMENT 也要认"),
        _期望("H_mka_tags_plus_ass.mka", 12, "LYRICS", "容器级标签"),
        _期望("I_mka_tags_only.mka", 12, "LYRICS", "容器级标签（无字幕轨）"),
        _期望("J_m4a_aac_clyr.m4a", 12, "lyrics", "ffmpeg 映射成小写 lyrics"),
        _期望("K_m4a_aac_clyr_tx3g.m4a", 12, "lyrics", "clyr 加 tx3g"),
        _期望("L_flac_LYRICS.flac", 12, "LYRICS", "Vorbis comment"),
        _期望("M_mp3_USLT.mp3", 12, "lyrics", "ID3v2 USLT"),
        _期望("N_mp3_syncedlyrics_TXXX.mp3", 12, "syncedlyrics", "TXXX 里的 syncedlyrics"),
        _期望("O_opus_cover_plus_lyrics.opus", 12, "LYRICS", "封面与歌词共存"),
        _期望("P_mp4_opus_tx3g.mp4", -1, "", "标签整批丢失，读不到是预期")
    }

    ' VB 里 期望 既是结构字段名又是函数名会歧义，这里显式转发一层。
    Private Function _期望(文件 As String, 条目 As Integer, 键 As String, 说明 As String) As 预期项
        Return 期望(文件, 条目, 键, 说明)
    End Function

    Friend Function 运行(参数 As String()) As Integer
        Dim 目录 = If(参数.Length > 0 AndAlso Not String.IsNullOrWhiteSpace(参数(0)),
                    参数(0), Environment.GetEnvironmentVariable(套件目录环境变量))
        If String.IsNullOrWhiteSpace(目录) Then
            Console.Error.WriteLine("未提供歌词素材目录：请用 --lyrics-kit-regression <目录> "& Environment.NewLine & "或设置环境变量 LYRICS_KIT_DIR（素材包不随仓库分发）。")
            Return 2
        End If
        If Not Directory.Exists(目录) Then
            Console.Error.WriteLine($"套件目录不存在：{目录}")
            Return 2
        End If

        Dim 通过 = 0, 失败 = 0
        Console.WriteLine($"=== 内嵌歌词提取回归：{目录} ===")

        For Each 项 In 用例
            Dim 路径 = Path.Combine(目录, 项.文件)
            If Not File.Exists(路径) Then
                Console.WriteLine($"SKIP  {项.文件}（文件不存在）")
                Continue For
            End If
            Dim 结果 = 跑一件(路径)
            Dim 实际条目 = If(结果 Is Nothing, 0, If(结果.资料?.条目.Count, 0))
            Dim 实际键 = If(结果?.来源键, "(无)")

            Dim 有条目 = 结果 IsNot Nothing AndAlso 结果.有条目
            Dim 条目对 = If(项.期望条目 < 0, Not 有条目, 有条目 AndAlso 实际条目 = 项.期望条目)
            Dim 键对 = String.IsNullOrEmpty(项.期望键) OrElse
                       String.Equals(实际键, 项.期望键, StringComparison.OrdinalIgnoreCase)
            Dim 好 = 条目对 AndAlso 键对
            If 好 Then 通过 += 1 Else 失败 += 1

            Dim 标记 = If(好, "PASS", "FAIL")
            Console.WriteLine($"{标记}  {项.文件,-34} 条目={实际条目,-3} 键={实际键,-14} {项.说明}")
            If Not 好 Then
                Console.WriteLine($"      期望 条目={项.期望条目} 键={If(项.期望键, "任意")}")
                If 结果 IsNot Nothing AndAlso Not 结果.有条目 AndAlso Not String.IsNullOrWhiteSpace(结果.纯文本) Then
                    Console.WriteLine($"      实际拿到纯文本 {结果.纯文本.Length} 字节（无时间轴）")
                End If
            End If
        Next

        Console.WriteLine()
        Console.WriteLine($"通过 {通过} / 失败 {失败}（共 {通过 + 失败}）")
        If 失败 > 0 Then Return 1

        ' 边车 LRC 的 BOM 是 MANIFEST 点名的坑之一：ffmpeg 系会静默丢首行（12→11）。
        ' 这条必须单独验，因为它走的是**文件读取**路径而不是内嵌标签路径。
        Return 验证边车BOM(目录)
    End Function

    ''' <summary>验证带 BOM 的边车 .lrc 不会丢首行（MANIFEST 四之三）。</summary>
    Private Function 验证边车BOM(目录 As String) As Integer
        Dim 带Bom = Path.Combine(目录, "E_opus_lrc_with_BOM.lrc")
        Dim 不带Bom = Path.Combine(目录, "C_opus_three_keys_plus_lrc.lrc")
        If Not File.Exists(带Bom) OrElse Not File.Exists(不带Bom) Then
            Console.WriteLine("SKIP  边车 BOM 用例（样本缺失）")
            Return 0
        End If
        Dim 甲 = LRC歌词自动加载器.加载歌词Async(带Bom, CancellationToken.None).GetAwaiter().GetResult()
        Dim 乙 = LRC歌词自动加载器.加载歌词Async(不带Bom, CancellationToken.None).GetAwaiter().GetResult()
        Dim 甲数 = If(甲?.条目.Count, 0)
        Dim 乙数 = If(乙?.条目.Count, 0)
        ' 关键判据：BOM 版必须**不缺首行**，即与无 BOM 版条目数相同。
        Dim 好 = 甲数 > 0 AndAlso 甲数 = 乙数
        Console.WriteLine()
        Console.WriteLine($"{If(好, "PASS", "FAIL")}  边车 BOM 不丢首行：带BOM={甲数} 条，不带BOM={乙数} 条")
        If Not 好 Then
            Console.WriteLine("      带 BOM 少 1 条就是 MANIFEST 描述的缺陷复现。")
            Return 1
        End If
        Return 0
    End Function

    ''' <summary>走真实链路：内核 MediaInfo JSON -> 宿主模型 -> 提取器。</summary>
    Private Function 跑一件(路径 As String) As 内嵌歌词提取器.提取结果
        Dim 信息 = 读取媒体信息(路径)
        If 信息 Is Nothing Then Return Nothing
        Return 内嵌歌词提取器.提取(信息)
    End Function

    Private Function 读取媒体信息(路径 As String) As 媒体信息
        Using 会话 As New 播放器会话(New 播放器配置 With {
            .解码器 = 解码模式.CPU,
            .色彩模式 = 色彩输出模式.映射到SDR})
            会话.设置音量(0.0F, True)
            会话.打开Async(路径).GetAwaiter().GetResult()
            Return 会话.当前媒体信息
        End Using
    End Function
End Module
