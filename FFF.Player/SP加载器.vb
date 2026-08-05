Imports System.Reflection

Public Module SP加载器
    Public Sub 启动时加载()
        SP_UnLock = False
        Dim assemblyPath = IO.Path.Combine(程序目录, "FFFPlayerSupporter.dll")
        If Not IO.File.Exists(assemblyPath) Then Return
        Try
            Dim loadedAssembly = Assembly.LoadFile(assemblyPath)
            Dim type = loadedAssembly.GetType("FFFPlayerSupporter.Entry", False)
            type?.GetMethod("Entry", BindingFlags.Public Or BindingFlags.NonPublic Or BindingFlags.Static)?.Invoke(Nothing, Nothing)
            Dim propertyInfo = type?.GetProperty("Unlocked", BindingFlags.Public Or BindingFlags.NonPublic Or BindingFlags.Static)
            If propertyInfo IsNot Nothing Then SP_UnLock = CBool(propertyInfo.GetValue(Nothing))
        Catch
            SP_UnLock = False
        End Try
    End Sub
End Module
