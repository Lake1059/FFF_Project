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
        Return 结果
    End Function
End Class
