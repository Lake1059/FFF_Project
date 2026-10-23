"""L0 静态自检：宿主语言表 ↔ worker 语言表是否漂移、设置页新增行是否放得下。

为什么需要它：
  * 宿主 `Ai字幕语言.名称表` 与 worker `LANGUAGE_NAMES` 是**两份**表（VB / Python）。
    两份一旦不一致，现象是"选了某语言 → 模型没提示 或 matched 永远 False"，
    而勾了"只显示所选语言"时就是整片空白，且不抛任何异常。
  * 设置页的语言行是脚本批量下移高级区之后加上去的（ClientSize 433 → 476），
    光靠肉眼没法确认有没有压字或越界。
  * 界面只列 4 个主项（其余 28 个未验证语种不上面板）：这里钉住"下拉只引用主项"，
    防止后人把循环改回整表、让未验证语种重新出现（L0-1b）。

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
        and "MTB_Gpu层数.Visible = 显示" in 高级块.group(1),
        "L0-1b GPU 层数控件挂进高级区可见性切换",
        "显示高级 里找不到 LBL_Gpu层数/MTB_Gpu层数 的 Visible 行")

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
    _, _, sy, _, _ = box("LBL_状态")
    断言(sy - (ly + lh) >= 8, "L0-2 语言下拉与状态行间距 ≥ 8px", f"实际 {sy - (ly + lh)}px")

    _, _, hy, _, hh = box("MCK_显示高级")
    断言(hy - (sy + 70) >= 8, "L0-2 状态行（3 行高 70）与高级区间距 ≥ 8px", f"实际 {hy - (sy + 70)}px")

    bottom = max(b[2] + b[4] for b in boxes)
    断言(ch - bottom >= 8, "L0-2 底部留白 ≥ 8px", f"最低控件 {bottom}px / 客户区 {ch}px")

    print(f"\n通过 {通过} / 失败 {失败}")
    return 失败


if __name__ == "__main__":
    sys.exit(main())
