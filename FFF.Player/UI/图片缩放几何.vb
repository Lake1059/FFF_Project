' 图片模式缩放律：全部纯函数，宿主控制器与 FFF.Player.Tests 调同一份。
'
' 为什么要抽出来：体感类改动最难验的就是"测的不是真正跑的那份逻辑"。钳位、换算、
' 弹簧步进放这里，回归可以直接拿数字判；只有计时器/双击这类 GUI 接线留在控制器里。
'
' 基准口径：缩放值 z 是**相对适配盒**的倍数（内核 VideoRenderer.cpp:5913
' zoomedWidth = fittedWidth * zoom），而 z=1 时一个源像素占的屏幕像素数 f = Min(1, 适应比)。
' 三条界都由 f 派生：整图入窗 z=1；像素级 1:1 z=1/f；"每像素最多 64 屏幕像素" z=64/f。
' 这个 f **不是可选开关**：内核对静图恒定把适配盒钳在源原生尺寸
' （PlayerSession.cpp:3708/1642 → VideoRenderer.cpp:967-970/5479/2169/5900），
' 所以旧版注释里"进图片模式要打开封顶（A3）"那句作废 —— 那支导出是给视频逐帧检视用的。
' 推论：小图 f=1 ⇒ 适应窗口与 1:1 同档；大图 64/f > 64 被内核上钳吃掉 ⇒ 上限恒等于 64。

Imports System.Math

Public Module 图片缩放几何
    ''' <summary>内核滚轮入口 VideoRenderer.cpp:2377 的钳位（绝对入口 :2327-2328 在图片模式是 [1.0,32]，另一套数）。宿主必须与之一致：
    ''' 此前宿主用 [0.1, 32]、内核用 [0.05, 64]，两套数会把对方的合法值当边界。</summary>
    Public Const 内核最小缩放 As Single = 0.05F
    Public Const 内核最大缩放 As Single = 64.0F

    ''' <summary>对齐 LakeUI PixelPictureBox.vb:289 的「最大像素边长」默认 64：
    ''' 一个图片像素最多显示成 64 屏幕像素。</summary>
    Public Const 每像素最大屏幕边长 As Single = 64.0F

    ''' <summary>滚轮步进律。×1.25 与 ×0.8 互逆，所以往返同一步数必须回到原值。</summary>
    Public Const 缩放步进 As Single = 1.25F
    Public Const 反向步进 As Single = 0.8F
    Public Const 每刻度像素 As Integer = 120

    ''' <summary>弹簧收敛判定。位移阈值取**相对**目标值：临界阻尼的尾部是渐近的，
    ''' 绝对小阈值会把"已经看不出动"也算成未静止 —— 实测 阻尼0.18s + 绝对0.0015
    ''' 要 0.93 秒才停表，那是糊，不是阻尼。</summary>
    Public Const 静止位移相对比 As Single = 0.004F
    Public Const 静止速度相对比 As Single = 0.02F

    ''' <summary>缩放弹簧的阻尼时间。**按停表预算实测定的数**：0.05s 时一格 ×1.25
    ''' 约 0.2s 停表；0.18s 时约 0.9s。判据见 ImageZoomRegression 的"停表预算"条。</summary>
    Public Const 缩放阻尼时间秒 As Single = 0.05F

    ''' <summary>适应比 = 整图入窗时"一个源像素占多少屏幕像素"。</summary>
    Public Function 适应比(视口宽 As Integer, 视口高 As Integer,
                            源宽 As UInteger, 源高 As UInteger) As Single
        If 视口宽 <= 0 OrElse 视口高 <= 0 OrElse 源宽 = 0 OrElse 源高 = 0 Then Return 1.0F
        Dim 水平 = CSng(视口宽) / CSng(源宽)
        Dim 垂直 = CSng(视口高) / CSng(源高)
        Return Min(水平, 垂直)
    End Function

    ''' <summary>f：z=1 时一个源像素对应的屏幕像素数。封顶开启后不超过 1。</summary>
    Public Function 单位屏幕比(视口宽 As Integer, 视口高 As Integer,
                                源宽 As UInteger, 源高 As UInteger) As Single
        Return Min(1.0F, 适应比(视口宽, 视口高, 源宽, 源高))
    End Function

    ''' <summary>适配盒：z=1 时画面在屏幕上占多大（内核摆放与平移的基准，屏幕像素）。
    ''' 内核用的是这个而不是控件宽 —— offsetX = panX*|zoomedWidth−fittedWidth|/2 且
    ''' zoomedWidth = fittedWidth*zoom（VideoRenderer.cpp:5913/:5933）。非限制轴上
    ''' fittedWidth ≠ 控件宽，拿控件宽当分母会按比例算错平移（实测比值 0.7184 = 908/1264）。</summary>
    Public Function 适配盒宽(视口宽 As Integer, 视口高 As Integer,
                              源宽 As UInteger, 源高 As UInteger) As Single
        Return CSng(源宽) * 单位屏幕比(视口宽, 视口高, 源宽, 源高)
    End Function

    Public Function 适配盒高(视口宽 As Integer, 视口高 As Integer,
                             源宽 As UInteger, 源高 As UInteger) As Single
        Return CSng(源高) * 单位屏幕比(视口宽, 视口高, 源宽, 源高)
    End Function

    ''' <summary>缩放下界：封顶开启时恒为 1.0（整图入窗就是 z=1，绝不低于它 —— 对齐
    ''' PixelPictureBox.vb:554-566 的"图小于视口时下界取 1"，也躲开了内核 0.05 下钳）。</summary>
    Public Function 最小缩放() As Single
        Return 1.0F
    End Function

    ''' <summary>像素级 1:1 对应的缩放值 = 1/f。拿不到源尺寸时退回 1.0（=适应）。</summary>
    Public Function 一比一缩放(单位屏幕比 As Single) As Single
        If 单位屏幕比 <= 0.0F Then Return 1.0F
        Return Clamp(1.0F / 单位屏幕比, 最小可达缩放, 内核最大缩放)
    End Function

    ''' <summary>上界：受"每像素最多 64 屏幕像素"与内核 64 钳的双重限制，取更紧的那个。</summary>
    Public Function 最大缩放(单位屏幕比 As Single) As Single
        If 单位屏幕比 <= 0.0F Then Return 内核最大缩放
        Return Clamp(每像素最大屏幕边长 / 单位屏幕比, 内核最小缩放, 内核最大缩放)
    End Function

    Private Const 最小可达缩放 As Single = 内核最小缩放

    ''' <summary>把任意缩放值夹进 [下界, 上界]∩内核钳位。上界小于下界时以界本身为准，
    ''' 保证返回值仍落在内核能接受的区间里（否则内核会把它再夹一次，宿主显示与内核不一致）。</summary>
    Public Function 钳制缩放(值 As Single, 下界 As Single, 上界 As Single) As Single
        Dim 低 = Max(下界, 内核最小缩放)
        Dim 高 = Min(上界, 内核最大缩放)
        If 高 < 低 Then 高 = 低
        ' 先算界、再判值：非有限或 <=0（内核 ZoomViewAt 光盘分支不写出参 ⇒ 宿主回读到 0）
        ' 落到**已过内核下钳**的低界，而不是原始 下界 —— 否则下界被改成 0 时崩 UI 线程。
        If Not Single.IsFinite(值) OrElse 值 <= 0.0F Then Return 低
        Return Clamp(值, 低, 高)
    End Function

    ''' <summary>1:1 是否够得着：源边长 > 64 × 视口边长时 1/f 超过内核上钳，够不着。
    ''' 够不着必须显式告诉用户，不能把"停在 64×"说成"已到 1:1"。</summary>
    Public Function 一比一可达(视口宽 As Integer, 视口高 As Integer,
                                源宽 As UInteger, 源高 As UInteger) As Boolean
        Dim f = 单位屏幕比(视口宽, 视口高, 源宽, 源高)
        If f <= 0.0F Then Return False
        Return f >= (1.0F / 内核最大缩放)
    End Function

    ''' <summary>适应窗口与 1:1 是否同一档：源两轴都不超过视口（内核封顶分支是 AND，
    ''' VideoRenderer.cpp:970）。这类图双击本来就没有第二档可切 —— 必须说一句，
    ''' 否则用户读到的是"1:1 坏了"。</summary>
    Public Function 适应即一比一(视口宽 As Integer, 视口高 As Integer,
                                  源宽 As UInteger, 源高 As UInteger) As Boolean
        If 视口宽 <= 0 OrElse 视口高 <= 0 OrElse 源宽 = 0 OrElse 源高 = 0 Then Return False
        Return 源宽 <= CSng(视口宽) AndAlso 源高 <= CSng(视口高)
    End Function

    ''' <summary>是否存在可平移余量：显示尺寸超过视口才谈得上平移。
    ''' 控制器旧写法是"缩放<=1 就归零平移"，那只在对"铺满窗口"这一基准成立；
    ''' 打开原生尺寸封顶后小图的 z=1 并不铺满窗口，必须换成这个真实判据。</summary>
    Public Function 有平移余量(缩放 As Single, 单位屏幕比 As Single,
                                源宽 As UInteger, 源高 As UInteger,
                                视口宽 As Integer, 视口高 As Integer) As Boolean
        If 视口宽 <= 0 OrElse 视口高 <= 0 Then Return False
        Dim 显示宽 = CSng(源宽) * 单位屏幕比 * 缩放
        Dim 显示高 = CSng(源高) * 单位屏幕比 * 缩放
        Return 显示宽 > 视口宽 + 0.5F OrElse 显示高 > 视口高 + 0.5F
    End Function

    ''' <summary>临界阻尼弹簧一步（与 播放器360视角控制器 同族，但阻尼时间独立：
    ''' 缩放要"跟手且不过冲"，0.25s 的角度常数不适合直接借用）。
    ''' 间隔秒必须由实测经过时间给出 —— 拿计时器标称间隔当步长就是赌。</summary>
    Public Function 阻尼步进(当前值 As Single, 目标值 As Single,
                              ByRef 当前速度 As Single, 间隔秒 As Single,
                              阻尼时间秒 As Single) As Single
        If 间隔秒 <= 0.0F OrElse 阻尼时间秒 <= 0.0F Then Return 当前值
        Dim 角频率 = 2.0F / 阻尼时间秒
        Dim x = 角频率 * 间隔秒
        Dim 衰减 = 1.0F / (1.0F + x + 0.48F * x * x + 0.235F * x * x * x)
        Dim 差值 = 当前值 - 目标值
        Dim 临时速度 = (当前速度 + 角频率 * 差值) * 间隔秒
        当前速度 = (当前速度 - 角频率 * 临时速度) * 衰减
        Return 目标值 + (差值 + 临时速度) * 衰减
    End Function

    ''' <summary>是否已静止：位移与速度都要低过**按目标幅度缩放**的阈值。
    ''' 只量位移会在"慢慢爬"时提前停表，留下一个肉眼可见的错位终点；
    ''' 用绝对阈值则会在大倍数档位上迟迟不停表。</summary>
    Public Function 已静止(当前值 As Single, 目标值 As Single, 当前速度 As Single) As Boolean
        Dim 位移阈值 = Math.Max(0.0002F, Math.Abs(目标值) * 静止位移相对比)
        Dim 速度阈值 = Math.Max(0.0005F, Math.Abs(目标值) * 静止速度相对比)
        Return Abs(当前值 - 目标值) < 位移阈值 AndAlso Abs(当前速度) < 速度阈值
    End Function
End Module
