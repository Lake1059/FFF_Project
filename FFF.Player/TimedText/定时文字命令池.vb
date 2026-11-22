''' <summary>各文字图层在同步提交后复用命令，下一帧从池首重新取用。</summary>
Friend NotInheritable Class 定时文字命令池
    Private ReadOnly 命令 As List(Of 定时文字命令)
    Private 使用数 As Integer

    Friend Sub New(容量 As Integer)
        命令 = New List(Of 定时文字命令)(容量)
    End Sub

    Friend Sub 开始帧()
        使用数 = 0
    End Sub

    Friend Function 取得() As 定时文字命令
        If 使用数 = 命令.Count Then 命令.Add(New 定时文字命令())
        使用数 += 1
        Return 命令(使用数 - 1)
    End Function

    Friend Sub 清空()
        命令.Clear()
        开始帧()
    End Sub
End Class
