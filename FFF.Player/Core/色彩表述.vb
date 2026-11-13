Imports System

' 色彩元数据的标准表述：把内核经媒体信息 JSON 原值直传的 FFmpeg 公开枚举
' （codecpar 的 color_primaries / color_trc / colorspace / color_range，
' 定义见 libavutil/pixfmt.h，属公开接口）翻译成标准机构 + 曲线全名的口径：
'   色彩标准  BT.2020 / BT.709 / Display P3 …（原色三角色坐标所属的标准）
'   传输曲线  PQ (SMPTE ST 2084) / HLG (ARIB STD-B67) …（光电转换特性）
'   色彩矩阵  BT.2020 NCL / BT.2020 CL / BT.709 …（YUV→RGB 矩阵系数）
'   色彩范围  Limited / Full
' 这里只做命名映射，不做任何推断：值缺席（未标注、保留值、负数哨兵）一律
' 返回空串，由调用方决定隐藏（信息图层）还是省略该行（媒体信息窗体）。
' 枚举序号是 ABI 值，必须与 libavutil/pixfmt.h 逐个对齐——序号错一位就会把
' PQ 说成别的曲线，所以下面每个分支都按头文件注释落名。
' 显示文本是标准身份名（信息性使用）；媒体信息窗体与信息图层共用这一份，
' 保证两处口径永不漂移。
Friend Module 色彩表述

    ''' <summary>色彩标准（FFmpeg AVColorPrimaries）。</summary>
    Public Function 色彩标准(值 As Integer) As String
        Select Case 值
            Case 1 : Return "BT.709"
            Case 4 : Return "BT.470 M"
            Case 5 : Return "BT.601 625 (BT.470 BG)"
            Case 6 : Return "BT.601 525 (SMPTE 170M)"
            Case 7 : Return "SMPTE 240M"
            Case 8 : Return "Film (Illuminant C)"
            Case 9 : Return "BT.2020"
            Case 10 : Return "SMPTE ST 428-1 (XYZ)"
            Case 11 : Return "DCI-P3 (SMPTE ST 431-2)"
            Case 12 : Return "Display P3 (SMPTE ST 432-1)"
            Case 22 : Return "EBU Tech 3213 (JEDEC P22)"
            Case Else : Return String.Empty
        End Select
    End Function

    ''' <summary>传输曲线（FFmpeg AVColorTransferCharacteristic）。</summary>
    Public Function 传输曲线(值 As Integer) As String
        Select Case 值
            Case 1 : Return "BT.709"
            Case 4 : Return "Gamma 2.2 (BT.470 M)"
            Case 5 : Return "Gamma 2.8 (BT.470 BG)"
            Case 6 : Return "BT.601 (SMPTE 170M)"
            Case 7 : Return "SMPTE 240M"
            Case 8 : Return "Linear"
            Case 9 : Return "Logarithmic (100:1)"
            Case 10 : Return "Logarithmic (316:1)"
            Case 11 : Return "xvYCC (IEC 61966-2-4)"
            Case 12 : Return "BT.1361 (extended gamut)"
            Case 13 : Return "sRGB (IEC 61966-2-1)"
            Case 14 : Return "BT.2020 (10-bit)"
            Case 15 : Return "BT.2020 (12-bit)"
            Case 16 : Return "PQ (SMPTE ST 2084)"
            Case 17 : Return "SMPTE ST 428-1"
            Case 18 : Return "HLG (ARIB STD-B67)"
            Case Else : Return String.Empty
        End Select
    End Function

    ''' <summary>色彩矩阵（FFmpeg AVColorSpace）。</summary>
    Public Function 色彩矩阵(值 As Integer) As String
        Select Case 值
            Case 0 : Return "RGB (GBR)"
            Case 1 : Return "BT.709"
            Case 4 : Return "FCC"
            Case 5 : Return "BT.601 625 (BT.470 BG)"
            Case 6 : Return "BT.601 525 (SMPTE 170M)"
            Case 7 : Return "SMPTE 240M"
            Case 8 : Return "YCgCo"
            Case 9 : Return "BT.2020 NCL"
            Case 10 : Return "BT.2020 CL"
            Case 11 : Return "SMPTE 2085 (Y'D'zD'x)"
            Case 12 : Return "Chroma-derived NCL"
            Case 13 : Return "Chroma-derived CL"
            Case 14 : Return "ICtCp (BT.2100)"
            Case 15 : Return "IPT-C2 (SMPTE ST 2128)"
            Case 16 : Return "YCgCo-R (even)"
            Case 17 : Return "YCgCo-R (odd)"
            Case Else : Return String.Empty
        End Select
    End Function

    ''' <summary>色彩范围（FFmpeg AVColorRange）。</summary>
    Public Function 色彩范围(值 As Integer) As String
        Select Case 值
            Case 1 : Return "Limited"
            Case 2 : Return "Full"
            Case Else : Return String.Empty
        End Select
    End Function

End Module
