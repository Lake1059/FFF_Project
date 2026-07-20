Imports System.Globalization

Public Class Form视频参数
    Private 正在初始化 As Boolean

    Friend Sub 初始化页面()
        正在初始化 = True
        Try
            设置索引(MCB_视频编码器, 设置.实例对象.视频编码器索引)
            设置索引(MCB_视频分辨率, 设置.实例对象.视频分辨率索引)
            MCB_自定义宽度.Text = 设置.实例对象.自定义视频宽度
            MCB_自定义高度.Text = 设置.实例对象.自定义视频高度
            MCK_是否捕获鼠标.Checked = 设置.实例对象.捕获鼠标
            更新分辨率依赖项()
            设置索引(MCB_帧率模式, 设置.实例对象.帧率模式)
            MCB_录制帧率.Items.Clear()
            For Each 帧率文本 In 录制帧率规则.预设
                MCB_录制帧率.Items.Add(帧率文本)
            Next
            Dim 当前帧率文本 = 录制帧率规则.格式化(CUInt(设置.实例对象.帧率), CUInt(设置.实例对象.帧率分母))
            Dim 预设索引 = 录制帧率规则.预设.ToList().IndexOf(当前帧率文本)
            If 预设索引 >= 0 Then
                MCB_录制帧率.SelectedIndex = 预设索引
            Else
                MCB_录制帧率.SelectedIndex = -1
                MCB_录制帧率.Text = 当前帧率文本
            End If
            设置索引(MCB_像素采样, 设置.实例对象.像素采样)
            设置索引(MCB_灰阶位深, 设置.实例对象.灰阶位深)
            设置索引(MCB_色彩模式, 设置.实例对象.色彩模式)
            设置索引(MCB_SDR亮度, {100, 200, 300}.ToList().IndexOf(设置.实例对象.SDR亮度), 2)
            设置索引(MCB_HDR最亮值, {400, 600, 800, 1000, 2000}.ToList().IndexOf(设置.实例对象.HDR峰值), 3)
            设置索引(MCB_质量控制模式, 设置.实例对象.质量控制模式)
            ETB_质量值.Value = 设置.实例对象.质量值
            MTB_自定义参数.Text = 设置.实例对象.自定义视频参数
            更新编码器依赖项(True)
        Finally
            正在初始化 = False
        End Try
    End Sub

    Friend Function 取得编码器名称() As String
        Select Case MCB_视频编码器.SelectedIndex
            Case 1 : Return "libsvtav1"
            Case 2 : Return "av1_nvenc"
            Case 3 : Return "av1_qsv"
            Case 4 : Return "av1_amf"
            Case 5 : Return "libx265"
            Case 6 : Return "hevc_nvenc"
            Case 7 : Return "hevc_qsv"
            Case 8 : Return "hevc_amf"
            Case 9 : Return "libx264"
            Case 10 : Return "h264_nvenc"
            Case 11 : Return "h264_qsv"
            Case 12 : Return "h264_amf"
            Case Else : Return "libx264"
        End Select
    End Function

    Friend Function 取得编码预设() As String
        If MCB_视频编码器.SelectedIndex = 0 Then Return "medium"
        Return If(MCB_编码预设.SelectedIndex >= 0, MCB_编码预设.Text, String.Empty)
    End Function

    Friend Function 取得输出尺寸(源宽度 As UInteger, 源高度 As UInteger) As (宽度 As UInteger, 高度 As UInteger)
        Select Case MCB_视频分辨率.SelectedIndex
            Case 1
                Return (解析自定义尺寸(MCB_自定义宽度.Text, 源宽度, "宽度"),
                        解析自定义尺寸(MCB_自定义高度.Text, 源高度, "高度"))
            Case 2 : Return (3840UI, 2160UI)
            Case 3 : Return (2560UI, 1440UI)
            Case 4 : Return (1920UI, 1080UI)
            Case 5 : Return (1600UI, 900UI)
            Case Else : Return (源宽度, 源高度)
        End Select
    End Function

    Friend Function 取得录制帧率() As (分子 As UInteger, 分母 As UInteger)
        Dim 分子 As UInteger
        Dim 分母 As UInteger
        If Not 录制帧率规则.尝试解析(MCB_录制帧率.Text, 分子, 分母) Then
            Throw New InvalidOperationException(
                $"录制帧率无效，请输入 {录制帧率规则.最小帧率} 到 {录制帧率规则.最大帧率} 之间的数字或分数。")
        End If
        设置.实例对象.帧率 = CInt(分子)
        设置.实例对象.帧率分母 = CInt(分母)
        Return (分子, 分母)
    End Function

    Private Shared Function 解析自定义尺寸(文本 As String, 源尺寸 As UInteger, 尺寸名称 As String) As UInteger
        Dim 值 = If(文本, String.Empty).Trim()
        Dim 结果 As UInteger
        If UInteger.TryParse(值, NumberStyles.None, CultureInfo.InvariantCulture, 结果) AndAlso 结果 > 0UI Then
            If 结果 > 16384UI Then Throw New InvalidOperationException($"自定义{尺寸名称}不能超过 16384。")
            Return 结果
        End If
        If 值.StartsWith("x", StringComparison.OrdinalIgnoreCase) Then
            Dim 倍率 As Double
            If Double.TryParse(值.Substring(1), NumberStyles.Float, CultureInfo.InvariantCulture, 倍率) AndAlso 倍率 > 0 AndAlso Double.IsFinite(倍率) Then
                Dim 缩放后 = Math.Round(源尺寸 * 倍率, MidpointRounding.AwayFromZero)
                If 缩放后 >= 1 AndAlso 缩放后 <= 16384 Then Return CUInt(缩放后)
            End If
        End If
        Throw New InvalidOperationException($"自定义{尺寸名称}无效，请输入 1 到 16384 的整数或 x0.5 形式的倍率。")
    End Function

    Private Sub MCB_视频编码器_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_视频编码器.SelectedIndexChanged
        If 正在初始化 Then Return
        设置.实例对象.视频编码器索引 = MCB_视频编码器.SelectedIndex
        设置.实例对象.视频编码器名称 = 取得编码器名称()
        更新编码器依赖项()
        设置.实例对象.视频预设 = 取得编码预设()
    End Sub

    Private Sub MCB_编码预设_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_编码预设.SelectedIndexChanged
        If Not 正在初始化 Then 设置.实例对象.视频预设 = If(MCB_编码预设.SelectedIndex <= 0, String.Empty, MCB_编码预设.Text)
    End Sub

    Private Sub MCB_配置文件_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_配置文件.SelectedIndexChanged
        If Not 正在初始化 Then 设置.实例对象.视频配置文件 = If(MCB_配置文件.SelectedIndex <= 0, String.Empty, MCB_配置文件.Text)
    End Sub

    Private Sub MCB_场景优化_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_场景优化.SelectedIndexChanged
        If Not 正在初始化 Then 设置.实例对象.视频场景优化 = If(MCB_场景优化.SelectedIndex <= 0, String.Empty, MCB_场景优化.Text)
    End Sub

    Private Sub MCB_视频分辨率_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_视频分辨率.SelectedIndexChanged
        If Not 正在初始化 Then 设置.实例对象.视频分辨率索引 = Math.Max(0, MCB_视频分辨率.SelectedIndex)
        更新分辨率依赖项()
    End Sub

    Private Sub 自定义分辨率_TextChanged(sender As Object, e As EventArgs) Handles MCB_自定义宽度.TextChanged, MCB_自定义高度.TextChanged
        If 正在初始化 Then Return
        设置.实例对象.自定义视频宽度 = MCB_自定义宽度.Text.Trim()
        设置.实例对象.自定义视频高度 = MCB_自定义高度.Text.Trim()
    End Sub

    Private Sub MCK_是否捕获鼠标_CheckedChanged(sender As Object, e As EventArgs) Handles MCK_是否捕获鼠标.CheckedChanged
        If Not 正在初始化 Then 设置.实例对象.捕获鼠标 = MCK_是否捕获鼠标.Checked
    End Sub

    Private Sub MCB_帧率模式_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_帧率模式.SelectedIndexChanged
        If Not 正在初始化 Then 设置.实例对象.帧率模式 = Math.Max(0, MCB_帧率模式.SelectedIndex)
    End Sub

    Private Sub MCB_录制帧率_Changed(sender As Object, e As EventArgs) Handles MCB_录制帧率.SelectedIndexChanged, MCB_录制帧率.TextChanged
        If 正在初始化 Then Return
        Dim 分子 As UInteger
        Dim 分母 As UInteger
        If 录制帧率规则.尝试解析(MCB_录制帧率.Text, 分子, 分母) Then
            设置.实例对象.帧率 = CInt(分子)
            设置.实例对象.帧率分母 = CInt(分母)
        End If
    End Sub

    Private Sub MCB_像素采样_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_像素采样.SelectedIndexChanged
        If Not 正在初始化 Then 设置.实例对象.像素采样 = Math.Max(0, MCB_像素采样.SelectedIndex)
    End Sub

    Private Sub MCB_灰阶位深_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_灰阶位深.SelectedIndexChanged
        If Not 正在初始化 Then 设置.实例对象.灰阶位深 = Math.Max(0, MCB_灰阶位深.SelectedIndex)
    End Sub

    Private Sub MCB_色彩模式_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_色彩模式.SelectedIndexChanged
        If Not 正在初始化 Then 设置.实例对象.色彩模式 = Math.Max(0, MCB_色彩模式.SelectedIndex)
    End Sub

    Private Sub MCB_SDR亮度_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_SDR亮度.SelectedIndexChanged
        If Not 正在初始化 AndAlso MCB_SDR亮度.SelectedIndex >= 0 Then 设置.实例对象.SDR亮度 = Integer.Parse(MCB_SDR亮度.Text.Replace("nit", ""))
    End Sub

    Private Sub MCB_HDR最亮值_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_HDR最亮值.SelectedIndexChanged
        If Not 正在初始化 AndAlso MCB_HDR最亮值.SelectedIndex >= 0 Then 设置.实例对象.HDR峰值 = Integer.Parse(MCB_HDR最亮值.Text.Replace("nit", ""))
    End Sub

    Private Sub MCB_质量控制模式_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_质量控制模式.SelectedIndexChanged
        If Not 正在初始化 Then 设置.实例对象.质量控制模式 = Math.Max(0, MCB_质量控制模式.SelectedIndex)
    End Sub

    Private Sub ETB_质量值_ValueChanged(sender As Object, e As EventArgs) Handles ETB_质量值.ValueChanged
        If Not 正在初始化 Then 设置.实例对象.质量值 = ETB_质量值.Value
    End Sub

    Private Sub MTB_自定义参数_TextChanged(sender As Object, e As EventArgs) Handles MTB_自定义参数.TextChanged
        If Not 正在初始化 Then 设置.实例对象.自定义视频参数 = MTB_自定义参数.Text
    End Sub

    Private Sub 更新分辨率依赖项()
        Dim 启用自定义 = MCB_视频分辨率.SelectedIndex = 1
        MCB_自定义宽度.Enabled = 启用自定义
        MCB_自定义高度.Enabled = 启用自定义
    End Sub

    Private Sub 更新编码器依赖项(Optional 强制 As Boolean = False)
        If 正在初始化 AndAlso Not 强制 Then Return
        Dim 原初始化状态 = 正在初始化
        正在初始化 = True
        Try
            Dim encoder = 取得编码器名称()
            Dim presets As New List(Of String) From {String.Empty}
            Dim profiles As New List(Of String) From {String.Empty}
            Dim scenes As New List(Of String) From {String.Empty}
            If encoder.Contains("x264") OrElse encoder.Contains("x265") Then presets.AddRange({"ultrafast", "fast", "medium", "slow", "veryslow"})
            If encoder.Contains("nvenc") Then presets.AddRange({"p1", "p2", "p3", "p4", "p5", "p6", "p7"})
            If encoder.Contains("hevc") Then profiles.AddRange({"main", "main10", "rext"})
            If encoder.Contains("av1") Then scenes.AddRange({"good", "realtime"})
            Dim 当前预设 = If(MCB_视频编码器.SelectedIndex = 0, "medium", 设置.实例对象.视频预设)
            重建下拉(MCB_编码预设, presets, 当前预设)
            重建下拉(MCB_配置文件, profiles, 设置.实例对象.视频配置文件)
            重建下拉(MCB_场景优化, scenes, 设置.实例对象.视频场景优化)
        Finally
            正在初始化 = 原初始化状态
        End Try
    End Sub

    Private Shared Sub 重建下拉(控件 As LakeUI.ModernComboBox, 候选 As IEnumerable(Of String), 当前值 As String)
        Dim 候选列表 = 候选.ToList()
        控件.Items.Clear()
        For Each 值 In 候选列表
            控件.Items.Add(值)
        Next
        Dim 索引 = 候选列表.IndexOf(If(当前值, String.Empty))
        控件.SelectedIndex = If(索引 >= 0, 索引, 0)
    End Sub

    Private Shared Sub 设置索引(控件 As LakeUI.ModernComboBox, 索引 As Integer, Optional 回退 As Integer = 0)
        If 控件.Items.Count = 0 Then Return
        If 索引 < 0 OrElse 索引 >= 控件.Items.Count Then 索引 = Math.Clamp(回退, 0, 控件.Items.Count - 1)
        控件.SelectedIndex = 索引
    End Sub

    Private Sub Form视频参数_Load(sender As Object, e As EventArgs) Handles MyBase.Load

    End Sub
End Class
