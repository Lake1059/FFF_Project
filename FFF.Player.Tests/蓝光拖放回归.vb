Imports System.IO
Imports FFF.Player

Friend Module 蓝光拖放回归
    Friend Function 运行() As Integer
        Dim 临时 = Directory.CreateTempSubdirectory("fff-bluray-drop-")
        Try
            Dim 光盘根 = Directory.CreateDirectory(Path.Combine(临时.FullName, "蓝光样片.HK.BluRay")).FullName
            Dim bdmv = Directory.CreateDirectory(Path.Combine(光盘根, "BDMV")).FullName
            Dim 索引 = Path.Combine(bdmv, "index.bdmv")
            File.WriteAllText(索引, "fixture")
            Dim 普通目录 = Directory.CreateDirectory(Path.Combine(临时.FullName, "普通视频")).FullName
            Dim 媒体文件 = Path.Combine(普通目录, "clip.mkv")
            File.WriteAllText(媒体文件, "fixture")
            Dim 无索引目录 = Directory.CreateDirectory(Path.Combine(临时.FullName, "无索引", "BDMV")).Parent.FullName
            Dim 备份索引 = Directory.CreateDirectory(Path.Combine(临时.FullName, "备份索引", "BDMV", "BACKUP"))
            File.WriteAllText(Path.Combine(备份索引.FullName, "index.bdmv"), "fixture")
            Dim 备份盘根 = 备份索引.Parent.Parent.FullName

            检查(光盘路径.是光盘路径(光盘根) AndAlso 光盘路径.媒体存在(光盘根), "蓝光碟根目录")
            检查(光盘路径.取得媒体路径(光盘根 & Path.DirectorySeparatorChar) = 光盘根, "路径末尾分隔符")
            检查(光盘路径.取得媒体路径(bdmv) = 光盘根, "识别直接拖入的 BDMV 目录")
            检查(光盘路径.取得媒体路径(索引) = 光盘根, "索引文件与光盘根使用同一路径")
            检查(光盘路径.是光盘路径(备份盘根), "识别带备用索引的光盘")
            检查(Not 光盘路径.媒体存在(普通目录) AndAlso Not 光盘路径.是光盘路径(无索引目录), "普通目录和空 BDMV 不作为光盘")
            检查(Form1.取得命令行文件({普通目录, bdmv}) = 光盘根, "命令行选择有效光盘")
            检查(Form1.取得打开路径({光盘根, 普通目录, bdmv, 索引, 媒体文件}).SequenceEqual({光盘根, 媒体文件}),
               "画面拖放混合光盘、文件和别名时按原顺序保留两个媒体")

            Dim 列表 As New 播放列表()
            列表.添加多个(Form播放列表.枚举拖入媒体({光盘根, bdmv, 索引, 普通目录}))
            检查(列表.取得项目().Select(Function(x) x.路径).SequenceEqual({光盘根, 媒体文件}),
               "播放列表按一张碟添加并继续扫描普通目录")
            检查(列表.当前项目.标题 = "蓝光样片.HK.BluRay", "光盘标题保留目录全名")

            Dim 列表文件 = Path.Combine(临时.FullName, "disc.m3u8")
            列表.导出M3U8(列表文件)
            Dim 导回 As New 播放列表()
            导回.导入M3U8(列表文件)
            检查(导回.取得项目().Select(Function(x) x.路径).SequenceEqual({光盘根, 媒体文件}), "播放列表保存再加载后保留光盘项")
            Return 0
        Catch ex As Exception
            Console.Error.WriteLine(ex.Message)
            Return 1
        Finally
            临时.Delete(recursive:=True)
        End Try
    End Function

    Private Sub 检查(条件 As Boolean, 名称 As String)
        If Not 条件 Then Throw New InvalidOperationException("FAIL  " & 名称)
        Console.WriteLine("ok    " & 名称)
    End Sub
End Module
