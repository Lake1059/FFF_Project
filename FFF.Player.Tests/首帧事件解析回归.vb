Imports FFF.Player

''' <summary>
''' L4-P1-01 回归：原生 FFF3FPEvent::FirstFramePresented=8 的宿主接线。
''' 不依赖真实内核，直接喂 detail JSON 给 <see cref="首帧呈现事件参数.解析"/>，
''' 断言 presented/total/width/height 被正确解析（而不是只断言“不抛”），
''' 并断言枚举值与 native 的 8 对齐、坏 JSON 落 0。
'''
''' 跑法：FFF.Player.Tests.exe --first-frame-event-regression
''' </summary>
Friend Module 首帧事件解析回归

    Private 失败数 As Integer
    Private 用例数 As Integer

    Friend Function 运行() As Integer
        失败数 = 0
        用例数 = 0

        ' 1) 枚举值必须与 FFF.Player.Api.h 的 FirstFramePresented=8 逐位一致。
        检查("枚举 首帧呈现 = 8", CInt(播放器事件类型.首帧呈现) = 8)

        ' 2) 真实 detail JSON：四个字段全部解析。
        Dim 正常 = 首帧呈现事件参数.解析("{""presented"":1,""total"":3,""width"":1920,""height"":1080}")
        检查("解析 presented=1", 正常.已呈现帧数 = 1UI)
        检查("解析 total=3", 正常.总帧数 = 3UI)
        检查("解析 width=1920", 正常.视频宽度 = 1920UI)
        检查("解析 height=1080", 正常.视频高度 = 1080UI)

        ' 3) 缺字段：原生未来加字段或缺某个键时，缺的字段落 0，已给的字段仍要读出来。
        Dim 缺字段 = 首帧呈现事件参数.解析("{""width"":1280,""height"":720}")
        检查("缺 presented/total 落 0", 缺字段.已呈现帧数 = 0UI AndAlso 缺字段.总帧数 = 0UI)
        检查("缺字段时 width/height 仍解析", 缺字段.视频宽度 = 1280UI AndAlso 缺字段.视频高度 = 720UI)

        ' 4) 坏 JSON / 空串：绝不抛，全 0。
        Dim 坏 = 首帧呈现事件参数.解析("这不是JSON")
        检查("坏 JSON 不抛且全 0", 坏.已呈现帧数 = 0UI AndAlso 坏.总帧数 = 0UI AndAlso
                                   坏.视频宽度 = 0UI AndAlso 坏.视频高度 = 0UI)
        Dim 空 = 首帧呈现事件参数.解析(Nothing)
        检查("null detail 不抛且全 0", 空.视频宽度 = 0UI AndAlso 空.视频高度 = 0UI)

        ' 5) 类型错位：width 给字符串而非数字时该字段落 0，不能把脏字节当成分辨率。
        Dim 错类型 = 首帧呈现事件参数.解析("{""width"":""abc"",""height"":1080}")
        检查("width 给字符串落 0", 错类型.视频宽度 = 0UI)
        检查("height 正常仍解析", 错类型.视频高度 = 1080UI)

        Console.WriteLine()
        Console.WriteLine($"首帧事件解析回归：{用例数} 条，失败 {失败数} 条")
        Return If(失败数 = 0, 0, 1)
    End Function

    Private Sub 检查(名称 As String, 条件 As Boolean)
        用例数 += 1
        If 条件 Then
            Console.WriteLine("ok    " & 名称)
        Else
            失败数 += 1
            Console.WriteLine("FAIL  " & 名称)
        End If
    End Sub
End Module
