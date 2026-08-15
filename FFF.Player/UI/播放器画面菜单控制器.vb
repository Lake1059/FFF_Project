Imports System.Drawing.Drawing2D
Imports System.Drawing.Imaging
Imports System.Globalization
Imports System.IO
Imports System.Text.RegularExpressions

''' <summary>挂接并处理标题栏中的画面尺寸与截图菜单。</summary>
Friend NotInheritable Class 播放器画面菜单控制器
    Implements IDisposable

    Private Shared ReadOnly 百分比尺寸格式 As New Regex(
        "^原始(?:宽度|高度)\s+(?<percent>\d+(?:\.\d+)?)%$",
        RegexOptions.CultureInvariant Or RegexOptions.Compiled)
    Private Shared ReadOnly 固定宽度格式 As New Regex(
        "^宽度\s+(?<width>\d+)\s+比例\s+(?<x>\d+)\s*:\s*(?<y>\d+)$",
        RegexOptions.CultureInvariant Or RegexOptions.Compiled)

    Private ReadOnly 宿主窗口 As Form
    Private ReadOnly 画面控件 As 播放器画面控件
    Private ReadOnly 标题栏菜单 As LakeUI.ModernContextMenu
    Private ReadOnly 尺寸菜单 As LakeUI.ModernContextMenu
    Private ReadOnly 截图菜单 As LakeUI.ModernContextMenu
    Private ReadOnly 窗口布局控制器 As 播放器窗口布局控制器
    Private ReadOnly 快照提供器 As Func(Of 播放器快照)
    Private ReadOnly 初始画面尺寸提供器 As Func(Of Size)
    Private ReadOnly 当前媒体路径提供器 As Func(Of String)
    Private ReadOnly 操作提示 As Action(Of String)
    Private 已释放 As Boolean

    Friend Sub New(宿主窗口值 As Form,
                   画面控件值 As 播放器画面控件,
                   标题栏菜单值 As LakeUI.ModernContextMenu,
                   尺寸菜单值 As LakeUI.ModernContextMenu,
                   截图菜单值 As LakeUI.ModernContextMenu,
                   窗口布局控制器值 As 播放器窗口布局控制器,
                   快照提供器值 As Func(Of 播放器快照),
                   初始画面尺寸提供器值 As Func(Of Size),
                   当前媒体路径提供器值 As Func(Of String),
                   操作提示值 As Action(Of String))
        ArgumentNullException.ThrowIfNull(宿主窗口值)
        ArgumentNullException.ThrowIfNull(画面控件值)
        ArgumentNullException.ThrowIfNull(标题栏菜单值)
        ArgumentNullException.ThrowIfNull(尺寸菜单值)
        ArgumentNullException.ThrowIfNull(截图菜单值)
        ArgumentNullException.ThrowIfNull(窗口布局控制器值)
        ArgumentNullException.ThrowIfNull(快照提供器值)
        ArgumentNullException.ThrowIfNull(初始画面尺寸提供器值)
        ArgumentNullException.ThrowIfNull(当前媒体路径提供器值)
        ArgumentNullException.ThrowIfNull(操作提示值)

        宿主窗口 = 宿主窗口值
        画面控件 = 画面控件值
        标题栏菜单 = 标题栏菜单值
        尺寸菜单 = 尺寸菜单值
        截图菜单 = 截图菜单值
        窗口布局控制器 = 窗口布局控制器值
        快照提供器 = 快照提供器值
        初始画面尺寸提供器 = 初始画面尺寸提供器值
        当前媒体路径提供器 = 当前媒体路径提供器值
        操作提示 = 操作提示值

        挂接菜单()
    End Sub

    Friend Sub 应用全局字体(fontName As String)
        If 已释放 Then Return
        Dim 字体名称 = If(String.IsNullOrWhiteSpace(fontName), "Microsoft YaHei UI", fontName.Trim())
        For Each 菜单 In {标题栏菜单, 尺寸菜单, 截图菜单}
            应用菜单字体(菜单, 字体名称)
        Next
    End Sub

    Private Sub 挂接菜单()
        For Each 菜单项 In 标题栏菜单.Items
            Select Case 读取菜单文字(菜单项)
                Case "调整渲染区域大小"
                    菜单项.SubMenu = 尺寸菜单
                Case "截取当前画面"
                    菜单项.SubMenu = 截图菜单
            End Select
        Next

        For Each 菜单项 In 尺寸菜单.Items
            If Not 菜单项.IsSeparator AndAlso Not 菜单项.IsDescription Then
                AddHandler 菜单项.Click, AddressOf 尺寸菜单项_Click
            End If
        Next
        For Each 菜单项 In 截图菜单.Items
            If Not 菜单项.IsSeparator AndAlso Not 菜单项.IsDescription Then
                AddHandler 菜单项.Click, AddressOf 截图菜单项_Click
            End If
        Next
    End Sub

    Private Sub 尺寸菜单项_Click(sender As Object, e As EventArgs)
        If 已释放 Then Return
        Dim 菜单项 = TryCast(sender, LakeUI.ModernContextMenu.ModernMenuItem)
        Dim 快照 = 安全读取快照()
        Dim 原始尺寸 = 取得原始画面尺寸(快照)
        Dim 目标尺寸 = 解析目标画面尺寸(读取菜单文字(菜单项), 原始尺寸, 初始画面尺寸提供器())
        If 目标尺寸.IsEmpty Then
            显示错误("无法调整画面大小", "当前菜单选项无效，或媒体没有可用的视频画面。")
            Return
        End If
        窗口布局控制器.调整画面尺寸(目标尺寸)
    End Sub

    Private Sub 截图菜单项_Click(sender As Object, e As EventArgs)
        If 已释放 Then Return
        Dim 菜单项 = TryCast(sender, LakeUI.ModernContextMenu.ModernMenuItem)
        Dim 菜单文字 = 读取菜单文字(菜单项)
        If 宿主窗口.IsDisposed OrElse Not 宿主窗口.IsHandleCreated Then Return
        Try
            宿主窗口.BeginInvoke(Sub() 执行截图菜单项(菜单文字))
        Catch ex As InvalidOperationException
            显示错误("无法截取画面", ex.Message)
        End Try
    End Sub

    Private Sub 执行截图菜单项(菜单文字 As String)
        If 已释放 OrElse 宿主窗口.IsDisposed Then Return
        Try
            Select Case If(菜单文字, String.Empty).Trim()
                Case "剪贴板：原始画面"
                    Using 图像 = 截取原始画面()
                        Clipboard.SetImage(图像)
                    End Using
                    操作提示("原始画面已复制到剪贴板")
                Case "剪贴板：实际渲染"
                    Using 图像 = 截取实际渲染()
                        Clipboard.SetImage(图像)
                    End Using
                    操作提示("实际渲染画面已复制到剪贴板")
                Case "原始画面保存到当前目录"
                    Using 图像 = 截取原始画面()
                        Dim 路径 = 生成截图路径()
                        图像.Save(路径, ImageFormat.Png)
                        操作提示($"画面已保存到 {路径}")
                    End Using
                Case Else
                    显示错误("无法截取画面", "当前截图菜单选项无效。")
            End Select
        Catch ex As Exception
            显示错误("无法截取画面", ex.Message)
        End Try
    End Sub

    Private Function 截取原始画面() As Bitmap
        Dim 快照 = 安全读取快照()
        Dim 原始尺寸 = 取得原始画面尺寸(快照)
        If 原始尺寸.IsEmpty Then Throw New InvalidOperationException("媒体没有可用的视频画面。")

        Dim 视频区域 = 计算视频显示矩形(画面控件.ClientSize, 原始尺寸)
        Using 当前画面 = 截取客户区(视频区域)
            Dim 原始画面 As New Bitmap(原始尺寸.Width, 原始尺寸.Height, PixelFormat.Format32bppArgb)
            Using 图形 = Graphics.FromImage(原始画面)
                图形.CompositingMode = CompositingMode.SourceCopy
                图形.CompositingQuality = CompositingQuality.HighQuality
                图形.InterpolationMode = InterpolationMode.HighQualityBicubic
                图形.PixelOffsetMode = PixelOffsetMode.HighQuality
                图形.DrawImage(当前画面, New Rectangle(Point.Empty, 原始尺寸))
            End Using
            Return 原始画面
        End Using
    End Function

    Private Function 截取实际渲染() As Bitmap
        Return 截取客户区(画面控件.ClientRectangle)
    End Function

    Private Function 截取客户区(客户区范围 As Rectangle) As Bitmap
        If Not 画面控件.Visible OrElse Not 画面控件.IsHandleCreated OrElse
            客户区范围.Width <= 0 OrElse 客户区范围.Height <= 0 Then
            Throw New InvalidOperationException("当前没有可截取的渲染画面。")
        End If
        Dim 屏幕原点 = 画面控件.PointToScreen(客户区范围.Location)
        Dim 图像 As New Bitmap(客户区范围.Width, 客户区范围.Height, PixelFormat.Format32bppArgb)
        Try
            Using 图形 = Graphics.FromImage(图像)
                图形.CopyFromScreen(屏幕原点, Point.Empty, 客户区范围.Size, CopyPixelOperation.SourceCopy)
            End Using
            Return 图像
        Catch
            图像.Dispose()
            Throw
        End Try
    End Function

    Private Function 生成截图路径() As String
        Dim 媒体路径 = 当前媒体路径提供器()
        Dim 基本名称 = Path.GetFileNameWithoutExtension(媒体路径)
        If String.IsNullOrWhiteSpace(基本名称) Then 基本名称 = "FFF.Player"
        Dim 文件名 = $"{基本名称}_{Date.Now:yyyyMMdd_HHmmss_fff}.png"
        Return Path.Combine(Environment.CurrentDirectory, 文件名)
    End Function

    Private Function 安全读取快照() As 播放器快照
        Try
            Return 快照提供器()
        Catch ex As ObjectDisposedException
            Return Nothing
        End Try
    End Function

    Private Sub 显示错误(标题 As String, 说明 As String)
        If 宿主窗口.IsDisposed Then Return
        LakeUI.ExOverlayMsgBox(宿主窗口, 说明, MsgBoxStyle.Critical Or MsgBoxStyle.OkOnly, 标题)
    End Sub

    Friend Shared Function 解析目标画面尺寸(菜单文字 As String, 原始尺寸 As Size, 初始尺寸 As Size) As Size
        Select Case If(菜单文字, String.Empty).Trim()
            Case "设置的初始值"
                Return 有效尺寸或空值(初始尺寸)
            Case "原始视频"
                Return 有效尺寸或空值(原始尺寸)
        End Select

        Dim 百分比匹配 = 百分比尺寸格式.Match(If(菜单文字, String.Empty).Trim())
        If 百分比匹配.Success AndAlso Not 原始尺寸.IsEmpty Then
            Dim 百分比 As Double
            If Double.TryParse(百分比匹配.Groups("percent").Value, NumberStyles.AllowDecimalPoint,
                               CultureInfo.InvariantCulture, 百分比) AndAlso 百分比 > 0 Then
                Dim 缩放 = 百分比 / 100.0R
                Return New Size(Math.Max(1, CInt(Math.Round(原始尺寸.Width * 缩放))),
                                Math.Max(1, CInt(Math.Round(原始尺寸.Height * 缩放))))
            End If
        End If

        Dim 固定宽度匹配 = 固定宽度格式.Match(If(菜单文字, String.Empty).Trim())
        If 固定宽度匹配.Success Then
            Dim 宽度, 比例宽度, 比例高度 As Integer
            If Integer.TryParse(固定宽度匹配.Groups("width").Value, 宽度) AndAlso
                Integer.TryParse(固定宽度匹配.Groups("x").Value, 比例宽度) AndAlso
                Integer.TryParse(固定宽度匹配.Groups("y").Value, 比例高度) AndAlso
                宽度 > 0 AndAlso 比例宽度 > 0 AndAlso 比例高度 > 0 Then
                Return New Size(宽度, Math.Max(1, CInt(Math.Round(CDbl(宽度) * 比例高度 / 比例宽度))))
            End If
        End If
        Return Size.Empty
    End Function

    Friend Shared Function 计算视频显示矩形(输出尺寸 As Size, 视频尺寸 As Size) As Rectangle
        If 输出尺寸.Width <= 0 OrElse 输出尺寸.Height <= 0 OrElse
            视频尺寸.Width <= 0 OrElse 视频尺寸.Height <= 0 Then Return Rectangle.Empty
        Dim 宽度 = 输出尺寸.Width
        Dim 高度 = 输出尺寸.Height
        If CLng(输出尺寸.Width) * 视频尺寸.Height <= CLng(输出尺寸.Height) * 视频尺寸.Width Then
            高度 = Math.Max(1, CInt((CLng(输出尺寸.Width) * 视频尺寸.Height + 视频尺寸.Width \ 2) \ 视频尺寸.Width))
        Else
            宽度 = Math.Max(1, CInt((CLng(输出尺寸.Height) * 视频尺寸.Width + 视频尺寸.Height \ 2) \ 视频尺寸.Height))
        End If
        宽度 = Math.Min(宽度, 输出尺寸.Width)
        高度 = Math.Min(高度, 输出尺寸.Height)
        Return New Rectangle((输出尺寸.Width - 宽度) \ 2, (输出尺寸.Height - 高度) \ 2, 宽度, 高度)
    End Function

    Private Shared Function 取得原始画面尺寸(快照 As 播放器快照) As Size
        If 快照 Is Nothing OrElse 快照.视频宽度 = 0 OrElse 快照.视频高度 = 0 OrElse
            快照.视频宽度 > Integer.MaxValue OrElse 快照.视频高度 > Integer.MaxValue Then Return Size.Empty
        Return New Size(CInt(快照.视频宽度), CInt(快照.视频高度))
    End Function

    Private Shared Function 有效尺寸或空值(尺寸 As Size) As Size
        Return If(尺寸.Width > 0 AndAlso 尺寸.Height > 0, 尺寸, Size.Empty)
    End Function

    Private Shared Function 读取菜单文字(菜单项 As LakeUI.ModernContextMenu.ModernMenuItem) As String
        Return If(菜单项?.Text, String.Empty).Trim()
    End Function

    Private Shared Sub 应用菜单字体(菜单 As LakeUI.ModernContextMenu, 字体名称 As String)
        Dim 旧菜单字体 = 菜单.MenuFont
        Dim 旧说明字体 = 菜单.DescriptionFont
        Dim 新菜单字体 = 创建菜单字体(字体名称, 旧菜单字体, 10.0F)
        Dim 新说明字体 = 创建菜单字体(字体名称, 旧说明字体, 9.0F)
        菜单.MenuFont = 新菜单字体
        菜单.DescriptionFont = 新说明字体
        If 旧菜单字体 IsNot Nothing AndAlso Not ReferenceEquals(旧菜单字体, 新菜单字体) Then 旧菜单字体.Dispose()
        If 旧说明字体 IsNot Nothing AndAlso Not ReferenceEquals(旧说明字体, 新说明字体) Then 旧说明字体.Dispose()
    End Sub

    Private Shared Function 创建菜单字体(字体名称 As String, 当前字体 As Font, 默认字号 As Single) As Font
        Dim 字号 = If(当前字体 Is Nothing, 默认字号, 当前字体.SizeInPoints)
        Try
            Return New Font(字体名称, 字号, FontStyle.Regular, GraphicsUnit.Point)
        Catch
            Try
                Return New Font("Microsoft YaHei UI", 字号, FontStyle.Regular, GraphicsUnit.Point)
            Catch
                Return New Font(SystemFonts.DefaultFont.FontFamily, 字号, FontStyle.Regular, GraphicsUnit.Point)
            End Try
        End Try
    End Function

    Public Sub Dispose() Implements IDisposable.Dispose
        If 已释放 Then Return
        已释放 = True
        For Each 菜单项 In 尺寸菜单.Items
            RemoveHandler 菜单项.Click, AddressOf 尺寸菜单项_Click
        Next
        For Each 菜单项 In 截图菜单.Items
            RemoveHandler 菜单项.Click, AddressOf 截图菜单项_Click
        Next
    End Sub
End Class
