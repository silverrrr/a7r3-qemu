# 项目状态（公开版）

最后更新：2026-11-14。技术细节见 [A7R3_NOTES.md](A7R3_NOTES.md)，
阶段总结见 [SUMMARY.md](SUMMARY.md)。

## 1. 状态

| 线 | 状态 |
| --- | --- |
| QEMU 跑原厂固件 | ✅ 可显示原生 UI、可注入按键、可开菜单 |
| 新菜单叠层 | ✅ 全中文、真字体、真实设置值 |
| 不被原生重绘打断 | ✅ 20 ms present 金丝雀 + 立即重画 |
| 真正修改设置 | ✅ `setItemSelectedBySRNum`，10 s 后仍保持 |
| 触摸 | ✅ 点选 / 双击应用 / 切分类（轮询坐标） |
| 真机 | ⏳ 只做了只读探测；写测试与触摸尺度待验证 |

## 2. 关键路径（相对于本仓库）

| 用途 | 位置 |
| --- | --- |
| 机器模型 | `qemu/bionz.c` |
| shim（注入 guest） | `payloads/gpu_shim_qemu.c` |
| 新菜单叠层 | `payloads/menu_overlay.c`、`payloads/menu_shell.c` |
| 设置树探针 | `payloads/probe_menu.c` |
| 构建/安装 | `payloads/scripts/` |
| 离线分析 | `tools/` |
| 真机只读脚本 | `tools/real-device/` |

## 3. 运行前提

- 你自己的 A7R3 固件（内核/initrd/eMMC 镜像），本仓库不附带；
- ARM 交叉 clang/lld、Python 3 + Pillow；
- 字体/图标素材需按 `SUMMARY.md` 第 7 节从你的固件生成。

## 4. 下一步

1. 真机写测试：确认触摸坐标尺度与 60 fps present 下的闪烁窗口；
2. 把 456 个设置项按原生页面分组，接完整分页；
3. 触摸独占：关闭原生触控区域，避免触控 AF 抢事件；
4. 真机 present 钩子：在 blit 提交前写源面，彻底消除闪烁。
