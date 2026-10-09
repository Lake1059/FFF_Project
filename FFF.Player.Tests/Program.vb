Imports System
Imports System.Linq

Friend Module Program
    <STAThread>
    Public Function Main(args As String()) As Integer
        ' Dedicated entry so the lyrics-carriage kit can be run on its own; without
        ' this it would need a media path and be indistinguishable from playback.
        If args.Length > 0 AndAlso String.Equals(args(0), "--lyrics-kit-regression",
                StringComparison.OrdinalIgnoreCase) Then
            Return LyricsKitRegression.运行(args.Skip(1).ToArray())
        End If
        Return 0
    End Function
End Module
