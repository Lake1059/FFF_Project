Imports LakeUI

Public Class Form设置_设备与性能
    Private 正在初始化 As Boolean = True
    Private 正在切换LakeUI As Boolean
    Private ReadOnly 枚举设备 As Func(Of IReadOnlyList(Of GPU解码设备))
    Private ReadOnly 设备列表 As New List(Of GPU解码设备)

    Public Sub New()
        Me.New(AddressOf GPU解码设备.枚举)
    End Sub

    Friend Sub New(枚举设备 As Func(Of IReadOnlyList(Of GPU解码设备)))
        Me.枚举设备 = 枚举设备
        InitializeComponent()
        正在初始化 = False
    End Sub

    Friend Sub 初始化页面()
        正在初始化 = True
        Try
            Dim 配置 = 设置.实例对象
            回显GPU设备(配置.GPU解码设备标识)
            MCB_视频渲染引擎API.SelectedIndex = CInt(配置.视频渲染引擎)
            MCB_视频渲染引擎API.Enabled = 播放器原生接口.支持渲染后端
            If Not 正在切换LakeUI Then MCB_LakeUI渲染引擎API.SelectedIndex = CInt(LakeUI渲染后端.当前)
            MCB_视频缓冲区策略.SelectedIndex = 配置.视频缓冲区策略
            MCB_音频缓冲区策略.SelectedIndex = 配置.音频缓冲区策略
            MCB_视频缩放质量.SelectedIndex = 配置.视频缩放质量选项
            MCB_音频效果.SelectedIndex = If(配置.启用单声道音频, 2, If(配置.启用空间音频, 1, 0))
            MCB_音频直通.SelectedIndex = 配置.音频直通选项
            刷新蓝牙设备显示()
            MCK_蓝牙延迟补偿.Checked = 配置.蓝牙低延迟模式
            MCB_蓝牙编码.SelectedIndex = 配置.蓝牙音频编码
            ETB_蓝牙额外延迟补偿.Value = 配置.蓝牙额外提前量毫秒
            更新蓝牙控件状态()
            MCK_音频响度标准化.Checked = 配置.启用音频响度标准化
            MCB_目标响度.SelectedIndex = 设置.最高音频目标响度 - 配置.音频目标响度
            MCB_动态范围.SelectedIndex = 配置.音频动态范围 - 1
            更新响度控件状态()
        Finally
            正在初始化 = False
        End Try
    End Sub

    Private Sub 同步行内下拉框宽度(sender As Object, e As LayoutEventArgs) Handles ModernPanel6.Layout, ModernPanel2.Layout,
        ModernPanel5.Layout, ModernPanel7.Layout
        If sender Is ModernPanel6 Then
            Form设置.均分设置列(ModernPanel6, JustEmptyControl1.Width, MCB_视频渲染引擎API)
        ElseIf sender Is ModernPanel2 Then
            Form设置.均分设置列(ModernPanel2, JustEmptyControl2.Width, MCB_视频缓冲区策略)
            MCB_视频缩放质量.Width = MCB_视频缓冲区策略.Width
        ElseIf sender Is ModernPanel5 Then
            Form设置.均分设置列(ModernPanel5, JustEmptyControl6.Width, MCB_音频效果)
        ElseIf sender Is ModernPanel7 Then
            Form设置.均分设置列(ModernPanel7, MCK_音频响度标准化.Width + JustEmptyControl7.Width + JustEmptyControl5.Width, MCB_目标响度)
        End If
    End Sub

    Private Sub 回显GPU设备(保存标识 As String)
        设备列表.Clear()
        Try
            设备列表.AddRange(枚举设备())
        Catch ex As Exception
            Debug.WriteLine("枚举 GPU 解码设备失败：" & ex.Message)
        End Try
        While MCB_GPU视频解码设备.Items.Count > 1
            MCB_GPU视频解码设备.Items.RemoveAt(MCB_GPU视频解码设备.Items.Count - 1)
        End While
        For Each 设备 In 设备列表
            MCB_GPU视频解码设备.Items.Add(设备.名称)
        Next
        Dim 选中 = If(保存标识.Length = 0, 0, 设备列表.FindIndex(Function(x) x.标识 = 保存标识) + 1)
        If 保存标识.Length > 0 AndAlso 选中 = 0 Then
            设备列表.Add(New GPU解码设备(保存标识, "已保存设备不可用（暂用自动选择）", -1))
            MCB_GPU视频解码设备.Items.Add(设备列表.Last().名称)
            选中 = 设备列表.Count
        End If
        MCB_GPU视频解码设备.SelectedIndex = 选中
    End Sub

    Private Sub 选项_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_GPU视频解码设备.SelectedIndexChanged,
        MCB_视频缓冲区策略.SelectedIndexChanged, MCB_音频缓冲区策略.SelectedIndexChanged, MCB_视频缩放质量.SelectedIndexChanged,
        MCB_音频效果.SelectedIndexChanged, MCB_音频直通.SelectedIndexChanged
        If 正在初始化 Then Return
        Dim 控件 = DirectCast(sender, ModernComboBox)
        Dim 选项 = 控件.SelectedIndex
        If 选项 < 0 Then Return
        Dim 配置 = 设置.实例对象
        If 控件 Is MCB_GPU视频解码设备 Then
            配置.GPU解码设备标识 = If(选项 = 0, "", 设备列表(选项 - 1).标识)
        ElseIf 控件 Is MCB_视频缓冲区策略 Then
            配置.视频缓冲区策略 = 选项
        ElseIf 控件 Is MCB_音频缓冲区策略 Then
            配置.音频缓冲区策略 = 选项
        ElseIf 控件 Is MCB_视频缩放质量 Then
            配置.视频缩放质量选项 = 选项
        ElseIf 控件 Is MCB_音频效果 Then
            配置.启用空间音频 = 选项 = 1
            配置.启用单声道音频 = 选项 = 2
            Form1.当前主窗体?.应用音频效果设置()
        ElseIf 控件 Is MCB_音频直通 Then
            配置.音频直通选项 = 选项
            Form1.当前主窗体?.应用音频直通设置()
        End If
    End Sub

    Friend Sub 刷新蓝牙设备显示(Optional 信息 As 蓝牙音频信息 = Nothing)
        信息 = If(信息, 蓝牙音频检测器.检测默认输出())
        Dim 文案 = "仅输出到蓝牙设备时生效，推荐选择自动识别协议编码，还可使用滑块进行额外调整<br>" &
            $"当前蓝牙设备：{Net.WebUtility.HtmlEncode(信息.显示设备名称)} | 当前编码协议：{Net.WebUtility.HtmlEncode(信息.显示协议名称)}"
        If HCL_蓝牙延迟补偿说明和显示.Text <> 文案 Then HCL_蓝牙延迟补偿说明和显示.Text = 文案
    End Sub

    Private Sub 蓝牙设置改变(sender As Object, e As EventArgs) Handles MCK_蓝牙延迟补偿.CheckedChanged,
        MCB_蓝牙编码.SelectedIndexChanged, ETB_蓝牙额外延迟补偿.ValueChanged
        If 正在初始化 OrElse MCB_蓝牙编码.SelectedIndex < 0 Then Return
        Dim 配置 = 设置.实例对象
        配置.蓝牙低延迟模式 = MCK_蓝牙延迟补偿.Checked
        配置.蓝牙音频编码 = MCB_蓝牙编码.SelectedIndex
        配置.蓝牙额外提前量毫秒 = CInt(ETB_蓝牙额外延迟补偿.Value)
        更新蓝牙控件状态()
        Form1.当前主窗体?.应用蓝牙音频延迟()
    End Sub

    Private Sub 更新蓝牙控件状态()
        MCB_蓝牙编码.Enabled = MCK_蓝牙延迟补偿.Checked
        ETB_蓝牙额外延迟补偿.Enabled = MCK_蓝牙延迟补偿.Checked
    End Sub

    Private Sub 响度设置改变(sender As Object, e As EventArgs) Handles MCK_音频响度标准化.CheckedChanged,
        MCB_目标响度.SelectedIndexChanged, MCB_动态范围.SelectedIndexChanged
        If 正在初始化 OrElse MCB_目标响度.SelectedIndex < 0 OrElse MCB_动态范围.SelectedIndex < 0 Then Return
        Dim 配置 = 设置.实例对象
        配置.启用音频响度标准化 = MCK_音频响度标准化.Checked
        配置.音频目标响度 = 设置.最高音频目标响度 - MCB_目标响度.SelectedIndex
        配置.音频动态范围 = MCB_动态范围.SelectedIndex + 1
        更新响度控件状态()
        Form1.当前主窗体?.应用音频响度设置()
    End Sub

    Private Sub 更新响度控件状态()
        Dim 可用 = 播放器原生接口.支持音频响度设置
        MCK_音频响度标准化.Enabled = 可用
        MCB_目标响度.Enabled = 可用 AndAlso MCK_音频响度标准化.Checked
        MCB_动态范围.Enabled = 可用 AndAlso MCK_音频响度标准化.Checked
    End Sub

    Private Sub 视频渲染后端_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_视频渲染引擎API.SelectedIndexChanged
        If 正在初始化 OrElse MCB_视频渲染引擎API.SelectedIndex < 0 Then Return
        设置.实例对象.视频渲染引擎 = CType(MCB_视频渲染引擎API.SelectedIndex, 播放器渲染引擎)
    End Sub

    Private Async Sub LakeUI后端_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_LakeUI渲染引擎API.SelectedIndexChanged
        If 正在初始化 OrElse 正在切换LakeUI OrElse MCB_LakeUI渲染引擎API.SelectedIndex < 0 Then Return
        正在切换LakeUI = True
        MCB_LakeUI渲染引擎API.Enabled = False
        Dim 成功 As Boolean
        Try
            Dim 请求 = CType(MCB_LakeUI渲染引擎API.SelectedIndex, RenderBackend)
            成功 = Await LakeUI渲染后端.尝试切换Async(请求)
            If 成功 Then
                设置.实例对象.LakeUI渲染引擎 = 请求
                设置.退出时保存设置()
            End If
        Finally
            If Not IsDisposed Then
                MCB_LakeUI渲染引擎API.SelectedIndex = CInt(LakeUI渲染后端.当前)
                MCB_LakeUI渲染引擎API.Enabled = True
            End If
            正在切换LakeUI = False
        End Try
        If Not 成功 AndAlso Not IsDisposed AndAlso Visible Then
            ExOverlayMsgBox(FindForm(), "无法切换 LakeUI 渲染引擎，当前使用 " &
                            If(LakeUI渲染后端.当前 = RenderBackend.Direct3D12, "Direct3D 12", "Direct3D 11"),
                            MsgBoxStyle.Exclamation, "LakeUI 渲染引擎")
        End If
    End Sub
End Class
