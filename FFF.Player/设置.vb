Imports System.IO
Imports System.Runtime.InteropServices
Imports System.Text.Json

Public Class 设置
    Public Shared Property 实例对象 As New 设置()

    Public Property 字体 As String = SystemFonts.DefaultFont.FontFamily.Name
    Public Property 初始画面尺寸选项 As Integer = 2
    Public Property 自定义初始画面宽度 As Integer = 1024
    Public Property 自定义初始画面高度 As Integer = 576
    Public Property 解码方式 As 解码模式 = 解码模式.CPU
    Public Property HDR峰值亮度选项 As Integer = 0
    Public Property HDR峰值亮度 As Integer = 0
    Public Property HDR映射SDR参考亮度 As Integer = 250

    Public Property 字幕第一行字体 As String = "Microsoft YaHei UI"
    Public Property 字幕第一行字号 As Single = 48.0F
    Public Property 字幕第一行样式 As Integer = CInt(FontStyle.Regular)
    Public Property 字幕第一行颜色ARGB As UInteger = &HFFFFFFFFUI
    Public Property 字幕第二行字体 As String = "Microsoft YaHei UI"
    Public Property 字幕第二行字号 As Single = 48.0F
    Public Property 字幕第二行样式 As Integer = CInt(FontStyle.Regular)
    Public Property 字幕第二行颜色ARGB As UInteger = &HFFFFFFFFUI
    Public Property 字幕其他行字体 As String = "Microsoft YaHei UI"
    Public Property 字幕其他行字号 As Single = 48.0F
    Public Property 字幕其他行样式 As Integer = CInt(FontStyle.Regular)
    Public Property 字幕不透明度 As Integer = 255
    Public Property 字幕描边样式 As Integer = 1
    Public Property 字幕阴影样式 As Integer = 1
    Public Property 字幕行间距 As Integer = 10
    Public Property 字幕底部间距 As Integer = 10
    Public Property 字幕底部对齐方式 As Integer = 0
    Public Property 字幕尺寸缩放方式 As Integer = 0

    Public Property 弹幕已启用 As Boolean = True
    Public Property 弹幕字体 As String = "Microsoft YaHei UI"
    Public Property 弹幕字号 As Single = 36.0F
    Public Property 弹幕字体样式 As Integer = CInt(FontStyle.Regular)
    Public Property 弹幕不透明度 As Integer = 255
    Public Property 弹幕描边样式 As Integer = 1
    Public Property 弹幕阴影样式 As Integer = 1
    Public Property 渲染常规滚动弹幕 As Boolean = True
    Public Property 渲染顶部弹幕 As Boolean = True
    Public Property 渲染底部弹幕 As Boolean = True
    Public Property 弹幕最大行数 As Integer = 5
    Public Property 弹幕最大渲染数量 As Integer = 100
    Public Property 弹幕行内前后间距 As Integer = 30
    Public Property 弹幕滚动速度 As Integer = 30
    Public Property 弹幕尺寸缩放方式 As Integer = 0

    Public Property 启用歌词支持 As Boolean = True
    Public Property 渲染封面图毛玻璃背景 As Boolean = True
    Public Property 渲染封面图 As Boolean = True

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

    Private Shared ReadOnly 设置文件路径 As String = Path.Combine(程序目录, "FFF.Player.Settings.json")
    Public Shared ReadOnly 自定义图标路径 As String = Path.Combine(程序目录, "3FP_Icon")
    Public Shared ReadOnly 自定义背景图路径 As String = Path.Combine(程序目录, "3FP_BackImage")
    Private Shared 当前自有背景图 As Image
    Private Shared 当前自有图标 As Icon

    Public Shared Sub 启动时加载设置()
        Try
            If Not File.Exists(设置文件路径) Then
                If FontFamily.Families.Any(Function(f) f.Name = "微软雅黑") Then 实例对象.字体 = "微软雅黑"
                退出时保存设置()
                Return
            End If
            Dim 读取 = JsonSerializer.Deserialize(Of 设置)(File.ReadAllText(设置文件路径, Text.Encoding.UTF8))
            If 读取 Is Nothing Then Throw New JsonException("设置对象为空。")
            实例对象 = 读取
            实例对象.规范化()
        Catch
            Try
                If File.Exists(设置文件路径) Then
                    File.Copy(设置文件路径, 设置文件路径 & ".broken-" & DateTime.Now.ToString("yyyyMMddHHmmss"), True)
                End If
            Catch
            End Try
            实例对象 = New 设置()
            退出时保存设置()
        End Try
    End Sub

    Public Shared Sub 退出时保存设置()
        Dim 临时路径 As String = Nothing
        Try
            实例对象.规范化()
            临时路径 = 设置文件路径 & ".tmp-" & Guid.NewGuid().ToString("N")
            File.WriteAllText(临时路径, JsonSerializer.Serialize(实例对象, JsonSO), Text.Encoding.UTF8)
            File.Move(临时路径, 设置文件路径, True)
        Catch ex As Exception
            If Not String.IsNullOrWhiteSpace(临时路径) Then
                Try
                    If File.Exists(临时路径) Then File.Delete(临时路径)
                Catch
                End Try
            End If
            MessageBox.Show($"保存设置失败：{ex.Message}", "FFF.Player", MessageBoxButtons.OK, MessageBoxIcon.Error)
        End Try
    End Sub

    Public Sub 规范化()
        字体 = 规范字体(字体, SystemFonts.DefaultFont.FontFamily.Name)
        初始画面尺寸选项 = Math.Clamp(初始画面尺寸选项, 0, 9)
        自定义初始画面宽度 = Math.Clamp(自定义初始画面宽度, 320, 7680)
        自定义初始画面高度 = Math.Clamp(自定义初始画面高度, 180, 4320)
        If 解码方式 <> CInt(解码模式.CPU) AndAlso 解码方式 <> CInt(解码模式.GPU) Then
            解码方式 = CInt(解码模式.CPU)
        End If
        HDR峰值亮度选项 = Math.Clamp(HDR峰值亮度选项, 0, 9)
        If HDR峰值亮度 < 0 OrElse HDR峰值亮度 > 10000 Then HDR峰值亮度 = 0
        HDR映射SDR参考亮度 = Math.Clamp(HDR映射SDR参考亮度, 1, 500)
        规范化字幕字体(字幕第一行字体, 字幕第一行字号, 字幕第一行样式)
        规范化字幕字体(字幕第二行字体, 字幕第二行字号, 字幕第二行样式)
        规范化字幕字体(字幕其他行字体, 字幕其他行字号, 字幕其他行样式)
        字幕不透明度 = Math.Clamp(字幕不透明度, 0, 255)
        字幕描边样式 = Math.Clamp(字幕描边样式, 0, 2)
        字幕阴影样式 = Math.Clamp(字幕阴影样式, 0, 2)
        字幕行间距 = Math.Clamp(字幕行间距, 0, 30)
        字幕底部间距 = Math.Clamp(字幕底部间距, 0, 50)
        字幕底部对齐方式 = Math.Clamp(字幕底部对齐方式, 0, 1)
        字幕尺寸缩放方式 = Math.Clamp(字幕尺寸缩放方式, 0, 3)
        弹幕字体 = 规范字体(弹幕字体, "Microsoft YaHei UI")
        If Not Single.IsFinite(弹幕字号) OrElse 弹幕字号 < 8 OrElse 弹幕字号 > 200 Then 弹幕字号 = 36
        弹幕字体样式 = 规范字体样式(弹幕字体样式)
        弹幕不透明度 = Math.Clamp(弹幕不透明度, 0, 255)
        弹幕描边样式 = Math.Clamp(弹幕描边样式, 0, 2)
        弹幕阴影样式 = Math.Clamp(弹幕阴影样式, 0, 2)
        弹幕最大行数 = Math.Clamp(弹幕最大行数, 1, 20)
        弹幕最大渲染数量 = Math.Clamp(弹幕最大渲染数量, 1, 200)
        弹幕行内前后间距 = Math.Clamp(弹幕行内前后间距, 0, 100)
        弹幕滚动速度 = Math.Clamp(弹幕滚动速度, 1, 100)
        弹幕尺寸缩放方式 = Math.Clamp(弹幕尺寸缩放方式, 0, 3)
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

    Private Shared Sub 规范化字幕字体(ByRef 字体名称 As String, ByRef 字号 As Single,
                                ByRef 样式 As Integer)
        字体名称 = 规范字体(字体名称, "Microsoft YaHei UI")
        If Not Single.IsFinite(字号) OrElse 字号 < 8 OrElse 字号 > 200 Then 字号 = 48
        样式 = 规范字体样式(样式)
    End Sub

    Private Shared Function 规范字体(value As String, fallback As String) As String
        Return If(String.IsNullOrWhiteSpace(value), fallback, value.Trim())
    End Function

    Private Shared Function 规范字体样式(value As Integer) As Integer
        Return value And CInt(FontStyle.Bold Or FontStyle.Italic Or FontStyle.Underline Or FontStyle.Strikeout)
    End Function

    Public Function 取得初始画面尺寸() As Size
        Dim 预设 = {Size.Empty, New Size(640, 360), New Size(854, 480), New Size(960, 540),
                    New Size(1024, 576), New Size(1366, 768), New Size(1600, 900),
                    New Size(1920, 1080), New Size(2560, 1440), New Size(3840, 2160)}
        If 初始画面尺寸选项 = 0 Then Return New Size(自定义初始画面宽度, 自定义初始画面高度)
        Return 预设(初始画面尺寸选项)
    End Function

    Public Function 创建SRT字幕样式() As SRT字幕样式
        Dim 描边宽度 = {0.0F, 2.0F, 4.0F}(字幕描边样式)
        Dim 阴影偏移 = {0.0F, 2.0F, 4.0F}(字幕阴影样式)
        Return New SRT字幕样式 With {
            .第一行字体 = 字幕第一行字体, .第一行字号 = 字幕第一行字号,
            .第一行字体样式 = CType(字幕第一行样式, FontStyle),
            .第一行颜色ARGB = 应用不透明度(字幕第一行颜色ARGB, 字幕不透明度),
            .第二行字体 = 字幕第二行字体, .第二行字号 = 字幕第二行字号,
            .第二行字体样式 = CType(字幕第二行样式, FontStyle),
            .第二行颜色ARGB = 应用不透明度(字幕第二行颜色ARGB, 字幕不透明度),
            .其他行字体 = 字幕其他行字体, .其他行字号 = 字幕其他行字号,
            .其他行字体样式 = CType(字幕其他行样式, FontStyle),
            .颜色ARGB = 应用不透明度(&HFFFFFFFFUI, 字幕不透明度),
            .描边宽度 = 描边宽度,
            .描边颜色ARGB = If(描边宽度 > 0, 应用不透明度(&HC0000000UI, 字幕不透明度), 0UI),
            .阴影偏移 = 阴影偏移,
            .阴影颜色ARGB = If(阴影偏移 > 0, 应用不透明度(&H70000000UI, 字幕不透明度), 0UI),
            .行间距 = 字幕行间距, .底部边距 = 字幕底部间距,
            .底部对齐方式 = 字幕底部对齐方式, .尺寸缩放方式 = 字幕尺寸缩放方式
        }
    End Function

    Public Function 创建弹幕显示配置() As 弹幕显示配置
        Dim 类型 = 弹幕类型.无
        If 渲染常规滚动弹幕 Then 类型 = 类型 Or 弹幕类型.常规滚动
        If 渲染顶部弹幕 Then 类型 = 类型 Or 弹幕类型.顶部
        If 渲染底部弹幕 Then 类型 = 类型 Or 弹幕类型.底部
        Dim 描边宽度 = {0.0F, 1.0F, 2.0F}(弹幕描边样式)
        Dim 阴影深度 = {0.0F, 1.5F, 3.0F}(弹幕阴影样式)
        Return New 弹幕显示配置 With {
            .字体 = 弹幕字体, .字号 = 弹幕字号, .字体样式 = CType(弹幕字体样式, FontStyle),
            .滚动速度 = 弹幕滚动速度 * 6.0F, .同屏最大数量 = 弹幕最大渲染数量,
            .常规滚动最大行数 = 弹幕最大行数, .顶部最大行数 = 弹幕最大行数,
            .行内前后间距 = 弹幕行内前后间距, .尺寸缩放方式 = 弹幕尺寸缩放方式,
            .启用类型 = 类型, .不透明度 = 弹幕不透明度, .描边宽度 = 描边宽度,
            .描边颜色ARGB = If(描边宽度 > 0, 应用不透明度(&HC0000000UI, 弹幕不透明度), 0UI),
            .阴影深度 = 阴影深度,
            .阴影颜色ARGB = If(阴影深度 > 0, 应用不透明度(&H70000000UI, 弹幕不透明度), 0UI)
        }
    End Function

    Friend Shared Function 应用不透明度(value As UInteger, opacity As Integer) As UInteger
        Dim 限定不透明度 = Math.Clamp(opacity, 0, 255)
        Dim 原始Alpha = CULng((value >> 24) And &HFFUI)
        Dim 合成Alpha = CUInt((原始Alpha * CULng(限定不透明度) + 127UL) \ 255UL)
        Return (value And &HFFFFFFUI) Or (合成Alpha << 24)
    End Function

    Public Shared Sub 应用SP个性化设置()
        If Not SP_UnLock OrElse Form1.当前主窗体 Is Nothing Then Return
        Dim 窗口 = Form1.当前主窗体.ThisIsYourWindow1
        Dim 边框颜色 = Color.FromArgb(实例对象.SP_窗口边框颜色_A, 实例对象.SP_窗口边框颜色_R,
                                  实例对象.SP_窗口边框颜色_G, 实例对象.SP_窗口边框颜色_B)
        窗口.BorderColor = 边框颜色
        窗口.BorderInactiveColor = 边框颜色
        窗口.LayerShadowColor = Color.FromArgb(实例对象.SP_分层阴影颜色_A, 实例对象.SP_分层阴影颜色_R,
                                           实例对象.SP_分层阴影颜色_G, 实例对象.SP_分层阴影颜色_B)
        窗口.BorderSize = 实例对象.SP_边框宽度
        Select Case 实例对象.SP_毛玻璃模式
            Case 0
                窗口.BackdropMode = LakeUI.ThisIsYourWindow.BackdropModeEnum.None
                窗口.BackdropBlurPasses = 0
                窗口.BackdropNoiseOpacity = 0
                清除SP自有背景图()
            Case Else
                窗口.BackdropBlurPasses = If(实例对象.SP_毛玻璃模式 = 1, 0, If(实例对象.SP_毛玻璃模式 = 2, 1, 3))
                窗口.BackdropBlurRadius = If(实例对象.SP_毛玻璃模式 = 2, 10, 24)
                If 实例对象.SP_毛玻璃背景来源 = 0 Then
                    窗口.BackdropMode = LakeUI.ThisIsYourWindow.BackdropModeEnum.Image
                    加载SP自定义背景图()
                ElseIf 实例对象.SP_毛玻璃背景来源 = 1 Then
                    窗口.BackdropMode = LakeUI.ThisIsYourWindow.BackdropModeEnum.Auto
                    清除SP自有背景图()
                Else
                    窗口.BackdropMode = LakeUI.ThisIsYourWindow.BackdropModeEnum.None
                    清除SP自有背景图()
                End If
                窗口.BackdropNoiseOpacity = If(实例对象.SP_毛玻璃噪点颗粒 = 1, 18,
                                                If(实例对象.SP_毛玻璃噪点颗粒 = 2, 36, 0))
        End Select
    End Sub

    Public Shared Sub 加载SP自定义图标()
        If Not SP_UnLock OrElse Form1.当前主窗体 Is Nothing OrElse Not File.Exists(自定义图标路径) Then Return
        Dim image = 加载图片副本(自定义图标路径)
        Dim newIcon = 从图片创建图标(image)
        image.Dispose()
        Dim oldIcon = 当前自有图标
        当前自有图标 = newIcon
        Form1.当前主窗体.Icon = newIcon
        oldIcon?.Dispose()
    End Sub

    Public Shared Sub 加载SP自定义背景图()
        If Not SP_UnLock OrElse Form1.当前主窗体 Is Nothing OrElse Not File.Exists(自定义背景图路径) Then Return
        Dim newImage = 加载图片副本(自定义背景图路径)
        Dim oldImage = 当前自有背景图
        当前自有背景图 = newImage
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
        当前自有图标?.Dispose()
        当前自有图标 = Nothing
    End Sub

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
