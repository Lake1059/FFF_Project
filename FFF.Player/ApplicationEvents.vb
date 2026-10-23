Imports Microsoft.VisualBasic.ApplicationServices

Namespace My
    Partial Friend Class MyApplication
        Private 待处理启动文件 As String = String.Empty

        Private Sub MyApplication_Startup(sender As Object, e As StartupEventArgs) Handles Me.Startup
            记录初次启动命令行(e.CommandLine)
        End Sub

        Private Sub MyApplication_StartupNextInstance(sender As Object, e As StartupNextInstanceEventArgs) Handles Me.StartupNextInstance
            e.BringToForeground = True
            Dim 文件路径 = Form1.取得命令行文件(e.CommandLine)
            If String.IsNullOrEmpty(文件路径) Then Return
            Dim 主窗体 = TryCast(MainForm, Form1)
            If 主窗体 Is Nothing Then
                待处理启动文件 = 文件路径
            Else
                主窗体.打开命令行文件({文件路径})
            End If
        End Sub

        Friend Function 取出待处理启动文件() As String
            Dim 文件路径 = 待处理启动文件
            待处理启动文件 = String.Empty
            Return 文件路径
        End Function

        Friend Sub 记录初次启动命令行(参数 As IEnumerable(Of String))
            待处理启动文件 = Form1.取得命令行文件(参数)
        End Sub
    End Class
End Namespace
