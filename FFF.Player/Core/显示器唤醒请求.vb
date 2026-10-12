Imports System.Runtime.InteropServices

''' <summary>
''' 在视频实际播放期间阻止 Windows 自动关闭显示器。
''' </summary>
Public NotInheritable Class 显示器唤醒请求
    Implements IDisposable

    Private Const ES_DISPLAY_REQUIRED As UInteger = &H2UI
    Private Const ES_CONTINUOUS As UInteger = &H80000000UI

    Private 已请求 As Boolean
    Private 已释放 As Boolean

    <DllImport("kernel32.dll", SetLastError:=True)>
    Private Shared Function SetThreadExecutionState(执行状态 As UInteger) As UInteger
    End Function

    ''' <summary>根据当前快照更新显示器唤醒请求。</summary>
    Public Sub 更新(快照 As 播放器快照)
        If 已释放 Then Return
        Dim 需要请求 = 快照 IsNot Nothing AndAlso
            快照.状态 = 播放状态.正在播放 AndAlso 快照.当前视频流 >= 0
        If 需要请求 = 已请求 Then Return

        If 需要请求 Then
            ' 仅阻止显示器休眠；系统睡眠策略保持不变。
            If SetThreadExecutionState(ES_CONTINUOUS Or ES_DISPLAY_REQUIRED) <> 0UI Then
                已请求 = True
            End If
        Else
            SetThreadExecutionState(ES_CONTINUOUS)
            已请求 = False
        End If
    End Sub

    Public Sub 释放() Implements IDisposable.Dispose
        If 已释放 Then Return
        已释放 = True
        If 已请求 Then
            SetThreadExecutionState(ES_CONTINUOUS)
            已请求 = False
        End If
        GC.SuppressFinalize(Me)
    End Sub

End Class
