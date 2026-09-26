Imports System.Diagnostics
Imports System.IO
Imports System.Runtime.InteropServices
Imports System.Threading
Imports System.Threading.Tasks
Imports Microsoft.Win32

Friend Enum 文件关联类别
    常见视频
    不常见视频
    老旧视频
    常见音频
    不常见音频
    老旧音频
    常见图片
    不常见图片
    老旧图片
End Enum

Friend NotInheritable Class 文件关联选项
    Public Property 关联常见视频 As Boolean
    Public Property 关联不常见视频 As Boolean
    Public Property 关联老旧视频 As Boolean
    Public Property 关联常见音频 As Boolean
    Public Property 关联不常见音频 As Boolean
    Public Property 关联老旧音频 As Boolean
    Public Property 关联常见图片 As Boolean
    Public Property 关联不常见图片 As Boolean
    Public Property 关联老旧图片 As Boolean

    Friend Shared Function 从设置(值 As 设置) As 文件关联选项
        ArgumentNullException.ThrowIfNull(值)
        Return New 文件关联选项 With {
            .关联常见视频 = 值.关联常见视频,
            .关联不常见视频 = 值.关联不常见视频,
            .关联老旧视频 = 值.关联老旧视频,
            .关联常见音频 = 值.关联常见音频,
            .关联不常见音频 = 值.关联不常见音频,
            .关联老旧音频 = 值.关联老旧音频,
            .关联常见图片 = 值.关联常见图片,
            .关联不常见图片 = 值.关联不常见图片,
            .关联老旧图片 = 值.关联老旧图片
        }
    End Function

    Friend Function 已启用(类别 As 文件关联类别) As Boolean
        Select Case 类别
            Case 文件关联类别.常见视频 : Return 关联常见视频
            Case 文件关联类别.不常见视频 : Return 关联不常见视频
            Case 文件关联类别.老旧视频 : Return 关联老旧视频
            Case 文件关联类别.常见音频 : Return 关联常见音频
            Case 文件关联类别.不常见音频 : Return 关联不常见音频
            Case 文件关联类别.老旧音频 : Return 关联老旧音频
            Case 文件关联类别.常见图片 : Return 关联常见图片
            Case 文件关联类别.不常见图片 : Return 关联不常见图片
            Case 文件关联类别.老旧图片 : Return 关联老旧图片
            Case Else : Throw New ArgumentOutOfRangeException(NameOf(类别))
        End Select
    End Function
End Class

Friend NotInheritable Class 文件关联注册表位置
    Friend Sub New(classesRoot As String, applicationRoot As String, registeredApplications As String)
        Me.ClassesRoot = classesRoot
        Me.ApplicationRoot = applicationRoot
        Me.RegisteredApplications = registeredApplications
    End Sub

    Friend ReadOnly Property ClassesRoot As String
    Friend ReadOnly Property ApplicationRoot As String
    Friend ReadOnly Property RegisteredApplications As String
    Friend ReadOnly Property CapabilitiesRoot As String
        Get
            Return ApplicationRoot & "\Capabilities"
        End Get
    End Property
End Class

Friend NotInheritable Class 文件关联管理器
    Private Const 应用注册名称 As String = "FFF.Player"
    Private Const 应用程序注册键名 As String = "FFF.Player.exe"
    Private Const 缩略图接口 As String = "{e357fccd-a995-4576-b01f-234630154e96}"
    Private Const 系统属性缩略图处理器 As String = "{9DBD2C50-62AD-11D0-B806-00C04FD706EC}"
    Private Const SHCNE_ASSOCCHANGED As UInteger = &H8000000UI
    Private Const SHCNF_IDLIST As UInteger = 0UI

    Private Shared ReadOnly 默认注册表位置 As New 文件关联注册表位置(
        "Software\Classes", "Software\1059 Studio\FFF.Player", "Software\RegisteredApplications")
    Private Shared ReadOnly 同步门 As New SemaphoreSlim(1, 1)
    Private Shared ReadOnly 文件类型 As IReadOnlyList(Of 文件类型定义) = 创建文件类型()
    Private Shared 启动同步任务 As Task = Task.CompletedTask

    Private Sub New()
    End Sub

    Friend Shared Function 取得扩展名(类别 As 文件关联类别) As IReadOnlyList(Of String)
        Return 文件类型.Where(Function(x) x.类别 = 类别).Select(Function(x) x.扩展名).ToArray()
    End Function

    Friend Shared Sub 启动后台同步(选项 As 文件关联选项)
        ArgumentNullException.ThrowIfNull(选项)
        ' 启动时全部未勾选表示用户没有委托播放器管理关联；不要清理
        ' 其他播放器留下的关联。运行期间的取消操作仍通过同步全部Async
        ' 显式执行注销和备份恢复。
        If Not 有已启用的类别(选项) Then
            启动同步任务 = Task.CompletedTask
            Return
        End If
        启动同步任务 = 启动后台同步核心Async(选项)
    End Sub

    '''判据取自类别枚举，不取自逐个选项属性：后者新增类别时容易漏改，
    '''漏改的后果是该类别的勾选只在运行期间有效、重启后静默失效。
    Friend Shared Function 有已启用的类别(选项 As 文件关联选项) As Boolean
        ArgumentNullException.ThrowIfNull(选项)
        Return [Enum].GetValues(Of 文件关联类别)().Any(Function(类别) 选项.已启用(类别))
    End Function

    Private Shared Async Function 启动后台同步核心Async(选项 As 文件关联选项) As Task
        Try
            Await 同步门.WaitAsync().ConfigureAwait(False)
            Try
                ' 启动时只接管当前勾选的类别；未勾选的类别保留系统和其他
                ' 播放器现状，只有运行期间的显式取消才执行注销恢复。
                Await Task.Run(Sub() 同步核心(选项, 程序路径, 默认注册表位置, True, True)).ConfigureAwait(False)
            Finally
                同步门.Release()
            End Try
        Catch ex As Exception
            Debug.WriteLine($"启动时同步文件关联失败：{ex}")
        End Try
    End Function

    Friend Shared Async Function 同步全部Async(选项 As 文件关联选项) As Task
        ArgumentNullException.ThrowIfNull(选项)
        Await 同步门.WaitAsync().ConfigureAwait(False)
        Try
            Await Task.Run(Sub() 同步核心(选项, 程序路径, 默认注册表位置, True)).ConfigureAwait(False)
        Finally
            同步门.Release()
        End Try
    End Function

    Friend Shared Sub 同步用于测试(选项 As 文件关联选项, executablePath As String,
                              注册表位置 As 文件关联注册表位置)
        同步核心(选项, executablePath, 注册表位置, False)
    End Sub

    Private Shared Sub 同步核心(选项 As 文件关联选项, executablePath As String,
                           注册表位置 As 文件关联注册表位置, 通知资源管理器 As Boolean,
                           Optional 仅同步已启用 As Boolean = False)
        ArgumentNullException.ThrowIfNull(选项)
        ArgumentNullException.ThrowIfNull(注册表位置)
        ArgumentException.ThrowIfNullOrWhiteSpace(executablePath)
        Dim 完整程序路径 = Path.GetFullPath(executablePath)
        If Not File.Exists(完整程序路径) Then Throw New FileNotFoundException("找不到用于文件关联的程序。", 完整程序路径)

        Dim 有变更 As Boolean
        For Each 定义 In 文件类型
            If 选项.已启用(定义.类别) Then
                有变更 = 注册文件类型(定义, 完整程序路径, 注册表位置) OrElse 有变更
            ElseIf Not 仅同步已启用 Then
                有变更 = 注销文件类型(定义, 注册表位置) OrElse 有变更
            End If
        Next

        If 文件类型.Any(Function(x) 选项.已启用(x.类别)) Then
            有变更 = 注册应用(选项, 完整程序路径, 注册表位置, 仅同步已启用) OrElse 有变更
        ElseIf Not 仅同步已启用 Then
            有变更 = 注销应用(注册表位置) OrElse 有变更
        End If

        If 有变更 AndAlso 通知资源管理器 Then
            SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, IntPtr.Zero, IntPtr.Zero)
        End If
    End Sub

    Private Shared Function 注册文件类型(定义 As 文件类型定义, executablePath As String,
                                    注册表位置 As 文件关联注册表位置) As Boolean
        Dim 有变更 As Boolean
        Dim progId = 定义.ProgId
        Dim extensionPath = 注册表位置.ClassesRoot & "\" & 定义.扩展名
        Using extensionKey = Registry.CurrentUser.CreateSubKey(extensionPath, True)
            If Not 注册表值等于(extensionKey, String.Empty, progId) Then
                保存注册表值(定义, "Default", extensionKey, String.Empty, 注册表位置)
                有变更 = 设置字符串(extensionKey, String.Empty, progId) OrElse 有变更
            End If
            If Not 注册表值等于(extensionKey, "Content Type", 定义.内容类型) Then
                保存注册表值(定义, "ContentType", extensionKey, "Content Type", 注册表位置)
                有变更 = 设置字符串(extensionKey, "Content Type", 定义.内容类型) OrElse 有变更
            End If
            If Not 注册表值等于(extensionKey, "PerceivedType", 定义.感知类型) Then
                保存注册表值(定义, "PerceivedType", extensionKey, "PerceivedType", 注册表位置)
                有变更 = 设置字符串(extensionKey, "PerceivedType", 定义.感知类型) OrElse 有变更
            End If
            Using openWith = extensionKey.CreateSubKey("OpenWithProgids", True)
                If Not 存在注册表值(openWith, progId) Then
                    openWith.SetValue(progId, Array.Empty(Of Byte)(), RegistryValueKind.None)
                    有变更 = True
                End If
            End Using
            Using thumbnailKey = extensionKey.OpenSubKey("ShellEx\" & 缩略图接口, True)
                If thumbnailKey IsNot Nothing Then
                    有变更 = 恢复注册表值(定义, "ThumbnailProvider", thumbnailKey, String.Empty,
                                          系统属性缩略图处理器, 注册表位置) OrElse 有变更
                End If
            End Using
        End Using

        Dim progIdPath = 注册表位置.ClassesRoot & "\" & progId
        Using progIdKey = Registry.CurrentUser.CreateSubKey(progIdPath, True)
            有变更 = 设置字符串(progIdKey, String.Empty, 定义.友好类型名称) OrElse 有变更
            有变更 = 删除子键树(progIdKey, "DefaultIcon") OrElse 有变更
            Using commandKey = progIdKey.CreateSubKey("shell\open\command", True)
                有变更 = 设置字符串(commandKey, String.Empty, $"""{executablePath}"" ""%1""") OrElse 有变更
            End Using
            Using applicationKey = progIdKey.CreateSubKey("Application", True)
                有变更 = 设置字符串(applicationKey, "ApplicationName", 应用注册名称) OrElse 有变更
            End Using
        End Using
        Return 有变更
    End Function

    Private Shared Function 注销文件类型(定义 As 文件类型定义,
                                    注册表位置 As 文件关联注册表位置) As Boolean
        Dim 有变更 As Boolean
        Dim 键待删 As Boolean
        Dim extensionPath = 注册表位置.ClassesRoot & "\" & 定义.扩展名
        Using extensionKey = Registry.CurrentUser.OpenSubKey(extensionPath, True)
            If extensionKey IsNot Nothing Then
                有变更 = 恢复注册表值(定义, "Default", extensionKey, String.Empty,
                              定义.ProgId, 注册表位置) OrElse 有变更
                有变更 = 恢复注册表值(定义, "ContentType", extensionKey, "Content Type",
                              定义.内容类型, 注册表位置) OrElse 有变更
                有变更 = 恢复注册表值(定义, "PerceivedType", extensionKey, "PerceivedType",
                              定义.感知类型, 注册表位置) OrElse 有变更
                Using openWith = extensionKey.OpenSubKey("OpenWithProgids", True)
                    If openWith IsNot Nothing AndAlso 存在注册表值(openWith, 定义.ProgId) Then
                        openWith.DeleteValue(定义.ProgId, False)
                        有变更 = True
                    End If
                End Using
                Using thumbnailKey = extensionKey.OpenSubKey("ShellEx\" & 缩略图接口, True)
                    If thumbnailKey IsNot Nothing Then
                        有变更 = 恢复注册表值(定义, "ThumbnailProvider", thumbnailKey, String.Empty,
                                      系统属性缩略图处理器, 注册表位置) OrElse 有变更
                    End If
                End Using
                ' 注册时 CreateSubKey 建过 OpenWithProgids；值收干净后把空子键也删掉，
                ' 否则扩展名键会因为剩一个空子键而删不干净。
                For Each 子键名 In extensionKey.GetSubKeyNames().ToArray()
                    If Not String.Equals(子键名, "OpenWithProgids", StringComparison.OrdinalIgnoreCase) Then Continue For
                    Using 子键 = extensionKey.OpenSubKey(子键名, True)
                        If 子键 IsNot Nothing AndAlso 子键.GetValueNames().Length = 0 AndAlso
                           子键.GetSubKeyNames().Length = 0 Then
                            extensionKey.DeleteSubKey(子键名, False)
                            有变更 = True
                        End If
                    End Using
                Next
                键待删 = 键已空(extensionKey)
            End If
        End Using
        ' 我们可能为本不存在的扩展名建过键（图片档里的 .apng 就是这样）。值都还原后若键已空，
        ' 连键一起删：留在 HKCU 的空键会把机器级（HKLM）的同名注册遮掉。
        If 键待删 AndAlso 注册表键存在(extensionPath) Then
            Registry.CurrentUser.DeleteSubKeyTree(extensionPath, False)
            有变更 = True
        End If

        Dim progIdPath = 注册表位置.ClassesRoot & "\" & 定义.ProgId
        If 注册表键存在(progIdPath) Then
            Registry.CurrentUser.DeleteSubKeyTree(progIdPath, False)
            有变更 = True
        End If
        Using capabilities = Registry.CurrentUser.OpenSubKey(注册表位置.CapabilitiesRoot & "\FileAssociations", True)
            If capabilities IsNot Nothing AndAlso 存在注册表值(capabilities, 定义.扩展名) Then
                capabilities.DeleteValue(定义.扩展名, False)
                有变更 = True
            End If
        End Using
        删除备份(定义, 注册表位置)
        Return 有变更
    End Function

    Private Shared Function 注册应用(选项 As 文件关联选项, executablePath As String,
                                注册表位置 As 文件关联注册表位置,
                                Optional 保留未启用 As Boolean = False) As Boolean
        Dim 有变更 As Boolean
        Dim applicationPath = 注册表位置.ClassesRoot & "\Applications\" & 应用程序注册键名
        Using application = Registry.CurrentUser.CreateSubKey(applicationPath, True)
            有变更 = 设置字符串(application, "FriendlyAppName", 应用注册名称) OrElse 有变更
            有变更 = 删除子键树(application, "DefaultIcon") OrElse 有变更
            Using commandKey = application.CreateSubKey("shell\open\command", True)
                有变更 = 设置字符串(commandKey, String.Empty, $"""{executablePath}"" ""%1""") OrElse 有变更
            End Using
            Using supportedTypes = application.CreateSubKey("SupportedTypes", True)
                For Each 定义 In 文件类型.Where(Function(x) 选项.已启用(x.类别))
                    If Not 存在注册表值(supportedTypes, 定义.扩展名) Then
                        supportedTypes.SetValue(定义.扩展名, String.Empty, RegistryValueKind.String)
                        有变更 = True
                    End If
                Next
                If Not 保留未启用 Then
                    For Each extension In supportedTypes.GetValueNames().Except(
                            文件类型.Where(Function(x) 选项.已启用(x.类别)).Select(Function(x) x.扩展名),
                            StringComparer.OrdinalIgnoreCase).ToArray()
                        supportedTypes.DeleteValue(extension, False)
                        有变更 = True
                    Next
                End If
            End Using
        End Using
        Using capabilities = Registry.CurrentUser.CreateSubKey(注册表位置.CapabilitiesRoot, True)
            有变更 = 设置字符串(capabilities, "ApplicationName", 应用注册名称) OrElse 有变更
            有变更 = 设置字符串(capabilities, "ApplicationDescription", "3FP 本地媒体播放器") OrElse 有变更
            If 存在注册表值(capabilities, "ApplicationIcon") Then
                capabilities.DeleteValue("ApplicationIcon", False)
                有变更 = True
            End If
            Using associations = capabilities.CreateSubKey("FileAssociations", True)
                For Each 定义 In 文件类型.Where(Function(x) 选项.已启用(x.类别))
                    有变更 = 设置字符串(associations, 定义.扩展名, 定义.ProgId) OrElse 有变更
                Next
            End Using
        End Using
        Using registered = Registry.CurrentUser.CreateSubKey(注册表位置.RegisteredApplications, True)
            有变更 = 设置字符串(registered, 应用注册名称, 注册表位置.CapabilitiesRoot) OrElse 有变更
        End Using
        Return 有变更
    End Function

    Private Shared Function 注销应用(注册表位置 As 文件关联注册表位置) As Boolean
        Dim 有变更 As Boolean
        Dim applicationPath = 注册表位置.ClassesRoot & "\Applications\" & 应用程序注册键名
        If 注册表键存在(applicationPath) Then
            Registry.CurrentUser.DeleteSubKeyTree(applicationPath, False)
            有变更 = True
        End If
        If 注册表键存在(注册表位置.CapabilitiesRoot) Then
            Registry.CurrentUser.DeleteSubKeyTree(注册表位置.CapabilitiesRoot, False)
            有变更 = True
        End If
        Using registered = Registry.CurrentUser.OpenSubKey(注册表位置.RegisteredApplications, True)
            If registered IsNot Nothing AndAlso 存在注册表值(registered, 应用注册名称) Then
                registered.DeleteValue(应用注册名称, False)
                有变更 = True
            End If
        End Using
        Return 有变更
    End Function

    Private Shared Sub 保存注册表值(定义 As 文件类型定义, label As String, sourceKey As RegistryKey,
                              valueName As String, 注册表位置 As 文件关联注册表位置)
        Using backup = Registry.CurrentUser.CreateSubKey(取得备份路径(定义, 注册表位置), True)
            Dim exists = 存在注册表值(sourceKey, valueName)
            backup.SetValue(label & ".Captured", 1, RegistryValueKind.DWord)
            backup.SetValue(label & ".Exists", If(exists, 1, 0), RegistryValueKind.DWord)
            If exists Then
                Dim kind = sourceKey.GetValueKind(valueName)
                Dim value = sourceKey.GetValue(valueName, Nothing, RegistryValueOptions.DoNotExpandEnvironmentNames)
                backup.SetValue(label & ".Kind", CInt(kind), RegistryValueKind.DWord)
                backup.SetValue(label & ".Value", value, kind)
            Else
                backup.DeleteValue(label & ".Kind", False)
                backup.DeleteValue(label & ".Value", False)
            End If
        End Using
    End Sub

    Private Shared Function 恢复注册表值(定义 As 文件类型定义, label As String, targetKey As RegistryKey,
                                    valueName As String, expectedValue As String,
                                    注册表位置 As 文件关联注册表位置) As Boolean
        Using backup = Registry.CurrentUser.OpenSubKey(取得备份路径(定义, 注册表位置))
            If backup Is Nothing OrElse CInt(backup.GetValue(label & ".Captured", 0)) <> 1 OrElse
               Not 注册表值等于(targetKey, valueName, expectedValue) Then Return False

            If CInt(backup.GetValue(label & ".Exists", 0)) = 1 Then
                Dim value = backup.GetValue(label & ".Value", Nothing, RegistryValueOptions.DoNotExpandEnvironmentNames)
                Dim kind = CType(CInt(backup.GetValue(label & ".Kind", CInt(RegistryValueKind.String))), RegistryValueKind)
                targetKey.SetValue(valueName, value, kind)
            Else
                targetKey.DeleteValue(valueName, False)
            End If
            Return True
        End Using
    End Function

    Private Shared Sub 删除备份(定义 As 文件类型定义, 注册表位置 As 文件关联注册表位置)
        Dim path = 取得备份路径(定义, 注册表位置)
        If 注册表键存在(path) Then Registry.CurrentUser.DeleteSubKeyTree(path, False)
    End Sub

    Private Shared Function 取得备份路径(定义 As 文件类型定义,
                                   注册表位置 As 文件关联注册表位置) As String
        Return 注册表位置.ApplicationRoot & "\FileAssociationBackups\" & 定义.扩展名.TrimStart("."c)
    End Function

    Private Shared Function 设置字符串(key As RegistryKey, valueName As String, value As String) As Boolean
        If 注册表值等于(key, valueName, value) Then Return False
        key.SetValue(valueName, value, RegistryValueKind.String)
        Return True
    End Function

    Private Shared Function 删除子键树(key As RegistryKey, subKeyName As String) As Boolean
        Using existing = key.OpenSubKey(subKeyName)
            If existing Is Nothing Then Return False
        End Using
        key.DeleteSubKeyTree(subKeyName, False)
        Return True
    End Function

    Private Shared Function 注册表值等于(key As RegistryKey, valueName As String, expected As String) As Boolean
        If key Is Nothing OrElse Not 存在注册表值(key, valueName) Then Return False
        Dim actual = TryCast(key.GetValue(valueName, Nothing, RegistryValueOptions.DoNotExpandEnvironmentNames), String)
        Return String.Equals(actual, expected, StringComparison.OrdinalIgnoreCase)
    End Function

    Private Shared Function 存在注册表值(key As RegistryKey, valueName As String) As Boolean
        If key Is Nothing Then Return False
        Try
            key.GetValueKind(valueName)
            Return True
        Catch ex As IOException
            Return False
        Catch ex As ArgumentException
            Return False
        End Try
    End Function

    Private Shared Function 注册表键存在(path As String) As Boolean
        Using key = Registry.CurrentUser.OpenSubKey(path)
            Return key IsNot Nothing
        End Using
    End Function

    '''键上没有任何具名值、默认值为空、且没有带内容的子键 ⇒ 只剩一个空壳。
    Private Shared Function 键已空(key As RegistryKey) As Boolean
        If key Is Nothing Then Return False
        If key.GetValueNames().Any(Function(name) name.Length > 0) Then Return False
        Dim 默认值 = CStr(key.GetValue(String.Empty, String.Empty,
                                  RegistryValueOptions.DoNotExpandEnvironmentNames))
        If 默认值.Length > 0 Then Return False
        For Each 子键名 In key.GetSubKeyNames()
            Using 子键 = key.OpenSubKey(子键名)
                If 子键 Is Nothing Then Continue For
                If 子键.GetValueNames().Length > 0 OrElse 子键.GetSubKeyNames().Length > 0 Then Return False
            End Using
        Next
        Return True
    End Function

    ' 列表顺序即设置页分组：视频 → 音频 → 图片，每档再按 常见 / 不常见 / 老旧 分级。
    ' 图片三档全部默认不勾选（与视频/音频一致，由用户在设置页主动委托）。
    ' 图片档的入选判据是「随包内核实测能打开且呈现≥1帧」（tools/cover_probe.sh），
    ' 不是「FFmpeg 名义上有解码器」——实测不过的 .dib / .pcd 已按此判据排除。
    ' ⚠ 分组说明只能写在函数上方：注释行会截断多行数组初始化器的隐式续行，
    '   写在列表中间（上一项以逗号结尾之后）会报 BC30201/BC30035。
    Private Shared Function 创建文件类型() As IReadOnlyList(Of 文件类型定义)
        Return New 文件类型定义() {
            New 文件类型定义(文件关联类别.常见视频, ".mp4", "video/mp4", "video"),
            New 文件类型定义(文件关联类别.常见视频, ".mkv", "video/x-matroska", "video"),
            New 文件类型定义(文件关联类别.常见视频, ".mov", "video/quicktime", "video"),
            New 文件类型定义(文件关联类别.常见视频, ".avi", "video/x-msvideo", "video"),
            New 文件类型定义(文件关联类别.常见视频, ".wmv", "video/x-ms-wmv", "video"),
            New 文件类型定义(文件关联类别.常见视频, ".webm", "video/webm", "video"),
            New 文件类型定义(文件关联类别.不常见视频, ".m4v", "video/x-m4v", "video"),
            New 文件类型定义(文件关联类别.不常见视频, ".flv", "video/x-flv", "video"),
            New 文件类型定义(文件关联类别.不常见视频, ".ts", "video/mp2t", "video"),
            New 文件类型定义(文件关联类别.不常见视频, ".m2ts", "video/mp2t", "video"),
            New 文件类型定义(文件关联类别.不常见视频, ".mts", "video/mp2t", "video"),
            New 文件类型定义(文件关联类别.不常见视频, ".ogv", "video/ogg", "video"),
            New 文件类型定义(文件关联类别.老旧视频, ".mpg", "video/mpeg", "video"),
            New 文件类型定义(文件关联类别.老旧视频, ".mpeg", "video/mpeg", "video"),
            New 文件类型定义(文件关联类别.老旧视频, ".3gp", "video/3gpp", "video"),
            New 文件类型定义(文件关联类别.老旧视频, ".3g2", "video/3gpp2", "video"),
            New 文件类型定义(文件关联类别.老旧视频, ".rm", "video/vnd.rn-realvideo", "video"),
            New 文件类型定义(文件关联类别.老旧视频, ".rmvb", "application/vnd.rn-realmedia-vbr", "video"),
            New 文件类型定义(文件关联类别.老旧视频, ".asf", "video/x-ms-asf", "video"),
            New 文件类型定义(文件关联类别.老旧视频, ".divx", "video/divx", "video"),
            New 文件类型定义(文件关联类别.常见音频, ".mp3", "audio/mpeg", "audio"),
            New 文件类型定义(文件关联类别.常见音频, ".flac", "audio/flac", "audio"),
            New 文件类型定义(文件关联类别.常见音频, ".wav", "audio/wav", "audio"),
            New 文件类型定义(文件关联类别.常见音频, ".m4a", "audio/mp4", "audio"),
            New 文件类型定义(文件关联类别.常见音频, ".aac", "audio/aac", "audio"),
            New 文件类型定义(文件关联类别.常见音频, ".ogg", "audio/ogg", "audio"),
            New 文件类型定义(文件关联类别.常见音频, ".opus", "audio/opus", "audio"),
            New 文件类型定义(文件关联类别.不常见音频, ".ape", "audio/x-ape", "audio"),
            New 文件类型定义(文件关联类别.不常见音频, ".mka", "audio/x-matroska", "audio"),
            New 文件类型定义(文件关联类别.不常见音频, ".ac3", "audio/ac3", "audio"),
            New 文件类型定义(文件关联类别.不常见音频, ".eac3", "audio/eac3", "audio"),
            New 文件类型定义(文件关联类别.不常见音频, ".dts", "audio/vnd.dts", "audio"),
            New 文件类型定义(文件关联类别.不常见音频, ".wv", "audio/wavpack", "audio"),
            New 文件类型定义(文件关联类别.不常见音频, ".tak", "audio/x-tak", "audio"),
            New 文件类型定义(文件关联类别.老旧音频, ".wma", "audio/x-ms-wma", "audio"),
            New 文件类型定义(文件关联类别.老旧音频, ".aif", "audio/aiff", "audio"),
            New 文件类型定义(文件关联类别.老旧音频, ".aiff", "audio/aiff", "audio"),
            New 文件类型定义(文件关联类别.老旧音频, ".amr", "audio/amr", "audio"),
            New 文件类型定义(文件关联类别.老旧音频, ".au", "audio/basic", "audio"),
            New 文件类型定义(文件关联类别.老旧音频, ".ra", "audio/vnd.rn-realaudio", "audio"),
            New 文件类型定义(文件关联类别.老旧音频, ".tta", "audio/x-tta", "audio"),
            New 文件类型定义(文件关联类别.老旧音频, ".mpc", "audio/x-musepack", "audio"),
            New 文件类型定义(文件关联类别.常见图片, ".png", "image/png", "image"),
            New 文件类型定义(文件关联类别.常见图片, ".jpg", "image/jpeg", "image"),
            New 文件类型定义(文件关联类别.常见图片, ".jpeg", "image/jpeg", "image"),
            New 文件类型定义(文件关联类别.常见图片, ".jpe", "image/jpeg", "image"),
            New 文件类型定义(文件关联类别.常见图片, ".jfif", "image/jpeg", "image"),
            New 文件类型定义(文件关联类别.常见图片, ".apng", "image/apng", "image"),
            New 文件类型定义(文件关联类别.常见图片, ".bmp", "image/bmp", "image"),
            New 文件类型定义(文件关联类别.常见图片, ".gif", "image/gif", "image"),
            New 文件类型定义(文件关联类别.常见图片, ".webp", "image/webp", "image"),
            New 文件类型定义(文件关联类别.常见图片, ".avif", "image/avif", "image"),
            New 文件类型定义(文件关联类别.常见图片, ".ico", "image/x-icon", "image"),
            New 文件类型定义(文件关联类别.常见图片, ".tiff", "image/tiff", "image"),
            New 文件类型定义(文件关联类别.常见图片, ".tif", "image/tiff", "image"),
            New 文件类型定义(文件关联类别.常见图片, ".heic", "image/heic", "image"),
            New 文件类型定义(文件关联类别.不常见图片, ".avifs", "image/avif", "image"),
            New 文件类型定义(文件关联类别.不常见图片, ".heif", "image/heif", "image"),
            New 文件类型定义(文件关联类别.不常见图片, ".jp2", "image/jp2", "image"),
            New 文件类型定义(文件关联类别.不常见图片, ".j2k", "image/jpx", "image"),
            New 文件类型定义(文件关联类别.不常见图片, ".j2c", "image/jpx", "image"),
            New 文件类型定义(文件关联类别.不常见图片, ".jxl", "image/jxl", "image"),
            New 文件类型定义(文件关联类别.不常见图片, ".cur", "image/x-icon", "image"),
            New 文件类型定义(文件关联类别.不常见图片, ".exr", "image/x-exr", "image"),
            New 文件类型定义(文件关联类别.不常见图片, ".hdr", "image/vnd.radiance", "image"),
            New 文件类型定义(文件关联类别.不常见图片, ".tga", "image/x-tga", "image"),
            New 文件类型定义(文件关联类别.不常见图片, ".pcx", "image/x-pcx", "image"),
            New 文件类型定义(文件关联类别.不常见图片, ".dds", "image/vnd.ms-dds", "image"),
            New 文件类型定义(文件关联类别.不常见图片, ".qoi", "image/x-qoi", "image"),
            New 文件类型定义(文件关联类别.老旧图片, ".pnm", "image/x-portable-anymap", "image"),
            New 文件类型定义(文件关联类别.老旧图片, ".pbm", "image/x-portable-bitmap", "image"),
            New 文件类型定义(文件关联类别.老旧图片, ".pgm", "image/x-portable-graymap", "image"),
            New 文件类型定义(文件关联类别.老旧图片, ".ppm", "image/x-portable-pixmap", "image"),
            New 文件类型定义(文件关联类别.老旧图片, ".pam", "image/x-portable-anymap", "image"),
            New 文件类型定义(文件关联类别.老旧图片, ".xbm", "image/x-xbitmap", "image"),
            New 文件类型定义(文件关联类别.老旧图片, ".xpm", "image/x-xpixmap", "image"),
            New 文件类型定义(文件关联类别.老旧图片, ".xwd", "image/x-xwindowdump", "image"),
            New 文件类型定义(文件关联类别.老旧图片, ".dpx", "image/x-dpx", "image"),
            New 文件类型定义(文件关联类别.老旧图片, ".sun", "image/x-sun-raster", "image"),
            New 文件类型定义(文件关联类别.老旧图片, ".ras", "image/x-sun-raster", "image"),
            New 文件类型定义(文件关联类别.老旧图片, ".sgi", "image/x-sgi-rgb", "image")
        }
    End Function

    <DllImport("shell32.dll")>
    Private Shared Sub SHChangeNotify(eventId As UInteger, flags As UInteger, item1 As IntPtr, item2 As IntPtr)
    End Sub

    Private NotInheritable Class 文件类型定义
        Friend Sub New(类别 As 文件关联类别, 扩展名 As String, 内容类型 As String, 感知类型 As String)
            Me.类别 = 类别
            Me.扩展名 = 扩展名
            Me.内容类型 = 内容类型
            Me.感知类型 = 感知类型
        End Sub

        Friend ReadOnly Property 类别 As 文件关联类别
        Friend ReadOnly Property 扩展名 As String
        Friend ReadOnly Property 内容类型 As String
        Friend ReadOnly Property 感知类型 As String
        Friend ReadOnly Property ProgId As String
            Get
                Return 应用注册名称 & 扩展名
            End Get
        End Property
        Friend ReadOnly Property 友好类型名称 As String
            Get
                Dim 是图片 = 类别 = 文件关联类别.常见图片 OrElse
                               类别 = 文件关联类别.不常见图片 OrElse
                               类别 = 文件关联类别.老旧图片
                Dim 后缀 = If(是图片, "图片文件", "媒体文件")
                Return 扩展名.TrimStart("."c).ToUpperInvariant() & " " & 后缀
            End Get
        End Property
    End Class
End Class
