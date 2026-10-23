#!/usr/bin/env python3
"""把一份"绝对路径"的 AI 字幕扩展包变成可搬走的目录包。

一个扩展包 = 一个目录 + ai-subtitles.json。清单里的路径可以是绝对路径（指向
机器上已有的 venv / llama-server / 模型），也可以是相对包根的路径（真正便携）。
这个脚本做两件事：

  1. --emit   ：只重写清单，把绝对路径换成相对路径（前提是你已经把文件拷进包了）
  2. --copy   ：先把清单里指的运行时/模型/二进制拷进包目录，再重写清单

llama-server 特殊：CUDA 构建和 cublasLt / ggml-cuda 等一堆 DLL 同目录，只拷
exe 过去目标机器起不来。父目录文件数 >= 4 时整目录搬进 runtime/<原目录名>/，
清单 llamaServer 与 servers 映射都指向里面的 llama-server.exe；体积超过 2 GB
直接拒绝（通常是指错了上层目录）。

用法：
    # 1) 看看它会拷什么、生成什么清单（不动磁盘）
    python make_portable_pack.py --source E:/.../3FP-AI-Subtitles-Pack --target E:/.../pack-portable --copy --dry-run

    # 2) 真拷（venv + 模型 + llama-server，几 GB，自己看 --dry-run 的体积再决定）
    python make_portable_pack.py --source ... --target ... --copy

    # 2b) 连 venv 一起拷（目标机仍需同大版本 Python，见下）
    python make_portable_pack.py --source ... --target ... --copy --copy-python

    # 3) 文件已经拷好了，只要重写清单
    python make_portable_pack.py --source ... --target ... --emit

关于 venv 的边界（必须知道）：
    普通的 `python -m venv` **不能真正跨机器搬**：Scripts/*.exe 是转发器，
    pyvenv.cfg 指向创建它的 base python。所以 --copy 把 venv 拷过来之后，
    目标机器上仍然需要同一大版本（3.1x）的 Python 存在，否则起不来。
    要彻底免依赖，请用 python-build-standalone / embeddable 发行版当
    "包内 python"，清单的 python 指向它即可 —— 本脚本不替你下这个发行版，
    但会把它的路径按普通文件处理。
"""

import argparse
import json
import shutil
import sys
from pathlib import Path

MANIFEST = "ai-subtitles.json"

# 清单里可能指向文件的键 -> 拷进包内的目标子目录
COPY_LAYOUT = {
    "worker": ".",
    "model": "models",
    "mmproj": "models",
    "llamaServer": "runtime",
}
# python 不放进 COPY_LAYOUT：venv 是一个目录树，单独处理。
PYTHON_SUBDIR = "runtime/python"
# llama-server 特殊处理：CUDA 构建和 cublasLt / ggml-cuda 等一堆 DLL 同目录，
# 只拷 exe 过去目标机器直接起不来。父目录文件数达到这个值就整目录搬
# （dest: runtime/<原目录名>/），同时用体积上限防"指到上层目录"的误操作。
RUNTIME_DIR_MIN_FILES = 4
RUNTIME_DIR_MAX_BYTES = 2 * 1024 ** 3


def load(source):
    path = source / MANIFEST
    if not path.is_file():
        sys.exit(f"源目录里没有 {MANIFEST}: {source}")
    return json.loads(path.read_text(encoding="utf-8-sig"))


def resolve(base, value):
    if not value or not isinstance(value, str):
        return None
    p = Path(value.strip().strip('"'))
    return p if p.is_absolute() else (base / p)


def dir_size(path):
    if path.is_file():
        return path.stat().st_size
    total = 0
    for item in path.rglob("*"):
        try:
            if item.is_file():
                total += item.stat().st_size
        except OSError:
            pass
    return total


def human(n):
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return f"{n:.1f} {unit}"
        n /= 1024.0
    return f"{n:.1f} GB"


def plan(source, manifest, copy_python):
    """返回 [(源路径, 包内相对路径, 键名)]。只列需要动的。"""
    items = []
    for key in ("worker", "model", "mmproj"):
        raw = manifest.get(key)
        src = resolve(source, raw)
        if src is None or not src.exists():
            continue
        items.append((src, Path(COPY_LAYOUT[key]) / src.name, key))

    # llama-server：单文件是少数情况，多数构建（CUDA / Vulkan）带一堆同目录 DLL。
    llm = resolve(source, manifest.get("llamaServer"))
    if llm is not None and llm.exists():
        parent = llm.parent
        n_files = sum(1 for p in parent.iterdir() if p.is_file())
        if n_files >= RUNTIME_DIR_MIN_FILES:
            size = dir_size(parent)
            if size > RUNTIME_DIR_MAX_BYTES:
                sys.exit(f"llama-server 所在目录 {parent}（{n_files} 个文件，{human(size)}）"
                         f"超过 {human(RUNTIME_DIR_MAX_BYTES)} 上限：通常是指到了上层目录，"
                         f"请把清单 llamaServer 改到具体某个构建目录下的 llama-server.exe。")
            items.append((parent, Path("runtime") / parent.name, "llamaServer"))
        else:
            items.append((llm, Path("runtime") / llm.name, "llamaServer"))

    # models：{档名: {model, mmproj}}。每档的权重都要一起搬，否则到了别人机器上
    # 下拉里那几个档位就会整档消失（载入时路径不存在 ⇒ 丢弃该档）。
    for tier, entry in (manifest.get("models") or {}).items():
        if not isinstance(entry, dict):
            continue
        for key in ("model", "mmproj"):
            src = resolve(source, entry.get(key))
            if src is None or not src.exists():
                continue
            items.append((src, Path(COPY_LAYOUT[key]) / src.name, f"models.{tier}.{key}"))

    if copy_python:
        pysrc = resolve(source, manifest.get("python"))
        if pysrc is not None and pysrc.exists():
            # venv 时 Scripts/python.exe 上一级是包根；整个环境都要带走。
            env_root = pysrc.parent.parent if pysrc.parent.name.lower() in ("scripts", "bin") else pysrc.parent
            items.append((env_root, Path(PYTHON_SUBDIR), "python"))
    return items


def rel(target_root, path):
    """包内相对路径，正斜杠（清单规定正反斜杠都收，但正斜杠更不容易被转义吃掉）。"""
    return Path(path).as_posix()


def build_manifest(manifest, source, target, items, copy_python):
    """按 items 的结果重写路径：包内文件写相对，包外文件原样保留绝对路径。"""
    moved = {key: dest for _, dest, key in items}
    out = {k: v for k, v in manifest.items() if not k.startswith("_")}

    for key in ("worker", "model", "mmproj"):
        if key in moved:
            out[key] = rel(target, moved[key])

    if "llamaServer" in moved:
        # 目录型搬运时 moved 值是目录，exe 名要拼回去（Windows 构建就叫这个名）。
        dest = moved["llamaServer"]
        if (target / dest).is_dir() or dest.name != "llama-server.exe":
            dest = dest / "llama-server.exe"
        out["llamaServer"] = rel(target, dest)

    if "python" in moved:
        # venv 布局：<env>/Scripts/python.exe（Windows）或 <env>/bin/python（POSIX）
        # Windows 候选兜底：dry-run 时目标目录还不存在，exists() 全不通过会让
        # 预览里的 python 停留在绝对路径，看着像"没打算拷"。
        for candidate in ("Scripts/python.exe", "bin/python3", "bin/python"):
            p = Path(moved["python"]) / candidate
            if (target / p).exists() or candidate == "Scripts/python.exe":
                out["python"] = rel(target, p)
                break

    # models 表：已搬进包内的写成相对路径，包外的保持绝对。
    tiers = manifest.get("models")
    if isinstance(tiers, dict) and tiers:
        out["models"] = {}
        for tier, entry in tiers.items():
            if not isinstance(entry, dict):
                continue
            new_entry = dict(entry)
            for key in ("model", "mmproj"):
                moved_key = f"models.{tier}.{key}"
                if moved_key in moved:
                    new_entry[key] = rel(target, moved[moved_key])
            out["models"][tier] = new_entry

    # servers 表里的路径可能指向同一个 llama-server，也要跟着改。
    servers = manifest.get("servers")
    if isinstance(servers, dict) and "llamaServer" in moved:
        llm_src = resolve(source, manifest.get("llamaServer"))
        rebuilt = {}
        for backend, value in servers.items():
            src = resolve(source, value)
            if src is not None and llm_src is not None and src == llm_src:
                rebuilt[backend] = out["llamaServer"]  # 已拼好 exe 名
            elif src is not None and src.exists() and target in src.parents:
                rebuilt[backend] = rel(target, src.relative_to(target))
            elif src is not None:
                rebuilt[backend] = str(src)      # 包外，保持绝对
        if rebuilt:
            out["servers"] = rebuilt

    return out


def main():
    ap = argparse.ArgumentParser(description="生成可搬走的 AI 字幕扩展包")
    ap.add_argument("--source", required=True, help="现有包目录（含 ai-subtitles.json）")
    ap.add_argument("--target", required=True, help="目标包目录（不存在则创建）")
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--emit", action="store_true", help="只重写清单，不拷文件")
    mode.add_argument("--copy", action="store_true", help="拷运行时/模型/二进制再重写清单")
    ap.add_argument("--copy-python", action="store_true",
                    help="连 python 环境一起拷（venv 通常不能跨机器，见文件头说明）")
    ap.add_argument("--dry-run", action="store_true", help="只打印计划与体积，不落盘")
    args = ap.parse_args()

    source = Path(args.source).resolve()
    target = Path(args.target).resolve()
    manifest = load(source)

    items = plan(source, manifest, args.copy_python)
    if not items:
        sys.exit("清单里没有可解析的路径（worker/model/mmproj/llamaServer 全是空或不存在的？）")

    total = 0
    print(f"源: {source}")
    print(f"目标: {target}")
    print(f"模式: {'copy' if args.copy else 'emit'}{' (dry-run)' if args.dry_run else ''}\n")
    for src, dest, key in items:
        size = dir_size(src)
        total += size
        print(f"  [{key:12}] {src}")
        print(f"  {'':14}-> {dest}  ({human(size)})")
    print(f"\n合计 {human(total)}")

    if args.dry_run:
        planned = build_manifest(manifest, source, target, items, args.copy_python)
        print("\n将写入的清单：")
        print(json.dumps(planned, ensure_ascii=False, indent=2))
        return

    target.mkdir(parents=True, exist_ok=True)

    if args.copy:
        for src, dest, key in items:
            dst = target / dest
            if key == "worker":
                dst.parent.mkdir(parents=True, exist_ok=True)
                shutil.copy2(src, dst)
                print(f"copied {src.name}")
                continue
            if dst.exists():
                print(f"skip (exists) {dst}")
                continue
            dst.parent.mkdir(parents=True, exist_ok=True)
            if src.is_dir():
                print(f"copying tree {src} -> {dst} ...")
                shutil.copytree(src, dst, dirs_exist_ok=True)
            else:
                shutil.copy2(src, dst)
            print(f"copied {key}")

    out = build_manifest(manifest, source, target, items, args.copy_python)
    (target / MANIFEST).write_text(
        json.dumps(out, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"\nwrote {target / MANIFEST}")
    print("注意：清单里的 python 若仍指向包外路径，把那台机器的 Python 也准备好，"
          "或改用 python-build-standalone 放进包里。")


if __name__ == "__main__":
    main()
