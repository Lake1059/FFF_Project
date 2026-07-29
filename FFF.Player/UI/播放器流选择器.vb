Imports System.IO

''' <summary>按当前媒体状态重建流菜单，并把它显示在视频容器中央。</summary>
Friend NotInheritable Class 播放器流选择器
    Implements IDisposable

    Private ReadOnly 所属窗体 As Form
    Private ReadOnly 视频容器 As Control
    Private ReadOnly 菜单 As LakeUI.ModernContextMenu
    Private ReadOnly 播放控制器 As 播放器控制器
    Private 已释放 As Boolean

    Friend Sub New(所属窗体 As Form, 视频容器 As Control,
                   菜单 As LakeUI.ModernContextMenu, 播放控制器 As 播放器控制器)
        ArgumentNullException.ThrowIfNull(所属窗体)
        ArgumentNullException.ThrowIfNull(视频容器)
        ArgumentNullException.ThrowIfNull(菜单)
        ArgumentNullException.ThrowIfNull(播放控制器)
        Me.所属窗体 = 所属窗体
        Me.视频容器 = 视频容器
        Me.菜单 = 菜单
        Me.播放控制器 = 播放控制器
    End Sub

    Friend Sub 显示()
        If 已释放 OrElse 所属窗体.IsDisposed OrElse Not 视频容器.IsHandleCreated Then Return
        菜单.Close()
        菜单.Items.Clear()

        Dim 信息 = 播放控制器.安全读取媒体信息()
        Dim 快照 = 播放控制器.安全读取快照()
        If 信息 Is Nothing OrElse 快照 Is Nothing Then
            菜单.Items.Add(创建说明项("尚未打开媒体"))
            居中显示()
            Return
        End If

        Dim 视频流 = 信息.流.Where(
            Function(x) String.Equals(x.类型, "video", StringComparison.OrdinalIgnoreCase) AndAlso Not x.是封面图).ToArray()
        Dim 音频流 = 信息.流.Where(
            Function(x) String.Equals(x.类型, "audio", StringComparison.OrdinalIgnoreCase)).ToArray()
        Dim 字幕流 = 信息.流.Where(
            Function(x) String.Equals(x.类型, "subtitle", StringComparison.OrdinalIgnoreCase)).ToArray()

        添加标题("视频流", 视频流.Length)
        If 视频流.Length = 0 Then
            菜单.Items.Add(创建说明项("无可用视频流"))
        Else
            For Each 流 In 视频流
                Dim 流索引 = 流.索引
                Dim 项 = 创建流项(格式化视频流(流), 流索引 = 快照.当前视频流)
                AddHandler 项.Click, Sub() 播放控制器.选择视频流(流索引)
                菜单.Items.Add(项)
            Next
        End If

        菜单.Items.Add(创建分隔线())
        添加标题("音频流", 音频流.Length)
        If 音频流.Length = 0 Then
            菜单.Items.Add(创建说明项("无可用音频流"))
        Else
            For Each 流 In 音频流
                Dim 流索引 = 流.索引
                Dim 项 = 创建流项(格式化音频流(流), 流索引 = 快照.当前音频流)
                AddHandler 项.Click, Sub() 播放控制器.选择音频流(流索引)
                菜单.Items.Add(项)
            Next
        End If

        菜单.Items.Add(创建分隔线())
        Dim 外部字幕 = 播放控制器.可用外部字幕
        添加标题("字幕流", 字幕流.Length + 外部字幕.Count)
        Dim 当前字幕索引 = 播放控制器.当前字幕流索引
        Dim 当前字幕 = 播放控制器.当前字幕
        Dim 关闭项 = 创建流项("关闭字幕", 当前字幕索引 = -2)
        AddHandler 关闭项.Click, Sub() 播放控制器.关闭字幕()
        菜单.Items.Add(关闭项)

        For Each 流 In 字幕流
            Dim 流索引 = 流.索引
            Dim 项 = 创建流项(格式化字幕流(流), 当前字幕索引 = 流索引)
            AddHandler 项.Click, Sub() 播放控制器.选择内嵌字幕(流索引)
            菜单.Items.Add(项)
        Next

        For Each 字幕 In 外部字幕
            Dim 字幕路径 = 字幕.路径
            Dim 已选择 = 当前字幕索引 = -1 AndAlso 当前字幕 IsNot Nothing AndAlso
                String.Equals(当前字幕.路径, 字幕路径, StringComparison.OrdinalIgnoreCase)
            Dim 项 = 创建流项(格式化外部字幕(字幕), 已选择)
            AddHandler 项.Click, Sub() 播放控制器.选择外部字幕(字幕路径)
            菜单.Items.Add(项)
        Next

        Dim 导入项 = 创建流项("导入外部字幕…", False)
        导入项.ForeColor = Color.CornflowerBlue
        AddHandler 导入项.Click, AddressOf 导入外部字幕
        菜单.Items.Add(导入项)
        居中显示()
    End Sub

    Private Sub 添加标题(名称 As String, 数量 As Integer)
        菜单.Items.Add(创建说明项($"{名称} - {数量}"))
    End Sub

    Private Shared Function 创建说明项(文本 As String) As LakeUI.ModernContextMenu.ModernMenuItem
        Return New LakeUI.ModernContextMenu.ModernMenuItem(文本) With {
            .IsDescription = True,
            .CloseOnClick = False
        }
    End Function

    Private Shared Function 创建分隔线() As LakeUI.ModernContextMenu.ModernMenuItem
        Return New LakeUI.ModernContextMenu.ModernMenuItem With {.IsSeparator = True}
    End Function

    Private Shared Function 创建流项(文本 As String, 已选择 As Boolean) As LakeUI.ModernContextMenu.ModernMenuItem
        Return New LakeUI.ModernContextMenu.ModernMenuItem(文本) With {.Checked = 已选择}
    End Function

    Private Sub 导入外部字幕(sender As Object, e As EventArgs)
        If Not 播放控制器.是否有媒体 Then Return
        Using 对话框 As New OpenFileDialog With {
            .CheckFileExists = True,
            .Filter = "字幕文件|*.srt;*.ass;*.ssa;*.sup|所有文件|*.*",
            .RestoreDirectory = True,
            .Title = "导入外部字幕"
        }
            If 对话框.ShowDialog(所属窗体) = DialogResult.OK Then 播放控制器.替换字幕(对话框.FileName)
        End Using
    End Sub

    Private Shared Function 格式化视频流(流 As 媒体流信息) As String
        Dim 参数 As New List(Of String) From {编码名称(流.编码, 流.配置档次)}
        If 流.宽度 > 0 AndAlso 流.高度 > 0 Then 参数.Add($"{流.宽度}×{流.高度}")
        If 流.平均帧率 > 0 Then 参数.Add($"{流.平均帧率:0.###} fps")
        If 流.是HDR Then 参数.Add("HDR")
        If 流.位深度 > 8 Then 参数.Add($"{流.位深度} bit")
        添加比特率(参数, 流.比特率)
        添加元数据(参数, 流)
        Return $"#{流.索引}  {String.Join(" | ", 参数)}"
    End Function

    Private Shared Function 格式化音频流(流 As 媒体流信息) As String
        Dim 参数 As New List(Of String) From {编码名称(流.编码, 流.配置档次)}
        添加比特率(参数, 流.比特率)
        If 流.采样率 > 0 Then 参数.Add(格式化采样率(流.采样率))
        If 流.声道数 > 0 Then 参数.Add(格式化声道(流.声道数))
        添加元数据(参数, 流)
        Return $"#{流.索引}  {String.Join(" | ", 参数)}"
    End Function

    Private Shared Function 格式化字幕流(流 As 媒体流信息) As String
        Dim 参数 As New List(Of String) From {编码名称(流.编码, String.Empty)}
        添加元数据(参数, 流)
        Return $"#{流.索引}  {String.Join(" | ", 参数)}"
    End Function

    Private Shared Function 格式化外部字幕(字幕 As 外部字幕候选) As String
        Dim 文件名 = 缩短文本(Path.GetFileName(字幕.路径), 42)
        Return $"外部  {字幕.格式.ToString().ToUpperInvariant()} · {文件名}"
    End Function

    Private Shared Sub 添加元数据(参数 As ICollection(Of String), 流 As 媒体流信息)
        Dim 语言 = 语言名称(流.语言)
        If Not String.IsNullOrEmpty(语言) Then 参数.Add(语言)
        If Not String.IsNullOrWhiteSpace(流.标题) Then 参数.Add(缩短文本(流.标题.Trim(), 28))
        If 流.是强制流 Then
            参数.Add("强制")
        ElseIf 流.是默认流 Then
            参数.Add("默认")
        End If
    End Sub

    Private Shared Sub 添加比特率(参数 As ICollection(Of String), 比特率 As Long)
        If 比特率 >= 1_000_000 Then
            参数.Add($"{比特率 / 1_000_000.0:0.##} Mbps")
        ElseIf 比特率 >= 1_000 Then
            参数.Add($"{比特率 / 1_000.0:0.#} kbps")
        End If
    End Sub

    Private Shared Function 编码名称(编码 As String, 配置档次 As String) As String
        Dim 名称 As String
        Select Case If(编码, String.Empty).ToLowerInvariant()
            Case "h264" : 名称 = "H.264"
            Case "hevc" : 名称 = "H.265/HEVC"
            Case "av1" : 名称 = "AV1"
            Case "vp9" : 名称 = "VP9"
            Case "aac" : 名称 = "AAC"
            Case "ac3" : 名称 = "AC-3"
            Case "eac3" : 名称 = "E-AC-3"
            Case "truehd" : 名称 = "TrueHD"
            Case "dts" : 名称 = "DTS"
            Case "flac" : 名称 = "FLAC"
            Case "opus" : 名称 = "Opus"
            Case "subrip" : 名称 = "SRT"
            Case "ass" : 名称 = "ASS"
            Case "ssa" : 名称 = "SSA"
            Case "webvtt" : 名称 = "WebVTT"
            Case "mov_text" : 名称 = "MOV_TEXT"
            Case "hdmv_pgs_subtitle" : 名称 = "PGS"
            Case "dvd_subtitle" : 名称 = "VobSub"
            Case "dvb_subtitle" : 名称 = "DVB Subtitle"
            Case Else
                Dim 原值 = If(编码, String.Empty).Trim()
                名称 = If(原值.Length = 0, "未知编码", 原值.ToUpperInvariant())
        End Select
        If String.IsNullOrWhiteSpace(配置档次) OrElse
            名称.Contains(配置档次, StringComparison.OrdinalIgnoreCase) Then Return 名称
        Return $"{名称} {缩短文本(配置档次.Trim(), 18)}"
    End Function

    Private Shared Function 格式化采样率(采样率 As Integer) As String
        If 采样率 Mod 1000 = 0 Then Return $"{采样率 \ 1000} kHz"
        Return $"{采样率 / 1000.0:0.#} kHz"
    End Function

    Private Shared Function 格式化声道(声道数 As Integer) As String
        Select Case 声道数
            Case 1 : Return "单声道"
            Case 2 : Return "立体声"
            Case 6 : Return "5.1 声道"
            Case 8 : Return "7.1 声道"
            Case Else : Return $"{声道数} 声道"
        End Select
    End Function

    Private Shared Function 语言名称(语言 As String) As String
        Select Case If(语言, String.Empty).Trim().ToLowerInvariant()
            Case "", "und" : Return String.Empty
            Case "zh", "chi", "zho", "chs", "cht" : Return "中文"
            Case "en", "eng" : Return "英语"
            Case "ja", "jpn" : Return "日语"
            Case "ko", "kor" : Return "韩语"
            Case "fr", "fre", "fra" : Return "法语"
            Case "de", "ger", "deu" : Return "德语"
            Case "es", "spa" : Return "西班牙语"
            Case "ru", "rus" : Return "俄语"
            Case Else : Return 缩短文本(语言.Trim(), 12)
        End Select
    End Function

    Private Shared Function 缩短文本(文本 As String, 最大长度 As Integer) As String
        If String.IsNullOrEmpty(文本) OrElse 文本.Length <= 最大长度 Then Return If(文本, String.Empty)
        Return 文本.Substring(0, Math.Max(1, 最大长度 - 1)) & "…"
    End Function

    Private Sub 居中显示()
        Dim 预测大小 = 测量菜单大小()
        Dim x = Math.Max(0, (视频容器.ClientSize.Width - 预测大小.Width) \ 2)
        Dim y = Math.Max(0, (视频容器.ClientSize.Height - 预测大小.Height) \ 2)
        菜单.Show(视频容器, x, y)
    End Sub

    Private Function 测量菜单大小() As Size
        Dim scale = Math.Max(0.5F, 视频容器.DeviceDpi / 96.0F)
        Dim padL = CInt(菜单.MenuPadding.Left * scale)
        Dim padR = CInt(菜单.MenuPadding.Right * scale)
        Dim padT = CInt(菜单.MenuPadding.Top * scale)
        Dim padB = CInt(菜单.MenuPadding.Bottom * scale)
        Dim border = CInt(菜单.BorderSize * scale)
        Dim iconCol = If(菜单.IconSize > 0, CInt(菜单.IconSize * scale),
            If(菜单.Items.Any(Function(x) Not x.IsSeparator AndAlso Not x.IsDescription AndAlso x.Checked),
               CInt(20 * scale), 0))
        Dim iconGap = If(iconCol > 0, CInt(菜单.IconTextSpacing * scale), 0)
        Dim itemPad = CInt((菜单.ItemPadding.Left + 菜单.ItemPadding.Right) * scale)
        Dim maxContentWidth = CInt(80 * scale)
        Dim height = padT + border

        For Each 项 In 菜单.Items
            If 项.IsSeparator Then
                height += CInt(菜单.SeparatorHeight * scale)
                Continue For
            End If
            Dim 字体 = If(项.Font, If(项.IsDescription, 菜单.DescriptionFont, 菜单.MenuFont))
            Dim textWidth = TextRenderer.MeasureText(If(项.Text, String.Empty), 字体).Width
            maxContentWidth = Math.Max(maxContentWidth, itemPad + iconCol + iconGap + textWidth + CInt(20 * scale))
            height += CInt(If(项.IsDescription, 菜单.DescriptionItemHeight, 菜单.ItemHeight) * scale)
        Next
        Return New Size(maxContentWidth + padL + padR + border * 2 + 1,
                        height + padB + border + 1)
    End Function

    Public Sub Dispose() Implements IDisposable.Dispose
        If 已释放 Then Return
        已释放 = True
        菜单.Close()
        菜单.Items.Clear()
    End Sub
End Class
