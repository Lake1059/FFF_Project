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

    ''' <summary>应用值来源标签：声明值缺席时，内核实际用于转换的基础值来自哪一级证据。</summary>
    Public Function 应用来源标签(来源 As String) As String
        If String.IsNullOrWhiteSpace(来源) Then Return String.Empty
        Select Case 来源.Trim().ToLowerInvariant()
            Case "frame" : Return "（帧识别）"
            Case "bitstream" : Return "（码流识别）"
            Case "container" : Return "（元数据）"
            Case "inferred" : Return "（推断）"
            Case Else : Return String.Empty
        End Select
    End Function

    ''' <summary>
    ''' 显示内核实际应用的转换基础：声明值与实际应用值一致时直接用声明值；声明缺席
    ''' 或与实际应用值冲突（容器标签写错、码流/帧有更强证据）时用应用值并标注来源。
    ''' 这样信息图层/媒体信息窗体既不会整项留空，也不会把已被转换路径纠正的旧标签
    ''' 继续当作生效值展示。
    ''' </summary>
    Public Function 色彩项文本(声明值 As Integer, 应用值 As Integer, 应用来源 As String,
                              映射 As Func(Of Integer, String)) As String
        Dim 声明 = 映射(声明值)
        If 应用值 = 声明值 Then Return 声明
        Dim 应用 = 映射(应用值)
        If String.IsNullOrEmpty(应用) Then Return 声明
        Return 应用 & 应用来源标签(应用来源)
    End Function

    Public Function 色彩标准显示(声明值 As Integer, 应用值 As Integer, 应用来源 As String) As String
        Return 色彩项文本(声明值, 应用值, 应用来源, AddressOf 色彩标准)
    End Function

    Public Function 传输曲线显示(声明值 As Integer, 应用值 As Integer, 应用来源 As String) As String
        Return 色彩项文本(声明值, 应用值, 应用来源, AddressOf 传输曲线)
    End Function

    Public Function 色彩矩阵显示(声明值 As Integer, 应用值 As Integer, 应用来源 As String) As String
        Return 色彩项文本(声明值, 应用值, 应用来源, AddressOf 色彩矩阵)
    End Function

    Public Function 色彩范围显示(声明值 As Integer, 应用值 As Integer, 应用来源 As String) As String
        Return 色彩项文本(声明值, 应用值, 应用来源, AddressOf 色彩范围)
    End Function

End Module
