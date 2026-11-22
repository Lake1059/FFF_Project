# AI 字幕扩展包示例

`ai-subtitles.json` 是 ONNX 扩展包示例。将它和 [asr_worker.py](../Worker/asr_worker.py) 放在包根，将模型放进 `model/`。模型目录需包含 `conv_frontend`、`encoder`、`decoder` 三个 ONNX 文件以及 `tokenizer/`。可附 `selftest.wav` 和 `selftest.txt` 做模型自检。

设置 →「AI 字幕」中选择包根目录。当前播放器接受版本 2 和 3 的清单。`worker`、`model` 和 `python` 的相对路径从包根解析。`python` 留空时从 PATH 查找。选用 ONNX 的 sherpa 后端时，该 Python 环境需安装 `sherpa-onnx`。

GGUF 扩展包使用版本 3。`model` 指向含主权重和 `mmproj*.gguf` 的目录，`runtime` 指向含 `llama-server.exe` 及其依赖的目录。`window` 和 `step` 以秒为单位，默认均为 5 秒。
