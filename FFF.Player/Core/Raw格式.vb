Imports System.Linq
Imports System.Runtime.InteropServices

''' <summary>
''' 相机 RAW 格式的唯一清单：文件关联、打开对话框、播放列表白名单共用同一组扩展名。
''' <para>入选口径是「内核 RawStillSource::LooksLikeRawPath 认得，且 LibRaw 能解」，
''' 不是 LibRaw 名义支持的全部相机格式。新增扩展名时必须同时改内核的扩展名表，
''' 否则双击/拖入会走到 FFmpeg 分支，表现为“打开了但没画面”。</para>
''' <para>.x3f 是条件格式：只有随包 libraw.dll 带 USE_X3FTOOLS 能力位时才列入，
''' 否则 UI/关联会承诺一个内核实际打不开的扩展名。</para>
''' </summary>
Friend NotInheritable Class Raw格式
    Private Sub New()
    End Sub

    ' 顺序按常见程度排列；筛选器字符串也按此顺序生成，设置页显示时便于核对。
    Private Shared ReadOnly 全部扩展名 As String() = {
        ".dng", ".cr2", ".cr3", ".crw",
        ".nef", ".nrw", ".arw", ".srf", ".sr2",
        ".raf", ".orf", ".rw2", ".rwl",
        ".pef", ".srw", ".3fr", ".fff", ".iiq", ".mos",
        ".mrw", ".x3f", ".gpr", ".erf",
        ".kdc", ".k25", ".dcr", ".raw", ".rwz", ".mef"
    }

    Friend Shared ReadOnly X3F可用 As Boolean = 检测X3F能力()

    Friend Shared ReadOnly 扩展名列表 As String() =
        If(X3F可用, 全部扩展名, 全部扩展名.Where(Function(x) x <> ".x3f").ToArray())

    Friend Shared ReadOnly 扩展名 As New HashSet(Of String)(扩展名列表, StringComparer.OrdinalIgnoreCase)

    Friend Shared ReadOnly 筛选器 As String =
        "相机 RAW 相关格式|" &
        String.Join(";", 扩展名列表.Select(Function(x) "*" & x).ToArray())

    <DllImport("libraw.dll", CallingConvention:=CallingConvention.Cdecl)>
    Private Shared Function libraw_capabilities() As UInteger
    End Function

    Private Shared Function 检测X3F能力() As Boolean
        Try
            ' LIBRAW_CAPS_X3FTOOLS = 1 << 4
            Return (libraw_capabilities() And &H10UI) <> 0UI
        Catch ex As Exception
            ' 开发目录可能还没复制 libraw.dll；此时按“不支持 .x3f”处理，
            ' 不把没有能力证据的格式写进关联承诺。
            System.Diagnostics.Debug.WriteLine($"LibRaw 能力探测失败：{ex.Message}")
            Return False
        End Try
    End Function

    Friend Shared Function 内容类型(扩展名 As String) As String
        Select Case 扩展名.ToLowerInvariant()
            Case ".dng" : Return "image/x-adobe-dng"
            Case ".cr2" : Return "image/x-canon-cr2"
            Case ".cr3" : Return "image/x-canon-cr3"
            Case ".crw" : Return "image/x-canon-crw"
            Case ".nef" : Return "image/x-nikon-nef"
            Case ".nrw" : Return "image/x-nikon-nrw"
            Case ".arw" : Return "image/x-sony-arw"
            Case ".srf" : Return "image/x-sony-srf"
            Case ".sr2" : Return "image/x-sony-sr2"
            Case ".raf" : Return "image/x-fujifilm-raf"
            Case ".orf" : Return "image/x-olympus-orf"
            Case ".rw2" : Return "image/x-panasonic-rw2"
            Case ".rwl" : Return "image/x-leica-rwl"
            Case ".pef" : Return "image/x-pentax-pef"
            Case ".srw" : Return "image/x-samsung-srw"
            Case ".3fr" : Return "image/x-hasselblad-3fr"
            Case ".fff" : Return "image/x-hasselblad-fff"
            Case ".iiq" : Return "image/x-phaseone-iiq"
            Case ".mos" : Return "image/x-leaf-mos"
            Case ".mrw" : Return "image/x-minolta-mrw"
            Case ".x3f" : Return "image/x-sigma-x3f"
            Case ".gpr" : Return "image/x-gopro-gpr"
            Case ".erf" : Return "image/x-epson-erf"
            Case ".kdc" : Return "image/x-kodak-kdc"
            Case ".k25" : Return "image/x-kodak-k25"
            Case ".dcr" : Return "image/x-kodak-dcr"
            Case ".rwz" : Return "image/x-raw"
            Case ".mef" : Return "image/x-mamiya-mef"
            Case Else : Return "image/x-raw"
        End Select
    End Function

    Friend Shared Function 是RAW(路径 As String) As Boolean
        Return Not String.IsNullOrWhiteSpace(路径) AndAlso 扩展名.Contains(IO.Path.GetExtension(路径))
    End Function
End Class
