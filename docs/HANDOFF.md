# 项目状态（公开版）

最后更新：2026-11-15。技术细节见 [A7R3_NOTES.md](A7R3_NOTES.md)，
阶段总结见 [SUMMARY.md](SUMMARY.md)。

## 1. 状态

| 线 | 状态 |
| --- | --- |
| QEMU 跑原厂固件 | ✅ 可显示原生 UI、可注入按键、可开菜单 |
| 新菜单叠层 | ✅ 全中文、真字体、真实设置值 |
| 不被原生重绘打断 | ✅ 20 ms present 金丝雀 + 立即重画 |
| 真正修改设置 | ✅ `setItemSelectedBySRNum`，10 s 后仍保持 |
| 触摸 | ✅ 点选 / 切分类 / 开二级页（轮询坐标） |
| 二级页面 | ✅ 点已选中的行 → 选项弹窗（固件 SR 表 + 单选点 + 分页） |
| 新旧菜单切换 | ✅ 右上角 44×44 双箭头按钮，两种状态同一位置 |
| 关闭时交还原生画面 | ✅ 打开前抓帧、关闭时回写（原生 App 不会自己重绘） |
| 菜单路径不用 dlopen/dlsym | ✅ 模块基址（`/proc/self/maps`）+ 自解析 ELF 符号表 → `base + st_value`；456 项枚举实测零回退（§66.13/§66.14） |
| SD 卡两段式加载 | ⏳ 载荷已能全部走反射，stub 里 dlopen 完整载荷这一步还没做（§66.9） |
| 物理 MENU = 返回 | ⏳ 用"原生 present"间接判定，机理清楚但只在真机可验 |
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

## 4. 交互与避坑（2026-11-15）

- **进二级页**：行是"选中 / 打开"二段式 —— 第一次点选该行，再点一次打开它的选项
  弹窗；弹窗里点一项就 `setItemSelectedBySRNum(SR)` 并回到列表。
- **off-by-one**：`children[]` 是 0 基、`selected` 是 SR（1 基），直接
  `children[selected]` 会显示下一档的值。统一走 `getSRNumByIndex` 反查下标。
- **交还屏幕**：原生 App 只在自己有新内容时 present，所以"停止绘制"不会让原生
  画面回来。打开前 `panel_grab()` 存 640×480，关闭时 `panel_restore()` 写回。
- **别去 patch 输入函数**：`snd_InfraInputEvent_to_InputServant` 在热路径上，
  8 字节补丁会被正在执行的线程踩到，guest 直接掉进 `Exception>` 调试 shell。
  本项目改为"叠层开着 + App 又 present + 距上次触摸 >1.5 s ⇒ 当作返回"。
- **旧菜单的点击**：原生菜单是按键式的（触摸只做对焦），网页点击要翻成
  移动选择 + ENTER；`/nmclick` 在叠层关闭时就是这么做的。
- **警告框"变黑"**：其实是查看器只 dump 了一个固定 surface，而对话框画在另一块
  缓冲上。模型现在每 tick dump 三块，网页取最新的非全黑帧，对话框显示正常；
  角落那个 44×44 的小按钮实测无害，保持默认开启。
- QEMU 里一帧要写 3×614400 像素（全软件模拟），点一下到画面更新 1–3 s；
  截图前多等几秒，或用 `payloads/scripts/nm-drive.py`。
- 反复强杀 QEMU 会把 guest `/tmp` 的 FAT 写脏 → 只读挂载 → 按键文件通道失效
  （点了没反应，但画面正常）：`payloads/scripts/fsck-guest-tmp.sh fix`。

## 5. 下一步

1. 真机写测试：确认触摸坐标尺度与 60 fps present 下的闪烁窗口；
2. 把 456 个设置项按原生页面分组，接完整分页；
3. 触摸独占：关闭原生触控区域，避免触控 AF 抢事件；
4. 真机 present 钩子：在 blit 提交前写源面，彻底消除闪烁。
