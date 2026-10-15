Friend Module Program
    ' ⚠ 本文件此前调用了四个**树里不存在**的例程（Assert / CheckPlaylist /
    ' CheckSubtitleBuffers / CheckPlayback），且用了 FFF.Player 里的 Friend 类型
    ' （播放器会话 / 播放器配置 / 播放状态 —— 跨程序集不可见），所以整个工程
    ' **在我动手之前就编不过**（BC30002 ×2 + BC30451 ×6，编译器实测）。这正是
    ' docs/78 §1 记下的那笔账：#17 提交时 FFF.Player.Tests 不可构建。
    ' 这里按同一口径收口：只留树里真实存在的入口，缺失的那些**不复活、不假编**。
    ' 需要恢复"会话级"检查的话，前提是给 FFF.Player 加 InternalsVisibleTo 并
    ' 把那四个例程重新落进本工程 —— 那是另一件事，不在本轮范围。
    <STAThread>
    Public Function Main(args As String()) As Integer
        Try
            If args.Length > 0 AndAlso String.Equals(args(0), "--lyrics-kit-regression",
                    StringComparison.OrdinalIgnoreCase) Then
                Return LyricsKitRegression.运行(args.Skip(1).ToArray())
            End If
            If args.Length > 0 AndAlso String.Equals(args(0), "--image-zoom-regression",
                    StringComparison.OrdinalIgnoreCase) Then
                Return ImageZoomRegression.运行(args.Skip(1).ToArray())
            End If
            If args.Length > 0 AndAlso String.Equals(args(0), "--stream-selection-regression",
                    StringComparison.OrdinalIgnoreCase) Then
                Return StreamSelectionRegression.运行(args.Skip(1).ToArray())
            End If
            ' 体感那一路要真计时器 + 真消息循环才观测得到，所以另开一档（同进程，不起产品 exe）。
            If args.Length > 0 AndAlso String.Equals(args(0), "--image-zoom-live",
                    StringComparison.OrdinalIgnoreCase) Then
                Return ImageZoomLiveRegression.运行(args.Skip(1).ToArray())
            End If
            ' ⚠ 认不出的旗标**必须判红**，不许再往下兜底（这就是本文件最后一处落笔的牙）。
            ' 为什么：出货树那份 Program.vb（5713 行注册表，含 --static-image-open-regression）里有
            ' 23 个 --*-regression 旗标，而本树只实现 --lyrics-kit-regression 一个。此前任何未知旗标
            ' 都会一路落到下面的 ImageZoomRegression 并返回它的 0 —— 于是驱动脚本拿到 23 个 PASS，
            ' 把"这个测试在本树根本不存在"洗成绿灯，比失败更坏（静默兜底 = 伪证生成器）。
            ' 现在：点名旗标 + 说明"本树未实现" + exit 3。3 是本工程新占的码，专职"入口不存在"，
            ' 与 0（通过）/ 1（断言失败）/ 2（参数或前置条件不对）都区分得开，驱动侧一眼能分类。
            ' 注：--image-zoom-regression 与 --image-zoom-live 两档的分派**保留不动** —— 经核实
            ' ImageZoomRegression.vb / ImageZoomLiveRegression.vb 确在本工程目录内（Friend Module +
            ' 运行(参数 As String())，且符号已编进 bin/Debug 的 FFF.Player.Tests.dll），不是上一段
            ' 注释里那种"树里不存在的例程"。只是提醒：这两个文件目前未被 git 跟踪（本目录只跟踪
            ' Program.vb / LyricsKitRegression.vb / .vbproj），干净检出会编不过 —— 那是入账事项，
            ' 不在本轮范围。动图那一路（计划中的 --animated-image-open-regression）另案，本轮不新增
            ' 测试，只把"缺席"做成可见。
            If args.Length > 0 Then
                Console.WriteLine($"入口未实现：{args(0)} —— 本树（上游 PR 工作区 3fp/kernel）没有这个入口，本次未运行任何回归。")
                Console.WriteLine("本树已实现的入口：--lyrics-kit-regression [kit目录] | --image-zoom-regression [参数] | --image-zoom-live [参数] | --stream-selection-regression [样本目录]")
                Console.Error.WriteLine($"UNIMPLEMENTED IN THIS TREE: {args(0)} => exit 3 (no regression was run)")
                Return 3
            End If
            ' 无参数时跑**不依赖媒体与窗口**的那一档：图片缩放几何是纯函数，
            ' 同进程就能判；比"打印用法就退出"多给一条真绿灯。
            Console.WriteLine("未指定入口，运行图片缩放几何回归（纯函数，无媒体依赖）。")
            Console.WriteLine("可用入口：--lyrics-kit-regression [kit目录] | --image-zoom-regression [参数]")
            Return ImageZoomRegression.运行(Array.Empty(Of String)())
        Catch ex As Exception
            Console.Error.WriteLine(ex)
            Return 1
        End Try
    End Function
End Module
