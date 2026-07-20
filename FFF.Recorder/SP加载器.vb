Imports System.Reflection

Public Module SP加载器
    Public Sub 启动时加载()
        SP_UnLock = False
        Dim assemblyPath As String = IO.Path.Combine(Application.StartupPath, "FFFRecorderSupporter.dll")
        If Not IO.File.Exists(assemblyPath) Then Return
        Try
            Dim loadedAssembly As Assembly = Assembly.LoadFile(assemblyPath)
            Dim type = loadedAssembly.GetType("FFFRecorderSupporter.Entry", False)
            Dim method = type?.GetMethod("Entry", BindingFlags.Public Or BindingFlags.NonPublic Or BindingFlags.Static)
            method?.Invoke(Nothing, Nothing)
            Dim propertyInfo = type?.GetProperty("Unlocked", BindingFlags.Public Or BindingFlags.NonPublic Or BindingFlags.Static)
            If propertyInfo IsNot Nothing Then SP_UnLock = CBool(propertyInfo.GetValue(Nothing))
        Catch
            SP_UnLock = False
        End Try
    End Sub
End Module
