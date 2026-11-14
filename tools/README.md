# tools —— 离线分析 / 解码

所有工具都只读**本地文件**（需要你自己从固件里取出的资源或 dump），
不联网、不内置任何固件数据。

## 字体与字符串

| 工具 | 作用 |
| --- | --- |
| `ltt-probe.py` / `ltt-extract.py` | 解析 Sony `.ltt` 字体容器，抽出内嵌 TrueType |
| `bake-font-atlas.py` | 把 TrueType 烘成叠层用的 4-bit 字形图集（`font_atlas.h`） |
| `bake-labels.py` | 把固定文案烘成 alpha 掩码（旧方案，保留） |
| `render-pua.py` | 预览私有区（图标）码位在字体里的样子 |
| `measure-native-text.py` | 从原生帧测字高/行距 |
| `uxc-strtable.py` | 从 `.uxc` 字符串容器导出 `id → UTF-8` 表 |
| `uxc-around.py` / `uxc-offset-test.py` / `probe-uxc-strings.py` | 容器格式探测 |

## 命令流 / GPU

| 工具 | 作用 |
| --- | --- |
| `decode-cmdbuf.py` | 解 Sugilite 2D 命令表（`(op<<16)|0xf000`） |
| `decode-sugi-runs.py` / `find-sugi-lists.py` | 在 RAM dump 里找命令表并解出目标面/纹理 |
| `decode-gpu-region.py` / `diff-gpu-regions.py` | 解 GPU 共享区里的 surface 描述符 |
| `scan-regs-for-pa.py` | 在寄存器 dump 里找 surface 地址 |
| `movwt-consts.py` | 重建 ARM `movw/movt` 拼出的 32 位常量（找 MMIO 基址） |
| `vtable-slot.py` / `vtable-slot2.py` | 通过重定位读 vtable 槽位 |
| `touch-struct-slot.py` | 算 libObj 里“最后触摸点”结构体的地址 |

## 帧与图像

| 工具 | 作用 |
| --- | --- |
| `raw2png.py` | 640×480 RGBA4444 → PNG |
| `raw-device2png.py` | 真机 16bpp dump → 4 种候选解码（线性/Morton × 4444/565） |
| `decode-at.py` | 从 RAM dump 的任意物理地址解一帧 |
| `find-ui-surface.py` / `diag-ram-dump.py` | 在整段 RAM 里找“像画面”的窗口 |
| `frame-changes.py` / `find-overlay-frames.py` / `scan-bars.py` | 帧稳定性与叠层命中统计 |

## 符号 / 固件

| 工具 | 作用 |
| --- | --- |
| `probe-symbols.py` | 直接从 ELF 的 `.dynsym` 查符号与地址 |
| `hexdump-sym.py` | 反查某个符号的机器码 |
| `gen-root-names.py` / `dump-roots.py` | 生成/解析设置 root 列表 |

## 真机只读

`real-device/` 下的脚本需要 Sony-PMCA-RE（外部依赖）与 service mode，
只读、把结果写到 SD 卡；详见 `real-device/README.md`。
