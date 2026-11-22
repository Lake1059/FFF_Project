Imports LakeUI

Friend Module LakeUI渲染后端
    Friend ReadOnly Property 当前 As RenderBackend
        Get
            Return If(GlobalOptions.ActiveBackend, If(GlobalOptions.RequestedBackend, RenderBackend.Direct3D11))
        End Get
    End Property

    Friend Async Function 尝试切换Async(请求 As RenderBackend) As Task(Of Boolean)
        Try
            Dim 结果 = Await GlobalOptions.SetRenderBackendAsync(请求)
            Return 结果 = BackendSelectionResult.AlreadyActive OrElse 结果 = BackendSelectionResult.Switched
        Catch ex As Exception
            Debug.WriteLine("LakeUI 渲染引擎切换失败：" & ex.Message)
            Return False
        End Try
    End Function
End Module
