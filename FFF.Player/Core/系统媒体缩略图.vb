Imports System.Drawing
Imports System.Drawing.Drawing2D
Imports System.Drawing.Imaging
Imports System.IO

''' <summary>
''' 系统媒体卡缩略图的托管侧编码：把当前首帧 / 封面渲染帧压成小尺寸 PNG。
''' SMTC 的缩略图入口只接受已编码 PNG 字节；真源可能是 4K 视频帧或 16 位 HDR
''' 帧，直接原样塞进去会让内核多走一大段不必要的数据，所以这里统一限制最长边。
''' </summary>
Public Module 系统媒体缩略图

    ''' <summary>缩略图最长边；Windows 媒体面板的卡片本身很小，320 已足够清晰。</summary>
    Public Const 默认最大边长 As Integer = 320

    ''' <summary>
    ''' 把一帧编码为 PNG。源为空、尺寸非法或编码失败时返回空数组（调用方据此
    ''' 清除系统卡上的旧缩略图），绝不向 SMTC 抛异常。
    ''' </summary>
    Public Function 编码PNG(源 As Bitmap, Optional 最大边长 As Integer = 默认最大边长) As Byte()
        If 源 Is Nothing OrElse 源.Width <= 0 OrElse 源.Height <= 0 Then Return Array.Empty(Of Byte)()
        If 最大边长 <= 0 Then Throw New ArgumentOutOfRangeException(NameOf(最大边长))

        Dim 比例 = Math.Min(1.0R, 最大边长 / CDbl(Math.Max(源.Width, 源.Height)))
        Dim 宽 = Math.Max(1, CInt(Math.Round(源.Width * 比例)))
        Dim 高 = Math.Max(1, CInt(Math.Round(源.Height * 比例)))

        Try
            Using 目标 As New Bitmap(宽, 高, PixelFormat.Format32bppArgb)
                Using 图形 = Graphics.FromImage(目标)
                    图形.Clear(Color.Black)
                    图形.CompositingMode = CompositingMode.SourceCopy
                    图形.CompositingQuality = CompositingQuality.HighQuality
                    图形.InterpolationMode = InterpolationMode.HighQualityBicubic
                    图形.PixelOffsetMode = PixelOffsetMode.HighQuality
                    图形.DrawImage(源, New Rectangle(0, 0, 宽, 高))
                End Using
                Using 流 As New MemoryStream()
                    目标.Save(流, ImageFormat.Png)
                    Return 流.ToArray()
                End Using
            End Using
        Catch ex As Exception
            Debug.WriteLine("SMTC：缩略图 PNG 编码失败（已忽略）：" & ex.GetType().Name)
            Return Array.Empty(Of Byte)()
        End Try
    End Function

End Module
