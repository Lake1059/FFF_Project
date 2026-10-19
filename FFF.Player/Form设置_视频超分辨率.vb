' 设置 → 视频超分辨率 页（10-18 从标题栏菜单移过来）。
' 页内上下两区：上 RTX 视频超分辨率（VSR，PPE 私有流扩展），下 自动 HDR（TrueHDR，
' 同一 ID3D11VideoProcessor 私有流扩展家族的另一个 GUID）。两者互不代理、各自持有会话内
' 开关状态；概念不同（一个放大增强、一个 SDR→HDR10 色调映射），载荷与门槛也不同。
'
' 分工：原生调用、偏好落盘、"每个新会话重放开关"全在各自的控制器
' （播放器视频超分辨率控制器 / 播放器视频自动HDR控制器）里；
' 本页只画控件、转发动作、摊开内核读数。页内不再自己发第二次请求。
'
' 为什么二级菜单里没有"强度/档位"控件：交给 NVIDIA 驱动的视频处理器流扩展载荷只有
' {version, method, enable} 三个 UINT（FFF.Native/3FP/Render/NvidiaVideoExtensions.h:40-44），
' 画质档位 1-4 与总开关归 NVIDIA App / 控制面板，本程序既设不了也读不回来（同文件 :19-21、:70-71）。
' 放一个滑条就是骗人，所以这里只放真的能动的东西：请求/取消、清闩重探、内核读数与本树自定的门槛。
Public Class Form设置_视频超分辨率
    Private 正在初始化 As Boolean

    Public Sub New()
        正在初始化 = True
        Try
            InitializeComponent()
        Finally
            正在初始化 = False
        End Try
    End Sub

    Friend Sub 初始化页面()
        正在初始化 = True
        Try
            Dim 主窗体 = Form1.当前主窗体
            ' 不用 If(主窗体?.X(), False) 那种写法：可空布尔进不了 If 的条件位，编译就报错。
            Dim 可用 = If(主窗体 Is Nothing, False, 主窗体.视频超分辨率可用())
            Dim 已启用 = If(主窗体 Is Nothing, False, 主窗体.视频超分辨率已启用())
            MCK_启用RTX视频超分辨率.Checked = 已启用
            MCK_启用RTX视频超分辨率.Enabled = 可用
            HCL_说明.Text = If(可用,
                "RTX 视频超分辨率：由内核逐帧判定是否使用；" &
                "画质档位 1-4 归 NVIDIA App / 控制面板，本程序不可设置也读不回来。",
                "当前内核未提供 RTX 视频超分辨率入口（需更新 FFF.Native.dll）。")
            HCL_状态读数.Text = "状态：" & If(主窗体?.读取视频超分辨率状态(), "未读取（会话未就绪）")
            Dim 自动HDR可用 = If(主窗体 Is Nothing, False, 主窗体.视频自动HDR可用())
            Dim 自动HDR已启用 = If(主窗体 Is Nothing, False, 主窗体.视频自动HDR已启用())
            MCK_启用视频自动HDR.Checked = 自动HDR已启用
            MCK_启用视频自动HDR.Enabled = 自动HDR可用
            HCL_自动HDR状态读数.Text = "状态：" & If(主窗体?.读取视频自动HDR状态(), "未读取（会话未就绪）")
        Finally
            正在初始化 = False
        End Try
    End Sub

    Private Sub 开关_CheckedChanged(sender As Object, e As EventArgs) _
            Handles MCK_启用RTX视频超分辨率.CheckedChanged
        If 正在初始化 Then Return
        Dim 主窗体 = Form1.当前主窗体
        If 主窗体 Is Nothing Then Return
        ' 控制器内部幂等：与当前值一致就不重复下发，所以这里可以直说目标值。
        主窗体.应用视频超分辨率设置(MCK_启用RTX视频超分辨率.Checked)
        正在初始化 = True
        Try
            MCK_启用RTX视频超分辨率.Checked = 主窗体.视频超分辨率已启用()
            HCL_状态读数.Text = "状态：" & If(主窗体.读取视频超分辨率状态(), "未读取（会话未就绪）")
        Finally
            正在初始化 = False
        End Try
    End Sub

    ''' <summary>自动 HDR 开关。与 VSR 分开 Handler：两个概念各自的控制器互不代理，
    ''' 合成一个 Handler 就要在方法里再分岔，分岔就会长歪。</summary>
    Private Sub 自动HDR开关_CheckedChanged(sender As Object, e As EventArgs) _
            Handles MCK_启用视频自动HDR.CheckedChanged
        If 正在初始化 Then Return
        Dim 主窗体 = Form1.当前主窗体
        If 主窗体 Is Nothing Then Return
        主窗体.应用视频自动HDR设置(MCK_启用视频自动HDR.Checked)
        正在初始化 = True
        Try
            MCK_启用视频自动HDR.Checked = 主窗体.视频自动HDR已启用()
            HCL_自动HDR状态读数.Text = "状态：" & If(主窗体.读取视频自动HDR状态(), "未读取（会话未就绪）")
        Finally
            正在初始化 = False
        End Try
    End Sub

    ''' <summary>自动 HDR 二级菜单：能动的动作 + 内核真读数。</summary>
    Private Sub MB_自动HDR调整与读数_Click(sender As Object, e As EventArgs) _
            Handles MB_自动HDR调整与读数.Click
        Dim 主窗体 = Form1.当前主窗体
        If 主窗体 Is Nothing Then Return
        Dim 菜单 As New LakeUI.ModernContextMenu With {
            .MenuFont = Me.Font,
            .IconSize = 0
        }

        Dim 开启项 = New LakeUI.ModernContextMenu.ModernMenuItem("请求自动 HDR") With {
            .Checked = 主窗体.视频自动HDR已启用(),
            .CloseOnClick = False,
            .ToggleCheckOnClick = False
        }
        AddHandler 开启项.Click, Sub()
                                    主窗体.应用视频自动HDR设置(Not 主窗体.视频自动HDR已启用())
                                    初始化页面()
                                End Sub
        菜单.Items.Add(开启项)

        Dim 重探项 = New LakeUI.ModernContextMenu.ModernMenuItem(
            "清掉驱动拒绝闩并重新探测（先关后开）") With {.CloseOnClick = True}
        AddHandler 重探项.Click, Sub()
                                    主窗体.重新探测视频自动HDR()
                                    初始化页面()
                                End Sub
        菜单.Items.Add(重探项)

        Dim 读数项 = New LakeUI.ModernContextMenu.ModernMenuItem("内核读数与本树门槛") With {
            .SubMenu = 创建自动HDR读数子菜单(主窗体)
        }
        菜单.Items.Add(读数项)

        菜单.Show(MB_自动HDR调整与读数, New Point(0, MB_自动HDR调整与读数.Height))
    End Sub

    Private Function 创建自动HDR读数子菜单(主窗体 As Form1) As LakeUI.ModernContextMenu
        Dim 子菜单 As New LakeUI.ModernContextMenu With {
            .MenuFont = Me.Font,
            .IconSize = 0
        }
        ' 全部用 IsDescription：这些行天生不可点，正好用来摊开内核的原因码。
        ' 另外不要在这些字符串里写全角引号——VB 把成对的弯引号也当字符串定界符，
        ' 编译器会在这里报 BC32017（VSR 页实测踩过）。
        添加描述行(子菜单, "状态：" &
            If(主窗体.读取视频自动HDR状态(), "未读取（会话未就绪）"))
        添加描述行(子菜单, "已启用 指探测通过且最后一帧 Blt 成功，不等于画质有保证")
        添加描述行(子菜单, "生成的 HDR 由驱动计算；强度档位 1-4 与总开关归 NVIDIA App / 控制面板")
        添加描述行(子菜单, "本树门槛：NVIDIA 适配器、SDR 源、已是 10 位 HDR 输出契约、非隔行")
        添加描述行(子菜单, "对比模式激活时内核强制互斥（返回 InvalidState），本开关不绕过")
        Return 子菜单
    End Function

    ''' <summary>二级菜单：能动的动作 + 内核真读数。每次打开重建，避免勾与闩状态变陈旧。</summary>
    Private Sub MB_调整与读数_Click(sender As Object, e As EventArgs) Handles MB_调整与读数.Click
        Dim 主窗体 = Form1.当前主窗体
        If 主窗体 Is Nothing Then Return
        Dim 菜单 As New LakeUI.ModernContextMenu With {
            .MenuFont = Me.Font,
            .IconSize = 0
        }

        Dim 开启项 = New LakeUI.ModernContextMenu.ModernMenuItem("请求超分辨率") With {
            .Checked = 主窗体.视频超分辨率已启用(),
            .CloseOnClick = False,
            .ToggleCheckOnClick = False
        }
        AddHandler 开启项.Click, Sub()
                                    主窗体.应用视频超分辨率设置(Not 主窗体.视频超分辨率已启用())
                                    初始化页面()
                                End Sub
        菜单.Items.Add(开启项)

        Dim 重探项 = New LakeUI.ModernContextMenu.ModernMenuItem(
            "清掉驱动拒绝闩并重新探测（先关后开）") With {.CloseOnClick = True}
        AddHandler 重探项.Click, Sub()
                                    主窗体.重新探测视频超分辨率()
                                    初始化页面()
                                End Sub
        菜单.Items.Add(重探项)

        Dim 读数项 = New LakeUI.ModernContextMenu.ModernMenuItem("内核读数与本树门槛") With {
            .SubMenu = 创建读数子菜单(主窗体)
        }
        菜单.Items.Add(读数项)

        菜单.Show(MB_调整与读数, New Point(0, MB_调整与读数.Height))
    End Sub

    Private Function 创建读数子菜单(主窗体 As Form1) As LakeUI.ModernContextMenu
        Dim 子菜单 As New LakeUI.ModernContextMenu With {
            .MenuFont = Me.Font,
            .IconSize = 0
        }
        ' 全部用 IsDescription：这些行天生不可点，正好用来摊开内核的原因码，
        ' 免得用户拿"勾没勾"去推断"有没有生效"。
        ' 另外不要在这些字符串里写全角引号——VB 把成对的弯引号也当字符串定界符，
        ' 编译器会在这里报 BC32017（本轮实测）。
        添加描述行(子菜单, "状态：" &
            If(主窗体.读取视频超分辨率状态(), "未读取（会话未就绪）"))
        添加描述行(子菜单, "驱动已接受 只说明没被拒绝，不等于画面已被增强")
        添加描述行(子菜单, "本树门槛：源高度 360..1440、半平面 NV12/P010、非隔行、≤10bit、需 D3D11_BIND_DECODER")
        添加描述行(子菜单, "画质档位 1-4 与总开关归 NVIDIA App / 控制面板（驱动载荷无档位字段）")
        Return 子菜单
    End Function

    Private Shared Sub 添加描述行(菜单 As LakeUI.ModernContextMenu, 文本 As String)
        Dim 项 = New LakeUI.ModernContextMenu.ModernMenuItem(文本)
        项.IsDescription = True
        菜单.Items.Add(项)
    End Sub
End Class
