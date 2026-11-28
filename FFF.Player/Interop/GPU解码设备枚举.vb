Imports Vortice.DXGI

' DXGI 序号只在本次枚举有效；设置保存型号标识，打开媒体时重新解析。
Friend NotInheritable Class GPU解码设备
    Friend Sub New(标识 As String, 名称 As String, 索引 As Integer)
        Me.标识 = 标识
        Me.名称 = 名称
        Me.索引 = 索引
    End Sub

    Friend ReadOnly 标识 As String
    Friend ReadOnly 名称 As String
    Friend ReadOnly 索引 As Integer

    Friend Shared Function 枚举() As IReadOnlyList(Of GPU解码设备)
        Dim 结果 As New List(Of GPU解码设备)
        Dim 同型计数 As New Dictionary(Of String, Integer)(StringComparer.Ordinal)
        Using 工厂 = DXGI.CreateDXGIFactory1(Of IDXGIFactory1)()
            For 索引 As UInteger = 0 To 15
                Dim 适配器 As IDXGIAdapter1 = Nothing
                If 工厂.EnumAdapters1(索引, 适配器).Failure Then Exit For
                Using 适配器
                    Dim 描述 = 适配器.Description1
                    If (描述.Flags And AdapterFlags.Software) <> 0 Then Continue For
                    Dim 型号 = $"{描述.VendorId:X8}:{描述.DeviceId:X8}:{描述.SubsystemId:X8}:{描述.Revision:X8}:{描述.Description}"
                    Dim 序号 As Integer
                    同型计数.TryGetValue(型号, 序号)
                    同型计数(型号) = 序号 + 1
                    结果.Add(New GPU解码设备($"{型号}:{序号}", 描述.Description, CInt(索引)))
                End Using
            Next
        End Using
        ' 同型号多卡（实测双 RTX 4060 Laptop）的 DXGI 描述逐字相同，下拉框与信息层
        ' 都会出现两个无法区分的条目 ⇒ 重名时统一追加 DXGI 索引后缀。
        ' 后缀用**索引**而不是同型号序号：索引正是内核回报给宿主的那一个数字，
        ' 信息层显示的名字与"实际解码在 #几"能直接对上。
        Dim 名称次数 As New Dictionary(Of String, Integer)(StringComparer.Ordinal)
        For Each 设备 In 结果
            Dim 键 = 名称键(设备.名称)
            名称次数(键) = 名称次数.GetValueOrDefault(键) + 1
        Next
        For 位置 = 0 To 结果.Count - 1
            Dim 设备 = 结果(位置)
            Dim 键 = 名称键(设备.名称)
            Dim 显示 = If(键.Length = 0, $"适配器 {设备.索引}", 设备.名称)
            If 名称次数(键) > 1 Then 显示 = $"{显示} #{设备.索引}"
            结果(位置) = New GPU解码设备(设备.标识, 显示, 设备.索引)
        Next
        Return 结果
    End Function

    ''' <summary>按 DXGI 索引取设置页同款显示名（信息层用）。
    ''' <para>复用 <see cref="枚举"/> 而不是另写一套 DXGI 查询：设置下拉与信息层必须
    ''' 显示同一个名字，否则用户会以为播放器偷偷换了张卡。</para>
    ''' <para>找不到（索引失效 / 软件适配器 / 枚举失败）返回空字符串，调用方据此退回
    ''' 只显示"GPU"——绝不编一个名字。</para></summary>
    Friend Shared Function 按索引名称(索引 As Integer) As String
        If 索引 < 0 Then Return String.Empty
        Try
            For Each 设备 In 枚举()
                If 设备.索引 = 索引 Then Return 设备.名称
            Next
        Catch ex As Exception
            System.Diagnostics.Debug.WriteLine("枚举 GPU 解码设备失败：" & ex.Message)
        End Try
        Return String.Empty
    End Function

    Private Shared Function 名称键(名称 As String) As String
        Return If(名称, String.Empty).Trim()
    End Function
End Class
