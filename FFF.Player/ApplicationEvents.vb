Imports Microsoft.VisualBasic.ApplicationServices

Namespace My
    ' The following events are available for MyApplication:
    ' Startup: Raised when the application starts, before the startup form is created.
    ' Shutdown: Raised after all application forms are closed. This event is not raised if the application terminates abnormally.
    ' UnhandledException: Raised if the application encounters an unhandled exception.
    ' StartupNextInstance: Raised when launching a single-instance application and the application is already active.
    ' NetworkAvailabilityChanged: Raised when the network connection is connected or disconnected.

    ' **NEW** ApplyApplicationDefaults: Raised when the application queries default values to be set for the application.

    ' Example:
    ' Private Sub MyApplication_ApplyApplicationDefaults(sender As Object, e As ApplyApplicationDefaultsEventArgs) Handles Me.ApplyApplicationDefaults
    '
    '   ' Setting the application-wide default Font:
    '   e.Font = New Font(FontFamily.GenericSansSerif, 12, FontStyle.Regular)
    '
    '   ' Setting the HighDpiMode for the Application:
    '   e.HighDpiMode = HighDpiMode.PerMonitorV2
    '
    '   ' If a splash dialog is used, this sets the minimum display time:
    '   e.MinimumSplashScreenDisplayTime = 4000
    ' End Sub

    Partial Friend Class MyApplication
        Private 待处理启动文件 As String = String.Empty

        Private Sub MyApplication_Startup(
            sender As Object, e As StartupEventArgs) Handles Me.Startup

            记录初次启动命令行(e.CommandLine)
        End Sub

        Private Sub MyApplication_StartupNextInstance(
            sender As Object, e As StartupNextInstanceEventArgs) Handles Me.StartupNextInstance

            e.BringToForeground = True
            Dim 文件路径 = Form1.取得命令行文件(e.CommandLine)
            If String.IsNullOrEmpty(文件路径) Then Return
            Dim mainWindow = TryCast(MainForm, Form1)
            If mainWindow Is Nothing Then
                待处理启动文件 = 文件路径
            Else
                mainWindow.打开命令行文件({文件路径})
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
