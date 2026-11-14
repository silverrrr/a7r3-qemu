# 真机只读探针

这些脚本在相机的 **service mode** 里通过 Sony-PMCA-RE 的 shell 通道执行，
全程**只读**（只从 `/dev/mem` 读、只把结果写到插入的 SD 卡），不写相机存储。

| 脚本 | 作用 |
| --- | --- |
| `dump_display_probe.py` | 一次会话：收集 `/proc` 文本 + 读三块候选显示缓冲 |
| `dump_mem_range.py` / `dump_mem_ranges.py` | 读任意物理区间到卡（分块 + 校验） |
| `dump_regs_probe.py` | 读 MMIO 窗口（注意：错误的基址会读回 0 字节） |
| `dump_text_probe2.py` | 把 vmallocinfo/kallsyms 等文本写卡 |
| `show_proc_text.py` | 直接从 shell 读回小的 `/proc` 文件 |
| `usb_enum_camera.py` | 枚举相机 USB 描述符（判断当前 USB 模式） |

## 依赖

脚本 `from dump_partitions import ShellSession` 等来自
[Sony-PMCA-RE](https://github.com/ma1co/Sony-PMCA-RE)（外部依赖，不在本仓库）。
把它的 `pmca` 包与 `probe_jiritsu.py` 等辅助模块放到可导入路径即可。

## 注意

- service 会话很脆：一次只做一件事，做完直接断电，不要调用 `auth.stop()`；
- 正常固件模式通常只暴露 MTP/WPD，**没有 shell**；
- 读 MMIO 前先确认基址（错误的基址会 `dd` 回 0 字节而不是报错）。
