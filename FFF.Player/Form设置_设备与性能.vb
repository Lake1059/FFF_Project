Public Class Form设置_设备与性能

    Private 正在初始化 As Boolean = True
    Private 设备列表 As New List(Of GPU解码设备)
    Private ReadOnly 枚举设备 As Func(Of IReadOnlyList(Of GPU解码设备))

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
            配置.规范化()
            设备列表 = New List(Of GPU解码设备) From {New GPU解码设备(String.Empty, "自动选择", -1)}
            Try
                设备列表.AddRange(枚举设备())
            Catch ex As Exception
                Debug.WriteLine("设备设置页：" & ex.Message)
            End Try
            Dim 选中 = 设备列表.FindIndex(Function(x) x.标识 = 配置.GPU解码设备标识)
            If 选中 < 0 Then
                选中 = 设备列表.Count
                设备列表.Add(New GPU解码设备(配置.GPU解码设备标识, "已保存设备当前不可用（暂用自动选择）", -1))
            End If
            MCB_GPU解码设备.Items.Clear()
            For Each 设备 In 设备列表
                MCB_GPU解码设备.Items.Add(设备.名称)
            Next
            MCB_GPU解码设备.SelectedIndex = 选中
            MCB_视频缓冲区策略.SelectedIndex = 配置.视频缓冲区策略
            MCB_音频缓冲区策略.SelectedIndex = 配置.音频缓冲区策略
            MCB_视频缩放质量.SelectedIndex = 配置.视频缩放质量选项
            MCB_空间音频.SelectedIndex = If(配置.启用空间音频, 0, 1)
        Finally
            正在初始化 = False
        End Try
    End Sub

    Private Sub 选项_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_GPU解码设备.SelectedIndexChanged,
        MCB_视频缓冲区策略.SelectedIndexChanged, MCB_音频缓冲区策略.SelectedIndexChanged,
        MCB_视频缩放质量.SelectedIndexChanged, MCB_空间音频.SelectedIndexChanged
        If 正在初始化 Then Return
        Dim 控件 = DirectCast(sender, LakeUI.ModernComboBox)
        If 控件.SelectedIndex < 0 Then Return
        Dim 配置 = 设置.实例对象
        If 控件 Is MCB_GPU解码设备 Then
            配置.GPU解码设备标识 = 设备列表(控件.SelectedIndex).标识
        ElseIf 控件 Is MCB_视频缓冲区策略 Then
            配置.视频缓冲区策略 = 控件.SelectedIndex
        ElseIf 控件 Is MCB_音频缓冲区策略 Then
            配置.音频缓冲区策略 = 控件.SelectedIndex
        ElseIf 控件 Is MCB_视频缩放质量 Then
            配置.视频缩放质量选项 = 控件.SelectedIndex
            Form1.当前主窗体?.应用视频缩放设置()
        ElseIf 控件 Is MCB_空间音频 Then
            配置.启用空间音频 = 控件.SelectedIndex = 0
            Form1.当前主窗体?.应用空间音频设置()
        End If
    End Sub
End Class
