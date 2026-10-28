Friend Module Program
    <STAThread>
    Public Function Main(args As String()) As Integer
        If args.Length > 0 Then
            Console.Error.WriteLine($"未知测试入口：{args(0)}。未运行任何测试。")
            Return 1
        End If

        Console.WriteLine("尚未注册测试，未运行任何测试。")
        Return 0
    End Function
End Module
