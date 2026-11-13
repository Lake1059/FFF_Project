# libass / LGPL 组件源码获取与重新链接说明

本文件适用于 3FP 发布包中的 libass 字幕栈。它不是许可文本本身；各组件许可见 `licenses/` 下的对应文件与 `THIRD-PARTY-NOTICES.md`。

## 1. 分发形态

这些组件以**独立 DLL** 形式随包分发，未静态链接进 `FFF.Player.exe` 或 `FFF.Native.dll`：

- `ass-9.dll`（libass，ISC）
- `bluray-3.dll`（libbluray，LGPL-2.1-or-later）
- `udfread-3.dll`（libudfread，LGPL-2.1-or-later）
- `iconv-2.dll`、`charset-1.dll`（libiconv，LGPL）
- `fribidi-0.dll`（fribidi，LGPL-2.1-or-later）
- `freetype.dll`（freetype，FTL OR GPL-2.0-or-later）
- `harfbuzz.dll`（MIT）
- `brotlicommon.dll`、`brotlidec.dll`（MIT）
- `bz2.dll`（bzip2-1.0.6）
- `libpng16.dll`（libpng-2.0）
- `z.dll`（Zlib）
- `libxml2.dll`（MIT）

`libass` 本身是 ISC；LGPL 义务主要来自 libbluray、libudfread、libiconv/charset、fribidi。动态链接的 DLL 可由用户用兼容构建替换，不需要对 3FP 目标文件做重新链接。

## 2. 对应源码获取

3FP 使用 vcpkg 准备这些组件；构建入口为 `tools/准备Libass.ps1`，依赖清单在 `third_party/vcpkg.json`（或对应 vcpkg 清单）中。上游源码位置：

| 组件 | 上游源码/版本入口 |
|---|---|
| libass | <https://github.com/libass/libass>（vcpkg 当前 0.17.4） |
| libbluray | <https://code.videolan.org/videolan/libbluray>（vcpkg 当前 1.4.1#1） |
| libudfread | <https://code.videolan.org/videolan/libudfread>（vcpkg 当前 1.2.0） |
| libiconv | <https://www.gnu.org/software/libiconv/>（vcpkg 当前 1.18） |
| fribidi | <https://github.com/fribidi/fribidi>（vcpkg 当前 1.0.16） |
| freetype | <https://gitlab.freedesktop.org/freetype/freetype>（vcpkg 当前 2.14.3） |
| harfbuzz | <https://github.com/harfbuzz/harfbuzz>（vcpkg 当前 14.2.0） |
| brotli | <https://github.com/google/brotli>（vcpkg 当前 1.2.0） |
| bzip2 | <https://sourceware.org/bzip2/>（vcpkg 当前 1.0.8） |
| libpng | <https://github.com/pnggroup/libpng>（vcpkg 当前 1.6.58） |
| zlib | <https://github.com/madler/zlib>（vcpkg 当前 1.3.2） |
| libxml2 | <https://gitlab.gnome.org/GNOME/libxml2>（vcpkg 当前 2.15.3） |

具体补丁与编译参数以 `third_party/vcpkg_installed` 的 vcpkg 构建记录和 `tools/准备Libass.ps1` 为准。

## 3. 书面提供承诺

对于随包分发的 LGPL 组件，1059 Studio 承诺：自该发布包发布之日起至少三年内，任何收到该包的第三方均可通过项目仓库 Issue 或项目公开联系方式，索取对应组件的完整源码、修改文件（如有）和构建脚本的机器可读副本；也可直接使用上表上游地址获取同一版本源码。

如 3FP 对 LGPL 组件做过修改，修改部分同样按 LGPL-2.1-or-later 提供；当前发布包使用 vcpkg 的上游构建，未对上述 LGPL 库做 3FP 私有源码修改。vcpkg 端口自带的构建选项/补丁（例如 `tools/vcpkg-ports/libbluray` 和 vcpkg 的 `libiconv` 端口补丁）属于构建脚本的一部分，随源码提供时一并提供；若未来修改，必须在本文件追加“修改文件清单”并随包提供。

## 4. 许可文本

- `licenses/LGPL-2.1-only.txt`：LGPL-2.1 全文。
- `licenses/LGPL-CREDITS.md`：libbluray、libudfread、libiconv、fribidi 等组件的版权归属与许可表达。
- `licenses/vcpkg-libbluray.txt`、`licenses/vcpkg-libudfread.txt`、`licenses/vcpkg-libiconv.txt`、`licenses/vcpkg-libass.txt`：对应上游/回退许可文本。
- `licenses/vcpkg-*.txt`：发布脚本收集的 vcpkg 版权文件（若有）。

本文件是合规操作说明，不构成法律意见；如与上游许可原文冲突，以许可原文为准。
