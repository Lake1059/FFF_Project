Imports System.Collections.Concurrent
Imports System.Threading

Friend NotInheritable Class 图形命令调度器
    Implements IDisposable

    Private NotInheritable Class 工作项
        Friend ReadOnly 操作 As Action
        Friend ReadOnly 完成 As New ManualResetEventSlim(False)
        Friend Property 异常 As Exception

        Friend Sub New(图形操作 As Action)
            操作 = 图形操作
        End Sub
    End Class

    Private ReadOnly 队列 As New BlockingCollection(Of 工作项)(64)
    Private ReadOnly 线程 As Thread
    Private 已释放 As Boolean

    Friend Sub New()
        线程 = New Thread(AddressOf 运行) With {.IsBackground = True, .Name = "FFF GPU Pipeline"}
        线程.Start()
    End Sub

    Friend Sub 执行(操作 As Action)
        ArgumentNullException.ThrowIfNull(操作)
        If 已释放 Then Throw New ObjectDisposedException(NameOf(图形命令调度器))
        If Thread.CurrentThread Is 线程 Then
            操作()
            Return
        End If
        Dim 项目 As New 工作项(操作)
        队列.Add(项目)
        项目.完成.Wait()
        项目.完成.Dispose()
        If 项目.异常 IsNot Nothing Then Throw New InvalidOperationException("GPU Pipeline 命令执行失败。", 项目.异常)
    End Sub

    Private Sub 运行()
        For Each 项目 In 队列.GetConsumingEnumerable()
            Try
                项目.操作()
            Catch 错误 As Exception
                项目.异常 = 错误
            Finally
                项目.完成.Set()
            End Try
        Next
    End Sub

    Friend Sub 释放() Implements IDisposable.Dispose
        If 已释放 Then Return
        队列.CompleteAdding()
        If Thread.CurrentThread IsNot 线程 Then 线程.Join()
        队列.Dispose()
        已释放 = True
    End Sub
End Class
