Friend Module Program
    <STAThread>
    Public Function Main(参数 As String()) As Integer
        For Each 项 In 参数
            If String.Equals(项, "--bluray-drop-regression", StringComparison.OrdinalIgnoreCase) Then
                Return 蓝光拖放回归.运行()
            End If
            If String.Equals(项, "--security-regression", StringComparison.OrdinalIgnoreCase) Then
                Return 安全修复回归.运行()
            End If
            If String.Equals(项, "--360-identification-regression", StringComparison.OrdinalIgnoreCase) Then
                Return 播放器360识别回归.运行()
            End If
            If String.Equals(项, "--pr57-fixes-regression", StringComparison.OrdinalIgnoreCase) Then
                Return PR57修复回归.运行()
            End If
            If String.Equals(项, "--settings-designer-regression", StringComparison.OrdinalIgnoreCase) Then
                Return 设置页回归.运行()
            End If
            If String.Equals(项, "--content-size-regression", StringComparison.OrdinalIgnoreCase) Then
                Return 窗口内容尺寸回归.运行()
            End If
            If String.Equals(项, "--module-abi-regression", StringComparison.OrdinalIgnoreCase) OrElse
               String.Equals(项, "--external-module-abi-regression", StringComparison.OrdinalIgnoreCase) Then
                Return 外部模块ABI回归.运行()
            End If
            If String.Equals(项, "--raw-file-association-regression", StringComparison.OrdinalIgnoreCase) Then
                Return 文件关联回归.运行()
            End If
            If String.Equals(项, "--bluetooth-latency-regression", StringComparison.OrdinalIgnoreCase) Then
                Return 蓝牙延迟策略测试.运行()
            End If
            If String.Equals(项, "--playback-progress-regression", StringComparison.OrdinalIgnoreCase) Then
                Return 播放进度回归.运行()
            End If
            If String.Equals(项, "--smtc-mapping-regression", StringComparison.OrdinalIgnoreCase) Then
                Return 系统媒体映射回归.运行()
            End If
            If String.Equals(项, "--smtc-runtime-smoke", StringComparison.OrdinalIgnoreCase) Then
                Return 系统媒体会话冒烟.运行()
            End If
            If String.Equals(项, "--ai-pack-resource-regression", StringComparison.OrdinalIgnoreCase) Then
                Return 内置资源回归.运行()
            End If
            If String.Equals(项, "--first-frame-event-regression", StringComparison.OrdinalIgnoreCase) Then
                Return 首帧事件解析回归.运行()
            End If
            If String.Equals(项, "--decode-adapter-readback-regression", StringComparison.OrdinalIgnoreCase) Then
                Return 解码适配器读取回归.运行()
            End If
        Next
        Console.WriteLine("可用套件：--bluray-drop-regression  --security-regression  --360-identification-regression  --pr57-fixes-regression  --settings-designer-regression  --content-size-regression  --module-abi-regression  --raw-file-association-regression  --bluetooth-latency-regression  --playback-progress-regression  --smtc-mapping-regression  --smtc-runtime-smoke  --first-frame-event-regression  --ai-pack-resource-regression  --decode-adapter-readback-regression")
        Return 2
    End Function
End Module
