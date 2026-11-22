Imports System.Threading.Tasks

Friend Module 后台任务
    Friend Async Sub 观察(任务 As Task)
        If 任务 Is Nothing Then Return
        Try
            Await 任务.ConfigureAwait(False)
        Catch ex As Exception
            Debug.WriteLine(ex)
        End Try
    End Sub
End Module
