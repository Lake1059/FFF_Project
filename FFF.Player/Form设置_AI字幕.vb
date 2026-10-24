Imports LakeUI

''' <summary>
''' 设置页 → AI 字幕（docs/ai-subtitles/）。
'''
''' 本体不内置任何 ASR 能力，只认"扩展包"这一个契约：一个含 ai-subtitles.json 的
''' 目录，包自己说清 worker / python / 模型 / llama-server 在哪、用什么量化档。
''' 于是这一页只需要一行"选哪个目录"——选到有效包即启用，包没了就自动停用。
''' 窗口/步长/设备序号/模型档/GPU 层数/驻留偏移/附加参数在"高级选项"里（默认展开，
''' 可随手折回去），留空表示跟随包清单（默认）。GPU 层数是下拉，预置档按包清单
''' 声明的 layers（模型层数）生成——不同架构的模型不再是写死的 28 层；包没声明时
''' 沿用旧的 28 层表。模型档位取自包清单的 models 表：没配包或包里只有一个档时
''' 下拉置灰，水印会说明原因。
'''
''' 按模型能力开放的几处（条件不满足时置灰而非藏起来——藏起来用户会以为这项丢了）：
'''   · 模型档：清单 models 表有多档才可点；没配包 / 包里只有一档时原因作为可见项；
'''   · GPU 层数：选 CPU 后端时置灰（设备是 none，无从卸起）；预置档随包 layers 变，
'''     包没声明 layers 时用默认 28 层表——下拉照样可用，不是置灰；
'''   · 附加参数框：选了有效包才可编辑——那些 flag 是给这个包的 llama-server 的。
'''
''' 路径文本框走 Leave 而不是 TextChanged：半途路径写进设置会在下次打开媒体时
''' 被当成有效配置去启 worker，滋一脸错误提示。数值类同理，解析失败就不写。
''' </summary>
Public Class Form设置_AI字幕
    Private 正在初始化 As Boolean

    ' GPU 层数下拉项：显示 ↔ 值（--ngl；负数 = 不传，llama-server 自行全卸）。
    ' 表按包清单声明的 layers（模型层数）动态生成，见 重建层数下拉 —— 同一个
    ' 设置页要适配不同架构的模型，不能把某一层的层数写死。包没声明 layers 时
    ' 用 _默认层数（= 旧固定表的 28 层基准），老包行为逐字节不变。
    Private Const 默认层数 As Integer = 28
    Private _层数显示表 As New List(Of String)
    Private _层数值表 As New List(Of Integer)
    Private _层数表签名 As String = String.Empty
    Private _生效层数 As Integer = 默认层数

    ' 窗口/步长预置：显示 ↔（窗口秒, 步长秒）。索引 0 = 跟随包清单（两个覆盖都
    ' 写 0）；最后一项"自定义"没有自己的数值（见 MCB_预置_SelectedIndexChanged）。
    ' 预置存在的理由：批式 ASR 的贴合度基本由"步长 + 推理耗时"决定，手填两个框
    ' 对普通用户是黑话；灵敏档把滞后压到 1s 量级，完整档给长句更多上下文。
    Private Shared ReadOnly _预置显示 As String() = {
        "跟随包清单", "灵敏（窗 2s · 步长 1s）", "均衡（窗 5s · 步长 3s）",
        "完整（窗 8s · 步长 4s）", "自定义"}
    Private Shared ReadOnly _预置窗口 As Double() = {0, 2, 5, 8, 0}
    Private Shared ReadOnly _预置步长 As Double() = {0, 1, 3, 4, 0}

    Public Sub New()
        正在初始化 = True
        Try
            InitializeComponent()
            ' 下拉项只在构造时填一次：初始化页面会被反复调用（每次打开设置），
            ' 在里面 Add 会越加越多。顺序与 Ai字幕后端.全部 一一对应，
            ' 于是 SelectedIndex 就是后端下标，不需要另存一份映射。
            For Each id In Ai字幕后端.全部
                MCB_设备.Items.Add(Ai字幕后端.显示名(id))
            Next
            ' 语言项只列主项 4 个（自动/中文/English/中英混说）——都经过真机验证。
            ' 其余语种模型卡上见过名字但没逐个验证，用户裁定"未验证的内容不出现"，
            ' 整表留在 Ai字幕语言.全部 里仅供防漂移对比。顺序与主项一一对应，
            ' 于是 SelectedIndex 就是语言下标，不需要另存一份映射。
            For Each id In Ai字幕语言.主项
                MCB_语言.Items.Add(Ai字幕语言.显示名(id))
            Next
            ' 预置下拉是静态表：构造时填一次（初始化页面会被反复调用，在里面 Add
            ' 会越加越多），但选中项要等设置读进来后再同步（见 同步预置下拉）。
            ' GPU 层数下拉不在这儿填：它的预置档随包清单声明的 layers 而变，
            ' 由 重建层数下拉 在 刷新状态 时按签名重建。
            For i = 0 To _预置显示.Length - 1
                MCB_预置.Items.Add(_预置显示(i))
            Next
        Finally
            正在初始化 = False
        End Try
    End Sub

    Friend Sub 初始化页面()
        正在初始化 = True
        Try
            MTB_包目录.Text = 设置.实例对象.Ai字幕包目录
            MCB_设备.SelectedIndex = 后端下标(设置.实例对象.Ai字幕设备后端)
            ' 残留的非主项语言值（老设置/手改 json，如 "ja"）在这里一次性归 auto：
            ' 只改内存、不主动落盘，用户一动语言就会把主项值写回去；空串是出厂默认
            ' （语义即 auto），不动它。行为则立刻与界面一致，不会"显示自动、锁着日语"。
            Dim 旧语言 = 设置.实例对象.Ai字幕语言
            If Not String.IsNullOrEmpty(旧语言) AndAlso Ai字幕语言.规整(旧语言) <> 旧语言 Then
                设置.实例对象.Ai字幕语言 = Ai字幕语言.规整(旧语言)
            End If
            MCB_语言.SelectedIndex = 语言下标(设置.实例对象.Ai字幕语言)
            MCK_仅所选语言.Checked = 设置.实例对象.Ai字幕仅所选语言
            刷新语言可用性()
            ' 高级项默认展开：窗口/步长/设备序号/模型档/层数/偏移/附加参数都是用户
            ' 明确想调才会来调的东西，藏一层折叠只会让人以为"这些不让改"。
            ' 想看简洁画面随手再折上去。
            MCK_显示高级.Checked = True
            MTB_窗口秒.Text = 覆盖文本(设置.实例对象.Ai字幕窗口秒覆盖)
            MTB_步长秒.Text = 覆盖文本(设置.实例对象.Ai字幕步长秒覆盖)
            MTB_设备序号.Text = 序号文本(设置.实例对象.Ai字幕设备序号)
            MTB_偏移.Text = 偏移文本(设置.实例对象.Ai字幕字幕偏移秒)
            MTB_附加参数.Text = 设置.实例对象.Ai字幕附加参数
            ' 层数归位必须发生在建表之后：预置表随包清单声明的 layers 而变
            ' （重建层数下拉 带签名缓存，重复调用不重建、只会归位）。老设置里
            ' 文本框时代存下来的任意值（含 999 这种"全部"的老写法）在这里归位：
            ' ≥ 生效层数记生效层数（llama-server 本来也会 clamp），其余归自动。
            ' 只改内存不主动落盘，与语言残留值同一套路——界面显示什么就是什么。
            Dim 归位错误 As String = String.Empty
            Dim 归位包 = Ai字幕包.载入(MTB_包目录.Text.Trim(), 归位错误)
            重建层数下拉(归位包)
            同步预置下拉()
            刷新设备可用性()
            显示高级(MCK_显示高级.Checked)
            刷新状态()
        Finally
            正在初始化 = False
        End Try
    End Sub

    Private Sub MTB_包目录_Leave(sender As Object, e As EventArgs) Handles MTB_包目录.Leave
        If 正在初始化 Then Return
        设置.实例对象.Ai字幕包目录 = MTB_包目录.Text.Trim()
        刷新状态()
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    Private Sub MB_浏览_Click(sender As Object, e As EventArgs) Handles MB_浏览.Click
        Try
            Using 对话框 As New OpenFileDialog With {
                .Title = "选择扩展包清单（ai-subtitles.json）",
                .Filter = "AI 字幕扩展包清单|ai-subtitles.json|JSON 文件|*.json|所有文件|*.*",
                .CheckFileExists = True
            }
                If 对话框.ShowDialog() <> DialogResult.OK Then Return
                ' 选的是清单文件，包目录 = 它所在的那一级。
                Dim 目录 = IO.Path.GetDirectoryName(对话框.FileName)
                If String.IsNullOrWhiteSpace(目录) Then Return
                MTB_包目录.Text = 目录
                设置.实例对象.Ai字幕包目录 = 目录
                刷新状态()
                Form1.当前主窗体?.应用Ai字幕设置()
            End Using
        Catch
            ' 浏览失败不该打断设置页。
        End Try
    End Sub

    Private Sub MCB_设备_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_设备.SelectedIndexChanged
        If 正在初始化 OrElse MCB_设备.SelectedIndex < 0 Then Return
        设置.实例对象.Ai字幕设备后端 = 后端标识(MCB_设备.SelectedIndex)
        刷新设备可用性()
        刷新状态()
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    Private Sub MTB_设备序号_Leave(sender As Object, e As EventArgs) Handles MTB_设备序号.Leave
        If 正在初始化 Then Return
        设置.实例对象.Ai字幕设备序号 = 解析序号(MTB_设备序号.Text)
        MTB_设备序号.Text = 序号文本(设置.实例对象.Ai字幕设备序号)
        刷新状态()
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    ''' <summary>GPU 层数下拉：和模型档一样是启动期参数，改完走应用Ai字幕设置
    ''' 让控制器重启 worker（-ngl 变了必须重起 llama-server）。</summary>
    Private Sub MCB_Gpu层数_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_Gpu层数.SelectedIndexChanged
        If 正在初始化 OrElse MCB_Gpu层数.SelectedIndex < 0 Then Return
        设置.实例对象.Ai字幕Gpu层数 = 层数值(MCB_Gpu层数.SelectedIndex)
        刷新状态()
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    ''' <summary>选 CPU 后端时设备是 none，层数无从卸起 —— 置灰而不是藏起来：
    ''' 藏起来用户会以为这一项丢了。</summary>
    Private Sub 刷新设备可用性()
        Dim 用显卡 = Ai字幕后端.规整(设置.实例对象.Ai字幕设备后端) <> Ai字幕后端.处理器
        MCB_Gpu层数.Enabled = 用显卡
        LBL_Gpu层数.Enabled = 用显卡
    End Sub

    Private Sub MCB_语言_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_语言.SelectedIndexChanged
        If 正在初始化 OrElse MCB_语言.SelectedIndex < 0 Then Return
        设置.实例对象.Ai字幕语言 = 语言标识(MCB_语言.SelectedIndex)
        刷新语言可用性()
        刷新状态()
        ' 语言是 worker 的启动期参数，改它必须重启管道；控制器自己比对后端口语决定。
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    Private Sub MCK_仅所选语言_CheckedChanged(sender As Object, e As EventArgs) Handles MCK_仅所选语言.CheckedChanged
        If 正在初始化 Then Return
        ' 没指定语言时这个开关没有语义，勾上也归零（语言下拉改回 auto 时同理）。
        设置.实例对象.Ai字幕仅所选语言 =
            MCK_仅所选语言.Checked AndAlso Ai字幕语言.已指定(设置.实例对象.Ai字幕语言)
        刷新状态()
        ' 闸门在宿主侧判定，worker 不用重启；那个开关不该逼人重载 1GB 模型。
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    Private Sub MCK_显示高级_CheckedChanged(sender As Object, e As EventArgs) Handles MCK_显示高级.CheckedChanged
        If 正在初始化 Then Return
        显示高级(MCK_显示高级.Checked)
    End Sub

    Private Sub MTB_窗口秒_Leave(sender As Object, e As EventArgs) Handles MTB_窗口秒.Leave
        If 正在初始化 Then Return
        设置.实例对象.Ai字幕窗口秒覆盖 = 解析覆盖(MTB_窗口秒.Text)
        MTB_窗口秒.Text = 覆盖文本(设置.实例对象.Ai字幕窗口秒覆盖)
        同步预置下拉()
        刷新状态()
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    Private Sub MTB_步长秒_Leave(sender As Object, e As EventArgs) Handles MTB_步长秒.Leave
        If 正在初始化 Then Return
        设置.实例对象.Ai字幕步长秒覆盖 = 解析覆盖(MTB_步长秒.Text)
        MTB_步长秒.Text = 覆盖文本(设置.实例对象.Ai字幕步长秒覆盖)
        同步预置下拉()
        刷新状态()
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    ''' <summary>字幕驻留偏移：走 Leave 不 TextChanged，理由同路径框。偏移是纯显示侧
    ''' （控制器同步进状态机，不重启 worker），但照样要过 应用Ai字幕设置 让控制器
    ''' 把新值同步过去——播放中改也立刻见效。</summary>
    Private Sub MTB_偏移_Leave(sender As Object, e As EventArgs) Handles MTB_偏移.Leave
        If 正在初始化 Then Return
        Dim 值 = 解析偏移(MTB_偏移.Text)
        设置.实例对象.Ai字幕字幕偏移秒 = 值
        MTB_偏移.Text = 偏移文本(值)
        刷新状态()
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    ''' <summary>用户附加参数框：Leave 时整篇落盘（不 TextChanged——半途一个 --flag
    ''' 写进设置会在下次打开媒体时重启 worker，等于每敲一个字符重载一次模型）。
    ''' 它是启动期参数：控制器比对原文，变了就重启 worker。</summary>
    Private Sub MTB_附加参数_Leave(sender As Object, e As EventArgs) Handles MTB_附加参数.Leave
        If 正在初始化 Then Return
        设置.实例对象.Ai字幕附加参数 = MTB_附加参数.Text
        刷新状态()
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    ''' <summary>窗口/步长预置：只是"快速填数值"的写入器——真存储仍是窗口/步长两个
    ''' 覆盖项（0 = 跟随包清单）。选中预置即写设置并回填两个文本框；"自定义"没有
    ''' 自己的数值，选中它什么都不写，它只表示"当前值与任何预置都对不上"。</summary>
    Private Sub MCB_预置_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_预置.SelectedIndexChanged
        If 正在初始化 OrElse MCB_预置.SelectedIndex < 0 Then Return
        If MCB_预置.SelectedIndex >= _预置显示.Length - 1 Then Return   ' 自定义：只反映现状
        设置.实例对象.Ai字幕窗口秒覆盖 = _预置窗口(MCB_预置.SelectedIndex)
        设置.实例对象.Ai字幕步长秒覆盖 = _预置步长(MCB_预置.SelectedIndex)
        MTB_窗口秒.Text = 覆盖文本(设置.实例对象.Ai字幕窗口秒覆盖)
        MTB_步长秒.Text = 覆盖文本(设置.实例对象.Ai字幕步长秒覆盖)
        刷新状态()
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    ' ---------------- 内部 ----------------

    Private Sub 显示高级(显示 As Boolean)
        LBL_窗口秒.Visible = 显示
        MTB_窗口秒.Visible = 显示
        LBL_步长秒.Visible = 显示
        MTB_步长秒.Visible = 显示
        LBL_设备序号.Visible = 显示
        MTB_设备序号.Visible = 显示
        LBL_模型档.Visible = 显示
        MCB_模型档.Visible = 显示
        LBL_Gpu层数.Visible = 显示
        MCB_Gpu层数.Visible = 显示
        LBL_偏移.Visible = 显示
        MTB_偏移.Visible = 显示
        LBL_预置.Visible = 显示
        MCB_预置.Visible = 显示
        LBL_附加参数.Visible = 显示
        MTB_附加参数.Visible = 显示
    End Sub

    ''' <summary>"只显示所选语言"没有 auto 可言：没指定语言时置灰并归零。
    ''' 归零必须写设置——否则界面上框是空的、worker 却在按旧值筛除。</summary>
    Private Sub 刷新语言可用性()
        Dim 已指定 = Ai字幕语言.已指定(设置.实例对象.Ai字幕语言)
        MCK_仅所选语言.Enabled = 已指定
        If Not 已指定 AndAlso MCK_仅所选语言.Checked Then
            正在初始化 = True
            Try
                MCK_仅所选语言.Checked = False
            Finally
                正在初始化 = False
            End Try
            设置.实例对象.Ai字幕仅所选语言 = False
        End If
    End Sub

    ''' <summary>状态行：包是否有效、实际生效的窗口/步长、有没有被高级覆盖，
    ''' 第三行是"这个包/这个模型能干什么"的能力行。</summary>
    Private Sub 刷新状态()
        Dim 目录 = MTB_包目录.Text.Trim()
        Dim 错误 As String = String.Empty
        Dim 配置 = Ai字幕包.载入(目录, 错误)
        应用附加参数可用性(配置 IsNot Nothing)
        重建层数下拉(配置)
        If 配置 Is Nothing Then
            重建模型档下拉(Nothing)
            If String.IsNullOrWhiteSpace(目录) Then
                LBL_状态.ForeColor = Color.Gray
                LBL_状态.Text = "未启用。选择一个扩展包目录（含 ai-subtitles.json）即可启用。"
            Else
                LBL_状态.ForeColor = Color.OrangeRed
                LBL_状态.Text = "未启用：" & 错误
            End If
            Return
        End If

        Dim 窗口 = 配置.窗口秒
        Dim 步长 = 配置.步长秒
        Dim 覆盖窗口 = 设置.实例对象.Ai字幕窗口秒覆盖
        Dim 覆盖步长 = 设置.实例对象.Ai字幕步长秒覆盖
        If 覆盖窗口 > 0 Then 窗口 = 覆盖窗口
        If 覆盖步长 > 0 Then 步长 = Math.Min(覆盖步长, 窗口)

        ' 用户选 auto 时跟随包清单的 device；包也没写就还是 auto，由 worker 那边
        ' 问 llama-server 的 --list-devices 自己决定。
        Dim 后端 = Ai字幕后端.规整(设置.实例对象.Ai字幕设备后端)
        If 后端 = Ai字幕后端.自动 Then 后端 = Ai字幕后端.规整(配置.推荐后端)
        Dim 命中 As Boolean = False
        配置.取服务器(后端, 命中)
        Dim 序号 = 设置.实例对象.Ai字幕设备序号
        Dim 设备说明 = Ai字幕后端.显示名(后端)
        If 序号 >= 0 Then 设备说明 &= $"（第 {序号} 块）"
        ' 换后端 = 换二进制。纯 CPU 是唯一的例外：任何 llama-server 都自带 CPU
        ' 后端，回落到包默认的二进制照样能跑（-ngl 0），所以不提示。
        Dim 缺二进制 = (后端 <> Ai字幕后端.自动 AndAlso 后端 <> Ai字幕后端.处理器 AndAlso Not 命中)

        LBL_状态.ForeColor = If(缺二进制, Color.Orange, Color.OliveDrab)
        Dim 后缀 = If(覆盖窗口 > 0 OrElse 覆盖步长 > 0, "（高级覆盖生效）", "")
        Dim 语言id = Ai字幕语言.规整(设置.实例对象.Ai字幕语言)
        Dim 语言说明 = Ai字幕语言.显示名(语言id)
        If Ai字幕语言.已指定(语言id) Then
            语言说明 &= If(设置.实例对象.Ai字幕仅所选语言, "（只显示该语言）", "（提示，不筛除）")
        End If
        Dim 档命中 As Boolean
        Dim 档名 = "默认档"
        If Not String.IsNullOrWhiteSpace(设置.实例对象.Ai字幕模型档) Then
            Dim 档 = 配置.取模型档(设置.实例对象.Ai字幕模型档, 档命中)
            If 档命中 Then 档名 = 档.档名 Else 档名 = $"默认档（{设置.实例对象.Ai字幕模型档} 不在包内）"
        End If
        重建模型档下拉(配置)
        ' 层数只在用户显式设过时进状态行：默认"全部卸载"没什么可说的，
        ' 而无条件拼上去会把状态行撑爆（568px 放不下"设备+模型+语言+层数"全套）。
        Dim 层数 = 设置.实例对象.Ai字幕Gpu层数
        Dim 层数说明 = If(层数 >= 0, $"　层数：{层数}", "")
        ' 能力行：模型形态（有没有投影权重）、层数表从哪来、附加参数两个来源各几条。
        ' 单文件模型不是错误——llama-server 能加载它就能跑；但值得让人看见，
        ' 因为"双文件包塞个单文件模型"正是换模型时最常见的踩坑。
        Dim 形态说明 = If(String.IsNullOrWhiteSpace(配置.投影文件),
                          "单文件模型（无投影权重）",
                          "双文件（LLM + mmproj）")
        Dim 层数表说明 = If(配置.模型层数 > 0,
                            $"包声明 {配置.模型层数} 层",
                            $"默认 {默认层数} 层（包未声明 layers）")
        Dim 包参数条数 = 配置.额外参数.Count
        Dim 用户参数条数 = Ai字幕管道.解析附加参数(设置.实例对象.Ai字幕附加参数).Count
        Dim 参数说明 As String
        If 包参数条数 = 0 AndAlso 用户参数条数 = 0 Then
            参数说明 = "无"
        ElseIf 用户参数条数 = 0 Then
            参数说明 = $"包 {包参数条数} 条"
        Else
            参数说明 = $"包 {包参数条数} 条 + 用户 {用户参数条数} 条"
        End If
        LBL_状态.Text = $"已启用：{配置.名称} —— 窗口 {窗口:0.###}s / 步长 {步长:0.###}s{后缀}" & vbCrLf &
                        $"推理设备：{设备说明}　模型：{档名}　字幕语言：{语言说明}{层数说明}" & vbCrLf &
                        $"模型形态：{形态说明}　GPU 层数表：{层数表说明}　附加参数：{参数说明}"
        If 缺二进制 Then
            LBL_状态.Text &= vbCrLf & "注意：包内没有该后端专用的二进制，会回落到包默认的那个。"
        End If
    End Sub

    ''' <summary>附加参数框按"有没有有效包"开放：那些 flag 是给这个包的 llama-server
    ''' 的，没包时置灰并把水印换成原因（可见项口径，与模型档下拉一致——藏起来或
    ''' 只靠水印说话，用户都看不到"为什么不能填"）。</summary>
    Private Sub 应用附加参数可用性(有效 As Boolean)
        MTB_附加参数.Enabled = 有效
        LBL_附加参数.Enabled = 有效
        MTB_附加参数.WaterText = If(有效,
            "每行一个参数，如 --ctx-size 换行 8192",
            "需先选择有效扩展包（含 ai-subtitles.json）")
    End Sub

    ''' <summary>下拉框下标 ← → 后端标识。项是在构造时按 Ai字幕后端.全部 的顺序填的。</summary>
    Private Shared Function 后端下标(值 As String) As Integer
        Dim id = Ai字幕后端.规整(值)
        For i = 0 To Ai字幕后端.全部.Length - 1
            If String.Equals(Ai字幕后端.全部(i), id, StringComparison.Ordinal) Then Return i
        Next
        Return 0
    End Function

    Private Shared Function 后端标识(下标 As Integer) As String
        If 下标 < 0 OrElse 下标 >= Ai字幕后端.全部.Length Then Return Ai字幕后端.自动
        Return Ai字幕后端.全部(下标)
    End Function

    ' ---------------- 模型档位 ----------------
    '
    ' 档位来自包清单的 models（可为空）。低配档的意义：同一个包能在弱机上跑 ——
    ' 换包对普通用户成本太高，换一档权重只需重启 worker。
    Private Const 默认档显示 As String = "默认档（清单 model 字段）"
    Private _档名表 As New List(Of String) From {String.Empty}
    Private _档表签名 As String = String.Empty

    ''' <summary>按当前包重建下拉。列表没变就什么都不做 —— 否则每次刷新状态都会
    ''' 清空重建，用户正打开的下拉会被抽走。</summary>
    Private Sub 重建模型档下拉(配置 As Ai字幕包配置)
        Dim 新 As New List(Of String) From {String.Empty}
        If 配置 IsNot Nothing Then
            For Each 档 In 配置.模型档表
                新.Add(档.档名)
            Next
        End If
        ' 签名带上"有没有包"：有包无档 与 没包 的档位列表都只剩默认档一项，
        ' 只按列表签名会漏掉这两种状态之间的切换（水印理由不同）。
        Dim 签名 = If(配置 Is Nothing, "无包", "有包") & String.Join("|", 新)
        If 签名 = _档表签名 Then Return
        _档表签名 = 签名
        _档名表 = 新

        Dim 选中 = If(设置.实例对象.Ai字幕模型档, String.Empty)
        Dim 下标 = 0
        For i = 1 To 新.Count - 1
            If String.Equals(新(i), 选中, StringComparison.OrdinalIgnoreCase) Then 下标 = i
        Next

        正在初始化 = True
        Try
            MCB_模型档.Items.Clear()
            If 新.Count > 1 Then
                ' 多档：默认档打头，后面跟各档名。
                MCB_模型档.Items.Add(默认档显示)
                For i = 1 To 新.Count - 1
                    MCB_模型档.Items.Add(新(i))
                Next
                MCB_模型档.SelectedIndex = 下标
                MCB_模型档.Enabled = True
            Else
                ' 没得选（没配包 / 包里只有一档）：原因必须作为可见项显示。
                ' 下拉框的水印只在无选中项时可见，而 SelectedIndex 恒 ≥ 0 ——
                ' 靠 WaterText 说话等于没说，用户只看到一个灰下拉、
                ' 没有任何地方告诉他为什么。置灰 + 项里写清下一步动作。
                MCB_模型档.Items.Add(If(配置 Is Nothing,
                    "需先选择有效扩展包（含 ai-subtitles.json）",
                    "此包仅一个档位"))
                MCB_模型档.SelectedIndex = 0
                MCB_模型档.Enabled = False
            End If
            MCB_模型档.WaterText = "模型档位"
        Finally
            正在初始化 = False
        End Try
    End Sub

    Private Sub MCB_模型档_SelectedIndexChanged(sender As Object, e As EventArgs) Handles MCB_模型档.SelectedIndexChanged
        If 正在初始化 Then Return
        Dim 下标 = MCB_模型档.SelectedIndex
        If 下标 < 0 Then Return
        设置.实例对象.Ai字幕模型档 =
            If(下标 < _档名表.Count, _档名表(下标), String.Empty)
        ' 档位是启动期参数：控制器会比对新旧档名并重启 worker。
        Form1.当前主窗体?.应用Ai字幕设置()
        刷新状态()
    End Sub

    ''' <summary>语言下拉下标 ← → 标识。项按 Ai字幕语言.主项 的顺序填（只 4 项）。</summary>
    Private Shared Function 语言下标(值 As String) As Integer
        Dim id = Ai字幕语言.规整(值)
        For i = 0 To Ai字幕语言.主项.Length - 1
            If String.Equals(Ai字幕语言.主项(i), id, StringComparison.Ordinal) Then Return i
        Next
        Return 0
    End Function

    Private Shared Function 语言标识(下标 As Integer) As String
        If 下标 < 0 OrElse 下标 >= Ai字幕语言.主项.Length Then Return Ai字幕语言.自动
        Return Ai字幕语言.主项(下标)
    End Function

    ''' <summary>序号：留空或非法 = -1（交给 llama-server 自选）。上限 15 只为挡脏数据。</summary>
    Private Shared Function 解析序号(文本 As String) As Integer
        Dim 值 As Integer
        If Integer.TryParse(文本, 值) AndAlso 值 >= 0 AndAlso 值 <= 15 Then Return 值
        Return -1
    End Function

    Private Shared Function 序号文本(值 As Integer) As String
        Return If(值 >= 0, 值.ToString(Globalization.CultureInfo.InvariantCulture), String.Empty)
    End Function

    ''' <summary>按包清单声明的 layers 重建 GPU 层数下拉。列表没变（签名相同）就
    ''' 什么都不做——否则每次刷新状态都会清空重建，用户正打开的下拉会被抽走
    ''' （与模型档下拉同一套路）。签名带上"有没有包"：没包 与 有包但没声明 layers
    ''' 的层数表相同（都落默认 28），只按层数签名会漏掉这两种状态间的切换。</summary>
    Private Sub 重建层数下拉(配置 As Ai字幕包配置)
        Dim 层数 = If(配置 IsNot Nothing AndAlso 配置.模型层数 > 0, 配置.模型层数, 默认层数)
        Dim 签名 = If(配置 Is Nothing, "无包", "有包") & 层数.ToString(Globalization.CultureInfo.InvariantCulture)
        If 签名 = _层数表签名 Then Return
        _层数表签名 = 签名
        _生效层数 = 层数
        _层数显示表 = New List(Of String)
        _层数值表 = New List(Of Integer)
        ' 自动 = 不传 --ngl，由 llama-server 自己全卸（GPU 后端）/ 不卸（CPU）。
        _层数显示表.Add("自动（全部卸载）")
        _层数值表.Add(-1)
        If 层数 < 4 Then
            ' 微型模型（不足 4 层）："全部"与"0"之外没有有意义的中间档。
            If 层数 > 0 Then
                _层数显示表.Add($"全部 {层数} 层")
                _层数值表.Add(层数)
            End If
        Else
            ' 全部 N 层打头，每 4 层一档递减到 4，0 层（纯 CPU）收尾。
            ' N=28 时恰为旧固定表（28/24/20/16/12/8/4/0）——老包不声明 layers
            ' 也走得这条分支（默认层数 = 28），行为与过去逐字节一致。
            Dim 档 As Integer = 层数
            While 档 >= 4
                _层数显示表.Add(If(档 = 层数, $"全部 {层数} 层", $"{档} 层"))
                _层数值表.Add(档)
                档 -= 4
            End While
        End If
        _层数显示表.Add("0 层（纯 CPU）")
        _层数值表.Add(0)
        ' 换包后存着的层数可能不在新表里（不同架构层数不同）：先归位再选中，
        ' 否则会出现"界面显示自动、worker 却按 16 卸"——显示什么就必须是什么。
        Dim 值 = 规整层数(设置.实例对象.Ai字幕Gpu层数)
        If 值 <> 设置.实例对象.Ai字幕Gpu层数 Then 设置.实例对象.Ai字幕Gpu层数 = 值
        Dim 外层初始化 = 正在初始化
        正在初始化 = True
        Try
            MCB_Gpu层数.Items.Clear()
            For i = 0 To _层数显示表.Count - 1
                MCB_Gpu层数.Items.Add(_层数显示表(i))
            Next
            MCB_Gpu层数.SelectedIndex = 层数下标(值)
        Finally
            正在初始化 = 外层初始化
        End Try
    End Sub

    ''' <summary>把存着的层数值归位到当前预置表：在表里原样用；≥ 生效层数记生效层数
    ''' （llama-server 本来也会 clamp，老设置里 999 这种"全部"写法走这条）；其余归
    ''' 自动（-1）。只改内存不主动落盘——用户一动界面就会把正确值写回去
    ''' （与语言残留值同一套路）。</summary>
    Private Function 规整层数(值 As Integer) As Integer
        If 层数已预置(值) Then Return 值
        Return If(值 >= _生效层数, _生效层数, -1)
    End Function

    ''' <summary>层数 ← → 下拉下标。项按 _层数显示表/_层数值表 的顺序填。
    ''' 不在表里的值理论上已被 规整层数 归位，落 0（自动）只是兜底。</summary>
    Private Function 层数下标(值 As Integer) As Integer
        For i = 0 To _层数值表.Count - 1
            If _层数值表(i) = 值 Then Return i
        Next
        Return 0
    End Function

    Private Function 层数值(下标 As Integer) As Integer
        If 下标 < 0 OrElse 下标 >= _层数值表.Count Then Return -1
        Return _层数值表(下标)
    End Function

    ''' <summary>存着的层数是不是当前下拉里的预置项。老设置里可能是文本框时代的
    ''' 任意 0–999 整数，归位策略见 规整层数。</summary>
    Private Function 层数已预置(值 As Integer) As Boolean
        Return _层数值表.Contains(值)
    End Function

    ''' <summary>字幕驻留偏移：留空或非法 = 0；范围钳 [-3, +3] 秒（设置项同区间，双保险）。
    ''' 它只调"每行驻留多久"（守卫判定位置 = 播放位置 − 偏移），不调"何时出现"。</summary>
    Private Shared Function 解析偏移(文本 As String) As Double
        Dim 值 As Double
        If Double.TryParse(文本, 值) Then Return Math.Clamp(值, -3.0, 3.0)
        Return 0
    End Function

    Private Shared Function 偏移文本(值 As Double) As String
        Return 值.ToString("0.###", Globalization.CultureInfo.InvariantCulture)
    End Function

    ''' <summary>按当前窗口/步长覆盖值同步预置下拉：两个都是 0 = 跟随包清单；与某项
    ''' 预置完全相等 = 选中它；都对不上 = 自定义（手填过）。</summary>
    Private Sub 同步预置下拉()
        Dim 窗口 = 设置.实例对象.Ai字幕窗口秒覆盖
        Dim 步长 = 设置.实例对象.Ai字幕步长秒覆盖
        Dim 下标 = _预置显示.Length - 1   ' 默认落"自定义"
        If 窗口 <= 0 AndAlso 步长 <= 0 Then
            下标 = 0
        Else
            For i = 1 To _预置显示.Length - 2
                If _预置窗口(i) = 窗口 AndAlso _预置步长(i) = 步长 Then
                    下标 = i
                    Exit For
                End If
            Next
        End If
        Dim 外层初始化 = 正在初始化
        正在初始化 = True
        Try
            MCB_预置.SelectedIndex = 下标
        Finally
            正在初始化 = 外层初始化
        End Try
    End Sub

    Private Shared Function 解析覆盖(文本 As String) As Double
        Dim 值 As Double
        If Double.TryParse(文本, 值) AndAlso 值 >= 0.5 AndAlso 值 <= 30 Then Return 值
        Return 0   ' 0 = 跟随包清单（留空或非法值都归到这里）
    End Function

    Private Shared Function 覆盖文本(值 As Double) As String
        Return If(值 > 0, 值.ToString("0.###"), String.Empty)
    End Function
End Class
