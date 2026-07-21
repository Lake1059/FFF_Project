Imports LakeUI

Imports System.Runtime.InteropServices
Imports LakeUI.Notifications

Public Class Form1
    Public Shared Property 当前主窗体 As Form1
    Private Const WM_HOTKEY As Integer = &H312
    Private Const MOD_ALT As UInteger = &H1UI
    Private Const MOD_CONTROL As UInteger = &H2UI
    Private Const MOD_SHIFT As UInteger = &H4UI
    Private Const MOD_WIN As UInteger = &H8UI
    Private Const MOD_NOREPEAT As UInteger = &H4000UI
    Private pendingShortcut As Integer
    Private ReadOnly registeredHotKeys As New Dictionary(Of Integer, (Modifiers As UInteger, Key As Keys))
    Private ReadOnly registeredHotKeyIds As New Dictionary(Of (Modifiers As UInteger, Key As Keys), Integer)
    Private ReadOnly registeredHotKeyCombos As New Dictionary(Of Integer, (Modifiers As UInteger, Key As Keys))
    Private nextHotKeyRegistrationId As Integer = 1000
    Private suppressCapturedHotKey As Boolean
    Private 快捷键通知可用 As Boolean
    Private ReadOnly 快捷键通知锁 As New Threading.SemaphoreSlim(1, 1)
    Private ReadOnly 选项卡根面板 As New List(Of ModernPanel)
    Private 性能统计计时器 As PrecisionTimer

    <DllImport("user32.dll", SetLastError:=True)>
    Private Shared Function RegisterHotKey(hWnd As IntPtr, id As Integer, fsModifiers As UInteger, vk As UInteger) As Boolean
    End Function
    <DllImport("user32.dll", SetLastError:=True)>
    Private Shared Function UnregisterHotKey(hWnd As IntPtr, id As Integer) As Boolean
    End Function
    <DllImport("user32.dll")>
    Private Shared Function GetAsyncKeyState(vKey As Integer) As Short
    End Function
    Private Sub Form1_Load(sender As Object, e As EventArgs) Handles MyBase.Load
        KeyPreview = True
        当前主窗体 = Me
        SP加载器.启动时加载()
        设置.启动时加载设置()
        Me.ThisIsYourWindow1.Attach(Me)
        录制交互.初始化(Me)
        初始化快捷键通知()
        更新快捷键按钮文本()
        注册已保存快捷键()
        Me.ModernTabListControl1.Items(1).BoundControl = Form总控台
        绑定选项卡(Form总控台.ModernPanel1)
        Me.ModernTabListControl1.Items(3).BoundControl = Form输出设置
        绑定选项卡(Form输出设置.ModernPanel1)
        Me.ModernTabListControl1.Items(4).BoundControl = Form视频参数
        绑定选项卡(Form视频参数.ModernPanel1)
        Me.ModernTabListControl1.Items(5).BoundControl = Form音频参数
        绑定选项卡(Form音频参数.ModernPanel1)
        Me.ModernTabListControl1.Items(7).BoundControl = Form设置
        绑定选项卡(Form设置.ModernPanel1)
        Me.ModernTabListControl1.Items(8).BoundControl = Form个性化
        绑定选项卡(Form个性化.ModernPanel1)
        Me.ModernTabListControl1.Items(9).BoundControl = Form支持者
        绑定选项卡(Form支持者.ModernPanel1)
        Form个性化.初始化页面()
        字体控制.更新所有控件字体属性()
        设置.应用SP个性化设置()
        设置.加载SP自定义图标()

        LakeUI.GlobalOptions.GlobalTextQuality = GlobalOptions.TextQualityMode.Outline

    End Sub

    Private Sub Form1_Shown(sender As Object, e As EventArgs) Handles Me.Shown
        Me.ModernTabListControl1.SelectedIndex = 1
        Form总控台.确保预览()
        Form总控台.预览?.设置源(录制交互.当前视频源)
        更新总控台活动状态()
        初始化性能统计()
    End Sub

    Private Sub Form1_FormClosing(sender As Object, e As FormClosingEventArgs) Handles Me.FormClosing
        Form总控台.设置页面活动(False)
        停止性能统计()
        注销所有快捷键()
        录制交互.关闭()
        Form视频参数.保存当前质量值()
        设置.退出时保存设置()
    End Sub

    Private Sub 更新总控台活动状态(Optional sender As Object = Nothing, Optional e As EventArgs = Nothing) _
        Handles ModernTabListControl1.SelectedIndexChanged, Me.Resize
        If 当前主窗体 IsNot Me Then Return
        Dim 窗口活动 = Visible AndAlso WindowState <> FormWindowState.Minimized
        Form总控台.设置页面活动(窗口活动 AndAlso ModernTabListControl1.SelectedIndex = 1)
        If 性能统计计时器 IsNot Nothing Then
            If 窗口活动 Then 性能统计计时器.Start() Else 性能统计计时器.Stop()
        End If
    End Sub

    Private Sub Form1_FormClosed(sender As Object, e As FormClosedEventArgs) Handles Me.FormClosed
        设置.释放SP资源()
    End Sub

    Private Sub 初始化性能统计()
        If 性能统计计时器 IsNot Nothing Then Return
        MainAppUsageCounter.Start()
        性能统计计时器 = New PrecisionTimer With {
            .Interval = 1000,
            .DispatchMode = PrecisionTimer.DispatchModeEnum.NonBlocking,
            .OverrunPolicy = PrecisionTimer.OverrunPolicyEnum.Drop,
            .WorkerThreadCount = 1,
            .SynchronizingObject = Me
        }
        AddHandler 性能统计计时器.Tick, AddressOf 刷新性能统计
        If Visible AndAlso WindowState <> FormWindowState.Minimized Then
            性能统计计时器.Start()
            刷新性能统计(Nothing, EventArgs.Empty)
        End If
    End Sub

    Private Sub 刷新性能统计(sender As Object, e As EventArgs)
        Try
            Dim 标题 = "<Title>"
            标题 &= $"   |   CPU {MainAppUsageCounter.GetCpuUsagePercent():F1}%"
            标题 &= $"   |   RAM {MainAppUsageCounter.GetActivePrivateWorkingSetBytes() / 1024 / 1024:F0}M / {MainAppUsageCounter.GetCommitSizeBytes() / 1024 / 1024:F0}M"
            标题 &= $"   |   GPU {MainAppUsageCounter.GetGpuUsagePercent():F1}% {MainAppUsageCounter.GetGpuDedicatedMemoryBytes() / 1024 / 1024:F0}M + {MainAppUsageCounter.GetGpuSharedMemoryBytes() / 1024 / 1024:F0}M"
            ThisIsYourWindow1.TitleTextPrivateProtocol = 标题
        Catch
            ThisIsYourWindow1.TitleTextPrivateProtocol = "<Title>"
        End Try
    End Sub

    Private Sub 停止性能统计()
        If 性能统计计时器 IsNot Nothing Then
            RemoveHandler 性能统计计时器.Tick, AddressOf 刷新性能统计
            性能统计计时器.Dispose()
            性能统计计时器 = Nothing
        End If
        MainAppUsageCounter.Disable()
    End Sub
    Private Sub Form1_KeyDown(sender As Object, e As KeyEventArgs) Handles Me.KeyDown
        If pendingShortcut = 0 Then Return
        If e.KeyCode = Keys.Escape Then
            pendingShortcut = 0
            更新快捷键按钮文本()
            e.SuppressKeyPress = True
            e.Handled = True
            Return
        End If
        If e.KeyCode = Keys.ControlKey OrElse e.KeyCode = Keys.ShiftKey OrElse e.KeyCode = Keys.Menu OrElse e.KeyCode = Keys.LWin OrElse e.KeyCode = Keys.RWin Then Return
        Dim Win键按下 = (GetAsyncKeyState(CInt(Keys.LWin)) And Short.MinValue) <> 0 OrElse
                       (GetAsyncKeyState(CInt(Keys.RWin)) And Short.MinValue) <> 0
        Dim 文本 = 组合键文本(e.Modifiers, e.KeyCode, Win键按下)
        If 文本 <> "" Then
            Dim 快捷键编号 = pendingShortcut
            suppressCapturedHotKey = True
            pendingShortcut = 0
            保存快捷键(快捷键编号, 文本)
            更新快捷键按钮文本()
            e.SuppressKeyPress = True
            e.Handled = True
        End If
    End Sub

    Private Sub Form1_KeyUp(sender As Object, e As KeyEventArgs) Handles Me.KeyUp
        If Not suppressCapturedHotKey Then Return
        BeginInvoke(Sub()
                        If pendingShortcut = 0 Then suppressCapturedHotKey = False
                    End Sub)
    End Sub

    Protected Overrides Sub WndProc(ByRef m As Message)
        If m.Msg = WM_HOTKEY AndAlso pendingShortcut = 0 AndAlso Not suppressCapturedHotKey Then
            Dim 组合 As (Modifiers As UInteger, Key As Keys)
            If registeredHotKeyCombos.TryGetValue(m.WParam.ToInt32(), 组合) Then 执行快捷键组合(组合)
        End If
        MyBase.WndProc(m)
    End Sub

    Friend Sub 开始捕获快捷键(id As Integer)
        pendingShortcut = id
        更新快捷键按钮文本()
    End Sub

    Friend Sub 清除快捷键(id As Integer)
        pendingShortcut = 0
        注销快捷键(id)
        保存快捷键文本(id, String.Empty)
        更新快捷键按钮文本()
    End Sub

    Private Sub 保存快捷键(id As Integer, 文本 As String)
        Dim parsed = 解析快捷键(文本)
        If Not parsed.HasValue Then Return
        For Each item In registeredHotKeys
            If item.Key <> id AndAlso 快捷键相同(item.Value, parsed.Value) AndAlso
                Not 快捷键允许共用(item.Key, id) Then
                Dim 冲突动作名称 = 快捷键动作名称(item.Key)
                MessageBox.Show($"该快捷键已用于【{冲突动作名称}】。只有【开始/结束】和【暂停/继续】可以成对共用快捷键。",
                    "快捷键冲突", MessageBoxButtons.OK, MessageBoxIcon.Warning)
                Return
            End If
        Next

        Dim 当前组合 As (Modifiers As UInteger, Key As Keys)
        Dim 已有当前组合 = registeredHotKeys.TryGetValue(id, 当前组合)
        If 已有当前组合 AndAlso 快捷键相同(当前组合, parsed.Value) Then
            保存快捷键文本(id, 文本)
            Return
        End If

        If Not 确保快捷键组合已注册(parsed.Value) Then
            MessageBox.Show("注册全局快捷键失败，可能已被其他程序占用。", "快捷键", MessageBoxButtons.OK, MessageBoxIcon.Warning)
            Return
        End If
        registeredHotKeys(id) = parsed.Value
        If 已有当前组合 Then 清理未使用快捷键组合(当前组合)
        保存快捷键文本(id, 文本)
        更新快捷键按钮文本()
    End Sub

    Private Sub 注销快捷键(id As Integer)
        Dim 组合 As (Modifiers As UInteger, Key As Keys)
        If Not registeredHotKeys.TryGetValue(id, 组合) Then Return
        registeredHotKeys.Remove(id)
        清理未使用快捷键组合(组合)
    End Sub

    Private Sub 注销所有快捷键()
        For Each id In registeredHotKeyCombos.Keys
            UnregisterHotKey(Handle, id)
        Next
        registeredHotKeys.Clear()
        registeredHotKeyIds.Clear()
        registeredHotKeyCombos.Clear()
    End Sub

    Private Function 确保快捷键组合已注册(组合 As (Modifiers As UInteger, Key As Keys)) As Boolean
        If registeredHotKeyIds.ContainsKey(组合) Then Return True
        While registeredHotKeyCombos.ContainsKey(nextHotKeyRegistrationId)
            nextHotKeyRegistrationId += 1
        End While
        Dim 注册编号 = nextHotKeyRegistrationId
        nextHotKeyRegistrationId += 1
        If Not RegisterHotKey(Handle, 注册编号, 组合.Modifiers Or MOD_NOREPEAT, CUInt(组合.Key)) Then Return False
        registeredHotKeyIds.Add(组合, 注册编号)
        registeredHotKeyCombos.Add(注册编号, 组合)
        Return True
    End Function

    Private Sub 清理未使用快捷键组合(组合 As (Modifiers As UInteger, Key As Keys))
        If registeredHotKeys.Values.Any(Function(x) 快捷键相同(x, 组合)) Then Return
        Dim 注册编号 As Integer
        If Not registeredHotKeyIds.TryGetValue(组合, 注册编号) Then Return
        UnregisterHotKey(Handle, 注册编号)
        registeredHotKeyIds.Remove(组合)
        registeredHotKeyCombos.Remove(注册编号)
    End Sub

    Private Sub 执行快捷键组合(组合 As (Modifiers As UInteger, Key As Keys))
        Dim 当前状态 = If(Not 录制交互.是否录制中, 1,
            If(录制交互.是否已暂停, 4, 2))
        Dim 动作编号 = registeredHotKeys.
            Where(Function(item) 快捷键相同(item.Value, 组合) AndAlso
                (快捷键动作状态(item.Key) And 当前状态) <> 0).
            Select(Function(item) item.Key).
            FirstOrDefault()
        If 动作编号 = 0 Then Return
        Select Case 动作编号
            Case 1 : 录制交互.开始录制()
            Case 2 : 录制交互.暂停()
            Case 3 : 录制交互.继续录制()
            Case 4 : 录制交互.停止录制()
            Case 5 : 录制交互.切分文件()
        End Select
        If 快捷键动作已完成(动作编号) Then 显示快捷键状态通知(动作编号)
    End Sub

    Private Sub 初始化快捷键通知()
        Try
            Dim 错误消息 = String.Empty
            快捷键通知可用 = LakeNotificationManager.TryInitialize(
                New LakeNotificationRegistrationOptions With {
                    .DisplayName = "FFF.Recorder",
                    .ShowRuntimeInstallerUi = False
                }, 错误消息)
        Catch
            快捷键通知可用 = False
        End Try
    End Sub

    Private Async Sub 显示快捷键状态通知(动作编号 As Integer)
        If Not 快捷键通知可用 Then Return
        Await 快捷键通知锁.WaitAsync()
        Try
            Await LakeNotificationManager.RemoveByGroupAsync("recording-hotkeys")
            Dim 通知 As New LakeNotificationRequest With {
                .MuteAudio = True,
                .Tag = "latest-hotkey",
                .Group = "recording-hotkeys"
            }
            通知.Texts.Add(New LakeNotificationText("FFF.Recorder"))
            通知.Texts.Add(New LakeNotificationText(快捷键状态通知文案(动作编号)))
            LakeNotificationManager.Show(通知)
        Catch
            ' 通知不可用时仍需正常执行快捷键动作。
        Finally
            快捷键通知锁.Release()
        End Try
    End Sub

    Private Shared Function 快捷键动作已完成(id As Integer) As Boolean
        Select Case id
            Case 1 : Return 录制交互.是否录制中
            Case 2 : Return 录制交互.是否已暂停
            Case 3 : Return 录制交互.是否录制中 AndAlso Not 录制交互.是否已暂停
            Case 4 : Return Not 录制交互.是否录制中
            Case 5 : Return 录制交互.是否录制中 AndAlso Not 录制交互.是否已暂停
            Case Else : Return False
        End Select
    End Function

    Private Shared Function 快捷键状态通知文案(id As Integer) As String
        Select Case id
            Case 1 : Return "已开始录制。"
            Case 2 : Return "已暂停录制。"
            Case 3 : Return "已继续录制。"
            Case 4 : Return "已停止录制。"
            Case 5 : Return "已切分文件。"
            Case Else : Return "录制状态已更新。"
        End Select
    End Function

    Private Shared Function 快捷键动作状态(id As Integer) As Integer
        Select Case id
            Case 1 : Return 1
            Case 2, 5 : Return 2
            Case 3 : Return 4
            Case 4 : Return 2 Or 4
            Case Else : Return 0
        End Select
    End Function

    Private Shared Function 快捷键允许共用(第一个动作 As Integer, 第二个动作 As Integer) As Boolean
        Return (第一个动作 = 1 AndAlso 第二个动作 = 4) OrElse
            (第一个动作 = 4 AndAlso 第二个动作 = 1) OrElse
            (第一个动作 = 2 AndAlso 第二个动作 = 3) OrElse
            (第一个动作 = 3 AndAlso 第二个动作 = 2)
    End Function

    Private Shared Function 快捷键动作名称(id As Integer) As String
        Select Case id
            Case 1 : Return "开始录制"
            Case 2 : Return "暂停录制"
            Case 3 : Return "继续录制"
            Case 4 : Return "停止录制"
            Case 5 : Return "切分文件"
            Case Else : Return "未知动作"
        End Select
    End Function

    Private Shared Function 快捷键相同(left As (Modifiers As UInteger, Key As Keys),
        right As (Modifiers As UInteger, Key As Keys)) As Boolean
        Return left.Modifiers = right.Modifiers AndAlso left.Key = right.Key
    End Function

    Private Sub 保存快捷键文本(id As Integer, 文本 As String)
        Select Case id
            Case 1 : 设置.实例对象.快捷键开始 = 文本
            Case 2 : 设置.实例对象.快捷键暂停 = 文本
            Case 3 : 设置.实例对象.快捷键继续 = 文本
            Case 4 : 设置.实例对象.快捷键停止 = 文本
            Case 5 : 设置.实例对象.快捷键切分 = 文本
        End Select
    End Sub

    Private Sub 更新快捷键按钮文本()
        Form设置.MB_设定开始录制快捷键.Text = 快捷键按钮文本(1, 设置.实例对象.快捷键开始)
        Form设置.MB_设定暂停录制快捷键.Text = 快捷键按钮文本(2, 设置.实例对象.快捷键暂停)
        Form设置.MB_设定继续录制快捷键.Text = 快捷键按钮文本(3, 设置.实例对象.快捷键继续)
        Form设置.MB_设定停止录制快捷键.Text = 快捷键按钮文本(4, 设置.实例对象.快捷键停止)
        Form设置.MB_设定切分文件快捷键.Text = 快捷键按钮文本(5, 设置.实例对象.快捷键切分)
    End Sub

    Private Function 快捷键按钮文本(id As Integer, 已保存文本 As String) As String
        If pendingShortcut = id Then Return "请按键"
        Return If(String.IsNullOrWhiteSpace(已保存文本), "未设定", 已保存文本)
    End Function

    Private Function 组合键文本(modifiers As Keys, key As Keys, Win键按下 As Boolean) As String
        Dim parts As New List(Of String)
        If (modifiers And Keys.Control) <> 0 Then parts.Add("Ctrl")
        If (modifiers And Keys.Shift) <> 0 Then parts.Add("Shift")
        If (modifiers And Keys.Alt) <> 0 Then parts.Add("Alt")
        If Win键按下 Then parts.Add("Win")
        parts.Add(key.ToString())
        Return String.Join("+", parts)
    End Function

    Private Function 解析快捷键(text As String) As (Modifiers As UInteger, Key As Keys)?
        If String.IsNullOrWhiteSpace(text) Then Return Nothing
        Dim mods As UInteger = 0
        Dim key As Keys = Keys.None
        For Each token In text.Split("+"c, StringSplitOptions.RemoveEmptyEntries)
            Select Case token.Trim().ToUpperInvariant()
                Case "CTRL", "CONTROL" : mods = mods Or MOD_CONTROL
                Case "SHIFT" : mods = mods Or MOD_SHIFT
                Case "ALT" : mods = mods Or MOD_ALT
                Case "WIN", "WINDOWS" : mods = mods Or MOD_WIN
                Case Else
                    If Not [Enum].TryParse(token.Trim(), True, key) Then Return Nothing
            End Select
        Next
        If key = Keys.None Then Return Nothing
        Return (mods, key)
    End Function

    Private Sub 注册已保存快捷键()
        Dim texts = {设置.实例对象.快捷键开始, 设置.实例对象.快捷键暂停, 设置.实例对象.快捷键继续, 设置.实例对象.快捷键停止, 设置.实例对象.快捷键切分}
        For i = 0 To texts.Length - 1
            If Not String.IsNullOrWhiteSpace(texts(i)) Then 保存快捷键(i + 1, texts(i))
        Next
    End Sub

    Sub 绑定选项卡(选项卡的根面板容器 As ModernPanel)
        If Not 选项卡根面板.Contains(选项卡的根面板容器) Then 选项卡根面板.Add(选项卡的根面板容器)
        应用根面板玻璃背景(选项卡的根面板容器, SP_UnLock AndAlso 设置.实例对象.SP_毛玻璃模式 > 0)
    End Sub

    Friend Sub 应用玻璃页面背景(启用 As Boolean)
        Dim 背景颜色 = If(启用, Color.Transparent, Color.FromArgb(48, 48, 48))
        ModernTabListControl1.BackColor = 背景颜色
        ModernTabListControl1.TabStripBackColor = 背景颜色
        ModernTabListControl1.ContentBackColor = 背景颜色
        For Each panel In 选项卡根面板
            应用根面板玻璃背景(panel, 启用)
        Next
    End Sub

    Private Sub 应用根面板玻璃背景(panel As ModernPanel, 启用 As Boolean)
        panel.BackColor = If(启用, Color.Transparent, Color.FromArgb(24, 24, 24))
        panel.BackColor1 = If(启用, Color.Transparent, Color.FromArgb(24, 24, 24))
        panel.BackgroundSource = If(启用, Me, Nothing)
    End Sub

End Class
