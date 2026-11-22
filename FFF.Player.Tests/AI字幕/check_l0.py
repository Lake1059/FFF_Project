"""检查宿主语言表、字幕设置与 worker 协议的对应关系。

为什么需要它：
  * 宿主 `Ai字幕语言.名称表` 与 worker `LANGUAGE_NAMES` 是**两份**表（VB / Python）。
    两份一旦不一致，现象是"选了某语言 → 模型没提示 或 matched 永远 False"，
    而勾了"只显示所选语言"时就是整片空白，且不抛任何异常。
  * 界面只列 4 个主项（其余 28 个未验证语种不上面板）：这里钉住"下拉只引用主项"，
    防止后人把循环改回整表、让未验证语种重新出现（L0-1b）。
  * 高级设置直接显示，模型档在多档时才可修改，字幕偏移由状态机应用；
    已废止的 GPU 层数和附加参数不应重新出现，见 L0-5。

用法（从仓库根目录）：
    python FFF.Player.Tests/AI字幕/check_l0.py
退出码 = 失败项数。
"""
import io
import os
import re
import sys
import unicodedata

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
WORKER = os.path.join(ROOT, "FFF.Player", "AI字幕", "Worker", "asr_worker.py")
PY_ORT = os.path.join(ROOT, "FFF.Player", "AI字幕", "Worker", "qwen3_ort.py")

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
        cases.append(("zh-en", "只显示中文和 English"))
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
    dsg = read(VB_DESIGNER)
    语言项 = re.findall(r'^\s*MCB_字幕语言\.Items\.Add\("([^"]+)"\)', dsg, re.M)
    断言(语言项 == ["自动（显示所有语种）", "中文", "English", "中英混说"],
        "L0-1b 设置页设计器按顺序列出 4 个语言主项", str(语言项))
    下标块 = re.search(r"For i = 0 To Ai字幕语言\.(\w+)\.Length - 1\s*\n\s*If String\.Equals\(Ai字幕语言\.\w+\(i\), id", form)
    断言(下标块 is not None and 下标块.group(1) == "主项",
        "L0-1b 语言下标映射只认主项",
        "实际引用：" + (下标块.group(1) if 下标块 else "（没找到语言下标循环）"))
    # GPU 层数 / 附加参数 / 设备序号 三项已于 2026-10-29 从设置页删除：llama-server
    # 的参数全部由 worker 决定，用户逃生门没有可传的对象（见 Form设置_AI字幕 头部
    # 注释「删掉的老东西不再复活」）。所以这里钉的是**不许复活** —— 只把控件加回来
    # 而不接链路，就是「UI 看着能改、实际不生效」，比没有这项更糟。
    for 控件 in ("LBL_Gpu层数", "MCB_Gpu层数", "LBL_附加参数", "MTB_附加参数",
                 "LBL_设备序号", "MCB_设备序号"):
        断言(控件 not in dsg,
            f"L0-1b {控件} 未复活（旧 llama.cpp 时代控件已删）",
            f"Designer 里又出现了 {控件} —— 要恢复就得把整条链路接回，见 L0-4 / L0-5")

    # ---------------- L0-4：多模型适配契约不许回退 ----------------
    # 这批改动（2026-10-25）把"只认 Qwen3-ASR 双文件包"的硬约束逐个拆掉，
    # 让扩展包能带任意 llama-server 能加载的 gguf。三条链任断一环，
    # 现象都是"塞了新模型但跑的还是老行为/参数不生效"，且不抛异常：
    worker = read(WORKER)
    # 附加参数（--llama-arg）这条链 2026-10-29 整体废止：worker 自己决定
    # llama-server 的全部参数（--temp 0 / ctx / 模板），没有可交给用户的口子。
    # 钉"不许复活"而不是"必须有" —— 只把口子加回来而没有 UI 链，等于埋一个
    # 永不生效的参数。
    断言(not re.search(r'add_argument\("--llama-arg"', worker),
        "L0-4 worker 不收 --llama-arg（附加参数链已废止）",
        "worker 又有 --llama-arg 了 —— 要么把整条 UI 链接回，要么删掉这个口子")

    # mmproj 是另一回事：它是**音频编码器**，不是可调项，双文件模型必须有。
    # 找的是真实写法（_llama_weights 校验后无条件传），不是某一版的条件传形状 ——
    # 按旧形状找会误报"功能被删了"，而它其实是缺了就转不动音频的硬依赖。
    断言('"--mmproj", str(mmproj)' in worker,
        "L0-4 worker 仍传 --mmproj（音频编码器，缺了只认文字不认音频）",
        "start() 里找不到 --mmproj 传参 —— 模型会退化成纯文字")
    断言(re.search(r"没有 mmproj", worker),
        "L0-4 worker 强制校验 mmproj 存在（缺了要报错，不能静默退化）",
        "找不到缺 mmproj 的报错 —— 静默退化成纯文字模型是最难排查的一类")

    pipe = read(VB_PIPE)
    映射块 = re.search(r'For Each 附加 In 合并参数(.*?)Next', pipe, re.S)
    # 必须是等号形式（"--llama-arg=" & 附加）：拆成两个 token 传的话，argparse 的
    # append action 不收以 -- 开头的值，llamaArgs 的标准用法（["--ctx-size","8192"]）
    # 会让 worker 启动即 exit 2（2026-10-25 审查实测，见 PACK_FORMAT §2.5）。
    断言(映射块 is None,
        "L0-4 管道不再映射 --llama-arg=（附加参数链已废止）",
        "管道又在拼 --llama-arg —— worker 已经不认这个参数，拼了等于启动即退出")

    pack = read(VB_PACK)
    # ---------------- L0-3b：UI 摆出来的后端必须是 worker 真认的 ----------------
    # 宿主下拉里每一项都要能在 worker 的 _CANDIDATES 里找到落脚点。找不到的话
    # _normalize_request 会把请求值静默归 auto —— 用户选了「AMD 显卡 / DirectML」，
    # 实际跑的是 CUDA 或 CPU，而且**没有任何降档日志**（auto 本来就会走 CUDA）。
    # 这是最难自查的一类：界面上一切正常，只是那个选择从来没生效过。
    dml_段 = re.search(r"_ENGINE_ONNX:\s*\{(.*?)_ENGINE_LLAMA:", worker, re.S)
    onnx_键 = set(re.findall(r'"([\w\-]+)":\s*\(', dml_段.group(1))) if dml_段 else set()
    llama_段 = re.search(r"_ENGINE_LLAMA:\s*\{(.*?)\n\}", worker, re.S)
    llama_键 = set(re.findall(r'"([\w\-]+)":\s*\(', llama_段.group(1))) if llama_段 else set()
    全部键 = onnx_键 | llama_键
    # 宿主后端全集（Ai字幕推理档.后端全部）
    全集块 = re.search(r"后端全部 As String\(\) = \{([^}]*)\}", pack)
    断言(全集块 is not None, "L0-3b 能从宿主解析出后端全集", "找不到 后端全部 定义")
    后端全集 = set(re.findall(r"后端(\w+)", 全集块.group(1)))
    常量值 = dict(re.findall(r'后端(\w+) As String = "([\w\-]+)"', pack))
    落空 = sorted(常量值[k] for k in 后端全集 if 常量值.get(k) not in 全部键)
    断言(not 落空,
        "L0-3b 宿主每个后端都在 worker 调度表里登记（不许静默归 auto）",
        "这些后端 UI 上有、worker 表里没有：" + str(落空) +
        " —— 选中后 _normalize_request 会静默归 auto，等于选了没选")
    # DirectML 是这条坑的原爆点，单独钉一条：它是 AMD 在 Windows 上的唯一显卡路径
    # （ROCm / MIGraphX 都没有 Windows 构件），缺了它整个设备项就是空话。
    断言("dml" in onnx_键,
        "L0-3b worker 的 ONNX 调度表登记了 dml（AMD 唯一显卡路径）",
        "onnx 候选里没有 dml —— AMD 用户选「AMD 显卡」会被静默按 auto 处理")
    断言('if backend_id == "dml":' in worker,
        "L0-3b dml 有独立的 _capability 预检（缺 qwen3_ort 要明确跳过）",
        "_capability 不认 dml —— 缺实现文件时会静默放行到 load() 才炸")
    # 同上：整条链废止，包清单也不再读 llamaArgs。留着字段而没人用 = 埋雷。
    断言(not re.search(r'"llamaArgs"', pack),
        "L0-4 包载入不再读 llamaArgs（附加参数链已废止）",
        "Ai字幕包.vb 又在读 llamaArgs —— 读了要给谁用？")
    断言(not re.search(r"Friend Property 额外参数 As", pack),
        "L0-4 包配置不带 额外参数 字段（已废止）",
        "Ai字幕包配置 又有 额外参数 —— 没有消费方就是死字段")
    断言(not re.search(r"读附加参数", pack),
        "L0-4 载入路径不再调 读附加参数（已废止）",
        "还有 读附加参数 的调用 —— 链路复活要整条接，不能只接一半")

    # ---------------- L0-5：高级设置布局与字幕偏移 ----------------
    st = read(VB_SET)
    断言('Ai字幕附加参数' not in st,
        "L0-5 设置不再带 Ai字幕附加参数（逃生门已废止）",
        "设置.vb 又有 Ai字幕附加参数 —— 存了没有消费方，等于用户填了个空")
    断言('Public Property Ai字幕字幕偏移秒 As Double' in st,
        "L0-5 设置带 Ai字幕字幕偏移秒（字幕驻留偏移）")
    断言(re.search(r'Ai字幕字幕偏移秒 < -3\.0 OrElse Ai字幕字幕偏移秒 > 3\.0', st),
        "L0-5 偏移规范化钳在 [-3, +3]",
        "规范化() 里找不到偏移的区间钳制——脏设置会把字幕守卫带飞")

    # layers（GPU 层数）同样废止：EP 自己决定算子落在哪，没有"卸几层"这回事。
    断言(not re.search(r'"layers"', pack),
        "L0-5 包载入不再读 layers（GPU 层数控件已废止）",
        "Ai字幕包.vb 又在读 layers —— 层数不是可调项，读了也只能摆个无效下拉")
    断言(not re.search(r"Friend Property 模型层数 As", pack),
        "L0-5 包配置不带 模型层数 字段（已废止）",
        "Ai字幕包配置 又有 模型层数 —— 同上，没有消费方")

    fsm = read(VB_FSM)
    断言(re.search(r"Public Property 时间偏移100纳秒 As Long", fsm),
        "L0-5 状态机带 时间偏移100纳秒（驻留偏移入口）")
    断言(re.search(r"播放位置\.Ticks - 时间偏移100纳秒", fsm),
        "L0-5 守卫判定用 播放位置 − 偏移",
        "取当前条目 里找不到偏移项——偏移设了等于没设")

    form = read(VB_FORM)
    断言(not re.search(r"MCK_显示高级|Private Sub 显示高级\b", form + "\n" + dsg),
        "L0-5 高级选项不再依赖折叠开关")
    advanced_rows = {
        "MP_高级下拉": ("MCB_节奏预置", "MCB_推理线程"),
        "MP_高级时间": ("MP_窗口列", "MP_步长列", "MP_偏移列", "ModernPanel2"),
    }
    for row, controls in advanced_rows.items():
        断言(f"ModernPanel1.Controls.Add({row})" in dsg and f"{row}.Visible = False" not in dsg,
            f"L0-5 {row} 默认显示在页内")
        for control in controls:
            断言(f"{row}.Controls.Add({control})" in dsg, f"L0-5 {control} 位于 {row}")
    for column, text_box in (("MP_窗口列", "MTB_窗口秒"), ("MP_步长列", "MTB_步长秒"), ("MP_偏移列", "MTB_偏移")):
        断言(f"{column}.Controls.Add({text_box})" in dsg, f"L0-5 {text_box} 位于 {column}")
    断言("ModernPanel2.Controls.Add(MCB_模型档)" in dsg and "MCB_模型档.Enabled = _有多档" in form,
        "L0-5 模型档放在高级时间行，仅在多档时可修改")
    断言(not re.search(r"Private Sub 重建层数下拉", form),
        "L0-5 不再有 GPU 层数下拉（层数控件已废止）",
        "重建层数下拉 回来了 —— 层数不是可调项，重建了也是假选项")
    断言(not re.search(r"MTB_附加参数", form),
        "L0-5 设置页不再有附加参数框（已废止）",
        "MTB_附加参数 回来了 —— 没有消费方的输入框是说谎")
    断言(not re.search(r"解析附加参数", form),
        "L0-5 状态行不再数附加参数（已废止）",
        "状态行又去数附加参数 —— 那一行会说一个不存在的条数")

    断言(not re.search(r"附加参数", pipe),
        "L0-5 管道不再处理附加参数（已废止）",
        "管道里又出现 附加参数 —— 解析/合并都得有消费方，否则是死代码")

    ctrl = read(VB_CTRL)
    断言(not re.search(r"已启动附加参数", ctrl),
        "L0-5 控制器不再判附加参数（已废止）",
        "控制器又在判 已启动附加参数 —— 判一个不存在的设置项，条件永远为假")
    断言(re.search(r"状态机\.时间偏移100纳秒 = CLng\(", ctrl),
        "L0-5 控制器把偏移同步进状态机（应用设置即生效，不重启 worker）")
    # 文案与实测对齐：早期写「十几秒」，实测（2026-10-25，CUDA0）前 1~2 个转写
    # 请求各需 57~60s。提示低估会让人按错的方向排查（以为卡死），所以钉住口径。
    断言(re.search(r"AI 字幕：模型加载中", ctrl),
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
    for reason in ("empty", "isolated", "degenerate", "leak"):
        断言(f'reason = "{reason}"' in worker, f"L0-6 无有效对白的统计覆盖 {reason}")
    断言('"filtered": filtered' in worker and "字幕不会凭空编字" not in worker,
        "L0-6 普通无对白仅计入诊断，无操作提示")

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

    # ---------------- L0-8：分窗语义不许退化 ----------------
    # 触发阈值必须是 window_s。用 step_s 的话窗长会退化成 Δ + (window - step)，
    # Δ 是采集块间隔（几十毫秒）—— 默认 5/3 实际每窗只有 ~2 秒音频进模型，
    # 「完整 8 s / 4 s」实际约 4 秒，窗口秒这一格名不副实。
    断言("now_end100ns - self.window_start < int(self.window_s * 1e7)" in worker,
        "L0-8 出窗阈值是 window_s（攒够窗口才出，不是过了步长就出）",
        "阈值又退回 step_s —— 窗长会退化成 window-step，窗口秒这一格等于骗人")
    断言("self.window_start = start + int(self.step_s * 1e7)" in worker,
        "L0-8 出窗后按 step 精确推进左边界",
        "advance 写法变了 —— 要先确认步进仍是 step_s")

    # ---------------- L0-9：NPU 路径的两条不变量 ----------------
    # 第三方在 Ultra 9 285H 上的实测（2026-10-29）给了两个以前只能靠推理的事实：
    #   A. 无上界的动态维不是"编不过"，是 LLVM ERROR + 退出码 127 直接 abort。
    #      abort 不经过 Python 的异常处理，worker 会当场消失，连"降档到 CPU"的
    #      机会都没有 —— 所以必须在 compile_model 之前把它变成一次普通异常。
    #   B. 那台机器 GPU.0=Intel Arc 140T、GPU.1=RTX 5060：OpenVINO 把 N 卡也列进
    #      GPU.x，而设备名 "GPU" 取第一块。不按厂商钉，"选 Intel 显卡"会编到
    #      NVIDIA 上，且编译成功、数值也对 —— 无从察觉。
    ort = read(PY_ORT)
    断言("_assert_bounded(self.model" in ort,
        "L0-9 NPU 编译前断言输入维有有限上界",
        "找不到 _assert_bounded 调用 —— 无上界会让 vpux 编译器 abort（退出码 127），"
        "Python 抓不到，worker 会当场消失而不是降档")
    断言("if bounded_frames and backend.target" not in ort,
        "L0-9 上界缺失时不许静默跳过 reshape",
        "又写成 bounded_frames and ... —— 没传上界就跳过 reshape 去硬编译，正是会崩的那种写法")
    断言("def _assert_bounded(" in ort and "def _reshape_bounded(" in ort,
        "L0-9 上界处理是两个独立函数（reshape + 断言）",
        "reshape 与断言被合并或删掉了 —— 改回去要确认两件事仍都做到")
    断言("def _pick_intel_gpu(" in worker and "_pick_intel_gpu(core, have)" in worker,
        "L0-9 ov-gpu 按厂商挑 Intel 那块（多显卡时不许编到别家）",
        "找不到 _pick_intel_gpu —— OpenVINO 把 N 卡也列进 GPU.x，"
        "不钉就会在 NVIDIA 上编译还不报错")
    断言("ov_device=ov_dev" in worker,
        "L0-9 挑到的设备真的传进了 Engine",
        "Engine 调用没有 ov_device —— 挑了不用，等于没挑")


    print(f"\n通过 {通过} / 失败 {失败}")
    return 失败


if __name__ == "__main__":
    sys.exit(main())
