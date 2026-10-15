Friend Module Form媒体信息定位扩展
    ''' <summary>按宿主窗体的完整边界居中，并限制在当前工作区可见范围内。</summary>
    <System.Runtime.CompilerServices.Extension>
    Friend Function 居中于(窗体 As Form, 宿主边界 As Rectangle) As Point
        Dim point = New Point(宿主边界.Left + (宿主边界.Width - 窗体.Width) \ 2,
                              宿主边界.Top + (宿主边界.Height - 窗体.Height) \ 2)
        Dim workArea = Screen.FromRectangle(宿主边界).WorkingArea
        point.X = Math.Clamp(point.X, workArea.Left, Math.Max(workArea.Left, workArea.Right - 窗体.Width))
        point.Y = Math.Clamp(point.Y, workArea.Top, Math.Max(workArea.Top, workArea.Bottom - 窗体.Height))
        Return point
    End Function
End Module
