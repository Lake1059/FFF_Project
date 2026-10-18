' 设置 → 视频超分辨率 页的回归（10-18 把 VSR 从标题栏菜单移进设置页 + 加二级菜单）。
'
' 这一条判据要同时证明"移动"的两半，只证一半等于没证：
'   正向：新页存在、控件齐备且落点有序、设置窗口确实绑了它、控制器仍提供那三个动作。
'   反向：控制器**不再造菜单项**（原先它自己往标题栏菜单塞两项）。所以这里用
'   "类型里不再有 ModernMenuItem 字段"与"构造参数里没有 ModernContextMenu"两条反证，
'   而不是只数新页的控件。
'
' 结构检查不需要窗口句柄；落点（Y 序与高度）需要 CreateControl()，在无交互会话里
' 可能建不出句柄 ⇒ 那两项按"前置不具备"跳过（exit 2），不判红（同 3FP 内核套件的规矩）。
'
' 用法：FFF.Player.Tests --vsr-page-regression
' 退出码：0 全绿 / 1 有失败条 / 2 前置条件不具备（句柄建不出来）
Imports System
Imports System.IO
Imports System.Linq
Imports System.Reflection
Imports FFF.Player

Friend Module VsrPageRegression
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

    ' Designer 生成的控件字段是 Friend WithEvents（对 VB 就是 internal），
    ' 本工程靠 InternalsVisibleTo 看得见，但为了对模板改动不敏感，这里按名字反射取。
    Private Function 读布尔(obj As Object, 名 As String) As Boolean?
        Dim p = obj?.GetType().GetProperty(名)
        If p Is Nothing Then Return Nothing
        Dim v = p.GetValue(obj)
        If TypeOf v Is Boolean Then Return CType(v, Boolean)
        Return Nothing
    End Function

    Private Function 取控件(页 As System.Windows.Forms.Control, 名 As String) As System.Windows.Forms.Control
        ' VB 把 Friend WithEvents 字段在 IL 里落成带下划线前缀的名字（实测 _HCL_说明），
        ' 只按源码名查会查不到 —— 那会把"我的查找方式不对"写成"控件不存在"的假红。
        Dim t = 页.GetType()
        For Each 候选 In {名, "_" & 名}
            Dim f = t.GetField(候选,
                BindingFlags.NonPublic Or BindingFlags.Instance Or BindingFlags.Public)
            If f IsNot Nothing Then Return CType(f.GetValue(页), System.Windows.Forms.Control)
        Next
        Return Nothing
    End Function

    Friend Function 运行(args As String()) As Integer
        Console.OutputEncoding = System.Text.Encoding.UTF8
        Dim 控制器类型 = GetType(播放器视频超分辨率控制器)
        Dim 页类型 = GetType(Form设置_视频超分辨率)

        ' ── 反向：控制器不再拥有菜单项，也不再收菜单 ────────────────────────────
        ' 这些类型与成员都是 Friend（对 .NET 就是 internal），而反射默认只看 Public：
        ' GetConstructors() 会给空数组、GetMethod 会给 Nothing。必须显式带 NonPublic。
        Dim 实例掩码 = BindingFlags.NonPublic Or BindingFlags.Public Or BindingFlags.Instance
        Dim 构造参数 = 控制器类型.GetConstructors(实例掩码)(0).GetParameters()
        Dim 参数表 = String.Join(",", 构造参数.Select(Function(p) p.ParameterType.Name))
        检查(Not 参数表.Contains("ModernContextMenu"), "控制器构造不再收标题栏菜单", 参数表)
        检查(构造参数.Length = 6, "控制器参数个数（无菜单项后应为 6）", 构造参数.Length.ToString())
        Dim 残留菜单字段 = 控制器类型.GetFields(BindingFlags.NonPublic Or BindingFlags.Instance).
            Where(Function(f) f.FieldType.Name = "ModernMenuItem").Select(Function(f) f.Name).ToArray()
        检查(残留菜单字段.Length = 0, "控制器不再持有 ModernMenuItem 字段（菜单项已从标题栏撤走）",
            If(残留菜单字段.Length = 0, "无", String.Join(",", 残留菜单字段)))

        ' ── 正向：三个动作仍在（移动不能把能力搬丢）─────────────────────────────
        For Each 动作 In {"设置为", "重新探测", "刷新状态", "媒体已打开"}
            检查(控制器类型.GetMethod(动作, 实例掩码) IsNot Nothing,
                $"控制器保留 {动作}", "反射命中")
        Next

        ' ── 正向：设置窗口绑了新页 ───────────────────────────────────────────
        Dim 页面字段 = GetType(Form设置).GetFields(BindingFlags.NonPublic Or BindingFlags.Instance).
            Where(Function(f) f.FieldType Is 页类型).Select(Function(f) f.Name).ToArray()
        检查(页面字段.Length = 1, "Form设置 绑定了 视频超分辨率 页",
            If(页面字段.Length = 0, "无匹配字段", String.Join(",", 页面字段)))

        ' ── 正向：无主窗体时页面不抛，且如实报"不可用/未读取" ───────────────────
        Dim 页 = CType(Activator.CreateInstance(页类型), System.Windows.Forms.Form)
        Dim 抛错 As String = Nothing
        Try
            页类型.GetMethod("初始化页面",
                BindingFlags.NonPublic Or BindingFlags.Public Or BindingFlags.Instance).Invoke(页, Nothing)
        Catch ex As Exception
            抛错 = ex.GetType().Name & ":" & ex.Message
        End Try
        检查(抛错 Is Nothing, "无主窗体时 初始化页面 不抛异常", If(抛错, "未抛"))
        Dim 勾选 = 取控件(页, "MCK_启用RTX视频超分辨率")
        Dim 按钮 = 取控件(页, "MB_调整与读数")
        Dim 读数 = 取控件(页, "HCL_状态读数")
        Dim 说明 = 取控件(页, "HCL_说明")
        检查(勾选 IsNot Nothing AndAlso 按钮 IsNot Nothing AndAlso 读数 IsNot Nothing AndAlso
              说明 IsNot Nothing, "页上四件控件都在（说明/勾选/二级菜单按钮/读数）",
            $"说明={说明 IsNot Nothing} 勾选={勾选 IsNot Nothing} 按钮={按钮 IsNot Nothing} 读数={读数 IsNot Nothing}")
        If 勾选 Is Nothing Then
            ' 取不到就点名本类型到底有哪些实例字段，别把"我的查找方式不对"写成"控件不存在"。
            Dim 字段名 = String.Join(",", 页类型.GetFields(BindingFlags.NonPublic Or
                BindingFlags.Public Or BindingFlags.Instance).Select(Function(f) f.Name).ToArray())
            Console.WriteLine($"  页类型实例字段：{字段名}")
        End If
        If 勾选 IsNot Nothing Then
            ' Checked 是 LakeUI.ModernCheckBox 自己的属性（不是 WinForms Control 的成员），
            ' 所以按名字读，避免本工程硬依赖 LakeUI 的类型。
            Dim 勾值 = 读布尔(勾选, "Checked")
            检查(勾值.HasValue AndAlso Not 勾值.Value, "无主窗体时勾不带假值",
                If(勾值.HasValue, 勾值.Value.ToString(), "读不到 Checked 属性"))
            检查(Not 勾选.Enabled, "无主窗体时勾不可点（不制造「勾了没反应」）", 勾选.Enabled.ToString())
        End If
        If 读数 IsNot Nothing Then
            检查(读数.Text.StartsWith("状态："), "读数行带「状态：」前缀", 读数.Text)
        End If

        ' ── 正向：出货构型的落点（需要句柄）─────────────────────────────────
        Dim 句柄可用 As Boolean = True
        Try
            页.CreateControl()
        Catch ex As Exception
            句柄可用 = False
            Console.WriteLine($"  CreateControl 失败：{ex.GetType().Name}:{ex.Message}")
        End Try
        If Not 句柄可用 OrElse 说明 Is Nothing OrElse 勾选 Is Nothing OrElse
           按钮 Is Nothing OrElse 读数 Is Nothing Then
            跳过检查("四件控件自上而下顺序不重叠", If(句柄可用, "控件缺失", "无交互会话，建不出句柄"))
        Else
            Dim ys = New Integer() {说明.Bounds.Bottom, 勾选.Bounds.Top, 按钮.Bounds.Top, 读数.Bounds.Top}
            检查(ys(0) <= ys(1) AndAlso ys(1) <= ys(2) AndAlso ys(2) <= ys(3),
                "落点顺序：说明 → 勾选 → 二级菜单按钮 → 读数",
                String.Join("<", ys.Select(Function(v) v.ToString())))
            检查(勾选.Height > 0 AndAlso 按钮.Height > 0 AndAlso 读数.Height > 0,
                "三件可交互/可读控件高度非零",
                $"勾选高={勾选.Height} 按钮高={按钮.Height} 读数高={读数.Height}")
            检查(按钮.Width > 0 AndAlso 按钮.Bounds.Right <= 页.ClientSize.Width,
                "二级菜单按钮在页面宽度内", $"按钮宽={按钮.Width} 右边={按钮.Bounds.Right} 客户宽={页.ClientSize.Width}")
        End If

        Console.WriteLine($"VSR_PAGE_SUMMARY pass={通过} fail={失败} skip={跳过}")
        If 失败 > 0 Then Return 1
        If 跳过 > 0 AndAlso 通过 = 0 Then Return 2
        Return 0
    End Function
End Module
