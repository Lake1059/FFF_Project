Imports System.Runtime.InteropServices
Imports LakeUI

Friend NotInheritable Class 杜比视界测试授权
    Private Sub New()
    End Sub

    <DllImport("FFF.Native.dll", CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True)>
    Private Shared Function FFF3FP_GetColorExtensionStatus() As Integer
    End Function

    <DllImport("FFF.Native.dll", CallingConvention:=CallingConvention.Cdecl, ExactSpelling:=True,
        CharSet:=CharSet.Ansi)>
    <CodeAnalysis.SuppressMessage("Globalization", "CA2101:指定对 P/Invoke 字符串参数进行封送处理", Justification:="<挂起>")>
    Private Shared Function FFF3FP_AuthenticateColorExtension(<MarshalAs(UnmanagedType.LPUTF8Str)> codeUtf8 As String) As Integer
    End Function

    Friend Shared Sub 初始化(owner As IWin32Window)
        Dim status As Integer
        Try
            status = FFF3FP_GetColorExtensionStatus()
        Catch
            Return
        End Try
        If status = 0 Then Return
        If status = 2 Then Return
        Dim code = ExInputBox(owner, $"票据有效期为一个月{vbCrLf}如需刷新票据时间请删除票据文件并重新解锁{vbCrLf}{vbCrLf}此 DLL 仅限开发群内部测试使用！{vbCrLf}任何传播、公开使用、任何商业等行为导致违反杜比视界版权许可产生的纠纷均由使用者承担，与开发者没有任何关系！", "Dolby Vision 技术测试防传播验证")
        If String.IsNullOrWhiteSpace(code) Then Return
        code = code.Trim()
        If code.Length <> 8 OrElse Not code.All(Function(c) Char.IsDigit(c)) Then Return

        If FFF3FP_AuthenticateColorExtension(code) <> 0 Then Return
    End Sub
End Class
