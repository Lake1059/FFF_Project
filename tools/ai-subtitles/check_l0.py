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
WORKER = os.path.join(ROOT, "tools", "ai-subtitles", "asr_worker.py")

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
    # GPU 层数控件是后加进高级区的：必须挂进 显示高级 的可见性切换，否则折叠
    # 高级区时它还杵在面板上（与模型档同一批加，同一批漏过可见性的先例）。
    高级块 = re.search(r"Private Sub 显示高级\(显示 As Boolean\)(.*?)End Sub", form, re.S)
    断言(高级块 is not None and "LBL_Gpu层数.Visible = 显示" in 高级块.group(1)
        and "MCB_Gpu层数.Visible = 显示" in 高级块.group(1),
        "L0-1b GPU 层数控件挂进高级区可见性切换",
        "显示高级 里找不到 LBL_Gpu层数/MCB_Gpu层数 的 Visible 行")

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
    重叠 = []
    for i in range(len(boxes)):
        for j in range(i + 1, len(boxes)):
            n1, x1, y1, w1, h1 = boxes[i]
            n2, x2, y2, w2, h2 = boxes[j]
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

    # ---------------- L0-4：多模型适配契约不许回退 ----------------
    # 这批改动（2026-10-25）把"只认 Qwen3-ASR 双文件包"的硬约束逐个拆掉，
    # 让扩展包能带任意 llama-server 能加载的 gguf。三条链任断一环，
    # 现象都是"塞了新模型但跑的还是老行为/参数不生效"，且不抛异常：
    worker = read(WORKER)
    断言(re.search(r'add_argument\("--llama-arg".*action="append"', worker),
        "L0-4 worker 收 --llama-arg（append 多值）",
        "找不到 --llama-arg 的 add_argument（或不是 append）")
    断言("_user_first(defaults, self.extra)" in worker,
        "L0-4 worker 固定默认值遇同名 flag 让位（_user_first）",
        "start() 里没有走 _user_first —— 用户的 llamaArgs 会被固定参数压掉")
    断言("if self.mmproj:" in worker,
        "L0-4 worker 的 --mmproj 条件传（单文件模型不传投影）",
        "找不到 if self.mmproj: —— 无投影的模型会被塞一个空路径")
    断言('"--mmproj", str(self.mmproj)' in worker,
        "L0-4 worker 仍保留 mmproj 支持（拆约束不是删功能）",
        "start() 里找不到 --mmproj 传参 —— 双文件模型被改坏了")

    pipe = read(VB_PIPE)
    映射块 = re.search(r'For Each 附加 In 合并参数(.*?)Next', pipe, re.S)
    # 必须是等号形式（"--llama-arg=" & 附加）：拆成两个 token 传的话，argparse 的
    # append action 不收以 -- 开头的值，llamaArgs 的标准用法（["--ctx-size","8192"]）
    # 会让 worker 启动即 exit 2（2026-10-25 审查实测，见 PACK_FORMAT §2.5）。
    断言(映射块 is not None and '"--llama-arg=" & 附加' in (映射块.group(1) if 映射块 else ""),
        "L0-4 管道把（合并后的）附加参数逐项映射为 --llama-arg=（等号形式）",
        "找不到 For Each 附加 In 合并参数 → --llama-arg= 的映射（或退回了拆分形式）")
    断言(re.search(r"合并附加参数\(配置\.额外参数,", pipe),
        "L0-4 包清单 llamaArgs 仍进命令行（走合并入口，不是被用户参数顶掉）",
        "启动路径里找不到 合并附加参数(配置.额外参数, …)——清单参数被改丢了")

    pack = read(VB_PACK)
    断言('TryGetProperty("llamaArgs"' in pack,
        "L0-4 包载入读 llamaArgs 字段",
        "Ai字幕包.vb 里找不到 llamaArgs 的读取")
    断言(re.search(r"Friend Property 额外参数 As New List\(Of String\)", pack),
        "L0-4 包配置带 额外参数 字段",
        "Ai字幕包配置 里没有 额外参数 字段")
    # 方法还在但没人调 = 白写：载入路径必须真的走到 读附加参数（区别于函数定义行）。
    断言('读附加参数(根, 配置)' in pack,
        "L0-4 载入路径调用 读附加参数（方法有被调用）",
        "找不到 读附加参数(根, 配置) 调用点 —— 字段读了也没人填")

    # ---------------- L0-5：「扩展 UI + 按模型能力开放」契约不许回退 ----------------
    # 2026-10-25 这一轮把设置页从"只认 Qwen3-ASR 双文件 28 层"扩成按包声明的能力
    # 开放：清单 layers 驱动 GPU 层数预置、用户 llamaArgs 接进 UI 并与清单合并、
    # 驻留偏移进状态机、新控件全部挂进折叠区。任一处断链，现象都是"UI 看着能改、
    # 实际不生效"或"折叠后控件杵在面板上"，且不抛异常。
    st = read(VB_SET)
    断言('Public Property Ai字幕附加参数 As String' in st,
        "L0-5 设置带 Ai字幕附加参数（用户侧 llamaArgs 逃生门）",
        "设置.vb 里没有 Ai字幕附加参数 —— 用户框填了没处存")
    断言('Public Property Ai字幕字幕偏移秒 As Double' in st,
        "L0-5 设置带 Ai字幕字幕偏移秒（字幕驻留偏移）")
    断言(re.search(r'Ai字幕字幕偏移秒 < -3\.0 OrElse Ai字幕字幕偏移秒 > 3\.0', st),
        "L0-5 偏移规范化钳在 [-3, +3]",
        "规范化() 里找不到偏移的区间钳制——脏设置会把字幕守卫带飞")

    断言(re.search(r'读整数\(根, "layers"', pack),
        "L0-5 包载入读 layers 字段（模型层数，走通用整数助手）",
        "Ai字幕包.vb 里找不到 layers 的读取——GPU 层数表又变回写死 28 层")
    断言(re.search(r"Friend Property 模型层数 As Integer", pack),
        "L0-5 包配置带 模型层数 字段")

    fsm = read(VB_FSM)
    断言(re.search(r"Public Property 时间偏移100纳秒 As Long", fsm),
        "L0-5 状态机带 时间偏移100纳秒（驻留偏移入口）")
    断言(re.search(r"CLng\(播放位置\.Ticks\) - 时间偏移100纳秒", fsm),
        "L0-5 守卫判定用 播放位置 − 偏移",
        "取当前条目 里找不到偏移项——偏移设了等于没设")

    form = read(VB_FORM)
    高级块 = re.search(r"Private Sub 显示高级\(显示 As Boolean\)(.*?)End Sub", form, re.S)
    for 控件 in ("LBL_附加参数", "MTB_附加参数", "LBL_偏移", "MTB_偏移", "LBL_预置", "MCB_预置"):
        断言(高级块 is not None and f"{控件}.Visible = 显示" in 高级块.group(1),
            f"L0-5 {控件} 挂进高级区可见性切换",
            "显示高级 里找不到该控件的 Visible 行——折起来它还杵在面板上")
    断言(re.search(r"Private Sub 重建层数下拉", form),
        "L0-5 GPU 层数下拉按包 layers 动态重建",
        "找不到 重建层数下拉 —— 层数表写死，换架构模型时预置档全是错的")
    断言(re.search(r"MTB_附加参数\.Enabled = 有效", form),
        "L0-5 附加参数框按包有效性开放/置灰（没包时不能填）")
    断言(re.search(r"解析附加参数\(设置\.实例对象\.Ai字幕附加参数\)", form),
        "L0-5 状态行数一眼用户附加参数条数（能力行数据源）")

    断言(re.search(r"Friend Shared Function 解析附加参数", pipe),
        "L0-5 管道解析用户附加参数（按行拆分、# 注释、去空行）")
    断言(re.search(r"Private Shared Function 合并附加参数", pipe),
        "L0-5 管道合并包清单与用户附加参数（同名 flag 用户覆盖包）",
        "找不到 合并附加参数——用户参数会与清单参数重复传给 llama-server")
    断言(re.search(r"解析附加参数\(用户附加参数\)", pipe),
        "L0-5 启动路径解析用户附加参数文本")
    断言(re.search(r"合并附加参数\(配置\.额外参数, 用户参数\)", pipe),
        "L0-5 启动路径真的走合并（两个来源都进命令行）",
        "启动() 里找不到 合并附加参数(配置.额外参数, 用户参数)——用户参数被丢了")

    ctrl = read(VB_CTRL)
    断言(re.search(r"已启动附加参数 <> 设置对象\.Ai字幕附加参数", ctrl),
        "L0-5 控制器把附加参数纳入重启判定",
        "应用设置 的重启条件里没有 已启动附加参数——改了参数不重启，命令行还是旧的")
    断言(re.search(r"状态机\.时间偏移100纳秒 = CLng\(", ctrl),
        "L0-5 控制器把偏移同步进状态机（应用设置即生效，不重启 worker）")
    # 文案与实测对齐：早期写「十几秒」，实测（2026-10-25，CUDA0）前 1~2 个转写
    # 请求各需 57~60s。提示低估会让人按错的方向排查（以为卡死），所以钉住口径。
    断言(re.search(r"模型加载中，首句字幕约需 1 分钟", ctrl),
        "L0-5 首启加载提示（空窗期不解释，用户会以为开了没反应）")

    # ---------------- L0-6：实机验收抓到的输出护栏不许回退 ----------------
    # 2026-10-25 用真实影片片段验收才暴露的两个缺陷：无语音段模型编字（3808 个
    # 「啊」，占满 ctx）、语音稀疏段把 instruction 当内容输出（"Transcribe audio
    # to text"）。两者都不会抛异常，只会把一屏编造的字糊在画面上——用户看到的
    # 是"字幕功能坏了/模型在胡说"，比没字幕更糟。护栏任断一环，交互测试都看不出来
    # （界面上字幕照常出，只是内容是假的）。
    断言('"--n-predict", "512"' in worker,
        "L0-6 默认命令行带 --n-predict 512（失控输出的长度上限）",
        "worker 默认参数里找不到 --n-predict 512 —— 失控窗会再次占满 ctx")
    断言('"--no-cache-idle-slots"' not in worker,
        "L0-6 默认命令行不带 --no-cache-idle-slots（实测 RTF 元凶）",
        "它回来了：5 秒窗实测 7.6~30.5s（rtf 1.5~6.7），字幕永久滞后于画面")
    断言("def looks_degenerate(" in worker and "looks_degenerate(text)" in worker,
        "L0-6 失控编字护栏在（定义 + 主循环调用）",
        "looks_degenerate 没被调用 —— 无语音段会再次糊一屏重复字")
    断言("def looks_like_prompt_leak(" in worker and "looks_like_prompt_leak(text)" in worker,
        "L0-6 指令泄漏护栏在（定义 + 主循环调用）",
        "looks_like_prompt_leak 没被调用 —— 模板串会原样上屏")
    # 三种"这窗没有有效内容"，缺一种就漏一种污染：empty（只有语言头）/
    # degenerate（重复字）/ leak（模板串）。
    for 原因 in ("empty", "degenerate", "leak"):
        断言(f'原因 = "{原因}"' in worker,
            f"L0-6 丢弃分支覆盖 {原因}",
            f"主循环里找不到 原因 = \"{原因}\" —— 这类内容会重新上屏")
    断言("字幕不会凭空编字" in worker,
        "L0-6 丢弃时给用户一次解释（空白字幕 + 无提示 = 用户以为坏了）",
        "找不到丢弃提示文案 —— 用户面对空白字幕得不到任何解释")

    print(f"\n通过 {通过} / 失败 {失败}")
    return 失败


if __name__ == "__main__":
    sys.exit(main())
