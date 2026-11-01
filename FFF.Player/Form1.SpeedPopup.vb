Imports System.Drawing.Drawing2D

Partial Public Class Form1
    Private Sub 显示倍速展开()
        If 倍速弹窗 IsNot Nothing AndAlso Not 倍速弹窗.IsDisposed Then
            倍速弹窗.Close()
            Return
        End If
        Dim 比例 = DeviceDpi / 96.0F
        Dim 弹窗 As New Form With {.FormBorderStyle = FormBorderStyle.None, .ShowInTaskbar = False, .StartPosition = FormStartPosition.Manual, .AutoScaleMode = AutoScaleMode.Dpi, .AutoScaleDimensions = New SizeF(DeviceDpi, DeviceDpi), .ClientSize = New Size(CInt(258 * 比例), CInt(194 * 比例)), .BackColor = BackColor, .Font = MB_软件解码或硬件解码.Font, .KeyPreview = True}
        倍速弹窗 = 弹窗
        Dim 背景 As New LakeUI.ModernPanel With {.Dock = DockStyle.Fill, .BorderRadius = 8, .BackColor1 = BackColor, .BorderSize = 1, .BorderColor = MB_软件解码或硬件解码.BorderColor, .Padding = New Padding(CInt(8 * 比例))}
        弹窗.Controls.Add(背景)
        Dim 布局 As New TableLayoutPanel With {.Dock = DockStyle.Fill, .ColumnCount = 3, .RowCount = 5}
        For i = 0 To 2
            布局.ColumnStyles.Add(New ColumnStyle(SizeType.Percent, 100.0F / 3))
        Next
        For i = 0 To 4
            布局.RowStyles.Add(New RowStyle(SizeType.Percent, 20))
        Next
        背景.Controls.Add(布局)
        Dim 预设按钮 As New List(Of (按钮 As LakeUI.ModernButton, 值 As Double))
        Dim 倍速输入 As New LakeUI.ModernTextBox With {.Name = "倍速输入", .Dock = DockStyle.Fill, .MultiLine = False, .TextAlign = LakeUI.ModernTextBox.TextAlignMode.Center, .BorderRadius = 5, .Font = 弹窗.Font, .ForeColor = ForeColor, .BackColor1 = BackColor, .Margin = New Padding(2)}
        Dim 刷新 As Action = Sub()
                                For Each 项 In 预设按钮
                                    项.按钮.BackColor1 = If(Math.Abs(当前倍速 - 项.值) < 0.001, Color.FromArgb(80, 127, 189), Color.Transparent)
                                Next
                                倍速输入.Text = 当前倍速.ToString("0.00", Globalization.CultureInfo.InvariantCulture)
                            End Sub
        Dim 速度 = {0.25, 0.5, 0.75, 0.9, 1, 1.1, 1.25, 1.5, 1.75, 2, 2.5, 16}
        For i = 0 To 速度.Length - 1
            Dim 值 = 速度(i)
            Dim 按钮 As New LakeUI.ModernButton With {.Text = 值.ToString("0.00", Globalization.CultureInfo.InvariantCulture), .Dock = DockStyle.Fill, .BorderRadius = 5, .Font = 弹窗.Font, .ForeColor = ForeColor, .Margin = New Padding(2)}
            布局.Controls.Add(按钮, i Mod 3, i \ 3)
            预设按钮.Add((按钮, 值))
            AddHandler 按钮.Click, Sub()
                                      应用倍速(值)
                                      刷新()
                                  End Sub
        Next
        Dim 步进 As New TableLayoutPanel With {.Dock = DockStyle.Fill, .ColumnCount = 5, .Margin = Padding.Empty}
        布局.Controls.Add(步进, 0, 4) : 布局.SetColumnSpan(步进, 3)
        Dim 标签 = {"≪", "‹", "", "›", "≫"}
        Dim 增量 = {-0.5, -0.05, 0, 0.05, 0.5}
        For i = 0 To 4
            步进.ColumnStyles.Add(New ColumnStyle(SizeType.Percent, 20))
            If i = 2 Then
                步进.Controls.Add(倍速输入, i, 0)
                Continue For
            End If
            Dim 值 = 增量(i)
            Dim 按钮 As New LakeUI.ModernButton With {.Text = 标签(i), .Dock = DockStyle.Fill, .BorderRadius = 5, .Font = 弹窗.Font, .ForeColor = ForeColor, .Margin = New Padding(2)}
            步进.Controls.Add(按钮, i, 0)
            Dim 调整 As EventHandler = Sub()
                                        应用倍速(Math.Clamp(Math.Round(当前倍速 + 值, 2), 0.1, 16))
                                        刷新()
                                    End Sub
            AddHandler 按钮.Click, 调整
            AddHandler 按钮.DoubleClick, 调整
        Next
        Dim 提交输入 As Action = Sub()
                                    Dim 值 As Decimal
                                    If Decimal.TryParse(倍速输入.Text, Globalization.NumberStyles.Float, Globalization.CultureInfo.InvariantCulture, 值) Then
                                        值 = Math.Clamp(值, 0.1D, 16D)
                                        应用倍速(CDbl(Math.Round(值 / 0.05D, 0, MidpointRounding.AwayFromZero) * 0.05D))
                                    End If
                                    刷新()
                                End Sub
        AddHandler 倍速输入.Leave, Sub() 提交输入()
        AddHandler 倍速输入.KeyDown, Sub(sender, e)
                                      If e.KeyCode = Keys.Enter Then
                                          e.SuppressKeyPress = True
                                          提交输入()
                                      End If
                                  End Sub
        AddHandler 弹窗.Deactivate, Sub() 弹窗.Close()
        AddHandler 弹窗.KeyDown, Sub(sender, e)
                                  If e.KeyCode = Keys.Escape Then 弹窗.Close()
                              End Sub
        AddHandler 弹窗.SizeChanged, Sub() 设置弹窗圆角(弹窗)
        设置弹窗圆角(弹窗)
        刷新()
        Dim 点 = 倍速按钮.PointToScreen(New Point(0, -弹窗.Height))
        Dim 屏幕 = Screen.FromControl(倍速按钮).WorkingArea
        弹窗.Location = New Point(Math.Clamp(点.X, 屏幕.Left, Math.Max(屏幕.Left, 屏幕.Right - 弹窗.Width)), Math.Clamp(点.Y, 屏幕.Top, Math.Max(屏幕.Top, 屏幕.Bottom - 弹窗.Height)))
        弹窗.Show(Me)
    End Sub

    Private Shared Sub 设置弹窗圆角(窗口 As Form)
        Dim d = 16
        Dim r = 窗口.ClientRectangle
        If r.Width < d OrElse r.Height < d Then Return
        Using 路径 As New GraphicsPath()
            路径.AddArc(0, 0, d, d, 180, 90)
            路径.AddArc(r.Width - d, 0, d, d, 270, 90)
            路径.AddArc(r.Width - d, r.Height - d, d, d, 0, 90)
            路径.AddArc(0, r.Height - d, d, d, 90, 90)
            路径.CloseFigure()
            Dim 旧区域 = 窗口.Region
            窗口.Region = New Region(路径)
            旧区域?.Dispose()
        End Using
    End Sub
End Class
