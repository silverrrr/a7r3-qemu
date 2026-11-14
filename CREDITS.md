# 引用与致谢 / CREDITS

本项目站在不少公开研究之上。下面区分**代码来源**（有许可证义务）和**思路参考**。

## 1. 代码来源（必须遵守其许可证）

| 仓库 | 作者 | 许可证 | 用到什么 |
| --- | --- | --- | --- |
| [cokeenet/zve10-qemu](https://github.com/cokeenet/zve10-qemu) | cokeenet | **PolyForm Noncommercial 1.0.0** | `payloads/menu_shell.c`、`payloads/menu_shell.h` 是它的派生作品；菜单的 A7C2 风格布局与绘制流程来自其 `tools/zve10/menu/` |
| [Derek2011cc/A7M3-Firmware-QEMU-Boot-Experiment](https://github.com/Derek2011cc/A7M3-Firmware-QEMU-Boot-Experiment) | Derek2011cc | MIT | `qemu/bionz.c` 里的 Sugilite 2D 软件光栅化（`soft_draw` / `soft_exec` / opcode 解码）由它移植而来 |
| [QEMU](https://www.qemu.org/) | QEMU 项目 | GPL-2.0 | `qemu/bionz.c` 作为 QEMU 机器模型运行，整体按 GPL-2.0-or-later 分发 |
| [ma1co/Sony-PMCA-RE](https://github.com/ma1co/Sony-PMCA-RE) | ma1co | MIT | `tools/real-device/` 依赖它的 `pmca` 包与 service-mode shell 通道（**未随仓库分发**，需自行安装） |

> ⚠️ **非商业限制**：由于 `payloads/menu_shell.c` / `.h` 派生自 PolyForm Noncommercial
> 许可的代码，**任何包含这两个文件的构建/分发仅限非商业用途**。
> 仓库其余部分（本项目的原创代码）按 MIT 发布。

## 2. 思路与资料参考（未直接复用代码）

| 仓库 | 作者 | 用途 |
| --- | --- | --- |
| [chocotan/a7c2-qemu](https://github.com/chocotan/a7c2-qemu) | chocotan | A7C2 原生 QEMU 实验，菜单/渲染思路参考 |
| [chocotan/a7m3-qemu](https://github.com/chocotan/a7m3-qemu) | chocotan | A7M3 QEMU 移植与调试经验 |
| [chocotan/zve10-qemu](https://github.com/chocotan/zve10-qemu) | chocotan | ZV-E10 菜单注入与素材流程参考 |
| [Derek2011cc/a7m3-QEMU-shell](https://github.com/Derek2011cc/a7m3-QEMU-shell) | Derek2011cc | QEMU 内 guest shell / 串口交互方式 |
| [Derek2011cc/a7m3-connect-shell](https://github.com/Derek2011cc/a7m3-connect-shell) | Derek2011cc | 真机连接 shell 的思路 |
| [robinshie/Sony_A7M3](https://github.com/robinshie/Sony_A7M3) | robin code | A7M3 逆向资料（文件/分区/符号） |
| [Bigrabbit057/sony-a7r3-creative-studio](https://github.com/Bigrabbit057/sony-a7r3-creative-studio) | Bigrabbit057 | A7R3 相关资料（GPLv3 项目，本项目**未复用其代码**） |
| [dino2048/ZV-E10-5GHz-WiFi-Unlock](https://github.com/dino2048/ZV-E10-5GHz-WiFi-Unlock) | dino2048 | 5GHz Wi-Fi 可行性对照 |
| [QAQLeonard/a7c2-fl23-research](https://github.com/QAQLeonard/a7c2-fl23-research) | QAQLeonard | FL23 平台资料参考（PolyForm NC，本项目**未复用其代码**） |

另外，以下公开研究对我们的方法论影响很大（未直接使用其代码）：

- **Lost-week-1025 / CXD90045-FPSU** —— CXD90045 平台资料
- **DavidBuchanan314** —— *ILCE-7M4-RE*、*Sony-ILCE-7M4-Linux*
- **ma1co** —— *Sony-PMCA-RE*、PMCA-QEMU、`fwtool.py`
- **YuuLuo/a7c2-service-preload** —— service mode / `ld.so.preload` 先行工作

## 3. 关于 Sony 资产

本仓库**不包含**任何 Sony 固件、镜像、字体、图标或字符串资源。
字体/图标/文案的生成脚本在 `tools/`，需要你用**自己合法获得**的固件来运行。
商标归 Sony Group Corporation 所有，本项目与其无关联。
