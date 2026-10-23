Imports LakeUI

''' <summary>
''' 设置页 → AI 字幕（docs/ai-subtitles/）。
'''
''' 本体不内置任何 ASR 能力，只认"扩展包"这一个契约：一个含 ai-subtitles.json 的
''' 目录，包自己说清 worker / python / 模型 / llama-server 在哪、用什么量化档。
''' 于是这一页只需要一行"选哪个目录"——选到有效包即启用，包没了就自动停用。
''' 窗口/步长/设备序号/模型档/GPU 层数在"高级选项"里（默认展开，可随手折回去），
''' 留空表示跟随包清单（默认）。GPU 层数是下拉（模型 28 层）：自动 = 不指定，
''' 由 llama-server 自己全卸；显存吃紧再逐档往下选。模型档位取自包清单的
''' models 表：没配包或包里只有一个档时下拉置灰，水印会说明原因。
'''
''' 路径文本框走 Leave 而不是 TextChanged：半途路径写进设置会在下次打开媒体时
''' 被当成有效配置去启 worker，滋一脸错误提示。数值类同理，解析失败就不写。
''' </summary>
Public Class Form设置_AI字幕
    Private 正在初始化 As Boolean

    ' GPU 层数下拉项：显示 ↔ 值（--ngl；负数 = 不传，llama-server 自行全卸）。
    ' 模型 28 层（Qwen3-ASR-0.6B 的 GGUF block_count），按 4 层一档切到 0。
    Private Shared ReadOnly _层数显示 As String() = {
        "自动（全部卸载）", "全部 28 层", "24 层", "20 层", "16 层", "12 层", "8 层", "4 层", "0 层（纯 CPU）"}
    Private Shared ReadOnly _层数值 As Integer() = {-1, 28, 24, 20, 16, 12, 8, 4, 0}

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
            ' GPU 层数下拉：自动（不指定）打头，之后按 4 层一档给到 0。
            ' 同样只在构造时填一次；顺序即下标，值表见 _层数值。
            For i = 0 To _层数显示.Length - 1
                MCB_Gpu层数.Items.Add(_层数显示(i))
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
            ' 高级项默认展开：窗口/步长/设备序号/模型档都是用户明确想调才会来调的东西，
            ' 藏一层折叠只会让人以为"这些不让改"。想看简洁画面随手再折上去。
            MCK_显示高级.Checked = True
            MTB_窗口秒.Text = 覆盖文本(设置.实例对象.Ai字幕窗口秒覆盖)
            MTB_步长秒.Text = 覆盖文本(设置.实例对象.Ai字幕步长秒覆盖)
            MTB_设备序号.Text = 序号文本(设置.实例对象.Ai字幕设备序号)
            ' 文本框时代存下来的任意层数（含 999 这种"全部"的老写法）归位到最近
            ' 预置项：≥28 记 28（llama-server 本来也会 clamp），其余归自动。只改
            ' 内存不主动落盘，与语言残留值同一套路——界面显示什么就是什么。
            Dim 层数 = 设置.实例对象.Ai字幕Gpu层数
            If Not 层数已预置(层数) Then
                层数 = If(层数 >= 28, 28, -1)
                设置.实例对象.Ai字幕Gpu层数 = 层数
            End If
            MCB_Gpu层数.SelectedIndex = 层数下标(层数)
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
        刷新状态()
        Form1.当前主窗体?.应用Ai字幕设置()
    End Sub

    Private Sub MTB_步长秒_Leave(sender As Object, e As EventArgs) Handles MTB_步长秒.Leave
        If 正在初始化 Then Return
        设置.实例对象.Ai字幕步长秒覆盖 = 解析覆盖(MTB_步长秒.Text)
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

    ''' <summary>状态行：包是否有效、实际生效的窗口/步长、有没有被高级覆盖。</summary>
    Private Sub 刷新状态()
        Dim 目录 = MTB_包目录.Text.Trim()
        Dim 错误 As String = String.Empty
        Dim 配置 = Ai字幕包.载入(目录, 错误)
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
        ' 而无条件拼上去会把第二行撑爆（568px 放不下"设备+模型+语言+层数"全套）。
        Dim 层数 = 设置.实例对象.Ai字幕Gpu层数
        Dim 层数说明 = If(层数 >= 0, $"　层数：{层数}", "")
        LBL_状态.Text = $"已启用：{配置.名称} —— 窗口 {窗口:0.###}s / 步长 {步长:0.###}s{后缀}" & vbCrLf &
                        $"推理设备：{设备说明}　模型：{档名}　字幕语言：{语言说明}{层数说明}"
        If 缺二进制 Then
            LBL_状态.Text &= vbCrLf & "注意：包内没有该后端专用的二进制，会回落到包默认的那个。"
        End If
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

    ''' <summary>层数 ← → 下拉下标。项在构造时按 _层数显示/_层数值 的顺序填。
    ''' 不在表里的值理论上已被调用方归位，落 0（自动）只是兜底。</summary>
    Private Shared Function 层数下标(值 As Integer) As Integer
        For i = 0 To _层数值.Length - 1
            If _层数值(i) = 值 Then Return i
        Next
        Return 0
    End Function

    Private Shared Function 层数值(下标 As Integer) As Integer
        If 下标 < 0 OrElse 下标 >= _层数值.Length Then Return -1
        Return _层数值(下标)
    End Function

    ''' <summary>存着的层数是不是下拉里的预置项。老设置里可能是文本框时代的
    ''' 任意 0–999 整数，归位策略见 初始化页面。</summary>
    Private Shared Function 层数已预置(值 As Integer) As Boolean
        For i = 0 To _层数值.Length - 1
            If _层数值(i) = 值 Then Return True
        Next
        Return False
    End Function

    Private Shared Function 解析覆盖(文本 As String) As Double
        Dim 值 As Double
        If Double.TryParse(文本, 值) AndAlso 值 >= 0.5 AndAlso 值 <= 30 Then Return 值
        Return 0   ' 0 = 跟随包清单（留空或非法值都归到这里）
    End Function

    Private Shared Function 覆盖文本(值 As Double) As String
        Return If(值 > 0, 值.ToString("0.###"), String.Empty)
    End Function
End Class
