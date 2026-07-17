Imports Vortice.Direct3D11

Friend NotInheritable Class D3D11纹理池
    Implements IDisposable

    Private ReadOnly 图形 As 图形设备
    Private ReadOnly 最大数量 As Integer
    Private ReadOnly 同步锁 As New Object
    Private ReadOnly 空闲纹理 As New Stack(Of ID3D11Texture2D)
    Private 已创建数量 As Integer
    Private 已释放 As Boolean

    Friend Sub New(设备 As 图形设备, 容量 As Integer)
        图形 = 设备
        最大数量 = 容量
    End Sub

    Friend Function 尝试租用(描述 As Texture2DDescription) As ID3D11Texture2D
        SyncLock 同步锁
            If 已释放 Then Throw New ObjectDisposedException(NameOf(D3D11纹理池))
            While 空闲纹理.Count > 0
                Dim 候选 = 空闲纹理.Pop()
                If 描述相同(候选.Description, 描述) Then Return 候选
                候选.Dispose()
                已创建数量 -= 1
            End While
            If 已创建数量 >= 最大数量 Then Return Nothing
            Dim 新纹理 As ID3D11Texture2D = Nothing
            图形.执行图形命令(Sub() 新纹理 = 图形.设备.CreateTexture2D(描述))
            已创建数量 += 1
            Return 新纹理
        End SyncLock
    End Function

    Friend Sub 归还(纹理 As ID3D11Texture2D)
        If 纹理 Is Nothing Then Return
        SyncLock 同步锁
            If 已释放 Then
                纹理.Dispose()
                Return
            End If
            空闲纹理.Push(纹理)
        End SyncLock
    End Sub

    Private Shared Function 描述相同(左 As Texture2DDescription, 右 As Texture2DDescription) As Boolean
        Return 左.Width = 右.Width AndAlso 左.Height = 右.Height AndAlso 左.Format = 右.Format AndAlso
            左.ArraySize = 右.ArraySize AndAlso 左.MipLevels = 右.MipLevels AndAlso
            左.SampleDescription.Count = 右.SampleDescription.Count AndAlso
            左.SampleDescription.Quality = 右.SampleDescription.Quality AndAlso
            左.BindFlags = 右.BindFlags AndAlso 左.Usage = 右.Usage AndAlso
            左.CPUAccessFlags = 右.CPUAccessFlags AndAlso 左.MiscFlags = 右.MiscFlags
    End Function

    Friend Sub 释放() Implements IDisposable.Dispose
        SyncLock 同步锁
            If 已释放 Then Return
            已释放 = True
            While 空闲纹理.Count > 0
                空闲纹理.Pop().Dispose()
                已创建数量 -= 1
            End While
        End SyncLock
    End Sub
End Class
