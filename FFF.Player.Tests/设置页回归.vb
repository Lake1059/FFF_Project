Imports System.IO
Imports System.Drawing
Imports System.Windows.Forms
Imports FFF.Player

Friend Module 设置页回归
    Friend Function 运行() As Integer
        Dim 原配置 = 设置.实例对象
        Try
            设置.实例对象 = New 设置()
            测导航与播放行为()
            测设备与字幕()
            测AI字幕布局与进度()
            测AI字幕渲染()
            测ASS回退()
            Console.WriteLine("设置页回归通过。")
            Return 0
        Catch ex As Exception
            Console.Error.WriteLine(ex)
            Return 1
        Finally
            设置.实例对象 = 原配置
        End Try
    End Function

    Private Sub 测导航与播放行为()
        Using 窗口 As New Form设置()
            Dim 页面 = 窗口.ModernTabListControl1.Items.Where(Function(项) Not 项.IsSeparator).
                ToDictionary(Function(项) 项.Text, Function(项) 项.BoundControl)
            断言(TypeOf 页面("播放行为") Is Form设置_播放行为, "播放行为绑定到同名导航")
            断言(TypeOf 页面("HDR") Is Form设置_HDR, "HDR 导航不串页")
            断言(TypeOf 页面("字幕") Is Form设置_字幕, "字幕导航不串页")
            断言(TypeOf 页面("AI 字幕") Is Form设置_AI字幕, "AI 字幕绑定到同名导航")
            Dim 行为 = DirectCast(页面("播放行为"), Form设置_播放行为)
            设置.实例对象.单独打开视频尺寸 = 视频打开尺寸策略.内容原始尺寸
            设置.实例对象.播放列表打开视频尺寸 = 视频打开尺寸策略.保持当前尺寸
            行为.初始化页面()
            With 行为.MCB_单独打开视频时自动将渲染大小调整为
                断言(.SelectedIndex = 3 AndAlso .Items.Count = 5, "单独打开视频的尺寸回显")
                .SelectedIndex = 4
            End With
            With 行为.MCB_播放列表打开视频时自动将渲染大小调整为
                断言(.SelectedIndex = 1 AndAlso .Items.Count = 5, "列表打开视频的尺寸回显")
                .SelectedIndex = 2
            End With
            断言(设置.实例对象.单独打开视频尺寸 = 视频打开尺寸策略.最大化 AndAlso
                 设置.实例对象.播放列表打开视频尺寸 = 视频打开尺寸策略.初始画面尺寸,
                 "两类打开来源独立写回")
        End Using
    End Sub

    Private Sub 测设备与字幕()
        Dim 配置 = 设置.实例对象
        配置.蓝牙低延迟模式 = False
        断言(Not 配置.启用音频响度标准化, "新配置默认关闭响度标准化")
        配置.蓝牙音频编码 = 3
        配置.蓝牙额外提前量毫秒 = 73
        断言(配置.音频目标响度 = -18, "新配置默认目标响度为 -18 LUFS")
        配置.音频动态范围 = 9
        配置.ASS替代字体 = "Courier New"
        Using 容器 As New Panel With {.Size = New Drawing.Size(624, 461)},
              设备 As New Form设置_设备与性能(Function() {New GPU解码设备("test-gpu", "测试显卡", 0)}) With {
                  .TopLevel = False, .Parent = 容器, .Dock = DockStyle.Fill, .Visible = True},
              字幕 As New Form设置_字幕()
            断言(设备.MCB_目标响度.SelectedIndex = -1 AndAlso Not 设备.MCK_蓝牙延迟补偿.Checked,
                "设计器保持未选中")
            设备.初始化页面()
            断言(设备.MCB_目标响度.SelectedItem = "-18" AndAlso Not 设备.MCK_音频响度标准化.Checked, "设备页回显默认响度和关闭状态")
            设备.MCB_视频渲染引擎API.SelectedIndex = 1
            断言(配置.视频渲染引擎 = 播放器渲染引擎.Direct3D12, "播放器 DX12 选项写入设置")
            Dim 端点 As New 蓝牙音频信息 With {.是蓝牙 = True, .已连接 = True, .设备名称 = "耳机 <测试>", .协议名称 = "A2DP"}
            设备.刷新蓝牙设备显示(端点)
            断言(设备.HCL_蓝牙延迟补偿说明和显示.Text.EndsWith("当前蓝牙设备：耳机 &lt;测试&gt; | 当前编码协议：A2DP"), "设备页填入当前端点资料")
            配置.音频目标响度 = -23
            设备.初始化页面()
            断言(设备.MCB_GPU视频解码设备.Items.Count = 2, "GPU 刷新不重复添加固定项")
            设备.MCB_GPU视频解码设备.SelectedIndex = 1
            断言(配置.GPU解码设备标识 = "test-gpu", "GPU 写入持久标识")
            设备.MCB_音频缓冲区策略.SelectedIndex = 2
            设备.MCB_音频直通.SelectedIndex = 2
            断言(配置.音频缓冲区策略 = 2 AndAlso 配置.音频直通选项 = 2, "音频缓冲与直通绑定")
            断言(设备.MCB_蓝牙编码.SelectedIndex = 3 AndAlso 设备.ETB_蓝牙额外延迟补偿.Value = 73,
                 "设计器蓝牙控件回显")
            设备.MCK_蓝牙延迟补偿.Checked = True
            设备.MCB_蓝牙编码.SelectedIndex = 9
            设备.ETB_蓝牙额外延迟补偿.Value = -100
            断言(配置.蓝牙低延迟模式 AndAlso 配置.蓝牙音频编码 = 9 AndAlso 配置.蓝牙额外提前量毫秒 = -100,
                 "蓝牙控件写回")
            断言(播放器控制器.生成蓝牙补偿信息行(配置, 端点).EndsWith("手动补偿 -100 ms"), "非零手动补偿写入信息层")
            配置.蓝牙额外提前量毫秒 = 0
            断言(播放器控制器.生成蓝牙补偿信息行(配置, 端点) = "耳机 <测试>   A2DP", "零值只显示设备与协议")
            断言(设备.MCB_目标响度.SelectedItem = "-23" AndAlso 设备.MCB_动态范围.SelectedIndex = 8,
                 "目标响度和动态范围回显")
            设备.MCK_音频响度标准化.Checked = True
            设备.MCB_目标响度.SelectedIndex = 0
            设备.MCB_动态范围.SelectedIndex = 0
            断言(配置.启用音频响度标准化 AndAlso 配置.音频目标响度 = -12 AndAlso 配置.音频动态范围 = 1,
                 "响度参数写回")
            字幕.初始化页面()
            字幕.MCB_尺寸缩放方式.SelectedIndex = 2
            断言(配置.字幕尺寸缩放方式 = 2, "字幕尺寸缩放事件已绑定")
            For Each 宽度 In {624, 841, 842, 624}
                容器.Width = 宽度
                断言等宽行(设备.MCB_视频渲染引擎API, 设备.JustEmptyControl1, 设备.MCB_LakeUI渲染引擎API, "渲染引擎")
                断言等宽行(设备.MCB_视频缓冲区策略, 设备.JustEmptyControl2, 设备.MCB_音频缓冲区策略, "缓冲区策略")
                断言(设备.MCB_视频缩放质量.Width = 设备.MCB_视频缓冲区策略.Width, "视频缩放质量与视频缓冲区策略等宽")
                断言等宽行(设备.MCB_音频效果, 设备.JustEmptyControl6, 设备.MCB_音频直通, "音频效果与直通")
                断言等宽行(设备.MCB_目标响度, 设备.JustEmptyControl5, 设备.MCB_动态范围, "响度标准化")
            Next
        End Using
    End Sub

    Private Sub 测AI字幕布局与进度()
        Using 容器 As New Panel With {.Size = New Drawing.Size(624, 461)},
              页面 As New Form设置_AI字幕 With {.TopLevel = False, .Parent = 容器, .Dock = DockStyle.Fill, .Visible = True}
            Dim 使用说明 = 页面.HCL_使用说明.Text
            页面.初始化页面()
            断言(页面.MP_高级下拉.Visible AndAlso 页面.MP_高级时间.Visible, "打开页面时高级设置直接显示")
            断言(Not 页面.MP_下载进度条的容器.Visible, "空闲时不占用进度容器")
            页面.更新下载进度(64, "正在下载模型", 512 * 1024)
            断言(页面.EPB_下载.Value = 64 AndAlso 页面.EPB_下载.Text = "64%　正在下载模型　512.0 KB/s" AndAlso
                页面.MP_下载进度条的容器.Visible, "下载进度显示 KB/s")
            页面.更新下载进度(72, "正在下载模型", 2.5 * 1048576)
            断言(页面.EPB_下载.Text.EndsWith("2.5 MB/s"), "下载速度自动切换到 MB/s")
            页面.更新下载进度(100, "下载完成")
            断言(页面.EPB_下载.Text = "100%　下载完成" AndAlso 页面.EPB_下载.Visible, "完成状态留在进度条上")
            For Each 宽度 In {624, 841, 842, 624}
                容器.Width = 宽度
                断言有间距(页面.MTB_包目录, 页面.JEC_目录间距, 页面.MB_浏览, "目录与浏览")
                断言(页面.MB_浏览.Right = 页面.MP_目录.ClientSize.Width, "浏览按钮贴齐行右侧")
                断言(页面.MB_下载包.Visible <> 页面.MB_删除选中包.Visible, "按所选包状态显示操作")
                If 页面.MB_下载包.Visible Then
                    断言有间距(页面.MCB_选择推理包, 页面.JEC_下载间距, 页面.MB_下载包, "包类型与下载")
                Else
                    断言有间距(页面.MCB_选择推理包, 页面.JEC_删除间距, 页面.MB_删除选中包, "包类型与删除")
                End If
                断言等宽行(页面.MCB_推理设备, 页面.JEC_推理间距, 页面.MCB_推理后端, "设备与后端")
                断言等宽行(页面.MCB_字幕语言, 页面.JEC_语言间距, 页面.MCB_自动翻译为中文, "字幕与中文翻译")
                断言等宽行(页面.MCB_节奏预置, 页面.JEC_高级下拉间距, 页面.MCB_推理线程, "预置与线程")
                断言高级时间布局(页面)
            Next
            页面.MCB_推理设备.SelectedIndex = 2
            断言(页面.MCB_推理后端.Items.Count > 1, "设备变化后可选择兼容后端")

            Dim 配置 = 设置.实例对象
            页面.MCB_节奏预置.SelectedIndex = 1
            页面.MCB_推理线程.SelectedIndex = 2
            断言(配置.Ai字幕窗口秒覆盖 = 8 AndAlso 配置.Ai字幕步长秒覆盖 = 6 AndAlso 配置.Ai字幕线程数 = 6,
                 "无需展开即可修改节奏与线程数")
            Dim 原包目录 = 配置.Ai字幕包目录
            Try
                配置.Ai字幕包目录 = Path.Combine(Path.GetTempPath(), Guid.NewGuid().ToString("N"))
                页面.初始化页面()
                断言(页面.MP_高级下拉.Visible AndAlso 页面.MP_高级时间.Visible AndAlso
                     页面.MCB_节奏预置.SelectedIndex = 1 AndAlso 页面.MCB_推理线程.SelectedIndex = 2,
                     "重新初始化时高级设置仍显示并回显当前值")
                断言(页面.EPB_下载.Visible AndAlso 页面.EPB_下载.Text.Contains("无法使用扩展包"), "无效目录在进度条提示")
                断言(页面.HCL_使用说明.Text = 使用说明, "包与推理设置变化时使用说明保持固定")
            Finally
                配置.Ai字幕包目录 = 原包目录
            End Try
        End Using
    End Sub

    Private Sub 断言有间距(左侧 As Control, 间距 As LakeUI.JustEmptyControl, 右侧 As Control, 说明 As String)
        断言(左侧.Width > 0 AndAlso 间距.Width > 0 AndAlso 右侧.Width > 0 AndAlso
             左侧.Right = 间距.Left AndAlso 间距.Right = 右侧.Left, 说明 & "按停靠顺序保留专用间距")
    End Sub

    Private Sub 断言等宽行(左侧 As Control, 间距 As LakeUI.JustEmptyControl, 右侧 As Control, 说明 As String)
        断言有间距(左侧, 间距, 右侧, 说明)
        断言(Math.Abs(左侧.Width - 右侧.Width) <= 1, 说明 & "两列宽度随行宽等分")
        断言(右侧.Right = 右侧.Parent.ClientSize.Width - 右侧.Parent.Padding.Right, 说明 & "右列填满剩余宽度")
    End Sub

    Private Sub 断言高级时间布局(页面 As Form设置_AI字幕)
        断言有间距(页面.MP_窗口列, 页面.JEC_窗口步长间距, 页面.MP_步长列, "窗口与步长")
        断言有间距(页面.MP_步长列, 页面.JEC_步长偏移间距, 页面.MP_偏移列, "步长与偏移")
        断言有间距(页面.MP_偏移列, 页面.JEC_模型档位间距, 页面.ModernPanel2, "偏移与模型档")
        Dim 列宽 = {页面.MTB_窗口秒.Width, 页面.MTB_步长秒.Width, 页面.MTB_偏移.Width, 页面.MCB_模型档.Width}
        断言(列宽.Max() - 列宽.Min() <= 1 AndAlso 页面.ModernPanel2.Right = 页面.MP_高级时间.ClientSize.Width,
             $"时间与模型档四列均分剩余宽度 [{String.Join(",", 列宽)}] {页面.ModernPanel2.Right}/{页面.MP_高级时间.ClientSize.Width}")
    End Sub

    Private Sub 测AI字幕渲染()
        Dim 清单 = Ai字幕包下载器.组运行时清单(Ai字幕包下载器.包类型.GpuCpu)
        断言(清单.Any(Function(项) 项.相对路径 = "python-embed.zip") AndAlso
             清单.Any(Function(项) 项.相对路径 = "numpy.whl"), "GGUF 官方回退源包含 Python 和 NumPy")
        断言(Ai字幕双语.规整("en-zh") = Ai字幕双语.译文优先 AndAlso
             Ai字幕双语.规整("zh-en") = Ai字幕双语.译文优先, "旧双语偏好仍然开启翻译")
        Dim 旧英文结果 = Ai字幕双语.排列结果("你好", "Hello", "English", False)
        Dim 旧中文结果 = Ai字幕双语.排列结果("你好", "Hello", "Chinese", False)
        Dim 新结果 = Ai字幕双语.排列结果("Hello", "你好", "Chinese", True)
        断言(旧英文结果.上行 = "你好" AndAlso 旧中文结果.上行 = "Hello" AndAlso 新结果.上行 = "Hello",
             "新旧 worker 均按识别来源将译文置顶")
        Dim 单语结果 = Ai字幕双语.排列结果("Hello", "", "English", True)
        断言(单语结果.上行 = "Hello" AndAlso 单语结果.下行 = "", "翻译失败时保留原文")

        Const 译文 As String = "你好，🎬"
        Const 原文 As String = "Hello."
        Dim 画布 As New Size(640, 480)
        Dim 文字样式 As New SRT字幕样式 With {
            .第一行字体 = "Segoe UI", .第二行字体 = "Arial", .第一行字号 = 36.0F, .第二行字号 = 24.0F,
            .第一行字体样式 = FontStyle.Bold, .第一行颜色ARGB = &H80FFFF00UI, .第二行颜色ARGB = &HFF00FFFFUI,
            .尺寸缩放方式 = 3, .底部对齐方式 = 1, .底部边距 = 17, .行间距 = 8
        }
        Dim 实际 As 定时文字命令() = Nothing
        Using 画面 As New 播放器画面控件(),
              实时 As New 播放器字幕叠加呈现器(画面, Function() Nothing, Function() Nothing,
                  Sub(尺寸, 命令, 序号, 帧率) 实际 = 命令.ToArray(), Function(位置, 代次) (译文, 原文, 0L))
            实时.应用SRT字幕样式(文字样式)
            实时.提交当前帧(画布, 640UI, 360UI, TimeSpan.FromSeconds(1), Nothing)
            断言(实际 IsNot Nothing AndAlso 实际.Length = 2, "Unicode 双语字幕可提交图层")
            断言(实际(0).文本 = 译文 AndAlso 实际(1).文本 = 原文 AndAlso 实际(0).Y < 实际(1).Y,
                 "译文绘制在原文上方")
            Dim 片尾命令 = 实时.生成命令(画布, 640UI, 360UI, TimeSpan.FromSeconds(1), Nothing, 显示AI字幕:=False)
            断言(片尾命令.Count = 0, "媒体结束时移除 AI 字幕，即使源仍能返回字幕")
            Dim srt = SRT字幕解析器.解析(New StringReader($"1{vbLf}00:00:00,000 --> 00:00:02,000{vbLf}{译文}{vbLf}{原文}"))
            Dim 条目数 = srt.提示.Count
            断言(条目数 = 1, "构建等价的外挂 SRT")
            Using 外挂 As New 外部字幕轨道("test.srt", 外部字幕格式.SRT, New SRT字幕帧生成器(srt, 文字样式), Nothing),
                  普通 As New 播放器字幕叠加呈现器(画面, Function() Nothing, Function() Nothing, Sub(尺寸, 命令, 序号, 帧率) Exit Sub)
                Dim 预期 = 普通.生成命令(画布, 640UI, 360UI, TimeSpan.FromSeconds(1), 外挂)
                For i = 0 To 1
                    断言(实际(i).字体 = 预期(i).字体 AndAlso 实际(i).字号 = 预期(i).字号 AndAlso
                         实际(i).样式 = 预期(i).样式 AndAlso 实际(i).前景色ARGB = 预期(i).前景色ARGB AndAlso
                         实际(i).描边宽度 = 预期(i).描边宽度 AndAlso 实际(i).阴影色ARGB = 预期(i).阴影色ARGB AndAlso
                         实际(i).Y = 预期(i).Y, $"实时字幕第 {i + 1} 行复用 SRT 样式与位置")
                Next
                Dim 切轨命令 = 实时.生成命令(画布, 640UI, 360UI, TimeSpan.FromSeconds(1), 外挂)
                断言(切轨命令.Count = 预期.Count, "选择普通轨后不与尚未清空的 AI 结果叠画")
            End Using
        End Using
    End Sub

    Private Sub 测ASS回退()
        断言(播放器原生接口.支持ASS替代字体 AndAlso 播放器原生接口.支持音频响度设置, "新原生导出可用")
        Using 会话 As New 播放器会话(New 播放器配置 With {.解码器 = 解码模式.CPU})
            断言(播放器原生接口.FFF3FP_SetAudioLoudness(会话.取得句柄(), True, -24, 10) = 原生播放器结果.成功,
                 "原生接口接受最低目标响度")
            断言(播放器原生接口.FFF3FP_SetAudioLoudness(会话.取得句柄(), True, -12, 1) = 原生播放器结果.成功,
                 "原生接口接受最高目标响度 -12 LUFS")
            断言(播放器原生接口.FFF3FP_SetAudioLoudness(会话.取得句柄(), True, -11, 1) <> 原生播放器结果.成功,
                 "原生接口拒绝高于上限的目标响度")
            断言(播放器原生接口.FFF3FP_SetAudioLoudness(会话.取得句柄(), True, -25, 10) <> 原生播放器结果.成功,
                 "原生接口拒绝越界响度")
        End Using
        Dim 路径 = Path.GetTempFileName()
        Try
            For Each 字体 In {"Arial", "3FP Uninstalled Regression Font"}
                File.WriteAllText(路径, $"[Script Info]
ScriptType: v4.00+
PlayResX: 640
PlayResY: 360
[V4+ Styles]
Format: Name, Fontname, Fontsize, PrimaryColour, SecondaryColour, OutlineColour, BackColour, Bold, Italic, Underline, StrikeOut, ScaleX, ScaleY, Spacing, Angle, BorderStyle, Outline, Shadow, Alignment, MarginL, MarginR, MarginV, Encoding
Style: Default,{字体},48,&H00FFFFFF,&H0000FFFF,&H00000000,&H00000000,0,0,0,0,100,100,0,0,1,1,0,2,10,10,10,1
[Events]
Format: Layer, Start, End, Style, Name, MarginL, MarginR, MarginV, Effect, Text
Dialogue: 0,0:00:00.00,0:00:05.00,Default,,0,0,0,,iiiWWW 12345
")
                设置.实例对象.ASS替代字体 = "Arial"
                Using 生成器 As New ASS特效字幕帧生成器(路径, 路径)
                    Dim 切换前 = 生成器.生成帧(TimeSpan.FromSeconds(1), 640, 360).像素BGRA.ToArray()
                    设置.实例对象.ASS替代字体 = "Courier New"
                    Dim 切换后 = 生成器.生成帧(TimeSpan.FromSeconds(1), 640, 360).像素BGRA
                    断言(切换前.SequenceEqual(切换后) = (字体 = "Arial"), "ASS 字体回退只影响缺失字体，且同一时间点即时生效")
                End Using
            Next
        Finally
            File.Delete(路径)
        End Try
    End Sub

    Private Sub 断言(条件 As Boolean, 说明 As String)
        If Not 条件 Then Throw New InvalidOperationException(说明)
    End Sub
End Module
