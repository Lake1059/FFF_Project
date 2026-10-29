"""L0 静态自检：宿主语言表 ↔ worker 语言表是否漂移、设置页新增行是否放得下。

为什么需要它：
  * 宿主 `Ai字幕语言.名称表` 与 worker `LANGUAGE_NAMES` 是**两份**表（VB / Python）。
    两份一旦不一致，现象是"选了某语言 → 模型没提示 或 matched 永远 False"，
    而勾了"只显示所选语言"时就是整片空白，且不抛任何异常。
  * 设置页的语言行是脚本批量下移高级区之后加上去的（ClientSize 598x644），
    光靠肉眼没法确认有没有压字或越界。
  * 界面只列 4 个主项（其余 28 个未验证语种不上面板）：这里钉住"下拉只引用主项"，
    防止后人把循环改回整表、让未验证语种重新出现（L0-1b）。
  * 「扩展 UI + 按模型能力开放」一轮（2026-10-25）的新契约：清单 layers 驱动
    GPU 层数预置、用户 llamaArgs 进 UI 并合并透传、驻留偏移进状态机——
    断链现象全是"UI 看着能改、实际不生效"且不抛异常，见 L0-5。

用法（从仓库根目录）：
    python tools/ai-subtitles/check_l0.py
退出码 = 失败项数。
"""
import io
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
VB_LANG = os.path.join(ROOT, "FFF.Player", "AI字幕", "Ai字幕语言.vb")
VB_FORM = os.path.join(ROOT, "FFF.Player", "Form设置_AI字幕.vb")
VB_DESIGNER = os.path.join(ROOT, "FFF.Player", "Form设置_AI字幕.Designer.vb")
VB_PACK = os.path.join(ROOT, "FFF.Player", "AI字幕", "Ai字幕包.vb")
VB_PIPE = os.path.join(ROOT, "FFF.Player", "AI字幕", "Ai字幕管道.vb")
VB_FSM = os.path.join(ROOT, "FFF.Player", "AI字幕", "Ai字幕状态机.vb")
VB_CTRL = os.path.join(ROOT, "FFF.Player", "AI字幕", "播放器AI字幕控制器.vb")
VB_SET = os.path.join(ROOT, "FFF.Player", "设置.vb")
VB_CTRLER = os.path.join(ROOT, "FFF.Player", "Core", "播放器控制器.vb")
CPP_SESSION = os.path.join(ROOT, "FFF.Native", "3FP", "Core", "PlayerSession.cpp")
CPP_RENDER = os.path.join(ROOT, "FFF.Native", "3FP", "Render", "VideoRenderer.cpp")
WORKER = os.path.join(ROOT, "tools", "ai-subtitles", "asr_worker.py")
SAMPLE_PACK = os.path.join(ROOT, "tools", "ai-subtitles", "pack-sample", "ai-subtitles.json")

通过 = 0
失败 = 0


def 断言(ok, 名称, 细节=""):
    global 通过, 失败
    if ok:
        通过 += 1
        print(f"PASS  {名称}")
    else:
        失败 += 1
        print(f"FAIL  {名称}" + (f"  << {细节}" if 细节 else ""))


def read(path):
    return io.open(path, encoding="utf-8-sig", newline="").read()


# ---------------- L0-3：两份语言表不许漂移 ----------------

def parse_vb():
    src = read(VB_LANG)
    m = re.search(r"Friend Shared ReadOnly 全部 As String\(\) = \{(.*?)\}", src, re.S)
    body = m.group(1)
    # 全部 里有两项是裸标识符（自动 / 混说），不是字符串字面量，要按位置补回来。
    ids = (["auto"] if "自动" in body else []) + re.findall(r'"([^"]*)"', body)
    if "混说" in body:
        ids = ids[:3] + ["zh-en"] + ids[3:]
    # 名称表 的条目是 {"zh", "Chinese"}，一行里可能排四对：先切出整块再全量找，
    # 不要用行首锚定（会漏掉每行第二对之后的项）。
    start = src.find("名称表 As New Dictionary")
    block = src[start: src.find("\n    }", start)] if start >= 0 else ""
    pairs = re.findall(r'\{"([a-z]+)", "([A-Za-z]+)"\}', block)
    cases = re.findall(r'Case "([^"]*)" : Return "([^"]*)"', src)
    # 混说那一行用的是常量（Case 混说），不是字符串字面量，单独补进表里。
    if "Case 混说" in src:
        cases.append(("zh-en", "中文 · English（中英混说）"))
    return ids, dict(pairs), dict(cases)


def parse_py():
    src = read(WORKER)
    m = re.search(r"^LANGUAGE_NAMES = \{(.*?)^\}", src, re.S | re.M)
    return dict(re.findall(r'"([^"]+)": "([^"]+)"', m.group(1)))


def main():
    ids, vb_names, vb_display = parse_vb()
    py_names = parse_py()

    断言(ids[0] == "auto", "L0-1 全部[0] == auto（下拉默认项）")
    断言(len(ids) == 32, "L0-1 全部 32 项（auto + 中文 + English + 混说 + 28 种）", f"实际 {len(ids)}")
    断言(ids[:4] == ["auto", "zh", "en", "zh-en"], "L0-1 前四项是主项（自动/中文/English/混说）", str(ids[:4]))
    断言(len(ids) == len(set(ids)), "L0-1 全部 无重复项", "重复：" + str([x for x in ids if ids.count(x) > 1]))
    需名 = set(ids) - {"auto", "zh-en"}      # 混说故意没有英文名（不传提示）
    断言(需名 == set(vb_names), "L0-1 名称表 覆盖全部需要提示的项",
         "差集：" + str(sorted(需名 - set(vb_names)) + sorted(set(vb_names) - 需名)))

    # 两份表逐项对比
    差异 = {k: (vb_names.get(k), py_names.get(k)) for k in set(vb_names) | set(py_names)
            if vb_names.get(k) != py_names.get(k)}
    断言(not 差异, "L0-3 宿主名称表 == worker LANGUAGE_NAMES", str(差异))

    # 显示名：每个**主项**都要有专属 Case（否则落到"自动（让模型自判）"）。
    # 未验证语种（整表后 28 项）刻意没有 Case——它们不上面板，显示名塌缩成自动的。
    缺显示 = [i for i in ids[:4] if i != "auto" and i not in vb_display]
    断言(not 缺显示, "L0-1 每个主项都有专属显示名", "缺：" + str(缺显示))
    重复显示 = [v for v in set(vb_display.values()) if list(vb_display.values()).count(v) > 1]
    断言(not 重复显示, "L0-1 显示名互不重复", str(重复显示))

    # ---------------- L0-1b：设置页语言下拉只列主项 ----------------
    # 用户裁定"未验证的内容不出现"：一旦有人把下拉改回 Ai字幕语言.全部，
    # 28 个未验证语种会重新出现在面板上（而规整只认主项，选了也白选）。
    form = read(VB_FORM)
    填充块 = re.search(r"For Each id In Ai字幕语言\.(\w+)\s*\n\s*MCB_语言\.Items\.Add", form)
    断言(填充块 is not None and 填充块.group(1) == "主项",
        "L0-1b 设置页语言下拉只列主项 4 项",
        "实际引用：" + (填充块.group(1) if 填充块 else "（没找到语言填充循环）"))
    下标块 = re.search(r"For i = 0 To Ai字幕语言\.(\w+)\.Length - 1\s*\n\s*If String\.Equals\(Ai字幕语言\.\w+\(i\), id", form)
    断言(下标块 is not None and 下标块.group(1) == "主项",
        "L0-1b 语言下标映射只认主项",
        "实际引用：" + (下标块.group(1) if 下标块 else "（没找到语言下标循环）"))
    # 设备下拉来自常量表而不是写死字符串（写死就会与 设备全部 脱节：加一档要改
    # 两处，改漏就是"下拉里有、代码不认"）。
    设备填充 = re.search(r"For Each id In Ai字幕推理档\.(\w+)\s*\n(?:.*\n){0,5}?\s*MCB_设备\.Items\.Add", form)
    断言(设备填充 is not None and 设备填充.group(1) == "设备全部",
        "L0-1c 设备下拉来自 Ai字幕推理档.设备全部",
        "实际引用：" + (设备填充.group(1) if 设备填充 else "（没找到设备填充循环）"))
    # 后端下拉**不能**在构造期写死填一遍：它的内容随设备变。必须由 设备后端(设备)
    # 现算 —— 写死就是第二份真值表，症状是「AMD 显卡 + CUDA」这种不存在的组合。
    断言(re.search(r"Dim 列表 = Ai字幕推理档\.设备后端\(设备\)", form) and
         re.search(r"For Each id In 列表\s*\n(?:.*\n){0,6}?\s*MCB_后端\.Items\.Add", form),
        "L0-1c 后端下拉由 设备后端(设备) 现算，不在构造期写死")
    # 项提示只能挂 ItemToolTips（按项文本建索引的集合）。Items 是
    # Collection(Of String)，写成 Items.Add(文本, 提示) 编译就不过 —— 而如果
    # 干脆不挂提示，"这个后端把哪一段放到显卡上"用户就只能靠猜。
    断言(form.count("ItemToolTips.Add(") >= 2 and "ItemToolTips.Clear()" in form,
        "L0-1c 设备 / 后端两个下拉都挂项提示，且换设备时清旧项",
        "ItemToolTips 缺失或不清理 —— 提示会串到上一次设备的项上")
    # 归位（换设备后旧后端不在新表里）必须真的写回内存：只改下拉不写内存，
    # 就是「界面显示 CPU、worker 收到 dml」。
    断言(re.search(r"设置\.实例对象\.Ai字幕推理后端 = 选中", form),
        "L0-1c 后端归位写回设置（换设备后不留不存在的组合）")
    # 赋值 SelectedIndex 时正在初始化，事件被吞 ⇒ 初始化页面必须显式重建一次，
    # 否则打开设置页看到一个空后端下拉。
    断言(re.search(r"Friend Sub 初始化页面\(\)(.*?)重建后端下拉\(\)", form, re.S),
        "L0-1c 初始化页面显式重建后端下拉（否则打开设置页是空的）")

    # ---------------- L0-2：设置页几何 ----------------
    src = read(VB_DESIGNER)
    loc = re.compile(r"^\s+([^\s.]+)\.Location = New Point\((\d+), (\d+)\)")
    siz = re.compile(r"^\s+([^\s.]+)\.Size = New Size\((\d+), (\d+)\)")
    geo = {}
    for line in src.splitlines():
        m = loc.match(line)
        if m:
            geo.setdefault(m.group(1), {})["xy"] = (int(m.group(2)), int(m.group(3)))
        m = siz.match(line)
        if m:
            geo.setdefault(m.group(1), {})["wh"] = (int(m.group(2)), int(m.group(3)))

    m = re.search(r"ClientSize = New Size\((\d+), (\d+)\)", src)
    cw, ch = int(m.group(1)), int(m.group(2))
    m = re.search(r"ModernPanel1\.Size = New Size\((\d+), (\d+)\)", src)
    pw, ph = int(m.group(1)), int(m.group(2))
    断言((cw, ch) == (pw, ph), "L0-2 面板尺寸 == 客户区尺寸", f"client={cw}x{ch} panel={pw}x{ph}")

    boxes = []
    for name, g in geo.items():
        if "xy" in g and "wh" in g and name != "ModernPanel1":
            x, y = g["xy"]
            w, h = g["wh"]
            boxes.append((name, x, y, w, h))

    越界 = [b[0] for b in boxes if b[1] + b[3] > cw or b[2] + b[4] > ch]
    断言(not 越界, "L0-2 所有控件在客户区内", "越界：" + str(越界))

    # 视觉行的重叠：只比同一水平带上真正可能互相压的（跳过隐藏的高级区标签与输入框）
    # 互斥对例外：同一格里按条件只显示其中一个，坐标相同是设计而不是压字。
    # 目前只有模型那一格（单档显示只读事实、有第二档才显示下拉，见 L0-9）。
    互斥 = {frozenset(("LBL_模型档值", "MCB_模型档"))}
    重叠 = []
    for i in range(len(boxes)):
        for j in range(i + 1, len(boxes)):
            n1, x1, y1, w1, h1 = boxes[i]
            n2, x2, y2, w2, h2 = boxes[j]
            if frozenset((n1, n2)) in 互斥:
                continue
            if x1 < x2 + w2 and x2 < x1 + w1 and y1 < y2 + h2 and y2 < y1 + h1:
                重叠.append((n1, n2))
    断言(not 重叠, "L0-2 无控件重叠", str(重叠))

    # 关键行间距：语言行底 → 状态行顶 必须留白（脚本下移时最容易压这里）
    def box(n):
        return next(b for b in boxes if b[0] == n)

    _, _, ly, _, lh = box("MCB_语言")
    _, _, sy, _, sh = box("LBL_状态")
    断言(sy - (ly + lh) >= 8, "L0-2 语言下拉与状态行间距 ≥ 8px", f"实际 {sy - (ly + lh)}px")

    _, _, hy, _, _ = box("MCK_显示高级")
    断言(hy - (sy + sh) >= 8, "L0-2 状态行与高级区间距 ≥ 8px（状态行高随内容变，取实测值）",
        f"实际 {hy - (sy + sh)}px")

    bottom = max(b[2] + b[4] for b in boxes)
    断言(ch - bottom >= 8, "L0-2 底部留白 ≥ 8px", f"最低控件 {bottom}px / 客户区 {ch}px")

    # ---------------- L0-4：清单 v2 契约不许回退 ----------------
    # v2（2026-10-27）把模型从"单文件 GGUF + 投影"换成"一整套 ONNX 目录"。
    # 这里钉住三件最容易在后续改动里被悄悄弄回去的事：模型四件套的文件名、
    # version 门控、以及 v1 死字段不许回到读取路径上。
    worker = read(WORKER)
    断言('_MODEL_STEMS = ("conv_frontend", "encoder", "decoder")' in worker,
        "L0-4 worker 模型必备件在（三 onnx 主干名 + tokenizer/）",
        "找不到 _MODEL_STEMS —— 模型目录校验会形同虚设")
    断言('conv = _pick_onnx(md, "conv_frontend")' in worker,
        "L0-4 worker 按主干名解析权重（官方包是 encoder.int8.onnx，硬钉全名会直接起不来）",
        "load() 里没走 _pick_onnx —— 有人把文件名钉回去了")
    断言('"encoder.onnx"' not in worker and '"decoder.onnx"' not in worker,
        "L0-4 不许再出现硬编码的 encoder.onnx / decoder.onnx",
        "发现硬编码全名：官方 int8 包用的是 encoder.int8.onnx，会让解压即用失效")
    断言(re.search(r'add_argument\("--model-dir", required=True\)', worker),
        "L0-4 worker --model-dir 必填（没有模型目录就没有推理）",
        "找不到 --model-dir 的 required=True")

    pack = read(VB_PACK)
    断言('If 版本 <> 2 Then' in pack,
        "L0-4 清单 version 门控在（v1 清单必须被拒，不能一路走到奇怪报错）",
        "找不到 If 版本 <> 2 Then —— v1 包会被当成 v2 载入")
    断言(re.search(r'必需权重 As String\(\) = \{"conv_frontend", "encoder", "decoder"\}', pack),
        "L0-4 宿主也钉住三 onnx 的主干名（与 worker 同口径）",
        "Ai字幕包.vb 的 必需权重 数组变了或被删")
    断言('找权重(模型目录, 主干, 原因)' in pack and '"encoder.onnx"' not in pack,
        "L0-4 宿主缺件检查走 找权重，不硬钉 encoder.onnx",
        "宿主又硬钉全名了 —— 与 worker 的解析口径会分叉")
    断言('If Not Directory.Exists(Path.Combine(模型目录, 分词器目录))' in pack,
        "L0-4 宿主校验 tokenizer/ 是子目录（它是目录不是文件）",
        "找不到 tokenizer 子目录校验")
    断言('属性.Value.ValueKind = JsonValueKind.String' in pack,
        "L0-4 models 支持「档名 → 目录」简写（不必写 {dir:...}）",
        "读模型档表只剩对象写法 —— PACK_FORMAT §2.4 的简写会整档消失")
    for 死字段 in ("llamaArgs", "servers", "mmproj"):
        断言(f'TryGetProperty("{死字段}"' not in pack,
            f"L0-4 v1 死字段 {死字段} 没有回到读取路径",
            f"Ai字幕包里又在读 {死字段} —— 那是 llama.cpp 时代的字段，v2 没有对应物")

    # ---------------- L0-5：字幕驻留偏移（纯显示侧，不随推理栈变） ----------------
    st = read(VB_SET)
    断言('Public Property Ai字幕字幕偏移秒 As Double' in st,
        "L0-5 设置带 Ai字幕字幕偏移秒（字幕驻留偏移）")
    断言(re.search(r'Ai字幕字幕偏移秒 < -3\.0 OrElse Ai字幕字幕偏移秒 > 3\.0', st),
        "L0-5 偏移规范化钳在 [-3, +3]",
        "规范化() 里找不到偏移的区间钳制——脏设置会把字幕守卫带飞")

    fsm = read(VB_FSM)
    断言(re.search(r"Public Property 时间偏移100纳秒 As Long", fsm),
        "L0-5 状态机带 时间偏移100纳秒（驻留偏移入口）")
    断言(re.search(r"CLng\(播放位置\.Ticks\) - 时间偏移100纳秒", fsm),
        "L0-5 守卫判定用 播放位置 − 偏移",
        "取当前条目 里找不到偏移项——偏移设了等于没设")

    form = read(VB_FORM)
    高级块 = re.search(r"Private Sub 显示高级\(显示 As Boolean\)(.*?)End Sub", form, re.S)
    for 控件 in ("LBL_偏移", "MTB_偏移", "LBL_预置", "MCB_预置"):
        断言(高级块 is not None and f"{控件}.Visible = 显示" in 高级块.group(1),
            f"L0-5 {控件} 挂进高级区可见性切换",
            "显示高级 里找不到该控件的 Visible 行——折起来它还杵在面板上")

    ctrl = read(VB_CTRL)
    断言(re.search(r"状态机\.时间偏移100纳秒 = CLng\(", ctrl),
        "L0-5 控制器把偏移同步进状态机（应用设置即生效，不重启 worker）")
    断言(re.search(r"模型加载中，首句字幕约需 1 分钟", ctrl),
        "L0-5 首启加载提示（空窗期不解释，用户会以为开了没反应）")

    # ---------------- L0-6：输出护栏不许回退 ----------------
    # 2026-10-25 用真实影片片段验收才暴露的两个缺陷：无语音段模型编字、
    # 语音稀疏段把 instruction 当内容输出。两者都不抛异常，只会把一屏编造的字
    # 糊在画面上。护栏任断一环，交互测试都看不出来。
    断言("looks_degenerate(" in worker and "looks_degenerate(text)" in worker,
        "L0-6 失控编字护栏在（定义 + 主循环调用）",
        "looks_degenerate 没被调用 —— 无语音段会再次糊一屏重复字")
    断言("looks_like_prompt_leak(" in worker and "looks_like_prompt_leak(text)" in worker,
        "L0-6 指令泄漏护栏在（定义 + 主循环调用）",
        "looks_like_prompt_leak 没被调用 —— 模板串会原样上屏")
    # 三种"这窗没有有效内容"，缺一种就漏一种污染。
    for 原因 in ("empty", "degenerate", "leak"):
        断言(f'原因 = "{原因}"' in worker,
            f"L0-6 丢弃分支覆盖 {原因}",
            f"主循环里找不到 原因 = \"{原因}\" —— 这类内容会重新上屏")
    断言("字幕不会凭空编字" in worker,
        "L0-6 丢弃时给用户一次解释（空白字幕 + 无提示 = 用户以为坏了）",
        "找不到丢弃提示文案 —— 用户面对空白字幕得不到任何解释")
    # 长度上限从 llama.cpp 的 --n-predict 512 换成 sherpa 的 max_new_tokens：
    # 没有它，失控窗会把预算全烧在同一个窗口上（外层还有共享 token 预算）。
    断言("max_new_tokens=" in worker,
        "L0-6 单窗生成上限 max_new_tokens 在（sherpa 版的长度护栏）",
        "找不到 max_new_tokens —— 失控窗没有长度上限")

    # ---------------- L0-7：推理档只有三档，降档只有一处 ----------------
    # 2026-10-27 把"设备类别 + 厂商后端 + 二进制扫描"整套删掉，收敛成
    # 一个标识 + 一张 _CANDIDATES 表 + 一处 _select_backend。这些断言防的是
    # "又长出一层推断"：多一处回落，用户看到的现象就是"选了 A 实际跑 B 还不说"。
    # 二维真值表：设备（哪块硬件）× 后端（哪个运行时）。标识一律是字符串常量，
    # 设置里存标识不存下标，所以数组顺序可以安全调整。
    断言(re.search(r"Friend Shared ReadOnly 设备全部 As String\(\) = \{设备自动, 设备Cpu, 设备Nvidia, 设备Amd, 设备Intel, 设备Npu\}", pack),
        "L0-7 设备维六项：auto / cpu / nvidia / amd / intel / npu",
        "Ai字幕包.vb 的 设备全部 变了 —— 加设备要同步 设备后端 表与文档 §20")
    断言('Friend Const 设备自动 As String = "auto"' in pack,
        "L0-7 设备标识是字符串常量（设置里存标识不存下标，重排安全）",
        "找不到 Friend Const 设备自动 As String = \"auto\"")

    # 后端维必须与引擎 BACKENDS 的键**逐一相等**（除 auto）。两边分叉的唯一症状
    # 是用户在下拉里挑了个名字、日志里跑的是另一个，而界面上没有任何异常。
    后端值 = {m.group(1) for m in
             re.finditer(r'Friend Const 后端\w+ As String = "([^"]+)"', pack)}
    引擎键 = set(re.findall(r'^\s{4}"([a-z0-9\-]+)": Backend\(',
                            read(os.path.join(ROOT, "tools", "ai-subtitles", "qwen3_ort.py")), re.M))
    断言(后端值 - {"auto"} == 引擎键,
        "L0-7 后端标识与 qwen3_ort.BACKENDS 的键逐一相等",
        "宿主=%s 引擎=%s —— 多的那侧是「下拉里有、引擎不认」，少的那侧是「引擎能跑、界面选不到」"
        % (sorted(后端值 - {"auto"}), sorted(引擎键)))

    # 每类实体设备各一张候选表；表里不能有拼错的标识（拼错 = 该设备永远起不来）；
    # 每个非 auto 后端至少被一台设备列出来（否则是一条用户永远选不到的死路）。
    候选表 = dict((m.group(1), re.findall(r"后端(\w+)", m.group(2)))
                 for m in re.finditer(r"Case 设备(\w+)\s*\n\s*Return \{([^}]*)\}", pack))
    断言(set(候选表) == {"Cpu", "Nvidia", "Amd", "Intel", "Npu"},
        "L0-7 五类实体设备各有一张候选表",
        "实际：%s" % sorted(候选表))
    后端名 = {m.group(1) for m in re.finditer(r'Friend Const 后端(\w+) As String = "', pack)}
    坏项 = [d + ":" + x for d, xs in 候选表.items() for x in xs if x not in 后端名]
    断言(not 坏项,
        "L0-7 候选表里没有拼错的标识",
        "未知标识：%s" % 坏项)
    被列出 = {x for xs in 候选表.values() for x in xs}
    断言(后端名 - {"自动"} <= 被列出,
        "L0-7 每个后端至少被一台设备列出来（没有选不到的死项）",
        "永远选不到：%s" % sorted(后端名 - {"自动"} - 被列出))

    # 设备与后端分两个字段、不合成串：合成串要解析，脏值就解析成别的档
    # （v1 那套「设备类别 + 厂商后端 + 二进制扫描」正是这么长出来的）。
    断言("Public Property Ai字幕运行设备 As String" in st and
         "Public Property Ai字幕推理后端 As String" in st,
        "L0-7 设备与后端各存一个字段（不合成串）",
        "设置类里少了 Ai字幕运行设备 / Ai字幕推理后端 之一")

    # worker 还不认的后端必须当场说出来。不说 = 用户以为选了 DirectML 就在跑。
    断言("当前worker可用" in read(VB_PIPE) and "当前worker可用" in form,
        "L0-7 worker 未接的后端在界面与日志里都点名",
        "找不到 当前worker可用 —— 选了跑不了的后端会被静默按 auto 处理")
    断言(re.search(r"_CANDIDATES = \{", worker) and '"npu": ("npu", "cpu")' in worker,
        "L0-7 worker 有 _CANDIDATES 且 npu 有降档候选",
        "找不到 _CANDIDATES 或 npu 候选 —— 选 NPU 会直接起不来而不是降档")
    断言("def _select_backend(" in worker and "backend.load()" in worker,
        "L0-7 降档集中在 _select_backend（load 失败即换下一个候选）")
    断言(re.search(r'raise RuntimeError\("NPU 需要 OpenVINO GenAI 运行时，本期未实现"\)', worker),
        "L0-7 NPU 占位必须显式抛错（不许静默回落到 CPU 假装成功）",
        "NpuBackend.load() 里的显式抛错不见了")

    pipe = read(VB_PIPE)
    for 参 in ("--model-dir", "--window", "--step", "--device", "--language"):
        断言(f'ArgumentList.Add("{参}")' in pipe,
            f"L0-7 管道传 {参}",
            f"启动参数里找不到 {参}")
    断言('"--llama-arg="' not in pipe and "合并附加参数" not in pipe,
        "L0-7 管道不再有 llama-arg 逃生门（v1 的附加参数通道已整体删除）",
        "管道里又能看到 llama-arg / 合并附加参数 —— v1 通道被恢复了")

    断言(re.search(r"已启动后端 <> 新后端", ctrl) and re.search(r"已启动模型档 <> 新模型档", ctrl),
        "L0-7 控制器重启判据含 后端 + 模型档（启动期参数，中途改必须重启）",
        "应用设置 的重启条件里缺后端或模型档")

    # --- cuda 必须在 load() 之前静态预检 -------------------------------------
    # sherpa-onnx 在 provider 不可用时**不抛异常**，只打一行 C++ 日志就回落到 CPU
    # （那句日志走控制台句柄，Python 重定向抓不到，对象上也没有 provider 查询接口）。
    # 所以"靠 load() 抛错来降档"这条路根本不通，必须提前查包形态。
    断言("def _cuda_ready(" in worker and "_CUDA_EP_DLL" in worker,
        "L0-8 cuda 有静态预检（provider 不可用时不报错，只能查包形态）",
        "_cuda_ready / _CUDA_EP_DLL 不见了 —— cuda 档会退回"
        "「报 cuda、实跑 CPU」")
    段落 = worker[worker.find("def _select_backend("):]
    段落 = 段落[:段落.find("def build_arg_parser(")]
    断言(段落.find("_capability(") != -1
        and 段落.find("_capability(") < 段落.find("backend.load()"),
        "L0-8 降档循环里预检在 load() 之前调用",
        "_select_backend 没有在 load() 之前调 _capability()，"
        "cuda 会先选上再静默回落")
    断言("onnxruntime_providers_cuda.dll" in worker,
        "L0-8 预检查的是 CUDA EP 的 DLL（纯 CPU 版 wheel 不带它）",
        "预检没查 onnxruntime_providers_cuda.dll")

    # --- 线程数上限 ---------------------------------------------------------
    # 实测（6P+8E，5 s 窗，4 次中位数）：4→0.991 / 6→0.907 / 8→1.052 / 20→4.698。
    # 超过 6 会有线程落到 E-core，慢核拖住同步点，整段被拉长。
    断言("min(6, os.cpu_count()" in worker,
        "L0-8 默认线程数上限 6（超 6 在 P+E 混合架构上断崖退化）",
        "默认线程数不是 min(6, 核数) —— 8 线程 5 s 窗口 rtf=1.05，追不上实时")
    断言("min(8, os.cpu_count()" not in worker,
        "L0-8 旧的 min(8, 核数) 没有残留",
        "代码里还留着 min(8, 核数)")

    # ---------------- L0-9：底部高级区的三项（预置 / 线程 / 模型档） ----------------
    # 预置项文字里那个「1.5×」不是修辞，是 窗口÷步长，也就是每秒要处理几秒音频。
    # 它一变，用户据此判断"我这台机器够不够"的结论就跟着错 —— 而这类错不抛异常。
    预置块 = re.search(
        r"_预置显示 As String\(\) = \{(.*?)\}\s*\n\s*Private Shared ReadOnly _预置窗口 As Double\(\) = \{(.*?)\}\s*\n\s*Private Shared ReadOnly _预置步长 As Double\(\) = \{(.*?)\}",
        form, re.S)
    断言(预置块 is not None,
        "L0-9 预置表三段（显示 / 窗口 / 步长）齐全",
        "找不到 _预置显示 / _预置窗口 / _预置步长 的相邻定义")
    if 预置块 is not None:
        显示 = re.findall(r'"([^"]*)"', 预置块.group(1))
        窗 = [float(x) for x in re.findall(r"\d+(?:\.\d+)?", 预置块.group(2))]
        步 = [float(x) for x in re.findall(r"\d+(?:\.\d+)?", 预置块.group(3))]
        断言(len(显示) == len(窗) == len(步),
            "L0-9 预置三段长度一致",
            "显示 %d 项 / 窗口 %d 项 / 步长 %d 项" % (len(显示), len(窗), len(步)))
        坏 = []
        for 名, w, s in zip(显示, 窗, 步):
            if w <= 0 and s <= 0:
                continue                      # 跟随包清单 / 自定义 没有自己的数值
            if s > w:
                坏.append("%s：步长 %.2f > 窗口 %.2f（会在窗与窗之间留空洞）" % (名, s, w))
                continue
            m = re.search(r"([\d.]+)×", 名)
            if m and abs(float(m.group(1)) - w / s) > 0.051:
                坏.append("%s：文字 %s× 实际 %.2f×" % (名, m.group(1), w / s))
        断言(not 坏,
            "L0-9 预置的倍率文字与实际 窗口÷步长 一致",
            "；".join(坏))

    # 线程数：worker 早就有 --threads，管道不传就等于这项调了没用（改设置重启一次，
    # 日志里线程数照样是自动）。
    断言('ArgumentList.Add("--threads")' in pipe,
        "L0-9 管道传 --threads（否则界面上的线程数是个摆设）",
        "启动参数里找不到 --threads")
    断言(re.search(r"已启动线程 <> 新线程", ctrl),
        "L0-9 控制器重启判据含线程数（启动期参数，中途改必须重启）",
        "应用设置 的重启条件里没有线程数")
    断言("Public Property Ai字幕线程数 As Integer" in st,
        "L0-9 设置带 Ai字幕线程数（0 = 自动）",
        "设置类里没有 Ai字幕线程数")

    # 模型档位：单档时必须报「这一档是什么形态、多大」，而不是一句「仅一个档位」；
    # 多档时每一项都带形态与体积 —— 用户加档的决策依据全在这里。
    断言("Ai字幕包.体积文本" in form and "Property 默认档形态 As String" in pack
         and "Property 形态 As String" in pack and "Property 体积字节 As Long" in pack,
        "L0-9 模型档位显示形态与体积",
        "找不到 体积文本 / 默认档形态 / 模型档.形态 之一")
    断言("此包仅一个档位" not in form,
        "L0-9 单档时不说「此包仅一个档位」这句废话",
        "单档分支又回到了只报「仅一个档位」——用户看不出这档是什么、多大")
    断言("MCB_模型档.ItemToolTips.Add" in form,
        "L0-9 模型档项挂提示（体积 + 目录，排查「这档指到哪」的唯一线索）",
        "模型档没有挂 ItemToolTips")

    # 本版只有 Qwen3-ASR 0.6B int8 一份权重：定了的事不能摆成选项。一个永远只有
    # 一项的下拉（哪怕置灰）在暗示"还能选别的"，用户就得试着点一遍才知道没有。
    # 所以单档位置必须是只读文字，下拉只在包清单真放了第二套权重时出现。
    断言("LBL_模型档值" in form and "LBL_模型档值.Text = If(详 = \"\", \"未标注量化形态\", 详)" in form,
        "L0-9 单档时模型一格是只读事实行",
        "找不到 LBL_模型档值 或它没有报形态体积")
    断言("MCB_模型档.Visible = 显示 AndAlso _有多档" in form
         and "LBL_模型档值.Visible = 显示 AndAlso Not _有多档" in form,
        "L0-9 下拉与只读行按「有没有第二档」互斥",
        "模型下拉不是常驻的条件可见 —— 单档时它又会出现")
    断言("_有多档 = 新.Count > 1" in form,
        "L0-9 有无第二档由 模型档表.Count 判定（不靠档名猜）",
        "找不到 _有多档 的赋值")

    # 名不副实的档比没有档更坏：档名写 int4、目录里是 int8，界面显示
    # 「int4（int8 · 941 MB）」，用户按错了精度做判断且不抛任何异常。
    断言("Function 形态相符" in pack and "If Not 形态相符(属性.Name, 形态) Then Continue For" in pack,
        "L0-9 档名声明的量化形态必须与目录实际形态一致",
        "读模型档表 不校验档名与形态，名不副实的档会进列表")
    样例 = read(SAMPLE_PACK)
    断言('"models"' not in 样例,
        "L0-9 样例清单不示范 models 多档（本版只有一份权重）",
        "pack-sample 又写了 models —— 复制出去就会在设置页多出一堆档位")
    # 字面判不行：_说明models 里本来就要举 int8 / int4 / fp16 / fp32 这些词。
    # 判的是结构 —— 样例目录下不能真躺着一个只被清单引用的第二套权重复制品。
    断言(not os.path.isdir(os.path.join(os.path.dirname(SAMPLE_PACK), "model-fp16")),
        "L0-9 样例目录不含 fp16 重复权重（本版只有一份 int8）",
        "pack-sample 下又出现了 model-fp16 目录")

    # 新控件必须挂进高级区折叠，否则收起时它还杵在面板上（L0-5 同一口径）。
    高级块 = re.search(r"Private Sub 显示高级\(显示 As Boolean\)(.*?)End Sub", form, re.S)
    for 控件 in ("LBL_线程", "MCB_线程"):
        断言(高级块 is not None and f"{控件}.Visible = 显示" in 高级块.group(1),
            f"L0-9 {控件} 挂进高级区可见性切换",
            "显示高级 里找不到该控件的 Visible 行")


    # 段分配默认表：界面「后端说明」对每个加速后端都承诺「int8 两段留 CPU」，
    # 引擎 STAGE_DEFAULTS 必须真的这么分 —— 否则用户选了 NPU/GPU，引擎把
    # decoder.int8 也送上去，轻则编译失败，重则算错不抛异常（假绿三形态之三）。
    eng = read(os.path.join(ROOT, "tools", "ai-subtitles", "qwen3_ort.py"))
    seg = {}
    for _ln in eng.splitlines():
        _s = _ln.strip()
        if _s.startswith('"') and _s.endswith('),') and '(' in _s and ': Backend(' not in _s:
            _key = _s.split('"')[1]
            _tup = _s[_s.index('(') + 1:_s.rindex(')')]
            _parts = [p.strip() for p in _tup.split(',')]
            if len(_parts) == 3 and all(p.startswith('"') and p.endswith('"') for p in _parts):
                seg[_key] = [p.strip('"') for p in _parts]
    断言(set(seg) == 引擎键,
        "L0-10 STAGE_DEFAULTS 覆盖全部后端（少一个 = 该后端没有缺省段分配）",
        "STAGE_DEFAULTS 的键=%s 与 BACKENDS=%s 不一致" % (sorted(seg), sorted(引擎键)))
    for _k, _row in seg.items():
        if _k in ("cpu", "ov-cpu"):
            _want = (_k, _k, _k)
        else:
            _want = (_k, "cpu", "cpu")
        断言(tuple(_row) == _want,
            "L0-10 %s 缺省段分配 = %s" % (_k, _want),
            "实际 %s —— 与界面「后端说明」的承诺不一致" % (_row,))
    断言("device in STAGE_DEFAULTS" in eng and "pick(conv_device" in eng,
        "L0-10 Engine 缺省走 STAGE_DEFAULTS（显式 --conv-device 才让位）",
        "Engine.__init__ 没有引用 STAGE_DEFAULTS —— 表是死的，三段仍全送 --device")

    # ---------------- L0-7：切文件/切解码器会话损坏（2026-10-25 实机 P0）------------
    # 实机矩阵第 6 项（切文件）暴露的永久沉默：worker 持续出 final、rtf 正常，
    # 但字幕图层恒为 呈现帧数=0 已绘制序号冻结 命令数=0 画布=0x0，一条不上屏；
    # 追加 seek 也无效。同一根因也造成用户报告的"切 GPU/CPU 解码黑屏中断"。
    # 三处修法任断一环，现象都是"没有字幕/没有画面"且不抛异常：
    #   C1 代次坐标系：DoOpen 必须自增 timelineGeneration（worker 唯一的重挂锚
    #      机制就是代次变化），EnableAudioTap 装的新 tap 必须盖当前代次——
    #      否则新 tap 从 0 开始而快照仍是旧值，代次守卫丢弃全部条目；
    #   A1 首帧呈现：SetOutputWindow 对"有视频流但无缓存帧"必须走
    #      PresentAfterBind()——Redraw() 在 !hasCachedVideo_ 时必然空转，
    #      暂停/就绪态的新会话永远等不到首帧；
    #   A2 自愈：SetWindow 同 HWND 早退不得跳过 swapchain 重建（设备恢复/
    #      渲染器拆装后 window_ 还在，再绑同一句柄会被卡死）；
    #   B  重绑失败不得静默吞（DXGI 会在旧 flip chain 未 retire 完时拒绝
    #      CreateSwapChainForHwnd，用户当时看不到任何提示）；
    #   +  worker 退出标志的代次 gate：旧结果线程迟到的 Finally 会把新 worker
    #      的每条结果全部丢掉（停止再播/改设置重启现场）。
    session = read(CPP_SESSION)
    render = read(CPP_RENDER)
    ctrler = read(VB_CTRLER)
    pipe = read(VB_PIPE)
    断言(re.search(r"\+\+snapshot_\.timelineGeneration;\s*\r?\n\s*NoteTapGeneration\(\);", session) is not None,
        "L0-7 DoOpen 自增代次并紧接着盖 tap（新文件 = 新时间轴）",
        "DoOpen 没有自增 timelineGeneration —— worker 端着旧文件的尾音按旧时钟报位置，"
        "宿主代次守卫又对不上，切文件后字幕整段沉默")
    断言(session.count("installed->NoteGeneration(snapshot_.timelineGeneration)") >= 1,
        "L0-7 EnableAudioTap 给新 tap 盖当前代次",
        "新 tap 从 0 开始而快照仍是旧代次 —— 每条音频块都被代次守卫丢弃")
    断言("videoRenderer_.PresentAfterBind()" in session and
        "!videoRenderer_.HasCachedVideo()" in session,
        "L0-7 SetOutputWindow 无缓存帧时强制首帧呈现",
        "普通视频仍只走 Redraw() —— headless 期间帧被丢，暂停/就绪态会话永远黑屏")
    断言("FFFResult PlayerVideoRenderer::PresentAfterBind() noexcept {" in render,
        "L0-7 PresentAfterBind 有实现（不是只声明）",
        "找不到 PresentAfterBind 定义")
    断言("window == window_ && (swapChain_ != nullptr || window == nullptr)" in render,
        "L0-7 SetWindow 同句柄早退带 swapchain 检查（缺 chain 就重建）",
        "同 HWND 早退不检查 chain —— 设备恢复后再绑同一句柄会被静默卡死")
    断言(ctrler.count("会话.设置输出窗口(窗口)") >= 2 and "视频输出绑定失败" in ctrler,
        "L0-7 重绑输出窗口失败要重试一次并上报（不静默吞）",
        "重绑仍只吞异常 —— 用户面对黑屏得不到任何解释")
    断言("Private _结果线程代 As Integer" in pipe and
        "Private Sub Result循环(代 As Integer)" in pipe and
        "Volatile.Read(_结果线程代) = 代" in pipe,
        "L0-7 结果线程收尾通知有代次 gate（旧线程迟到不许置退出标志）",
        "没有代次 gate —— 重启时旧结果线程的 Finally 会把新 worker 的每条结果全部丢掉")
    断言(pipe.count("Interlocked.Increment(_结果线程代)") >= 2,
        "L0-7 停/启两条路径都作废旧结果线程代次",
        "少一处自增就留一个竞态窗口（进程退场 ≠ 线程退场）")

    print(f"\n通过 {通过} / 失败 {失败}")
    return 失败


if __name__ == "__main__":
    sys.exit(main())
