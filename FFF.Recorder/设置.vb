Imports System.IO
Imports System.Text.Json
Imports System.Reflection
Imports System.Runtime.InteropServices

Public Class 设置
    Public Shared Property 实例对象 As New 设置

    Public Property 输出目录 As String = Application.StartupPath
    Public Property 自动命名方式 As Integer = 0
    Public Property 视频源键 As String = String.Empty
    Public Property 视频源类型 As Integer = 0
    Public Property 视频捕获模式 As Integer = -1
    Public Property 音频源键 As String = String.Empty
    Public Property 音频跟随默认设备 As Boolean = True
    Public Property 视频编码器索引 As Integer = 8
    Public Property 视频编码器名称 As String = "libx264"
    Public Property 视频预设 As String = "medium"
    Public Property 视频配置文件 As String = String.Empty
    Public Property 视频场景优化 As String = String.Empty
    Public Property 视频分辨率索引 As Integer = 0
    Public Property 自定义视频宽度 As String = String.Empty
    Public Property 自定义视频高度 As String = String.Empty
    Public Property 捕获鼠标 As Boolean = True
    Public Property 帧率模式 As Integer = 0
    Public Property 帧率 As Integer = 30
    Public Property 帧率分母 As Integer = 1
    Public Property 像素采样 As Integer = 0
    Public Property 灰阶位深 As Integer = 1
    Public Property 色彩模式 As Integer = 0
    Public Property SDR亮度 As Integer = 300
    Public Property HDR峰值 As Integer = 1000
    Public Property 质量控制模式 As Integer = 0
    Public Property 质量值 As Integer = 23
    Public Property 自定义视频参数 As String = String.Empty
    Public Property 音频编码器索引 As Integer = 0
    Public Property 音频采样率 As Integer = 48000
    Public Property 音频声道 As Integer = 1
    Public Property 音频每声道码率 As Integer = 160
    Public Property 防误触模式 As Boolean
    Public Property 快捷键开始 As String = String.Empty
    Public Property 快捷键暂停 As String = String.Empty
    Public Property 快捷键继续 As String = String.Empty
    Public Property 快捷键停止 As String = String.Empty
    Public Property 快捷键切分 As String = String.Empty

    Public Property 图形DX_文字渲染模式 As Integer = 0
    Public Property 图形DX_缓存预算级别 As Integer = 2
    Public Property 图形DX_动画帧率 As Integer = 60
    Public Property 图形DX_HDR As Integer = 0
    Public Property 字体 As String = SystemFonts.DefaultFont.FontFamily.Name
    Public Property SP_窗口标题文字 As String = String.Empty
    Public Property SP_窗口边框颜色_A As Integer = 255
    Public Property SP_窗口边框颜色_R As Integer = Color.Gray.R
    Public Property SP_窗口边框颜色_G As Integer = Color.Gray.G
    Public Property SP_窗口边框颜色_B As Integer = Color.Gray.B
    Public Property SP_分层阴影颜色_A As Integer = 255
    Public Property SP_分层阴影颜色_R As Integer = Color.Black.R
    Public Property SP_分层阴影颜色_G As Integer = Color.Black.G
    Public Property SP_分层阴影颜色_B As Integer = Color.Black.B
    Public Property SP_边框宽度 As Integer = 1
    Public Property SP_毛玻璃模式 As Integer = 0
    Public Property SP_毛玻璃背景来源 As Integer = -1
    Public Property SP_毛玻璃噪点颗粒 As Integer = -1

    Private Shared 当前自有背景图 As Image
    Private Shared 默认背景图 As Image
    Private Shared 当前自有图标 As Icon

    ' 发布时程序目录会被整体替换，设置必须放在用户数据目录中才能跨版本保留。
    Private Shared ReadOnly 用户本地数据目录 As String = Environment.GetFolderPath(Environment.SpecialFolder.LocalApplicationData)
    Private Shared ReadOnly 设置目录路径 As String = If(String.IsNullOrWhiteSpace(用户本地数据目录),
        Application.StartupPath, Path.Combine(用户本地数据目录, "FFF.Recorder"))
    Private Shared ReadOnly 设置文件路径 As String = Path.Combine(设置目录路径, "Settings.json")
    Private Shared ReadOnly 旧设置文件路径 As String = Path.Combine(Application.StartupPath, "FFF.Recorder.Settings.json")
    Public Shared ReadOnly 自定义图标路径 As String = Path.Combine(设置目录路径, "SP_Icon")
    Public Shared ReadOnly 自定义背景图路径 As String = Path.Combine(设置目录路径, "SP_BackImage")
    Private Shared ReadOnly 旧自定义图标路径 As String = Path.Combine(Application.StartupPath, "SP_Icon")
    Private Shared ReadOnly 旧自定义背景图路径 As String = Path.Combine(Application.StartupPath, "SP_BackImage")
    Public Shared Sub 退出时保存设置()
        Dim 临时路径 As String = Nothing
        Try
            实例对象.规范化()
            Dim 目录 = Path.GetDirectoryName(设置文件路径)
            If Not String.IsNullOrWhiteSpace(目录) Then Directory.CreateDirectory(目录)
            临时路径 = 设置文件路径 & ".tmp-" & Guid.NewGuid().ToString("N")
            File.WriteAllText(临时路径, JsonSerializer.Serialize(实例对象, JsonSO), System.Text.Encoding.UTF8)
            File.Move(临时路径, 设置文件路径, True)
        Catch ex As Exception
            If Not String.IsNullOrWhiteSpace(临时路径) Then
                Try
                    If File.Exists(临时路径) Then File.Delete(临时路径)
                Catch
                End Try
            End If
            MsgBox($"保存设置失败：{ex.Message}", MsgBoxStyle.Critical)
        End Try
    End Sub
    Public Shared Sub 启动时加载设置()
        Try
            迁移旧个性化文件()
            Dim 读取路径 = If(File.Exists(设置文件路径), 设置文件路径,
                If(File.Exists(旧设置文件路径), 旧设置文件路径, Nothing))
            If String.IsNullOrWhiteSpace(读取路径) Then
                If FontFamily.Families.Any(Function(f) f.Name = "微软雅黑") Then 实例对象.字体 = "微软雅黑"
                退出时保存设置()
                Return
            End If
            Dim 读取 = JsonSerializer.Deserialize(Of 设置)(File.ReadAllText(读取路径, System.Text.Encoding.UTF8))
            If 读取 Is Nothing Then Throw New JsonException("设置对象为空。")
            实例对象 = 读取
            实例对象.规范化()
            If String.Equals(读取路径, 旧设置文件路径, StringComparison.OrdinalIgnoreCase) AndAlso
                Not String.Equals(设置文件路径, 旧设置文件路径, StringComparison.OrdinalIgnoreCase) Then
                退出时保存设置()
            End If
        Catch
            Try
                Dim 损坏源 = If(File.Exists(设置文件路径), 设置文件路径, 旧设置文件路径)
                If File.Exists(损坏源) Then File.Copy(损坏源, 损坏源 & ".broken-" & DateTime.Now.ToString("yyyyMMddHHmmss"), True)
            Catch
            End Try
            实例对象 = New 设置()
            退出时保存设置()
        End Try
    End Sub

    Private Shared Sub 迁移旧个性化文件()
        If String.Equals(设置目录路径, Application.StartupPath, StringComparison.OrdinalIgnoreCase) Then Return
        Try
            Directory.CreateDirectory(设置目录路径)
            If Not File.Exists(自定义图标路径) AndAlso File.Exists(旧自定义图标路径) Then
                File.Copy(旧自定义图标路径, 自定义图标路径)
            End If
            If Not File.Exists(自定义背景图路径) AndAlso File.Exists(旧自定义背景图路径) Then
                File.Copy(旧自定义背景图路径, 自定义背景图路径)
            End If
        Catch
            ' 个性化资源迁移失败不能阻止主设置加载。
        End Try
    End Sub

    Public Sub 规范化()
        输出目录 = If(String.IsNullOrWhiteSpace(输出目录), Application.StartupPath, 输出目录)
        If Not Directory.Exists(输出目录) Then 输出目录 = Application.StartupPath
        自动命名方式 = Math.Clamp(自动命名方式, 0, 1)
        If 视频捕获模式 < 0 Then 视频捕获模式 = If(视频源类型 = 1, 1, 0)
        视频捕获模式 = Math.Clamp(视频捕获模式, 0, 2)
        Dim 视频编码器名称列表 = {
            "libsvtav1", "av1_nvenc", "av1_qsv", "av1_amf",
            "libx265", "hevc_nvenc", "hevc_qsv", "hevc_amf",
            "libx264", "h264_nvenc", "h264_qsv", "h264_amf"}
        视频编码器索引 = Array.FindIndex(视频编码器名称列表,
            Function(名称) String.Equals(名称, 视频编码器名称, StringComparison.OrdinalIgnoreCase))
        If 视频编码器索引 < 0 Then 视频编码器索引 = 8
        视频编码器名称 = 视频编码器名称列表(视频编码器索引)
        视频预设 = 规范化视频预设(视频编码器名称, 视频预设)
        视频分辨率索引 = Math.Clamp(视频分辨率索引, 0, 5)
        自定义视频宽度 = If(自定义视频宽度, String.Empty).Trim()
        自定义视频高度 = If(自定义视频高度, String.Empty).Trim()
        帧率模式 = Math.Clamp(帧率模式, 0, 1)
        Dim 规范帧率分子 = If(帧率 > 0, CUInt(帧率), 0UI)
        Dim 规范帧率分母 = If(帧率分母 > 0, CUInt(帧率分母), 0UI)
        If 录制帧率规则.规范化(规范帧率分子, 规范帧率分母) Then
            帧率 = CInt(规范帧率分子)
            帧率分母 = CInt(规范帧率分母)
        Else
            帧率 = 30
            帧率分母 = 1
        End If
        像素采样 = Math.Clamp(像素采样, 0, 2)
        ' 当前 GPU 处理与编码接口只提供 8-bit 和 10-bit 路径；旧版 12/16-bit 选择迁移为 10-bit。
        灰阶位深 = If(灰阶位深 <= 0, 0, 1)
        色彩模式 = Math.Clamp(色彩模式, 0, 1)
        If Not {100, 200, 300}.Contains(SDR亮度) Then SDR亮度 = 300
        If Not {400, 600, 800, 1000, 2000}.Contains(HDR峰值) Then HDR峰值 = 1000
        质量控制模式 = Math.Clamp(质量控制模式, 0, 4)
        质量值 = Math.Clamp(质量值, 0, 63)
        If Not {44100, 48000, 96000, 192000}.Contains(音频采样率) Then 音频采样率 = 48000
        音频声道 = Math.Clamp(音频声道, 0, 7)
        音频编码器索引 = Math.Clamp(音频编码器索引, 0, 5)
        SP_窗口标题文字 = If(SP_窗口标题文字, String.Empty)
        SP_窗口边框颜色_A = Math.Clamp(SP_窗口边框颜色_A, 0, 255)
        SP_窗口边框颜色_R = Math.Clamp(SP_窗口边框颜色_R, 0, 255)
        SP_窗口边框颜色_G = Math.Clamp(SP_窗口边框颜色_G, 0, 255)
        SP_窗口边框颜色_B = Math.Clamp(SP_窗口边框颜色_B, 0, 255)
        SP_分层阴影颜色_A = Math.Clamp(SP_分层阴影颜色_A, 0, 255)
        SP_分层阴影颜色_R = Math.Clamp(SP_分层阴影颜色_R, 0, 255)
        SP_分层阴影颜色_G = Math.Clamp(SP_分层阴影颜色_G, 0, 255)
        SP_分层阴影颜色_B = Math.Clamp(SP_分层阴影颜色_B, 0, 255)
        SP_边框宽度 = Math.Clamp(SP_边框宽度, 0, 2)
        SP_毛玻璃模式 = Math.Clamp(SP_毛玻璃模式, 0, 3)
        SP_毛玻璃背景来源 = Math.Clamp(SP_毛玻璃背景来源, -1, 1)
        SP_毛玻璃噪点颗粒 = Math.Clamp(SP_毛玻璃噪点颗粒, -1, 2)
    End Sub

    Private Shared Function 规范化视频预设(编码器 As String, 当前值 As String) As String
        Dim 值 = If(当前值, String.Empty).Trim().ToLowerInvariant()
        If 值.Length = 0 Then Return String.Empty
        Select Case 编码器
            Case "libx264", "libx265"
                ' 旧版允许的三个极慢档迁移为 slow，避免历史设置继续造成长时间收尾。
                If {"placebo", "veryslow", "slower"}.Contains(值) Then Return "slow"
                If {"slow", "medium", "fast", "faster", "veryfast", "superfast", "ultrafast"}.Contains(值) Then Return 值
            Case "libsvtav1"
                Dim 数字预设 As Integer
                If Integer.TryParse(值, 数字预设) AndAlso 数字预设 >= 1 AndAlso 数字预设 <= 13 Then Return 数字预设.ToString()
            Case "av1_nvenc", "hevc_nvenc", "h264_nvenc"
                If {"p1", "p2", "p3", "p4", "p5", "p6", "p7"}.Contains(值) Then Return 值
            Case "av1_qsv", "hevc_qsv", "h264_qsv"
                If {"veryfast", "faster", "fast", "medium", "slow", "slower", "veryslow"}.Contains(值) Then Return 值
            Case "av1_amf", "hevc_amf", "h264_amf"
                If {"high_quality", "quality", "balanced", "speed"}.Contains(值) Then Return 值
        End Select
        ' 编码器切换后遗留的其他编码器预设回到该编码器的原生默认值。
        Return String.Empty
    End Function

    Public Shared Sub 应用SP个性化设置()
        If Not SP_UnLock OrElse Form1.当前主窗体 Is Nothing Then Return

        Dim 主窗体 = Form1.当前主窗体
        主窗体.Text = If(String.IsNullOrWhiteSpace(实例对象.SP_窗口标题文字), "FFF.Recorder", 实例对象.SP_窗口标题文字)

        Dim 边框颜色 = Color.FromArgb(实例对象.SP_窗口边框颜色_A,
                                  实例对象.SP_窗口边框颜色_R,
                                  实例对象.SP_窗口边框颜色_G,
                                  实例对象.SP_窗口边框颜色_B)
        主窗体.ThisIsYourWindow1.BorderColor = 边框颜色
        主窗体.ThisIsYourWindow1.BorderInactiveColor = 边框颜色
        主窗体.ThisIsYourWindow1.LayerShadowColor = Color.FromArgb(实例对象.SP_分层阴影颜色_A,
                                                               实例对象.SP_分层阴影颜色_R,
                                                               实例对象.SP_分层阴影颜色_G,
                                                               实例对象.SP_分层阴影颜色_B)
        主窗体.ThisIsYourWindow1.BorderSize = 实例对象.SP_边框宽度

        Select Case 实例对象.SP_毛玻璃模式
            Case 0
                主窗体.ThisIsYourWindow1.BackdropMode = LakeUI.ThisIsYourWindow.BackdropModeEnum.None
                主窗体.ThisIsYourWindow1.BackdropBlurPasses = 0
                主窗体.ThisIsYourWindow1.BackdropNoiseOpacity = 0
                清除SP自有背景图()
            Case 1
                主窗体.ThisIsYourWindow1.BackdropBlurPasses = 0
                应用SP背景来源()
                应用SP噪点颗粒()
            Case 2
                主窗体.ThisIsYourWindow1.BackdropBlurPasses = 1
                主窗体.ThisIsYourWindow1.BackdropBlurRadius = 10
                应用SP背景来源()
                应用SP噪点颗粒()
            Case 3
                主窗体.ThisIsYourWindow1.BackdropBlurPasses = 3
                主窗体.ThisIsYourWindow1.BackdropBlurRadius = 24
                应用SP背景来源()
                应用SP噪点颗粒()
        End Select

        主窗体.应用玻璃页面背景(实例对象.SP_毛玻璃模式 > 0)
    End Sub

    Private Shared Sub 应用SP背景来源()
        Dim 窗口 = Form1.当前主窗体.ThisIsYourWindow1
        Select Case 实例对象.SP_毛玻璃背景来源
            Case 0
                窗口.BackdropMode = LakeUI.ThisIsYourWindow.BackdropModeEnum.Image
                加载SP自定义背景图()
            Case 1
                窗口.BackdropMode = LakeUI.ThisIsYourWindow.BackdropModeEnum.Auto
                清除SP自有背景图()
            Case Else
                窗口.BackdropMode = LakeUI.ThisIsYourWindow.BackdropModeEnum.None
                清除SP自有背景图()
        End Select
    End Sub

    Private Shared Sub 应用SP噪点颗粒()
        Select Case 实例对象.SP_毛玻璃噪点颗粒
            Case 1 : Form1.当前主窗体.ThisIsYourWindow1.BackdropNoiseOpacity = 18
            Case 2 : Form1.当前主窗体.ThisIsYourWindow1.BackdropNoiseOpacity = 36
            Case Else : Form1.当前主窗体.ThisIsYourWindow1.BackdropNoiseOpacity = 0
        End Select
    End Sub

    Public Shared Sub 加载SP自定义图标()
        If Not SP_UnLock OrElse Not File.Exists(自定义图标路径) Then Return
        Dim image = 加载图片副本(自定义图标路径)
        Dim newIcon = 从图片创建图标(image)
        image.Dispose()
        Dim oldIcon = 当前自有图标
        当前自有图标 = newIcon
        Form1.当前主窗体.Icon = newIcon
        oldIcon?.Dispose()
    End Sub

    Public Shared Sub 加载SP自定义背景图()
        If Not SP_UnLock Then Return
        Dim newImage As Image
        If File.Exists(自定义背景图路径) Then
            newImage = 加载图片副本(自定义背景图路径)
        Else
            newImage = 获取默认背景图()
        End If
        Dim oldImage = 当前自有背景图
        当前自有背景图 = If(newImage Is 默认背景图, Nothing, newImage)
        Form1.当前主窗体.ThisIsYourWindow1.BackdropImage = newImage
        oldImage?.Dispose()
    End Sub

    Public Shared Sub 清除SP自有背景图()
        If Form1.当前主窗体 IsNot Nothing Then Form1.当前主窗体.ThisIsYourWindow1.BackdropImage = Nothing
        当前自有背景图?.Dispose()
        当前自有背景图 = Nothing
    End Sub

    Public Shared Sub 释放SP资源()
        清除SP自有背景图()
        默认背景图?.Dispose()
        默认背景图 = Nothing
        当前自有图标?.Dispose()
        当前自有图标 = Nothing
    End Sub

    Private Shared Function 获取默认背景图() As Image
        If 默认背景图 Is Nothing Then
            Using stream = Assembly.GetExecutingAssembly().GetManifestResourceStream("FFF.Recorder.Assets.SP_DefaultBackground.jpg")
                If stream Is Nothing Then Return Nothing
                Using source = Image.FromStream(stream, False, False)
                    默认背景图 = New Bitmap(source)
                End Using
            End Using
        End If
        Return 默认背景图
    End Function

    Private Shared Function 加载图片副本(filePath As String) As Image
        Using stream As New FileStream(filePath, FileMode.Open, FileAccess.Read, FileShare.ReadWrite)
            Using source = Image.FromStream(stream, False, False)
                Return New Bitmap(source)
            End Using
        End Using
    End Function

    Private Shared Function 从图片创建图标(image As Image) As Icon
        Using bitmap As New Bitmap(image)
            Dim handle = bitmap.GetHicon()
            Try
                Using tempIcon = Icon.FromHandle(handle)
                    Return DirectCast(tempIcon.Clone(), Icon)
                End Using
            Finally
                If handle <> IntPtr.Zero Then DestroyIcon(handle)
            End Try
        End Using
    End Function

    <DllImport("user32.dll")>
    Private Shared Function DestroyIcon(handle As IntPtr) As Boolean
    End Function
End Class
