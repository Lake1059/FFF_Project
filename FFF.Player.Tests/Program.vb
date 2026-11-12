Friend Module Program
    <STAThread>
    Public Function Main(参数 As String()) As Integer
        For Each 项 In 参数
            If String.Equals(项, "--360-identification-regression", StringComparison.OrdinalIgnoreCase) Then
                Return 播放器360识别回归.运行()
            End If
            If String.Equals(项, "--pr57-fixes-regression", StringComparison.OrdinalIgnoreCase) Then
                Return PR57修复回归.运行()
            End If
        Next
        Console.WriteLine("可用套件：--360-identification-regression  --pr57-fixes-regression")
        Return 2
    End Function
End Module
