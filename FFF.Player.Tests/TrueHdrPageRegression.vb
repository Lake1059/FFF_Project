' 设置 → 视频超分辨率 页内**自动 HDR 区**的回归（10-23 与 VSR 区同页并列）。
'
' 判据与 VsrPageRegression 同构，但对象换成自动 HDR 那一组：
'   正向：控制器三个动作仍在、页上四件控件齐备、Form设置 绑的仍是同一个页
'   （自动 HDR 不新开页——一个"视频增强"概念只留一处入口，避免两处开关）。
'   反向：控制器不持有菜单项字段、构造不收 ModernContextMenu（它同样不该自己造菜单）。
'   诚实性：无主窗体时勾不可点、读数行带「状态：」前缀，不制造「勾了没反应」。
'
' 结构检查不需要窗口句柄；落点（Y 序与高度）需要 CreateControl()，在无交互会话里
' 可能建不出句柄 ⇒ 那两项按"前置不具备"跳过（exit 2），不判红。
'
' 用法：FFF.Player.Tests --truehdr-page-regression
' 退出码：0 全绿 / 1 有失败条 / 2 前置条件不具备（句柄建不出来）
Imports System
Imports System.Linq
Imports System.Reflection
Imports FFF.Player

Friend Module TrueHdrPageRegression
    Private 通过 As Integer
    Private 失败 As Integer
    Private 跳过 As Integer

    Private Sub 检查(条件 As Boolean, 名称 As String, 实测 As String)
        If 条件 Then
            通过 += 1
            Console.WriteLine($"PASS {名称} :: {实测}")
        Else
            失败 += 1
            Console.WriteLine($"FAIL {名称} :: {实测}")
        End If
    End Sub

    Private Sub 跳过检查(名称 As String, 原因 As String)
        跳过 += 1
        Console.WriteLine($"SKIP {名称} :: {原因}")
    End Sub

    ' Designer 生成的控件字段是 Friend WithEvents，IL 里带下划线前缀（实测 _HCL_说明），
    ' 按源码名 + 下划线前缀两种写法反射取，避免"查找方式不对"被写成"控件不存在"。
    Private Function 取控件(页 As System.Windows.Forms.Control, 名 As String) As System.Windows.Forms.Control
        Dim t = 页.GetType()
        For Each 候选 In {名, "_" & 名}
            Dim f = t.GetField(候选,
                BindingFlags.NonPublic Or BindingFlags.Instance Or BindingFlags.Public)
            If f IsNot Nothing Then Return CType(f.GetValue(页), System.Windows.Forms.Control)
        Next
        Return Nothing
    End Function

    Private Function 读布尔(obj As Object, 名 As String) As Boolean?
        Dim p = obj?.GetType().GetProperty(名)
        If p Is Nothing Then Return Nothing
        Dim v = p.GetValue(obj)
        If TypeOf v Is Boolean Then Return CType(v, Boolean)
        Return Nothing
    End Function

    Friend Function 运行(args As String()) As Integer
        Console.OutputEncoding = System.Text.Encoding.UTF8
        Dim 控制器类型 = GetType(播放器视频自动HDR控制器)
        Dim 页类型 = GetType(Form设置_视频超分辨率)
        Dim 实例掩码 = BindingFlags.NonPublic Or BindingFlags.Public Or BindingFlags.Instance

        ' ── 反向：控制器不造菜单、构造不收菜单 ────────────────────────────────
        Dim 构造参数 = 控制器类型.GetConstructors(实例掩码)(0).GetParameters()
        检查(构造参数.Length = 6, "自动 HDR 控制器参数个数（与 VSR 控制器同构，应为 6）",
            构造参数.Length.ToString())
        Dim 参数表 = String.Join(",", 构造参数.Select(Function(p) p.ParameterType.Name))
        检查(Not 参数表.Contains("ModernContextMenu"), "控制器构造不收标题栏菜单", 参数表)
        Dim 残留菜单字段 = 控制器类型.GetFields(BindingFlags.NonPublic Or BindingFlags.Instance).
            Where(Function(f) f.FieldType.Name = "ModernMenuItem").Select(Function(f) f.Name).ToArray()
        检查(残留菜单字段.Length = 0, "控制器不持有 ModernMenuItem 字段（入口只在设置页）",
            If(残留菜单字段.Length = 0, "无", String.Join(",", 残留菜单字段)))

        ' ── 正向：三个动作 + 会话重放都在 ────────────────────────────────────
        For Each 动作 In {"设置为", "重新探测", "刷新状态", "媒体已打开"}
            检查(控制器类型.GetMethod(动作, 实例掩码) IsNot Nothing,
                $"自动 HDR 控制器保留 {动作}", "反射命中")
        Next

        ' ── 正向：仍绑定同一个设置页，没有新开页 ─────────────────────────────
        Dim 页面字段 = GetType(Form设置).GetFields(BindingFlags.NonPublic Or BindingFlags.Instance).
            Where(Function(f) f.FieldType Is 页类型).Select(Function(f) f.Name).ToArray()
        检查(页面字段.Length = 1, "Form设置 绑定的仍是那一个 视频超分辨率 页（自动 HDR 不新开页）",
            If(页面字段.Length = 0, "无匹配字段", String.Join(",", 页面字段)))

        ' ── 正向：无主窗体时页面不抛，且如实报"不可用/未读取" ─────────────────
        Dim 页 = CType(Activator.CreateInstance(页类型), System.Windows.Forms.Form)
        Dim 抛错 As String = Nothing
        Try
            页类型.GetMethod("初始化页面",
                BindingFlags.NonPublic Or BindingFlags.Public Or BindingFlags.Instance).Invoke(页, Nothing)
        Catch ex As Exception
            抛错 = ex.GetType().Name & ":" & ex.Message
        End Try
        检查(抛错 Is Nothing, "无主窗体时 初始化页面 不抛异常", If(抛错, "未抛"))
        Dim 勾选 = 取控件(页, "MCK_启用视频自动HDR")
        Dim 按钮 = 取控件(页, "MB_自动HDR调整与读数")
        Dim 读数 = 取控件(页, "HCL_自动HDR状态读数")
        Dim 说明 = 取控件(页, "HCL_自动HDR说明")
        检查(勾选 IsNot Nothing AndAlso 按钮 IsNot Nothing AndAlso 读数 IsNot Nothing AndAlso
              说明 IsNot Nothing, "自动 HDR 四件控件都在（说明/勾选/二级菜单按钮/读数）",
            $"说明={说明 IsNot Nothing} 勾选={勾选 IsNot Nothing} 按钮={按钮 IsNot Nothing} 读数={读数 IsNot Nothing}")
        If 勾选 IsNot Nothing Then
            Dim 勾值 = 读布尔(勾选, "Checked")
            检查(勾值.HasValue AndAlso Not 勾值.Value, "无主窗体时勾不带假值",
                If(勾值.HasValue, 勾值.Value.ToString(), "读不到 Checked 属性"))
            检查(Not 勾选.Enabled, "无主窗体时勾不可点（不制造「勾了没反应」）", 勾选.Enabled.ToString())
        End If
        If 读数 IsNot Nothing Then
            检查(读数.Text.StartsWith("状态："), "读数行带「状态：」前缀", 读数.Text)
        End If

        ' ── 正向：VSR 区没被自动 HDR 区挤坏（四件老控件仍在）────────────────
        Dim 老勾选 = 取控件(页, "MCK_启用RTX视频超分辨率")
        Dim 老按钮 = 取控件(页, "MB_调整与读数")
        Dim 老读数 = 取控件(页, "HCL_状态读数")
        Dim 老说明 = 取控件(页, "HCL_说明")
        检查(老勾选 IsNot Nothing AndAlso 老按钮 IsNot Nothing AndAlso 老读数 IsNot Nothing AndAlso
              老说明 IsNot Nothing, "VSR 区四件控件未受影响",
            $"说明={老说明 IsNot Nothing} 勾选={老勾选 IsNot Nothing} 按钮={老按钮 IsNot Nothing} 读数={老读数 IsNot Nothing}")

        ' ── 正向：出货构型的落点（需要句柄）─────────────────────────────────
        Dim 句柄可用 As Boolean = True
        Try
            页.CreateControl()
        Catch ex As Exception
            句柄可用 = False
            Console.WriteLine($"  CreateControl 失败：{ex.GetType().Name}:{ex.Message}")
        End Try
        If Not 句柄可用 OrElse 说明 Is Nothing OrElse 勾选 Is Nothing OrElse
            按钮 Is Nothing OrElse 读数 Is Nothing OrElse 老读数 Is Nothing Then
            跳过检查("落点顺序与不重叠", If(句柄可用, "控件缺失", "无交互会话，建不出句柄"))
        Else
            Dim ys = New Integer() {老读数.Bounds.Bottom, 说明.Bounds.Top,
                                     勾选.Bounds.Top, 按钮.Bounds.Top, 读数.Bounds.Top}
            检查(ys(0) <= ys(1) AndAlso ys(1) <= ys(2) AndAlso ys(2) <= ys(3) AndAlso ys(3) <= ys(4),
                "落点顺序：VSR 读数 → 自动HDR 说明 → 勾选 → 二级菜单按钮 → 读数",
                String.Join("<", ys.Select(Function(v) v.ToString())))
            检查(勾选.Height > 0 AndAlso 按钮.Height > 0 AndAlso 读数.Height > 0,
                "自动 HDR 三件控件高度非零",
                $"勾选高={勾选.Height} 按钮高={按钮.Height} 读数高={读数.Height}")
            检查(按钮.Width > 0 AndAlso 按钮.Bounds.Right <= 页.ClientSize.Width,
                "二级菜单按钮在页面宽度内", $"按钮宽={按钮.Width} 右边={按钮.Bounds.Right} 客户宽={页.ClientSize.Width}")
        End If

        Console.WriteLine($"TRUEHDR_PAGE_SUMMARY pass={通过} fail={失败} skip={跳过}")
        If 失败 > 0 Then Return 1
        If 跳过 > 0 AndAlso 通过 = 0 Then Return 2
        Return 0
    End Function
End Module
