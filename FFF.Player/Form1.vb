Imports System.IO
Imports System.Runtime.InteropServices
Imports System.Threading

Partial Public Class Form1
    Private 光盘控制器 As 播放器光盘控制器
    Private Const WM_ENTERSIZEMOVE As Integer = &H231
    Private Const WM_EXITSIZEMOVE As Integer = &H232
    Private Const WM_APPCOMMAND As Integer = &H319
    Private Enum 多媒体操作
        下一项 = 11
        上一项 = 12
        停止 = 13
        播放暂停 = 14
        播放 = 46
        暂停 = 47
        快进 = 49
        快退 = 50
    End Enum
    Public Shared Property 当前主窗体 As Form1
    Private Const 跳转秒数 As Integer = 5
    Private Const HDR操作提示键 As String = "HDR模式"
    Private Shared ReadOnly 核心文件名称 As String() = {"FFF.Native.dll"}
    Private Shared ReadOnly FFmpeg核心文件前缀 As String() = {
        "avcodec",
        "avfilter",
        "avformat",
        "avutil",
        "swresample",
        "swscale"
    }

    Private 画面控件 As 播放器画面控件
    Private 播放控制器 As 播放器控制器
    Private 界面呈现器 As 播放器界面呈现器
    Private 窗口布局控制器 As 播放器窗口布局控制器
    Private 字幕图层呈现器 As 播放器字幕叠加呈现器
    Private 弹幕图层呈现器 As 播放器弹幕图层呈现器
    Private 歌词图层呈现器 As 播放器歌词呈现器
    Private 信息图层呈现器 As 播放器信息图层呈现器
    Private 剪辑区间控制器 As 播放器剪辑区间控制器
    Private 全屏交互控制器 As 播放器全屏交互控制器
    Private 流选择器 As 播放器流选择器
    Private 画面菜单控制器 As 播放器画面菜单控制器
    Private 视角360控制器 As 播放器360视角控制器
    Private 图片浏览控制器 As 播放器图片浏览控制器
    Private 视图旋转控制器 As 播放器视图旋转控制器
    Private 视频超分辨率控制器 As 播放器视频超分辨率控制器
    Private 视频自动HDR控制器 As 播放器视频自动HDR控制器
    Private 显示器唤醒 As 显示器唤醒请求
    Private 按钮图标 As 播放器按钮图标资源
    Private 设置窗口 As Form设置
    Private 媒体信息窗口 As Form媒体信息
    Private ReadOnly 播放列表数据 As New 播放列表 With {.播放模式 = 列表播放模式.顺序播放}
    Private 播放列表窗口 As Form播放列表
    Private 当前弹幕路径 As String = String.Empty
    Private 待打开外部文件 As String = String.Empty
    Private 待选中播放列表路径 As String = String.Empty
    Private 正在关闭 As Boolean
    Private 核心文件检查通过 As Boolean
    Private 核心文件错误说明 As String = String.Empty
    Private 正在显示HDR强制确认 As Boolean
    Private 已跳过HDR强制确认 As Boolean

    Private Event 方向键快捷键已请求 As KeyEventHandler

    Protected Overrides Sub WndProc(ByRef m As Message)
        If m.Msg = WM_APPCOMMAND AndAlso 窗口当前有焦点() Then
            Dim 命令 = CType((m.LParam.ToInt64() >> 16) And &HFFFL, 多媒体操作)
            If 处理多媒体命令(命令) Then
                m.Result = New IntPtr(1)
                Return
            End If
        End If
        If m.Msg = WM_ENTERSIZEMOVE Then 播放控制器?.设置窗口移动状态(True)
        MyBase.WndProc(m)
        If m.Msg = WM_EXITSIZEMOVE Then 播放控制器?.设置窗口移动状态(False)
    End Sub

    Private Sub Form1_Load(sender As Object, e As EventArgs) Handles MyBase.Load
        当前主窗体 = Me
        LakeUI.GlobalOptions.GlobalTextQuality = LakeUI.GlobalOptions.TextQualityMode.Outline
        LakeUI.MessageDialogOptions.BackdropEnabled = True
        LakeUI.MessageDialogOptions.BackdropMode = LakeUI.PopupBackdropMode.Auto
        LakeUI.MessageDialogOptions.BackdropTintColor = Color.FromArgb(120, 0, 0, 0)
        LakeUI.MessageDialogOptions.BackdropBlurRadius = 30
        LakeUI.MessageDialogOptions.BackdropBlurPasses = 2
        LakeUI.FloatingToolTipForm.BackdropEnabled = True
        LakeUI.FloatingToolTipForm.BackdropMode = LakeUI.PopupBackdropMode.Auto
        LakeUI.FloatingToolTipForm.BackdropTintColor = Color.FromArgb(120, 0, 0, 0)
        LakeUI.FloatingToolTipForm.BackdropBlurRadius = 30
        LakeUI.FloatingToolTipForm.BackdropBlurPasses = 2
        ThisIsYourWindow1.Attach(Me)
        KeyPreview = True
        MinimumSize = New Size(875, 500)
        更新弹幕按钮可见性()
        SP加载器.启动时加载()
        设置.启动时加载设置()
        文件关联管理器.启动后台同步(文件关联选项.从设置(设置.实例对象))
        字体控制.更新所有控件字体属性()
        设置.应用SP个性化设置()
        设置.加载SP自定义图标()
        AddHandler 播放列表数据.列表变化, AddressOf 播放列表数据_列表变化

        Dim 缺失文件 = 核心文件名称.Where(Function(文件名) Not 可以加载核心文件(文件名)).
            Concat(FFmpeg核心文件前缀.
                Where(Function(前缀) Not 可以加载核心文件(前缀)).
                Select(Function(前缀) $"{前缀}.dll 或 {前缀}-数字.dll")).
            ToArray()
        If 缺失文件.Length > 0 Then
            核心文件错误说明 = String.Join(vbCrLf, {
                "以下播放器核心文件缺失或无法加载：",
                String.Empty,
                String.Join(vbCrLf, 缺失文件.Select(Function(文件名) $"  {文件名}")),
                String.Empty,
                "请将完整且同一份的 Shared FFmpeg 的 DLL 放到程序目录或环境变量后重新启动。"})
            Return
        End If

        按钮图标 = 播放器按钮图标资源.加载()
        配置播放器按钮图标()

        画面控件 = New 播放器画面控件 With {.Dock = DockStyle.Fill}
        MP_DX视频容器.Controls.Add(画面控件)
        AddHandler 画面控件.文件拖入, AddressOf 画面控件_文件拖入

        播放控制器 = New 播放器控制器(
            Function() 画面控件.输出窗口句柄, SynchronizationContext.Current,
            CType(设置.实例对象.解码方式, 解码模式))
        显示器唤醒 = New 显示器唤醒请求()
        播放控制器.设置SDR峰值亮度(设置.实例对象.HDR映射SDR参考亮度)
        播放控制器.设置HDR峰值亮度(设置.实例对象.HDR峰值亮度)
        剪辑区间控制器 = New 播放器剪辑区间控制器(播放控制器, 画面控件,
            MB_剪辑区间模式, MP_剪辑区间操作容器, P_剪辑区间进度条容器,
            P_剪辑区间按钮容器, MB_传给3FUI)
        初始化VSP功能()
        更新WASAPI按钮()
        流选择器 = New 播放器流选择器(Me, MP_DX视频容器, MCM_流选择器, 播放控制器)
        流选择器.应用全局字体(设置.实例对象.字体)
        界面呈现器 = 创建界面呈现器()
        界面呈现器.设置音量百分比(设置.实例对象.音量百分比)
        播放控制器.设置音量(CSng(设置.实例对象.音量百分比 / 100.0F))
        字幕图层呈现器 = New 播放器字幕叠加呈现器(画面控件,
            AddressOf 播放控制器.安全读取快照, Function() If(字幕显示, 播放控制器.当前字幕, Nothing),
            AddressOf 播放控制器.提交定时文字图层, Function() If(字幕显示, Threading.Volatile.Read(当前次字幕), Nothing))
        ' 弹幕与字幕分轨：弹幕有自己的呈现器，节拍跟随视频（见 播放器弹幕图层呈现器）。
        ' 字幕仍走 播放器字幕叠加呈现器，保留它那套为静态内容设计的节拍与去重。
        弹幕图层呈现器 = New 播放器弹幕图层呈现器(画面控件,
            AddressOf 播放控制器.安全读取快照,
            AddressOf 播放控制器.提交弹幕图层,
            Function() If(设置.实例对象.弹幕已启用, 播放控制器.当前弹幕, Nothing),
            设置.实例对象.创建弹幕显示配置())
        歌词图层呈现器 = New 播放器歌词呈现器(画面控件,
            AddressOf 播放控制器.安全读取快照,
            Function() If(设置.实例对象.启用歌词支持, 播放控制器.当前歌词, Nothing),
            Function() 设置.实例对象.渲染封面图 AndAlso 播放控制器.当前音乐有封面,
            AddressOf 播放控制器.提交歌词图层, AddressOf 创建歌词呈现设置)
        AddHandler 画面控件.Ctrl滚轮, AddressOf 画面控件_Ctrl滚轮
        ' VSR 与自动 HDR 的逐帧判定态进 Tab 图层：内核静默跳过时，那是用户唯一能看见它的地方。
        ' 两者都现读，不走各自控制器的缓存——那份只在开页与勾选时更新，恰好错过内核真正
        ' 做判定的那一刻。文本由 播放器会话 侧组好（判词 + 一句人话），图层不再拼字段。
        信息图层呈现器 = New 播放器信息图层呈现器(画面控件,
            AddressOf 播放控制器.安全读取快照, AddressOf 播放控制器.安全读取媒体信息,
            Function() 播放控制器.当前媒体路径, Function() 播放控制器.当前字幕,
            Function() 播放控制器.当前弹幕, AddressOf 播放控制器.读取定时文字状态,
            AddressOf 播放控制器.读取弹幕状态, Function() 播放控制器.WASAPI模式,
            AddressOf 播放控制器.读取色彩变换内容描述,
            AddressOf 播放控制器.提交播放器信息图层,
            AddressOf 播放控制器.读取视频超分辨率图层文本,
            AddressOf 播放控制器.读取视频自动HDR图层文本)
        信息图层呈现器.应用全局字体(设置.实例对象.字体)
        窗口布局控制器 = New 播放器窗口布局控制器(Me, MP_DX视频容器, 画面控件,
            AddressOf 播放控制器.重绑输出窗口)
        画面菜单控制器 = New 播放器画面菜单控制器(
            Me, 画面控件, MCM_标题栏菜单, MCM_调整渲染区域大小, MCM_截取当前画面,
            窗口布局控制器, AddressOf 播放控制器.安全读取快照,
            Function() 设置.实例对象.取得初始画面尺寸(),
            Function() 播放控制器.当前媒体路径,
            Sub(文本) 信息图层呈现器?.显示操作信息(文本, &HFF69DF8BUI),
            Function() 播放控制器.读取原始帧(),
            Function() 播放控制器.当前视图旋转,
            AddressOf 播放控制器.安全读取媒体信息)
        视角360控制器 = New 播放器360视角控制器(
            Me, 画面控件, MCM_标题栏菜单,
            Sub(启用, 水平角度, 垂直角度, 视场角)
                播放控制器.设置360视角(启用, 水平角度, 垂直角度, 视场角)
            End Sub,
            Sub(文本) 信息图层呈现器?.显示操作信息(文本, &HFF69DF8BUI, "360°视频"))
        图片浏览控制器 = New 播放器图片浏览控制器(
            画面控件,
            Sub(缩放, 水平平移, 垂直平移)
                播放控制器.设置视图变换(缩放, 水平平移, 垂直平移)
            End Sub,
            Sub(方向) 播放相邻项目(方向),
            Sub(文本) 信息图层呈现器?.显示操作信息(文本, &HFF69DF8BUI, "图片"),
            AddressOf 图片光标锚定缩放,
            AddressOf 播放控制器.平移视图,
            Function() 播放控制器.当前视图旋转,
            AddressOf 播放控制器.读取图片缩放)
        ' 图片模式通过 Handled 接管左右方向键，用于切换图片。
        AddHandler 方向键快捷键已请求, AddressOf 图片浏览控制器.处理方向键快捷键
        AddHandler 图片浏览控制器.图片模式已变化, AddressOf 图片浏览控制器_图片模式已变化
        ' 内核在打开媒体时按源方向转正，控制器管理手动旋转及自动转正偏好。
        视图旋转控制器 = New 播放器视图旋转控制器(
            MCM_视图旋转,
            Sub(四分一转) 播放控制器.设置视图旋转(四分一转),
            Function() 播放控制器.当前视图旋转,
            Sub(文本) 信息图层呈现器?.显示操作信息(文本, &HFF69DF8BUI, "旋转"),
            设置.实例对象.自动转正,
            Sub(值) 设置.实例对象.自动转正 = 值)
        AddHandler 播放控制器.媒体已打开, AddressOf 视图旋转控制器.媒体已打开
        ' RTX 视频超分辨率：内核逐帧判定，宿主只负责"请求/取消"与把原因摊开。
        ' 媒体打开后重放偏好是必需的——VSR 请求是会话内状态，新会话从内核默认的 Off 起步。
        ' 入口已移到 设置 → 视频超分辨率 页（10-18），标题栏菜单不再挂它，避免两处开关。
        视频超分辨率控制器 = New 播放器视频超分辨率控制器(
            Sub(开启) 播放控制器.设置视频超分辨率(开启),
            Function() 播放控制器.视频超分辨率状态文本(),
            Function() 播放控制器.支持视频超分辨率,
            Sub(文本) 信息图层呈现器?.显示操作信息(文本, &HFF69DF8BUI, "超分辨率"),
            设置.实例对象.RTX视频超分辨率,
            Sub(值) 设置.实例对象.RTX视频超分辨率 = 值)
        AddHandler 播放控制器.媒体已打开, AddressOf 视频超分辨率控制器.媒体已打开
        ' NVIDIA 视频处理器私有流扩展自动 HDR（TrueHDR）：同样会话内状态、同样逐帧判定。
        ' 与 VSR 的入口同页并列，但概念、载荷、GUID 都不同，各自的开关互不代理。
        ' compare 模式由内核强制互斥（返回 InvalidState），宿主不尝试绕过。
        视频自动HDR控制器 = New 播放器视频自动HDR控制器(
            Sub(开启) 播放控制器.设置视频自动HDR(开启),
            Function() 播放控制器.视频自动HDR状态文本(),
            Function() 播放控制器.支持视频自动HDR,
            Sub(文本) 信息图层呈现器?.显示操作信息(文本, &HFF69DF8BUI, "自动 HDR"),
            设置.实例对象.NVIDIA自动HDR,
            Sub(值) 设置.实例对象.NVIDIA自动HDR = 值)
        AddHandler 播放控制器.媒体已打开, AddressOf 视频自动HDR控制器.媒体已打开
        画面菜单控制器.应用全局字体(设置.实例对象.字体)
        光盘控制器 = New 播放器光盘控制器(Me, 画面控件, 播放控制器, MCM_标题栏菜单)
        全屏交互控制器 = New 播放器全屏交互控制器(Me, 画面控件,
            ModernPanel1, MP_剪辑区间操作容器, Function() 剪辑区间控制器.模式已启用)

        AddHandler ThisIsYourWindow1.FullScreenChanged, AddressOf ThisIsYourWindow1_FullScreenChanged
        AddHandler 播放控制器.状态已变化, AddressOf 播放控制器_状态已变化
        AddHandler 播放控制器.媒体已打开, AddressOf 播放控制器_媒体已打开
        AddHandler 播放控制器.播放结束, AddressOf 播放控制器_播放结束
        AddHandler 播放控制器.媒体已打开, AddressOf 剪辑区间控制器.媒体已打开
        AddHandler 播放控制器.播放错误, AddressOf 播放控制器_播放错误
        AddHandler 播放控制器.操作提示, AddressOf 播放控制器_操作提示
        AddHandler 播放控制器.HDR输出状态已确认, AddressOf 播放控制器_HDR输出状态已确认
        AddHandler 播放控制器.外部字幕已加载, AddressOf 播放控制器_外部字幕已加载
        AddHandler 播放控制器.字幕选择已变化, AddressOf 播放控制器_字幕选择已变化
        AddHandler 播放控制器.外部弹幕已加载, AddressOf 播放控制器_外部弹幕已加载
        AddHandler 播放控制器.外部歌词已加载, AddressOf 播放控制器_外部歌词已加载
        AddHandler 界面呈现器.请求跳转到关键帧, AddressOf 界面呈现器_请求跳转到关键帧
        AddHandler 界面呈现器.音量已变更, AddressOf 界面呈现器_音量已变更
        AddHandler 界面呈现器.播放状态已刷新, AddressOf 剪辑区间控制器.播放状态已刷新
        AddHandler 剪辑区间控制器.模式已变化, AddressOf 剪辑区间控制器_模式已变化
        AddHandler MB_剪辑区间模式.Click, AddressOf 剪辑区间控制器.切换模式
        AddHandler P_剪辑区间按钮容器.SizeChanged, AddressOf 剪辑区间控制器.按钮容器大小已变化
        AddHandler 剪辑区间控制器.进度条.请求跳转, AddressOf 剪辑区间控制器.进度条请求跳转
        AddHandler MB_后退到关键帧.Click, AddressOf 剪辑区间控制器.后退到关键帧
        AddHandler MB_前进到关键帧.Click, AddressOf 剪辑区间控制器.前进到关键帧
        AddHandler MB_后退一帧.Click, AddressOf 剪辑区间控制器.后退一帧
        AddHandler MB_进一帧.Click, AddressOf 剪辑区间控制器.前进一帧
        AddHandler MB_设为入点.Click, AddressOf 剪辑区间控制器.设为入点
        AddHandler MB_设为出点.Click, AddressOf 剪辑区间控制器.设为出点
        AddHandler MB_去入点.Click, AddressOf 剪辑区间控制器.去入点
        AddHandler MB_去出点.Click, AddressOf 剪辑区间控制器.去出点
        AddHandler MB_传给3FUI.Click, AddressOf 剪辑区间控制器.传给3FUI
        AddHandler MB_停止.Click, AddressOf 剪辑区间控制器.停止已点击
        AddHandler 方向键快捷键已请求, AddressOf 剪辑区间控制器.处理方向键快捷键
        界面呈现器.启动()
        更新弹幕按钮状态()
        PerformLayout()
        窗口布局控制器.应用初始画面尺寸(设置.实例对象.取得初始画面尺寸())
        核心文件检查通过 = True

    End Sub

    Private Shared Function 可以加载核心文件(文件名 As String) As Boolean
        If FFmpeg核心文件前缀.Any(Function(前缀) String.Equals(前缀, 文件名, StringComparison.OrdinalIgnoreCase)) Then
            For Each 候选路径 In 枚举可加载核心文件(文件名)
                If 尝试加载核心文件(候选路径) Then Return True
            Next
            Return False
        End If

        Return 尝试加载核心文件(文件名)
    End Function

    Private Shared Function 尝试加载核心文件(文件名 As String) As Boolean
        Dim 句柄 = IntPtr.Zero
        Try
            If Path.IsPathFullyQualified(文件名) Then
                Return NativeLibrary.TryLoad(文件名, 句柄)
            End If
            Return NativeLibrary.TryLoad(文件名, GetType(Form1).Assembly, Nothing, 句柄)
        Catch ex As BadImageFormatException
            Return False
        Catch ex As DllNotFoundException
            Return False
        Finally
            If 句柄 <> IntPtr.Zero Then NativeLibrary.Free(句柄)
        End Try
    End Function

    Private Shared Iterator Function 枚举可加载核心文件(前缀 As String) As IEnumerable(Of String)
        Dim 已检查目录 As New List(Of String)
        Dim 目录列表 As New List(Of String) From {AppContext.BaseDirectory}
        Dim 程序集目录 = Path.GetDirectoryName(GetType(Form1).Assembly.Location)
        If Not String.IsNullOrWhiteSpace(程序集目录) Then 目录列表.Add(程序集目录)

        Dim 环境路径 = Environment.GetEnvironmentVariable("PATH")
        If Not String.IsNullOrWhiteSpace(环境路径) Then
            目录列表.AddRange(环境路径.Split(Path.PathSeparator).
                Select(Function(目录) 目录.Trim().Trim(""""c)).
                Where(Function(目录) Not String.IsNullOrWhiteSpace(目录)))
        End If

        For Each 目录 In 目录列表
            Dim 完整目录 As String
            Try
                完整目录 = Path.GetFullPath(目录)
                If 完整目录.Length > 1 Then
                    完整目录 = 完整目录.TrimEnd(Path.DirectorySeparatorChar, Path.AltDirectorySeparatorChar)
                End If
            Catch ex As Exception
                Continue For
            End Try
            If 已检查目录.Any(Function(已检查) String.Equals(已检查, 完整目录, StringComparison.OrdinalIgnoreCase)) Then Continue For
            已检查目录.Add(完整目录)

            Dim 候选文件 As IEnumerable(Of String)
            Try
                候选文件 = Directory.EnumerateFiles(完整目录, 前缀 & "*.dll", SearchOption.TopDirectoryOnly).
                    Where(Function(路径) 是支持的核心文件名(Path.GetFileName(路径), 前缀)).
                    OrderByDescending(Function(路径) String.Equals(Path.GetFileName(路径), 前缀 & ".dll", StringComparison.OrdinalIgnoreCase)).
                    ThenBy(Function(路径) Path.GetFileName(路径), StringComparer.OrdinalIgnoreCase).
                    ToArray()
            Catch ex As IOException
                Continue For
            Catch ex As UnauthorizedAccessException
                Continue For
            End Try

            For Each 候选路径 In 候选文件
                Yield 候选路径
            Next
        Next
    End Function

    Private Shared Function 是支持的核心文件名(文件名 As String, 前缀 As String) As Boolean
        If String.IsNullOrEmpty(文件名) OrElse
            Not 文件名.EndsWith(".dll", StringComparison.OrdinalIgnoreCase) OrElse
            Not 文件名.StartsWith(前缀, StringComparison.OrdinalIgnoreCase) Then Return False

        Dim 后缀 = 文件名.Substring(前缀.Length, 文件名.Length - 前缀.Length - 4)
        If 后缀.Length = 0 Then Return True
        If Not 后缀.StartsWith("-", StringComparison.Ordinal) OrElse 后缀.Length = 1 Then Return False
        Return 后缀.Skip(1).All(Function(字符) 字符 >= "0"c AndAlso 字符 <= "9"c)
    End Function

    Private Function 创建界面呈现器() As 播放器界面呈现器
        Return New 播放器界面呈现器(
            ETB_媒体进度条, ETB_音量条, MB_播放和暂停,
            按钮图标.取得(播放器按钮图标.播放), 按钮图标.取得(播放器按钮图标.暂停),
            MB_软件解码或硬件解码, MB_HDR模式,
            MB_当前视频编码显示, MB_当前音频编码显示, MB_当前声道数显示, HCL_时间戳显示, Panel4,
            JEC_HDR选项前面的空白占位, JEC_当前视频编码显示前面的空白占位,
            JEC_当前音频编码显示前面的空白占位, JEC_当前声道数显示前面的空白占位, 画面控件,
            AddressOf 播放控制器.安全读取快照,
            Function() 播放控制器.是否正在切换,
            Function() 播放控制器.解码器)
    End Function

    Private Sub 配置播放器按钮图标()
        按钮图标.应用(MB_播放和暂停, 播放器按钮图标.播放)
        按钮图标.应用(MB_停止, 播放器按钮图标.停止)
        按钮图标.应用(MB_倒退或上一个, 播放器按钮图标.倒退或上一个)
        按钮图标.应用(MB_快进或下一个, 播放器按钮图标.前进或下一个)
        按钮图标.应用(MB_打开文件, 播放器按钮图标.打开)
        按钮图标.应用(MB_软件设置, 播放器按钮图标.设置)
        按钮图标.应用(MB_播放列表, 播放器按钮图标.播放列表)
        按钮图标.应用(MB_剪辑区间模式, 播放器按钮图标.剪辑区间)
        按钮图标.应用(MB_查看当前媒体信息, 播放器按钮图标.元数据)
        按钮图标.应用(MB_选择流, 播放器按钮图标.流选择)
    End Sub

    Private Sub Form1_Shown(sender As Object, e As EventArgs) Handles Me.Shown
        If Not 核心文件检查通过 Then
            BeginInvoke(Sub()
                            LakeUI.ExOverlayMsgBox(Me, 核心文件错误说明,
                                MsgBoxStyle.Critical Or MsgBoxStyle.OkOnly, "播放器核心文件缺失")
                            Close()
                        End Sub)
            Return
        End If

        启动时探测色彩变换模块()

        Dim 启动文件 = My.Application.取出待处理启动文件()
        Dim 请求文件 = If(String.IsNullOrEmpty(待打开外部文件), 启动文件, 待打开外部文件)
        待打开外部文件 = String.Empty
        If Not String.IsNullOrEmpty(请求文件) Then BeginInvoke(Sub() 打开外部文件(请求文件))
    End Sub

    ' ═══════════════════════════════════════════════════════════════════════════
    '  外部色彩变换模块（FFF.HDRdp.Test.dll）：启动探测 + 限时解锁输码（每进程一次）
    '
    '  ● 用户裁定（2026-10-18 二次修正，最终口径）：输码 UI 是本体必须内容；弹出规则——
    '    ① 启动后 1s~60s 窗口内检测到 DLL 且未解锁 ⇒ 弹一次；超过 60s 一律不再弹；
    '    ② 每进程至多一次：取消 / 输完（成功或错满）后本次会话不再弹，重启软件才恢复；
    '    ③ 没 DLL / 旧件 / 已解锁 ⇒ 不弹（探测只回答"要不要弹"）。
    '  ● 解锁入口只此一处：码 → 模块铸票（票据落 exe 旁或 %LOCALAPPDATA%\FFF.HDRdp.Test）；
    '    解锁后本体行为才改变（kind=3 注入 + 「内容：」行）。
    '  ● 三条纪律：① 解锁码绝不进任何日志——只记"接受 / 拒绝 / 够不到"；② 文案用本体
    '    自有的中性措辞，不引用模块自带文本；③ 探测/轮询异常静默吞掉，不让启动链断在这里。
    ' ═══════════════════════════════════════════════════════════════════════════
    Private Const 解锁最大尝试次数 As Integer = 5
    Private Const 解锁弹窗最早毫秒 As Long = 1000
    Private Const 解锁弹窗最晚毫秒 As Long = 60000

    Private 解锁轮询计时器 As System.Windows.Forms.Timer
    Private 解锁轮询起点 As Long
    Private 解锁轮询已收工 As Boolean

    ''' <summary>启动探测（只记调试读数）并排期解锁输码轮询：1s 起、60s 截止、每进程最多弹一次。</summary>
    Private Sub 启动时探测色彩变换模块()
        Try
            Dim 原因 As String = Nothing
            Debug.WriteLine("色彩变换模块：" & 色彩变换模块接口.探测(原因).ToString() &
                If(String.IsNullOrEmpty(原因), "", "（" & 原因 & "）"))
        Catch ex As Exception
            Debug.WriteLine("色彩变换模块：启动时探测异常（已忽略）：" & ex.GetType().Name)
        End Try

        解锁轮询起点 = Environment.TickCount64
        解锁轮询已收工 = False
        解锁轮询计时器 = New System.Windows.Forms.Timer With {.Interval = 1000}
        AddHandler 解锁轮询计时器.Tick, AddressOf 解锁轮询_Tick
        解锁轮询计时器.Start()
    End Sub

    Private Sub 解锁轮询_Tick(sender As Object, e As EventArgs)
        If 解锁轮询已收工 Then
            停止解锁轮询()
            Return
        End If
        Try
            Dim 已过毫秒 = Environment.TickCount64 - 解锁轮询起点
            If 已过毫秒 < 解锁弹窗最早毫秒 Then Return
            If 已过毫秒 > 解锁弹窗最晚毫秒 Then
                解锁轮询已收工 = True
                停止解锁轮询()
                Debug.WriteLine("色彩变换模块：解锁提示窗口（启动后 60 秒）已过且未弹，本进程不再提示")
                Return
            End If

            Dim 原因 As String = Nothing
            Select Case 色彩变换模块接口.探测(原因)
                Case 色彩变换模块接口.模块在场形态.已锁定
                    ' 命中唯一该弹的一格：先收工再生弹，保证"每进程至多一次"。
                    解锁轮询已收工 = True
                    停止解锁轮询()
                    Debug.WriteLine("色彩变换模块：在场未解锁，弹出输码（每进程仅一次）")
                    解锁对话框循环()
                Case 色彩变换模块接口.模块在场形态.缺席
                    ' DLL 可能稍后才出现：窗口内继续等。
                Case Else
                    ' 已解锁 / 旧件：没有可解锁的路径，收工。
                    解锁轮询已收工 = True
                    停止解锁轮询()
            End Select
        Catch ex As Exception
            ' 轮询异常不打扰用户：下一跳再试，窗口到点自然收工。
            Debug.WriteLine("色彩变换模块：解锁轮询异常（已忽略）：" & ex.GetType().Name)
        End Try
    End Sub

    Private Sub 停止解锁轮询()
        If 解锁轮询计时器 IsNot Nothing Then
            解锁轮询计时器.Stop()
            解锁轮询计时器.Dispose()
            解锁轮询计时器 = Nothing
        End If
    End Sub

    ''' <summary>输码闭环：输入 → 提交 → 错了再来（有上限）→ 接受或放弃。码不落日志。
    ''' 只由 <see cref="解锁轮询_Tick"/> 调用（每进程至多一次）。</summary>
    Private Sub 解锁对话框循环()
        For 尝试 As Integer = 1 To 解锁最大尝试次数
            Dim 提示文本 As String
            If 尝试 = 1 Then
                提示文本 = "检测到色彩变换模块，但尚未解锁。" & vbCrLf &
                    "请输入解锁码；留空或取消则本次按基础画面播放。"
            Else
                提示文本 = "解锁码不正确，请重新输入。（第 " & 尝试.ToString() & " / " &
                    解锁最大尝试次数.ToString() & " 次）" & vbCrLf &
                    "留空或取消则本次按基础画面播放。"
            End If

            Dim 码 = LakeUI.ExInputBox(Me, 提示文本, "色彩变换模块解锁")
            If String.IsNullOrWhiteSpace(码) Then Return   ' 取消 / 空输入 ⇒ 放弃，静默

            Dim 详情 As String = Nothing
            Select Case 色彩变换模块接口.提交解锁码(码.Trim(), 详情)
                Case 色彩变换模块接口.解锁结果.已接受
                    Debug.WriteLine("色彩变换模块：解锁码已被模块接受（票据已落盘）")
                    ' 泵的补装载预算可能已经用尽（用户输码慢于 60 秒）：显式唤醒，
                    ' 否则这一场播放不会开始注入，而用户已经看到“已解锁”。
                    播放控制器?.唤醒色彩变换通路()
                    LakeUI.ExOverlayMsgBox(Me, "色彩变换模块已解锁。",
                        MsgBoxStyle.Information Or MsgBoxStyle.OkOnly, "色彩变换模块解锁")
                    Return

                Case 色彩变换模块接口.解锁结果.已接受未落盘
                    ' 码是对的，但票据写不进目标位置（常见于装在不可写目录）：
                    ' 本次进程能用，重启后要重输。这句必须说清，否则用户以为"没成功"。
                    Debug.WriteLine("色彩变换模块：解锁码已被模块接受（票据未落盘，仅本次进程）")
                    ' 这条路径的授权只活在本进程的内存里，唤醒同样必须做——
                    ' 否则用户会看到"已通过"但画面毫无变化。
                    播放控制器?.唤醒色彩变换通路()
                    LakeUI.ExOverlayMsgBox(Me,
                        "解锁码已通过，但票据无法写入目标位置：本次运行可用，重启后需要重新输入。",
                        MsgBoxStyle.Exclamation Or MsgBoxStyle.OkOnly, "色彩变换模块解锁")
                    Return

                Case 色彩变换模块接口.解锁结果.被拒
                    ' 码不对：换文案再给一次机会（详情只进调试输出，不含码）。
                    Debug.WriteLine("色彩变换模块：解锁码被拒" &
                        If(String.IsNullOrEmpty(详情), "", "（" & 详情 & "）"))
                    Continue For

                Case Else
                    ' 够不到模块 / 不含解锁入口：这不是"码对不对"的问题，
                    ' 再弹下去只是让用户白试，直接放弃并回到静默。
                    Debug.WriteLine("色彩变换模块：无法提交解锁码" &
                        If(String.IsNullOrEmpty(详情), "", "（" & 详情 & "）"))
                    Return
            End Select
        Next
        ' 连续错满上限：放弃，回到"本次按基础层出画"，不再打扰（重启后恢复）。
        Debug.WriteLine("色彩变换模块：连续输错满 " & 解锁最大尝试次数.ToString() & " 次，放弃")
    End Sub


    Private Sub Form1_FormClosed(sender As Object, e As FormClosedEventArgs) Handles Me.FormClosed
        正在关闭 = True
        播放列表数据.取消自动扫描()
        停止解锁轮询()
        设置.退出时保存设置()
        RemoveHandler ThisIsYourWindow1.FullScreenChanged, AddressOf ThisIsYourWindow1_FullScreenChanged
        全屏交互控制器?.Dispose()
        视角360控制器?.Dispose()
        RemoveHandler 方向键快捷键已请求, AddressOf 图片浏览控制器.处理方向键快捷键
        If 图片浏览控制器 IsNot Nothing Then RemoveHandler 图片浏览控制器.图片模式已变化, AddressOf 图片浏览控制器_图片模式已变化
        图片浏览控制器?.Dispose()
        If 视图旋转控制器 IsNot Nothing Then
            If 播放控制器 IsNot Nothing Then
                ' 与安装处同一个方法组；显式委托实例避免 BC42328 的宽松转换歧义。
                RemoveHandler 播放控制器.媒体已打开,
                    New EventHandler(Of 播放器媒体事件参数)(AddressOf 视图旋转控制器.媒体已打开)
            End If
            视图旋转控制器.Dispose()
        End If
        If 视频超分辨率控制器 IsNot Nothing Then
            If 播放控制器 IsNot Nothing Then
                RemoveHandler 播放控制器.媒体已打开,
                    New EventHandler(Of 播放器媒体事件参数)(AddressOf 视频超分辨率控制器.媒体已打开)
            End If
            视频超分辨率控制器.Dispose()
        End If
        If 视频自动HDR控制器 IsNot Nothing Then
            If 播放控制器 IsNot Nothing Then
                RemoveHandler 播放控制器.媒体已打开,
                    New EventHandler(Of 播放器媒体事件参数)(AddressOf 视频自动HDR控制器.媒体已打开)
            End If
            视频自动HDR控制器.Dispose()
        End If
        光盘控制器?.Dispose()
        画面菜单控制器?.Dispose()
        窗口布局控制器?.释放()
        界面呈现器?.释放()
        信息图层呈现器?.释放()
        歌词图层呈现器?.Dispose()
        弹幕图层呈现器?.释放()
        字幕图层呈现器?.释放()
        流选择器?.Dispose()
        媒体信息窗口?.Close()
        媒体信息窗口 = Nothing
        播放列表窗口?.Dispose()
        显示器唤醒?.释放()
        播放控制器?.释放()
        播放器按钮图标资源.清除(MB_播放和暂停, MB_停止, MB_倒退或上一个, MB_快进或下一个,
            MB_打开文件, MB_软件设置, MB_播放列表, MB_剪辑区间模式, MB_查看当前媒体信息, MB_选择流)
        按钮图标?.Dispose()
        设置.释放SP资源()
        当前主窗体 = Nothing
    End Sub

    Private Sub ThisIsYourWindow1_FullScreenChanged(
        sender As Object, e As LakeUI.ThisIsYourWindow.FullScreenChangedEventArgs)
        If e.HostForm Is Me AndAlso Not 正在关闭 Then 全屏交互控制器?.设置全屏状态(e.IsFullScreen)
    End Sub

    Private Sub 剪辑区间控制器_模式已变化(sender As Object, e As 剪辑区间模式变化事件参数)
        界面呈现器.设置精确时间戳(e.已启用)
        全屏交互控制器?.剪辑区间模式已变化()
    End Sub

    Private Sub 图片浏览控制器_图片模式已变化(sender As Object, e As EventArgs)
        界面呈现器?.设置图片模式(图片浏览控制器.图片模式已启用)
    End Sub

    ''' <summary>光标锚定缩放：交给 3FP 内核执行，它在拟合盒与平移映射上信息完整，
    ''' 能保证锚点下的内容不动；宿主自己改缩放只能绕画面中心放大，光标处会漂走。
    ''' 旧内核没有 FFF3FP_ZoomViewAt 时返回 False，控制器回退到旧行为。</summary>
    Private Function 图片光标锚定缩放(倍数 As Single, 锚点水平 As Single, 锚点垂直 As Single,
                                       ByRef 结果缩放 As Single) As Boolean
        If 播放控制器 Is Nothing Then Return False
        Return 播放控制器.光标锚定缩放(倍数, 锚点水平, 锚点垂直, 结果缩放)
    End Function

    Private Sub MB_打开文件_Click(sender As Object, e As EventArgs) Handles MB_打开文件.Click
        If 正在关闭 Then Return
        Using 对话框 As New OpenFileDialog With {
            .CheckFileExists = True,
            .Filter = "所有文件|*.*",
            .Multiselect = True,
            .RestoreDirectory = True,
            .Title = "打开媒体或替换歌词/字幕/弹幕"
        }
            If 对话框.ShowDialog(Me) = DialogResult.OK Then 打开或替换文件(对话框.FileNames)
        End Using
    End Sub

    Private Sub 画面控件_文件拖入(sender As Object, e As 播放器文件拖入事件参数)
        If e Is Nothing OrElse e.文件路径 Is Nothing Then Return
        打开或替换文件(e.文件路径)
    End Sub

    Friend Sub 打开命令行文件(参数 As IEnumerable(Of String))
        Dim 文件路径 = 取得命令行文件(参数)
        If String.IsNullOrEmpty(文件路径) OrElse 正在关闭 Then Return
        If InvokeRequired Then
            BeginInvoke(Sub() 打开命令行文件({文件路径}))
            Return
        End If
        打开外部文件(文件路径)
    End Sub

    Friend Shared Function 取得命令行文件(参数 As IEnumerable(Of String)) As String
        If 参数 Is Nothing Then Return String.Empty
        Dim 文件路径 = 参数.FirstOrDefault(AddressOf 光盘路径.媒体存在)
        Return If(String.IsNullOrEmpty(文件路径), String.Empty, Path.GetFullPath(文件路径))
    End Function

    Private Sub 打开外部文件(文件路径 As String)
        If 正在关闭 OrElse String.IsNullOrEmpty(文件路径) Then Return
        If Not 核心文件检查通过 OrElse 播放控制器 Is Nothing Then
            待打开外部文件 = 文件路径
            Return
        End If
        If WindowState = FormWindowState.Minimized Then WindowState = FormWindowState.Normal
        If Not Visible Then Show()
        Activate()
        BringToFront()
        打开或替换文件(文件路径)
    End Sub

    Private Sub 打开或替换文件(路径 As IEnumerable(Of String))
        If 路径 Is Nothing Then Return
        Dim 存在的文件 = 路径.Where(Function(x) Not String.IsNullOrWhiteSpace(x) AndAlso
                                             光盘路径.媒体存在(x)).
            Select(Function(x) Path.GetFullPath(x)).
            Distinct(StringComparer.OrdinalIgnoreCase).
            ToArray()
        If 存在的文件.Length = 0 Then Return
        If 存在的文件.Length > 1 Then
            添加并打开多个媒体文件(存在的文件)
            Return
        End If
        打开或替换单个文件(存在的文件(0))
    End Sub

    Private Sub 打开或替换文件(路径 As String)
        If String.IsNullOrWhiteSpace(路径) Then Return
        打开或替换单个文件(Path.GetFullPath(路径))
    End Sub

    Private Sub 添加并打开多个媒体文件(路径 As IEnumerable(Of String))
        Dim 媒体文件 = 路径.Where(Function(x) File.Exists(x) AndAlso
                                      播放列表.是支持的媒体文件(x)).ToArray()
        If 媒体文件.Length = 0 Then Return

        播放列表数据.取消自动扫描()
        播放列表数据.添加多个(媒体文件)
        Dim 首个路径 = 媒体文件(0)
        播放列表数据.选择路径(首个路径)
        播放控制器.打开媒体(首个路径)
    End Sub

    Private Sub 打开或替换单个文件(路径 As String)
        If LRC歌词自动加载器.是支持的歌词文件(路径) Then
            播放控制器.替换歌词(路径)
        ElseIf 外部字幕自动加载器.是支持的字幕文件(路径) Then
            播放控制器.替换字幕(路径)
        ElseIf 弹幕自动加载器.是支持的弹幕文件(路径) Then
            播放控制器.替换弹幕(路径)
        ElseIf 外部音频自动加载器.是支持的音频文件(路径) AndAlso
            播放控制器.当前媒体是视频 Then
            播放控制器.加载外部音轨(路径)
        Else
            If Not 光盘路径.是光盘路径(路径) Then
                启动后台任务(播放列表数据.从媒体扫描同目录Async(路径))
            Else
                播放列表数据.取消自动扫描()
            End If
            播放控制器.打开媒体(路径)
        End If
    End Sub

    Private Sub 播放控制器_状态已变化(sender As Object, e As EventArgs)
        If Not 正在关闭 Then
            显示器唤醒?.更新(播放控制器.安全读取快照())
            界面呈现器.刷新()
            更新WASAPI按钮()
        End If
    End Sub

    Private Sub 播放控制器_媒体已打开(sender As Object, e As 播放器媒体事件参数)
        If 正在关闭 Then Return
        显示器唤醒?.更新(e.快照)
        If Not 播放列表数据.选择路径(e.文件路径) Then
            待选中播放列表路径 = e.文件路径
        Else
            待选中播放列表路径 = String.Empty
        End If
        播放列表窗口?.更新正在播放项()
        当前弹幕路径 = String.Empty
        更新弹幕按钮可见性()
        字幕图层呈现器?.使图层失效()
        弹幕图层呈现器?.使图层失效()
        歌词图层呈现器?.使图层失效()
        信息图层呈现器?.使内容失效()
        Dim 光盘标题 = If(光盘路径.是光盘路径(e.文件路径) AndAlso Directory.Exists(e.文件路径), e.文件路径.TrimEnd("\"c, "/"c), Path.GetFileName(e.文件路径))
        Text = 光盘标题
        界面呈现器.媒体已打开(e.保留剪辑区间)
        界面呈现器.更新媒体信息(e.媒体信息, e.快照)
        ' IAMF 空间音频声明（docs/52 验收 #6 的产品拍板：UI 声明即可）。
        ' 调试层的"声场："行要手动开信息层才可见；libiamf 接管与否决定用户听到
        ' 的是沉浸声渲染还是降级下混，值得在打开媒体时主动声明一次。
        ' 内容声道数小于渲染目标时补一句（libiamf 对缺失声道填数字静音），
        ' 避免 7.1 素材被声明成 7.1.4 而四只顶置音箱实际无声。
        If e.快照 IsNot Nothing AndAlso e.快照.IAMF已接管 Then
            Dim 布局 = e.快照.IAMF布局名
            Dim 声明 = If(String.IsNullOrEmpty(布局),
                $"IAMF 空间音频 · {e.快照.IAMF声道数} 声道",
                $"IAMF 空间音频 · {布局}（{e.快照.IAMF声道数} 声道）")
            If e.快照.IAMF内容声道数 > 0 AndAlso
                e.快照.IAMF内容声道数 < e.快照.IAMF声道数 Then
                声明 &= $"，内容 {e.快照.IAMF内容声道数} 声道"
            End If
            信息图层呈现器?.显示操作信息(声明, &HFF55E7EAUI, "IAMF")
        End If
        视角360控制器?.媒体已打开(e.文件路径, e.媒体信息, e.快照)
        图片浏览控制器?.媒体已打开(e.媒体信息, e.快照)
        界面呈现器.设置图片模式(图片浏览控制器 IsNot Nothing AndAlso 图片浏览控制器.图片模式已启用)
        界面呈现器.刷新()
    End Sub

    Private Sub 播放控制器_播放结束(sender As Object, e As EventArgs)
        If 光盘路径.是光盘路径(播放控制器.当前媒体路径) Then Return
        If 正在关闭 Then Return
        Dim 下一项 = 播放列表数据.移动到播放结束后的项目()
        If 下一项 IsNot Nothing Then
            播放列表数据.取消自动扫描()
            播放控制器.打开媒体(下一项.路径)
        End If
    End Sub

    Private Sub 播放控制器_播放错误(sender As Object, e As 播放器错误事件参数)
        If Not 正在关闭 Then
            LakeUI.ExOverlayMsgBox(Me, e.消息, MsgBoxStyle.Critical Or MsgBoxStyle.OkOnly, e.标题)
        End If
    End Sub

    Private Sub 播放控制器_操作提示(sender As Object, e As 播放器操作提示事件参数)
        If 正在关闭 Then Return
        If e.弹出提示 Then
            LakeUI.ExOverlayMsgBox(Me, e.说明, MsgBoxStyle.Exclamation Or MsgBoxStyle.OkOnly, e.标题)
        Else
            信息图层呈现器?.显示操作信息(e.说明, &HFFF0D35DUI, "解码回退")
        End If
    End Sub

    <CodeAnalysis.SuppressMessage("Performance", "CA1861:不要将常量数组作为参数", Justification:="<挂起>")>
    Private Sub 播放控制器_HDR输出状态已确认(sender As Object, e As 播放器HDR状态事件参数)
        If 正在关闭 Then Return
        信息图层呈现器?.显示操作信息(e.说明, &HFF69DF8BUI, HDR操作提示键,
                            AddressOf 播放控制器.读取HDR操作提示)
        If Not e.可以强制开启 OrElse 正在显示HDR强制确认 OrElse 已跳过HDR强制确认 Then Return

        正在显示HDR强制确认 = True
        Try
            Dim 内容 = "Windows 未将当前显示设备报告为可用的 HDR 输出。部分智能电视不会向 Windows 提供完整、规范的 HDR 能力信息。可以尝试忽略检测结果强行创建真实 HDR 高亮输出。强制开启可能出现颜色异常、亮度错误或短暂黑屏；如果驱动拒绝 scRGB 色彩空间，播放器仍会自动回退到 SDR。"
            Dim 结果 = LakeUI.ExOverlayMsgBox(Me, 内容,
                {"忽略检测并强制开启", "保持 SDR"},
                "当前显示设备未报告 HDR 支持",
                MsgBoxStyle.Exclamation, 1)
            If 结果 = 0 AndAlso Not 正在关闭 Then
                播放控制器.强制开启HDR模式()
            Else
                已跳过HDR强制确认 = True
            End If
        Finally
            正在显示HDR强制确认 = False
        End Try
    End Sub

    Private Sub 播放控制器_外部字幕已加载(sender As Object, e As 播放器字幕事件参数)
        If 正在关闭 Then Return
        字幕图层呈现器?.使图层失效()
        信息图层呈现器?.显示操作信息($"已加载 {e.格式.ToString().ToUpperInvariant()} 字幕 · {Path.GetFileName(e.路径)}", &HFF55E7EAUI)
        信息图层呈现器?.使内容失效()
    End Sub

    Private Sub 播放控制器_字幕选择已变化(sender As Object, e As EventArgs)
        If Not 正在关闭 Then
            字幕图层呈现器?.使图层失效()
            信息图层呈现器?.使内容失效()
        End If
    End Sub

    Private Sub MB_选择流_Click(sender As Object, e As EventArgs) Handles MB_选择流.Click
        If Not 正在关闭 Then 流选择器?.显示()
    End Sub

    Private Sub MB_查看当前媒体信息_MouseClick(sender As Object, e As MouseEventArgs) Handles MB_查看当前媒体信息.MouseClick
        If 正在关闭 Then Return
        Select Case e.Button
            Case MouseButtons.Left
                显示媒体信息窗口()
            Case MouseButtons.Right
                切换媒体信息层()
        End Select
    End Sub

    Private Sub 显示媒体信息窗口()
        If 媒体信息窗口 Is Nothing OrElse 媒体信息窗口.IsDisposed Then
            媒体信息窗口 = New Form媒体信息(
                AddressOf 播放控制器.安全读取媒体信息,
                AddressOf 播放控制器.安全读取快照,
                AddressOf 播放控制器.读取定时文字状态,
                AddressOf 播放控制器.读取弹幕状态,
                Function() 播放控制器.当前字幕,
                Function() 播放控制器.当前弹幕,
                Function() 播放控制器.WASAPI模式,
                Function() 画面控件.ClientSize,
                AddressOf 播放控制器.读取音频峰值,
                AddressOf 播放控制器.读取输入音频峰值)
            AddHandler 媒体信息窗口.FormClosed,
                Sub(sender, args)
                    If Object.ReferenceEquals(sender, 媒体信息窗口) Then 媒体信息窗口 = Nothing
                End Sub
        End If
        媒体信息窗口.Location = 媒体信息窗口.居中于(Bounds)
        If 媒体信息窗口.WindowState = FormWindowState.Minimized Then
            媒体信息窗口.WindowState = FormWindowState.Normal
        End If
        If Not 媒体信息窗口.Visible Then 媒体信息窗口.Show(Me)
        媒体信息窗口.Activate()
        媒体信息窗口.BringToFront()
    End Sub

    Private Sub 切换媒体信息层()
        信息图层呈现器?.切换调试信息()
        MB_查看当前媒体信息.BackColor1 = Color.Transparent
        If 画面控件 IsNot Nothing AndAlso 画面控件.CanFocus Then 画面控件.Focus()
    End Sub

    Private Sub MB_当前声道数显示_Click(sender As Object, e As EventArgs) Handles MB_当前声道数显示.Click
        If 正在关闭 OrElse Not 播放控制器.是否有媒体 Then Return
        Dim 原模式 = 播放控制器.WASAPI模式
        If 原模式 = WASAPI共享模式.共享 AndAlso Not 确认切换到WASAPI独占模式() Then Return
        If 正在关闭 OrElse Not 播放控制器.是否有媒体 OrElse 播放控制器.WASAPI模式 <> 原模式 Then Return
        播放控制器.切换WASAPI模式()
        信息图层呈现器?.显示操作信息(If(原模式 = WASAPI共享模式.共享,
            "正在切换到 WASAPI 独占模式", "正在切换到 WASAPI 共享模式"), &HFFFFA85AUI)
    End Sub

    <CodeAnalysis.SuppressMessage("Performance", "CA1861:不要将常量数组作为参数", Justification:="<挂起>")>
    Private Function 确认切换到WASAPI独占模式() As Boolean
        Dim 内容 = String.Join(vbCrLf, {String.Empty,
            "是否切换到 WASAPI 独占模式？",
            String.Empty,
            "独占模式直通设备，能提供理论最佳音质，但也有诸多要求和限制：",
            String.Empty,
            "1. 再次提醒，如果您正戴着耳机，请立刻取下！",
            "2. 无法通过系统控制输出音量，请调整硬件设备旋钮或按键",
            "3. 其他应用无法发出任何声音，可能导致您错过重要事项",
            "4. 如果已经有应用占用了，则本应用会失败",
            "5. 安装在系统中的音效软件无法在独占模式工作",
            "6. 硬件设备必须支持对应的音频输出规格才能正常工作"})
        Return LakeUI.ExOverlayMsgBox(Me, 内容,
            {"我已取下耳机并确认切换独占模式", "现在不"},
            "如果您正戴着耳机，请立即取下！",
            MsgBoxStyle.Exclamation, 1) = 0
    End Function

    Private Sub 更新WASAPI按钮()
        If 播放控制器 Is Nothing Then Return
        MB_当前声道数显示.ForeColor = If(播放控制器.WASAPI模式 = WASAPI共享模式.独占, Color.IndianRed, Color.Silver)
    End Sub

    Private Sub 播放控制器_外部弹幕已加载(sender As Object, e As 播放器弹幕事件参数)
        If 正在关闭 Then Return
        当前弹幕路径 = e.路径
        更新弹幕按钮可见性()
        弹幕图层呈现器?.使图层失效()
        信息图层呈现器?.显示操作信息($"已加载 {e.数量} 条弹幕 · {Path.GetFileName(e.路径)}", &HFFFFA85AUI)
        信息图层呈现器?.使内容失效()
    End Sub

    Friend Sub 应用HDR峰值设置()
        播放控制器?.设置HDR峰值亮度(设置.实例对象.HDR峰值亮度)
    End Sub

    Friend Sub 应用SDR亮度设置()
        播放控制器?.设置SDR峰值亮度(设置.实例对象.HDR映射SDR参考亮度)
    End Sub

    Friend Sub 应用自动HDR模式设置()
        播放控制器?.应用自动HDR模式设置()
    End Sub

    Friend Sub 应用空间音频设置()
        播放控制器?.应用空间音频设置()
    End Sub

    Friend Sub 应用字幕设置()
        Threading.Volatile.Read(当前次字幕)?.SRT生成器?.设置样式(设置.实例对象.创建SRT字幕样式())
        播放控制器?.当前字幕?.SRT生成器?.设置样式(设置.实例对象.创建SRT字幕样式())
        字幕图层呈现器?.使图层失效()
    End Sub

    Friend Sub 应用弹幕设置()
        弹幕图层呈现器?.应用弹幕设置(设置.实例对象.创建弹幕显示配置())
        更新弹幕按钮状态()
    End Sub

    ' ── 设置页 → 视频超分辨率 的后端（10-18 从标题栏菜单移到这里）───────────────
    ' 原生调用与"新会话重放偏好"都留在 播放器视频超分辨率控制器 里，这里只做转发；
    ' 设置页不许自己再发一次 FFF3FP_SetVideoSuperResolution，否则两处会各写各的偏好。
    Friend Function 视频超分辨率可用() As Boolean
        Return 视频超分辨率控制器?.可用 OrElse False
    End Function

    Friend Function 视频超分辨率已启用() As Boolean
        Return 视频超分辨率控制器 IsNot Nothing AndAlso 视频超分辨率控制器.已启用
    End Function

    Friend Sub 应用视频超分辨率设置(开启 As Boolean)
        视频超分辨率控制器?.设置为(开启)
    End Sub

    Friend Sub 重新探测视频超分辨率()
        视频超分辨率控制器?.重新探测()
    End Sub

    Friend Function 读取视频超分辨率状态() As String
        Return 视频超分辨率控制器?.刷新状态()
    End Function

    ' ── 设置页 → 视频超分辨率 页内 自动 HDR 区的后端 ────────────────────────
    ' 与 VSR 同规矩：原生调用与"新会话重放偏好"都留在 播放器视频自动HDR控制器 里，
    ' 这里只做转发；设置页不许自己再发一次 FFF3FP_SetVideoAutoHdr。
    Friend Function 视频自动HDR可用() As Boolean
        Return 视频自动HDR控制器?.可用 OrElse False
    End Function

    Friend Function 视频自动HDR已启用() As Boolean
        Return 视频自动HDR控制器 IsNot Nothing AndAlso 视频自动HDR控制器.已启用
    End Function

    Friend Sub 应用视频自动HDR设置(开启 As Boolean)
        视频自动HDR控制器?.设置为(开启)
    End Sub

    Friend Sub 重新探测视频自动HDR()
        视频自动HDR控制器?.重新探测()
    End Sub

    Friend Function 读取视频自动HDR状态() As String
        Return 视频自动HDR控制器?.刷新状态()
    End Function

    Friend Sub 应用歌词设置()
        歌词图层呈现器?.使图层失效()
    End Sub

    Private Function 创建歌词呈现设置() As 歌词呈现设置
        Return New 歌词呈现设置(20.0F,
            If(设置.实例对象.渲染封面图毛玻璃背景, 5, 0), 4, &H78000000UI,
            40.0F, 60.0F, 20.0F, 0.0F, 7.5F)
    End Function

    Private Sub 更新弹幕按钮状态()
        MB_弹幕开关.ForeColor = If(设置.实例对象.弹幕已启用,
                                  Color.FromArgb(251, 114, 153), Color.Silver)
    End Sub

    Private Sub 更新弹幕按钮可见性()
        Dim 当前弹幕 = 播放控制器?.当前弹幕
        Dim 有可用弹幕 = 当前弹幕 IsNot Nothing AndAlso 当前弹幕.数量 > 0
        MB_弹幕开关.Visible = 有可用弹幕
        JEC_弹幕开关前面的空白占位.Visible = 有可用弹幕
    End Sub

    Private Sub MB_弹幕开关_Click(sender As Object, e As EventArgs) Handles MB_弹幕开关.Click
        设置.实例对象.弹幕已启用 = Not 设置.实例对象.弹幕已启用
        弹幕图层呈现器?.使图层失效()
        更新弹幕按钮状态()
    End Sub

    Friend Sub 设置窗口应用字体(fontName As String)
        界面呈现器?.更新字体()
        信息图层呈现器?.应用全局字体(fontName)
        流选择器?.应用全局字体(fontName)
        画面菜单控制器?.应用全局字体(fontName)
        设置窗口?.应用字体(fontName)
        播放列表窗口?.应用字体(fontName)
    End Sub

    Friend Sub 应用设置窗口玻璃背景(启用 As Boolean)
        设置窗口?.应用玻璃背景(启用)
    End Sub

    Private Sub 播放控制器_外部歌词已加载(sender As Object, e As 播放器歌词事件参数)
        If 正在关闭 Then Return
        歌词图层呈现器?.使图层失效()
        信息图层呈现器?.显示操作信息(
            $"已加载 {e.条目数} 组 LRC 歌词 · {Path.GetFileName(e.路径)}", &HFF55E7EAUI)
    End Sub

    Private Sub 界面呈现器_请求跳转到关键帧(sender As Object, e As 播放器跳转请求事件参数)
        播放控制器.跳转到关键帧(e.位置)
    End Sub

    Private Sub 界面呈现器_音量已变更(sender As Object, e As 播放器音量事件参数)
        设置.实例对象.音量百分比 = 界面呈现器.音量百分比
        播放控制器.设置音量(e.音量)
        信息图层呈现器?.显示操作信息($"音量 {界面呈现器.音量百分比}%", &HFFF0D35DUI, "音量")
    End Sub

    Private Sub MB_播放和暂停_Click(sender As Object, e As EventArgs) Handles MB_播放和暂停.Click
        播放控制器.切换播放暂停()
    End Sub

    Private Sub MB_停止_Click(sender As Object, e As EventArgs) Handles MB_停止.Click
        图片浏览控制器?.关闭图片模式()
        播放控制器.停止()
        当前弹幕路径 = String.Empty
        更新弹幕按钮可见性()
        播放列表窗口?.更新正在播放项()
        Text = "FFF.Player"
        界面呈现器.清除媒体()
    End Sub

    Private Sub MB_倒退或上一个_MouseClick(sender As Object, e As MouseEventArgs) Handles MB_倒退或上一个.MouseClick
        Select Case e.Button
            Case MouseButtons.Left : 播放控制器.相对跳转(-跳转秒数)
            Case MouseButtons.Right : 播放相邻项目(-1)
        End Select
    End Sub

    Private Sub MB_快进或下一个_MouseClick(sender As Object, e As MouseEventArgs) Handles MB_快进或下一个.MouseClick
        Select Case e.Button
            Case MouseButtons.Left : 播放控制器.相对跳转(跳转秒数)
            Case MouseButtons.Right : 播放相邻项目(1)
        End Select
    End Sub

    Private Sub 播放相邻项目(方向 As Integer)
        Dim 项目 = 播放列表数据.移动到相邻项目(方向)
        If 项目 IsNot Nothing Then
            播放列表数据.取消自动扫描()
            播放控制器.打开媒体(项目.路径)
        End If
    End Sub

    Private Sub MB_软件解码或硬件解码_Click(sender As Object, e As EventArgs) Handles MB_软件解码或硬件解码.Click
        If Not String.IsNullOrEmpty(播放控制器.切换解码器()) Then
            设置.实例对象.解码方式 = CInt(播放控制器.解码器偏好)
        End If
    End Sub

    Private Sub MB_HDR模式_Click(sender As Object, e As EventArgs) Handles MB_HDR模式.Click
        已跳过HDR强制确认 = False
        播放控制器.切换HDR模式()
    End Sub

    Private Sub Form1_DpiChanged(sender As Object, e As DpiChangedEventArgs) Handles Me.DpiChanged
        界面呈现器?.更新Dpi()
    End Sub

    Protected Overrides Function ProcessDialogKey(keyData As Keys) As Boolean
        If 播放控制器 Is Nothing OrElse Not 窗口当前有焦点() Then Return MyBase.ProcessDialogKey(keyData)
        If 光盘控制器 IsNot Nothing AndAlso 光盘控制器.处理按键(keyData) Then Return True
        If 图片浏览控制器 IsNot Nothing AndAlso 图片浏览控制器.处理缩放快捷键(keyData) Then Return True
        If keyData = Keys.Tab AndAlso Not 正在关闭 Then
            切换媒体信息层()
            Return True
        End If
        If 处理方向键快捷键(keyData) Then Return True
        Return MyBase.ProcessDialogKey(keyData)
    End Function

    Protected Overrides Function ProcessCmdKey(ByRef msg As Message, keyData As Keys) As Boolean
        If 播放控制器 Is Nothing OrElse Not 窗口当前有焦点() Then Return MyBase.ProcessCmdKey(msg, keyData)
        If 光盘控制器 IsNot Nothing AndAlso 光盘控制器.处理按键(keyData) Then Return True
        If 图片浏览控制器 IsNot Nothing AndAlso 图片浏览控制器.处理缩放快捷键(keyData) Then Return True
        If 处理方向键快捷键(keyData) Then Return True
        Select Case keyData
            Case Keys.Control Or Keys.O
                MB_打开文件_Click(MB_打开文件, EventArgs.Empty)
            Case Keys.Space
                播放控制器.切换播放暂停()
            Case Keys.MediaPlayPause, Keys.MediaStop,
                 Keys.MediaNextTrack, Keys.MediaPreviousTrack
                Return 处理多媒体按键(keyData)
            Case Keys.M
                播放控制器.切换静音()
                信息图层呈现器?.显示操作信息(If(播放控制器.静音,
                    "已静音", $"音量 {界面呈现器.音量百分比}%"), &HFFF0D35DUI, "音量")
            Case Keys.R, Keys.Shift Or Keys.R
                ' 手动旋转：R = 顺时针 90°，Shift+R = 逆时针 90°（Shift 含在 keyData 里）。
                ' 源的自动转正由内核在打开时套用，这里是在其之上再做用户调整。
                If 视图旋转控制器 Is Nothing Then Return MyBase.ProcessCmdKey(msg, keyData)
                视图旋转控制器.旋转(顺时针:=(keyData And Keys.Shift) <> Keys.Shift)
            Case Keys.F11, Keys.Alt Or Keys.Enter
                ThisIsYourWindow1.ToggleFullScreen(Me)
            Case Keys.Escape
                If Not ThisIsYourWindow1.IsFullScreen(Me) Then Return MyBase.ProcessCmdKey(msg, keyData)
                ThisIsYourWindow1.ExitFullScreen(Me)
            Case Keys.S
                MB_停止_Click(MB_停止, EventArgs.Empty)
            Case Keys.PageUp
                播放相邻项目(-1)
            Case Keys.PageDown
                播放相邻项目(1)
            Case Else
                Return MyBase.ProcessCmdKey(msg, keyData)
        End Select
        Return True
    End Function

    Private Function 窗口当前有焦点() As Boolean
        Return Not 正在关闭 AndAlso ContainsFocus AndAlso Form.ActiveForm Is Me
    End Function

    Private Function 处理多媒体按键(按键 As Keys) As Boolean
        Select Case 按键
            Case Keys.MediaPlayPause
                Return 处理多媒体命令(多媒体操作.播放暂停)
            Case Keys.MediaStop
                Return 处理多媒体命令(多媒体操作.停止)
            Case Keys.MediaNextTrack
                Return 处理多媒体命令(多媒体操作.下一项)
            Case Keys.MediaPreviousTrack
                Return 处理多媒体命令(多媒体操作.上一项)
            Case Else
                Return False
        End Select
    End Function

    Private Function 处理多媒体命令(命令 As 多媒体操作) As Boolean
        If 播放控制器 Is Nothing Then Return False
        Select Case 命令
            Case 多媒体操作.下一项
                播放相邻项目(1)
            Case 多媒体操作.上一项
                播放相邻项目(-1)
            Case 多媒体操作.停止
                MB_停止_Click(MB_停止, EventArgs.Empty)
            Case 多媒体操作.播放暂停
                播放控制器?.切换播放暂停()
            Case 多媒体操作.播放
                If 播放控制器?.安全读取快照()?.状态 <> 播放状态.正在播放 Then
                    播放控制器?.切换播放暂停()
                End If
            Case 多媒体操作.暂停
                If 播放控制器?.安全读取快照()?.状态 = 播放状态.正在播放 Then
                    播放控制器?.切换播放暂停()
                End If
            Case 多媒体操作.快进
                播放控制器?.相对跳转(跳转秒数)
            Case 多媒体操作.快退
                播放控制器?.相对跳转(-跳转秒数)
            Case Else
                Return False
        End Select
        Return True
    End Function

    Private Sub 画面控件_Ctrl滚轮(sender As Object, e As HandledMouseEventArgs)
        If Not 窗口当前有焦点() Then Return
        Dim 滚轮格数 = e.Delta \ 120
        If 滚轮格数 = 0 Then Return
        If 信息图层呈现器 IsNot Nothing AndAlso 信息图层呈现器.调试信息可见 Then
            信息图层呈现器.调整临时字号(滚轮格数)
            e.Handled = True
            Return
        End If
        If Not 设置.实例对象.启用歌词支持 OrElse 播放控制器?.当前歌词 Is Nothing Then Return
        Dim 快照 = 播放控制器.安全读取快照()
        If 快照 Is Nothing OrElse 播放控制器.当前媒体是视频 OrElse 快照.当前音频流 < 0 OrElse
            播放控制器.当前歌词.条目.Count = 0 Then Return
        歌词图层呈现器?.调整临时字号(滚轮格数)
        e.Handled = True
    End Sub

    Private Function 处理方向键快捷键(keyData As Keys) As Boolean
        If 正在关闭 Then Return False
        Dim 快捷键事件参数 As New KeyEventArgs(keyData)
        RaiseEvent 方向键快捷键已请求(Me, 快捷键事件参数)
        If 快捷键事件参数.Handled Then Return True
        Dim 按键 = keyData And Keys.KeyCode
        Dim 修饰键 = keyData And Keys.Modifiers
        Select Case 按键
            Case Keys.Left, Keys.Right
                If 修饰键 <> Keys.None Then Return False
                Dim 方向 = If(按键 = Keys.Left, -1, 1)
                播放控制器.相对跳转(方向 * 跳转秒数)
            Case Keys.Up, Keys.Down
                If 修饰键 <> Keys.None Then Return False
                Dim 增量 = If(按键 = Keys.Up, 5, -5)
                界面呈现器.调整音量(增量)
            Case Else
                Return False
        End Select
        Return True
    End Function

    Private Sub MB_播放列表_Click(sender As Object, e As EventArgs) Handles MB_播放列表.Click
        If 正在关闭 Then Return
        If 播放列表窗口 Is Nothing OrElse 播放列表窗口.IsDisposed Then
            播放列表窗口 = New Form播放列表()
            播放列表窗口.连接(播放列表数据, AddressOf 播放列表请求播放,
                         Function() If(播放控制器?.当前媒体路径, String.Empty))
        End If
        播放列表窗口.显示窗口(Me)
    End Sub

    Private Sub 播放列表请求播放(索引 As Integer)
        If 正在关闭 OrElse 索引 < 0 OrElse 索引 >= 播放列表数据.数量 Then Return
        播放列表数据.取消自动扫描()
        播放列表数据.选择(索引)
        Dim 项目 = 播放列表数据.当前项目
        If 项目 IsNot Nothing Then 播放控制器.打开媒体(项目.路径)
    End Sub

    Private Sub 播放列表数据_列表变化(sender As Object, e As EventArgs)
        If 正在关闭 Then Return
        If InvokeRequired Then
            BeginInvoke(Sub() 播放列表数据_列表变化(sender, e))
            Return
        End If
        If String.IsNullOrEmpty(待选中播放列表路径) Then Return
        If 播放列表数据.选择路径(待选中播放列表路径) Then
            待选中播放列表路径 = String.Empty
            播放列表窗口?.更新正在播放项()
        End If
    End Sub

    Private Shared Sub 启动后台任务(任务 As Task)
        If 任务 Is Nothing Then Return
        If 任务.IsCompleted Then
            Dim 忽略 = 任务.Exception
            Return
        End If
        Dim 忽略继续 = 任务.ContinueWith(
            Sub(t)
                Dim 忽略 = t.Exception
            End Sub,
            CancellationToken.None,
            TaskContinuationOptions.OnlyOnFaulted,
            TaskScheduler.Default)
    End Sub

    Private Sub MB_软件设置_Click(sender As Object, e As EventArgs) Handles MB_软件设置.Click
        If 设置窗口 Is Nothing OrElse 设置窗口.IsDisposed Then 设置窗口 = New Form设置()
        设置窗口.显示窗口()
    End Sub

    Private Sub MB_标题栏菜单按钮_Click(sender As Object, e As EventArgs) Handles MB_标题栏菜单按钮.Click
        光盘控制器?.请求扫描光驱()
        MCM_标题栏菜单.Show(MP_DX视频容器, New Point(0, 0))
    End Sub
End Class
