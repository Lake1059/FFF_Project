#!/usr/bin/env python3
"""把一份"绝对路径"的 AI 字幕扩展包变成可搬走的目录包（清单 v3）。

一个扩展包 = 一个目录 + ai-subtitles.json。清单里的路径可以是绝对路径（指向
机器上已有的 python env / 模型 / 运行时），也可以是相对包根的路径（真正便携）。

  1. --emit  ：只重写清单，把绝对路径换成相对路径（前提是你已经把文件拷进包了）
  2. --copy  ：先把清单里指的 python 环境、模型目录、运行时目录拷进包，再重写清单

清单 v3 有五类实体要搬：

    worker   单个 .py 文件                       -> 包根
    python   一个 python 环境（venv / 独立版）    -> runtime/python/
    model    一个模型目录                        -> model/
    models   {档名: 目录 | {dir: 目录}}          -> models/<档名>/
    runtime  一个原生运行时目录（llama 引擎）     -> runtime/<目录名>/

**两条引擎，取决于模型目录里放的是什么容器**（清单的 engine 字段只是备注，
worker 与打包脚本都按文件判，写错了不会把包判死）：

    onnx   conv_frontend / encoder / decoder 三个 .onnx + tokenizer/
           （DynamicQuantizeLinear -> MatMulInteger，W8A8；sherpa-onnx 进程内跑）
    llama  一份主权重 *.gguf + 一份 mmproj*.gguf
           （block-wise weight-only int8；llama-server 子进程跑，需要 runtime/）

llama 的运行时目录里必须有 llama-server.exe 和 ggml-*.dll。Vulkan 那份只有
87 MB 且只依赖系统的 vulkan-1.dll，所以默认只带它；CUDA 那份 1.1 GB（其中
cublasLt 452 MB 是 NVIDIA 闭源的，裁不掉），实测同卡只快 4.6%、冷启慢 24 倍，
不进基础包 —— 要出也是单独一份 runtime/cuda。

关于 python 的边界（必须知道）：
    普通的 `python -m venv` **不能真正跨机器搬**：Scripts/*.exe 是转发器，
    pyvenv.cfg 指向创建它的 base python。--copy-python 把 venv 拷过来之后，
    目标机仍需同一大版本（3.1x）的 Python，否则起不来。要彻底免依赖，请用
    python-build-standalone / embeddable 发行版当"包内 python"，清单的 python
    指向它即可——本脚本不替你下这个发行版，但会把它的路径按普通目录处理。

关于 onnx 引擎的 sherpa-onnx（同样必须知道）：
    它是 pip 包，原生库与 ONNX Runtime 都随那个包装在 python 环境里，**不进清单**。
    llama 引擎不需要 sherpa，worker 在那边只用 numpy —— 所以 GGUF 包的 python
    环境可以不带 sherpa-onnx 与 onnxruntime（省 44 MB）。

用法：
    # 1) 看看它会拷什么、生成什么清单（不动磁盘）
    python make_portable_pack.py --source E:/.../AI-Subtitles-Pack --target E:/.../pack-portable --copy --dry-run

    # 2) 真拷（python 环境 + 模型 + 运行时，几 GB，先看 --dry-run 的体积）
    python make_portable_pack.py --source ... --target ... --copy

    # 2b) 连 python 环境一起拷
    python make_portable_pack.py --source ... --target ... --copy --copy-python

    # 3) 文件已经拷好了，只要重写清单
    python make_portable_pack.py --source ... --target ... --emit
"""

import argparse
import json
import shutil
import sys
from pathlib import Path

MANIFEST = "ai-subtitles.json"

# 包内布局（相对包根）
WORKER_DIR = Path(".")
MODEL_DIR = Path("model")
MODELS_DIR = Path("models")
PYTHON_DIR = Path("runtime/python")

# 一个合格模型目录必须有的权重主干名（tokenizer 是目录，单独判）。
# 官方包是 encoder.int8.onnx / decoder.int8.onnx（量化标在文件名里），换 int4 又变成
# encoder.int4.onnx。这里只钉主干，和 worker / 宿主的解析口径保持一致。
MODEL_STEMS = ("conv_frontend", "encoder", "decoder")
MODEL_SUBDIRS = ("tokenizer",)

ENGINE_LLAMA = "llama"
ENGINE_ONNX = "onnx"
RUNTIME_DIR = Path("runtime")
LLAMA_EXE = "llama-server.exe"


def detect_engine(path):
    """按模型目录里的容器判引擎：有 *.gguf 就是 llama，否则 onnx。

    不读清单的 engine 字段做主判据 —— 文件是唯一真相。清单写错了只会让
    "备注"和实际不一致，不该让包判不合格。
    """
    if path is not None and path.is_dir() and any(path.glob("*.gguf")):
        return ENGINE_LLAMA
    return ENGINE_ONNX


def llama_weights(path):
    """返回 (主权重, mmproj)，缺任何一个都给 None。"""
    if path is None or not path.is_dir():
        return None, None
    allg = sorted(p for p in path.glob("*.gguf") if p.is_file())
    mm = [p for p in allg if "mmproj" in p.name.lower()]
    main = [p for p in allg if "mmproj" not in p.name.lower()]
    return (main[0] if main else None), (mm[0] if mm else None)


def pick_onnx(path, stem):
    """<stem>.onnx 优先，否则唯一的 <stem>.<后缀>.onnx。没有则 None。"""
    exact = path / f"{stem}.onnx"
    if exact.is_file():
        return exact
    cands = sorted(p for p in path.glob(f"{stem}.*.onnx") if p.is_file())
    return cands[0] if len(cands) == 1 else None


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


def model_missing(path):
    """模型目录不合格时返回原因，合格返回 None。按引擎分别判。"""
    if not path.is_dir():
        return "目录不存在"
    if detect_engine(path) == ENGINE_LLAMA:
        main, mm = llama_weights(path)
        if main is None:
            return "缺主权重 *.gguf（mmproj 不算主权重）"
        if mm is None:
            return "缺 mmproj*.gguf（音频编码器，缺了模型不认音频）"
        return None
    for stem in MODEL_STEMS:
        if pick_onnx(path, stem) is None:
            extra = sorted(p.name for p in path.glob(f"{stem}.*.onnx"))
            if extra:
                return f"{stem} 有多个候选（{'、'.join(extra)}），只能留一份"
            return f"缺 {stem}.onnx（也接受 {stem}.int8.onnx 这类带后缀的）"
    for name in MODEL_SUBDIRS:
        if not (path / name).is_dir():
            return f"缺 {name}/ 子目录"
    return None


def runtime_missing(path):
    """llama 运行时目录不合格时返回原因，合格返回 None。"""
    if path is None:
        return "清单里没有写 runtime 目录（llama 引擎必需）"
    if not path.is_dir():
        return "目录不存在"
    if not (path / LLAMA_EXE).is_file():
        return f"缺 {LLAMA_EXE}"
    if not any(path.glob("ggml-*.dll")):
        return "缺 ggml-*.dll（后端 kernel）"
    return None


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


def tier_dir(entry):
    """models 里的一项：字符串或 {dir: ...}，两种写法都收（与宿主加载器一致）。"""
    if isinstance(entry, str):
        return entry
    if isinstance(entry, dict):
        return entry.get("dir")
    return None


def plan(source, target, manifest, copy_python):
    """返回 [dict(src, dest, keys, copy)]。

    copy=False 表示该源已经在目标包里了（重复跑或者清单本来就写的相对路径），
    只需要写清单不需要再拷一遍。同一个源只搬一次——比如 model 与
    models.int4 指同一个目录时，只搬一份，两个键都登记到同一条上的 keys，
    清单里两处都指向 model/。
    """
    items = []
    by_src = {}

    def add(src, dest, key):
        hit = by_src.get(src)
        if hit is not None:
            # 同源复用：把新键挂到已有那条上，清单改写时才找得到。
            hit["keys"].append(key)
            return
        if target == src or target in src.parents:
            # 已经在包里：dest 按它在包内的真实位置算，不再拷。
            entry = {"src": src, "dest": src.relative_to(target),
                     "keys": [key], "copy": False}
        else:
            entry = {"src": src, "dest": dest, "keys": [key], "copy": True}
        by_src[src] = entry
        items.append(entry)

    worker = resolve(source, manifest.get("worker"))
    if worker is not None and worker.is_file():
        add(worker, WORKER_DIR / worker.name, "worker")
        # 与 worker 同级的两个伴生脚本，少一个包就残：
        #   qwen3_ort.py     —— onnx 引擎的 NPU / Intel GPU 逐段实现。缺了不报错，
        #                       只是 ov-* 档全部被跳过，包退化成老 ONNX 包。
        #   npu_precheck.py  —— NPU 预检（离线编译验证），随包给用户排障。
        for 伴生 in ("qwen3_ort.py", "npu_precheck.py"):
            候选 = worker.parent / 伴生
            if 候选.is_file():
                add(候选, WORKER_DIR / 候选.name, f"side.{伴生}")

    model = resolve(source, manifest.get("model"))
    if model is not None and model.is_dir():
        add(model, MODEL_DIR, "model")

    # 原生运行时（llama 引擎）：整目录搬，落点保留它原来的目录名，
    # 这样 runtime/vulkan 与 runtime/cuda 能并排放着，切换只是改清单一行。
    runtime = resolve(source, manifest.get("runtime"))
    if runtime is not None and runtime.is_dir():
        add(runtime, RUNTIME_DIR / runtime.name, "runtime")

    tiers = manifest.get("models")
    if isinstance(tiers, dict):
        for tier, entry in tiers.items():
            src = resolve(source, tier_dir(entry))
            if src is None or not src.is_dir():
                continue
            add(src, MODELS_DIR / tier, f"models.{tier}")

    if copy_python:
        pysrc = resolve(source, manifest.get("python"))
        if pysrc is not None and pysrc.exists():
            # venv 时 Scripts/python.exe 上一级是包根；整个环境都要带走。
            env_root = (pysrc.parent.parent
                        if pysrc.parent.name.lower() in ("scripts", "bin")
                        else pysrc.parent)
            add(env_root, PYTHON_DIR, "python")
    return items


def rel(path):
    """包内相对路径，正斜杠（清单规定正反斜杠都收，但正斜杠更不容易被转义吃掉）。"""
    return Path(path).as_posix()


def build_manifest(manifest, target, items, engine):
    """按 items 的结果重写路径：包内文件写相对，包外文件原样保留绝对路径。"""
    moved = {k: it["dest"] for it in items for k in it["keys"]}
    out = {k: v for k, v in manifest.items() if not k.startswith("_")}
    out["version"] = 3

    if "worker" in moved:
        out["worker"] = rel(moved["worker"])
    if "model" in moved:
        out["model"] = rel(moved["model"])
    if "runtime" in moved:
        out["runtime"] = rel(moved["runtime"])

    # engine 按源模型目录的真实内容写（不是照抄旧值，也不是看目标目录——
    # dry-run 时目标目录还不存在）。清单只是备注，文件才是判据。
    out["engine"] = engine
    if engine == ENGINE_ONNX:
        out.pop("runtime", None)

    # models 表：搬进包内的写相对路径（直接写成字符串，比 {dir:...} 省字），
    # 包外的原样保留。整档不合格的仍在清单里保留原值——宿主加载器会丢弃它并
    # 在设置页少列一个档，删字段反而让人看不出"这档配过但没成功"。
    tiers = manifest.get("models")
    if isinstance(tiers, dict) and tiers:
        rebuilt = {}
        for tier, entry in tiers.items():
            key = f"models.{tier}"
            if key in moved:
                rebuilt[tier] = rel(moved[key])
            else:
                rebuilt[tier] = entry
        out["models"] = rebuilt

    # python：venv 是 Scripts/python.exe（Windows）或 bin/python（POSIX），
    # 独立发行版是根目录的 python.exe。dry-run 时目标目录还不存在，
    # exists() 全不通过会让预览停在绝对路径，看着像"没打算拷"，所以带兜底。
    if "python" in moved:
        env = moved["python"]
        candidates = ("Scripts/python.exe", "python.exe", "bin/python3", "bin/python")
        chosen = None
        for cand in candidates:
            if (target / env / cand).exists():
                chosen = env / cand
                break
        out["python"] = rel(chosen if chosen is not None else env / candidates[0])

    # 顺带把 v1 遗留字段清掉，免得旧清单被当成"还带二进制"的包。
    for dead in ("llamaServer", "mmproj", "servers", "quant", "llamaArgs",
                 "layers", "device"):
        out.pop(dead, None)
    return out


def main():
    ap = argparse.ArgumentParser(description="生成可搬走的 AI 字幕扩展包（清单 v2）")
    ap.add_argument("--source", required=True, help="现有包目录（含 ai-subtitles.json）")
    ap.add_argument("--target", required=True, help="目标包目录（不存在则创建）")
    mode = ap.add_mutually_exclusive_group(required=True)
    mode.add_argument("--emit", action="store_true", help="只重写清单，不拷文件")
    mode.add_argument("--copy", action="store_true", help="拷 python 环境/模型再重写清单")
    ap.add_argument("--copy-python", action="store_true",
                    help="连 python 环境一起拷（venv 通常不能跨机器，见文件头说明）")
    ap.add_argument("--dry-run", action="store_true", help="只打印计划与体积，不落盘")
    args = ap.parse_args()

    source = Path(args.source).resolve()
    target = Path(args.target).resolve()
    manifest = load(source)
    engine = detect_engine(resolve(source, manifest.get("model")))

    items = plan(source, target, manifest, args.copy_python)
    if not items:
        sys.exit("清单里没有可解析的路径（worker / model / models / python 全是空或不存在的？）")

    total = 0
    print(f"源: {source}")
    print(f"目标: {target}")
    print(f"模式: {'copy' if args.copy else 'emit'}{' (dry-run)' if args.dry_run else ''}\n")
    bad = []
    for it in items:
        size = dir_size(it["src"])
        total += size
        flag = "" if it["copy"] else "  [已在包内]"
        labels = "+".join(it["keys"])
        print(f"  [{labels:14}] {it['src']}")
        print(f"  {'':16}-> {it['dest']}  ({human(size)}){flag}")
        # 模型目录逐个查完整性：缺文件的档在目标机上会整档消失，早说早改。
        if any(k == "model" or k.startswith("models.") for k in it["keys"]):
            why = model_missing(it["src"])
            if why:
                bad.append(f"{labels}: {why}（{it['src']}）")
        if "runtime" in it["keys"]:
            why = runtime_missing(it["src"])
            if why:
                bad.append(f"{labels}: {why}（{it['src']}）")

    # llama 包必须有 runtime：没有它模型文件再多也起不来。
    engine = detect_engine(resolve(source, manifest.get("model")))
    if engine == ENGINE_LLAMA and resolve(source, manifest.get("runtime")) is None:
        bad.append("这是 GGUF 模型，但清单里没有 runtime 目录"
                   "（应指向含 llama-server.exe 的目录，如 runtime/vulkan）")
    print(f"\n合计 {human(total)}")
    if bad:
        print("\n包内容不合格：")
        for line in bad:
            print(f"  - {line}")
        sys.exit("请先把上面这几项补齐再打包。")

    if args.dry_run:
        print("\n将写入的清单：")
        print(json.dumps(build_manifest(manifest, target, items, engine),
                         ensure_ascii=False, indent=2))
        return

    target.mkdir(parents=True, exist_ok=True)

    if args.copy:
        for it in items:
            if not it["copy"]:
                continue
            src, dst = it["src"], target / it["dest"]
            if "worker" in it["keys"]:
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
            print(f"copied {'+'.join(it['keys'])}")

    out = build_manifest(manifest, target, items, engine)
    (target / MANIFEST).write_text(
        json.dumps(out, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"\nwrote {target / MANIFEST}")
    if detect_engine(resolve(target, out.get("model"))) == ENGINE_LLAMA:
        print("注意：这是 GGUF 包，目标机不需要装 sherpa-onnx / onnxruntime —— "
              "推理在包内 runtime/<后端>/llama-server.exe 里跑，python 侧只要 numpy。")
    else:
        print("注意：目标机的 python 必须已 pip install sherpa-onnx（原生库 + ONNX Runtime "
              "都随那个包走）；没拷 venv 时尤其要记得装。")


if __name__ == "__main__":
    main()
