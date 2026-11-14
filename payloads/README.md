# payloads —— 注入到相机进程里的代码

| 文件 | 作用 |
| --- | --- |
| `gpu_shim_qemu.c` | `LD_PRELOAD` shim：观测 GPU ioctl、写面板、注入按键、读设置树 |
| `menu_overlay.c` | 新菜单叠层：渲染、字符串、设值、触摸、自测、present 守卫 |
| `menu_shell.c` / `.h` | A7C2 风格界面绘制；**派生自 [cokeenet/zve10-qemu](https://github.com/cokeenet/zve10-qemu)（PolyForm Noncommercial 1.0.0）**，见 [CREDITS.md](../CREDITS.md) |
| `probe_menu.c` | M0 探针：遍历 456 个设置 root，dump 节点/属性 |
| `shutter_hook*.c`、`normal_logdump.c` | 早期的观测实验，保留作参考 |

## 不随仓库提供的文件

下面这些是**从固件或宿主字体生成的素材**，本仓库不包含（避免分发专有资产）：

| 文件 | 来源 | 生成方式 |
| --- | --- | --- |
| `font_atlas.h` | 固件 `.ltt` 里的 TrueType | `tools/ltt-extract.py` → `tools/bake-font-atlas.py` |
| `menu_labels.h`、`menu_digits.h` | 宿主 CJK 字体 | `tools/bake-labels.py` |
| `original_assets.h` | A7C2 界面素材 | 自行准备（或改 `menu_shell.c` 用纯色占位） |

生成后放进本目录，再执行 `scripts/build-newmenu.sh`。

## 构建

```bash
bash scripts/build-shim-gotkey.sh     # 生成 shim（ARM 软浮点、freestanding）
bash scripts/build-newmenu.sh         # 生成 newmenu.so（需要上面的 .h）
```

## 环境相关的路径

`scripts/*.sh` 里的固件/镜像路径是原作者环境的绝对路径（如
`/root/firmware/a7r3/...`），使用前请改成你自己的路径。
安装脚本会挂载 eMMC 镜像的 `/usr`、`/setting` 分区，建议先备份镜像。

## 注入方式

- `LD_PRELOAD` 由镜像里 `/bin/iperf` 头 512 字节的规格串决定
  （`/usr/tool/newmenu.so:/usr/tool/libefence.so`），由 `install-newmenu.sh` 改写；
- 开关是 `/setting/` 下的空文件（`newmenu`、`nm_flip`、`nm_selftest` 等）。
