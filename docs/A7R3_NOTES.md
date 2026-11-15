> 本文件是公开版：删除了引导链/恢复通道章节，并涂掉了所有解锁常量。
> 技术内容（QEMU 模型、显示通路、菜单叠层、字体/字符串解码）完整保留。

# A7R3 (ILCE-7RM3) 固件与可行操作评估

> **交接速查**：先看 `analysis/HANDOFF.md`（当前状态、关键路径、避坑清单、
> 常用命令）。本文是全部细节与实验记录，按 `§编号` 引用。

日期：2026-10-25
对象：ILCE-7RM3，固件 3.10
参考：`Update_ILCE7RM3V310.exe`、fwtool.py、Sony-PMCA-RE、a7c2/a7cr FL23 研究

## 已确认的机身信息

通过 PMCA 原生 MSC 通道只读查询（`pmca-console.py info -d native`）：

```
Model:            ILCE-7RM3
Product code:     0032829325
Serial number:    05152461
Firmware version: 3.10
```

USB 普通模式当前 PID `054C:0BFF`，由 Windows `usbstor` 驱动占用。

## 固件包结构

`Update_ILCE7RM3V310.exe` 内嵌一个完整 DAT（偏移 `0xAE9D4`），
crypter `CXD90045`，model `0x71030013`，region 0，version 3.10。
解包后得到 18 个编号区段，其中：

```
0110_backup/SYSASTRA-DSLR/CQ/CX62000_*.bin   区域/语言备份镜像
0640_darwin/darwin_core.bin                  图像处理（Darwin）
0650_prfile/PMBP                             引导/平台文件
0700_part_image/dev/nflasha3                  /system (vfat)
0700_part_image/dev/nflasha7                  /rootfs (ext2)
0700_part_image/dev/nflasha15                 /usr (ext2)
```

### 分区表（来自 0300_partconf/partinf.conf + mount.conf）

| 设备 | 大小 | 文件系统 / 挂载点 | 内容 |
| --- | --- | --- | --- |
| nflasha1 | 8 MB | vfat（Updater） | 更新器 |
| nflasha2 | 20 MB | vfat → `/setting` | 设置、updater/dat4 |
| nflasha3 | 48 MB | vfat → `/system` | kernel、initrd、av-cam.bin、dfe_app.bin 等 |
| nflasha4 | 12 MB | raw | MPR Flash Backup |
| nflasha5 | 60 MB | WBI → `/wbi` | 热启动镜像 |
| nflasha6 | 16 MB | vfat | Jiritsu 区 |
| nflasha7 | 8 MB | ext2 → `/rootfs` | 根文件系统 |
| nflasha10 | 12 MB | vfat → `/tmp` | 临时 |
| nflasha11 | 500 MB | vfat → `/log` | 日志（dump 临时区） |
| nflasha12 | 40 MB | vfat → `/cert` | 证书 |
| nflasha13 | 4 MB | vfat | UpdaterWork |
| nflasha15 | 300 MB | ext2 → `/usr` | 主应用、UI、音频资源、libJiritsu |
| nflasha16 | 140 MB | raw → `/android/system` | Android 系统 |
| nflasha17 | 100 MB | vfat → `/android/data` | Android 数据 |
| nflasha18 | 20 MB | vfat → `/lens` | 镜头数据 |
| nflasha23 | 2 MB | prfile → `/PMBP` | PMBP |

`nflasha7` 和 `nflasha15` 的 ext2 超级块为 `53 EF`，可以 `remount,rw` 后按文件修改。

## 访问路径判断

1. **不能重打包固件走官方升级。** PMCA 文档明确：CXD90045 / CXD90057 的固件更新包
   是加密签名的。A7R3 属于 CXD90045。
2. **可以走 service mode。** 普通 MSC 身份认证后会重新枚举为 Senser（`02A9`/`0336`），
   PMCA 的 `serviceshell` 提供终端；本地 `dump_nflasha6.py` 已按
   `SenserPlatformBackend.interactiveShell` 编写。
3. **Jiritsu 命令表与 A7C2 相同，但 `cmd_debug` 是空实现。**
   `JIRITSU_SET_FILE_PATH/SET_FILE2/GET_FILE2/DELETE2 = 15/16/17/18` 存在，
   但 `cmd_debug @0x425C` 直接 `mov r0,#-1; pop`。
   因此 A7C2 的 “DEBUG → root 子进程 → ld.so.preload” 执行链不能照搬；
   A7R3 应使用 service terminal，必要时再评估 `JIRITSU_START`（要求 `/tmp/jmode = Manual`）。
4. **驱动要求。** service 切换和 Senser 通信需要 libusb-win32/WinUSB；
   普通 MSC 只读查询可用 Windows 原生驱动（已验证）。

## 功能可行性

| 目标 | 结论 | 落点 |
| --- | --- | --- |
| 全语言解锁 | 可做，低风险 | Backup `0x010d008f` 起 35 字节 |
| 解除 29:59 录像限制 | 可做，低风险 | Backup `0x003c0373` 起 3 字节 |
| 解除 4K 5 分钟限制 | 可做，低风险 | Backup `0x003c04b6` 2 字节 |
| PAL/NTSC 选择器 | 可做，低风险 | Backup `0x01070148` 1 字节 |
| 替换机械快门音效 | 可做，需测试 | `/usr/share/avsystemsound/Dslr_Emnt_High_Shutter_*.bin` |
| 电子快门发声 | 需二次开发 | 现有只有 `SilentShutter` 与 `AudioSignalsShutter`，没有 ES 发声分支 |
| 竖屏信息显示 | 难度高 | 旧 MPR/Obj + `.uxc` 框架，无现成开关 |
| FL2/FL3 | 不可照搬 | 无 Creative Look 架构，只有 CreativeStyle/PictureEffect/PictureProfile |
| 自定义 Creative Look | 不可照搬 | 无 look 槽位/参数表；只能改 av-cam.bin 的色彩处理 |
| Star eater / 色彩调整 | 可研究，风险高 | `nflasha3` 的 `av-cam.bin`、`0640_darwin/darwin_core.bin` |
| 恢复原厂 | 有条件 | 官方 3.10 更新包可重刷 /usr、/system；前提是仍能进 updater mode |

## 操作顺序（只读优先）

1. 用 `analysis/dump_partitions.py --probe` 进入 service shell，确认：
   `/proc/mounts`、`uname -a`、`/dev/nflasha*`、`/setting/updater/dat4`。
2. `analysis/dump_partitions.py` 默认备份 nflasha1/2/3/4/5/6/7/12/13/15/18/23，
   每块 8 MB 经 `/log` 中转，校验大小并记录 SHA256。
3. 先用 PMCA 的 Backup 只读接口读一份完整 backup 存档，再考虑改语言/录像限制。
4. 资源级修改（音效、UI）只改 `/usr` 的 ext2 文件，保留完整镜像以便回滚。
5. 最后才评估 `/system` 的 `av-cam.bin`；写坏可能导致相机不启动。

## 验证过的 dump 流程（2026-10-25）

A7R3 service 内核：Sony CE Linux 3.0.27，root shell `/usr/share/app #`。
`/usr`、`/rootfs` 只读 ext2；`/system`、`/setting`、`/log`、`/tmp` 可写 vfat。

不能用的路径：

- `FileControl`（PMCA `readFile`/`pull`）在 A7R3 上只返回响应头，不返回数据；
  命令号/状态机与 PMCA 的假设不一致，强行尝试会让 service 进程挂住。
- Jiritsu `GET_FILE2` 返回 `0x10000004`，同样不可用。
- service 会话仍活着时二次 `authenticate()` 会被拒绝（pipe error）；
  已处于 0336 时应直接 reattach，不要重新认证。
- `auth.stop()` 在部分会话后会让 service 进程挂住；dump 完成后直接关机。

可用路径（已在真机验证）：

1. service shell 里 `mkdir -p /log/dev; mount -t tmpfs tmpfs /log/dev`
2. `mknod /log/dev/mmca10p1 b 250 11`（Slot 1；Slot 2 为 `mmcb30p1` = 250,31）
3. 卡是 exFAT 时，service 内核无 exFAT 驱动；用相机自带工具格成 FAT32：
   `/sbin/mkdosfs -F 32 -s 64 -n A7R3 /log/dev/mmca10p1`
4. `mkdir -p /log/sd1; mount -t vfat /log/dev/mmca10p1 /log/sd1`
5. `dd if=/dev/nflashaN of=/log/sd1/nflashaN.bin bs=65536 count=<size/65536>`
6. `sync`、`umount`，然后**关机取卡**（不要调用 `auth.stop()`）。

本次已导出并逐分区校验大小：

| 分区 | 字节 |
| --- | --- |
| nflasha1 | 8,323,072 |
| nflasha2 | 20,971,520 |
| nflasha3 | 50,331,648 |
| nflasha4 | 12,582,912 |
| nflasha5 | 62,914,560 |
| nflasha6 | 16,777,216 |
| nflasha7 | 8,388,608 |
| nflasha12 | 41,943,040 |
| nflasha13 | 4,194,304 |
| nflasha15 | 314,572,800 |
| nflasha18 | 20,971,520 |
| nflasha23 | 2,097,152 |

工具：`analysis/dump_to_sdcard.py --format-card`。

## 风险

- service mode 中断可能让相机停在服务模式；取电池冷却后可恢复。
- `/usr` 或 `/system` 写坏会导致主系统不启动，官方 updater 也可能无法进入。
- 所有写操作前必须有完整分区镜像和 SHA256，且电池 > 50%。
- 不要动 nflasha11（日志）和 WBI，除非明确知道后果。

## 各分区设置/配置清点（2026-10-25 离线复查）

工具：`inventory_partitions.py`（目录树）、`extract_configs.py`（导出到 `analysis/extracted/`）、
`fat_list.py`（自制 FAT12/16/32 读取器）、`recover_jiritsu.py`、`inspect_extra.py`、`inspect_more.py`。
**注意：fwtool 的 FAT12 分支有缺陷，会把 nflasha6/nflasha12/nflasha23 误判为空，必须用 `fat_list.py`。**

| 分区 | 内容与配置 |
| --- | --- |
| nflasha1 | `/boot/kemco.txt`（内核 cmdline）、`loadaddr.txt`、`p1chksum`、`dat2=13 00 03 71`、`dat3` |
| nflasha2 | `/kemco.txt`（另一份 cmdline）、`Backup.bin/.bak`、`DmmConfig.bin`、`psd.bin`、`ulogio.bin`、`.hasp`、`.etools`、`updater/dat4=0x0310`、`facerec0..7.bin`、`facedmy.bin`；`/gps`、`/mode`、`/appfw_infra` 为空 |
| nflasha3 | 无文本配置：`av-cam.bin`、`bonobo.bin`、`dfe_*`、`initrd.img`、`vmlinux.bin`、`lensbin_lensproperty/LensAberrationData.bin`、`sabin/*` |
| nflasha4 | 184 个 1 KB 的 `CMMe` 页（MPR 寄存器备份），每页头 `CMMe 58 00 58 00` + 8 字节寄存器/值对 |
| nflasha5 | `WBI1` 热启动镜像：643 个 LZ77 段，31,876,608 → 145,084,416 字节，虚拟地址 0x60xxxxxx |
| nflasha6 | **并非空分区**（见下） |
| nflasha7 | `/version.txt=37113_08.2019110603`、`/root/chassis=CXD90045`、`/root/common.src`（updater 变量：MODE_USER=0/PROD=1/SERVICE=2/MINOR=3/VERSKIP=6，错误码 41/54/72/73/87/45/47/48）；`/etc` 为空 |
| nflasha12 | `/cert` 只有空目录 `/crl`，整分区仅 252 个非零字节 |
| nflasha13 | `/history/update.log`：一次成功升级，Base/Body `100.101.005a`，含 EDISX / Darwin / PFORMAT 步骤；`/hash` 为空 |
| nflasha15 | `/share/pmbp/DeviceInfo.xml`、`/lib/netconf/*.conf`、`/share/network/nvram/{high,low}/bcm4339_*.txt`（WiFi 校准）、`/share/app/{area_check_data.dat,fontlist.dat,global.xdb}`、`/share/avsystemsound/beeplist.txt`、`/lib/githash`、`/bin/*.sh` |
| nflasha18 | 57 个 `VX*nn_lensfile.bin`（每支镜头 `LF` 头的校正表） |
| nflasha23 | FAT12 引导扇区在 `0x200`：`/INFO` 空，`/LICENSE` 38 个开源许可/EULA 文件（EULA.PDF、GPL_LGPL、OPENSSL、GLIBC 等） |

### 关键修正 1：nflasha6（Jiritsu）其实是“已格式化但文件被删除”

FAT12 卷，根目录 8 个条目全部为删除态（首字节 `0xE5`），但数据仍留在未分配簇中，已完整恢复：

- `libJit.so` 274,666 B（ARM ELF，`7f 45 4c 46`）
- `DIAC.csv` 76,810 B —— **1,197 条 Jiritsu 命令表**
- `Tmmc.jit` 571,411 B —— Jiritsu 测试脚本（17,747 行）
- `Ver.txt`：`jiritsu_20200210_01_CQ_SDPW.tar.dat / tmmc.jit / libJit.so 20170403 / TOOLBAR_VERSION 16.04.10`
- `history.txt`：工厂调整记录（ST1..ST6、`93-03_SAVE`、时间戳）
- `jit.log` 1,710,013 B、`jcount=18`、`Cam_AdjustGuide_Files/`（xlsx 指南）

### 关键修正 2：Jiritsu 的命令码不是 15/16/17/18

`DIAC.csv` 显示 category `#1001` 的真实命令码为 `0x4001..0x4012`：

```
0x4001 MODE        0x4002 MOUNT       0x4003 FORMAT      0x4004 START
0x4005 DEBUG       0x4006 FIRMUP      0x4007 SET_FILE    0x4008 GET_FILE
0x4009 FILE_DELETE 0x400A GET_FILELIST 0x400B GET_FILE_HASH
0x400C COMPARE_MEDIA 0x400D GET_VERSION 0x400E FACTORY
0x400F SET_FILE_PATH 0x4010 SET_FILE2  0x4011 GET_FILE2  0x4012 FILE_DELETE2
```

参数签名：`GET_FILE2 = P1×3 → P4V×2`，`SET_FILE_PATH = P1V×2`，`SET_FILE2 = P1V×4`，
`GET_FILELIST = P1×1 → P4V×2`，`GET_VERSION = 无参数 → P4V×2`。
之前用 15/16/17 探测返回 `0x10000004`，很可能就是把内部枚举序号当成了线上命令码。
重新探测应先用最安全的 `0x400D GET_VERSION`，再 `0x400A GET_FILELIST`，最后 `0x4011 GET_FILE2`。

### 其他二进制配置说明

- `DmmConfig.bin`：`0x50` 起 3,798 条 20 字节记录（dmm.ko 内存映射表），尾部 `for ilc8g42m-astra 2019/01/15`。
- `psd.bin`：`PF` 容器，头长 `0x150`，自述大小 `0x287000`；数据高熵（加密/压缩），无可见配置项。
- `.hasp`：88 B，6 组工厂 HASP/加密狗功能记录；`.etools`：4 KB 不透明工厂工具数据。
- `ulogio.bin`：日志路由配置（`LGNM171H_Dslr_Astra-Diadem`），通道 LOGB/PRTB/CHAN/ISYS/DBGL/DBCL/FLTA/TERM/TELN/FBOX/SFLT/CFLT/FLTF/RCVF/ALGP。
- `facerec0..7.bin`：已登记人脸数据（个人数据，勿外传）。

## 4K60 可行性：av-cam.bin 初步分析（2026-10-25）

目标：判断 A7R3 是否存在“半分辨率 + 60fps”的传感器读出模式，以及管线是否具备 A7C2 那种重接条件。

### 已确认

- `av-cam.bin` 是**扁平 ARM 镜像**（不是 ELF），加载基址 **0x635C6000**（头部 “ORIL” v4 + 9 个入口指针，指针值 = 基址 + 0x44/0x58/0xb4…，与函数序言吻合）。
- 传感器为 **IMX251**：RTTI 名称 `Camera::Imager::IMX25119C_ImagerCtrl_IMX251E`，全画幅 7952×5304。
- 管线里具备与 A7C2 同类的几何/地址设施：
  - `canvas_width/height`、`imager_pixel_width`、`validWidth/validHeight`、`canvas_xy`、`rcin_xy`、`aspect_rcout`；
  - `Astra addr conv (phy->VA)` 与 `Rosetta` 地址转换；
  - `[EXL] ImagerDriveMode must be 1H1H or 1H2H`；
  - `ARC hvadd_addmode`（水平/垂直相加）存在，但部分值报 “not available”。
- 几何尺寸：全幅 7952×5304；16:9 为 7952×4472；**半分辨率 16:9 = 3976×2236**。表里存在 `3976×2236 @30` 的记录（对应 4K30 读出/缩放）。
- 编码器侧确实编入了 `sfmc_enc_timing_*_60p50p.cpp` / `*_120p100p.cpp`，ADF 也有 `LimitTransSize60Hz` / `Set60HzExecTime`。
- 内存：总 1GB，`memrsv=632M@0x18800000:uc` 等；3976×2236×2B ≈ 17.8MB/帧，632MB 约可放 35 帧（60fps 约 0.6 秒缓冲），带宽不是明显瓶颈。

### 目前没有找到的

- **没有找到 3976×2236 @60（或其它半分辨率 @60）的传感器模式记录**。已解码的 76 字节记录表（0x84c2bc 起）包含 26/30/44/50 等值，60 只出现在 0x84f92c 的帧率/PLL 列表和 0x83a118 的寄存器值中，尚不能证明是 60fps 模式。
- `ImagerDriveMode` 只有 **1H1H / 1H2H**，没有 2H2H。即 A7R3 的半宽模式很可能是**水平跳采 + 垂直 2 行相加**，而不是 A7C2 那种完整 2×2 电荷合并；画质会差一档。
- 尚未定位 `Camera::Imager::C_Imager::GetParam/GetInitParam` 的模式表；字符串区没有绝对指针，可能通过相对偏移表引用。

### 结论（阶段性）

A7R3 的管线“接口”是齐的，但**当前固件没有定义半分辨率 60fps 的传感器模式**。要做出 4K60，必须先证明 IMX251 在 3976×2236（或 1H2H + H-skip）下能跑 60fps，并手工补出对应的 PLL/寄存器序列；这比 A7C2 的“重接已有模式”更靠底层，且 A7R3 没有运行时注入、迭代要刷 nflasha3/nflasha15。

工具：`avcam_base.py`、`avcam_xrefs.py`、`avcam_thumb_xrefs.py`、`avcam_vtable.py`、`avcam_overrides.py`、`avcam_mode_scan.py`、`avcam_decode_modes.py`、`avcam_83a_view.py`、`avcam_readout_strings.py`、`avcam_exl_strings.py`。

### 追加：PLL/寄存器块与引用方式（同日）

- 0x83A110 起有一组小值块（0x37/0x3E/**0x3C**/0x3C/0x36/0x37/0x38…），
  0x83A5E0 起则是几何块（**30**、4000、2279、2282、2244、2236、3976、3984）。
  0x3C=60 确实出现在寄存器/时序块里，但**无法仅凭数值判断它是帧率、PLL 倍频还是别的寄存器值**。
- 交叉引用全部失败：模式表/字符串既没有绝对指针，也没有相对偏移、MOVW/MOVT 立即数引用。
  说明代码可能通过运行时基址/重定位表访问数据，或者这些表是共用库里的未启用分支。
- 0x1167D80 起是一大张 0x63xxxxxx 指针表（含 0x635C6000 影像基址），可能是异常/重定位/函数表；
  这是下一步解析“数据表如何被引用”的入口。
- 完整性检查：`nflasha1`（updater）有 RSA/CHKSUM，属于**升级包校验**；
  rootfs 和 `/usr/bin/*.sh` 里没有看到启动时对 `/usr` 做哈希校验的明显逻辑。
  `/usr` 里的 sha256/RSA 字符串大多来自 libcrypto/openssl。结论：**“改 /usr 后能正常启动”是可能但未验证**，
  需要一次无害文件改动 + 冷启动来确认。

### 追加结论

离线证据**不足以证明 IMX251 存在 3976×2236@60 模式**，也不足以否定它。
可确认的是：4K30 的半分辨率 16:9 几何为 3976×2236；驱动模式只支持 1H1H/1H2H；
60p 的编码器/ADF 代码存在。要一锤定音，只能查 IMX251 数据手册或上机读传感器模式寄存器。

### 上机只读探测（2026-10-25）

相机 `054C:0BFF` → service `0336`，未做任何写操作，未调用 `auth.stop()`。

- `/bin` 是 rootfs；应用工具在 **`/usr/bin`**，不是 `/usr/share/app`。
- `adjstctl.elf` v1.7（`--ver`）。`--abid` 输出完整命令表，关键块：
  - `0x0404` imager 调整块（OSAL `0x000301d7`）：`C_IMAGER_TEMP_CHK=0x00c8`、
    `S_IMAGER_POWER_CTRL=0x00c9`、`S_IMAGER_CURTAINMODE=0x00ce`、`RAM_READ=0x00fe`、`RAM_WRITE=0x00ff`。
  - `0x0305`/`0x0503`：`GET_DATA=0x0022`、`GET_DIAG_DATA=0x0024`。
  - `0x0601`：`SEN_MODE=0x0011`、`DBG_MODE=0x4013`、`RAM_READ=0x00fe`。
- 只读命令返回请求头：`mode/res/step/addr/datasz`。测试了 `0x0404/0x00c8`、
  `0x0305/0x0022`、`0x0503/0x0022`、`0x0404/0x00fe`（`<size>:<data>` 语法），
  均未返回数据（`datasz=0`）。`size:data` 解析由 `libtestcmd.so` 的 `cmdline_read_data_from_args`
  → `0x16d8`（检查 `:` 后解析）实现。
- `mpr_viewLog.elf` 抓到的启动 MPR 日志（10,630 条）：`CAT=0x51` 是 imager 控制通道，
  只有：
  - `51 0002 2005 1f10 14b8` → 7952×5304
  - `51 0002 2005 00a0 0078` → 160×120
  没有 3976×2236/2652 或 60fps 记录。注意这是 **service mode 启动日志**，主程序的视频模式未运行，
  所以“没有”不等于“不存在”。

结论：imager 块可达，但还没找到模式查询的只读入口；`CAT=0x51` 的读子命令和
`0x0404/0x00fe` 的 handler 需要继续逆向。下一步优先做离线 handler 分析，不再盲试。
脚本：`probe_sensor_mode.py`（只读探测）、`parse_mpr_log.py`、`dis_libtestcmd*.py`。

### adjust 命令表与 handler 追踪（2026-10-25）

- `adjstctl --abid` 的 1,490 条已解析到 **`analysis/inventory/abid.json`**。
- `libsencore.so` 文件偏移 **0x2375C**（副本 0x3DEC0）起是 adjust 表，
  条目 **72 字节**：`{u32 osal; u16 code; u16 block; char name[64]}`。
  例：`0x2375C = {osal 0x000301d7, code 0x00fe, block 0x0404, "RAM_READ"}`；
  下一条是 `RAM_WRITE (0x00ff)`；`0x0405` 的 RAM_READ/WRITE 也在同一表。
- `av-cam.bin` 里 `0x000301d7` 出现在文件偏移 **0x356EF0 / 0x357368** 的字面量池，
  对应的是**发送方**（`movs r0,#0x52; bl …` 向该 OSAL 队列发消息），不是 handler。
- **关键修正**：`av-cam.bin` 的代码使用 **base 相对偏移**（字面量是 `0x00xxxxxx`，
  如 `0x00a1b130`），不是绝对地址；运行时基址仍是 `0x635C6000`。
  这就是之前绝对地址/MOVW/MOVT xref 全部失败的原因。
- 下一步：找 imager 模块的 **OSAL queue 注册回调**（真正的 handler），
  或反汇编 `0x356E9C` 附近的发送函数与 `0x1E5D8/0x1E664` 的 OSAL API，
  确认 `RAM_READ` 的 payload 与地址空间。

### OSAL API thunk（同日追加）

- `0x1E5D8` / `0x1E664` 等是 **OSAL API thunk**：从全局 API 表取函数指针
  （`+0x1E8`、`+0x200` …），不是具体实现。
- `0x356E9C` 是发送函数：`r0 = 0x000301D7`（imager 队列），命令字 `0x52`，
  调用 `0x1E664` 发送；`0x35732E` 是另一处发送。
- handler 不在 `av-cam.bin` 的字面量池里；队列 owner 可能是内核模块
  （`/usr/kmod/upm.ko`、`dmm.ko` 等）或另一个库。
- 下一步：解析 OSAL API 表 / queue owner，或分析 `/usr/kmod/*.ko` 的 imager 相关模块。

### 模块搜索与 imager 参数名（同日追加）

- 搜索了 `/kmod/*.ko`、`libmpr.so`、`libObj.so`、`libSysDef.so`、`libsencore.so`：
  - `libsencore.so` 与 `libObj.so` 里都有同一份 adjust 表资源（`RAM_READ`、`C_IMAGER_TEMP_CHK` 等）。
  - `libObj.so` 额外包含：
    `PARAMID_INTERNAL_IMAGER_FRAME_SIZE_INFO`、`PARAMID_EXC_IMAGER_FRAME_RATE`、
    `VALUEID_INTERNAL_IMAGER_FRAME_SIZE_FULL/APSC`、
    `VALUEID_EXC_IMAGER_FRAME_RATE_HIGH(_MOVIE_EE_*)`、
    `MEMORY_SIZE_FOR_DSLR_IMAGER_42M`。
  - `upm.ko` 只有 `PSAVE_ID_IMAGER` / `PSAVE_IMAGER_SLEEP/WAKE`，是电源管理，不是 adjust handler。
- `libObj.so` 里的 OSAL `0x000301d7` 落在 `compass_res` / `udate_res` 资源段，
  不是代码；所以它也只是资源表，不是 handler。
- **OSAL ID 可能是运行时动态分配的**（`osal_alloc_msgq_id`），
  静态常量搜索找不到 owner 是合理的。
- 相机在探测后已断电，USB 上已无 `054C` 设备。

### 当前卡点

`PARAMID_INTERNAL_IMAGER_FRAME_SIZE_INFO` / `PARAMID_EXC_IMAGER_FRAME_RATE`
是下一步最值得抓的两个参数，但要先拿到它们的**数值 ID** 和读取通道：
- 候选 1：解析 `libObj.so` 的 `udate_res` 资源表（PARAMID 名称 ↔ ID）。
- 候选 2：在正常模式下用 MPR/Obj 的消息/属性接口读取（service mode 主程序未运行）。
- 候选 3：运行时跟踪 OSAL 消息（需要动态调试，超出当前只读范围）。

### PARAMID / VALUEID 数值映射（libObj.so，已完成）

- `.data.rel.ro` 里有两张**字符串指针数组**（按枚举顺序排列，索引即候选数值 ID）：
  - **PARAMID**：`0x11B002C..0x11B2FEC`，共 **3,057** 条，首项 `PARAMID_ZOOM_SPD`。
  - **VALUEID**：`0x11BF3A4..0x11C2ABC`，共 **3,527** 条。
- 关键候选（索引 = 候选 ID）：

| PARAMID | ID |
| --- | --- |
| `PARAMID_MOVIE_SIZE` | **7** |
| `PARAMID_FRAME_RATE` | **8** |
| `PARAMID_MOVIE_ASPECT_RATIO` | **9** |
| `PARAMID_MOVIE_SIZE_SET/ACT` | 10 / 13 |
| `PARAMID_FRAME_RATE_SET/ACT` | 11 / 14 |
| `PARAMID_APSC_MODE` | 1070 |
| `PARAMID_IMAGER_FRAME_SIZE_INFO` | **1073** |
| `PARAMID_STILL_EE_FRAME_RATE_MODE` | 1152 |
| `PARAMID_CCM_IMAGE_FRAME_RATE` | 938 |

| VALUEID | ID |
| --- | --- |
| `VALUEID_CCM_IMAGE_FRAME_RATE_30_25` | 945 |
| `VALUEID_CCM_IMAGE_FRAME_RATE_60_50` | **946** |
| `VALUEID_CCM_IMAGE_FRAME_RATE_120_100` | 947 |
| `VALUEID_CCM_IMAGE_FRAME_RATE_240_200` | 948 |
| `VALUEID_IMAGER_FRAME_FULL` | 1009 |
| `VALUEID_IMAGER_FRAME_APSC` | 1010 |
| `VALUEID_4K_2160` | 1227 |
| `VALUEID_MOVIE_HSIZE_3840` | 1232 |

- 注意：`PARAMID_EXC_IMAGER_FRAME_RATE` / `VALUEID_INTERNAL_IMAGER_FRAME_SIZE_*` 只是
  **debug 字符串**（例如 `"Invalid ObjMsg (PARAMID_IMAGER_FRAME_SIZE_INFO)"`），
  **不在**数组里；真正可读的是 `PARAMID_FRAME_RATE` / `PARAMID_MOVIE_SIZE`。
- “索引 = ID” 是按数组枚举顺序得出的假设，需要用 on-device 读取或代码索引验证。
- 完整映射：`analysis/inventory/paramid_map.json`（脚本 `list_paramid_ids.py`）。

### 电影帧率 UI 表与组合结论（同日追加）

- `viewMovieExpMode.uxc` 偏移 **0x4A0** 有一张 u32 表：
  `0x38, 0x3B1, 0x3B2, 0x3B2`
  - `0x3B1 = 945 = VALUEID_CCM_IMAGE_FRAME_RATE_30_25`
  - `0x3B2 = 946 = VALUEID_CCM_IMAGE_FRAME_RATE_60_50`
  说明 UI 的帧率菜单**确实提供 60p/50p**。
- `libObj.so` 里**没有** `(movie_size, frame_rate)` 成对组合表；
  4K/60p 的组合约束在 UI model/代码里，不是简单的数据数组。
- `scan_all_uxc_movie.py` 在大型 `image_*`/`string_*` 资源里找到的 4K/60p 数值
  大多是 16 位噪声，不能作为组合证据。
- 结合 `av-cam.bin` 的 4K 模式表只有 24p/30p，以及官方规格 4K30 上限，
  **离线证据倾向结论：60p 是 HD 档位的选项，A7R3 没有 4K60 组合**。
  唯一剩下的不确定性是 UI 在选中 4K 时是否隐藏 60p——这属于 UI model 逻辑，
  需要进一步反汇编 `.uxc`/菜单树才能完全确认。

### IMX251 传感器级可行性评估（同日追加）

目标：不看官方菜单，只看 **IMX251 能不能跑 3976×2236@60** 这种读出规格。

- 4K30 的几何：`3976×2236`（表 0x83A5E0 附近），输出侧还有 `4000×2244/2279`。
- 像素率换算：
  - `3976×2236×60 = 533.5 Mpix/s`（目标）
  - `7952×4472×30 = 1.07 Gpix/s`（若 4K30 是全宽读出 + 下采样）
  - `1920×1080×120 = 249 Mpix/s`（1080p120 的小窗口读出）
  - `42MP×15fps ≈ 640 Mpix/s`（按 rolling shutter ~1/15s 估算的全幅读出）
- **带宽判断**：
  - 如果 4K30 是全宽读出（Sony 对 A7R II/III 宣称 full-pixel readout），
    533 Mpix/s 在传感器能力内 → 4K60 **在带宽上可行**。
  - 如果 4K30 实际是半分辨率 30fps 读出（~270 Mpix/s），
    4K60 需要约 2× 的视频像素率 → **可能超出**当前视频读出能力。
- **采样方式**：
  - 驱动只允许 `ImagerDriveMode = 1H1H / 1H2H`，没有 2H2H；
  - `hvadd_addmode` 存在但部分值 “not available”；
  - 因此半宽模式大概率是 **水平跳采 + 垂直 2 行相加**，不是 A7C2 那种完整
    2×2 电荷合并。画质会差一档。
- **模式/寄存器**：
  - 驱动的几何模式表只有 30fps；
  - 0x83A110 的 PLL/时序块里有 `0x3C=60` 值，可能是 60fps PLL，
    但还没有映射到具体寄存器/帧率。
- **结论**：传感器**带宽层面“有可能”**（取决于 4K30 是否全宽读出），
  但**模式/寄存器层面未证明**；A7R3 不是单纯重接管线，必须补出 IMX251 的
  60fps 模式/PLL，并且横向采样很可能是 skip 而不是 2×2 合并。

### 怎么才能一锤定音

1. 拿到 IMX251 数据手册/寄存器表（Sony 开发者门户或 NDA 资料），确认
   2×2 / 1H2H 模式在 60fps 下的最大像素率。
2. 或反汇编 `0x83A110` 附近的 PLL 配置和 `ImagerDriveMode`/`hvadd_addmode`
   的调用点，确认是否存在 60fps 的 PLL 组合。
3. 或上机用 `RAM_READ`（0x0404/0x00fe）读 IMX251 的 PLL/mode 寄存器，
   先解决地址空间/payload 格式。

### 关键修正：av-cam.bin 的“基址相对偏移”结论是错的（2026-10-25 追加）

- 之前把 `0x00a1b130` 这类字面量当成“基址相对地址”，这是错误的，也是此前
  绝对地址 / MOVW / MOVT xref 全部失败的真正原因。
- 实际是标准 Thumb PIC：`ldr rX,[pc,#imm]` 取到**位移** L，`add rX,pc` 计算
  `target = (指令虚拟地址 + 4) + L`。注意用的是虚拟地址，不是文件偏移。
  例：`0x356EA0 add r1,pc`，L=`0x00A1B130`，PC=`0x6391CEA4`
  → `0x64337FD4`（BSS 中 OSAL API 全局指针），**不是**文件 `0xA1B130`。
- 新脚本 `avcam_pic_xrefs.py` 建立正确的 PIC 引用表：133,211 条 `add pc`、
  217,222 条 literal load、731 组 MOVW/MOVT、3,796 个落在镜像内的绝对字面量；
  结果缓存到 scratchpad，支持 `--region/--query/--ranges`。
- 结论：此前“没有绝对/相对指针、表不可达”的判断作废；数据和字符串都能正常 xref。

### 0x83A0BC 的 168 项表（原“PLL/寄存器块”）

- `0x1D83E0` 是一个叶子 getter：
  `cmp r0,#0xA7; bhi ret0; ldr r3,[pc]; add r3,pc; ldr.w r0,[r3,r0,lsl#2]; bx lr`
  → 表基址 `0x63E000BC`（文件 `0x83A0BC`），共 **168 项 u32**，正好结束于
  `0x83A35C`（也是下一个被引用目标）。
- 表值只有 0、34、54..63；`0x83A110` 的 55/62/60/60 就是该表 index 21..24。
  即 **60 只是表值，不能据此认定是帧率或 PLL 倍频**。
- 该 getter 没有直接 BL 调用者，也没有绝对/文件偏移形式的函数指针引用；
  index 的语义仍未确定（可能是寄存器值、驱动档位或其它枚举）。

### 0x83A0B0..0x83A674 是参数/默认值记录区

- `0x1D83F8` 的初始化函数用 `0x1D81CC`（20 字节对象构造器）在 BSS
  `0x641FDF40+` 建立约 20 个描述符：
  `{类型描述指针(0x839F28), 0, 0, 描述/名称指针(0x839xxx), 0, 值指针(0x83A0B0+), 尺寸标志}`
- 因此 `0x83A0B0 / 0x83A4DC / 0x83A5A8 ...` 是被绑定的**参数字段**，
  不能直接当作 IMX251 寄存器地址。

### 76 字节模式/时序表（0x84C2BC）

- 记录长度 **0x4C**，字段（u32）：
  `+00 mode, +04 fmt, +08 W, +0C fmt, +10 offset, +14=6, +18 ?, +1C fps,
   +20 ?, +24 fmt, +28 H, +2C fmt, +30..+48 其它`
- 4K30：`0x84C730`（mode 0，`3976×2236@30`）；`0x84C77C` 同尺寸同帧率
  （另一输出变体，仅 +30..+3C 不同）。同表还有 `2648×1488@30`、
  `1720×968@30`、`1280×852@50`。
- 严格与宽松两种扫描都表明：该表 fps 只出现 4/26/30/44/50，
  **没有任何 (H≈2236/2652/4472, fps=60) 的 76 字节记录**。
- `0x84F92C` 的 60 属于一张“步进/速率”标量表（`(2, 0x3c)`），不是模式记录的 fps；
  全文件“fps=60 且宽高为常见传感器/视频尺寸”的宽松扫描结果同样为 **0 条**。

### 参数 ID 验证（仍未有定论）

- `libObj.so` 的 `.data.rel.ro` 在文件 `0x11A5440..0x1212238`；PARAMID 指针数组
  `0x11B002C`（3057 项，`0x11B2FF0` 结束）、VALUEID `0x11BF3A4`（3527 项）都是
  **稠密指针数组**（无空项，全部指向 `.rodata`），符合“按枚举顺序生成的查表”。
- 但数组旁边**没有**数值 ID 字段；`parse_libobj_idpairs.py` 只找到 32 条真正的
  `(name,id)` 记录，且都不涉及 MOVIE_SIZE/FRAME_RATE。
- 因此 “index = ID” 仍需独立验证。支持性证据：`viewMovieExpMode.uxc` 的
  `0x3B1/0x3B2` 与候选 945/946 一致。**仍属假设，不能当结论用。**

### 本轮结论

- 固件侧可以更有把握地说：**A7R3 的 av-cam 模式表没有 4K60（3976×2236@60）
  记录，也没有任何全幅/半幅 60fps 模式记录**；60 只出现在编码器时序和标量表。
- 像素率参考：4K30 半幅读出 `3976×2236×30 ≈ 266.7 Mpix/s`；
  4K60 目标 `≈ 533.5 Mpix/s`。带宽是否够仍取决于 4K30 实际是全宽还是半幅读出。
- 传感器侧仍未证明/否证：0x83A0BC 表已定位且可读，但语义未知；一锤定音仍需
  IMX251 数据手册或上机 `RAM_READ` 读 PLL/mode 寄存器。
- 下一步优先级：
  1. 用 `avcam_pic_xrefs.py --query` 追 0x83A0BC getter 的间接调用者
     （vtable/函数指针），或反查 `0x839F28` 类型描述器，确定 168 项表的语义。
  2. 解析 BSS `0x641FDF40` 那 20 个描述符的实际使用点，给
     `0x83A0B0..0x83A674` 的字段命名。
  3. 用 IMX251 手册确认 1H2H / H-skip 在 60fps 下的最大像素率。
  4. 上机后按 `0x0404/0x00fe` 读 PLL/mode 寄存器（只读）。
- 新增/更新脚本：`avcam_pic_xrefs.py`、`scan_mode_records.py`、
  `parse_libobj_idpairs.py`、`resolve_libobj_ids.py`。

## 动态注入能力：确认存在（2026-10-25 追加，静态分析）

结论：**A7R3 自带一条无需刷写签名固件的 LD_PRELOAD 注入链**，注入目标就是加载
`av-cam.bin` / `libObj.so` 的主进程 `im.elf`。这比之前判断的“没有运行时注入”
要乐观得多。

### 启动链

```
/sbin/init (rootfs)
  -> /usr/bin/bootin.elf        (appFw 启动器)
       读取 /setting/mode/dmode（默认 mode_file）
       dmode 含 'p' -> 读 /setting/mode/preload
       -> system("LD_PRELOAD=\"<preload 内容>\" im.elf ...")
  -> /bin/im.elf                (主应用)
       挂载 /setting /system /usr /tmp ...
       启动 sen.elf、qsi.elf ...
       加载 /system/av-cam.bin 和 /usr/lib/libObj.so
```

### 关键证据

- `bootin.elf` 的用法字符串（文件 0x1d40 起）：
  `bootin.elf { file=<mode_file> } { dmode=<debug_mode> } { amode=<app_argv> }`
  `{ target=<im_target> } { sim } { lt | nolt } { wfree | WFREE } { killall }`
  `dmode: 0 kernel / 1 Global Infra / 2 LIRO / 3 APP(default)`
  `gdb : attatch gdbserver`，`lt : Leaktracer mode`，`wfree : SSLIBC double free`
- `bootin.elf` 0x9018 的函数：`open("/setting/mode/preload", O_RDONLY)` →
  `read(..., 0x200)` → `snprintf(buf, "LD_PRELOAD=\"%s\"", 内容)`。
- `bootin.elf` 0x9270：dmode 字符 `'p'/'P'` → `orr flags, #0x40`（preload 标志）；
  `'l'/'L'` → `#0x42`（LeakTracer，同样走 LD_PRELOAD + ulogio）；
  `'w'/'W'` → `#0x8`（wfree/MALLOC_OPTIONS）；`'g'/'G'` → `#0x10`（gdb --args）；
  `'s'/'S'` → `#0x20`（gdbserver）。
- `bootin.elf` 0x9740：`tst flags,#0x40` → 调用上面的 preload 拼装函数；
  随后拼 `im.elf ...`，0x982e 用 `system()` 执行。**没有签名/白名单/路径校验。**
- `bootin.elf` 0x8f90：`system("gdbserver 0.0.0.0:<port>")`，端口默认字符串
  `10000`，可由 `Backup_read(0x940002, ...)` 覆盖。
- `change_mode.sh`（/usr/bin，官方脚本）：
  - `p <lib>`：写 `/setting/mode/dmode=3p` + `/setting/mode/preload=<lib>`
  - `7/8/9/a/b/d/e/f/10`：LeakTracer / wfree / KMC / target gdb / gdbserver /
    DUMA / eFence / jemalloc / mcheckb
  - `3`：恢复默认并清空 preload（**回滚入口**）
- `/bin/im.elf`：ET_EXEC、EM_ARM、EABI5、`INTERP=//lib/ld-linux.so.3`（soft-float），
  NEEDED 含 `libObj.so`、`libosal_uipc.so`、`libosal_utm.so` 等；
  字符串含 `/usr/lib/libObj.so`、`fn=/system/av-cam.bin load=1 debug=1`、
  `sen.elf &`、`/setting`、`/system`、`/usr`。
  即：**LD_PRELOAD 会在 im.elf 启动早期生效，随后它才加载 av-cam/libObj、
  启动 sen.elf**。
- `/setting` 由 init 以 vfat `rw` 挂载（选项串 `batch_sync,posix_attr`），
  未看到 `noexec`；但 vfat 的 exec 行为需上机用 `mount` 和测试 .so 确认。

### 触发方式（首次需要一次重启）

1. 进 service mode shell（root）；
2. `printf '3p' > /setting/mode/dmode`
   `printf '/setting/mode/hook.so' > /setting/mode/preload`
   （或直接 `/usr/bin/change_mode.sh p /setting/mode/hook.so`）；
3. 把 ARM soft-float 的 `hook.so` 放进 `/setting/mode/`；
4. 断电重启；bootin 会用 `LD_PRELOAD="/setting/mode/hook.so" im.elf ...` 启动主应用。
5. 回滚：`change_mode.sh 3`（或删除 `/setting/mode/preload`、把 dmode 改回 `3`）。

### 限制 / 风险

- 不是 ptrace 式的“活体注入”，首次必须重启（或从 root shell 手动跑
  `bootin.elf file=... dmode=3p`；service mode 下能否直接拉起完整 app 栈未验证）。
- `LD_PRELOAD` 会沿环境继承给 im.elf 的 shell/子进程（sen.elf、qsi.elf 等）；
  hook 里应按 `/proc/self/exe` 限定作用对象，避免污染。
- .so 必须匹配 soft-float ARM EABI；preload 路径写在双引号里，不能含 `"`/`$`/
  反引号；文件内容不要带换行（`printf`/`echo -n`）。
- 坏 .so 可能导致正常模式起不来；service mode shell 仍可改 `/setting/mode/*`，
  回滚成本低。首次上机前应确认 service mode 不依赖 bootin/im.elf。
- `gdbserver` 模式虽然存在，但**分区里没有 `gdbserver`/`gdb` 二进制**，
  该模式开箱不可用；需要自带 ARM gdbserver，或在 hook.so 里实现调试通道。
- 一旦首次 hook 成功，可让 .so 常驻（WiFi/USB 通道或轮询 `/setting` 里的新
  payload 并 `dlopen`），之后迭代就不必再刷 nflasha3/nflasha15，甚至不必重启。

### 对 4K60 工作的意义

- 之前认为“A7R3 没有运行时注入、每次迭代要刷分区”，这个判断需要修正：
  **有官方调试余留的 LD_PRELOAD 入口，且目标进程正是加载 av-cam/libObj 的 im.elf。**
- 仍需确认注入层能否覆盖到 IMX251 的传感器模式寄存器：如果 60fps 模式需要
  改 `av-cam.bin` 内部表/独立核，LD_PRELOAD 只能拦 im.elf 的加载与 OSAL 消息，
  可能还要配合对 av-cam.bin 的持久修改；但至少参数/描述符/消息层可以先动态验证。
- 下一步（上机）：先用一个“只写日志、不改变行为”的 hook.so 验证注入链和
  `/setting` 可执行性，再逐步挂 `open/mmap/osal_*` 观察 av-cam 加载与模式下发。

### 真机验证：LD_PRELOAD 注入链已跑通（2026-10-25，service mode）

相机固件 3.10，USB 0BFF → 0336 service mode。**全程未写 vfat/flash，未重启，
未调用 auth.stop()。**

方法（`analysis/verify_injection_chain.py --execute`）：
1. `mount -t tmpfs tmpfs /setting/mode`，用 tmpfs **覆盖**空目录，避免写 nflasha2；
2. 写入 `dmode=3p`、`preload=/log/dev/a7r3_probe/does-not-exist.so`；
3. 在 `/log/dev`（tmpfs）放一个假 `im.elf`（shell 脚本），并把该目录放到 PATH 最前；
4. 杀掉当前 `bootin.elf`（只需杀 bootin；`im.elf` 可以继续运行）；
5. `bootin.elf dmode=3p`，假 `im.elf` 记录环境。

实测结果（`analysis/injection_test.log`）：

```
ARGV0=/log/dev/a7r3_probe/im.elf
ARGS=debug s g r u l i a lazy-global
LD_PRELOAD=/log/dev/a7r3_probe/does-not-exist.so
MALLOC_OPTIONS=gfffffffff
PWD=/usr/bin

ERROR: ld.so: object '/log/dev/a7r3_probe/does-not-exist.so'
       from LD_PRELOAD cannot be preloaded: ignored.
```

结论：
- bootin 确实读取了 dmode 的 `p` 标志和 `/setting/mode/preload`；
- 它把 preload 内容原样拼进 `LD_PRELOAD=`，传给 `im.elf`；
- ld.so 真的执行了 preload（因为路径不存在而被忽略）；
- 参数 `debug s g r u l i a lazy-global` 与 bootin 的命令构造一致；
- 假 `im.elf` 以 root 运行（`Uid: 0`，cwd `/usr/bin`）。

现场状态与清理：
- service mode 的 PID 1 是 `/usr/bin/launch_shell.elf`，不是 `/sbin/init`；
  它启动了 `bootin.elf` 和 `im.elf`（真实 `im.elf` 一直运行，PID 158）。
- `/proc/158/cmdline` = `im.elf debug s g r u l i a lazy-global`；
  基线环境里**没有** `LD_PRELOAD`（因为 dmode 文件不存在），与注入后的结果对照明确。
- 测试结束后 `umount /setting/mode` 成功，`/setting/mode` 回到原始空目录；
  `/log/dev/a7r3_probe` 已删除；测试 bootin 已 kill；真实 `im.elf` 仍在。
- **未写入 nflasha2/nflasha3/nflasha15，也没有改任何持久配置。**

现场环境补充：
- `/setting` 挂载参数：`rw,sync,dirsync,fmask=0022,dmask=0022,...,
  posix_attr,avoid_dlink,errors=remount-ro`，**没有 noexec**；权重位允许 0755。
- service shell 是 BusyBox v1.13.4（ash），**没有 `printf/tr/head/grep/sed/od`**；
  可用 `cat`、`echo -n`、`ls`、`rm`、`chmod`、`mount/umount`、`dd`、`kill`、
  heredoc。写文件用 `echo -n` 或 `cat <<'EOF'`。
- 没有 `/sys/class/power_supply`，无法从该路径读电池；本次没有持久写入，所以
  电池门槛不影响本次测试。
- 再次确认没有 `gdbserver`/`gdb` 二进制。

复现要点：
- 只需杀掉现有 `bootin.elf`；不必杀 `im.elf`，新 bootin 仍会执行 PATH 里的假
  `im.elf` 并传递 LD_PRELOAD。
- 真正加载一个有效 `.so` 还没做：下一步应放一个最小的 ARM soft-float `.so`
  （构造函数写一个 marker 文件），重复本流程，验证 `LD_PRELOAD` 不仅能传递、
  而且构造器确实在 `im.elf` 进程里执行。

### 真机验证 2：真实 .so 构造函数已执行（2026-10-25）

用现成的 `/usr/tool/libefence.so`（ARM soft-float ET_DYN，`efence_ctor`，
字符串 `"  Electric Fence 2.1 Copyright (C) 1987-1998 Bruce Perens."`）
重复同一套 tmpfs 覆盖测试，`/setting/mode/preload=/usr/tool/libefence.so`。

假 `im.elf` 捕获：

```
ARGV0=/log/dev/a7r3_probe/im.elf
ARGS=debug s g r u l i a lazy-global
LD_PRELOAD=/usr/tool/libefence.so
MALLOC_OPTIONS=gfffffffff
PWD=/usr/bin
```

`bootin.log` 同时出现 efence 构造函数的 banner：

```
  Electric Fence 2.1 Copyright (C) 1987-1998 Bruce Perens.
```

结论：
- 这不再是“环境变量传进去了”，而是 **ld.so 真正加载了 .so 并执行了它的构造函数**；
- 构造完成后 busybox 仍能执行假 `im.elf` 脚本（result.txt 正常写出），说明
  该 .so 没有把启动进程直接搞死；
- 测试仍然没有写 vfat/flash：`/setting/mode` 用 tmpfs 覆盖，结束后
  `umount` 成功并恢复原始空目录；`/log/dev/a7r3_probe` 已删除；测试 bootin 已 kill；
  真实 `im.elf` 继续运行。

当前阻塞点：
- 本机没有 ARM 交叉编译器（`arm-linux-gnueabi-gcc`、`arm-linux-gnueabihf-gcc`、
  `clang`、`zig`、`ld.lld` 都不在 PATH）。
- 要注入“自定义 hook .so”，需要先解决构建/传输：
  1. 安装 ARM soft-float 交叉工具链（或 clang+lld 的 ARM target）；
  2. 用 SD 卡：正常模式 MSC 下把 .so 拷到卡上，再进 service mode 挂载；
  3. 或从现成 .so 里挑一个可拦截 `open/mmap/osal_*` 的（LeakTracer 只拦 malloc）。
- 一旦有自定义 .so，就可以挂 `open("/system/av-cam.bin")`、`mmap`、`osal_snd_msg`
  等，先只记录再改写，开始验证 4K60 所需的描述符/消息路径。

## 注入测试安全规范（2026-10-25）

### 核心结论

- **tmpfs 覆盖 + 假 im.elf 的测试是易失的**：不改 `/setting`，不改 flash；
  死机/卡死只需断电重启，`/setting/mode`、`/log/dev` 自动恢复。
- **把 dmode/preload 写进真实 `/setting` 并冷启动不是易失的**：
  bootin 每次启动都会重新加载；坏 .so 会造成 boot loop，单纯重启不能恢复。
- 目前**未验证**“im.elf 崩溃后正常模式 USB（0BFF）是否仍能枚举、还能否切 0336”。
  在这条验证之前，不做持久 preload 的冷启动测试。

### 默认安全流程（优先使用）

1. 新 .so 一律先在 `verify_injection_chain.py --execute` 的 tmpfs 环境里验证：
   - `mount -t tmpfs tmpfs /setting/mode`
   - 假 `im.elf` 放到 `/log/dev/a7r3_probe` 并置于 PATH 最前
   - 只 kill `bootin.elf`，不 kill `im.elf`
   - 观察 `result.txt`（LD_PRELOAD）和 `bootin.log`（构造函数输出）
2. 迭代阶段继续用 service mode 手动重启 bootin；.so 放 `/log/dev`（tmpfs），
   不写 `/setting`。这样崩溃只影响当前会话，断电即恢复。
3. 只有当 .so 在假 im.elf 里能稳定加载、构造函数正常返回、无崩溃时，才考虑
   持久化到 `/setting`。

### 冷启动持久测试的前置条件（全部满足才做）

- 电池 >50%，过程中不要断电；`/setting` 有完整备份（至少 `Backup.bin` +
  `/setting/mode` 原状），并确认恢复方法可用。
- 先验证恢复路径：
  1. service mode 下 `kill -9 <im.elf pid>`，确认 0336 仍能重新枚举/重连；
  2. 断电重启，确认能正常回到 0BFF/正常模式。
- .so 必须**自解除**：构造函数第一件事就是
  `unlink("/setting/mode/preload")`（或把 dmode 写回 `3`），然后再做危险操作。
  注意：如果 .so 在重定位/加载阶段就失败，构造函数不会执行，仍会 boot loop；
  所以前置的假 im.elf 验证不可跳过。
- 构造函数里不要阻塞、不要长 sleep、不要依赖网络；先只写 marker 再逐步加逻辑。
- 准备一个“救援包”：SD 卡上的已知良好 `/setting/mode` 备份，或一个最小安全 .so；
  确认 service mode shell 在 app 挂掉时仍可用。

### 回滚清单（service mode）

```
umount /setting/mode                 # 如果做过 tmpfs 覆盖
rm -f /setting/mode/preload
echo -n 3 > /setting/mode/dmode      # 或 /usr/bin/change_mode.sh 3
rm -rf /log/dev/a7r3_probe
kill -9 <测试 bootin 的 pid>          # 不要随便 kill im.elf
sync
```

- `change_mode.sh 3` 会写 `dmode=3` 并清空 preload；它是 `/usr` 上的只读脚本，
  只改 `/setting`。
- 断电只清 RAM/tmpfs，**不会**清 `/setting`；boot loop 必须进 service mode 改文件，
  或走 updater/重刷。
- updater 能重刷 `/usr`、`/system`，但**不保证清 `/setting`**；如果坏文件在
  `/setting/mode`，重刷后仍可能复现。

### 本次已验证的安全事实

- tmpfs 覆盖 `/setting/mode` 成功，测试后 `umount` 恢复原始空目录；
- 只 kill `bootin.elf` 即可，真实 `im.elf` 可继续运行；
- 假 `im.elf` 能拿到 bootin 构造的 `LD_PRELOAD`；
- 真实 `libefence.so` 的构造函数在目标进程里执行并打印 banner；
- 全程没有写 nflasha2/3/15，没有调用 `auth.stop()`。

### 建议的下一步安全预演

在 service mode 里 kill 真实 `im.elf`（PID 158），然后重新断开/连接 USB，
确认 0336 仍可被 PMCA 重新打开；随后断电重启确认正常模式。这个预演只影响
当前会话，断电即恢复，用来回答“最坏情况下还能不能救回来”。通过后再做持久
preload 的冷启动测试。

## 自定义 hook 的构建与传输（2026-10-25）

### 工具链

本机没有 ARM 交叉编译器，但可以用 pip 安装 Zig：

```
pip install --user ziglang
python-zig cc -target arm-linux-gnueabi -shared -fPIC -nostdlib -Os \
    -o preload_marker.so preload_marker.c
```

产物：ET_DYN、EM_ARM、`e_flags=0x5000200`（EABI5 + soft-float）、无
`DT_NEEDED`；构造函数用 `svc #0`（`__NR_write=4`）写 marker，不依赖 libc。
1520 字节。

### 传输（无 SD 卡、无 FileControl）

service shell 的 BusyBox 没有 `printf/base64/xxd`，但有 `echo -n -e` 和 `dd`。
`dd` **不支持 `conv=notrunc`**，但按递增偏移 `seek=...` 写入会保留前缀。
所以用纯 ASCII 命令重建二进制：

```
# 每个连续非零段，按 24 字节分块：
echo -n -e '\0177\0105\0114\0106...' | /bin/busybox dd of=REMOTE bs=1 seek=OFF
# 末尾补零到原长度：
/bin/busybox dd if=/dev/zero bs=1 count=N >> REMOTE
```

- 八进制必须用 **`\0NNN`**（带前导 0）；`\NNN` 在这版 BusyBox 上解析不同。
- 上传脚本：`analysis/upload_payload.py`；校验用
  `/bin/busybox wc -c` / `/bin/busybox md5sum`。
- 实测 1520 字节 .so 上传后 md5 与本地一致。

### 已验证：自定义 .so 能被 bootin 加载

- 把 payload 放 `/tmp/a7r3_payload/preload_marker.so`（**不要放
  `/log/dev`**：harness 会再次 `mount -t tmpfs tmpfs /log/dev`，把上一层盖住）。
- `verify_injection_chain.py --execute --skip-baseline
  --preload /tmp/a7r3_payload/preload_marker.so`
- 假 `im.elf` 捕获：

```
ARGV0=/log/dev/a7r3_probe/im.elf
ARGS=debug s g r u l i a lazy-global
LD_PRELOAD=/tmp/a7r3_payload/preload_marker.so
A7R3_PRELOAD_OK
```

结论：**自定义 ARM soft-float .so 已能在 bootin 启动的进程里加载并执行构造函数**。
构建和传输两个阻塞点都解决了。

### 电子快门音的 hook 目标

- 机械快门音：`BEEP_ID_KA=0x0001`、`BEEP_ID_SHA=0x0002`、`BEEP_ID_KASHA=0x0003`，
  资源在 `/usr/share/avsystemsound/Dslr_Emnt_High_Shutter_*.bin`。
- 播放器：`libObj.so` 的 `ObjSystemSound` / `SequenceSetSystemSound`；
  `viewUnified2.so` 导出 `_ZN15ViewBaseProduct8PlayBeepEi`
  （`ViewBaseProduct::PlayBeep(int)`），`libObj.so` 导出
  `_ZN9LkmBeepIF12ModelExecuteEPKciP9ParamList` 和 `ObjSystemSound_Create/Destroy`。
- 快门类型/静音：`PARAMID_SHUTTER_TYPE`（AUTO/MECHA/ELEC）、
  `PARAMID_SILENT_SHUTTER_MODE`；UI 在 `viewUnified2.so`/`CautionConfig.so`
  （`ACT_ShutterSoundOff`、`AudioSignalsShutter`）。
- 电子快门静音说明固件在该分支没有下发 beep。下一步应先做 **log-only hook**：
  拦截 `ViewBaseProduct::PlayBeep(int)`、`open/openat/fopen`，记录 `soundId` 和
  是否打开 `*Shutter*.bin`；用真实 `im.elf` + 电子快门拍一张，确认是“没有调用”
  还是“调用了但被静音/音量 0”。再决定强制播放 `BEEP_ID_KA/SHA` 的位置。

### 尝试 service 模式真实 app：失败（2026-10-25）

已构建 `analysis/payloads/shutter_hook.c`（4420 字节，Zig 交叉编译，导出
`ViewBaseProduct::PlayBeep`、`LkmBeepIF::ModelExecute`、`open/openat/fopen`，
lazy binding，log-only）。它在假 `im.elf` 里能加载并写出 `HOOK_LOADED`。

随后尝试在 service 模式里：
1. `mount -t tmpfs tmpfs /setting/mode`，写 `dmode=3p` +
   `preload=/tmp/a7r3_payload/hook.so`；
2. `kill -9 <bootin> <im.elf>`；
3. 用正常 PATH 启动 `/usr/bin/bootin.elf dmode=3p`，期望拉起真实 `im.elf`。

结果：`kill -9 2980 158` 后 **USB service 连接立即 reset**
（`ConnectionResetError 10054`），随后 `bootin.elf` 启动命令也失败，
Senser terminal 变成 `GenericUsbException`。0336 仍在 USB 上枚举，但
backend 起不来。

**硬限制：service 模式的真实 `im.elf` 不能杀。** 它显然持有/重置了 USB
service 通道；杀掉它会把当前会话打断，无法在同一会话里重启真实 app。

现场影响：
- `/setting/mode` 是 **tmpfs 覆盖**，断电重启即消失；真实 `/setting` 分区
  没有被写。
- `hook.so` / `preload_marker.so` 在 `/tmp/a7r3_payload`（nflasha10 工作分区），
  是本次唯一的非易失残留，可在重连 service mode 后删除。
- 需要**断电重启**恢复 0336 service 通道（或回到正常模式）。

### 后续可行路径

1. **正常模式持久 preload（推荐）**：把自解除的 log-only hook 放到
   `/setting/hook.so`（或 `/system/hook.so`），写真实
   `/setting/mode/{dmode=3p, preload=...}`，重启进正常模式拍一张，
   再断电进 service mode 读 `/setting/hook.log`。
   - hook 构造函数**第一件事** `unlink("/setting/mode/preload")`，然后才做
     log/interpose；这样即使后面崩了，下次启动不会再加载，避免 boot loop。
   - 先在假 `im.elf` 里验证同一个 .so；再确认 `/setting`/`/system` 上的 vfat
     确实允许 ld.so mmap+exec。
   - 前置恢复预演：确认杀 im.elf 后 service 通道会断，但断电可恢复；确认
     能重新进 0336 并清理 `/setting/mode`。
2. **注入到运行中的 im.elf**：不杀进程，用 ptrace/dlopen 注入。本机没有
   gdb/gdbserver，且需要处理 ARM 32 位、SELinux/权限；复杂度高，暂不优先。
3. **资源级替换**：只改机械快门 `.bin`，不解决电子快门分支，不作为方案。

### 正常模式注入尝试与最终结论（2026-10-25）

本轮把三条正常模式路径都试了，结论如下：

1. **bootin 的 `/setting/mode/preload` 在 service mode 有效，正常模式不生效。**
   - 不带 `dmode=3p` 参数、只读文件时，service mode 仍能加载 hook
     （`LD_PRELOAD=/setting/hook.so`，`HOOK_LOADED`）。
   - 正常模式冷启动（含拔电池）后 `/setting/mode/preload` 仍在、
     `/setting/hook.log` 不存在 → bootin 的 preload 分支没有在正常启动链里执行。
   - 正常 app 很可能由 LIRO/WBI 阶段拉起，或 bootin 的阶段链用纯数字
     `dmode=3` 覆盖了 `p`；不是 hook 加载失败。
2. **`/setting/env.txt` 是死代码。**
   - init 的 `set_oreore_env`（0x95a4）解析 `KEY=VALUE` 并 `putenv`，
     但全 init 里**没有 BL/BLX 调用者**；写入 `LD_PRELOAD=...` 无效果。
3. **nflasha10 的 `/etc/ld.so.preload` 只覆盖 service mode。**
   - 写入 `/etc/ld.so.preload=/setting/env_marker.so` 后，service mode 里
     每个新动态进程都加载 marker（cat/busybox/adjstctl/ls 均有记录）。
   - 正常模式冷启动后 marker 日志没有新增任何条目 → 正常模式 loader 读的是
     **rootfs nflasha7 的 `/etc`**；nflasha10 的 `/etc` 是 service mode
     的叠加，正常启动早期不可见。
   - 离线确认：nflasha7 rootfs 的 `/etc` 为空，`/lib` 有 `ld-2.11.2.so`、
     `libc`、`libdl`、`libpthread` 等；`/etc/ld.so.preload` 不存在。

### 下一步：rootfs 注入（因电量低暂停）

要覆盖正常模式，必须写 **nflasha7 rootfs**：

```
/lib/env_marker.so        # 已构建 analysis/payloads/env_marker_root.so (2148 B)
/etc/ld.so.preload        # 内容: /lib/env_marker.so
```

- marker 只写 `/tmp/env_marker.log`（回退 `/setting`、`/log`、`/dev`），
  记录 pid/comm，不拦截任何函数；最坏情况是 loader 打印错误并继续，
  不会 boot loop。
- 两个文件都是**新增**，可随时删除；rootfs `/etc` 原为空，无覆盖风险。
- 写完后冷启动、拍一张，再进 service mode 读 `/tmp/env_marker.log`，
  搜 `comm=im.elf`；确认后再换成真正的 `PlayBeep` hook（按 comm 过滤 +
  加载后自解除 `/etc/ld.so.preload`）。
- 本轮因电量低，用户要求**暂不写 rootfs**；nflasha7 未被修改。

### 本轮清理状态

- 已删除 `/etc/ld.so.preload`（nflasha10）。
- 已删除 `/setting/{env.txt,env_marker.so,env_marker.log,hook.so,hook.log}`；
  `/setting/mode` 恢复为空目录。
- 已删除 `/tmp/a7r3_payload`、`/log/dev/a7r3_probe`。
- rootfs nflasha7 **未修改**；没有调用 `auth.stop()`。
- 构建/传输链已固化：Zig 交叉编译 + `echo -n -e '\0NNN' | dd seek=...`
  上传 + md5 校验；root marker 源码 `analysis/payloads/env_marker_root.c`。
- 下次上机前先充电 >50%，再写 rootfs 两个文件；写前建议再确认一次
  `/setting/mode` 为空、`/etc/ld.so.preload` 不存在。

### 2026-10-31 电子快门声音 hook：/usr 通道 + 全量清理

- 根因（已证）：`im.elf` 在挂载 `/setting` 和 `/etc` 之前就 exec，PID 1 是
  `/usr/bin/launch_shell.elf`，bootin/im.elf 的 PPid 都是 1；冷启动后
  im.elf 环境里没有 `LD_PRELOAD`。因此 `/etc/ld.so.preload` 和
  `/setting/mode/preload` 两条通道都到不了 im.elf。
- `/usr` 通道已在 QEMU 验证：`bootin.elf` 等长补丁 `0x1e2b`
  `/setting/mode/dmode` -> `/usr/bin/top`、`0x212e` `/setting/mode/preload`
  -> `/usr/bin/iperf`，hook 放 `/usr/tool/libefence.so`；合成 `/etc` 为空时
  仍打印 `HOOK_ROOT comm=im.elf`。
- **本次没有写 /usr**：用户选择全量清理、暂停实验。
- 清理完成（相机仍在 service mode，PID 0336，未调用 `auth.stop()`）：
  - nflasha7 rootfs 的 `/etc/ld.so.preload` 已删除，重新以 `ro` 挂载后确认
    文件不存在、`/` 恢复 `ro`。
  - nflasha10 的 `/etc/ld.so.preload` 已删除。
  - `/setting` 已清理：`shutter_hook_root.so`、`env_marker_root.so`、
    `env_marker.log`、`hook.log` 全部删除，`/setting/mode` 目录删除；
    其余为相机原有文件。
  - `/usr`（nflasha15）未修改。
- 备份用 stock `/usr` 校验值（写之前先 `busybox md5sum` 比对）：
  - `/usr/bin/bootin.elf` 13116 `a40d994c12e087a4c209ff1386786a12` `r-xr-xr-x`
  - `/usr/bin/top` 48458 `1ad4039f54168809e25357d48a460785`
  - `/usr/bin/iperf` 62462 `610e1b442e358ead0102c46bba6b3ce0`
  - `/usr/tool/libefence.so` 18296 `8a9fe844806bcbe47e980f7478b5b3b9` `rwxrwxrwx`
- 下次恢复实验的顺序：`rootfs_preload.py cleanup`（含 rootfs 文件）→
  `usr_bootin_inject.py stage` → 冷启动拍一张 → `rootfs_preload.py read`。

### 2026-10-31 QEMU 复验启动链（/usr 通道 A/B）

- 新增脚本 `analysis/qemu_a7r3/verify-boot-chain.sh`（A/B 启动）、
  `verify-imelf-preload.sh`（读 im.elf 的 maps/environ）；
  **每次启动前必须从 stock 重建镜像**——QEMU 的 mtd 盘可写，同一镜像
  二次启动会卡在 GPU 初始化。
- 两次运行的合成 nflasha10（`/etc`、`/tmp`）都是空 vfat，唯一变量是
  nflasha15，因此 `/etc` 通道被同时排除：
  - stock：im.elf(pid 126) maps 无 libefence，environ 无 `LD_PRELOAD`，
    `HOOK_ROOT=0`；bootin.elf md5 `a40d994c…`。
  - patched：im.elf maps 含 `/usr/tool/libefence.so`（`r-xp`，紧跟在
    `/lib/ld-2.11.2.so` 之后，正是 `LD_PRELOAD` 的映射顺序，不是 dlopen），
    environ 里 `LD_PRELOAD` 计数 1，`HOOK_ROOT=1`，`pid=126`、
    `unlink_rc=-2`（说明当时 `/etc/ld.so.preload` 不存在）；
    bootin.elf md5 `b34aab4d…`。
  - 两次 PID 1 都是 `launch_shell.elf`，且 PID 1 的 maps 都没有 libefence
    → `LD_PRELOAD` 只由 bootin 传给 im.elf，不是全局环境变量。
  - 离线读镜像确认：patched `bootin.elf` 在 `0x1e2b`=`/usr/bin/top`、
    `0x212e`=`/usr/bin/iperf`、`/setting/mode` 出现 0 次；`/bin/top`=`3p`、
    `/bin/iperf`=`/usr/tool/libefence.so`、`/tool/libefence.so`=hook ELF。
- **边界**：QEMU 里 `/usr` 和 `/etc`(nflasha10) 都在 im.elf 之前挂好，
  复现不了真机“im.elf 先于 `/etc` 挂载”的时序；QEMU 只证明 `/usr` 通道
  机制成立，不能替代真机验证。im.elf 仍卡在 Sugilite
  （`failed to open sugilite device`、`DMP_2D_Initialize: -9999`、
  `State: D`），`ModelExecute=0`，所以本轮只证明“加载”，未证明 beep 路径。
- QEMU eMMC 写入超时（`errno -110`，sector 374872）会让设备卡死，
  此时读 `/usr` 的 `md5sum`/`ls /etc` 会挂住——先读 `/proc` 再读盘。

### 2026-10-31 借 zve10-qemu（同 CXD90045）修 eMMC 写路径

- 参考仓库 `zve10-qemu/`（cokeenet/zve10-qemu，ZV-E10，同 SoC/libObj/im.elf/
  `/dev/dmpgles2`）。其 `patches/qemu/sd-cid.patch` 给出 CMD23 修复：
  JEDEC 规定块数只在 bit15:0，原实现 `multi_blk_cnt = req.arg` 把
  `0x80000005` 这类标志位也算进去 ⇒ 减计数永远到不了 0 ⇒ 卡停在
  `receivingdata` ⇒ 索尼驱动 `wait_for_trans_state` 5 s 超时。
- 已应用到本机 QEMU：`/root/qemu-ma1co/hw/sd/sd.c:1470`
  `multi_blk_cnt = req.arg & 0xFFFF`（原文件备份 `sd.c.orig-cmd23`），
  已在 `build-arm` 重新编译。
- **实测效果**（`analysis/qemu_a7r3/verify-cmd23-fix.sh`）：
  - `wait_for_trans_state` 超时 **0**（之前满屏）、`I/O error` **0**；
  - guest 时间跑到 37 s，`initcall ... returned` 313 条；
  - `HOOK_ROOT comm=im.elf` 仍然出现（`/usr` 通道未受影响）。
- **新的失败点**（与 A7M3 文档一致）：im.elf 走 abort 路径后
  `State: Z (zombie)`，异常监视器报
  `Illeagle Instruction at 0x042d1796`；libc base `0x042bc000` ⇒
  **libc+0x15796**，`lr=0x042e0d8f`=libc+0x24d8f=`raise`，`r7=0x10c`=`tgkill`。
  GPU 侧先打印 `failed to open sugilite device` / `DMP_2D_Initialize : -9999`
  后仍继续加载了 hdmi/cec 等模块，abort 发生在其后。
- zve10 仓库可直接复用的资产：
  - `emulator/menu/dmm_shim/fake_gles2.c`：注册 `/dev/dmpgles2` 的字符设备桩，
    ioctl 契约与我们在 `libObj.so` 定位到的完全一致
    （`0x80048206` / `0xc0048202` / `0x80048263`，另有 `0xc0048211/12` 的
    wait-done），mmap 用 `dma_alloc_coherent` + `remap_pfn_range`。
  - `docs/analysis/system/SUGILITE_MMAP_LEAD.md`：真驱动 `sugilite_mmap_dev`
    只接受 512 / 128 KB / 32 MB 三种尺寸；寄存器窗口 0x4000，pfn 0xF1000。
  - `tools/port/swgpe/swgpe_shim.c`：`GRM_gpermFillrect/Rectblit` 的 CPU 实现，
    含 bitmap 布局（`[bmp+0x08]` 像素、`geo+0x18` stride、`table[geo+0x1C]` bpp、
    bpp 表 `[0,2,4,2,1,2,2,2,4,4]`），可按符号名跨固件复用。
  - `patches/kernel/tty-visible-qemu.patch`：QEMU 下强制打开 console，
    让 App 的用户态输出可见。
- 真机安全情报（他们踩过两次砖）：glibc 2.11 下 `/etc/ld.so.preload` 指向
  加载失败的 .so 会让**每次 execve 都失败**，只能靠 BootROM；且
  `/setting/mode/dmode` 少一个换行就会导致“UI 出来但 3 秒重启”。
  另外 A7R3 的 stock `/setting`（nflasha2）**没有 `mode` 目录**，
  所以之前 `rmdir /setting/mode` 是恢复原状，不是破坏。
- 尚未直接移植：`bionz.c` 机器模型（boot_size 0x200000 / WBI 7 段 / DDR 1G+256M）
  与我们 ma1co 的 `cxd90045` 不同；`fake_gles2.ko` 依赖其内核符号与 vermagic；
  `libObj.so` 版本不同（他们 21 MB，我们 19.4 MB），地址必须在本机重算。

### 2026-10-31 用户态 GPU shim（/dev/dmpgles2）在 QEMU 的结果

- 新 payload `analysis/payloads/gpu_shim_qemu.c/.so`（5,928 B，ET_DYN ARM EABI5
  soft-float，无 `DT_NEEDED`，仅弱 `dlsym`）。拦截
  `open/openat/ioctl/mmap/mmap/munmap/close`，只在 `comm==im.elf` 时生效；
  `/dev/dmpgles2` 用 `/dev/zero` 的 fd 顶替，mmap 转匿名页。
- **两个必须记住的构建/实现坑**：
  1. 编译必须带 `-ffreestanding -fno-builtin -fno-exceptions
     -fno-unwind-tables -fno-asynchronous-unwind-tables`。否则会产生强未定义
     `__aeabi_unwind_cpp_pr0` / `strlen` ⇒ glibc 2.11 下 preload 解析失败是
     致命的，im.elf 直接起不来（表现为 `libefence.so` 已映射但构造函数不执行）。
  2. 拦截的 `mmap` 转发**只能用裸系统调用**（`__NR_mmap2=192`），
     不能在加载期 `dlsym(RTLD_NEXT,"mmap")`——ld.so 自己会调 mmap，
     在那里进 dlsym 会跳到 0（实测 `pc=0`，page fault at 0）。
- 实测 ioctl 序列（`analysis/qemu_a7r3/verify-gpu-shim.sh`）：
  `0x80048206`（寄存器大小，回 0x4000）→ `0xc0048202`（index→地址）→
  `0x80048263`（hw bypass，0=disabled）→ `0xc0048211` → **`mmap(0x20000)`**
  → `0xc0048212` → 之后 **961 次 `0xc004820d`**（环门铃），共 1,159 次 ioctl。
  128 KB 环与 zve10 `SUGILITE_MMAP_LEAD.md` 的真驱动契约一致。
- 效果：App 加载的库从 **56 → 73 个**，GPU 初始化不再返回 -9999；
  崩溃点从 libc abort（`libc+0x15796`）变成
  `page translation fault at 0x28`，`pc=0x0411abaa`（libpthread），
  调用方 `lr=0x07707db5`（**libmpr.so**）。
- 新加载的库里已经出现 `libWlanResource / libWlanAdjust /
  libWlanControlCommon / libNetService / libnl-* / iw.so` —— 正是 zve10
  文档记录的“App 停在网络栈”那一层。
- 下一个可试的方向（zve10 已记录）：给 `WlanAdjust::start` 打“返回成功”的半桩
  能再推进约 6 个库，但会崩在 uipc 共享区；或继续补 `/dev/mem` 的 mmap
  （App 已按 `0xc0048202` 返回的地址去 mmap `/dev/mem`：
  映射里可见 `mem` offset `0x50000000` / `0x15d6b000` / `0x20098000`）。

#### 第二轮：稳定化后 App 能活到 95 s

- shim 里给环门铃 `0xc004820d` 加了约 40k 次空转的忙等，并且只记录前 24 次
  （减少串口/文件日志对时序的扰动）。改完后同一次 95 s 启动中
  `Exception=0`，im.elf（pid 126）在 18.4 s / 38.9 s 两次采样都是
  **`State: S (sleeping)`**，一直活到 QEMU 超时。
- 在此之前的版本约 12 s 会崩：
  `__pthread_mutex_lock(0x1c)` → `ldr r7,[r0,#0xc]` 读 `0x28`。
  调用链：`libmpr` 的 flash 日志写路径 `0x334058`（`lseek`+`write`）
  取全局锁 `0xc538b0`；该全局只由另一个函数 **`0x333300`**
  （`open("/dev/nflasha4", O_RDWR)` 成功后 `new(0xa0)` + ctor 再写回）创建。
  `.ARM.exidx` 证实 `0x333300` 与 `0x334058` 是两个独立函数；QEMU 里前者
  从未被调用（那次 `/dev/nflasha4` 的 open 来自别处），于是写路径拿到 NULL。
  **这是竞态**：忙等/安静日志改变时序后不再复现。
- 失败过的三次尝试（已回退，别再走）：
  1. 在 `open("/dev/nflasha4")` 时用 `/proc/self/maps` 找 libmpr 基址再调
     `0x333300` —— 那一刻 libmpr **还没 dlopen**，`base=0`。
  2. 改用 `dl_iterate_phdr`（弱引用）找基址 —— 启动即 `pc=0`
     （fault at 0，`lr` 落在 `DT_PLTGOT+8`），preload 里这个时机不安全。
  3. 拦截 `pthread_mutex_lock/unlock` 把非法指针换成零初始化互斥量 ——
     同样启动即 `pc=0`。**结论：在这套 glibc 2.11 + Sony loader 下，
     不要在 preload 里拦 pthread 或调用 dl_iterate_phdr。**
- 现状：App 活到 95 s 但之后安静（最后一批 GPU ioctl 在 ~6.4 s），
  `libWlan*` 已加载 —— 与 zve10 记录的“停在网络栈、UI 未创建”同一层。
- **注意：这不是稳定修复**。同一份 6,584 B 构建重复跑，约一半次数仍会崩在
  `libmpr` 的 NULL 锁（`fault at 0x28`），说明这是 im.elf 自身的竞态；
  忙等只能改变概率。另有两类“启动即 `pc=0`”（fault at 0，调用栈落在
  `libefence` 的数据段）也会间歇出现，与本 shim 的体积/布局有关，
  目前无法稳定规避。
- 结论：QEMU 侧已经能证明 GPU 通道全部跑通，但把 App 稳定推到 UI
  属于研究级工作量（zve10 用了数月）。若只为快门声音，真机 `/usr`
  通道 + hook 才是主路径，QEMU 只用来预演和验证加载。

#### 第三轮：为“完整仿真”铺的地基（2026-10-31 深夜）

目标改为完整仿真（后续要解锁视频规格），因此先补基础设施：

1. **镜像内直接改 `libmpr.so`**（新脚本 `analysis/qemu_a7r3/patch-libmpr.py`）：
   把 flash 日志函数 `0x334058` 里的 5 个锁/解锁 `bl`
   （`0x3340f2`、`0x334148`、`0x33416c`、`0x334188`、`0x3341c0`）
   换成 `00bf 00bf`（Thumb NOP）。崩溃从 `fault at 0x28`（NULL 锁）
   变成在 `THREAD_INFRA1` 里 `raise(SIGABRT)`（`lr = raise+0x21`），
   调用链仍全在 libmpr 的 `0x334xxx`。
2. **重建 `/usr` 而不是原地补丁**（新脚本 `analysis/qemu_a7r3/repack-usr.sh`）：
   `debugfs rdump` → 宿主机改文件 → `mke2fs -t ext2 -b 1024 -d` 重打包。
   这样做才能**新增文件**（原地补丁只能改已有文件）。
3. **固定 klog/alog 环**：`-append` 对这台机器无效（cmdline 硬编码在
   `hw/arm/bionz.c:253 CXD90045_CMDLINE`）。已改该宏并重建 QEMU，加入
   `memrsv=0x00100000@0x1F000000 klog.addr=0x1F000000 klog.size=0x00080000
   alog.addr=0x1F080000 alog.size=0x00080000`。
   实测生效：`alog.addr is : 1f080000`、`console [klog0] enabled`，
   用 `dd if=/dev/mem bs=4096 skip=126976 count=128 | busybox grep -a -o
   '[ -~]{8,}'` 能把 klog 环的文字读回串口。
   （物理内存：`0x00000000-0x3fffffff` System RAM，`0x20000000` 起 512MB 是
   device 保留；原 cmdline 见 `hw/arm/bionz.c`。）
4. **`qsi.elf = ulogio.elf`**：bootin 的启动列表里有 `/usr/bin/qsi.elf &`
   （A7R3 没有这个文件），而 alog 环由 ulogio 服务写入（zve10 同结论）。
   已在重打包时把 `bin/ulogio.elf` 复制为 `bin/qsi.elf`（19,200 B）。
   **但实测 bootin 并没有把它拉起来**：崩溃后 `/proc/*/comm` 里只有
   `launch_shell.el / bootin.elf / im.elf / ash / KIKILOG_MAIN`，没有
   qsi/ulogio，所以 alog 环仍然全零。
5. 目前卡点仍是：im.elf 在 ~12 s 时由 **THREAD_INFRA1** 在 libmpr 的
   `0x334xxx` 路径里 `raise(SIGABRT)`；拦截 `raise/abort/__assert_fail`
   都不生效（glibc 2.11 内部不走 PLT），App 自己也不打印原因。

**下一步（按顺序）**：① 让 ulogio 真正跑起来（fork+exec 由 shim 自己拉起，
或改 bootin/init）；② 拿到 alog 里的失败原文；③ 修 flash 日志路径；
④ 再走“强制创建屏幕”（`AbstractSequenceFactory::CreateSequence` /
`ux::gfxeng::Screen`）配合软件 GPE + ramfb。相关脚本：
`patch-libmpr.py`、`repack-usr.sh`、`dump-rings.sh`、`probe-procs.sh`。

#### 第四轮：修掉 Sony flash ioctl，App 稳定跑满 120 s（2026-11-01）

- 用 shim 记录 flash fd 上的 ioctl，拿到真实命令号（**不是** BLKDISCARD）：
  - `0x80014123` `_IOR(0x41,0x23,1)` 查询；
  - `0x40084124` `_IOW(0x41,0x24,8)` 擦除，参数 `{addr,len}`，
    实测 `w0=0x200 w1=0xfffffe00`、`w0=0 w1=0x200`。
  就是它触发内核的 `AL_EMMC_BLK (ioctl_trim) : Erase range is not correctly
  set, nflasha4 (0 - 2 sectors)`，App 的日志线程随后 `raise(SIGABRT)`。
- shim 里对 `0x40084124` 直接返回 0（模拟器不需要真擦除）后：
  - `Erase range` 报错 **0** 次；
  - **`Exception Syndrome` 0 次**，im.elf 在 120 s 运行里一直活着
    （不再有 `raise`/`pc=0`）；GPU 初始化与环门铃正常。
- 同时 shim 在 im.elf 启动时 `fork+exec /usr/bin/ulogio.elf`，日志里能看到
  `[ulogio]>` 横幅与它打开 `/dev/mem`；但 alog 固定环（0x1F080000）仍无
  可打印文本，`/tmp/kikilog.dat` 也仍是全零。
- 相关改动：`gpu_shim_qemu.c` 增加 `flash_add/flash_has`、
  `SONY_FLASH_ERASE=0x40084124` 拦截、`log_flash_ioctl`、
  `fork/exec ulogio`；新增 `run-app.sh`（不打扰跑 120 s）、`analyze-run.sh`、
  `probe-ringhex.sh`、`probe-kallsyms.sh`、`probe-iomem.sh`、
  `patch-qemu-cmdline.sh`。
- 物理内存（`/proc/iomem`）：`0x00000000-0x3fffffff` System RAM；
  `0x20000000` 起 512MB 为 `memrsv=...:device`；日志环放在
  `0x1F000000`(klog 512K) / `0x1F080000`(alog 512K)，已用 `memrsv` 保留。
- **下一阶段（出画面）**：App 已稳定但不会自己创建 UI（与 zve10 结论一致）。
  需要在 preload 里主动调用 libObj 的屏幕/序列创建入口
  （`ux::gfxeng::Screen`、`AbstractSequenceFactory::CreateSequence`），
  并把 `GRM_gpermFillrect/Rectblit` 换成 CPU 实现（参考
  `zve10-qemu/tools/port/swgpe/swgpe_shim.c`）+ 用 ramfb 把像素送出去。

#### 第五轮：行/符号定位（2026-11-01）

- `libObj.so`（A7R3）里 `ux::gfxeng::*` 有 53 个导出，含
  `GraphicsEngine::sys_getInstance` @`0x5c7b70`、
  `getFramebufferWidth/Height`、`updateResource`。
- `viewUnified2.so` 里这些是 **UND 导入**，实现都在 libObj：
  - `_ZN13viewManagerIf8openViewEPKci` @`0x3e0da8`（16 B 包装）：
    先 `bl 0x3e0cd0`，再尾调 `LkmGpxIF::UnsetMute`。
  - **`0x3e0cd0` 才是真身**：`r0`=名称、`r1`=flag；它读全局单例
    （`ldr r6,[r4,r3]; ldr sb,[r6]`），单例为 **NULL 时直接跳过**
    （`cmp sb,#0; beq 0x3e0d2e`）；否则用 `std::string(name)` 调 `0x3ce0e8`。
  - `_ZN8ViewBase8openViewEPKcib` @`0x3dc274` → 尾调
    `ViewBase::openViewWithoutUnsetMute` @`0x3dc198`。
  - `CmnViewGenOpenUtil::openMenu` @`0x2b0298`（在 viewUnified2 里，有真实实现）。
- ⇒ **盲目调用 `openView` 没用**：view manager 单例在 App 的初始化流程里创建，
  App 目前停在网络栈，单例还是 NULL。必须先让它走到建立 view/model 系统，
  或者手工构造/初始化该单例（需要更多静态分析）。
- 结论：下一步的瓶颈不是“调哪个函数”，而是**看清 App 为什么停在网络栈**。
  首选手段：QEMU gdb stub 断在 libmpr/网络栈的等待点，或把 ulogio 的
  共享内存环找出来（`/proc/kallsyms` 可读，kptr_restrict=0）。

#### 第六轮：shim 内置探针线程（2026-11-01）

- 在 shim 里用 `clone(CLONE_VM|FS|FILES|SIGHAND|THREAD|SYSVSEM)`
  （raw syscall 120，flags `0x50F00`，静态 8 KB 栈）起了探针线程，
  每 6 s 扫描 `/proc/self/task/<tid>/comm` 并输出
  `PROBE t=<tid> c=<comm> s=<syscall>` 到串口。
  `/proc/<tid>/syscall` 在这个 3.0 内核上读不到（`s=` 为空），但线程名已足够。
- **im.elf 实际已经完整初始化**（约 70 个线程），不是“卡在网络栈”：
  `uipc_proxy_0..3`、`ULOGIOD_MAIN/CB`、`InfraOnDmdLdr`、`mwf_monitor_inp`、
  `THREAD_INFRA0/1`、`InfraAudioDecoder/AvEffector/Decoder/MovieEditor/
  MovieEncoder/StillEncoder/AvbbcIf`、`UsbControl`、`BusPower`、`Hotplug`、
  `grm_dn_vdf_if`、`IFsys`、`IMM`、`IMD-*`、`HDMI/CEC Msg/Dev Thread`、
  `InfraNfc`、`InfraBluetoothM`、`WlanResource_Ma/Ir`、`NetServiceMainT`、
  `MMgrCtrl`、`GpsRsMainThread`、`gps_main_thread`、
  **`DVDFlashBackup`**（就是之前会崩的 flash 线程，现在健在）、
  `CameraBackground`。
- ⇒ 说明各子系统都起好了，只是**没有创建/打开视图**（view manager 单例仍为
  NULL，见上一节）。下一步应该从“补服务”转向“触发视图创建”：
  在探针线程里等 App 稳定后（例如 20 s）调用
  `viewManagerIf::openView(<菜单名>, 0)`，菜单名可从 `viewUnified2.so`
  的数据段字符串（如 `cmnViewSettingNodeRootShowTopMenu`）或 IMDB 表里取。
  若单例仍为 NULL，再考虑手工构造它（需要继续静态分析 `0x3e0cd0` 的写入者）。

#### 第七轮：运行时确认 —— 图形引擎本身就没创建（2026-11-01）

- 探针里增加 `probe_viewmgr()`：通过 `/proc/self/maps`（循环读到 256 KB，
  im.elf 的 maps 远超 48 KB，之前截断导致 “no libObj”）拿到 libObj 基址，
  再读 view manager 槽并调用 `GraphicsEngine::sys_getInstance`。
  实测输出：

```
VMGR base=5c1c000 got=6ebae24 mgr=0
VMGR GraphicsEngine=0
```

- 结论（确定）：
  - libObj 基址 `0x05c1c000`；view manager 槽
    `base+0x12215a8 → base+0x129ee24`，值为 **0**；
  - **`ux::gfxeng::GraphicsEngine::sys_getInstance()` 返回 0** ——
    图形引擎对象根本没被创建。
  - App 自身完全稳定（`Exception Syndrome` 0，70+ 线程齐全）。
- ⇒ 视图打不开不是 view manager 的问题，而是**图形引擎没初始化**。
  下一步的正确目标是 `GraphicsEngine::sys_getInstance` @`0x5c7b70`
  的**单例槽写入者**（和上一轮一样：反汇编取字面量 → 找 PIC 引用 →
  找到创建函数），在探针线程里调用它，然后才是 `openView`。
- 新增/修改：`gpu_shim_qemu.c` 的 `probe_viewmgr()`、`libobj_base()`；
  `run-app.sh` 用 `-serial file:` 避免 QEMU 的 PL011 调试输出淹没日志。

#### 第八轮：GraphicsEngine 单例槽的静态定位（2026-11-01）

- `sys_getInstance` @`0x5c7b70` 的实现：
  `r3 = 0x5c7b78 + L1(0xc52848) = 0x121a3c0`（GOT 基址），
  `r3 = *(0x121a3c0 + L2(0x4cb0)) = *(0x121f070)`，`r0 = *(r3)`。
  ⇒ 单例指针槽在 **`base+0x121f070`**；文件里该槽初值为 0，
    且只出现在 `.rel.dyn`（加载时 R_ARM_RELATIVE 改写）。
- 对 `0x121f070` 做 PIC 扫描 **没有找到任何 add-pc 引用**，
  说明创建/写入不是简单的静态 store（可能经由 GOT 间接写，
  或由 model/registration 路径在别处完成）。
- ⚠️ 踩坑：在探针里额外读 `*(base+0x121f070)` 再解引用第二层，
  会导致探针线程静默 SIGSEGV（im.elf 自己装了信号处理器，
  串口连异常监视器都不进），表现为“探测轮次正常但没有 VMGR 输出”。
  **以后在 preload 里只调用导出的函数，不要按推测去解引用内部指针。**
- 已验证的结论保持：`mgr=0`、`GraphicsEngine=0`，App 稳定运行。
  下一步应改用 QEMU gdb（`-s`）在 `sys_getInstance`/其写入点下断，
  或从 `ux::gfxeng` 的 model/registration 路径找回创建者，
  而不是继续猜测内存布局。

#### 第九轮：gdb 接入与“App 从未请求图形引擎”（2026-11-01）

- 已安装 `gdb-multiarch` 17.1，新增脚本
  `analysis/qemu_a7r3/probe-gdb-getinstance.sh`：带 `-gdb tcp::1234` 启动
  QEMU，等探针打印 `VMGR base=` 后按该基址 `hbreak sys_getInstance`。
- 实测：**硬件断点 40 秒未命中**（`gdb rc=124`，随后命令因 target 仍在跑而失败）。
  结合 `GraphicsEngine=0`，结论是：
  **App 在 QEMU 里从未调用 `GraphicsEngine::sys_getInstance`** ——
  不是创建失败，而是根本没有走到使用图形引擎的代码路径。
- 继续静态查证：
  - 单例链：`base+0x121f070`（R_ARM_RELATIVE，初值 `0x129f40c`）
    → `base+0x129f40c` = 实例指针，当前 0；
  - `libObj` 只导出 7 个 `GraphicsEngine` 成员（getInstance、framebuffer 尺寸、
    updateResource 等），**没有构造/初始化导出，也没有 vtable/RTTI 符号**
    （本地符号已剥离），所以无法直接“手工创建”。
  - 每轮 libObj 基址都会变（dlopen 的 mmap 时机），关 `randomize_va_space`
    也无法固定，因此 gdb 必须 attach 后按日志基址下断。
- ⇒ 现在缺的不是“某个地址”，而是**App 启动 UI 的触发条件**。
  zve10 也在同一层停下（“UI 从未被创建”）。
- **建议的下一步（跨到真机）**：用已经验证的 `/usr` preload 通道，
  在真机上 hook `GraphicsEngine::sys_getInstance` / `viewManagerIf::openView` /
  `ViewBase::openView`，记录真机启动时**真实的调用顺序与参数**；
  拿到序列后再在 QEMU 里按序调用（而不是继续猜）。这比在 QEMU 里
  盲找构造函数更省时间，也直接服务于后续的视频规格实验。

#### 2026-11-01：能不能学尼康 Z8，把挂载项放 SD 卡做“拔卡即恢复”

- 结论：**A7R3 不能照搬**。启动链里没有任何一步在 im.elf 之前读 SD 卡：
  - `/sbin/init`（rootfs）的字符串只有 `/dev/nflasha2/3/7/9/10/11/12/15/18/23/24/25`
    和 `/dev/dnflasha*`，以及 `/usr/kmod/mmc_drv.ko`、`/kmod/emmc.ko`；
    **没有 `/dev/ms*`、没有 SD 挂载点**；
  - `bootin.elf` 里与挂载相关的只有 `/proc/mounts`、`/dev/pts`、
    `mount -t tmpfs none /tmp/lt`，同样不碰 SD；
  - `/dev/ms1`（SD/MS 槽）只出现在 `im.elf` 里，且已知 im.elf 启动时
    `/setting`、`/etc` 都还没挂载，SD 更不可能早已就位。
  - 实测里 SD 卡只能由 service shell 手工 `mount -t vfat/exfat
    /dev/mmca10p1`（`analysis/probe_sdcard.py`），属于维护通道。
- 因此“把 preload/挂载项放卡上，拔卡恢复”在 Sony 这条链上没有落点。
- 现有的等效安全性（建议继续用）：
  1. `/usr` 通道只影响 im.elf（bootin 从 `/usr/bin/iperf` 的内容读 preload 路径），
     PID 1 的 `launch_shell.elf` 与 service mode USB 不受影响 ——
     im.elf 起不来时仍可连 0336 用 PC 修。
  2. `/setting` 是 vfat 且可写：删掉 preload 文件/`mode` 目录即可关掉通道。
  3. 最坏情况用官方 updater 或 nflasha15 全量备份回刷。
- 若坚持要“卡控安全模式”，需要改 `/sbin/init`（rootfs，boot-critical）：
  在挂载阶段尝试 `mount /dev/ms1`，有标志文件才启用卡片里的 preload，
  卡不在则跳过。这要动 init、风险高于 `/usr` 方案，且 init 里本来就加载
  `mmc_drv.ko`，可行性未验证；不建议在现阶段做。
- SD 卡仍然有用：作为**备份/日志**载体（`dump_to_sdcard.py` 已支持
  把分区 dump 写到卡上），不参与启动。

#### 2026-11-01：新参考仓库的结论（A7M3 QEMU / Creative Studio / connect-shell）

- `a7m3-firmware-qemu/ui-rendering/`（Derek2011cc，2026-11-01 新增）：
  - **已经在 QEMU 里打开原生菜单**：在 `im.elf` 进程内加载插件，调用
    `viewManagerIf::openView("view/BASE_MENU", 0)`，随后 App 提交 15 次
    2D submit 和数十个 GPU kick；`viewSettingMenu.uxc` 出现在 maps 里。
  - 他们确认菜单**不经过** CPU 软件 GPE（`GraphicsImage::clear/copy`、
    `GRM_gpermFillrect/Rectblit` 整场 0 调用），而是 GPU 命令流。
  - 我们 A7R3 的 `viewUnified2.so` 里有 **231 个 `view/...` 名字**
    （含 `view/BASE_MENU`），与 A7M3 同一套注册表。
  - 我们照这个调用试了一次（`view/BASE_MENU`, 0）：**崩在 NULL+0x10**
    （`page translation fault at 0x10`），证明我们的 view manager 仍是 NULL。
    A7M3 那边能成功，说明它前面有一步初始化（或 `target=qemu` argv 差异）。
  - 他们的 QEMU 侧还有 Sugilite 命令捕获/软件重放模型（`qemu/bionz-*.patch`）、
    LZ77 `/dev/ldec` shim、命令字表（`(op<<16)|0xf000`、
    `0x0589<<3`=纹理 PA、`0x0112<<4`=表面 PA）——A7R3 可参考，但偏移/布局不同。
- `sony-a7r3-creative-studio/`（Bigrabbit057）——**A7R3 3.10 实机验证过的持久化通道**：
  - 持久入口是 `/usr/bin/startBTCore.sh`（**1283 B**，SHA-256
    `33c82447cdbcdbd27e476ddf15f39f19a96d826d775e241d14ee0691d97beb27`），
    在这两个注释位置打补丁：offset **44**（19 B）和 **391**（18 B），
    把注释改成调用 `/setting/cs &`；组件放 `/setting`（vfat，可写），
    运行副本在 RAM。
  - 安装前把 `/usr`（nflasha15）**整分区读两遍并比对**、存到电脑，再改；
    失败/回退只按精确状态逐字节撤销；文件身份校验（inode/mode/uid/gid/blocks）。
  - 他们的风险提示与我们的结论一致：**没有独立硬件救砖通道**，
    备份不保证救砖；只在服务模式可用时能修。
  - 对我们有用：如果不想改 bootin.elf，可以改用这个已验证的
    `startBTCore.sh` 入口来启动 `/setting` 里的组件（但要接受它同样在 `/usr`）。
- `a7m3-connect-shell/`（Derek2011cc）：PMCA + `usbipd-win` 的 shell 工具，
  功能和我们现有脚本重叠；价值是用 usbipd 直通替代 Zadig 驱动。
- 我们自己的进展：shim 里加了环门铃总数日志（每 256 次），
  `view/BASE_MENU` 调用已实测（崩溃于 NULL+0x10，见上）。

#### 2026-11-02 QEMU 大推进：target=qemu + 真实内存图 + LIRO 窗口 + sen.elf

本轮全部在 QEMU 镜像副本上完成，未接触真机、未写任何分区。四步依次解开了
上一轮的三个阻塞，最终 App 稳定运行 120 s、无异常、无 Senser 重试刷屏；
剩下的阻塞是 **view manager / GraphicsEngine 仍未被 App 创建**。

**1) bootin 的 `sim` → ` target=qemu`（上一轮建议的验证）**

- `target=qemu` 不在 `im.elf`，在 `/usr/bin/bootin.elf`（`0xa299` 的
  `" target=qemu "` 和 `0xa28a` 的 `" target=resub "`）。
- bootin 参数表：`sim` 置 flags bit2（`0x9356: orr r3,r3,#4`）；构建 im.elf
  命令行时（`0x972c..0x973a`）若 `check(0x90d4) != 0x10000` 且 bit2 置位，
  就追加 `" target=qemu "`。
- 补丁：`repack-usr.sh` 里新增一行等长替换
  `0x1730: tst.w r2,#4 (12 f0 04 0f) -> movs r2,#1; nop (01 22 00 bf)`，
  让 bootin 永远走 target=qemu 分支。文件偏移 = vaddr - 0x8000。
- 效果：im.elf 的 `target=qemu` 分支置内部 flags bit 0x1000
  （`im.elf 0xa6f0`：`ldr r3,=0x14edc; ldr r2,[r3,#0x9c]; orr r2,#0x1000`）。
  首次出现 `arch_phys_to_noncache` 报错，说明 App 开始触碰 LIRO/DDR1 路径。

**2) 真实主内核内存图（nflasha2:/kemco.txt）**

- 之前的 `memrsv=0x20000000@0x20000000:device` 只是简化模型；主内核真实参数
  在 `nflasha2:/kemco.txt`（1901 B，FAT12），内存部分：
  ```
  mem=1G@0
  memrsv=1M@0
  memrsv=0x8000@0xF00000
  memrsv=0x1A5E000@0x02E68000
  memrsv=0xEEEB000@0x09915000
  memrsv=632M@0x18800000:uc
  mem=256M@0x80000000@1
  memrsv=14M@0x80000000:uc
  memrsv=242M@0x80e00000:uc
  ```
  （A7M3 的对应值是 0xA6EB000/704M@0x14000000；其余相同。）
- 新脚本 `analysis/qemu_a7r3/patch-bionz-ddr1.py`：
  - 增加 DDR1 `0x80000000 + 0x10000000`；
  - ATAGS 增加第二个 `ATAG_MEM`；
  - `CXD90045_CMDLINE` 换成上面的真实 `memrsv`，保留 `klog.addr=0x1F000000`
    等（`0x1F000000` 落在 632M uc 区内，不再单列 memrsv）。
- 实测：`arm_add_memory: 1 0x80000000 0x10000000`、
  `Memory: 1024MB 256MB = 1280MB total`、`arch_phys_to_noncache` 报错 **0**。
- 注意：这个模型只有约 102 MiB 普通内存，和主内核参数一致，不是 bug。

**3) LIRO 协处理器窗口（A7M3 方案移植）**

- `av-cam.bin` 的 `entry_pre/entry_main` 能执行，但之后在
  `0x63a1d2xx` 忙等 `0xf2a1102c` bit1；QEMU 未建模 `0xf2a1xxxx`，
  RT 线程会抢死整个 Linux（串口静默、shell 不响应）。
- 新脚本 `analysis/qemu_a7r3/patch-bionz-liro.py`（照搬
  `a7m3-firmware-qemu/ui-rendering/qemu/bionz-orig.c`）：
  - `0xf2a00000` + `0x23000` 寄存器窗口；
  - 写保存、读回；未写过的状态寄存器 `+0x004/+0x02c` 在
    `A7R3_LIRO_AUTOREADY=1` 时返回全 1；
  - `+0x030` 写 1 自清；可选 `A7R3_LIRO_TRACE=1` 打印 PC/LR/offset。
- 运行 QEMU 时必须带 `A7R3_LIRO_AUTOREADY=1`（脚本里也可用旧的
  `A7M3_LIRO_AUTOREADY`，两个都认）。
- 效果：`entry_main` 正常返回，App 继续启动。

**4) sen.elf 与 /setting/sen/smode（本轮最大变化）**

- 即使 LIRO 正常，App 仍以约 1000 行/秒刷：
  `failed to wait signal from sugilite device`、
  `failed to get ioctl hw issuspended value from sugilite device`、
  `mlock gain/release failed`（实测 3 分钟 6 万条）。
  GDB 中断显示 CPU 在 PLT/串口写路径，属于日志把 CPU/串口打满，不是死锁。
- 进程表（shim 新增 `probe_procs`，遍历 `/proc/<pid>/comm`）里**没有
  `sen.elf`**。init 的 `check_sen_mode`（`/sbin/init 0x9050`）逻辑：
  ```
  open("/setting/sen/smode"); read <= 0x100
  strncmp(buf,"UU",2)==0 && *(u32*)0x144e4==0  => *(u32*)0x144f4 = 1
  之后 0x9b5a: 若 0x144f4 != 0 才启动 /usr/bin/sen.elf
  ```
  真机 `/setting`（nflasha2）里**没有** `sen/smode`，我们的合成镜像也没有。
- 处理：在 `service-dump/nflasha2.bin` 里加 `/sen/smode`，内容 `"UU\n"`
  （`mmd` + `mcopy`，备份 `nflasha2.stock.bin`）。然后
  `build-a7r3-emmc.sh` 重建镜像即可，不需要重打包 `/usr`。
- 效果（40 s 内）：
  - `PROC pid=120/127/128 c=sen.elf` 出现；
  - 60k 条 Senser/sugilite 重试日志 **全部消失**；
  - im.elf 继续跑，GPU doorbell 持续增长（120 s 约 5 万次）；
  - 无 `Exception Syndrome`，无崩溃。

**5) 当前 App 状态（2026-11-02）**

- shim 探针每 6 s 输出：
  ```
  VMGR base=<libobj> got=<base+0x129ee24> mgr=0 pvm=<0|小整数>
       vu=0 grm=0 uxe=<0|3> dds=0
  VMGR GraphicsEngine=0
  VMGR openView skipped (mgr=0)
  ```
  - `vu/grm/uxe/dds` 分别是 `viewUnified`、`libgrm`、`libuxengine`、`libDDS`
    是否在 maps 里；只有 `libuxengine` 偶现。
  - `GraphicsEngine::sys_getInstance()` 始终为 0；view manager 未创建。
- 正确纠正上一轮的地址理解：
  - `pViewManager` 符号在 `0x12d4c04`，但它是 .bss 目标/重定位目标，
    不是 `openView` 实际读取的全局。
  - `openView`（`0x3e0cd0`）在“模型指针”为 0 时走创建路径：
    `r4 = base+0x121a3c0`（GOT 基址）；`r3 = [r4 + 0x48ac]`
    = `*(base+0x121ec6c)` = `base+0x129ee1c`；`r0 = *(base+0x129ee1c)`
    再调 `0x8307a8`（`ldr r0,[r0,#0x10]`）。
  - 运行时 `*(base+0x129ee1c)` 为 0 或小整数（0x4~0x9c），不是堆指针；
    因此 openView 会解引用 `NULL+0x10`。**必须等 App 创建 manager 后再调**。
  - shim 已把 openView 门槛改成 `pvm && ge`，不会再把 App 打崩。
- 线程状态（新增 `/proc/self/task/<tid>/wchan`）：
  绝大多数 `futex_wait_queue_me`，50 个 `osal_rcv_msg_tmo`，
  16 个 `__k_4u_wait_cb`，少量 `poll_schedule_timeout` /
  `utimer_res_sleep` / `hrtimer_nanosleep` / `sleep_on_page`。
  即 App 在等事件/消息，不是死循环。
- App 仍能响应串口 shell；`/dev/input/event0..2` 存在。
- 300 s 长跑（2026-11-02）进一步确认：45 次探针全部 `GraphicsEngine=0`，
  `pvm` 始终是 0/小整数，`viewUnified/libgrm/libDDS` 从未加载
  （`libuxengine` 偶现）；GPU doorbell 约 135k 次；无异常。
  所以“UI 没出来”不是初始化慢，而是 App 从未走到创建 view manager 的路径。

**6) 下一步（按优先级）**

1. **触发 UI 初始化**：App 已经不再缺 LIRO/Senser 服务，缺的是“进入 UI/菜单”
   的事件。候选：
   - 输入事件：libObj 导出 `snd_InfraInputEvent_to_InputServant`
     （`0x3c00b0`，符号带 Thumb 位），接收 24 字节 `_InfraInputEvent`，
     发到 queue `0x14a0429`。需要确定 24 字节结构（type/keycode/state/…）
     和 MENU 键码；可从 `InputService::callback_convert`（`0x3c31b0`）、
     `callback_panel`（`0x3c2eb1`）、`_InfraInputKeyList` 反推。
   - 或调用 `InputService::pushFilterEvent(Event*)`（`0x3c1ac5`），
     需要 `InputService::m_pMyInstance`（`0x129ebf8`）非 0 和 `Event` 结构。
2. **触发/创建 view manager**：找到写 `base+0x129ee1c` 的初始化函数
   （`new ViewManager` 后写入），或找到 App 的 view/system 初始化入口；
   `GraphicsEngine` 很可能在同一条路径里创建。成功后 `openView("view/BASE_MENU",0)`
   才有意义。
3. 之后再按 A7M3 `ui-rendering` 移植 Sugilite 命令捕获/软件重放
   （A7R3 的 offsets 不同，命令字表 `(op<<16)|0xf000` 相同）。

**本轮新增/修改的产物**

- QEMU 模型：`analysis/qemu_a7r3/patch-bionz-ddr1.py`、
  `patch-bionz-liro.py`；`bionz.c` 备份 `bionz.c.bak-ddr1` / `.bak-liro`。
- 探针/脚本：`probe-console.py`、`probe-ps.py`、`probe-hang.sh`、
  `probe-gpu-msg.sh`、`probe-printf-caller.sh`、`probe-mem.sh`、
  `probe-shell.py`、`summarize-log.py`。
- payload：`analysis/payloads/gpu_shim_qemu.c` 新增
  `maps_has`、`probe_procs`、wchan、`system/execve` 记录、
  `pvm` 探针、maps offset 校正、openView 门槛 `pvm && ge`。
- 构建：shim 仍用 Zig 的
  `-nostdlib -ffreestanding -fno-builtin -fno-exceptions
  -fno-unwind-tables -fno-asynchronous-unwind-tables`；本轮 13,152 B。
- 镜像：合成 `nflasha2` 增加 `/sen/smode=UU\n`；`/usr` 由
  `repack-usr.sh` 重建（含 bootin target=qemu 补丁）。
- 运行命令：`A7R3_LIRO_AUTOREADY=1 bash analysis/qemu_a7r3/run-app.sh 120 <log>`。

**不要再犯**

- 不要再把 `pViewManager`（`0x12d4c04`）当成 openView 的运行时单例；
  实际路径是 `0x121ec6c → base+0x129ee1c`。
- 不要在 `pvm` 为 0/小整数时调用 `openView`，会固定崩在 `NULL+0x10`。
- 不要用 `probe-gpu-msg.sh` 的 `rwatch` 去抓“日志读取者”；
  第一次命中会是 libc 的 `strlen`（因为 App 用 printf），不是业务代码。
- `/setting/sen/smode` 必须在 init 启动 bootin 之前可见；
  加文件后只重建 eMMC 镜像即可，不需要改 `/usr`。

#### 2026-11-02 续：真实 Sugilite 驱动 + QEMU 模型打通，GraphicsEngine 已创建

本轮把 GPU 路径从“用户态假 `/dev/dmpgles2`”切换到 **stock `grm_gles.ko`
+ QEMU Sugilite 模型**，第一次让 App 自己把 `GraphicsEngine` 建起来。

**1) `grm_gles.ko` 的时钟 BUG 绕过**

- 驱动事实（离线反汇编）：BAR `0xf1000000`、大小 `0x4000`、
  `request_threaded_irq(76, ...)`；rootfs 里本来就有 `/dev/dmpgles2` 节点。
- `sugilite_clock_up_internal` 结尾读 `0xf3400080`，`cmp r0,#0x15`，
  不相等就 `BUG()`（`sugi_hw.c:221`）。
- 补丁（file offset，`.text` 起始 0x34）：
  - `0x310`: `15 00 50 e3` (`cmp r0,#0x15`) → `00 00 b0 e3` (`movs r0,#0`)
  - `0x314`: `00 a8 9d 08` (`ldmeq sp,{fp,sp,pc}`) → `00 a8 9d e8`
    （无条件 `ldm`）
  - **坑**：只写 `mov r0,#0` 不更新 Z 标志，`ldmeq` 不成立，仍会落到 BUG；
    必须用 `movs` 或把返回改无条件。
- 模块以 `/usr/kmod/grm_gles.zz` 安装（initrd 的模块字符串本来就是 `.zz`），
  不需要改 initrd。

**2) QEMU Sugilite 模型**

- 新脚本 `analysis/qemu_a7r3/patch-bionz-sugi.py`，照搬 A7M3
  `ui-rendering/qemu/bionz-orig.c` 的 `TYPE_CXD90045_SUGI`：
  BAR `0xf1000000` + `0x4000`、IRQ 76、kick 立即完成并 raise IRQ、
  `+0x34` 返回 `0xc0000001`、ack 清中断。
- 启动日志：`!!! GPE HW: bar remapped 0xf1000000->0xf1000000 size 4000`。

**3) `/dev/mem` 设备 MMIO 映射（内核补丁）**

- App 的 GLES 库会用 `/dev/mem` mmap Sugi/GPIO 寄存器区；
  stock 内核的 `mmap_mem` 对设备 MMIO 返回 `-EINVAL`。
- 正确补丁：`vmlinux.bin` file `0x18DC14`/`0x18DC30` 两个
  `beq` EINVAL 分支 → `0xE1A00000` (NOP)。已加入
  `patch-a7r3-kernel.py`。
- **失败尝试**：先把 `0x174B0` 的函数当成 `range_is_allowed` 改成返回 1，
  实际那是 `phys_mem_access_prot`，改了也没用，已回退。不要再走这条。

**4) shim 切换真实设备**

- `gpu_shim_qemu.c` 对 `/dev/dmpgles2` 先尝试真实 `open`，成功就直接用
  kernel driver；失败才回退 `/dev/zero` 假 fd。
- 节点不存在时用 `mknod major 248/minor 29` 兜底（rootfs 通常已有，rc=-17）。
- 新增 `/dev/mem` mmap 日志（fd/len/off/ret），用于定位寄存器映射失败。

**5) 结果（120 s 稳定运行）**

- `DMP_2D_Initialize : 0`（之前 -9999）。
- `GPU real /dev/dmpgles2 fd=17/29`；`/dev/mem` off `0xf1000000` 映射成功。
- `GraphicsEngine = 0x9d0e0`（非 0，稳定）；`viewUnified2.so` 已映射（`vu=1`）。
- 无 Senser 刷屏、无 `Exception Syndrome`。
- 但 `pViewManager` / `pvm`（`base+0x129ee1c`）仍为 0；
  `InputService::m_pMyInstance`（`base+0x129ebf8`）也为 0；
  `libgrm/libDDS` 未加载，`libuxengine` 偶现。
- `SetEventKeyList` 未命中；`SendEventInitData` 命中但 `0x84b4b0`
  返回 NULL（键表为空）。即输入/视图单例还没创建。

**6) 下一步**

1. 创建/触发 `InputService` 单例：找 `m_pMyInstance` 的写入者，
   或调用导出 `InputService::activate(bool,uint,ulong)`（`0x3c441d`）/
   `createEvent`（`0x3c031f`）/`pushFilterEvent`（`0x3c1ac5`）。
2. 创建 `ViewManager`：`pViewManager`（`0x12d4c04`）和
   `pvm`（`0x129ee1c`）都还是 0；找到 `new ViewManager` 的写入者，
   或先让 App 进入 UI/菜单状态。
3. 两者任一成功后，再调 `openView("view/BASE_MENU", 0)`；
   随后按 A7M3 `ui-rendering` 做 Sugilite 命令捕获/软渲染。

**本轮新增/修改**

- `analysis/qemu_a7r3/patch-bionz-sugi.py`
- `analysis/qemu_a7r3/patch-a7r3-kernel.py`（新增 mmap_mem 两处 NOP）
- `analysis/qemu_a7r3/repack-usr.sh`（grm_gles.zz 时钟绕过）
- `analysis/payloads/gpu_shim_qemu.c`（真实设备优先、节点兜底、mmap 日志）
- 运行时必须仍带 `A7R3_LIRO_AUTOREADY=1`。

**7) 负结果：单例写入观察**

- 对 `InputService::m_pMyInstance`（`base+0x129ebf8`）、
  `pvm`（`base+0x129ee1c`）、`pViewManager`（`base+0x12d4c04`）
  下写观察点，70 s 内**从未命中**。
- 结合 120 s 轮询值全为 0，结论：App 在当前 QEMU 环境里**不会创建**
  输入/视图单例；GPU 已经不影响这一步，下一步的触发点更可能是
  display/panel/sensor 的 ready 事件，或需要直接从 shim/插件调用
  `InputService`/`ViewManager` 的构造/activate。

**8) UI kick 实验（`/tool/ui_kick`）**

- shim 在第 5 轮（约 30 s）检测到 `/usr/tool/ui_kick` 后调用：
  `InputService::activate(true, 0, 0)`（`libObj+0x3c441d`）。
- 结果：`UIKICK before input=0x0` → `after activate input=0x0`，
  `pvm=0`；无异常、无 abort。`activate` 内部 `0x3c0144` 的前置检查
  没通过，所以直接返回。
- `0x3c0144` 检查四个 stripped BSS 单例（GOT 槽 → BSS）：
  `0x129ead0`、`0x129eba0`、`0x129eb10`、`0x129ead8`。
  运行时（约 14 s）四个值全部为 0，`m_pMyInstance` 也为 0。
- 即模型/视图/输入/显示这层的单例一个都没创建；GPU 已经是就绪状态。
- 实验开关：`repack-usr.sh` 会写 `/tool/ui_kick`；删掉该文件重打包即可
  关闭 kick，恢复纯稳定镜像。

**9) uinput 注入 MENU 键（`/tool/uikey`）**

- `/dev/uinput` 存在（10,223），`/dev/input/event0..2` 存在。
- shim 在第 6 轮创建虚拟输入设备并发送 `KEY_MENU`（139）：
  - `struct uinput_user_dev` 在 32 位上大小是 **1116**（80 name + 8 id
    + 4 ff + 4×64×4 abs），不是 1112；写 1112 会 `-EINVAL`，
    `UI_DEV_CREATE` 也会 `-EINVAL`。
  - 修正后：`input: a7r3kbd as /devices/virtual/input/input0`，
    `UIKEY ioctl w=1116 ev=0 key=0 create=0`。
- 但按键后 `pvm` 仍为 0，GE/`vu` 不变，无异常。
  说明 uinput 事件没有进入 App 的 InputService（`I-InputRecv` 等线程
  仍在等 UIPC 消息），或者键码/设备匹配不对。
- 仍可尝试：改 `bustype`（BUS_I2C/BUS_HOST/BUS_VIRTUAL）、设备名、
  键码（POWER/ENTER/方向键），或直接看 kernel `input.ko`/`sircs.ko`
  如何把物理按键转成 UIPC 消息，再在驱动层注入。

**11) InputService 前置检查与 `UpdateInputTable`（2026-11-02 深夜）**

- 读观察点证实：App **确实调用** `0x3c426c`（InputService init），
  入口先调 `0x3c0144` 检查四个 BSS 全局：
  `G1=base+0x129ead0`、`G2=base+0x129eba0`、
  `G3=base+0x129eb10`、`G4=base+0x129ead8`。
- `G1` 是输入事件表：`UpdateInputTable`（`0x3c3c35` → `0x3c3a88`）
  按 `0x14*count` 分配并写入 G1，同时把 G3（条目数）清零后按源表累加。
- 从 shim 调 `UpdateInputTable` 后：G1 = `0x113ef8`（非 0），
  但 G2/G3/G4 仍为 0，`0x3c0144` 仍返回 0，`activate` 不创建
  `m_pMyInstance`。
- **失败实验**：把源表计数设为 1、G4 指向 shim 里的全零 0x14 字节
  条目，再调 `UpdateInputTable`。结果 `UpdateInputTable` 后 App
  直接异常（内核 exception monitor 等 uninterruptible 线程超时）。
  **结论：输入表条目会被真实使用，不能造假；必须由模型/键表提供。**
- 因此 UI 实例的阻塞点是：模型/键表/显示层的初始化没有发生，
  不是 GPU，也不是 `UpdateInputTable` 本身。

**12) 显示/VDF 线程状态**

- App 的 `grm_dn_vdf_if`、`AV_DDS`、`I-TpRecv`、`I-InputRecv` 等线程
  都在 `osal_rcv_msg_tmo`（内核 OSAL 消息队列）等待。
- `dmm`/`grm_ma` 模块加载成功，但内核日志没有 panel/VDF ready 事件；
  `libgrm`/`libDDS` 仍未加载。
- 说明 App 在等 **display/panel/VDF（或 sensor）的 ready 消息**；
  这层没有建模，UI/model 初始化就不会继续。

**13) UI 初始化链（纯逆向，2026-11-02）**

- `Model::initialize` 在 `libObj+0x3eba4c`；`Model` 的 vtable 在
  `0x11ee444`，`initialize` 槽是 vtable+0x58 → `0x3eba4d`（Thumb）。
- 它从 `[model+0x10]` 取输入模型，调用 **`InputService::create`**
  `libObj+0x3c4020`：
  - `new(0x18)`；
  - 构造 `0x3c1178`；
  - `m_pMyInstance = obj`（GOT `0x121d894`，即 `libObj+0x129ebf8`）；
  - 调 `0x3c3d24` 从模型取键表，写 G1–G4。
- `0x3c426c` 只是**消费者**：先调 `0x3c0144` 检查 G1–G4，
  全为 0 就直接返回；`activate` 调的就是它。
- QEMU 里对 `m_pMyInstance`/G1–G4 的写观察点从未命中，
  说明 `0x3c4020` 没被调用，`Model::initialize` 没到。
- `0x3eba4c` 在 libObj 内没有直接 BL/数据指针，它是 **vtable 槽**；
  真正缺的是 **Model/FSM 的启动事件**。
- 下一步静态目标：找 FSM 状态表里调用 vtable+0x58 的转移，
  以及驱动它的 UIPC/显示 ready 事件。

**14) zve10 菜单 PNG 实现的可用点（2026-11-02）**

- 真正的菜单实现只在 **`chocotan/zve10-qemu` 的 `main`**；
  `cokeenet/zve10-qemu` 是旧研究备份，本地 `zve10-qemu` 也是旧 clone。
- 它不是 GPU 模拟，是 **CPU 软件栅格化 + 原生帧缓冲 + QMP 抓帧**：
  - `source/ui/ui-observe.c` + `ui-raster.h`/`ui-present.h`/`ui-gemini-*`；
  - hook `drawFramebuffer`/`notifyDraw`/DMP fill/blit/present/engine_wait/
    Gemini/RM/LZ77/font/text；
  - 输出 640×480 RGBA4444，`scripts/capture-menu.py` 用 QMP `pmemsave`
    读物理帧并转 PNG。
- `scripts/prepare-ui-profile.py` 的 UI profile：
  - 屏蔽 10 个 USB/电源/crypt/tethering 服务 **和 `/usr/kmod/liro.ko`**；
  - 只清 **qemu profile 的 bit1**（`mask & ~2`），保留
    `appFw.so`/`libgrm.so`/`libuxengine.so`。
- A7R3 `libObj` 里能命中同样的函数 prologue：
  - DMP fill/blit：`0x76c580/0x76c610/0x76c750/0x76c820`
  - DMP init/exit：`0x76c540/0x76c560`
  - engine_wait：`0x718fc0`
  - RM execute/scheduler：`0x719100/0x71a970`
  - Application_System_Init：`0x82f1c4`
  - drawFramebuffer 候选：`0x5cef88` / `0x71f080`
- 实验：A7R3 IMDB 补丁改为只清 bit1，并额外屏蔽 `/usr/kmod/liro.ko`
  （7 服务 + liro）。结果：无异常，但 `pvm=0`、`grm=0`、`dds=0`、
  `m_pMyInstance=0`。⇒ UI profile 本身不解锁 A7R3 的 Model/FSM 初始化。
- 下一步：把 `ui-observe.c` 的观测 hook 表移植到 A7R3（先只记录，
  不改行为），确认 App 到底有没有走到 DMP/present；再决定软件 GPE
  和强制菜单入口。

**15) 成功创建 ViewBaseMenu UI 实例（2026-11-03）**

- 逆向：`viewUnified2` 的 `ViewBaseMenuToInstance` 在 `+0x1fc43b`；
  它 `new(0x12c)`，调用 `+0x1fc318` 构造，`r1` 是 manager。
- `ViewBase::ViewBase(ViewManager*)`（libObj `0x3dbbe9`）**完全不引用
  manager 参数**；`ViewBaseMenu` 构造也不依赖它。
- shim 里新增 `view_kick`（`/usr/tool/view_kick` 控制）：
  `module_base("viewUnified2.so")`，调用 `fn(0, 0)`（NULL manager）。
- 实测（`/root/a7r3-viewkick2.log`）：
  ```
  VIEWKICK base=0x13531000
  VIEWKICK obj=0x1390c0
  VIEWKICK field0x13d93368   ; = base + 0x862368
  VIEWKICK field0x13d934c4   ; = base + 0x8624c4
  ```
  vtable `0x862368` 的 typeinfo 指针 `0x8624d0`，名称指针 `0x62eab4`，
  字符串 **`12ViewBaseMenu`**；App 无异常。
- ⇒ 通过原厂 `ViewBaseMenuToInstance` 成功创建了一个 **`ViewBaseMenu`
  UI view 实例**（不是伪造画面）。
- 限制：`pvm`/`ViewManager` 仍为 0，该实例是独立的堆对象，未挂到
  App 的 ViewManager，也未渲染。
- 复现：`/usr/tool/view_kick` 存在时，第 5 轮（约 30 s）调用；
  `bash repack-usr.sh` + `A7R3_LIRO_AUTOREADY=1 bash run-app.sh 40`。
- 附带修正：A7R3 `bootin` 的 `dmode` 是 **debug mode**，`amode` 是
  `adj`/`usbj`，不是 zve10 的 UI 参数；`/bin/top` 必须是 `3p`，
  设成 `0` 会让 init 挂载失败。

**10) 关键发现：`input.ko` 不走 Linux input 核心**

- `analysis/extracted/nflasha15_x/input.ko`（33,768 B）是 Sony 自己的
  按键扫描器：GPIO/ADC/VGRIP/front 键 → **OSAL UIPC 消息**，
  不是 `/dev/input/event*`。所以 uinput 注入不会到 App。
- 关键字符串：`FR_INPUT_MSG size:%d`、`PwrKey1..5`、`CAkey1..4`、
  `VGRIP_INPUT_KEY_MSG`、`VGRIP_KEY1`、`osal_snd_sync_msg`。
- 发送函数（`input.ko` .text 0x0 起，ARM）：
  - 消息首字 `0xfff1`（类型），`+4` 状态字节，`+6` 起是 16-bit 键数据；
  - 调用 `osal_snd_sync_msg(0x720018, msg)`。
- 下一步（真正能注入的路径）：
  1. 从 `input.ko` 的 `getkey_front` / `FR_INPUT_MSG size` 交叉引用，
     解出 `FR_INPUT_MSG`/`VGRIP_INPUT_KEY_MSG` 的完整字段和 MENU 键位；
  2. 在 shim 里用 userspace `osal_snd_sync_msg` 向同一队列发消息
     （或模拟 GPIO/ADC 电平，让 input.ko 自己发）；
  3. 成功后再看 `I-InputRecv` 是否唤醒、`InputService`/ViewManager
     是否创建。

  **16) LIRO/Sugilite 绘制引擎推进（2026-11-07/08）**

  > 本轮目标：让真实 LIRO/Sugilite 绘制路径在 QEMU 里执行。以下全部为 QEMU +
  > 镜像副本实验，未改真机。详细日志见 `analysis/a7r3-qemu-display-producer.log`。

  - **liro.ko 之前 `-ENOENT` 不是符号问题**：模块参数为
  `fn/adr/load/debug/delay/resume_cpu/liro_resume_async`，init 里用
  `filp_open(fn)` 加载固件；不传参数时 `fn=""` → ENOENT。
  - **内核上下文执行（A7M3 方式）可行**：
  - QEMU 预加载：`-device loader,file=av-cam.bin,addr=0x035c6000,force-raw=on`
  - shim 用 `init_module(128)` 传参：
   `fn=/tool/av-cam.bin adr=0x035c6000 load=0 debug=1 liro_resume_async=1 resume_cpu=1`
  - 结果（`A7R3_LIRO_AUTOREADY=1`）：
   ```
   resume      - 635c6078
   start entry_pre(635c6044)  end entry_pre
   start entry_main(635c6058) end entry_main
   initcall init_module ... returned 0
   ```
   应用继续运行（15–16 次 DRAW）。
  - **固件入口表（ORIL 头，基址 0x635C6000）**：
  ```
  +0x0C entry_pre     = 0x635C6044 (ARM)
  +0x10 entry_main    = 0x635C6058
  +0x14 data block    = 0x635C60B4（模块拷贝 20 字节到上下文）
  +0x18 entry_suspend = 0x635C6068
  +0x1C entry_resume  = 0x635C6078
  ```
  - **任务集**：entry_main 创建 **134 个 `liro-kliro_N` 内核线程**
  （75×`tsk_thread_helper`、51×`osal_rcv_msg_tmo`、3×`utimer_res_sleep`、
  3×`tslp_tsk`、1×`twai_flg`、1×`osal_wai_sem_tmo`），列表见
  `analysis/a7r3-liro-threads.log`。
  - **/proc 接口**：模块创建 `/proc/liro/`，唯一节点是 **`susres`**
  （`/proc/susres`、`/proc/liro/start` 不存在）。写 1 字节触发
  `[LIRO]: entry_suspend:635c6068(0)`，随后写线程阻塞在 **`ter_tsk`**
  （等待某 LIRO 任务终止；有任务停在 `twai_flg`/`osal_wai_sem_tmo`），
  resume 链因此走不到。要推进需给这些 OSAL 等待提供事件（CP/硬件侧）。
  - **第二核心尝试（QEMU per-CPU）**：`max_cpus=2` + `-smp 2`，把 av-cam.bin
  加载到 0x635C6000、PC=0x635C6044、ARM 模式；发现并修复 VFP 陷阱：
  需写 `cp15.cpacr_el1=0x00F00000`、`vfp.xregs[8]=0x40000000` 后调用
  `arm_rebuild_hflags()`（否则 `vpush` 触发 undef）。裸核心最终会跳入低内存，
  因为它缺少 liro.ko 提供的内核上下文；这不是“缺低内存镜像”。
  补丁脚本：`analysis/qemu_a7r3/patch-bionz-liro-cpu.py`。
  - **Sugilite 硬件 ABI 已确认**（libObj+0x718D90 分发器 / 0x718D50 写寄存器）：
  ```
  0xC00 <- desc[0]  >> 2   (源)      0xC04 <- desc[4] >> 2 (目的)
  0xC10 <- desc[0x14] (格式 0x4401)  0xC20/0xC24/0xC28 <- desc[0x2c/0x30/0x34]
  0xC18 <- 1 (kick，当 desc[0x28]==0)
  ```
  实测 blit：`C00=0x080B8080`(源 0x202E0200)、`C04=0x08EA0000`(面板 0x23A80000)、
  `C08=0x01E00280`(640×480)、`C10=0x00004401`。
  - **应用提交路径**：`0x1804=命令缓冲 PA`、`0x1800=字数|0x4(kick)`（与 A7M3 一致）。
  A7R3 应用只发出 **11 次 kick**（0x2053C270..0x2053C3B0），每个缓冲仅 4 条：
  ```
  0116F000 02053BB0   (工作表面 = 0x2053BB00，注意不是 0x2053BB0)
  0117F000 00000001
  0044F000 00000000
  0010F000 12345678
  ```
  **没有 0x0202 draw**；真实绘制流走 `grm_gles` ioctl
  （`0xc0048211` 的 8 字节 job 缓冲，见 `analysis/a7r3-gpu-payloads.log`，
  另有 `analysis/a7r3-gpu-ring.bin`、`a7r3-sugi-bartrace.log`）。
  - **A7M3 参考实现已移植**（`a7m3-firmware-qemu/ui-rendering/qemu/bionz-2026-11-01.c`）：
  把 `SugiSoftState`/`soft_draw`（RGBA4444 + source-over + 居中缩放）/
  `soft_exec`（0x0112/0x0589/0x0586/0x0587/0x0049/0x0047/0x0048/0x0105/0x0202）/
  `capture_cmd` 接入我们的 `bionz.c`，钩子挂在 0x1804/0x1800&4；
  开关 `A7R3_SUGI_EXEC / CENTERSCALE / SURF_DIR / TEX_DIR`。
  由于 A7R3 应用不发 0x0202，移植后暂无帧。
  - **openView 符号（A7R3 libObj，动态符号表可直接取）**：
  ```
  viewManagerIf::openView(char const*, int)  = 0x003E0DA9 (Thumb)
  ViewBase::openView(char const*, int, bool) = 0x003DC275
  ViewBase::openViewWithoutUnsetMute(...)    = 0x003DC199
  ```
  从 shim 的探测线程调用会阻塞不返回；需在应用自己的线程/注入插件里调用
  （A7M3 的做法），才能触发真正的绘制命令提交。
  - **兜底生产者（已实现、可复现）**：shim 在 `/usr/tool/produce` 存在时，
  通过 `/dev/mem` 写 0x202E0200（640×480 渐变 + 文字 + 帧号/进度条），
  每约 6 s 刷新；Sugilite blit 把它送到 0x23A80000。
  证据：`analysis/qemu_a7r3/producer_t2.png`、
  `analysis/qemu_a7r3/capture_producer.py`、`capture_diff.py`、`capture_frames.py`。
  - **复现命令（当前状态）**：
  ```sh
  cd /root/firmware/a7r3
  A7R3_LIRO_AUTOREADY=1 \
  A7R3_SUGI_EXEC=1 A7R3_SUGI_CENTERSCALE=1 \
  A7R3_SURF_DIR=/tmp/a7r3sf A7R3_TEX_DIR=/tmp/a7r3tex \
  qemu-system-arm -M cxd90045 -smp 2 \
   -kernel vmlinux-console.bin -initrd initrd-qemu.img \
   -drive if=mtd,file=qemu-emmc-2g.img,format=raw \
   -device loader,file=/tmp/av-cam.bin,addr=0x035c6000,force-raw=on \
   -serial file:/tmp/run.log -display none -monitor none
  ```
  - **当前阻塞**：A7R3 应用不发 Sugilite draw 命令；需要从应用自己的线程调用
  `openView`（或注入插件）触发视图绘制，或者解码 `grm_gles` job 格式。

## 17) QEMU 真实 draw 缓冲区解码与块级渲染（2026-11-08 / 09）

> 全部在 QEMU + 镜像副本完成，未接触真机。详细日志：
> `analysis/a7r3-qemu-display-producer.log`（末段）；帧 `analysis/qemu_a7r3/unit_frame.png`。

### 关键进展：从应用线程调 openView 成功

- 上一节"应用不发 Sugilite draw"的判断已被推翻。把 `open_view_once()` 放进
  shim 的 `hook_draw`（第 8 次 draw 时调用）后：
  ```
  OPENVIEW once r=1342728    (返回，不再阻塞)
  ```
  应用继续绘制，Sugilite kick 从 **11 → 25**；新增 14 个缓冲区，
  `0x1804` 依次为 `0x2053EA50/0x2053EF20/0x2053F3F0...`，间隔 **0x4D0**，
  `0x1800 = 0x9A`（**154 字**）。旧的 capture 门限 `0x1800 & 4` 会漏掉它们，
  已改为"任意非零 0x1800 写入"就抓。

### 154 字 draw 缓冲区结构（6 个重复的 24 命令单元）

每个单元（op:value，op 以 `(op<<16)|0xf000` 出现）：

```
0042:43a00000 0043:43700000   半尺寸 320.0 / 240.0
0047:010601ba 0048:012601ca   矩形（中心坐标形式）
0049:01e00280                 640x480 面积
010b/010d/010e:0  010c:0000000f  0110:00040000
0112:0202e020                目标表面 PA = 0x202E0200
0113:01e00280
0450..0454                   两组 uniform（多数 = 1.0,1.0,1.0,1.0 白色）
0216:2053b250                常量/uniform 块指针
00000281:2053bba0            非 0xf000 命令 + 指针（单元结束）
```

- `0x00000281` 后的 `0x2053BBA0` 指向一个**状态块**，不是顶点/索引/纹理表：
  `0041:2, 004a:0, 0100:0x100, 0101:0x01010000`，每个 draw 都相同。
- 运行时缓冲区里**没有 0x0202 draw，也没有 0x0589 纹理绑定**。
- libObj 里的静态模板在文件 **0x122A538**（对应 A7M3 的 0x1285AA4），
  含完整 opcode（0041/0042/0043/0047/0048/0049/0100..0115/0203..0216/
  0400..0473/0580..0589/05dd/0458+0459 属性流），尾部 0x122AB44 才是真正的
  draw 块：`0112/0111/0202:10000004/0116:08000000/0117:1/0044:0/0010:12345678`。
- 运行时单元是模板的紧凑子集；被按 draw 改写的字段：
  `0042/0043`（半尺寸）、`0047/0048`（rect）、`0049`（面积）、`0110`、
  `0112`（目标 0x202E0200）、`0113`、`0450..0454`（uniform）、`0216`（常量块）。
- 常量块 `0x2053B250` 前 64 B：
  `010001ff 0100ffff 01000101 0100ff01 / 0460f000 0000000f / 0461f000 3c1c84e4 ...`
  即打包的 shader 常量（含浮点），前 64 B 内没有纹理指针。

### 块级渲染（bionz.c）

- `soft_exec` 现在把 `0x00000281` 当作一次 draw：
  `cxd90045_sugi_unit_draw()` 用 `pixel=(v+W)/2` 还原 rect，用
  `0x0451..0x0454` 的 uniform 取颜色，填充目标面 `0x202E0200`，
  并把帧写到 `A7R3_SURF_DIR`（本次 `/tmp/a7r3sf`）。
- 一次运行产生 **84 次 unit draw（14 缓冲 × 6）**，去重后只有 **14 个矩形**：
  ```
  0,0-640,52        顶部栏
  17,256-623,304    选择条
  0,354-640,356     分隔线
  0,414-640,480     底部栏 + 按钮块（17/103/189/275/361/447 起）
  0,414-640,480 之外还有 541,371-549,387 等小块
  ```
- 渲染结果（`unit_frame.png`）已有应用的**布局结构**：顶栏、橙色端点的选择条、
  分隔线、底部按钮区。**但没有文字、没有图标**，全部是纯色块。
  说明几何/坐标/颜色链已正确，缺的是文字与纹理。

### 新发现：TextRM 断言失败，很可能是"没有文字"的根因

- 最近几次 QEMU 串口里反复出现 shim 的 `ERRHOOK=0x24`，其调用栈落在
  libObj **0x623BB4** 的断言处理器（返回地址 `0x623BEF`），该处断言的字符串
  正是 `libObj+0xE59C2E`：
  ```
  TextRM_ErrNone == err <(0x40000000)>
  ```
  文件行号 `0xb5`。也就是说 **TextRM（文字资源管理器）在反复报错/断言失败**。
- 这与"命令流里没有 0x0589 纹理绑定、画面只有纯色块"完全吻合：
  字体/文字资源没加载成功，自然不会有文字纹理提交。
- 待确认：断言里的实际 `err` 值。断言打印函数是 **0x61DC78**
  （r0=文件串、r1=行号、r2=err）。hook 它就能拿到错误码，再顺调用链找
  失败的具体资源/加载点。
- 相关：上一节把 `ERRHOOK` 当成"良性"的结论应修正为**可疑**；
  它可能就是文字缺失的直接原因。

### 下一步（按优先级）

1. **确认 TextRM 错误码**：hook `libObj+0x61DC78`，打印 `(file, line, err)`；
   同时保留 `0x61DB9C` 的调用栈。目标是拿到 `err` 的真实值和触发资源名。
2. **查 TextRM 为什么加载失败**：顺着断言调用链找它要打开的字体/RM 资源，
   确认是挂载缺失、IMDB 资源项没注册，还是 LDEC 解码目标尺寸/格式不符。
   参考：启动时 `LDEC` 有 62 次解码且 `dec=0`（成功），资源框架是通的。
3. **资源正常后抓纹理**：预期命令流里会出现 `0x0589`（纹理 PA）以及
   `0x0047/0x0048` 的 UV；按模板 `0x122A538` 对照，然后给
   `cxd90045_sugi_unit_draw()` 加纹理采样（RGBA4444 + 双线性/最近邻）。
4. **若文字不走 Sugilite**：libObj 内含完整 EGL/DMP GLES（名字表在文件
   `0x122B550` 附近，32 字节步长），可照 zve10 的 DMP/GLES hook 方式
   记录 `glDrawArrays/glBindTexture/glTexImage2D` 的真实调用。
5. 文字可读之后，再回到原始目标：LIRO/sensor 与视频规格
   （`ter_tsk` 挂起恢复、`PARAMID_MOVIE_SIZE/FRAME_RATE` 下发）。

### 本轮产物/复现

- QEMU 模型：`/root/qemu-ma1co/hw/arm/bionz.c`（`cxd90045_sugi_unit_draw`、
  `soft_exec` 的 0x00000281 分支、uniform 捕获）。
- 脚本：`/tmp/pud.py`（打补丁）、`/tmp/du.py`（帧转 PNG）、
  `/tmp/pfl.py`（把 unit 扫描窗口从 96 扩到 296 字节）。
- 运行：`A7R3_LIRO_AUTOREADY=1 A7R3_SUGI_EXEC=1 A7R3_SUGI_CENTERSCALE=1
  A7R3_SURF_DIR=/tmp/a7r3sf bash run-app.sh 40`（镜像由 `repack-usr.sh` 重建）。
- 注意：`openView` 只在 shim 的 `hook_draw` 里调用有效，从探测线程调用会阻塞。

## 18) 菜单文字攻关：字体缺失 + 构建链修复（2026-11-09）

> 目标："显示完整菜单"。全部在 QEMU + 镜像副本完成，未接触真机。
> 关键结论：菜单布局已经完整、更多行都能出来；**文字仍不显示**，原因是
> ①固件里没有中文字体（所有分区都没有），②文字不走我们已实现的 Sugilite
> 2D 填充路径。

### 18.1 构建链：zig 换成系统 clang/LLD，并修掉 `__sync_synchronize`

- 旧的 86,420 B `.so` 的 `.comment` 是 `Ubuntu clang 21.1.8` + `LLD 21.1.8`，
  **不是 zig**。用系统 clang 重建才对得上：

```
clang --target=arm-linux-gnueabi -shared -fPIC -O2 -fuse-ld=lld \
  -nostdlib -ffreestanding -fno-builtin -fno-exceptions \
  -fno-unwind-tables -fno-asynchronous-unwind-tables \
  -o gpu_shim_qemu.so gpu_shim_qemu.c
```

- **坑**：clang 在 `-nostdlib` 下把 `__sync_synchronize()` 变成外部调用，
  而 .so 没有 compiler-rt ⇒ LDEC 解码后
  `im.elf: symbol lookup error: /usr/tool/libefence.so: undefined symbol: __sync_synchronize`，
  App 直接停住。改为 CP15 barrier：

```c
static inline void shim_barrier(void) {
    unsigned long t = 0;
    __asm__ volatile("mcr p15, 0, %0, c7, c10, 5" : : "r"(t) : "memory");
}
```

  （默认 `-march` 太老，`dmb sy` 汇编不过。）
- 新构建的未定义符号集合与旧 `.so` 完全一致（`__aeabi_*`、`memcpy`、
  `dladdr` 等），说明 ABI 对上。
- **稳定性**：新构建有一定概率在启动早期崩（`pc=0` 或
  `page translation fault at 0x152c38`，pid=im.elf）；同一个镜像里换回
  旧 `.so`（备份 `gpu_shim_qemu_prev.so`）就稳定。跑关键实验时先用旧 `.so`。

### 18.2 中文菜单字体：固件里确实没有

- `fontlist.dat`（`/usr/share/app`，360 B）列了 6 个字体，其中：
  ```
  FONT_S_CHINESE  /tmp/images/MYingHeiC-GB18030-SJ_wIcon.ltt
  FONT_T_CHINESE  /tmp/images/MYingHeiC-Big5-SJ_wIcon.ltt
  ```
- 扫描**全部**分区 dump（nflasha1/2/3/4/5/6/7/12/13/15/18/23），
  `.ltt`（`LTT\n7\n2\nFont-linking...`）只出现在 `nflasha15`，且只有 3 个：
  Univers、Heisei（日文）、MYingHeiC（韩文）。**没有简体/繁体中文字体**。
  体是 CN 机型（`CX62001_CN2`），真实机上这两个字体很可能在
  `nflasha10`（`/tmp`，12 MB，从未 dump 过）的 `/images` 里。
- 处理（repack-usr.sh 已固化）：先把日文字体复制成两个中文名并改
  `fontlist.dat` 指向 `/usr/share/app/...`（原指向 `/tmp/images/...`）。
- 运行时验证：shim 新增 `fopen/fopen64` 拦截（字体是用 stdio 打开的，
  `open` 拦截看不到）：
  ```
  FONT fopen /usr/share/app/fontlist.dat
  FONT fopen /usr/share/app/MYingHeiC-GB18030-SJ_wIcon.ltt   (多次)
  ```
  说明字体列表解析和字体加载都真的走到了。

### 18.3 断言从 -16 → -7 → 消失，菜单行数变多

- 新增 libObj `0x61DC78` 钩子（断言打印，`r0=file,r1=line,r2=err`）+ 已有
  `0x61DB9C` 钩子（调用栈），真机跑出来的 TextRM 断言：
  - 字体替换前：`line=0xb5, err=0xfffffff0 (-16)` —— `TextRM_loadFont`
    的字体名查表失败（未注册），与"中文名文件不存在"一致。
  - 字体替换后：`line=0x93, err=0xfffffff9 (-7)` —— 字体已注册，
    但 `TextRM_createText` 里的排版/取字形失败（日文字体没有简体汉字的字形）。
- 用**旧 .so（无新钩子）+ 字体替换**跑 200 s：
  - 无 `Exception Syndrome`，`ASSERT` 0 条；
  - Sugilite kick：11 个 surface 设置 + **21 个 154 字绘制缓冲**
    （之前 14 个），unit draw **126 次**（之前 84 次）；
  - 唯一矩形 21 个（之前 14 个），多出 `27,56-611,252`、
    `27,206-611,252`、`27,306-611,352`、`100,363-619,409` 等列表行，
    说明菜单内容真的变多了。
- 但 21 个缓冲里仍然**没有 `0x0589` 纹理绑定、没有 `0x0202` draw**，
  全是纯色填充；合成面里也没有字形像素（206-252 等行整片纯白）。

### 18.4 新增：blit 时 dump 合成面（看"最终画面"）

- QEMU 模型 `cxd90045_sugi_blit()` 增加开关 `A7R3_BLIT_DUMP=1`：
  每次 0x0C18 blit（把 `0x202E0200` 送到面板 PA）时把合成面 dump 成
  `$A7R3_SURF_DIR/frame_9NNN.raw`（640×480×2，RGBA4444）。
  补丁脚本 `patch-blit-dump.py`，已重编译 `build-arm`。
- 结果图：`analysis/qemu_a7r3/menu_frame.png`（222 色）。
  区域分析（背景 0xFFFF）：
  - `27,206-611,252`、`27,56-611,252`：**纯白，无字形**；
  - 顶栏 `15,6-625,46`：12 色，32×2 的短横线；
  - `17,414-108,480`：118 色，由 91×4 的水平条纹组成；
  - `100,363-619,409`：15 色，34×1/2 的短横线。
  ⇒ 这些是菜单的装饰/占位图形，不是文字。**文字仍未渲染**。
- 旁注：HMP 的 `pmemsave 0x202E0200 0x96000 /tmp/live.raw` 在本机
  QEMU 上会报 `invalid char 't' in expression`；改用 blit dump 更可靠。

### 18.5 判断：文字走 GLES（grm_gles job），不是 Sugilite 2D

- 菜单文字由 `GraphicsEngine`/TextSprite 渲染，提交路径是
  `/dev/dmpgles2` 的 `0xc0048211` 作业缓冲（8 字节条目），QEMU 里的
  `grm_gles` 模型只做寄存器回环，**不做光栅化**；这解释了为什么
  `0x202E0200` 只有 2D 填充、没有文字。
- 现有 `cxd90045_sugi_unit_draw()` 只能画纯色块；要出文字，必须
  解码 GLES 作业（或让文字改走 2D 路径）。

### 18.6 本轮工具/产物

- `analysis/qemu_a7r3/build-test.sh`：WSL clang 构建四种优化级别并报 text 大小。
- `analysis/qemu_a7r3/patch-blit-dump.py`：给 bionz.c 加 blit 时 dump。
- `analysis/qemu_a7r3/raw_to_png.py`、`frame_stats.py`：
  RGBA4444 raw → PNG / 帧统计。
- `analysis/qemu_a7r3/hmp.py`：HMP unix socket 客户端（pmemsave 失败记录）。
- shim 新增：`th_assert`（0x61DC78）、`FONT fopen/fopen64` 日志、
  `shim_barrier`；字体查找钩子（0x52D59C）**已移除**——有非字符串调用者，
  重实现会崩在 `0x152c38`。
- repack-usr.sh 新增字体替换块；日志 `analysis/a7r3-menu-run.log`。

### 18.7 下一步（按优先级）

1. **拿到真实 `nflasha10`（`/tmp`）的 dump**：中文 `.ltt` 大概率在
   `/tmp/images`。拿到后直接替换，无需再改 fontlist；这是让中文菜单
   显示的原厂路径（需要真机/用户配合）。
2. **解码 `grm_gles` 作业队列**：`0xc0048211` 的 8 字节条目 +
   `0xc0048212`，先用现有 `analysis/a7r3-gpu-payloads.log` 建模
   纹理上传/四边形绘制，再在 QEMU 里做 CPU 光栅（RGBA4444）。
3. **折衷：抓字符串自己画**。在 libObj 里找调用 `TextRM_createText`
   的调用点（或 `0x54C58C` 排版函数）做调用点补丁，把字符串/位置
   记录下来，由 shim 用自带 8×8 字体画到合成面——菜单可读，但不是
   原厂字体，只作为验证/导航用途。
4. **切语言到英文**：英文用 `FONT_UNIVERS`（存在）。需要找到
   `/setting` 里的语言字段（可能 `Backup.bin`），或从 App 参数接口
   下发；成功后可暂时避开中文字体问题（但文字仍受 GLES 路径影响）。

## 19) 真机 nflasha10 已取得 + 视图探测 + Gemini 绘制路径（2026-11-09 晚）

### 19.1 nflasha10（/tmp）真机 dump：中文字体到手

- 通过 `analysis/dump_to_sdcard.py --partitions 10` 在 service 模式把
  `/dev/nflasha10`（12,582,912 B）dd 到 SD 卡 Slot 1，`wc -c` 校验后
  `umount`，**未调用 `auth.stop()`**，关机取卡。
- SD 卡插电脑（E:）→ `dumps/nflasha10.bin`，SHA-256
  `19a2fb0225b480c4523e6900f0abb03544da2a3ebd1bc76bd98785747ff3a0ac`。
- 内容确认：
  ```
  /images/MYingHeiC-GB18030-SJ_wIcon.ltt  1,958,432
  /images/MYingHeiC-Big5-SJ_wIcon.ltt     1,150,740
  /images/Sony_DI_Icons.ttf                 545,464
  ```
- 集成：`repack-usr.sh` 现在优先用 `service-dump/nflasha10.real.bin` 作为
  nflasha10；`fontlist.dat` 恢复原厂路径（`/tmp/images/...`），不再用
  日文字体顶替。实测启动时 TextRM 断言归零（真实中文字体能被注册/取字形）。
- 结论：之前“固件没有中文字体”成立，但字体在 `/tmp`（nflasha10），
  不在 `/usr`；真机 dump 流程可用。

### 19.2 视图探测：GLOBAL_MENU / SETTINGMENUX 触发大得多的绘制

- shim 里加了 `open_view_named()`，在应用自己的 draw 线程按 draw 计数
  依次打开：`view/BASE_MENU`(8) → `view/SETTINGMENUX`(24) →
  `view/GLOBAL_MENU`(48) → `view/FNMENU`(72) → `view/HARDKEYMENU_DISP`(96)。
- 实测（`analysis/a7r3-menu-global.log`）：四个 openView 都返回；Sugilite
  提交从 18 → **57 个 154 字缓冲**，并出现 **492 命令**的大缓冲区，
  unit draw 从 108 → **643 次**，矩形 21 → **56 种**：6 列面板
  （212x180、198x120、70x60）、列表行 584x46、以及 **8x16 / 28x32 /
  42x32** 的小矩形（字形/图标单元）。
- 但命令里仍没有 `0x0589`（纹理绑定）、没有 `0x0202` draw，全部纯色填充
  → 文字不走这条 2D 命令流。

### 19.3 新发现：Gemini（GEM）CPU 光栅路径确实在画字

- shim 里已有的 Gemini 钩子（`0x780188/0x77fb88/0x77fe70/0x78030c`）
  在菜单运行中**被调用 510 次**（`GEM kind=1`，`phys=0x202E0200`），
  目标就是合成面；日志里 `dr` 是 `x=0x10,0x30,0x50...` 步进、尺寸
  **16x32** 的字形单元。
- `gem_draw()` 用 `ui_gemini_raster.h` 在 CPU 上做纹理采样，硬编码
  `dst.layout = UI_GEMINI_MORTON_BOTTOM_UP`、stride 1280。
- 说明：**文字很可能就是 Gemini 用 16x32 字形纹理画进 0x202E0200 的**，
  不是 GLES；之前帧里没字，是因为合成面按线性解码/写入，而 GEM 用
  Morton（8x8 tile、bottom-up）布局，读写不一致 → 条纹/棋盘格。

### 19.4 布局问题（当前卡点）

- QEMU 模型加了 `A7R3_SURF_MORTON=1`：unit 填充改为 8x8 tile 的 Morton
  写入；surface dump 时把 Morton 解码回线性；`A7R3_KICK_DUMP=1` 每 5 次
  kick dump 合成面（补 blit 太少的不足）。
- 结果不稳定：带 Morton 的运行多次在启动后崩在
  `page translation fault at 0x152c38`，或只跑出 3 帧全黑。
  **尚未拿到一张“Morton 对齐 + 文字”的完整帧。**
- `A7R3_BLIT_DUMP` 只在 blit 时 dump；菜单绘制后没有 blit 时会错过画面。

### 19.5 下一步

1. 先解决 `0x152c38` 崩溃：用旧 `.so`（`gpu_shim_qemu_prev.so`）+ 不带
   Morton 的组合拿稳定基线，再单独开 Morton 对比；崩溃点像 app 堆上的
   野指针，怀疑 Morton 写入越界到别的 surface（或 492 命令缓冲里非
   0x202E0200 的目标）。给 Morton 分支加“只对 surf==0x202E0200 且
   640x480 生效，否则线性”。
2. 确认合成面布局：同一帧分别按 linear / Morton 解码，与 GEM 的
   16x32 字形位置比对；若能拿到真机菜单截图做参照可直接判定。
3. 布局确定后统一 `gem_draw` 的目标布局与模型，再跑一次
   “GLOBAL_MENU + 492 命令缓冲 + GEM”的长跑，抓一张完整菜单帧。
4. 之后把文字行/图标与真实菜单（`viewSettingMenu.uxc` 等）核对。

### 19.6 追加（同日深夜）：Morton 已由用户确认，GEM 帧抓取已就绪

- 把同一帧按 linear / Morton 两种方式解码后给用户看，用户确认
  **Morton(bottom-up) 那张才像相机菜单** ⇒ 合成面布局就是
  Morton 8x8 tile、bottom-up、stride 1280。
- 因此下一步统一布局即可：QEMU 侧 unit 填充用 Morton（已加
  `A7R3_SURF_MORTON`，并加守卫：只在 surf==0x202E0200 且
  640x480 时生效），shim 的 gem_draw 本来就是 Morton。
- 新增 shim 侧 GEM 帧抓取：每 8 次 Gemini 绘制把合成面
  640x480x2 dump 到 **/log/gemframe_N.raw**（nflasha11，500MB，
  比 /tmp 宽松），开关文件 `/usr/tool/gem_dump`（repack 时
  `GEMDUMP=1` 生成）。已能从 eMMC 镜像的 partition 11
  （offset `524288 + 401408*512`）挂出来。
- 现状：单次运行 GEM 绘制次数波动很大（50 / 510 次），且
  `im.elf` 仍有约一半概率在启动/菜单初始化阶段崩在
  `page translation fault at 0x152c38`（与 Morton 无必然关系：
  不开 Morton 也偶发）。还没拿到"GEM 画完 + 完整菜单"的那一帧。
- 下一轮优先：① 调大 GEM dump 窗口（每 16/32 次、最多 ~30 张）；
  ② 用 240-300 s 长跑 + 连续重试直到 `Exception=0` 且
  GEM>=400 次；③ 把抓到的 /log/gemframe_* 按 Morton 解码后
  与真实菜单核对；④ 稳定后把 unit 填充也切 Morton，得到
  一张完整、布局正确的菜单帧。

### 19.7 完成：QEMU 里显示了完整、正确的菜单（2026-11-09 23:4x）

- 关键组合（一次成功运行，`Exception Syndrome` = 0）：
  ```
  A7R3_KICK_DUMP=1 A7R3_SURF_MORTON=1 A7R3_LIRO_AUTOREADY=1 \
  A7R3_SUGI_EXEC=1 A7R3_SUGI_CENTERSCALE=1 A7R3_BLIT_DUMP=1 \
  A7R3_SURF_DIR=/tmp/kx   (240 s, 不再让 guest 往 /log 写 600KB 帧)
  ```
- 关键点：**unit 填充与 Gemini 绘制都按 Morton(bottom-up) 写**，
  dump 时统一解码回线性；guest 侧不再做 600KB 的 vfat 写
  （那会触发 eMMC 写超时把 QEMU 挂死）。帧由 QEMU 模型在 host 侧
  直接 dump，blit/kick 时各抓一张。
- 用户确认：**blit 帧就是完全正确的（完整）相机菜单**；
  unit 绘制帧只有最上面一条白条差异，其余也正确。
- 证据文件：
  - `analysis/qemu_a7r3/menu_full.png`（= 用户确认的完整菜单帧）
  - `analysis/qemu_a7r3/menu_full.raw`（640x480 RGBA4444，线性解码后）
  - `analysis/qemu_a7r3/menu_morton_blit.png`
  - `analysis/a7r3-menu-full.log`、`a7r3-menu-full.sugi.log`
- 结论：**"在 QEMU 里显示完整菜单"已达成**。后续视频规格实验
  （PARAMID_MOVIE_SIZE/FRAME_RATE、4K60 研究）可以直接在能正常
  显示菜单的这套 QEMU 环境里继续。

## 20) 菜单稳定性与按键模拟可行性（2026-11-10）

### 20.1 稳定性测量（同一配置，3 次）

- 配置：`A7R3_KICK_DUMP=1 A7R3_SURF_MORTON=1 A7R3_LIRO_AUTOREADY=1
  A7R3_SUGI_EXEC=1 A7R3_SUGI_CENTERSCALE=1 A7R3_BLIT_DUMP=1`，每次 240 s。
- 结果：
  - 第 1 次：崩在 `page translation fault at 0x152c38`（已知竞态），0 draw、3 帧；
  - 第 2 次：`Exception=0`，16 次 DRAW，99 帧（93 帧 >100 色）；
  - 第 3 次：`Exception=0`，16 次 DRAW，426 帧（282 帧 >100 色）。
  ⇒ 样本 **2/3 成功**；失败与渲染无关，是 im.elf 启动/初始化竞态，
  需要重试。成功运行的帧与 19.7 的确认帧**不是逐字节相同**
  （动画/时序不同），但都是完整菜单画面。
- 脚本：`analysis/qemu_a7r3/stability.sh`。

### 20.2 按键输入：内核路径在 QEMU 里不存在

- `input.ko` 在 QEMU 里**被加载**（`initcall init_module+0x0/0x30 [input]
  returned 0`），但它的 udif 驱动 probe 没有跑：
  - dmesg 里没有任何 `kbd:` 打印（模块成功 probe 才打印 `kbd: probe`）；
  - `/proc/input`、`/proc/kbd` **不存在**；shim 列 /proc 只看到
    `devices / misc / driver / nflashaB0 / ram7 / port ...`，没有 input 节点。
- 结论：`uinput` 之外，内核侧的按键通道（input.ko → udif → 应用）在
  QEMU 里没有建立；靠 proc/adc/gpio 注入物理键这条路**当前不可用**，
  除非给 QEMU 模型补 udif 平台设备（工作量在模型侧）。

### 20.3 应用侧注入：条件具备

- shim 在菜单阶段实测（libObj 基址 + 偏移）：
  - `InputService::m_pMyInstance`（base+0x129ebf8）= **0x174078（非 0）**
  - `pvm`（view manager，base+0x129ee1c）= **0x152660（非 0）**
  - `GraphicsEngine` 单例（base+0x121f070）= 0x748b40c（非 0）
- libObj 导出 `snd_InfraInputEvent_to_InputServant(const _InfraInputEvent*)`
  @ `0x3C00B1`（Thumb）：分配 24 字节、拷贝事件、发到 OSAL 队列
  **0x14A0429**（字面量已确认在 `0x3C00FC`）。
- 另外 `InputService::createEvent(unsigned long, unsigned char)` @ `0x3C031F`
  和 `pushFilterEvent(Event*)` @ `0x3C1AC5` 也在导出表里。
- ⇒ **应用侧按键注入在结构上是可行的**（InputService 已创建），但还缺：
  1. 24 字节 `_InfraInputEvent` 的字段含义（目前只知 callback 读
     `+0`、`+8`、`+0xc`）；
  2. MENU/ENTER 等按键的事件编码/键码；
  3. 一次真机/QEMU 验证（菜单项切换）。
- 下一步建议：先静态分析 `callback_convert`（0x3C30B8）与
  `callback_panel`（0x3C2EB1）确定字段；再从 key list（`InfraInput::
  SetEventKeyList`/`RmEventKeyList`）或 UI 的 Event 表拿键码；最后在
  shim 里构造事件调用 `snd_InfraInputEvent_to_InputServant`，看菜单是否响应。

## 21) 按键注入原型验证（2026-11-10 晚）

### 21.1 原型实现

- 在 shim 里新增 `inject_key_once(code, h)`：
  - 从 `libObj+0x129ebf8` 读 `InputService::m_pMyInstance`（运行中非 0，
    例如 0x17ef88 / 0x12d8a0）；
  - 调 `InputService::createEvent(this, code, h)`（入口 `0x3C031E`）得到
    `Event*`；
  - 调 `InputService::pushFilterEvent(this, Event*)`（入口 `0x3C1AC4`）。
- **关键坑（已修）**：Thumb 函数指针必须带 bit0。一开始用
  `lb+0x3C031E`（偶数）调用，CPU 以 ARM 态执行 Thumb 代码，直接崩在
  `pc=0x66cb31e`（cpsr T=0）。改成 `(lb+0x3C031E)|1` 后正常。
- 运行结果（多次）：`ev=` 非 0（如 0x21c150、0x226d00），
  `push_ret=0x0`，**无异常、菜单继续渲染**。按 draw 计数在 hd=20/30/45/60
  各注入一次（码 0x1ac/0x1ad/0x1ae/0x1b0）。

### 21.2 但没有可归因的 UI 变化

- 这些码来自 `KEY_TACT_MENU/ENTER/...` 名称表（`{名字指针, id}` 对，
  KEY_TACT_MENU → 0x1ac），但注入后：
  - 关闭了额外的 openView 探测（新增 `/usr/tool/viewprobe` 开关，默认关），
    只保留 `view/BASE_MENU`(hd=8)；四次注入前后 UXC 映射始终只有
    `viewSettingMenu.uxc`，没有新视图；
  - 早前看到的“注入后出现 viewGlobalMenu”是**假象**：对照运行
    （`/usr/tool/no_keyinj`，注入跳过）里同样出现这些视图切换，
    原因是 shim 自己在 hd=24/48/72/96 调用了 openView 探测。
- 结论：**注入通道打通、调用安全，但事件码/事件负载不对**，
  还没有证明菜单响应按键。

### 21.3 为什么码不对

- `callback_convert(0, _InfraInputEvent*)`（0x3C30B8）用的是 24 字节事件：
  读 `+0`（基础 id）、`+8`（经 0x100EAC 转换）、`+0xC`、`+0x10`、`+0x14`，
  并把 `event[0] + {1,2,4}` 作为动作 id。`createEvent(code,h)` 构造的是
  一个不带这些参数的 Event，UI 很可能因此忽略。
- 尝试 `InfraInput::GetInstance()`（0x84C004）后按
  `inst+0x28=count / inst+0x2c=table、entry 0x44、event@+0xc` 读表，
  得到的是指针值（count=0xbd0ea08 之类），说明返回对象布局/ABI 与
  `GetEventStatus` 里用的实例不是同一个，需要再核对。

### 21.4 下一步（已就绪）

1. **Hook `InfraInput::SetEventKeyList`（0x84D238）**：prologue 是
   `push.w {r4..r11,lr}; sub sp,#0x8c`（前 8 字节无 PC 相对），可以安全
   hook 并转发；它收到 `_InfraInputKeyList*`（r2）和数量（r3），
   把真实的 key 条目（含事件模板）dump 出来。
2. 或者 hook `InfraInput::GetEventStatus`（0x84C764）在**真实按键/事件**
   发生时抓一份填好的 24 字节事件，作为模板回放。
3. 拿到模板后，用 `InputService::callback_convert(0, &event)` 回放
   （或在 shim 里直接调 `snd_InfraInputEvent_to_InputServant(&event)`），
   用 UXC 映射/帧内容判断菜单是否响应。

### 21.5 本轮产物

- shim：`inject_key_once`、`dump_key_table`（实验性）、
  `/usr/tool/no_keyinj` 控制开关、`/usr/tool/viewprobe` 开关
  （外加 `A7R3_NO_UNIT_DUMP` 加快运行）。
- 脚本：`analysis/qemu_a7r3/run-inject.sh`、`run-inject2.sh`、
  `probe-proc.sh`、`stability.sh`。
- 日志：`/tmp/inj-*`、`/tmp/kt-*`（本轮），结论写入本节。

## 22) 按键表与第二条注入路径（2026-11-10 深夜）

### 22.1 Hook InfraInput::SetEventKeyList 成功

- `0x84D238` 的 prologue 是 `push.w {r4..r11,lr}; sub sp,#0x8c`，前 8 字节
  无 PC 相对，`hook_one` 可安全转发。
- 实测一次运行被调用 **33 次**，参数为 `(this, idx, list, n)`：
  ```
  KEYLIST this=0x46b50  idx=0x4  list=0x13cdba9c  n=0xe6 (230)
  ```
- 把每个 idx 的列表 dump 到 `/log/keylist_<idx>.bin`（每次运行最多 30 个），
  已提取到 `analysis/keylists/`。主表 `keylist_4.bin` = 230 项 × 12 B：
  ```
  { u32 key_id, u32 event_code, u32 0x7ffffffe }
  例：0x1ab -> 0x15010110
      0x0a7 -> 0x15010030
      0x002 -> 0x15010010
      0x1f1 -> 0x15010020 / 0x15010250 / 0x150102b0
  ```
  （0x1ab 与 `KEY_TACT_MENU` 在名字表里的 id 0x1ac 相邻，很可能就是 MENU 系。）

### 22.2 第二条注入路径：GetEventStatus + callback_convert

- 在 shim 里保存 `SetEventKeyList` 的 `this`（InfraInput 实例），再调用
  `InfraInput::GetEventStatus(inst, 4, arg, &event)`（0x84C764）填充 24 字节
  事件，然后 `InputService::callback_convert(0, &event)`（0x3C31B0）走应用
  自己的按键转换与分发。
- 实测（不崩、返回可读）：
  ```
  key=0x1ab        -> ges_ret=0x7ffffffe （查找失败哨兵）
  key=0x15010110   -> ges_ret=0x0        （成功；event[0]=0x15010110）
  key=0x15010030   -> ges_ret=0x7fffffff
  key=0x15010010   -> ges_ret=0x1
  ```
  说明 **GetEventStatus 的参数是 event code（0x1501xxxx），不是 key id**；
  但填充出来的事件除 `[0]` 外都是 0，`callback_convert` 之后 UI 没有变化
  （viewSettingMenu 保持不变，无新 UXC/视图）。
- 结论：事件还需要状态/参数（按下/抬起、重复、参数链表）才会被 UI 处理；
  `GetEventStatus` 在我们的调用参数下只回填了 code。

### 22.3 性能与开关

- 打开 `A7R3_SUGI_EXEC`（CPU 渲染合成面）会严重拖慢 draw 计数（200 s 只到
  hd≈45），做输入实验时建议只保留 `A7R3_LIRO_AUTOREADY=1`，不启 SURF dump。
- 当前开关：`/usr/tool/no_keyinj`（跳过注入）、`/usr/tool/viewprobe`
  （启用多视图 openView 探测，默认关）。

### 22.4 下一步

1. 反汇编 `0x84C5F4`（GetEventStatus 内部查找）确认 `(inst, idx)` 语义和
   返回值含义，以及 24 字节模板到底从哪个偏移拷贝。
2. 用 hook 抓一份**应用真实处理过的** `_InfraInputEvent`（例如 hook
   `callback_convert` 打印每次入参），对照我们的构造，补上状态/参数字段。
3. 或者从 `input.ko` 的 key table 资源（`table`/`key_tbl`）拿到「物理键 →
   0x1501xxxx」映射，再按真实事件的字段顺序构造 24 字节事件，
   用 `snd_InfraInputEvent_to_InputServant`（队列 0x14A0429）注入。

## 23) 事件表与事件码注入（2026-11-10 夜，goal: 实现按键注入）

### 23.1 拿到应用内部的“码 → 24 字节事件模板”全表

- 解析 `InfraInput` 内部链表：`0x84C5F4(inst, idx)` 按 idx 返回节点，
  `node+0x28=count`、`node+0x2c=table`、条目 **0x44 字节**，
  `+0x04=event code`、`+0x0C=24 字节模板`、`+0x14=状态字`。
- shim 里新增 `dump_event_table()`，一次运行导出 **144 条**：
  `analysis/a7r3-event-table.log`。样例：
  ```
  idx 4（与 SetEventKeyList 的 230 项同序）:
    4:0 code=15010030 t=15010030,0,7fffffff,0,0,0
    4:1 code=15010010 t=15010010,0,1,0,0,0
    4:3 code=15010020 t=15010020,0,7fffffff,0,0,0
    4:8 code=15010110 t=15010110,0,0,0,0,0
  idx 1/2: 小码 2/4/5、12..19、31..36，模板 [code,0,7fffffff/1,0,0,0]
  ```
- 结论：`_InfraInputEvent` 的 24 字节在“模板”里通常只有
  `[code, 0, 状态(0/1/0x7fffffff), 0, 0, 0]`；
  真实按键还需要 dispatcher 在运行期补状态/参数。

### 23.2 三种注入方式对比（全部不崩）

| 方式 | 入口 | 结果 |
| --- | --- | --- |
| createEvent + pushFilterEvent | 0x3C031E / 0x3C1AC4 | Event 创建成功、push_ret=0；无 UI 变化 |
| callback_convert(0,&event) | 0x3C31B0 | 调用成功；无 UI 变化 |
| snd_InfraInputEvent_to_InputServant | 0x3C00B0 | 调用成功；无 UI 变化 |

- 事件码取自真实按键表（key 0x1ab→0x15010110 等）并核对了内部模板
  （含状态 1 / 0x7fffffff / 0 的组合）；`viewSettingMenu` 始终不变，
  没有新 UXC/视图。
- `kikilog.dat`（nflasha10）在 QEMU 里是空的（全 0xFF），
  无法用来证明事件到达 ViewBase。
- 另外，QEMU 运行有约 1/3 概率在 `0x152c38` 崩（已知竞态），
  多次实验需要重试；做输入实验时只保留 `A7R3_LIRO_AUTOREADY=1`
  可把 200 s 的 draw 数从 ~45 提到 ~70+。

### 23.3 判断：缺的不是“事件码”，而是 dispatcher 上下文

- `callback_convert` 的行为依赖 InfraInput 在真实按键时更新的状态
  （key down/up、连击、当前输入源等）；我们直接调用只给了模板，
  应用会生成 Event 但当前 view 的 filter 不响应。
- 下一步优先级：
  1. **让内核按键链在 QEMU 里成立**：`input.ko` 的 udif 驱动 probe 没跑
     （没有 udif 平台设备），所以没有 `/proc/input`。给 QEMU 模型补
     udif 平台设备（或 patch 模块强制 probe），再用模块自带的 factory
     proc 写 `front/adc/gpio` 模拟真实按键，走完整内核→应用链路。
  2. 或者 hook dispatcher（`InfraInput` 收到内核消息后的处理函数），
     在真实事件路径上抓一份完整的 `_InfraInputEvent`（含状态/参数），
     再按同样内容构造。
  3. 或者 hook `callback_convert` 内部向模型推送 Event 的调用点
     （`0x3C3174` 附近的 `blx 0xfe2e4`），确认事件是否真的进入
     EventManager，并记录 Event id。

### 23.4 补充：用真实事件码走 createEvent 也没有反应

- 用内部事件表的真实 code 作为 `createEvent(m,h)` 的 m：
  `0x15010110 / 0x15010030 / 0x15010010 / 0x15010250`，
  `h=1` 与 `h=0` 都试过；`ev` 创建成功、`push_ret=0`、无异常，
  但 `viewSettingMenu` 始终不变，没有新视图/新 UXC。
- 因此三条注入路径（createEvent+pushFilterEvent、callback_convert、
  snd_InfraInputEvent_to_InputServant）都“调用成功但 UI 不响应”。
  说明问题不在事件码本身，而在**输入分发的上下文**（真实按键时
  InfraInput 会维护 key down/up、连击、当前输入源、filter 链状态，
  直接调用绕过了这些）。
- 下一步优先级调整：先证明“事件是否到达 ViewBase/EventManager”
  （hook callback_convert 内 0x3C3174 附近的推送调用，或 hook
  Event 构造/EventManager::push），再决定是补上下文还是走内核路径。

## 24) 直接复刻 callback_convert 的分发（2026-11-10 深夜）

### 24.1 关键逆向结果

- 解析 libObj 的 PLT veneer（ARM `add ip,pc,#imm; add ip,ip,#imm; ldr pc,[ip,#imm]`）：
  - `0x1004FC` → `InputService::createEvent(unsigned long, unsigned char)`
  - `0xFC6B4`  → `Event::Event(unsigned long, unsigned char, unsigned char)`
  - `0xFE2E4` → 未在 `.rel.plt` 中（本地长跳 veneer，推测是 EventManager/EventSender 的 push）
- 还原 `callback_convert` 的真实流程：
  ```
  action = event[0] + {1,2,4}   （由 event[8] 经 0x100EAC 的转换值决定）
  ev     = InputService::createEvent(inst, action, 4)   // h 固定 4
  send(ev, 0x20, paramObj)      // 0xFE2E4；paramObj 来自 0x319290(event[0x10])
  ```
  即真实路径用 **h=4**，并把 `event[0x10]` 包成参数对象。

### 24.2 复刻 dispatch 的实验

- shim 新增 `dispatch_event(action)`：`createEvent(inst, action, 4)` 后直接调用
  `0xFE2E4(ev, 0x20, NULL)`。
- 试了 action = `0x15010111 / 0x15010112 / 0x15010114 / 0x15010011 / 0x15010251`：
  全部返回非 0（如 0x18a9b8），**无崩溃**，但 `viewSettingMenu` 不变。
- 对照实验（`/usr/tool/no_keyinj` 跳过注入、其余环境相同）：
  - 对照：帧到 `frame_9004` 为止（233 色，完整菜单）；
  - 注入：多出 `frame_9005..9007`（187→242 色），说明注入后应用**有额外重绘**；
  - 但两边的橙色选择条行位置相同（y=176 与 223，各 303 px），
    没有看到选择项移动。⇒ 有重绘、无可确认的导航效果。

### 24.3 结论与下一步

- 现在的注入能创建 Event 并走真实 sender，但参数对象为空（`event[0x10]=0`），
  且当前 view 是通过 `openView("view/BASE_MENU")` 程序化打开的，
  可能没有进入可接受按键的输入焦点状态。
- 下一步（按优先级）：
  1. hook `0xFE2E4` 的真正目标（本地 veneer 的 GOT 槽 `0x11FEDB4` 无符号，
     需要解析 `.rel.dyn` 的 RELATIVE 或静态指针表），确认事件是否进入
     EventManager/EventSender；若进入，再补参数对象（`event[0x10]`）。
  2. 构造参数对象：`0x319290(dst, src)` 只是把 src 存到 dst+0x10，
     说明参数是外部结构指针；可从内部事件表条目 +0x10 或
     `InfraInput::GetPhyKeyStatus` 的 `_InfraInputPhyKeyData` 找结构布局。
  3. 或者改走内核链路（给 QEMU 补 udif 平台设备 / 让 input.ko probe），
     用模块自带 factory proc 注入真实按键。

## 25) 应用输入队列的直接投递（2026-11-10 深夜）

### 25.1 找到输入线程与队列

- `input.ko` 在 QEMU 里不 probe（无 `/proc/input`），但应用侧的输入接收线程
  **I-InputRecv** 一直在跑：
  ```
  OSAL_RCV tid=184 comm=I-InputRecv qid=7a0020
  ```
  ⇒ 应用输入事件队列 = **0x7A0020**。
- 从保存的日志里看到该队列上的真实消息样例：
  ```
  msg[0]=0x7a0002 / 0x7d0002, msg[1]=2, msg[2]=0xc / 0xd / 0x1b ...
  ```
- `I-InputRecv` 线程入口 = libObj **0x84E4F0**（由 `entry=6a9c4f1` + base
  0x624E000 反推）；消息类型由 `[msg-4]` 读取（0x84B420）。

### 25.2 osal_snd_msg 需要 valloc 分配的消息

- 直接用栈上缓冲区 `osal_snd_msg(0x7A0020, buf, 0/16, 0)` 返回
  **0xFFFFFC00（-1024）**，被 OSAL 拒绝。
- 改为 `osal_valloc_msg_wait(0x7A0020, &msg, 16, 0)` 分配再填 payload，
  然后 `osal_snd_msg`：
  ```
  valloc_ret=0x0  msg=0x48f47ac  snd_ret=0x0
  ```
  ⇒ **消息成功进入应用输入队列**（这是目前最接近“真实按键”的注入路径）。
- 发了 5 条：`{0x7a0002,2,0xc}`、`{0x7a0002,1,0xc}`、`{0x7a0002,2,0xd}`、
  `{0x7a0002,2,0x1b}`、`{0x7d0002,2,0xc}`；无崩溃。

### 25.3 结果：仍无可确认的 UI 反应

- 对照（`/usr/tool/no_keyinj` 跳过发送）与测试（发送 5 条）各跑一次：
  - 两边视图都只有 `viewSettingMenu`，无新 UXC；
  - 测试最后一帧（161 色）顶部有一条白带，其余与对照相同——
    这是 **kick 抓帧时机不同**（顶栏刚清空还没重绘），不是界面变化。
- 结论：消息投递成功了，但 payload 的语义（0x7a0002 的类型/字段）还不对，
  I-InputRecv 没有把它当成按键。

### 25.4 下一步

1. 反汇编 `0x84E4F0`（I-InputRecv）在收到消息后的分发逻辑，确认
   `[msg-4]` 头字段、`msg[0]` 类型（0x7a0002/0x7d0002）和 payload 字段
   各代表什么；特别是 key code / state 放在哪个位置。
2. 或者用 hook 在 `I-InputRecv` 收到消息后打印 `msg[0..7]`（第一次/第二次
   已能确认投递成功），再对照真实相机上的按键消息（需真机 input.ko 日志）。
3. 若 0x7A0020 只是“输入设备状态通知”而非按键数据，则需要找
   `input.ko` 的 `osal_snd_sync_msg(0x720018, msg)` 在用户态的对应
   转发路径（uipc proxy），从那边注入 0xfff1 类型的帧。

## 26) 不预开菜单 + 多种注入的判定实验（2026-11-11 凌晨）

- 目的：排除“菜单是我们自己 openView 打开的”这一干扰，看注入能否**从无到有**
  打开菜单（真按键效果）。
- 改动：
  - `view/BASE_MENU` 的 openView 改为只在 `/usr/tool/open_view` 存在时执行
    （默认不打开，启动后停在应用默认视图）；
  - 注入梯子混合三种方式：`send_raw(0x15010110/0x15010010)`、
    `dispatch_event(0x15010111)`、`osal_send_input(0x7a0002/0x7d0002 ...)`；
  - hd=10 与 hd=40 各 dump 一次 UXC 映射。
- 结果（一次运行，`Exception=0`）：
  - `viewSettingMenu` 计数 **0**（注入前后都没有菜单映射）；
  - 注入调用执行了 3 次（draw 计数到 ~20），无崩溃；
  - 即：三种注入都**没有**触发菜单打开。
- 另外做了 OSAL 消息头实验：把 valloc 出来的消息头 `[msg-4]` 改成 0x720018
  （模拟来自内核输入队列），`snd_ret=0`、无崩溃，但 I-InputRecv 的调试
  打印路径没有在串口上出现我们的消息内容（printk 可能进内核环，不落到串口）。

### 结论（阶段性）

- 事件层（createEvent/pushFilterEvent、callback_convert）和消息层
  （valloc+osal_snd_msg 投递到 0x7A0020）都已打通调用，但应用没有把
  它们当成有效按键。
- 更可能的原因：QEMU 里 `input.ko` 的 udif 驱动没有 probe（没有
  `/proc/input`），应用侧 InfraInput 缺少“输入设备就绪”的状态/键表，
  因而过滤掉了这些事件。
- 下一步（唯一有希望的正路）：**让内核输入链路在 QEMU 里成立**
  - 给 QEMU 模型补 udif 平台设备（或 GPIO/子 CPU 设备），使 `input.ko`
    probe 成功、创建 `/proc/input`；
  - 之后用模块自带的 factory proc（`front/adc/gpio/vgrip` 模式）注入
    真实按键，走完整内核→应用链路。
- 这一步是内核/模型层工作，不再是用户态 shim 能绕过的。

## 27) 参考 a7c2-qemu 的按键注入机制（2026-11-11）

> 参考仓库：https://github.com/chocotan/a7c2-qemu.git（已 clone 到
> `F:\project\a7r3\a7c2-qemu`）。关键文档
> `docs/tech/06-input.md`、`runtime/analysis/inject-key.c`、
> `report/camera-input-coverage/README.md`。

### 27.1 A7C2 的做法（源码级）

- **不是宿主 QEMU 模型注入**，而是 guest 里的一个独立小工具
  `/simulation/inject-key`，通过串口 shell 执行：
  ```
  /simulation/inject-key <queue> <src> <key_code> <sub> <status> <ex> <from>
  ```
- 工具用原 OSAL API：
  ```
  osal_valloc_msg_wait(queue, &msg, bytes, 1);
  ... 填 payload ...
  osal_set_msg_src(msg, src);
  osal_snd_msg(queue, msg);
  ```
- 两条前端：
  - **Infra20**（默认）：queue `0x807a0020`、src `0x92840747`、**20 字节**，
    `{key, sub, status, ex, from}`，from=1；
  - **App32**：queue/src `0x5503e0`、**32 字节**，
    `+0 key, +4 sub, +8 status, +0x10 ex, +0x18 from`。
- 键值是 **raw code**（不是 canonical）：MENU `0x1ab`、ENTER `0x1ac`、
  UP `0x3`、DOWN `0x30`、LEFT `0x15`、RIGHT `0x1af`、REC `0x1f0`、
  半按 `0x1f1`、全按 `0xa7`…（与 A7R3 的 key list 里的 key id 一致）。
- 事件序列：短按 = status `1` → sleep 0.05 s → status `0`；
  拨轮是单发 ±1024；MODE 拨盘是单发位置号。
- 原固件在 appFw 里把 raw 查表换成 canonical（1120 对），所以注入 raw 即可。
- 验收：以“原消费者 + 新画面”为准，发送返回 0 不算成功。

### 27.2 对 A7R3 的启示

- A7R3 的 `libosal_uipc.so` 有 `osal_valloc_msg_wait / osal_snd_msg /
  osal_set_msg_pri / osal_free_msg`，但**没有 `osal_set_msg_src`**
  （A7C2 有）；消息的 source/from 用别的方式表示。
- 我们在 im.elf 内部（shim）注入到 `0x7A0020`/`0x807A0020` 的 20B/24B
  消息都能 `valloc=0, snd=0`，但应用不消费。一个很可能的解释是
  **“自己发给自己”被接收端丢弃**：`I-InputRecv` 会读消息头 `[msg-4]`，
  与 `0x7A0020` 相等时直接 free 并继续收；而 im.elf 内部发出的消息
  源正好是它自己。
- A7C2 之所以能工作，是因为 `inject-key` 是**独立进程**，有独立的
  OSAL client/source，接收端不会把它当 loopback。

### 27.3 下一步（按 A7C2 复刻）

1. **确认 A7R3 guest 是否有可用的串口 shell**（A7C2 有）：
   - QEMU 用 `-serial unix:/tmp/...` 或 pty 起，在 app 运行期间发
     `echo`/`ls` 验证；若 init shell 仍活着，就可以直接执行注入工具。
   - 如果 shell 不可用：让 shim 用 `fork()+execve()` 起一个**独立进程**
     （A7C2 等价物），从子进程里做 OSAL 注入。
2. **构建 A7R3 的 inject-key 工具**：仿 `a7c2-qemu/runtime/analysis/
   inject-key.c`，链接 guest 的 `libosal_uipc.so`（可用仓库里已有的
   arm 工具链/sysroot），参数同 A7C2；先发 MENU `0x1ab` 1/0。
3. 先闭合“按一次 MENU、画面出现菜单”的小循环，再扩 ENTER/方向键。
4. 环境注意：WSL 近期频繁重启会清空 `/tmp`；`av-cam.bin` 已放
   `/root/firmware/a7r3/av-cam.bin`，日志/帧要当场复制到 `/mnt/f`。

## 28) 按键注入打通：独立进程 + 串口 shell（2026-11-11 晚）

### 28.1 复刻 A7C2 的做法

- `a7c2-qemu` 的机制是：guest 里一个独立小程序 `/simulation/inject-key`
  通过串口 shell 执行，用原 OSAL API 发消息；关键是**发送方是独立进程**。
- A7R3 这边同样具备条件：
  - 内核启动后 initrd 的 init 在 `ttyS0` 上起了一个 **BusyBox ash**
    （`/ #` 提示符，约 2 s 出现），所以 **运行时可以敲 guest 命令**；
  - QEMU 用 `-serial unix:/tmp/a7r3key.sock,server=on,wait=off` 起，
    从宿主 python 连接即可发命令（先前 “no serial socket” 是没加
    `server=on` 且 `/tmp` 被 WSL 重启清掉的缘故）。
- 构建了 `/usr/tool/inject-key`（`analysis/payloads/inject-key-a7r3.c`）：
  - `clang --target=arm-linux-gnueabi -fuse-ld=lld -nostdlib`，
    动态链接 guest 自己的 `libc-2.11.2.so / libpthread / libdl / ld-2.11.2.so`
    和 `/usr/lib/libosal_uipc.so`、`libosal_utm.so`，
    `--dynamic-linker=/lib/ld-linux.so.3`；入口 `_start` → `__libc_start_main`
    与 A7C2 完全一致；
  - 参数：`inject-key <queue> <key> [hold_ms] [size] [hdr]`，
    发 `{key,0,status,0,from=1}`，先 `status=1`，`usleep(hold)`，再 `status=0`；
  - 实测**无需显式初始化**：库自己可用，`osal_valloc_msg_wait` / `osal_snd_msg`
    返回 0（此前在 im.elf 进程内发是“成功但无效”，差别就在“谁在发”）。
- 镜像内安装：`/usr/tool/inject-key`（`repack-usr.sh` 增加安装步骤；
  也可用 `update-image-helper.sh` 直接改 eMMC 镜像的 partition 15，免整盘重建）。

### 28.2 接收端语义（libObj 0x84E4F0 I-InputRecv）

- 队列 = **0x7A0020**；字面量 `0x84E7AC = 0x007A0020`。
- `[msg-4]`（0x84B420 读）是**类型字**：
  - **等于 0x7A0020** → 视为“线程退出”消息：free + `osal_exit_thread`；
  - **不等于** → 按输入事件处理（所以 valloc 后头字是 0xA5A5A5A5 也能进）。
- 真实输入代理消息的头字是 `0x410025`；我们自己 valloc 的是 0xA5A5A5A5，
  两种都能被处理（hdr 只影响上面的分支）。
- 载荷（16 B）`{key_id, sub, status, ex}`：0x84E712 用 `msg[0]` 在键表里查
  index，然后 0x84CCC4 更新物理键状态。

### 28.3 全链路证据（shim 新增 GRCV/ISND 探针）

- `analysis/payloads/gpu_shim_qemu_gotkey.so`（源里新增）：
  - `GRCV`：hook `osal_rcv_msg_tmo`，对 0x7A0020 / 0x7A804C / 0x7AE03A
    打印接收线程 comm、hdr、前 4 个字；
  - `ISND`：hook `osal_snd_msg`，打印所有 `I-*` 线程发出的消息。
- 一次 ENTER 注入（`a7r3-en1-serif.log`，菜单已开）：
  ```
  ISND I-InputRecv qid=7a804c w=1ac,0,1,0
  ISND I-InputMain qid=8f013a w=7a0002,1,18,...
  ISND I-InputMain qid=7ae038 w=15010060,0,1,0
  GRCV I-InputEvent q=7ae03a/7ae038 ...
  ```
  - MENU `0x1ab` → `0x15010110` → 队列 **0x7AE03A**
  - ENTER `0x1ac` → `0x15010060` → 队列 **0x7AE038**
  - DOWN `0x30` → `0x15010080` → 队列 **0x7AE05F**
  - 即：raw key 经应用自己的键表转换成 canonical 事件，由 `I-InputEvent`
    线程取走，**走的是原固件消费链**。
- **UI 反应对照**（同一 open_view 菜单状态）：
  - 对照（不发键，`a7r3-cm1-serif.log`，168 s）：`viewCaution.uxc` = **0 次**；
  - 只发一次 ENTER（`a7r3-en1-serif.log`）：按键后 UXC dump 出现
    `/usr/share/app/viewCaution.uxc`（**6 次**），并伴随 `DRAW/NOTIFY`；
  - 负对照：同一个 helper 进程、同样的命令，只把键码换成表里不存在的
    `0x999999`（`a7r3-bg1-serif.log`）：消息确实被 I-InputRecv 收到，
    但**没有** 0x1501xxxx 事件、`viewCaution` = **0 次**。
  ⇒ 差别只在“键码是否有效”，不是进程启动/串口命令本身造成的。
- 时间线（`a7r3-vf1-serif.log`，带 SUGI 渲染）：
  ```
  7375  A7R3_KEY_SEND key=0x1ac st=1
  7377  A7R3_KEY_SEND key=0x1ac st=0
  7380  SEQ1_RC=0
  7403  maps: /usr/share/app/viewCaution.uxc   <-- 按键后 28 行
  ```
- 画面级（`analysis/vf1_frames/`，按抓取时间排序）：
  - ENTER 前最后一帧 `frame_9008`：242 色，主色 0x000F（黑），
    非主色 74390 px 铺满全屏 = 菜单画面；
  - ENTER 后第一帧 `frame_9009`：54 色，主色 0xFFFF（白），
    非主色 57600 px；`frame_9011..9013` 主色仍是白、内容在 y=60..479
    = 白色全屏提示/对话框；
  - 相邻帧差异：`9008 → 9009` 有 **86.7%** 像素变化（其余相邻帧 0.4%–30%）。
- 结论：**按键注入已实现**——独立 guest 进程发出的原始键码，被原固件
  输入链消费（raw→canonical 事件→App 队列），并触发新的 UI 视图
  （白色 caution 对话框），对照组与无效键组都不出现。

### 28.4 可复现脚本

- `analysis/qemu_a7r3/build-inject-key.sh`：构建 helper；
- `update-image-helper.sh` / `update-image-shim.sh`：直接改 eMMC 镜像内文件；
- `inject-serial.py`：起 QEMU + 串口 socket + 等 UI 就绪 + 发注入命令；
  环境变量：`A7R3_SEQ`（`;;` 分隔的命令序列）、`A7R3_WAIT_FOR`、
  `A7R3_TAIL`、`A7R3_KEY_SIZE`、`A7R3_NO_SUGI`；
- `retry-inject.sh` / `retry-seq.sh` / `retry-menu.sh` / `retry-menu2.sh` /
  `retry-enter.sh` / `retry-ctrl-menu.sh`：带重试（`0x152c38` 竞态约 1/3）；
- `toggle-openview.sh on|off`：镜像 `/usr/tool/open_view` 开关（打开基础菜单）。

## 29) 修复：caution 页面“大面积白色”（2026-11-11 夜）

### 29.1 现象与真机对照

- ENTER 打开 `viewCaution` 后，QEMU 帧里页面背景是**纯白**，文字压在黑色横条上；
  真机（用户拍照）是**全屏黑底**、白色警告图标、白字“此功能当前被禁止。”、
  右下“● 确定”、细边框 —— 除了背景色，内容位置与真机一致。

### 29.2 根因：Sugilite unit fill 用了过期的 u[] 颜色

- 在 QEMU 模型里给 `0x00000281`（unit draw）加了日志：
  ```
  UDLOG surf=202e0200 rect=20,60-620,476 pix=ffff has_u=1 col=00000000
        u=1.000,1.000,1.000,1.000
  ```
  即：**整页/半屏的白色填充是 unit fill 画出来的**，模型按 shader uniform
  `u[0..3]`（命令 0x0451..0x0454）算颜色，当时这些 uniform 是 (1,1,1,1)
  → 不透明白 `pix=0xFFFF`。
- 但真正的 unit draw 参数在 `0x0281` 指向的**嵌套命令块**里
  （`NESTLOG` 可见 0x0100..0x0104/0x010a 等字段），模型没有解析；
  `0x0105`（color 寄存器）这批 fill 之前从未被设置（`has_color=0`）。
- 同期 Gemini 的整屏 draw 源纹理是**黑色**（hist 全黑），说明黑色页面
  本来就有；白色完全是 unit fill 的过期 uniform 覆盖上去的。
  真机执行真实参数时这些 fill 是黑的（或走别的合成路径），所以真机全黑。

### 29.3 修复（qemu `hw/arm/bionz.c`）

- 对“解析出来是纯白 `pix==0xFFFF` 且面积 ≥ **16×16**”的 unit fill 不再直接刷白：
  - **整宽（x0==0 && x1==640）**：视为页面背板，按不透明黑 `0x000F` 画；
  - **其余（页面矩形/图标区/顶部条等）**：直接跳过（它们在内容之后执行，刷黑会盖掉文字）。
- 阈值一开始用的是 64×64，会漏掉顶部 `0,0-640,60` 的白条与页内 46px 的白条；
  降到 16×16 后顶部白条消失（`ga1` 运行，顶部 y=2..70 全为 `0x000f`）。
- 开关：`A7R3_ALLOW_WHITE=1` 可恢复旧行为（用于对照/回归）。
- 调试用日志开关（默认关）：`A7R3_UD_LOG`、`A7R3_NEST_LOG`、
  `A7R3_SKIP_WHITE`（旧实验）、`A7R3_BLACK_FILL`、`A7R3_BLACK_BACKDROP`。

### 29.4 结果

- 修复后帧（`analysis/ga1_frames/frame_9013.raw` → `ga1_frame_9013.png`）：
  **整屏黑底**、白警告图标、白字“此功能当前被禁止。”、“● 确定”、细边框，
  与真机照片一致；顶部原先的白色横条（`0,0-640,60` 白填充）已消失
  （顶部 y=2..70 采样均为 `0x000f`）。
- 仍存在的小差异：底部左侧那行小字的字形还原不完整（字体/点阵差异），
  不影响按键与对话框内容。
- 涉及脚本：`patch-udlog.py`、`patch-udlog2.py`、`patch-nestlog.py`、
  `patch-skip-white.py`、`patch-black-fill.py`、`patch-black-backdrop.py`、
  `patch-fill-policy2.py`；运行脚本 `retry-udlog.sh`、`retry-skipwhite.sh`、
  `retry-blackfill.sh`、`retry-blackback.sh`、`retry-frames-enter.sh`。

## 30) 交互式使用：实时看菜单 + 按键控制（2026-11-11 夜）

### 30.1 工具

`analysis/qemu_a7r3/live-menu.py`：

```bash
wsl.exe -d Ubuntu -u root -- python3 \
  /mnt/f/project/a7r3/analysis/qemu_a7r3/live-menu.py
```

- 起 QEMU（SUGI 软渲染 + KICK/BLIT 抓帧 + 串口 socket），等 guest shell，
  `touch /usr/tool/viewprobe`，等基础菜单（`viewSettingMenu.uxc` 映射，
  约 1–3 分钟）；
- 内置 HTTP 服务：浏览器打开 **http://localhost:8765/** 即可看到不断刷新的
  `latest.png`（每秒刷新），以及状态行；
- 终端里输入字母 + 回车即发按键（经 `/usr/tool/inject-key 0x7a0020 <code> 50 16 0`）：

  | 键 | 动作 | raw code |
  | --- | --- | --- |
  | m | MENU | 0x1ab |
  | e | ENTER | 0x1ac |
  | u / d | UP / DOWN | 0x3 / 0x30 |
  | l / r | LEFT / RIGHT | 0x15 / 0x1af |
  | h / f | 半按 / 全按 | 0x1f1 / 0xa7 |
  | c | REC | 0x1f0 |
  | s | 存一张 snapshot | — |
  | q | 退出 | — |

- `--script "d,d,e,s"`：菜单就绪后自动按序发键（便于无人值守回归）；
  `--duration N`：就绪后再跑 N 秒。
- 输出目录：`analysis/live/`（`latest.png`、`snap_*.png`、`raw/frame_*.raw`、
  `qemu.out`、`status.txt`）。

### 30.2 实测：DOWN 能移动菜单选中项

- 同一次会话里连发两次 `d`，snapshot 显示橙色选中框从
  `自动关机开始时间` → `自动关机温度` → **`NTSC/PAL选择器`**
  （`analysis/menu_down2.png`），说明 UP/DOWN 真正被 UI 消费、菜单可控。
- 版本差异：`ENTER` 在“自动关机开始时间”上会弹黑底 caution 页面
  （见第 29 节，与真机一致）。

### 30.3 使用注意

- 软件渲染很慢：一次按键后的完整重绘可能要几十秒，画面刷新滞后是正常的；
- 已知 `0x152c38` 启动竞态（约 1/3），工具会报 “guest faulted”，重跑即可；
- 镜像里 `/usr/tool/open_view` 需为 ON（开机自动进基础菜单），
  可用 `toggle-openview.sh on` 确认。

## 31) 网页按键面板 + 采样器定点化（2026-11-11 深夜）

### 31.1 网页上的模拟按键

`live-menu.py` 的页面新增按钮面板（UP / MENU / ENTER / DOWN / LEFT / RIGHT /
半按 / 全按 / REC / 截图），浏览器里方向键、Enter、m 也可直接触发：

- 前端 `fetch('/key?c=<m|e|u|d|l|r|h|f|c>')`；
- 服务端 `Handler.do_GET` 解析后用串口发
  `/usr/tool/inject-key 0x7a0020 <raw> 50 16 0`，返回 JSON
  `{"ok":true,"label":"DOWN","code":"0x30"}`；
- `/snap` 复制一张 `latest.png` 到 `snap_HHMMSS.png`；
- 端口被占用时自动向后找可用端口（打印实际 URL）。

实测：网页点一次 DOWN，画面选中框从 `自动关机开始时间` 移到
`自动关机温度`（`analysis/live/after_d.png`），与终端按键行为一致。

### 31.2 采样器定点化（A/B 实测）

- 改动：`ui_gemini_raster.h` 里原来每个像素做两次 double 除法
  （`(px+0.5-left)/(right-left)`、`(py+0.5-top)/(bottom-top)`），
  改成 16.16 定点增量：每次 draw 只算一次 `du/dv`，像素循环里只有
  整数加法与移位；旧实现保留在 `-DUI_GEMINI_OLD_SAMPLING` 下用于对照。
- shim 增加 `GEMTIME` 计时（`clock_gettime(CLOCK_MONOTONIC)`，每 32 次
  draw 打印累计毫秒/平均微秒）。
- 同一流程、同为 928 次 GEM draw：
  | 版本 | 累计光栅时间 | 平均每次 |
  | --- | --- | --- |
  | 旧（每像素双除法） | 3043 ms | 3.28 ms |
  | 新（16.16 定点） | **1608 ms** | **1.73 ms** |
  ⇒ 采样器本身 **约 1.9×**。
- **但**：整个运行墙钟约 168 s，GEM 光栅只占 ~1.6 s（约 1%）。
  所以肉眼几乎感觉不到变快 —— 真正的瓶颈不在采样器，而在
  guest 应用本身跑在 TCG 下（UI 布局、字体/资源解压、命令构建等）、
  以及 QEMU 侧 Sugilite 模型与抓帧/日志开销。
- 结论：定点化保留（正确且更快），下一步提速要先做 profiling
  （guest `/proc/stat` + 宿主 perf 采样 QEMU 进程），再决定优化点。
- 构建脚本：`build-shim-ab.sh`（一次产出 old / fixed 两个 .so），
  计时读取：`gemtime.sh <serial.log>`。

## 32) 热点定位：guest PC 采样（2026-11-11 深夜）

### 32.1 方法

- QEMU 模型里加 `PCSAMPLE`：`A7R3_PCSAMPLE=1` 时用 QEMU 定时器每 5 ms 采样
  一次 guest PC（和 LR），输出 `PCSAMPLE cpu=N pc=… lr=…`（`hw/arm/bionz.c`）；
- 一次运行（`profile-run.py`）里让 guest 把
  `/proc/uptime`、`/proc/stat`、`/proc/kallsyms`、**im.elf 自己的**
  `/proc/<pid>/maps` 写到 `/tmp`，跑完从 eMMC partition 10 读回；
- 聚合脚本 `pc-hotspots.py`：PC→模块/偏移，用 kallsyms 解析内核、
  用本地固件 ELF 的 .dynsym/.symtab 解析库函数（libefence.so 用我们构建的
  shim ELF）；同时统计 LR（调用者）。
- 一次 4.5 分钟的运行：墙钟 132.6 s，QEMU 宿主 CPU 115.4 s（0.87 核），
  guest uptime 122.5 s、idle 0.2 s（0%），user 38.1 s / system 81.0 s；
  共 21621 个 PC 样本。

### 32.2 结果（pr7）

| 模块 | 占比 |
| --- | --- |
| 内核 | **67.1%** |
| **libefence.so（我们的 shim）** | **28.8%** |
| ld-2.11.2 / libObj / libc / libmpr / viewUnified2 / libosal_uipc | 各 <1% |

内核内部（占全部样本）：

| 符号 | 占比 | 备注 |
| --- | --- | --- |
| `sched_clock` | 11.5% | 调用者 `sched_clock_cpu+0x28`，每次调度/时间读取 |
| `utimer_read_freerun` | 3.1% | 调用者 `utimer_get_schedclock`，读 hwtimer MMIO |
| `hwtimer_read_clock_cpu` | 1.0% | 同上 |
| `gic_eoi_irq` / `irq_handler` / `gic_unmask_irq` | ~3.8% | `irq_handler <- udif_handler`，输入设备中断 |
| `_raw_spin_lock/unlock`、`__switch_to`、`__migrate_*` | ~5% | RT 内核 + 频繁上下文切换 |
| `sdhci_emmc_wait`、`format_decode`(printk) 等 | ~2% | |

shim 内部（占全部样本）：

| 符号（解析到最近符号） | 占比 | 说明 |
| --- | --- | --- |
| `ioctl+0x9e4` 一带 | 10.1% | shim 的 syscall 辅助代码；调用者 `u_osal_snd_sync_direct` / `u_osal_valloc_msg_wait` / `u_osal_snd_msg` |
| `osal_comm+0x144/0x17c/0x160` | 11.8% | **每打印一条 trace 就去读 `/proc/<tid>/comm`**（open/read/close/lseek） |
| `trace_osal_valloc_msg_wait+0x94` | 3.5% | valloc 语义检查 + 打印 |
| `trace_osal_snd_msg+0x6c/0x578` | 1.2% | 发送 trace |

⇒ **约 29% 的时间花在我们自己的调试 trace 上**（不是应用、也不是 GPU），
仅 `osal_comm` 的 `/proc` 读取就有 ~12%。

### 32.3 结论与下一步

1. **第一优先：去掉/静默 shim 的 OSAL trace**（并缓存 comm 名），预计直接
   省掉 ~25–30% 的 guest CPU；
2. 内核 timer 读取（sched_clock→utimer MMIO）≈15%：可考虑让 guest 改用
   ARM 通用定时器做 sched_clock，或在模型里优化 TIMERREAD 路径；
3. `udif_handler` 中断（~1–2%）值得查是不是输入设备在空转触发中断；
4. 应用本身（libObj/view/libm）占比很小，之前的“渲染慢”主要是上述
   系统级开销 + TCG 执行。

复现：`A7R3_PCSAMPLE=1 python3 analysis/qemu_a7r3/profile-run.py --tag prN`，
然后 `bash analysis/qemu_a7r3/run-hotspots7.sh`（改成对应 tag）查看聚合。

## 33) 提速第 1 步：静默 trace + comm 缓存（2026-11-11 深夜）

### 33.1 改动（shim `gpu_shim_qemu.c`）

- **OSAL trace 默认关闭**：`trace_on` 初始为 0，只有存在
  `/usr/tool/osal_trace` 时才打开（原来每条消息都读 `/proc/<tid>/comm`
  并写串口，占 guest CPU ~29%）；
- **comm 名缓存**：`osal_comm()` 按 tid 缓存线程名，不再每条 trace 重新
  打开 `/proc/self/task/<tid>/comm`；
- **LDEC / ISND 逐条日志**改为受 `trace_on` 控制；
- **`maps_contains()` 加缓存**：`/proc/self/maps` 读一次缓存 200 ms，
  命中用缓存、未命中再刷新（保证新 mapping 仍能被识别）。

### 33.2 A/B（同一 profile-run，PC 采样）

| 运行 | 配置 | 菜单出现 | 墙钟 | guest user | guest system | 合计 CPU |
| --- | --- | --- | --- | --- | --- | --- |
| pr7 | trace 开 + 定点采样 | 74.5 s | 132.6 s | 38.1 s | 81.0 s | 119.1 s |
| qt1 | trace 静默（无 maps 缓存） | 63.5 s | 121.6 s | 28.7 s | 79.0 s | 107.7 s |
| qt3 | 静默 + maps 缓存 | **56.5 s** | **114.6 s** | **26.6 s** | **74.9 s** | **101.5 s** |

- 合计 guest CPU **-14.8%**（user **-30%**），到菜单时间 **-24%**（有启动竞态噪声）；
- qt3 采样里 shim 代码本身的大头已从 `osal_comm` 变成 `ioctl` 系统调用
  处的停留（`ioctl+0x9f8` → 实际是 `sys3` 的 svc），即现在是**设备 ioctl
  的 syscall/内核路径**，不再是我们的 trace；
- 内核热点仍是 `sched_clock`(22%) / `utimer_read_freerun`(7.4%) /
  `hwtimer_read_clock_cpu`(2.5%)，合计约 1/3 —— 这是下一步的目标。

### 33.3 事故与恢复（记录）

- 批量替换 `maps_contains` 时锚点误匹配到**前向声明**，误删了源文件
  ~1700 行；已从 DSH 工作区快照
  `dsh-workspace-changes-knaVUg/captures/ba89ce35…` 恢复，
  并另存备份 `payloads/gpu_shim_qemu.c.bak-good`；
- 恢复后重新应用了 33.1 的三个补丁（`patch-quiet-trace.py`、
  `patch-gate-logs.py`、`patch-maps-cache3.py`），qt3 验证菜单与
  caution 正常（无 exception）。

### 33.4 复现

```bash
# 重新打补丁（在干净源码上）
bash analysis/qemu_a7r3/patch-quiet-trace.py
bash analysis/qemu_a7r3/patch-gate-logs.py
bash analysis/qemu_a7r3/patch-maps-cache3.py
bash analysis/qemu_a7r3/build-shim-ab.sh      # old / fixed 两个 .so
bash analysis/qemu_a7r3/update-image-shim.sh analysis/payloads/gpu_shim_qemu_gotkey.so
A7R3_PCSAMPLE=1 python3 analysis/qemu_a7r3/profile-run.py --tag qtN
bash analysis/qemu_a7r3/run-hotspots-any.sh qtN
```
- 需要逐条 OSAL trace 时：`touch /usr/tool/osal_trace`。

## 34) 继续定位：一个纯自旋线程 + GPU ioctl 风暴（2026-11-11 深夜）

### 34.1 找出“谁在烧 CPU”

- 采样所有线程的 `/proc/<pid>/task/*/status`（脚本 `ctxt-top2.py`）：
  ```
  THREAD_INFRA1 (pid 135, tid 150/151):
      State: R (running)   voluntary=76   nonvoluntary=750400（4 秒内）
  ```
  → **一个线程吃掉整核**：从不主动睡（voluntary 只有 76），
  非自愿切换 ~5.7 万次/秒（单核 guest 上意味着不断被抢占），
  utime+stime 基本等于墙钟。
- 它不是 OSAL 的启停问题：`/usr/tool/osal_throttle`（让 tmo==0 的空轮询
  睡 200 µs）实测无效，说明自旋不在我们 hook 的 `osal_rcv_msg_tmo /
  osal_wai_sem_tmo` 这一层。

### 34.2 自旋在做什么：GPU ioctl 风暴

- 在 shim 里加 `IOCTL_HIST`（`/usr/tool/ioctl_trace` 开启，每 5 万次打印
  请求码直方图 + fd）：
  ```
  IOCTL_HIST calls=450000
    req=80000006 n=210939 fd=4   (≈47%)
    req=8000000a n=114417 fd=4   (≈25%)
    req=80000004 n=106194 fd=4   (≈24%)
    req=80000005 n=8275  fd=4
    req=8000000b n=8359  fd=4
    ...
  ```
  → **~2.4 万次/秒** 的 `0x8000xxxx` ioctl，全部落在 **fd 4**（GPU/grm 设备），
  45 秒累计 110 万次。这就是 THREAD_INFRA1 的自旋。
- guest 侧每次 ioctl 在 TCG 下约 40–50 µs → 1.1M 次 ≈ 50 s 系统时间，
  正好对上 `system≈57 s`。

### 34.3 内核侧（与上节一致）

- `sched_clock` 22% / `utimer_read_freerun` 7.4% /
  `hwtimer_read_clock_cpu` 2.5%，合计约 1/3；`sctxt` 200 万次/70 s
  也主要来自这个自旋线程被抢占。

### 34.4 试过但无效的加速

| 尝试 | 结果 |
| --- | --- |
| `osal_throttle`（tmo==0 空轮询后 sleep 200 µs） | 无效（自旋在 GPU ioctl，不在 OSAL 层） |
| `-accel tcg,thread=single` | guest 本来只有 1 个 vCPU，耗时无变化（~92 s） |
| 静默 trace + maps 缓存（第 33 节） | 有效：user -30%，合计 CPU -15% |

### 34.5 下一步（明确的靶子）

1. 反汇编 guest 的 GPU 驱动（`/usr/kmod/dmpgles2.ko` / grm 相关模块），
   查 `0x80000006 / 0x8000000a / 0x80000004` 三个 ioctl 的语义；
2. 重点看 `0x80000006`（47%）：大概率是“查询 GPU/命令完成/空闲状态”，
   应用在轮询它；若 QEMU 的 Sugilite/GEM 模型没有把某个完成位/中断置起来，
   应用就会永远自旋；
3. 在模型里补齐该状态（或直接返回“已完成”）→ 轮询立刻退出，
   预计能把这 1 个核的 50 s 系统时间省掉大半；
4. 若确实是应用本身的设计（真机也轮询），那就只能靠 TCG 提速
   （QEMU 用 -O3 -march=native 重编、或更大 tb-size）来摊薄单次 ioctl 成本。

调试开关：`toggle-flag.sh ioctl_trace on|off`（直方图）、
`toggle-flag.sh osal_throttle on|off`（OSAL 空轮询节流）、
`toggle-flag.sh osal_trace on|off`（逐条 OSAL trace）。
采样工具：`profile-run.py`、`ctxt-top2.py`、`pc-hotspots.py`、
`prof-t151.py`。

### 34.6 更正：热 ioctl 其实是 /dev/uipc（不是 GPU）

- 给 ioctl 直方图加了 fd→路径解析（readlink `/proc/self/fd/N`）：
  ```
  IOCTL_FD req=0x80000006 fd=4 path=/dev/uipc
  IOCTL_FD req=0x80000004 fd=4 path=/dev/uipc
  IOCTL_FD req=0x8000000a fd=4 path=/dev/uipc
  ```
  之前把 fd 4 当成 GPU 是错的；GPU 的 ioctl 是 `0x800482xx/0xc00482xx`
  （shim 的 `GPUIO req=` 日志里能看到），量很小。
- 对照 libosal_uipc 的反汇编，`0x8000xxxx` 的低字节就是操作号，例如：
  `u_osal_sig_sem`→`0x80000002`、`u_osal_stat_msgq`→`0x80000003`、
  `u_osal_free_msg`→`0x8000000b`。热门的 `4/6/a` 对应
  **valloc_msg_wait / snd_msg / rcv_msg_tmo** 三个消息操作。
- 即 THREAD_INFRA1 在做 **~2.4 万次/秒的 OSAL 消息收发**
  （valloc 24% + snd 47% + rcv 25%）。真机上这些操作 ~1 µs，
  只占几个百分点；在 TCG 下每次 ~40 µs，于是吃满一个核。
- 这也解释了 `osal_throttle` 为什么无效：这些不是“空轮询返回”，
  而是真实成功收发的消息；加延时只会拖慢应用。

### 34.7 试过：放大 TCG TB 缓存

- `-accel tcg,tb-size=512` 一次运行：菜单 106 s、墙钟 169 s，比平常更慢，
  且该次 guest /proc 数据未更新（疑似又撞启动竞态），结论不成立；
  暂不采用。

### 34.8 下一步建议

1. 给 `osal_snd_msg/rcv` 加一条“按 (qid, msg[0]) 的直方图”（类似 IOCTL_HIST），
   看这 2.4 万条/秒消息到底发往哪个队列、类型是什么；
   若发现某个队列在 QEMU 里没有消费者（真机有对应服务），补上消费者，
   自旋自然会停；
2. 若确认是真机也有的高频消息（比如 UI 心跳），那就只剩 TCG 提速
   （QEMU 用 `-O3 -march=native` 重编、或把 guest 侧每次 OSAL 操作的
   固定开销摊薄）；
3. 短期可用性上，当前镜像（静默 trace + 定点采样）已经比最初快 ~15%，
   菜单 20–40 s 出现，交互每次按键数十秒。

### 34.9 真正的洪泛：ulogio 日志队列 0x8f013a

- 给 `osal_snd_msg` 加了按 `(qid, kind)` 的直方图（`/usr/tool/osal_hist`）：
  ```
  OSAL_HIST q=8f013a kind=1 n=1299764 w0=801b
  ```
  **130 万条日志消息**（约 2 万条/秒）全部发往 `0x8f013a`（ulogio 日志队列），
  其余队列都只有几十条。之前看到的 `/dev/uipc` ioctl 风暴（valloc/snd/rcv）
  就是这条日志流。
- 抓了消息内容（每 20 万条 dump 64 字节），是 ulogio 的二进制日志记录
  （`02 00 3c 00 …` 头 + 计数/时间戳），不是错误文本。
- 实验：在 shim 里对 `0x8f013a` 做 **200 条/秒的限流**（`/usr/tool/log_throttle`）：
  - 上下文切换从 **~220 万降到 21 万（−90%）**；
  - 但 guest 总 CPU 没有下降（user 反而升到 40.7 s）——限流路径里每条消息
    都做了一次 `clock_gettime` + `osal_free_msg`，把省下的又吃回去了；
  - 说明“日志太多”确实是现象，但当前限流实现方式不对，需要：
    ① 每 N 条才查一次时间（或纯计数桶），② 用 sleep 施加反压而不是丢弃。
- 该限流默认关闭（`toggle-flag.sh log_throttle off`），保持行为忠实。

**下一步（按收益）**：
1. 用“便宜的反压”重做限流：每 64 条检查一次速率，超限时 `nanosleep(1ms)`
   （让生产日志的循环自己慢下来，而不是我们逐条付 syscall）；
2. 查为什么固件在这个状态下会以 2 万条/秒写日志——很可能是某个
   错误/状态每帧都在打点，找到并让它不再发生（例如某个 emulated 设备
   一直报错）；
3. 若日志速率是固件常态，则 CPU 瓶颈就回到 TCG 单次操作成本上。

### 34.10 追调用者：遇到 -O2 把诊断代码优化掉

- 目标：找出是谁在往 `0x8f013a` 发日志。做法是给 `osal_snd_msg`
  interposer 记录调用者返回地址（`lr` 和 `lr-libobj_base`），每 5 万条打印一次，
  并顺带每 20 万条 dump 64 字节消息体。
- 现象：加了 `OSAL_CALLER` 后，运行时完全没有输出。检查发现
  **-O2 编译的 .so 里 `OSAL_HIST`/`OSAL_CALLER`/`GEMTIME` 这些字符串
  根本不存在**（`osal_hist_account.keys/cnts` 等静态变量倒还在）：
  ```
  -O0 size=108428 GEMTIME=1 OSAL_HIST=1
  -O1 size=118404 GEMTIME=1 OSAL_HIST=1
  -O2 size=124812 GEMTIME=0 OSAL_HIST=0
  ```
  即 clang -O2 把这些打印块消掉了（可能和 `put_s/inline+volatile` 组合有关），
  所以诊断版必须用 **-O1** 构建（`build-shim-diag.sh`）。
- 但 -O1 诊断版那次运行仍无输出：`/usr/tool/osal_hist` 标志在镜像里存在、
  `GPU_SHIM` 也打印了，但直方图没打印——需要先在 init 里加一行
  “OSAL_HIST on” 确认标志读到，再排查。
- 目前镜像已恢复为 -O2 生产版 shim（`gpu_shim_qemu_gotkey.so`），
  `osal_hist`/`ioctl_trace`/`log_throttle` 全部关闭。

**下一轮计划**：
1. 在 init 里给每个调试标志加确认日志；用 -O1 诊断版重新跑，
   拿到 `OSAL_CALLER lr/loff` 前几名；
2. 用 libObj 的符号表把 `loff` 解析成函数名，确认是哪个模块/子系统在打日志；
3. 按 A7M3 的 AP-only 思路裁掉对应服务/模块（或让它不再报错），
   再测 CPU/交互延迟。

### 34.11 本轮收尾：诊断构建不稳定，已回退到稳定版

- 在 shim 里加 `FLAG_LOG`（打印标志状态、把直方图周期改成 1000）后，
  用 **-O1 诊断版** 连续 5 次运行全部在启动竞态处 fault（rc=4）；
  换回 **-O2 生产版**（同一份源码）也是 fault。
- 判断：`OSAL_CALLER`（在 `osal_snd_msg` 里取 `__builtin_return_address(0)`
  并调 `libobj_base()`）和/或 `FLAG_LOG` 的改动让 shim 不稳定。
- 已用备份 `payloads/gpu_shim_qemu.c.bak-good` 回退到**已知稳定版**
  （静默 trace + maps 缓存 + 定点采样），重编并安装，
  `osal_hist/ioctl_trace/log_throttle` 全部关闭；
  连续 3 次尝试中第 3 次成功（菜单 102 s、无脚本错误），
  说明稳定版可用，但启动竞态本身命中率偏高（本轮 2/3 失败）。
- 未完成：日志调用者归属（`OSAL_CALLER` 没跑起来就回退了）。

**下一轮改用无侵入方式归属日志调用者**：
1. 静态分析 libObj：日志消息头是 `w0=0x3c0002`，在 libObj.so 里搜这个常量，
   找到构造 ulogio 消息的函数及其调用者；
2. 或用 QEMU 侧 PC 采样只统计“刚调用过 osal_snd_msg 的线程”的 PC，
   不修改 shim 热路径；
3. 定位到具体模块后，再决定裁服务（A7M3 的 AP-only 做法）还是修状态。

## 35) 关键提速：跳过 ulogio 日志洪泛（2026-11-12 晚）

### 35.1 做法

- 在稳定的 shim 上给 `trace_osal_snd_msg` 加一条**极廉价**的过滤：
  ```
  if (qid == 0x8f013a) {        /* ulogio 日志队列 */
      if ((n++ & 63) != 0) {    /* 只放行 1/64 */
          osal_free_msg(qid, msg);   /* 其余直接释放、不发送 */
          return 0;
      }
  }
  ```
  没有 `clock_gettime` 之类的额外 syscall，热路径只多一个计数器。
- 开关：`/usr/tool/log_skip`（已默认打开）；关掉即恢复全部日志。

### 35.2 实测对比（同 profile-run 流程）

| 指标 | 开 log_skip（ls2） | 关（st3，同机同流程） |
| --- | --- | --- |
| 菜单出现 | **32.5 s** | 102.5 s |
| 墙钟 | **94.6 s** | 164.6 s |
| guest user | 30.1 s | 37.9 s |
| guest system | **48.9 s** | 111.5 s |
| 合计 CPU | **79.0 s** | 149.4 s |
| 上下文切换 | **283,334** | 3,802,840 |

⇒ 合计 CPU **−47%**、上下文切换 **−93%**、墙钟 **−43%**。
（启动竞态仍有噪声，单次数据不代表稳定值，但方向明确。）

### 35.3 UI 正确性

- ls2 运行：`viewSettingMenu` 映射 7 次、`viewCaution` 6 次、无 exception，
  菜单 / 按键 / caution 页都正常。
- 代价：丢了 63/64 的 ulogio 日志（诊断信息），对交互没有影响。

### 35.4 还能继续省的部分

- 现在每条被丢掉的日志仍然先经过 `osal_valloc_msg_wait`（一次 ioctl）
  再由我们 `osal_free_msg`（再一次）。下一步可以在
  `trace_osal_valloc_msg_wait` 里对 `0x8f013a` 直接返回一个**静态哑缓冲区**
  （带合法 `[msg-4]` 头），snd 时识别哑指针直接丢弃、free 时忽略，
  这样连 valloc/free 的两次 ioctl 都省掉，预计还能再降一截。

### 35.5 哑缓冲区（valloc 旁路）实测：反而更慢

- 实现了 `DUMMY_MSG`：`osal_valloc_msg_wait` 对 `0x8f013a` 直接返回静态缓冲区，
  `osal_snd_msg` 识别哑指针丢弃，省掉 valloc/free 两次 ioctl。
- 实测（dm1）：菜单 49.5 s、墙钟 111.5 s、user 29.4 s、system 66.1 s、
  合计 **95.5 s**、ctxt 169k —— 比 1/64 跳过（79.0 s）**更慢**。
- 原因：valloc 的 ioctl 原本起到了**限流**作用；把它去掉后，日志产生循环跑得更快，
  单位时间产生的日志更多，CPU 反而上升。
- 结论：valloc 旁路用 `/usr/tool/log_dummy` 单独控制，**默认关闭**；
  生产配置仍是“真 valloc + snd 丢弃 63/64”。
- 复核（gl1，默认配置）：菜单 31.5 s、墙钟 93.6 s、user 29.8 s、system 48.0 s、
  合计 **77.8 s**、ctxt 280k、`viewSettingMenu` 7 次 / `viewCaution` 6 次、
  无 exception —— 与 ls2 一致，可复现。

### 35.6 当前最佳配置

- shim：静默 trace + maps 缓存 + 定点采样 + `log_skip`（snd 丢弃 63/64）。
- 开关：`/usr/tool/log_skip`（开）、`/usr/tool/log_dummy`（关）。
- 效果：相比第 33 节的基线，合计 guest CPU 约 **−48%**，上下文切换 **−93%**，
  墙钟 / 菜单时间大约减半。
- 下一步候选：按线程把“日志产生循环”的其余开销也压掉（valloc 不能去，
  但可以对它做**每 N 条只真分配一次**的计数节流），或者回到内核 timer 路径。

## 36/37) 引导链与恢复通道（本公开版省略）

那部分内容涉及**绕过设备保护措施**，不适合公开发布，因此本公开版整体删除，
只在私有笔记里保留。

这里仅保留可公开的结论：设备存在一个硬件级恢复通道，分区布局与备份策略已在
私有测试记录中归档；本项目**不提供**任何绕过签名/解锁的工具或数据。

---

## 38) 菜单"触屏化"探索（2026-11-12 晚）

### 38.1 结论

- **原生触摸改菜单不可行**：A7R3 的触摸只在**对焦**场景生效（用户确认），
  菜单视图虽然引用了 `InputService::callback_tp` / `setTpEnableArea`，
  但菜单导航是按键式。
- 已实现的可行替代：**点击 → 按键适配器**（在 live-menu 网页里点菜单行，
  自动把选中项移动过去；点已选中行 = ENTER）。

### 38.2 固件侧证据（触摸栈存在，但菜单不接）

- `libObj.so`：`infra_tp_init/exit/res/sus`、`InputService::callback_tp_raw/
  callback_tp`、`setTpEnableArea`、`setResourceTpSettings`、`forceReleaseTp`
- `viewUnified2.so`：`PAS_MenuSelectMode::setTouchable(bool)`、
  `PAS_TouchPosition::cast`、`PAS_ZoneArea_C::getTouchQuadrant`、
  `CmnViewTPAreaEnableUtil::*`、`CameraTouchAf`、`TouchShutter`、
  `cmnViewSettingNodeRootTouchOperation(Range)`
- 逆出的触摸事件布局（24 字节 `_InfraInputEvent`）：

  ```
  +0x00  u32  type = 0x11013001
  +0x04  u16  x
  +0x06  u16  y
  +0x08  u32  state = 0x80000001（按下）/ 0x80000002（抬起）
  ```

  UI 侧对应事件 id `0x11013008`，参数 9 = (x,y)。

### 38.3 实验：队列注入触摸（无效果）

- 新增 guest 工具 `inject-touch`（`analysis/payloads/inject-touch-a7r3.c`，
  已装到镜像 `/usr/tool/inject-touch`），payload 就是上面的 24 字节结构。
- 结果（OSAL 全部 rc=0，但**无任何重绘**）：

  | 队列 | type | 结果 |
  | --- | --- | --- |
  | `0x14a0429` | `0x11013001` | 无反应 |
  | `0x7a0020` | `0x11013001` | 无反应 |
  | `0x14a0429` | `0x11013008` | 无反应 |

  ⇒ 裸事件投递不经过应用自己的 `callback_tp_raw` 转换，SDK 不认。

### 38.4 已实现的适配器（live-menu 点击）

- `analysis/qemu_a7r3/menu_rows.py`：从菜单帧里识别 6 个菜单行区间 +
  橙色高亮行（实测：行 0..5，选中行 1，高亮带 177..222）。
- `analysis/qemu_a7r3/live-menu.py`：新增
  - `/touch?x=&y=` 端点：把点击 y 映射到菜单行，与当前选中行比较，
    发送 N 次 DOWN/UP；点在已选中行则发 ENTER；
  - 网页 `<img onclick>` 点击处理（坐标按 640×480 换算）。
- 离线验证：用 `tp1_frames/frame_9004.raw` 转出的真实菜单帧，
  行检测与高亮检测都正确。

### 38.5 端到端验证成功（ck2，2026-11-12 晚）

- 在 `inject-serial.py` 里加了 `A7R3_CLICK_Y` 测试模式：
  等 UI ready 后重试取最新的非黑菜单帧（最多 12 次、每次 2 s），
  用 `menu_rows` 算出"当前选中行/点击目标行"，再发 DOWN/UP 或 ENTER。
- 运行结果：
  ```
  click probe 2: frames=7 rows=6 sel=1 target=3
  click -> 0x30 x2 (row 1 -> 3)
  ```
- 逐帧验证（高亮行位置）：
  | 帧 | 高亮 y | 选中行 |
  | --- | --- | --- |
  | frame_9004（点击前） | 177 | 1（自动关机开始时间）|
  | frame_9005（1×DOWN） | 227 | 2（自动关机温度）|
  | frame_9006（2×DOWN） | 277 | 3（**NTSC/PAL选择器**）|
  | frame_9007 | 277 | 3（稳定）|
- 视觉确认：`ck2_9006.png` 高亮已落在"NTSC/PAL选择器"行。
  ⇒ **"点击菜单行 → 选中项跳到该行"的触摸式操作已经跑通。**

### 38.6 遗留

- `live-menu.py` 网页版仍受"帧全黑"问题影响（同一镜像用
  `inject-serial.py` 跑就有正常帧）。猜测是 live 脚本的启动/串口时序
  让应用少画了几帧，导致菜单帧从未被 dump。
- 复现点击的可靠方式：`A7R3_CLICK_Y=<y> bash run-click-test.sh <tag>`。
- 下一步：修 live-menu 的帧路径（对齐 inject-serial 的等待时机），
  再把 `/touch?x=&y=` 接到网页上做实时点击。

### 38.7 进一步：跳动速度与弹出框（2026-11-12 夜）

**窗口/弹出框现在能识别了。** `menu_rows.py` v2 的过滤规则：
忽略 tab 栏（y<70）与底栏（y>434）、忽略居中标题（文字中位 x>250）、
用"橙色高亮条的中心"匹配最近的行（而不是高亮条顶端）。实测：

| 帧 | 高亮 | 识别结果 |
| --- | --- | --- |
| 菜单 `ck2_9006` | y=277 | 6 行，选中第 3 行（NTSC/PAL）✓ |
| 对话框 `dlg5_9013` | y=96 | 2 行（开/关），选中第 0 行（开）✓ |
| 对话框 `dlg6_9014` | y=146 | 2 行，选中第 1 行（关）✓ |

**关于"只能一行行跳"**：固件本身没有"跳到第 N 行"的接口，只有
UP/DOWN 键事件，所以底层必然是一行行走；但适配器做了两件事把它藏起来：

1. **一次发一整串** 按键（`inject-key … ; sleep G ; …`）；实测 `G=1.0s`
   可靠，`G=0.2s` 会丢事件（4 次 DOWN 只生效 2 次）。
2. **发完重新读帧、只补差值**（verify-and-converge）：日志里能看到
   `row 1→5` 只走到 3，下一次点击补 `row 3→5`，再下一次才是 ENTER。
   用户视角就是"点一下，选中项自己走到那一行；再点一下激活"。

**弹出框点击**：已实现"点选项行 → 移到该选项；再点一次 → ENTER 确认"。
离线帧检测已验证（见上表）；现场一次 `dlg6` 里点 y=180 成功把选择从
"开"移到"关"（帧 17→18），但随后一帧是高亮缺失的过渡帧，导致收敛
逻辑判定失败。需要再修：过渡帧过滤（连续两帧一致才采信）。

**尚未解决**：
- 对话框动画中途的帧会导致识别失败 → 需要"连续 N 帧一致才判定"的策略；
- live-menu 网页路径的帧仍是黑的（用 inject-serial 路径可靠）。

### 38.8 空点 = ENTER + 顶部标签栏可点（2026-11-12 深夜）

统一成 `menu_rows.classify(png, x, y)`，返回四种动作：

| 判定 | 条件 | 动作 |
| --- | --- | --- |
| `tab` | y<62，检测到高亮标签 ≠ 点击的标签格 | UP → LEFT/RIGHT×N → DOWN |
| `move` | 有 ≥2 行，点击位置在某行 | UP/DOWN×|delta| |
| `enter` | 点已选中行 | ENTER |
| `enter` | **y>434（底栏/空白）、或行数<2（消息框/过渡帧）** | ENTER（确认/取消）|

- 顶部标签高亮检测：按 7 格求平均色，取 `r-b` 最大且 `r>90`、`r-b>25` 的格子。
  实测菜单帧：tab 4 高亮（均值 117,103,62），识别正确。
- 空点=ENTER 直接解决"此功能当前被禁止"这类**没有可点选项的提示框**：
  在 `cau7` 测试里，点击第 1 行 → 弹出 caution（日志 viewCaution.uxc ×6），
  再点底栏空白（y=470）→ 发 ENTER → 回到菜单（帧 9008 又是菜单），
  说明提示框被鼠标"点空"关掉了。
- 防误触发：`enter` 需要连续两次识别结果一致才执行；
  点击前还会先等"画面里出现 ≥2 行"的稳定帧（避免把开机黑帧当成消息框）。
- 离线分类自测（`test-classify.py`）全部符合预期：

  ```
  menu tab click      -> tab from 4 to 0
  menu row 3 click    -> enter（已选中）
  menu row 0 click    -> move -3
  dialog option 关     -> move +1
  dialog option 开     -> enter
  empty area (footer) -> enter
  black frame         -> enter
  ```

- `live-menu.py` 的 `/touch` 已改为调用同一个 `classify()`，行为与测试脚本一致。
- 尚未实测：顶部标签页切换（逻辑已离线验证，需一次完整运行确认
  UP→LEFT/RIGHT→DOWN 的焦点路径）。

### 38.9 live 网页路径修通 + 标签切换实测（2026-11-12 深夜）

**live 黑帧的根因（已解决）**：模型只在 blit（present）时 dump
`frame_9000+n`；某些启动时序下整个会话只发生 3 次 blit（9000..9002），
全是清屏黑帧，菜单那次 present 没发生 → 网页永远是黑的。
对比：同一镜像用 `inject-serial.py` 跑会 dump 5 次以上，能拿到菜单帧。

**修法**：live-menu 在 "menu ready" 之后检查最新帧，若没有可识别行
（rows<2），就**主动注入一次 DOWN+UP 触发重绘**（最多 3 次）。
实测 `/tmp/liveF`：重绘后 frames=7、rows=6、sel=1、tab=4，全部识别成功。
副作用：启动时选中项会短暂下移一行再回原处（DOWN+UP 抵消）。

另外给 live-menu 加了**独立串口读线程**（全速 drain 控制台），
并把启动序列对齐 inject-serial（PROBE/viewprobe/3s settle）。

**HTTP 实测**（`/tmp/liveH`，端口 8781）：

```
GET /touch?x=90&y=30   -> {"ok": true, "action": "tab", "from": 4, "to": 0}
```

- 返回动作正确，但**切换后的帧里 tab 仍是 4**（顶部工具包图标依然高亮），
  只有选中行回到了第 0 行。说明 `UP → RIGHT×4 → DOWN` 这套键序列
  没有真正切换分类标签。
- 猜测：标签栏的左右切换用的不是 0x15/0x1AF，或者焦点没真正落在标签栏
  （UP 可能进的是别的焦点），需要用"UP 后单步 RIGHT + 抓帧"来现场确认。

**当前结论**：
| 功能 | 状态 |
| --- | --- |
| 菜单行点击选中/激活 | ✅ 已验证（ck2 / dlg5）|
| 弹出框选项点击 | ✅ 检测正确（离线），现场移动过一次 |
| 空点 = ENTER（关提示框） | ✅ 已验证（cau7：viewCaution → 空白点击 → 回菜单）|
| live 网页帧 | ✅ 已修（强制重绘）|
| 顶部标签切换 | ⚠️ 动作已发出，但键码/焦点不对，未真正切换 |

### 38.10 标签焦点路径实测（2026-11-12 深夜）

逐步按键 + 抓帧，结论明确：

1. **UP 是从列表往上走**：初始选中第 1 行（自动关机开始时间），按 1 次 UP
   只是到第 0 行（显示质量），**不是**到标签栏；
2. **从第 0 行再按一次 UP 才进标签栏**（`tab2` 测试：UP、UP、RIGHT）；
3. **标签栏里 LEFT/RIGHT 确实切分类**：`tab2` 最后一帧显示
   **星形图标获得焦点、标题变成"我的菜单1"**（tab 4 → tab 5）✓；
4. **在列表里 LEFT/RIGHT 切的是"页"**：`tab1` 里 RIGHT 让
   设置2/7 → 设置3/7（标题"设置3"，首行变成"触摸屏/触摸板"），
   所以顶部 ◀ 2/7 ▶ 是页号，不是分类号。

**标签高亮有两种样式，检测要分开**（已实现）：
| 焦点位置 | 当前分类图标的样式 |
| --- | --- |
| 在列表里 | 图标底色偏黄（r-b 大）|
| 在标签栏 | 图标外面有浅灰/白焦点框（中性色、亮度高）|

`detect_tab()` 现在返回 `(index, on_tab_bar)`，`classify()` 据此决定
要先按几次 UP：`ups = 行号 + 1`（已在标签栏时 `ups = 0`）。

**端到端结果（tab3）**：从 tab 4 点 tab 0，判定
`{'kind': 'tab', 'from': 4, 'to': 0, 'delta': -4, 'ups': 2}`，
发了 `UP,UP,LEFT×4,DOWN`（间隔 1 s），最终画面停在**视频/播放分类（tab 3）**、
标题"播放1 1/4"——即 **4 次 LEFT 只生效了 1 次**，其余被丢掉。
⇒ 标签栏的按键需要更长的稳定时间，下一步把标签序列的间隔调到 2 s 再测。

**顺带发现**：列表内 LEFT/RIGHT 是翻页，这给"点页号箭头翻页"也留了接口。

### 38.11 菜单翻页的触屏方案（已验证，2026-11-12 深夜）

顶部标题行右侧是 `◀ 2/7 ▶`（页号）。触屏支持直接点箭头翻页：

- `classify()` 新增 `page` 动作：`62 ≤ y ≤ 128` 时，
  `x ≥ 605` → 下一页（RIGHT `0x1AF`），`540 ≤ x ≤ 585` → 上一页（LEFT `0x15`），
  标题中间区域返回 `none`（不误触）；
- 两个 harness 都实现了 page 动作（`inject-serial` / `live-menu`）；
- **实测（page2）**：点 `(620, 90)` → 判定 `{'kind':'page','delta':1}`
  → 发送一次 RIGHT → 画面从 **设置2/7** 变成 **设置3/7**（标题"设置3"、
  底部页点移到第 3 个）✓

分类自测（菜单帧）：

```
right arrow (620, 90) -> page +1
left arrow  (570, 90) -> page -1
header title(320, 90) -> none
row 3       (320,300) -> enter
footer      (320,470) -> enter
tab bar     ( 90, 30) -> tab 4 -> 0 (ups=4)
```

**触屏交互目前的完整映射**：

| 手势/点击位置 | 行为 |
| --- | --- |
| 顶部标签栏（y<62） | 切分类：UP×(行号+1) → LEFT/RIGHT×差值 → DOWN |
| 标题行左箭头（x 540-585） | 上一页（LEFT）|
| 标题行右箭头（x≥605） | 下一页（RIGHT）|
| 列表某行 | 移动选中项（逐行，自动补差值）|
| 当前选中行 | ENTER |
| 弹出框选项 | 移动/ENTER |
| 底栏/空白/消息框 | ENTER（确认/取消）|

### 38.12 标签切换：慢，但确实一步一切（2026-11-12 深夜）

`tab7` 逐步测试（UP、UP、LEFT、LEFT、LEFT，每步间隔 15 s）：

| 步骤 | 帧数 | 结果 |
| --- | --- | --- |
| UP | 6→8 | 选中行 1→0 |
| UP | 8→10 | 进入标签栏（星形/焦点框）|
| LEFT | 10→18 | 分类 4→3（**播放1**）|
| LEFT | 18→26 | 分类 3→1（**动态影像1**，camera-2 图标高亮）|
| LEFT | 26→41 | 继续切换 |

⇒ **每次 LEFT 只切一个分类，但切分类的重绘在模拟器里要 10–15 s**。
间隔 1 s / 2.5 s / 8 s 时，只有第一个键生效（tab3/tab5/tab8 都停在
tab 3）；15 s 时每一步都生效。

**已改**：标签序列改用 `A7R3_TAB_GAP`（默认 15 s）作为按键间隔；
行/页/ENTER 仍用较快的 `A7R3_CLICK_GAP`（默认 1 s）。

**代价**：点顶部标签跨 4 个分类约需 60 s（每步 15 s），这是软件渲染的
性能限制，不是逻辑问题。可选的加速方案：
1. 闭环：每发一个键后轮询帧，检测到分类变化立刻发下一个（省掉固定等待）；
2. 降低每次切换的重绘量（模型侧优化），但这属于渲染性能问题。

**待验证**：带 15 s 间隔的完整点击流程在 `tab9` 撞上启动竞态未跑完，
需要下一次成功运行确认最终停在目标分类。

### 38.13 标签切换改为闭环（2026-11-12 深夜）

`inject-serial.py` 与 `live-menu.py` 的 `tab` 动作都改成**闭环**：

```
到标签栏：UP × ups（每次间隔 2.5 s）
循环 abs(delta) 次：
    发一次 LEFT/RIGHT
    每 1.5 s 抓一次最新帧，用 detect_tab 看当前分类
    直到分类变成期望值（最多 10 次尝试），再发下一个
最后 DOWN 回列表
```

- 这样不用死等固定 15 s，而是"切完立刻下一个"，总耗时 ≈ 实际动画时间；
- 返回结果里带 `steps`（实际成功的步数），卡住时会打印
  `[!] tab step stuck at …`，便于定位；
- `A7R3_TAB_GAP` 不再用于点击流程（保留给脚本式测试）。

**验证状态**：最近几次运行 `tab9/tab10` 都撞上启动竞态
（`Entering exception monitor`，~1/3 概率的事件最近偏高），
闭环逻辑本身还没拿到一次干净的成功运行；下一次成功运行即可确认。

**目前触屏映射完整度**：

| 功能 | 逻辑 | 端到端 |
| --- | --- | --- |
| 行选中 / ENTER | ✅ | ✅ ck2 |
| 弹出框选项 | ✅ | ✅ dlg5（移动）/ 部分 |
| 空点关提示框 | ✅ | ✅ cau7 |
| 翻页箭头 | ✅ | ✅ page2 |
| 顶部标签切换 | ✅（闭环）| ✅ tcl1（4/4 步）|

### 38.14 标签闭环验证成功（2026-11-12 深夜，tcl1）

- 关键修复：`detect_tab()` 原来只认"黄色"，但每个分类的选中底色不同
  （相机1 红、相机2 紫、地球/视频 蓝、工具包 黄、星形 白）。
  改成通用评分：`score = (max-min) + 平均亮度`，阈值 100；
  `sat <= 20` 时判定焦点在标签栏（灰白框）。
- 验证结果：
  ```
  click y=30 -> TAB 4 -> 0 (4/4 steps)
  frames: 5 -> 42（每步都等到分类真正变化才发下一个）
  ```
  最终帧 `tcl1_9034.png`：**相机1（tab 0）高亮**，标题
  "影像质量/影像尺寸1 1/14" ✓
- 闭环总耗时约 26 s（4 步），比固定 15 s/步（60 s）快一倍多。

**至此触屏映射全部打通**：

| 功能 | 端到端验证 |
| --- | --- |
| 点菜单行 → 选中 / 再点 → ENTER | ✅ |
| 弹出框选项点选 | ✅ |
| 点空白 → ENTER（关提示框）| ✅ |
| 点标题行 ◀/▶ → 翻页 | ✅ |
| 点顶部标签 → 切分类 | ✅ |
| 网页 live 路径（强制重绘 + 串口读线程）| ✅ |

---

## 39) "新菜单"：ZV-E10 的成品做法 + A7R3 可行性（2026-11-13 晚）

素材：`zve10-qemu/`（同 SoC 的参考工程）。结论分两部分：**人家怎么做的**、**我们能不能照做**。

### 39.1 ZV-E10 有两条"新菜单"路线，只有一条成功

| 路线 | 做法 | 结果 |
| --- | --- | --- |
| A：整套换栈 | 把 ZV-1M2（Gen5）`appFw.so` 64 MB + `libObj` 3.9 MB + `share/app` 全换 | ❌ QEMU 里 0 像素 |
| B：注入叠加 | `LD_PRELOAD` 库在原生 UI 里加一行 + 自己 CPU 画 A7C2 风格菜单 | ✅ 三帧与上游逐字节相同 |

路线 A 的实测结论（`zve10-qemu/docs/QEMU_FRAME_VERDICT_2026-11-13.md`）：
启动链全通、`createLayer` 6 次全成功、`uxengine_init2` 返回非 0、画布 614400 B 已映射，
但 `updateFrame` 的 PLT 槽 **never-called**，画布前 2 KB 恒零 ⇒ **从未进入绘制循环**。
另有单变量对照：`av-cam.bin`（MUSASHI RTOS）存在时主流程停在 t=120，移除后到 t=1125
⇒ 单核上 MUSASHI 主循环与 UI 主流程互斥。**这条路线对我们没有参考价值。**

### 39.2 路线 B 的实际机制（`zve10-qemu/tools/zve10/menu/`）

产物 `libnewmenu.so`（139,744 B，真机 slim）/ `libnativeentry.so`（QEMU full）。

```
LD_PRELOAD=/setting/mode/libnewmenu.so  (A7R3 同款 bootin preload 链)
  ├─ hook dlopen@GLIBC_2.4 / dlsym（★必须带符号版本，否则真机一条都不触发）
  ├─ 等 viewUnified2 / CautionConfig / libObj 加载，按 12 字节 prologue 校验后
  │  inline hook 4 个函数：CmnViewSettingNode 构造 / populate / 主循环(输入) / row 构建
  ├─ 只在 caller == views_base+0x301921 这一处：
  │    clone `cmnViewSettingNodeRootSetupVersion` 节点 → 改属性表首项 value=NE_TITLE
  │    → 作为第 6 个 child 插入 → 原生设置页多一行 "New Menu" 并自动聚焦
  └─ 进入后：吞掉原生按键（*action=-1）→ 喂自己的 menu_state → ne_force_frame()
       触发原生重绘 → 在 present 钩子里把自绘 640×480 RGBA4444 按 8×8 tile 重排叠上去
```

- 画的部分是 `menu_shell.c`（纯 CPU：矩形 / sprite + alpha 混合）+ `original_assets.h`
  （395 KB，从 **A7C2 固件**烘出的 layout 表 44×13、tab 图、标题 alpha mask、配色）。
- `ui-present.h` 确认原生 present 就是 `640*480*2` RGBA4444 + 8×8 tile 重排
  （`tile = (y>>3)*80 + (x>>3)`，`z` 位交织），与我们 A7R3 的帧布局同一族。
- 验收：父页 / ENTER 后 / MENU 后三帧 sha256 与上游 `a7c2-re` 完全一致。
- 样子见 `zve10-qemu/docs/ref-frame-001.png`（8 个顶部图标 + 日文列表）。

### 39.3 A7R3 侧实测（`dumps/nflasha15.bin`，/usr 全量镜像）

用 `analysis/ext2_locate.py` 把符号串偏移映射回文件：

| 符号 | 所在文件 |
| --- | --- |
| `_ZN18CmnViewSettingNodeC1EPPS_iPK24CmnViewSettingProperties` | CautionConfig.so + viewUnified2..8.so |
| `cmnViewSettingNodeRootSetupVersion` | CautionConfig.so / viewUnified2.so |
| `_ZN18CmnViewSettingNode22setItemSelectedByIndexEi` | CautionConfig.so |
| `GRM_bitmapCreate` / `GRM_gpermRectblit` / `GRM_bitmapWriteLockBits` / `GRM_screenGetOnBitmap` | libObj.so |
| `_ZN9GEN_Label7setTextEj` | libObj.so |
| `viewSettingMenu.uxc` | global.xdb |

⇒ **A7R3 与 ZV-E10 是同一套设置节点树 + 同一套 GRM 位图 API**，注入式新菜单在结构上可迁移。

### 39.4 A7R3 反而比 ZV-E10 好办的地方

1. **QEMU 已经出帧**，格式确定（640×480 RGBA4444 + Morton/8×8 tile）；
   ZV-E10 当时还在猜 `/dev/streamfb` 的像素格式。
2. **键码一致**：MENU 0x1ab / ENTER 0x1ac / 下 0x30 / 上 0x3 / 左 0x15 / 右 0x1af
   （与 A7C2、ZV-E10 全同族）。
3. **注入链已真机验证**：service mode + `dmode=3p` + `/setting/mode/preload`，无签名校验（§动态注入）。
4. **A7C2 素材直接用**：A7C2 屏也是 640×480，`original_assets.h` 原样编译。
5. **触屏已打通**（§38），新菜单天然可点。

### 39.5 必须重做的部分（ZV-E10 常量不可照抄）

- 4 个 hook 的 prologue 字节与偏移（A7R3 libObj 19.4 MB vs ZV-E10 21 MB，地址全不同）。
- 设置树形状：父节点 `count==5`、version 属性表 6 项 `{key,kind,value}`、`last==6`、
  `vptr == base+0xfab968`、caller `views_base+0x301921` —— 全是 ZV-E10 专属，要实测。
- 原生按键 `function` 码表（1..6 的语义）。
- present 抓取点：A7R3 走 `grm_gles` ioctl / Sugilite，不是 ZV-E10 的 GPE type6。
- 标题字符串注入点（`GEN_Label::setText` 已在 libObj，但 hook 点不同）。

### 39.6 里程碑

```
M0  只读探针 .so（零改动）：dump 模块基址 + 设置树节点 + properties 表 + 按键 function 码
M1  移植 ne_add_row，证明原生菜单里多出第 N 行 "New Menu"
M2  接 menu_shell.c + A7C2 素材，present 钩子叠加，QEMU 截图证明
M3  按键/触摸闭环驱动（§38 工具直接复用）
M4  真机 preload（service mode 改 dmode=3p + preload，一次重启；回滚 change_mode.sh 3）
```

### 39.7 边界

- 这是**叠加层自绘**：原生菜单仍在，我们只是盖上去；不是"换固件 UI"。
- 新菜单里的项若要真改相机参数，仍需回调原生 API（PARAMID / Backup），属额外工作。
- 另有一条**更轻的路线**：只改 `view*.uxc` / `global.xdb` / 配色表做换皮（数据驱动，风险最低），
  但视觉上不是"新菜单"，只是重排/改色。

---

## 40) M0 探针：A7R3 设置树实测（2026-11-13 夜）

目标：把 ZV-E10 那套"注入一行"路线搬到 A7R3 之前，先测清 A7R3 的设置节点树到底长什么样。
全部证据来自本机 QEMU + 我们自己的镜像。

### 40.1 新工具（`analysis/qemu_a7r3/`）

| 脚本 | 作用 |
| --- | --- |
| `probe-symbols.py` | 从 `dumps/nflasha15.bin` 直接读 `.dynsym`，按 needle 列 DEF/UND 与地址 |
| `gen-root-names.py` | 生成 `payloads/probe_menu_roots.h`（456 个 root 名） |
| `hexdump-sym.py` | 反查某个符号在文件里的真实机器码（判断能否 inline hook） |
| `dump-roots.py` | 解析 root 原型（.bss，静态读不到，仅作参考） |
| `img-part.py` / `img-part-table.sh` | 列镜像分区与挂载偏移（/setting=img2，/tmp=img10，/log=img11，/usr=img15） |
| `read-guest-file.sh` | 从镜像里取出 guest 写到 /setting 的文件 |
| `build-probe-menu.sh` | 构建 `payloads/probe_menu.so`（soft-float ARM，freestanding） |
| `install-probe-menu.sh` | 装进 /usr/tool，并把 preload 改成 `probe_menu.so:libefence.so` |
| `run-probe-menu.sh` | 启动 → 取日志 → 自动重试已知的 boot race |
| `set-probe-hook.sh on/off` | 开关 `/setting/probe_hook`（是否允许代码 hook） |

### 40.2 静态：符号都在（关键结论）

`libObj.so`（DEF）：
```
GRM_bitmapCreate 0x636bd9   GRM_bitmapWriteLockBits 0x636e6d   GRM_bitmapWriteUnlockBits 0x636e7d
GRM_gpermRectblit 0x636047  GRM_gpermFillrect 0x635f0d         GRM_screenGetOnBitmap 0x6379e1
GEN_Label::setText(unsigned) 0x3ba171                          Application_System_Init 0x82f1c5
```
`CautionConfig.so`（DEF）：
```
456 个 cmnViewSettingNodeRoot*（每个 40 B）
CmnViewSettingNode 全套 API（43 个）：initSettingNode/getSubItemByIndex/setItemSelected/
  getProperties/getCountAll/getRootItem/setItemDisable/isItemGrayout ...
基类构造 _ZN18CmnViewSettingNodeC1EPPS_iPK24CmnViewSettingProperties @0x673cd9  ← 与 ZV-E10 同名同签名
约 900 个 CmnViewSettingNode*Page*Group* 子类构造（每个 52 B）
```
`viewUnified2.so`：导入全部 root；DEF `CmnViewGenOpenUtil::openMenu` @0x2b0299、
`PAS_MenuPopup::openMenuPopup` 等。

⇒ **A7R3 与 ZV-E10 是同一套 `CmnViewSettingNode` 框架，连基类构造符号都一字不差。**

### 40.3 运行时：节点布局与属性表（实测）

节点布局与 ZV-E10 完全一致（sizeof 0x28）：
```
+0x00 vptr        +0x10 parent      +0x20 physical
+0x04 children**  +0x14 depth       +0x24 state
+0x08 count       +0x18 selected
+0x0C properties* +0x1C selectable
```
属性表：`{const record *records; int count;}`，record = `{int32 key, int32 kind, uint32 value}`（12 B）。
`key=0` 是标题字符串 ID（`0x0204xxxx`），`kind 0=字符串 / 1=整数`。

实测样本（guest 内读到，t≈76 s）：
```
PM_ROOT cmnViewSettingNodeRootSetupVersion cnt=0 n=6
PM_P [0] key=0x0  kind=0 val=0x204e146     ← 标题
PM_P [1] key=0x6  kind=0 val=0x204e30d     ← 名称
PM_P [2] key=0xb  kind=1 val=0x2
PM_P [3] key=0xc  kind=1 val=0x4
PM_P [4] key=0xe  kind=1 val=0xcd
PM_P [5] key=0x20 kind=1 val=0x0
```

**★ 语义更正（与 ZV-E10 不同）：`properties->count` 是"记录条数"，不是"最后下标"。**
ZV-E10 的移植代码按 `last+1` 读，在 A7R3 上会多读一条（表现为 key=0、值为下一张表的
标题或 ASCII 残留，如 `0x6d433434`="44Cm"）。A7R3 必须按 `count` 条读。

规模：456 个 root 全部可解析（null=0）；t≈76 s 时 371 个已建子节点。
子节点数 = 该设置的**可选值个数**：`Aperture=50`、`CreativeStyle=91`、`ChSet=15`、
`ContrastAF_FocusSet=14`、`AfAreaContrastdetectAF=6`。

### 40.4 ★ parent 指向"注册表"，不是页面

每个 item 的 `parent` 都指向同一个 **456 项注册表节点**（`cnt=456`，在 CautionConfig 数据段），
而不是"页面 → 组 → 行"的父节点。⇒ **从 item 往上走找不到页面节点**，ZV-E10 那种
"沿 parent 找到 count==5 的父页"在 A7R3 不成立。

### 40.5 inline hook 实验（结论：不要 hook）

| 目标 | 代码 | 结果 |
| --- | --- | --- |
| 基类构造 `C1EPPS_iPK24...` | `b5f0 4604 4d05 af00` | ❌ 含 PC 相对 `ldr r5,[pc,#20]`，8 字节搬运无法重定位（跳飞） |
| `initSettingNode` | `6803 e92d41f0 af00` | 可重定位，hook 安装成功（rc=0），但**全程 0 次调用**（不在建页路径上） |

更重要的教训：**在活线程上打 8 字节补丁会让 UI 崩**。装好 hook 后 5 次运行全部
`page translation fault`（地址如 `0x1e9fcde6`、`0x152c38`），即使 wrapper 一次都没进；
去掉 marker 回到纯只读后，运行 `fault=0`、`PM_DUMP` 正常完成。

⇒ **M0 结论：A7R3 这条路上"改原生视图树"性价比很低。**
页面/组节点只在构造瞬间存在、抓不到；而 item 原型 + 属性表 + 完整 API 都是导出可用的。

### 40.6 因此 M1 的路线改为"叠加层"

不做原生行注入，改成：

```
① 自绘：menu_shell.c + A7C2 素材（640×480 RGBA4444）→ 由我们自己的渲染路径贴到帧上
② 输入：hook/拦截 snd_InfraInputEvent_to_InputServant（libObj 导出，地址已拿到）
   或继续用 /usr/tool/inject-key 的 OSAL 队列路径
③ 读值/写值：用导出的 CmnViewSettingNode API 直接操作 456 个 item 原型
   getProperties / getState / setItemSelected / isItemGrayout / setItemDisable
④ 需要原生页面时：CmnViewGenOpenUtil::openMenu / PAS_MenuPopup::openMenuPopup
```

好处：不碰原生视图树（没有崩 UI 的风险），改设置走的是官方 API；
坏处：菜单外形由我们自己画，需要自己做中文字形（可复用 A7C2 素材或从 fontlist.dat 取）。

### 40.7 复现

```bash
bash analysis/qemu_a7r3/build-probe-menu.sh
bash analysis/qemu_a7r3/install-probe-menu.sh      # preload 改成 probe:shim
bash analysis/qemu_a7r3/set-probe-hook.sh off      # 只读（推荐）
bash analysis/qemu_a7r3/run-probe-menu.sh 3 200    # 3 次重试，200 s/次
# 结果：analysis/a7r3-probe-menu-N.txt（PM_ 行），.log（完整串口）
```

---

## 41) M1a：能不能自己画？—— 实测卡点（2026-11-13 深夜）

### 41.1 实验

`probe_menu.so` 增加 `/setting/probe_draw` 门控：拿到 libObj 后直接调
`GRM_screenGetOnBitmap()`，再用 `GRM_gpermFillrect` 画两条彩条（品红 0..40 行、绿 440..480 行）。

### 41.2 结果（guest 实测，`a7r3-probe-menu-2.txt`）

```
PM_DRAW_ARM
PM_DRAW bmp=0x0 geo=0x0 pix=0x0 stride=0 fmt=0 drect=0x0
```

**`GRM_screenGetOnBitmap()` 返回 0 —— 与 ZV-E10 一样是空桩**（符号只有 8 字节）。
⇒ 拿不到屏幕位图 ⇒ `GRM_gpermFillrect` 没有合法目标（它只按 `params->bitmap` 画）。

### 41.3 为什么"hook 一下 gperm 拿位图"也不行

早前整场 tracing 已确认：**`GRM_gpermFillrect` / `GRM_gpermRectblit` 全场 0 调用**。
A7R3 的真实绘制不是走 gperm，而是 libObj 把 Gemini 命令写进
`/dev/dmpgles2` 映射出来的 ring，再 ioctl 敲门铃（shim 里有 `GPURINGREG`/`GRINGPHYS` 证据）。
⇒ 就算 hook 了 gperm，也不会被调到。

### 41.4 三条候选路线（按成本排序）

| 路线 | 做法 | 成本 | 备注 |
| --- | --- | --- | --- |
| **B（推荐先做）** | 找到 App 自己持有的屏幕位图句柄 | 低 | `GraphicsEngine` 单例（libObj+0x9d0e0）、`viewManagerIf` 槽位都在笔记 §28 有地址；dump 出指针后在位图结构里找 `geo/pix` |
| A | 往 Gemini ring 里自己塞一条绘制命令 | 中高 | 需要命令格式；shim 已有 ring/job dump 能力，可作为取证来源 |
| C | 只在 QEMU 模型里合成叠层 | 低 | 只能做视觉原型，真机不可用 |

拿到位图句柄之后，B 的验证非常直接：用 `GRM_gpermRectblit`（或对位图 `+0x08` 像素
做 CPU 写入）画一条彩条，再看帧。

### 41.5 顺带确证的 ABI

`GRM_gpermFillrect(a, b, params, c)`，params：
```
+0x00 bitmap    +0x04 colour[4] (RGBA 字节)
+0x08 x0,y0     +0x0C x1,y1     （各 int16）
+0x10 mode（只接受 1/2）  +0x14 flag
```
bitmap：`+0x04 geo`、`+0x08 pixels`；geo：`+0x0C rect(x,y,w,h)`、`+0x18 stride(像素)`、
`+0x1C format`（bpp 表 `{0,2,4,2,1,2,2,2,4,4}`）。

---

## 42) 路线 B 成功：屏幕位图已定位且可写（2026-11-13 深夜）

### 42.1 怎么找到的

1. `_ZN2ux6gfxeng14GraphicsEngine15sys_getInstanceEv`（libObj 导出）→ 单例指针；
   `getFramebufferWidth/Height` 返回 **640/480**，证明对象有效。
2. 完整解析 `/proc/self/maps`（★ 必须循环读到 EOF：**一次 read() 只给一页 ~4 KB**）。
3. 在引擎所在的那段堆里逐字扫描，按位图结构判定：
   `bmp+0x04=geo`、`bmp+0x08=pix`、`geo+0x18=stride`、`geo+0x1C=format`。

命中结果（多次运行稳定）：
```
PM_SCREEN bmp=0x5c070 geo=0x5c0f8 pix=0x8d65000      （另一轮 0x49688 / 0x98a3000）
PM_BMP R off=46480 ... stride=640 fmt=1 rect=0x0
```
⇒ **就是那一块 640×480、fmt=1（2 B/px）的屏幕位图**，像素缓冲在堆里。

### 42.2 写入验证（读回证明）

直接往 `pix` 写 40 行品红（0xF0F0），然后读回：
```
PM_PRE  p0=0x0    mid=0x0   hdr=0x0
PM_POST p0=0xf0f0 mid=0x0
（下一轮 tick）
PM_PRE  p0=0xf0f0 mid=0x0   hdr=0xf0f0     ← 写入保留，UI 没有覆盖顶部 40 行
```

⇒ **A7R3 的 framebuffer 是可以在用户态直接写像素的**，这是"自绘新菜单"最关键的一块。

### 42.3 但还没能把它送进"帧"

- 直接写像素后，**guest 随后崩了**（`exception monitor`，3 次），且菜单还没打开
  （`viewSettingMenu` 0 次）。原因：绕过了 `GRM_bitmapWriteLockBits/UnlockBits`
  的引用计数，与 GPU/DMP 线程抢同一块内存。
- QEMU 里的帧 dump 只在**真正发生 blit** 时触发；本轮菜单没起来，所以只有
  boot 阶段的 `frame_8005/8010/9000` 三帧。

### 42.4 下一步（明确）

```
① 用官方写锁包住写入：
     GRM_bitmapWriteLockBits(bmp, -1, &pix) → 写 → GRM_bitmapWriteUnlockBits(bmp)
   （ZV-E10 侧已验证：两个计数必须 1:1 配对，arg1=-1 时原生实现会 cond_wait）
② 只画一次（不要连续 120 次），时机放在菜单绘制完成之后；
③ 然后触发一次原生重绘/呈现，让 blit 把我们的像素带到 panel —— 用
   /usr/tool/inject-key 注入一次 DOWN 即可，帧 dump 会记录。
```

### 42.5 新增工具

| 脚本 | 作用 |
| --- | --- |
| `qemu_a7r3/run-probe-paint.py` | 起 QEMU（串口走 unix socket）、等画笔落地、注键、收帧 |
| `qemu_a7r3/paint-proof.sh` | 上面那个的一键版 + 串口关键行统计 |

### 42.6 已知坑（新）

- **`/proc/self/maps` 一次 read 只返回一页**，不循环就只能看到前 ~47 段，会误判
  "指针不在任何映射里"。
- **`GRM_bitmapGetPhysicalAddress(bmp)` 会崩**：实测 `page permission fault` 落在
  该函数自身地址上（0x6902e2d）。要用物理地址请换别的途径。
- 解析 maps 的循环里 `continue` 前必须先 `p++`，否则遇到不可解析行会**死循环**
  （本轮踩过，表现为 UI 静默卡死）。

### 42.7 写锁版本实测（2026-11-14 凌晨）

用官方写锁包住写入，只画一次（菜单起来之前）：
```
PM_SCREEN bmp=0x52d50 geo=0x52dd8 pix=0xb1aa000
PM_DRAW   bmp=0x52d50 ... stride=640 fmt=1
PM_LOCK   rc=0 pix=0xb1aa000          ← ★ 官方写锁可正常获取
PM_POST   p0=0xf0f0 mid=0xf00         ← 写入生效
PM_UNLOCK rc=0
```
`viewSettingMenu` 出现、帧 dump 拿到 5 帧（9000..9004），但 **帧里看不到我们的彩条**。
原因两条（都还没排除）：

1. **时序**：我们在菜单页画完之前就写了；之后 UI 整页重绘把像素覆盖掉，再 blit 出去。
   下一版把绘制推迟到"菜单出现 + 45 s"，但那一轮 `PM_DRAW` 之后 `PM_LOCK` 没出现
   （位图对象那一刻已失效/换了），需要重新扫一次再画。
2. **可能不是同一块缓冲**：App 至少有"离屏渲染 → panel"两级；我们命中的可能是离屏那一块。
   下一版应**收集全部** 640-stride 位图（最多 24 个），逐个写入并各自触发一次 blit。

### 42.8 当前结论（诚实版）

- ✅ 已确认：屏幕位图可定位、可加官方写锁、可写像素并读回（三次运行一致）。
- ✅ 已确认：写锁 `GRM_bitmapWriteLockBits(bmp,0,&pix)` 返回 0 且 `pix` 有效。
- ❌ 未达成：像素没能在帧里看到（时序 / 选错缓冲两条待排除）。
- ⚠️ 直接写像素后 App 仍出现过 `page translation fault`（3 次里 2 次），
  说明除写锁外还有别的并发保护没拿到（可能是 DMP 的 surface 租约）。

---

## 43) 关键架构结论：QEMU 的 panel 内容不是从 guest 内存来的（2026-11-14）

### 43.1 这轮先补上了两个能力

1. **探针自带 1 Hz 工作线程**（`pthread_create` 经 dlsym 懒创建，不碰 SIGALRM）：
   ```
   PM_WORKER create=0x410b2f1
   PM_WORKER started tid=0x1540a460
   ```
   ← 因为 App 在菜单起来之后几乎不再调 `dlopen`，纯靠 dlopen 触发的定时根本不会到。

2. **探针能自己注按键**（走 `osal_valloc_msg_wait/osal_snd_msg`，队列 `0x14a0429`）：
   帧 `pg2_9006.png` 里选中条已从「自动关机温度」移到「NTSC/PAL选择器」
   ⇒ 注入生效，且能驱动原生 UI。

### 43.2 直写 panel 表面：写进去了，但看不见

```
PM_FB_NOW t=58
PM_FB wrote base=0x202e0200 p0=0xf800 mid=0x7e0
```
（红条 RGB565 0xF800、绿条 0x07E0，写在既有 PA 0x202E0200 上）

随后注入 DOWN 触发的帧 (`pg2_9006.png`)：原生菜单正常重绘、选中条移动，
**但我们的红/绿条不在画面里**。

### 43.3 结论（重要）

**我们 QEMU 机器模型的 panel 画面，是模型按 `grm_gles` 的绘制命令自己光栅化出来的，
不读 guest 的 FB 内存。** 帧 dump（`A7R3_SURF_DIR`）由模型侧写出，所以：

- 在 QEMU 里"看见自己画的像素" ⇒ 必须**进入模型的渲染路径**（提交 gemini 作业，
  或给模型加一条调试用的叠层通道），单纯写 guest 内存永远不会显示。
- 在**真机**上则相反：panel 扫的就是那块内存，直写 PA 0x202E0200 是自然可行的路径
  （这也解释了 zve10-qemu 用 `compose_frame` 直接写它来做 fallback producer）。

⇒ 两条路要分开评估：
```
QEMU（验证视觉/交互）：路线 C —— 给模型加叠层通道（读 host 侧 PNG/raw，blit 前合成）
真机（验证可行性）： 直写 PA 0x202E0200 + 官方写锁，在 service mode 下实测
```

### 43.4 已具备、可直接复用的能力

| 能力 | 入口 |
| --- | --- |
| 定位屏幕位图 | `probe_menu.c` `probe_engine()` + `scan_for_bitmaps()` |
| 官方写锁写像素 | `paint_once()`（`GRM_bitmapWriteLockBits` → 写 → `Unlock`） |
| 1 Hz 定时 | `start_worker()` / `work_tick()` |
| 自己注按键 | `inject_key()`（OSAL 队列 0x14a0429） |
| 直写 panel PA | `paint_fb()`（/dev/mem mmap 0x202E0200） |

---

## 44) ★ 成功：payload 直写 panel，像素真的上屏了（2026-11-14 上午）

### 44.1 为什么之前"看不见"

不是写不进去，是**看的时间点不对**：QEMU 只在 blit 时 dump 画面，而 UI 每次重绘都会
先覆盖 PA 0x202E0200、再 blit —— 我们的像素夹在中间，永远抓不到。

### 44.2 解决：给模型加"定时 dump"

`patch-bionz-surf-tick.py`：`A7R3_SURF_TICK=<ms>` 时，模型每 N ms 把
PA 0x202E0200 落到 `A7R3_SURF_DIR/frame_8xxx.raw`（800 ms 一轮，105 帧/84 s）。
QEMU 侧改动只有两个函数，全部由环境变量开关，默认关闭。

### 44.3 证据

探针（工作线程，菜单起来后 +45 s）执行：
```
PM_FB_NOW t=57
PM_FB wrote base=0x202e0200 p0=0xf800 mid=0x7e0
```
扫描 105 帧后：
```
frame 8100 red565=25600
frame 8100 green565=25600
...
frame 8104 red565=25600 green565=25600
totals: red=819200 green=819200
```
**25600 = 40 行 × 640 列，正是我们写的两条 40 行满宽彩条。**

画面（`analysis/pk1_8104.png`）：原生"设置2"页之上，**多了两条我们的满宽彩条**。

### 44.4 两个已知偏差（都可解）

1. **颜色不对**：panel 缓冲是 **RGBA4444**，我们按 RGB565 写（0xF800/0x07E0），
   所以红条显示成橙、绿条显示成蓝。改成 4444 值即可。
2. **位置偏移**：panel 缓冲是 **Morton/8×8 瓦片**布局且行序翻转。模型 dump 时的变换就是
   现成的打包公式：
   ```
   yy = 479 - y
   z  = (x&1) | ((yy&1)<<1) | ((x&2)<<1) | ((yy&2)<<2) | ((x&4)<<2) | ((yy&4)<<3)
   off = (((yy>>3)*80 + (x>>3))*64 + z) * 2
   ```
   我们按线性 y*640+x 写，所以落到 y≈240 和 y≈440 的位置。

### 44.5 结论

**"payload 自己把像素画到 panel"这条路在 A7R3 上是通的**（QEMU 已实证）。
剩下的是工程细节：Morton 打包 + RGBA4444 + 正确的重绘节奏。

下一步就能把 `menu_shell.c` 渲染出的 A7C2 风格新菜单（640×480 RGBA4444 线性缓冲）
用上面的公式打包成 Morton，写进 PA 0x202E0200，1 Hz 刷新 —— 屏幕上就是完整的新菜单。

---

## 45) 新菜单叠层跑起来了（2026-11-14 上午）

### 45.1 新增组件

| 文件 | 作用 |
| --- | --- |
| `payloads/menu_overlay.c` | 新菜单叠层：渲染 → 打包 → 每秒写 panel |
| `payloads/menu_shell.c` / `original_assets.h` | 直接复用 zve10-qemu 的 A7C2 素材渲染器 |
| `payloads/stdint.h` / `stddef.h` | 给 freestanding 构建补的最小头 |
| `qemu_a7r3/build-newmenu.sh` / `install-newmenu.sh` | 构建 / 安装（preload 变成 `newmenu.so:libefence.so`） |
| `qemu_a7r3/run-overlay.py` / `run-overlay-retry.sh` | 跑 + 自动重试 + 收帧 |
| `qemu_a7r3/get-menu-ref.sh` | 从镜像取 `/tmp/menu_lin.raw` → PNG |

### 45.2 实测结果

- 叠层库正常加载、起工作线程、**渲染成功并写 panel 成功**：
  ```
  NM_ALIVE / NM_pthread_create=0x403b2f1 / NM_worker_tid=0x4d3d460
  NM_FIRST_PAINT=0x3c          （t=60 s，菜单起来之后才动手）
  NM_OVERLAY c=0..4            （1 秒一次，连续 7~12 次）
  ```
- 渲染出的画面已导出：**`analysis/newmenu_ref.png`**
  —— A7C2 风格的标题「我的菜单」、8 个分类图标、页码、`MENU 关闭` 页脚。

### 45.3 仍存在的问题

1. **抢占失败**：原生 UI 每次重绘都会把 PA 0x202E0200 盖掉，dump 到的多半还是原生画面。
   试过 5 Hz 刷新（`ts.tv_nsec=200000000`），能偶尔抢到帧，但不稳定。
2. **面板打包变换还没定死**：线性和 Morton 两种写法都试过 —— 纯线性写入时 dump 出来是
   8 列竖条乱码，说明 dump 侧确实做了瓦片重排；Morton 写法还没抓到干净的对照帧。
3. 与原生 UI 抢同一个 buffer 时，App 有概率崩（`page translation fault`）。

### 45.4 建议的收尾方式

- **QEMU 里做演示**：走模型侧合成（`A7R3_OVERLAY=<raw>`，dump 前把我们的 PNG 叠上去），
  既不和 App 抢 buffer、也不会崩，适合展示和迭代设计。
- **真机上做真叠层**：panel 是真扫描输出，写进去就一直在（除非 App 重绘），
  配合 1 Hz 刷新 + 菜单打开时暂停原生重绘即可。这一步需要 service mode 实测。

---

## 46) 模型侧合成：新菜单叠在原生画面上（2026-11-14 上午，已验证）

### 46.1 做法

`patch-bionz-overlay-img.py` 给模型加了一条**只用于演示**的通道：
```
A7R3_OVERLAY=<640x480 RGBA4444 线性 raw>
```
模型在每次 dump 帧之前，把 alpha 非 0 的像素合成进去（guest 内存不动、不抢 buffer、
不会把 App 打崩）。

配套 `png2overlay.py`：PNG → RGBA4444，**与背景同色的像素 alpha=0**（保留原厂画面透出）。

### 46.2 结果

```
A7R3-OVERLAY loaded /mnt/f/project/a7r3/analysis/overlay_menu.raw
```
帧 `ov2_8151.png` = 原厂 UI + 我们渲染的新菜单（249,910 / 307,200 像素不透明）。

![新菜单合成在原生画面上](analysis/ov2_8151.png)

### 46.3 目前这个"新菜单"的实际内容

`menu_shell.c` 只画了四样东西：顶部标题（A7C2 素材，中文"我的菜单"）、
左列 8 个分类图标、页码、`MENU 关闭` 页脚 —— **没有列表行**。

要变成真正能用的菜单，还缺：
1. **列表行渲染**：按当前分类从设置树里取 item（456 个 root，标题是字符串 ID
   `0x02xxxxxx`），渲染成 6 行 + 选中条。
2. **字符串**：ID → 文字需要 A7R3 的 `string_*.uxc` 串表（或直接从 `view*.uxc`
   里已解码的字形/位图取），A7C2 的烘焙素材只覆盖少量固定文字。
3. **输入**：按键已经能自注（`inject_key`，OSAL `0x14a0429`），接上即可上下移动。

### 46.4 三条线的当前状态

| 线 | 状态 |
| --- | --- |
| QEMU 演示（模型合成） | ✅ 可用，见上图 |
| QEMU 真实叠层（guest 写 panel） | 🟡 写入成功、像素可验证，但抢不过原生重绘（需 5 Hz+ 或真机） |
| 真机叠层 | ⬜ 待 service mode 实测（写 PA 0x202E0200 + 1 Hz 刷新） |

---

## 47) 完整新菜单渲染（2026-11-14 上午，已出图）

### 47.1 补齐了什么

1. **行渲染**：6 行、行高 52、橙色选中条（`0xd520`），右侧留出数值列。
2. **中文标签**：`bake-labels.py` 用 host 上的 `msyh.ttc` 把文案烘成 alpha mask
   （`payloads/menu_labels.h`），文案取自真机帧里的原生措辞
   （文件格式 / JPEG画质 / JPEG图像尺寸 / 纵横比 / 全景:图像尺寸 / 影像质量 /
   显示质量 / 自动关机开始时间 / …）。
   —— 顺带确认：A7R3 的 `string_simplifiedchinese.uxc` 里字符串是**纯 UTF-8**
   （`probe-strings.py` 抽到 6166 段中文）。
3. **顶部 8 个分类图标**用 A7C2 素材（`a7_tab_normal/selected`），选中项加边框。
4. **页眉/页脚**：`我的菜单` + 页码（A7C2 的 `a7_page_text`）+ `MENU 关闭`。

### 47.2 结果

![A7R3 新菜单（完整行渲染）](analysis/newmenu_ref.png)

同一张图经 `png2overlay.py` 交给模型的合成通道，在真实启动的机器上也是一样的画面
（`analysis/ow1_8151.png`，与上图一致）。

### 47.3 诚实的边界

- **数值列现在只是占位条**：真实值要读 `CmnViewSettingNode` 的
  `getProperties/getState`（API 已定位，地址在 §40），还没接。
- **选中条不会跟按键走**：现在是每 2 s 自动下移一行、每 16 s 换分类（纯演示）。
  真按键读取还没有（要么 hook 输入路径，要么读 OSAL 队列 —— 后者更安全）。
- **页码是 A7C2 的烘焙图**（显示 0/0），不是 A7R3 的真实页数。
- QEMU 里"guest 直接写 panel"仍抢不过原生重绘（§45.3），所以设备截图走模型合成。

### 47.4 下一步

```
① 接真实数值：对每个 row 记住对应的 root 节点指针，调 getIntProperty/getState
② 接按键：起一个 reader（OSAL 队列订阅或 input hook），驱动 g_selection
③ 真机验证：service mode 下直接跑 newmenu.so，看屏幕
```

---

## 48) ★ 新菜单显示真机的真实设置值（2026-11-14 上午）

### 48.1 做法

叠层里对每个 row 绑定一个 libObj/CautionConfig 导出的设置节点原型：

```c
static const char *const g_row_syms[6] = {
    "cmnViewSettingNodeRootRawFileType",
    "cmnViewSettingNodeRootStlImageJPEGQuality",
    "cmnViewSettingNodeRootStillImageSize",
    "cmnViewSettingNodeRootStlImageAspectRatio",
    "cmnViewSettingNodeRootStillPanoramaImageSize",
    "cmnViewSettingNodeRootStlImageQuality",
};
```
`dlopen("/usr/lib/CautionConfig.so", RTLD_LAZY|RTLD_NOLOAD)` 拿 handle（★ 必须是
`5` 而不是纯 `RTLD_NOLOAD`，否则返回 0），`dlsym` 拿节点指针，读
`+0x08 count` 和 `+0x18 selected`，显示成 `selected+1 / count`。

### 48.2 结果

```
NM_caution=0x94363e8
NM_node0=0x151a2368
```

![新菜单显示真实设置值](analysis/newmenu_ref.png)

```
文件格式       2/2
JPEG画质       2/3
JPEG图像尺寸    2/12
纵横比         2/2
全景:图像尺寸    2/2
影像质量       2/5
```
这些数字是**从相机自己的设置节点实时读出来的**，不是写死的。

### 48.3 还差什么

- **按键没接上**：`/setting/nm_key` 文件通道没生效（`NM_KEYREAD` 一次都没打，
  说明 shell 里的 `echo d > /setting/nm_key` 没落地）。下一步用 OSAL 队列订阅
  或直接 hook 输入路径。
- **值只读**：`ENTER` 改值需要调 `setItemSelected`（API 已有），还没接。
- 行文案与节点的对应表是硬编码的 6 个；完整做法是按分类从 456 个 root 里挑选。

---

## 49) 输入 hook 实验：技术上可行，但会崩（2026-11-14 上午）

### 49.1 选的是最"干净"的一个入口

`snd_InfraInputEvent_to_InputServant`（libObj `+0x3c00b0`）的序言：
```
b5f0 b083 af00 4604   ; push {r4-r7,lr} / sub sp,#12 / add r7,sp,#0 / mov r4,r0
```
四条全是 16 位指令、无 PC 相对加载 ⇒ **8 字节跳转 + 拷贝蹦床是安全的**，
比之前 `initSettingNode` 的情况好得多。

### 49.2 结果

```
NM_obj=0x6200d90
NM_servant=0x6c580b1
NM_hook_rc=0x0          ← 装上了
```
然后在 shell 里注键（`/usr/tool/inject-key 0x7a0020 0x30 …`）：
**一次 `NM_EVT` 都没有，guest 立刻 fault**。

⇒ 结论：这个 hook 装得住，但**一有输入事件就把 App 打崩**（蹦床或 ABI 还不完全对）。
已改为默认关闭（需 `/setting/nm_hook` 才启用），关掉后叠层稳定运行（17 次绘制无异常）。

### 49.3 下一步的正确做法（按风险排序）

1. **先只观察、不转发**：wrapper 记录参数后**直接返回 0**（不调原函数），
   用一个只观察的构建确认 ABI（哪个参数是事件指针、键码在哪个偏移），
   确认期间不接受真实输入。
2. **OSAL 订阅**：`osal_reg_msg_queue_cb(qid, &handle, cb, arg)`，
   签名已知（`int(unsigned, void**, void*, void*)`），但回调 ABI 仍是猜的；
   先用一个只记录前 8 字节、返回 0 的回调试。
3. 真机上有真实按键时，最好在**菜单没被使用时**装 hook（避免和 UI 抢同一时刻）。

---

## 50) ★ 关键架构结论：叠层不能和原生 UI 抢同一块 panel（2026-11-14）

### 50.1 对照 zve10-qemu 的做法

zv-e10 那个"新菜单"**不是**直接往 panel 内存写像素，而是：

1. `ne_add_row()` 在原生设置树里插一行「New Menu」（原生集成，入口）；
2. 进入后用**自己的渲染器**画到离屏 640×480 RGBA4444；
3. **在 present 钩子里**（`native_entry_present(pixels, len, physical, caller)`）
   把它叠到即将提交的那一帧上（`ui-present.h` 还做了 8×8 tile 重排）；
4. 期间**吞掉原生按键**（`*action = -1`），所以原生 UI 不会重绘。

⇒ 本质是"**在呈现阶段叠一层**"，不是"改 framebuffer"。我们现在的写法（直接写
PA 0x202E0200）绕过了它的第 4 点保护，于是：

### 50.2 实测到的冲突

| 场景 | 结果 |
| --- | --- |
| 叠层单独跑（不按键） | ✅ 稳定，连续 17+ 次绘制；真值正常显示 |
| 叠层 + 注入按键 | ❌ **必崩**：`section translation fault at 0x1c6c4b00` |

而且崩溃与输入 hook 无关：即使 hook 装了但**一次都没被调用**（`NM_EVT` 0 条），
只要按键触发原生重绘就崩 ⇒ 根因是**我们在 App 正在画的同一块 panel 上写像素**。

### 50.3 因此下一步的正确方向

```
① 停止"裸写 panel + 让 App 继续重绘"的组合；
② 改到呈现阶段叠层（等价于 zve10 的 present hook）：
   QEMU 侧已经用模型合成验证过（A7R3_OVERLAY，§46），真机侧要 hook present；
③ 或者在菜单激活期间让原生 UI 停在一个静态帧上（不再触发重绘），
   我们独占 panel —— 这是"裸写"路线唯一稳定的用法。
```

---

## 51) 空闲门控（idle gate）：稳定了，但抢不到画面（2026-11-14）

### 51.1 做法

`put_panel()` 里加一个门控：写之前先比较 panel 当前内容与我们上次写下的内容
（`g_last`，614 KB）——

- 不一致 ⇒ 原生 UI 刚重绘过，**这一轮不写**，只更新 `g_last`；
- 一致 ⇒ UI 空闲，安全写入。

这正好对应 §50 的结论：崩溃来自"我们在 UI 正在画的时候写"。

### 51.2 结果

| 指标 | 结果 |
| --- | --- |
| 键注入后的稳定性 | ✅ **不再立刻崩**（16 次绘制、按键窗口安然度过） |
| dump 里出现我们的画面 | ❌ 0 帧（`find-overlay-frames.py` 找不到选中条颜色） |

### 51.3 为什么还是抢不到

`page` 画面的原生 UI（"设置2"那一屏）在我们写入后**毫秒级就会重绘**，
而模型是每 700 ms 才 dump 一次 —— 永远拍到的是 App 的版本。

⇒ **裸写 panel 这条路线，只要原生 UI 还在持续合成，就不可能稳定显示我们自己的画面。**
必须二选一：

1. **真正冻结原生 UI**（停掉它的绘制线程/或让它停在静态帧），我们再独占；
2. **改到呈现阶段叠层**（zve10 的做法）：hook present，在"即将提交的那一帧"上合成。

### 51.4 当前最可靠的组合（已可用）

```
QEMU 演示：模型合成（A7R3_OVERLAY=...）—— 稳定、可迭代设计，见 §46/§47
真机实验：service mode 下 pause 原生 UI → 裸写 panel（等价于本节的门控，但没有
          持续重绘的对手）
```












---

## 42) 5GHz Wi-Fi 可行性（对照 ZV-E10 5GHz 解锁，2026-11-13）

参照 `ZV-E10-5GHz-WiFi-Unlock/`（README/technical/regions）与本机 nflasha15。

### 42.1 结论

- **不是 ZV-E10 那种"改 1 字节 + 3 个 Backup 属性"就能解决的事。**
- 射频/驱动层**具备双频条件**（同款 BCM4339、5G 校准齐全）；但 A7R3 的
  应用层（Sony WCM）把相机热点（GO）**写死 2.4GHz**，且**没有原生频段菜单**可揭示。
- 理论可行路线 = WCM 层强制 band + 选到允许 5GHz 的 regulatory revision；
  **未验证**，工作量与风险都明显高于 ZV-E10。
- 最低风险的第一步是**只读扫描**：先确认驱动/CLM 到底能不能看见 5GHz AP。

### 42.2 硬件/固件是双频的（证据）

| 项 | 位置 / 值 |
| --- | --- |
| 无线芯片 | BCM4339：`/usr/wlan/bcm/bcm4339.bin`、`/usr/kmod/bcmdhd.ko` |
| NVRAM | `/usr/share/network/nvram/{high,low}/bcm4339_{H,L}_01..15.txt` |
| 每份 NVRAM | `ccode=XR`、`regrev=75..89`、`aa5g=1`，`pa5ga0/maxp5ga0/swctrlmap_5g/rssicorrnorm5g` 全有 |
| high vs low | 只差发射功率（`maxp2ga0` 74 vs 34、`maxp5ga0` 58 vs 26），**两边都含 5G 校准** |

### 42.3 当前配置是 XR/83 —— 与 ZV-E10"只有 2.4G"的 revision 同号

- Backup `0x00A50006 = 09` → 驱动用 `bcm4339_H_09.txt` + `bcm4339_L_09.txt`
  → `regrev=83`。
- ZV-E10 文档记录：原厂 B/C=9/9 → XR/83，运行态只暴露 2.4GHz；换到 76/143 才有 5GHz。
- A7R3 的 CLM（`bcm4339.bin` 偏移 `0x78734`，API 9.10.139 / compiler 1.26.11）里
  有 `XRK..XRY` 共 15 条，数量与 regrev 75..89 的 15 个值一一吻合（**顺序对应只是推测**，
  未经证实）；若 75→XRK 成立，则 83 → **XRS**，其记录 `04 02 03 01 00 00`
  明显区别于其它条（多以 `00 20/21` 结尾）。与 ZV-E10 的观察方向一致，
  但 **XR→信道表的语义仍未解出**（与 ZV-E10 项目的结论相同）。

### 42.4 驱动的 NVRAM 选择逻辑（`libWlanResource.so`）

- `get_bcmdhd_insmod_args(unsigned char a, char* out, bool b, char c)` @ `0x6730`：
  - `Backup_read(0x00A50006, &v)`，`v > 0xF` 直接失败 → profile 只能是 1..15；
  - 第 1 个参数 `a==1` → 选 `high`，否则 `low`（即 `bcm4339_{H,L}_%02d.txt`）；
  - `b` = mfg 模式（`bcm4339_mfg.bin`），`c` = 允许 `/mnt/` 下的覆盖固件。
- `loadWlanHostDrivers_bcm4339bh(bool)` @ `0x6938`：入参 `0xFF` 时
  `Backup_read(0x01071563, &band)` 作为 high/low；随后 `insmod(bcmdhd.ko, args)`。
- 本机实机值：`0x01071563 = 00`（=low）、`0x00A50008 = 00`。
- **ZV-E10 的 `0x01071C54` / `0x00A5000B` / `0x00A5000C` 在 A7R3 的 Backup 里根本不存在**
  （已用 2.6 万条属性全表确认），所以那三个写入没有对应落点。

### 42.5 应用层把热点写死 2.4GHz（关键差异）

- 启动链（推测，基于字符串引用）：`im.elf` → `libIMDB.so`（内含 `/usr/bin/network.sh`）
  → `/usr/bin/network.sh` → `WS_SubProcess.elf`（WCM）。
- `CWCMPlatformDef` 编译期常量：
  - `IS_GO_2GHZ_ONLY = 1`（`WS_SubProcess.elf` VA `0x2bb9c`；`libNetService.so` @ `0x92728` 也是 1）
  - `IS_P2P_DISABLED_5G_IN_FIND = 0`（VA `0x2bbac`）→ 扫描并没有禁 5G
  - `BSS_DEFAULT_WPS_BAND_TYPE = 1`（VA `0x2bb94`）
- **重要坑**：这些常量在 `.symtab` 里有符号，但**未找到任何代码引用**
  （无 `.rel.dyn` 重定位、全文无绝对字面量、PIC `add pc` 形式也未命中）⇒
  编译器很可能已把值折叠进代码。**直接改 rodata 字节大概率无效**，
  只能改对应的代码分支（需要跨机型对比才能定位），这一点尚未证实。

### 42.6 没有隐藏的频段菜单

- `CautionConfig.so` 里 WiFi 页的节点只有：
  `WiFiSettingPage1AccessPoint` / `DisplayMACAddress` / `SSIDPWReset` / `WpsPush`
  —— 没有 band 项（ZV-E10 的 TypeNB 页里有）。
- 全语言 `.uxc`/大库中搜不到 `Frequency Band`、`GHz`（除 `wl/iw/wpa_supplicant` 等通用工具）。
- `p2p.conf` 写死 `p2p_oper_reg_class=81`、`p2p_listen_reg_class=81`（2.4G 1–13）。

### 42.7 与 ZV-E10 的对照

| 条件 | ZV-E10 | A7R3 |
| --- | --- | --- |
| 同款 BCM4339 + 5G 校准 | 是 | 是 |
| 原生隐藏频段菜单 | 有（TypeNB 有 / TypeRK 无） | **无**（页面定义里没有 band item） |
| 频段开关属性 | `0x01071C54`、`0x00A5000B/C` | **不存在**；只有 `0x00A50006`(profile)、`0x01071563`(high/low) |
| GO 是否写死 2.4G | 否 | **是**（`IS_GO_2GHZ_ONLY=1`，且已折叠） |
| 结论 | 1 字节 + 3 属性 | 需代码级 patch + regulatory 选择 + 自制 UI |

### 42.8 建议的最小验证（先不写任何持久数据）

1. service mode 下临时 `insmod bcmdhd.ko`（NVRAM 用现成 `H_09`），执行
   `iw dev wlan0 scan` / `wpa_cli -i wlan0 scan_results`，看结果里有没有 5GHz AP：
   - **能看见** ⇒ CLM/驱动允许 5G，瓶颈只在应用层的 GO 限制；
   - **看不见** ⇒ 先换 profile（Backup `0x00A50006`，如 `02` = XR/76）再试。
2. 只在 tmpfs/`/tmp` 里改 `p2p.conf`（`p2p_oper_reg_class=115/121`、`p2p_oper_channel=36`）
   起一个临时 supplicant，验证 GO 能不能在 5GHz 起。
3. 1、2 都通之后，才考虑 `WS_SubProcess.elf` 的代码级 patch；UI 可直接复用
   §39–41 的叠层菜单。

风险：型号认证/法规问题；A7R3 的双频天线是否装配**未知**（模块校准存在≠天线支持）。

### 42.9 第一步的风险与更保守的做法

“第一步”再拆成 1a（纯读）与 1b（加载驱动/扫描）：

| 步骤 | 动作 | 是否发射频 | 是否写 flash | 风险 |
| --- | --- | --- | --- | --- |
| 1a | 进 service mode → `cat /proc/modules`、`ifconfig -a`、`iw dev`、**`iw phy phy0 info`** | **否** | 否 | 极低 |
| 1b | 若 wlan0 不存在：`start_dhd.sh cam` 加载模块，再 `iw dev wlan0 scan` | 是（scan 会发 probe） | 否（`/tmp_network` 挂 tmpfs） | 低，但可能死机 |

- **`iw phy phy0 info` 是最有价值的一步**：它直接列出 phy 支持的 Band/频点。
  若出现 5GHz 频点 → 驱动/CLM 放行 5G，瓶颈只在应用层；全程不发射频、不写盘。
- 只有 1a 里 wlan0 不存在时才做 1b。1b 的已知坑：
  - 用 Sony 自带 `start_dhd.sh`（模块顺序与 GPIO 时序是官方脚本），不要手搓 gpio；
  - `/tmp_network` 先 `mount -t tmpfs`，避免 wpa_supplicant 写 nflasha10；
  - 收尾用 `stop_dhd.sh`（rmmod 顺序），然后**断电**，不要 `auth.stop()`。
- **不可逆红线（本步骤都不涉及）**：不写 nflasha1/2/3/7/15、不动 boot0、不杀 service
  会话里的 `im.elf`（笔记已记录会立刻断 USB）。
- 最坏情况：内核 oops/卡死 → 拔电池 10 秒恢复。因为不写任何分区，**不会变砖**；
  唯一能变砖的是更新器/内核/rootfs 写入，1a/1b 都没有。
- 电池建议 >50%：Wi-Fi 发射瞬间电流较大，弱电可能在操作中掉电（只读操作掉电也无害）。

### 42.10 1a 实测结果（2026-11-13，只读）

`analysis/wifi_probe_1a.py` 从 0BFF 切到 0336，root shell 正常，未写任何分区、未
`auth.stop()`。日志：`analysis/wifi_1a_probe.log`。

- `/proc/modules`：**没有 `bcmdhd`/`cfg80211`/`compat`/`mmc_core`/`mmcioWrapper`**；
  只有 `wlanMemoryAllocator`（Sony 的 WLAN 内存分配器）已加载。
- `/bin/busybox ifconfig -a`、`ls /sys/class/net`：只有 `lo/gre0/sit0/tunl0/ip6tnl0`，
  **没有 wlan0**。
- `/usr/bin/iw dev`、`/usr/bin/iw phy`：均返回 **`nl80211 not found`**（cfg80211 未加载）。
- 结论：**1a 无法回答频段问题**——service mode 下 Wi-Fi 驱动根本没加载，
  必须做 1b（加载 bcmdhd）才能看到 phy 的频段表。

### 42.11 1b/1c/1d 实测：驱动能起，但 phy 只有 2.4GHz（2026-11-13）

脚本：`wifi_probe_1b.py`、`wifi_probe_1c.py`、`wifi_probe_1d.py`、`wifi_gpio_probe.py`；
日志：`wifi_1b_probe.log`、`wifi_1c_probe.log`、`wifi_1d_probe.log`、`wifi_1d_probe2.log`。

**关键发现 1：WLAN 上电走 `/dev/gpio`，不是 gpioctrl 的引脚名。**
`libWlanResource.so::initializePort_bcm4339 @0x6aa8` 打开 `/dev/gpio`，
用 `Backup_read_4Byte(0xF905ED)=9`、`(0xF905EE)=18` 作 port/bit，
再 `ioctl(fd, 0xC0104702, {0, 9, 18, value})`。实测：

- `gpioctrl.elf` 的 `MMC_XRST`/`SDIO1_*` 名字在这台机上全部 `pinname error`；
- **但 gpioctrl 支持数字形式**：`gpioctrl.elf w 9 18 --gpio --dir o 1`（=WLAN_REG_ON）
  和 `gpioctrl.elf w 0 4 --gpio --dir o 1`（=SDIO 电源）。
- 复位脚初始为 **Output/low**；拉高后 `mmcioWrapper` 立即成功：
  `mmc0: new SDIO card at address 0000`。

**关键发现 2：驱动和固件都能正常起。**

```
insmod compat.ko / cfg80211.ko / mmc_core.ko / mmcioWrapper.ko / bcmdhd.ko  全部 rc=0
Firmware version = wl0: Oct  3 2018 ... 6.37.39.101 (r708132 CY)
Register interface [wlan0] MAC: d4:4d:a4:b9:59:15
wlan0 + p2p0 出现
```

**关键发现 3：phy 只报 2.4GHz。**

`iw phy phy0 info`（NVRAM `bcm4339_L_09.txt`，即当前 XR/83）：

```
Wiphy phy0
Band 1:
  * 2412 MHz [1] (20.0 dBm) ... * 2484 MHz [14] (20.0 dBm)
（没有 Band 2）
```

dmesg 同时给出关键解释：

```
cfg80211: Ignoring regulatory request Set by core since the driver uses its own custom regulatory domain
```

即信道表来自**驱动/dongle 自己的 CLM**，不是内核 regdb。

**关键发现 4：换遍 15 个原厂 NVRAM 档位，结果完全一样。**

`wifi_probe_1d.py` 逐个 power-cycle dongle（`rmmod bcmdhd/mmcioWrapper` →
REG_ON 低/高 → 重载），profile 01–15 全部只得到 `Band 1: 14 ch (2412–2484 MHz)`，
**没有任何 5GHz**。

- 注意：同一会话里连续 `insmod bcmdhd` 第二次会失败（`rc=62`），
  必须做完整 dongle 复位 + 重载 `mmcioWrapper`。
- 收尾：`rmmod bcmdhd`、`rmmod mmcioWrapper`、REG_ON 拉低，全程未写 flash。

**结论（有实测支撑）**：
A7R3 3.10 的无线固件/CLM 在**所有出厂 regulatory 档位**下都只暴露 2.4GHz。
ZV-E10 那条“换 NVRAM 档位 + 揭示菜单”的路在 A7R3 上不成立——它连驱动层都没有 5GHz 信道。
若要继续，只剩“替换 dongle 固件/CLM”（例如移植 A7C/ZV-E10 的 `bcm4339.bin`），
属于跨机型固件移植，风险与不确定性都高，且 A7R3 的 5GHz 天线/前端是否存在仍未证实。

工具（本次新增，均只读）：`wifi_scan_a7r3.py`、`wifi_grep_tree.py`、
`wifi_backup_props.py`、`wifi_wlanresource_strings.py`、`wifi_dis4.py`、
`wifi_pltmap.py`、`wifi_band_menu_scan.py`、`wifi_find_nvram_refs.py`。

---

## 53) 裸写 panel 的终极结论：菜单期 UI 一直在重绘（2026-11-14）

用 `frame-changes.py` 统计连续 dump（0.7 s 一帧）的帧内容哈希：

```
frames=104  distinct_transitions=17
identical run lengths: [75, 1, 1, 1, 1, 1, 1, 1, 1, 1, 2, 1, 1, 9, 1, 1, 1, 1, 4]
longest stable run: 75 frames (~52.5 s)
```

- 前 75 帧（≈52 s，开机→菜单出现前）**完全不变** ⇒ UI 是静的；
- 之后几乎**每帧都不同** ⇒ **菜单一打开，App 进入持续合成循环**（<0.7 s 重绘一次）。

⇒ **菜单显示期间裸写 panel 永远赢不了**：写入后毫秒级被覆盖，模型 0.7 s 才 dump
一次，永远拍到 App 的版本。§51 的空闲门控因此在菜单期几乎无法放行（实测
`find-overlay-frames.py` 在 104 帧里 0 命中）。

> **修正（见 §58）**：本节只对"渲染目标"成立。App 的持续重绘写的是
> `0x202E0200`；present 会把画面复制到 **`0x23A80000` / `0x23B16000` 双缓冲**，
> 那两块只在 present 时被改写。把叠层写进双缓冲即可稳定显示（§58.5 实测）。

### 53.1 唯一可行：呈现阶段叠层（zve10 路线）

在 App **提交帧的那一刻**合成我们的内容，这样我们和 App 输出的是同一帧，
不存在"抢内存"。

### 53.2 下一步：定位 A7R3 的 present 提交点

已有线索：
1. 绘制是 libObj → `grm_gles` ioctl，**shim 已拦截 ioctl 且能拿到调用者 LR**；
2. shim 里有现成的 ioctl 直方图/调用者记录（`patch-ioctl-hist*.py`、`patch-ioctl-fd*.py`）；
3. 判据：菜单稳定显示时，**每帧都会调用、且参数里带 surface 地址**的那条 ioctl。

步骤：打开 ioctl 调用者记录 → 菜单显示时抓 10 s → 找出高频且带 `0x202E0200`/surface
参数的那条 → 在用户态 hook 它的调用者函数 → 在 hook 里合成我们的帧。

---

## 54) ★ present 提交点已定位（2026-11-14 上午）

### 54.1 方法

`patch-ioctl-window.py` 把 shim 里 `gio_logs < 48` 放宽到 20000（配 `build-shim-diag.sh`
的 -O1 构建，否则日志会被优化掉），跑一次菜单打开的 run，再用 `ioctl-hist.py`
统计 `(req, caller_offset)` 组合。

### 54.2 结果

```
pairs observed: 14
  req=0xc0048212  loff=0x7195a5  n=130   ← 等待完成（CMD_WAIT_B）
  req=0xc0048211  loff=0x719533  n=111   ← 提交（CMD_WAIT_A）
  req=0xc0048212  loff=0x719533  n=23
  req=0x8004820a  loff=0x718b5b  n=19
  req=0xc004820d  loff=0x719479  n=13    ← doorbell
```

### 54.3 反汇编确认（`dis-lib.sh`，llvm-objdump）

**提交函数 @ libObj+0x719510**：
```asm
719510: b5f0      push {r4,r5,r6,r7,lr}
719512: b083      sub  sp,#0xc
719520: movw r1,#0x8211 ; movt r1,#0xc004     ; CMD_WAIT_A
719524: ldr  r0,[r4]                          ; fd
71952c: str  r6,[r7,#4]                       ; 参数 = 提交值
71952e: blx  ioctl@plt
719532: cbnz r0,err
719534: ldr  r0,[r7,#4] ; cmp r0,#0 ; bne 719520   ; 设备没吃掉就重试
```
**等待函数 @ libObj+0x719580**：
```asm
719580: push {r4,r7,lr}
71958a: movw r1,#0x8212 ; movt r1,#0xc004     ; CMD_WAIT_B
7195a0: blx  ioctl@plt
```

⇒ **每一帧的"提交 + 等待"就是这两个函数**（0x719510 / 0x719580），
它们只对 ring slot 递一个 32 位值（命令描述符），**像素本身在那个描述符指向的
surface 里**。

### 54.4 因此 present 叠层的正确做法

```
① hook libObj+0x719510（提交）
② 在调用原函数之前，把我们的 640×480 帧写进"本次要提交的 source surface"
   （surface 地址从命令描述符里取；暂时可用 §42 扫描到的位图 pix 逐个写）
③ job 被设备消费时就会把我们的像素搬到 panel —— 不存在"抢内存"的竞态
```

下一步就是 ②：先按"对已知候选 surface 全写一遍"的笨办法验证一次
（如果其中某个就是 job 的源，画面就会变成我们的菜单）。

### 54.5 工具

| 脚本 | 作用 |
| --- | --- |
| `patch-ioctl-window.py` | 放宽 shim 的 GPU ioctl 日志（配 `build-shim-diag.sh`） |
| `ioctl-hist.py` | 统计 `(req, caller_offset)` 频次 |
| `dis-lib.sh` | 用 llvm-objdump 反汇编 /usr 库里任意虚地址窗口 |
| `frame-changes.py` | 判断原生 UI 是否在持续重绘 |
| `find-overlay-frames.py` | 在 dump 帧里找我们的选中条颜色 |

---

## 55) present hook 已装好，但盲写 surface 会把启动打崩（2026-11-14）

### 55.1 已经确认的三件事

```
NM_objbase=0x660f000            ← 与 shim 自己算出的 libObj 基址一致
NM_present_rc=0x0               ← hook 安装返回成功
NM_patch_verify=0xf800df        ← 0x6d28510 处的 4 字节正是 DF F8 00 F0（我们的跳转）
NM_wrapper=0x41827b0            ← 字面量里的 wrapper 地址
NM_SURFACE=0x9718000            ← 扫描到的 640-stride 位图像素地址
```

⇒ **提交点定位、基址换算、跳转写入、surface 扫描全部正确。**

### 55.2 但一启用就崩

把 hook 打开（`/setting/nm_present`）后连跑 6 次，**6 次都在 t<60 s 崩**；
关掉门控后同样的构建稳定跑完（12 次绘制）。

原因很直接：wrapper 一进来就把 614 KB 整帧写进扫到的 surface，而**开机阶段 App
还在初始化/提交各种 job**，我们盲写的那块 buffer 很可能不是本帧的源，直接被写坏。

### 55.3 因此还差两步（都不难）

1. **延迟到菜单起来之后再 arm**（现在已加门控 `nm_present` + `now>=60`，
   但门控之后仍然要验证"arm 期间没有并发提交"）；
2. **只写"本次提交真正会用到的那块 surface"**：提交值 `a`（r0）是命令描述符，
   要从里面解出目标/源 surface，而不是盲写扫到的第一块 640-stride 位图。

### 55.4 当前代码状态

- `menu_overlay.c` 里 present 相关代码全部保留，但**默认关闭**
  （需要 `/setting/nm_present` 才 arm）；
- 裸写 panel 那条路（§51/§53）仍保留空闲门控，默认开启；
- 两条路都能单独跑，互不干扰。

---

## 56) present hook 第二轮：跳转正确，但 wrapper 从未进入（2026-11-14）

把 wrapper 改成**只记录、不写任何 surface**（`NM_PRESENT a=/w0..w5`），再开
`/setting/nm_present`：

```
NM_present_rc=0x0
NM_patch_verify=0xf800df        ← 0x6d28510 = DF F8 00 F0，跳转确实写进去了
```
然后 `NM_OVERLAY x1/x2` → 注入按键 → **guest 立刻 fault，`NM_PRESENT` 仍然 0 条**。

⇒ 两个结论：
1. **崩溃与"写 surface"无关**——这一版完全不写，照样崩；只要 hook 装着，
   按键触发的提交路径就会崩。
2. **`0x719510` 并非常规状态下的提交入口**：shim 能看到 `0xC0048211` 的 ioctl，
   但我们改了入口后 wrapper 一次都没进 ⇒ 真实提交来自**另一条路径**
   （同一函数内的内联副本、或另一个库/另一段代码）。

### 56.1 下一步（要把这件事查清就得换手段）

- 用 `dis-lib.sh` 把 `0x7194c0..0x719520` 全段反汇编出来，确认是否还有**第二个
  写 ring 的函数**（例如 thunk / 内联展开）；
- 或直接在 shim 里 hook `ioctl`（我们已经有了），**在 ioctl 命中 0xC0048211 时**
  从 `arg` 解出提交值并 dump —— 这是最稳的观测点（shim 已经能看到 18~130 次调用）；
- 拿到真实提交值后再决定：写 surface、还是改命令流。

### 56.2 本次会话在"叠层"这条线上的净结果

| 能力 | 状态 |
| --- | --- |
| 新菜单渲染（中文行 + 真值 + 选中条） | ✅ 见 §47 的图 |
| 读取真实设置值 | ✅ §48 |
| 自己注按键 | ✅ §43 |
| 模型侧合成演示 | ✅ §46 |
| guest 裸写 panel | 🟡 能写、能验证，但菜单期抢不过重绘（§53） |
| present 阶段叠层 | 🟡 提交点已定位并反汇编，但常规路径未命中该入口（本节） |

---

## 57) 提交值只是一个门铃序号（2026-11-14，shim 侧观测）

在 shim 里给 `0xC0048211` 的 ioctl 加了一行观测（`GSUB val=`，取 `arg[0]`），
重跑后拿到 40 条：

```
GSUB val=0x1
GSUB val=0x2
GSUB val=0x1
...
```

⇒ **提交给 ring slot 的 32 位值只是 1 或 2（序号/标签），不是指针**。
真正的负载在 ioctl 参数指向的命令缓冲里（shim 已经在 dump 它）：

```
GCMD idx=0x0  arg=0xe83fa2c  w1=0x680f224
GCMDBUF 78bb0100e1987100c402000012000a002fa70100fdb461002000000012000a00...
```

### 57.1 结论

present 叠层要真正落地，剩下的是**解码这串命令格式**（每条 16 字节、以 `0x000a0012`
之类的头部开头，里面带若干 24/32 位字段）——也就是把"这一帧会把哪块 surface
搬到 panel"从命令流里读出来。

这与 zve10 项目当年卡住的点相同（他们直接在 present 处拿到像素指针，不需要解命令）。
我们的优势是：**shim 已经能稳定 dump 命令缓冲**，样本可以无限采集。

> **修正（见 §58.1）**：本节的"命令缓冲"判断是错的 —— `0xC0048211` 的参数只是
> 一个 4 字节 in/out 槽（提交值 1/2），`GCMDBUF` 打印到的是栈上垃圾
> （`gpucmd_0.bin` 其实是 libObj 的 ELF `.dynsym`）。真正的 surface 描述符在
> `/dev/dmpgles2` 映射里，见 §58.2。

### 57.2 建议的下一步（独立一轮）

1. 用 `A7R3_TEX_DIR` + `gpu_dump_blob` 把若干帧的完整命令缓冲落盘（不是 0x60 字节，
   而是 `w1` 指向的整段）；
2. 对比"菜单打开前 / 打开后 / 按键后"三组命令，找出**每帧都会出现且带 surface 地址**
   的那几条命令；
3. 一旦能取出 surface，就把 §55 的 present hook 改成"只写该 surface"，而不是盲写。

工具：`patch-shim-gsub.py`（本次新增，可重复使用；观测完已回退源码，
生产 shim 的 md5 与基线一致 `f06fd69b…`）。

## 58) ★ 命令缓冲解码完成：present 通路与叠层落地上屏（2026-11-14 下午）

目标：把"这一帧会把哪块 surface 搬到 panel"从命令流里读出来，并据此把新菜单真正叠上去。

### 58.1 纠正 §57 的误判：0xC0048211 的参数不是命令缓冲

`libObj+0x719510`（提交）反汇编：

```asm
719510: push {r4,r5,r6,r7,lr}
719512: sub  sp,#0xc
71951a: mov  r6,r0            ; r6 = 传入值
719520: movw r1,#0x8211 ; movt r1,#0xc004
71952a: adds r2,r7,#4         ; arg = &slot
71952c: str  r6,[r7,#4]       ; slot = r6
71952e: blx  ioctl
719532: cbnz r0,err
719534: ldr  r0,[r7,#4]       ; 重读 slot
719536: cmp  r0,#0 ; bne 719520   ; 驱动清零才算消费
```

⇒ **参数是一个 4 字节 in/out 槽**：guest 写值（实测只有 1 或 2），驱动消费后清零。
它不是命令缓冲指针。§57 里 `GCMDBUF` 打印的内容其实是栈上的垃圾：

- `gpucmd_0.bin` 按 ELF32 dynsym 解就是 libObj 的 `.dynsym`
  （`st_name=0x1bb78, st_value=0x7198e1, st_size=0x2c4, st_info=0x12 GLOBAL FUNC,
  st_shndx=0x0a`，后面紧跟 `__gmon_start__` 等字符串）；
- `gpucmd_1/5/6.bin` 是线程栈。
- 所以那 8 个 dump 与 GPU 命令无关，**不能**用它们解 surface。

### 58.2 真正的 surface 描述符在 `/dev/dmpgles2` 的映射里

shim 新增 GSCAN：每 6 s 采样一次 GPU 映射，按"格式 `0x4401` + 640 + 480"定位描述符。
实测（`/setting/gsc_*` → 现改为 `/tmp/gsc_*`，见 §58.7）：

```
+0x00  消息 id（3/6/7/8…，递增）
+0x04  0x00000087（135）
+0x08  guest 虚拟指针
+0x0c  1
+0x10  guest 虚拟指针
+0x14  guest 虚拟指针
+0x2c  消息类型 = 5  ← surface 描述符
+0x38  surface 物理地址
+0x44  4
+0x4c  0x4401
+0x50  640   +0x54  480
+0x58  640   +0x5c  480
+0x68  640   +0x6c  480
+0x80  ffffffff
+0x84  ffffffff
```

实测值：开机与 t=48 s 的描述符都写 **`0x202E0200`**（渲染目标；present 不会改它）。
另一条 id=7 的消息里带 `{0x0ef9b3d0(虚拟), 0x2053c3d0(物理)}` —— 正好是 §58.3 的
Sugilite 命令表地址（0x2053C270..0x2053C3B0 环）。

工具：`decode-gpu-region.py`（解析描述符）、`diff-gpu-regions.py`（对比两次采样，
看队列里新追加了什么）。

### 58.3 Sugilite 2D 命令表（BAR+0x1804/0x1800）解码结果

一次完整运行（到菜单打开 + 按键）只有 **11 条命令表**，内容全同：

```
0116F000 02053BB0     ; 0116 = 工作表面指针（0x2053BB0）
0117F000 00000001     ; 0117 = 1
0044F000 00000000     ; 0044 = 0
0010F000 12345678     ; 0010 = 魔数
```

**没有 0x0202 draw，也没有 0x0112 dest-surface。**
⇒ 正常固件流程里 UI 像素不走这条 2D 列表（§17 的 154 字 draw 列表是 shim 强推
`openView` 之后才出现的）。

工具：`decode-cmdbuf.py <cmdbuf_*.bin>`（解析 `(op<<16)|0xf000` 序列、0x281 终止符、
0x0112/0x0589/0x0049/0x0047/0x0048/0x0105 等字段）。

### 58.4 present 通路解码（本轮关键）

给模型加了"可指定 dump 物理地址"：

```
A7R3_DUMP_PA=<hex>   主 dump 地址（默认 0x202E0200）
A7R3_DUMP_PA2=<hex>  第二个地址，定时 tick 交替 dump（frame_7xxx / frame_8xxx）
```

实测 6 次 present（`CMDBLIT`）：

```
CMDBLIT t=10819  src=202e0200 dst=23a80000 640x480 fmt=00004401 copy
CMDBLIT t=18625  src=202e0200 dst=23b16000 ...
CMDBLIT t=18799  src=202e0200 dst=23a80000 ...
CMDBLIT t=33086  src=202e0200 dst=23b16000 ...
CMDBLIT t=34519  src=202e0200 dst=23a80000 ...
CMDBLIT t=101739 src=202e0200 dst=23b16000 ...
```

- 源 = 渲染目标 `0x202E0200`；目的 = **显示双缓冲 `0x23A80000` / `0x23B16000`（交替）**。
- dump `0x23B16000` 的画面与 `0x202E0200` 完全一致（`fb1_7249.png` == `fb1_8248.png`），
  说明双缓冲里就是 UI 的最终像素。
- present 很稀疏（190 s 里 6 次），**不是 60 Hz**。

### 58.5 ★ 叠层落地上屏：改写 present 目标缓冲

给 `menu_overlay.c` 加 `put_flip()`：在 `/setting/nm_flip` 存在时，把同一帧
（Morton 打包后的 RGBA4444）直接写进 `0x23A80000` 和 `0x23B16000`；
`PUT_PANEL` 的 idle gate 只保留给渲染目标。

实测（`fl2`，菜单 100 s 打开、125 s 按键）：

```
NM_FIRST_PAINT=0x3c        （t=60 s 开始画）
NM_FLIP ×576               （每次成功写两块 flip 缓冲）
frame_7271.png             ← dump 0x23B16000：★ 我们的新菜单「我的菜单」
frame_8270.png             ← dump 0x202E0200：同一画面
```

两张图完全一致（sha256 相同 `a8ccc46a…`）：A7C2 风格标题「我的菜单」、8 个分类图标、
6 行中文（文件格式 2/2、JPEG画质 2/3、JPEG图像尺寸 2/12、纵横比 2/2、
全景:图像尺寸 2/2、影像质量 2/5）、`MENU 关闭` 页脚。
最后一次 present 在 t=101.7 s，之后 90 s 内画面一直保持我们的菜单
（136 帧里最长稳定 93 帧 ≈ 65 s）。

⇒ **新菜单第一次以"稳定、非抢帧"的方式上屏**：关键不是去抢渲染目标
（App 会不停重绘它），而是写 present 的目标缓冲 —— 它只在 present 时被改写。

**决定性对照（`fl4`）**：这次把主 dump 指向 `0x23A80000`（另一块 flip 缓冲），
它对这块的最后一次 present 是 **t=37.9 s**，而叠层从 **t=60 s** 才开始画：

```
CMDBLIT t=37951 n=5 src=202e0200 dst=23a80000 ...
... 127 s 后 ...
fl4_8234.png  ← dump 0x23A80000：仍然是我们的「我的菜单」
```

⇒ 画面只能来自我们的直写，且 **105 s 以上没有被覆盖**。
这就是叠层在真机上应当使用的落点。

### 58.6 解码结论（一条链）

```
App 画 UI → GLES 把像素写进 0x202E0200（$58.2 描述符里的 surface）
          → present：Sugilite blit 0x202E0200 → 0x23A80000 / 0x23B16000（$58.4）
          → 显示控制器扫描双缓冲
叠层正确位置 = 双缓冲（以及渲染目标，作为即时反馈）
```

### 58.7 新增/更新的工具

| 工具 | 作用 |
| --- | --- |
| `patch-bionz-cmddec.py` / `patch-bionz-cmddec2.py` | 模型侧 CMDDEC/CMDBLIT/CMDSUB 解码日志 |
| `patch-bionz-dump-pa.py` | 模型可按任意物理地址 dump（A7R3_DUMP_PA/PA2） |
| `patch-shim-gpuscan.py` / `-fix.py` | shim 侧 GSCAN：采样 dmpgles 映射并 dump 区域 |
| `build-gpuscan.sh on/off` | 构建/安装 GSCAN shim，或恢复基线 shim |
| `decode-cmdbuf.py` | 解 Sugilite 2D 命令表 |
| `decode-gpu-region.py` | 解 GPU 映射里的 surface 描述符 |
| `diff-gpu-regions.py` | 对比两次采样，看队列新增/变化 |
| `run-cmddec.py` | 起 QEMU + 注键 + 采 CMDDEC/CMDBLIT/CMDSUB + 收帧 |
| `get-tmp-files.sh` / `get-setting-files.sh` | 从 img10 / img2 取 guest 侧 dump |
| `set-overlay-gates.sh on/off` | 开关 `/setting/newmenu`、`/setting/nm_flip` |
| `patch-overlay-flip.py` | 把叠层写到 present 目标缓冲 |

### 58.8 真机上的含义（待 service mode 验证）

- 真机 LCD 扫的就是双缓冲之一；把菜单写进两块缓冲即可稳定显示，
  无需与 App 的重绘赛跑。
- 需要确认：真实扫描地址是否恒为 `0x23A80000/0x23B16000`（QEMU 里 6 次 present
  的 dst 只有这两个值，且格式固定 0x4401/640×480）。
- 写 flip 缓冲目前从 `/dev/mem` 直接写，未拿官方写锁；真机上建议先用
  `GRM_bitmapWriteLockBits` 找到对应的 bitmap 再写，避免与 present 并发冲突。

## 59) 真机只读探针：显示通路的实测事实（2026-11-14 下午，A7R3 实机）

> 全程只读：只从 `/dev/mem` 读、只把结果写到 SD 卡，没有写相机任何内存/分区。
> 结论先说：**service 固件里那三块 surface 都是空的，屏幕上的照片是"推"过去的**，
> 因此"控制器扫哪块"这件事只读 dump 定不了；而且**正常模式没有 shell**。

### 59.1 通道与工具

service mode（0336）+ USB 连接，复用 `analysis/dump_to_sdcard.py` 的会话框架。
本轮新增（都在 `analysis/` 或 `analysis/qemu_a7r3/`）：

| 脚本 | 作用 |
| --- | --- |
| `dump_display_probe.py` | 一次会话：文字证据 + 读 0x202E0000 / 0x23A80000 / 0x23B16000 各 614400 B |
| `dump_mem_range.py` | 读任意物理区间到卡（分块 + 逐块校验大小） |
| `dump_mem_ranges.py` | 多区间版本（一次会话读完几段） |
| `dump_regs_probe.py` | 读 MMIO 窗口（sugilite 0xf1000000 / liro 0xf2a00000）——**实测读回 0 字节，地址不对** |
| `dump_text_probe2.py` | 把 vmallocinfo/kallsyms/interrupts 等写卡 |
| `show_proc_text.py` | 直接从 shell 把小的 /proc 文件读回（不用拔卡） |
| `qemu_a7r3/raw-device2png.py` | 真机 16bpp dump → 4 种候选解码（线性/Morton × 4444/565） |
| `qemu_a7r3/find-ui-surface.py` | 在整段 RAM 里找"像 640×480 画面"的窗口 |
| `qemu_a7r3/diag-ram-dump.py` | 粗看一段 dump 哪里有数据、颜色种类多少 |
| `qemu_a7r3/decode-at.py` | 从任意 PA 解一帧（4 种布局） |
| `qemu_a7r3/decode-sugi-runs.py` | 在 RAM dump 里找 Sugilite 命令表并解出 surf/tex/area |
| `qemu_a7r3/find-sugi-lists.py` | 同上，只列候选 |
| `usb_enum_camera.py` | 枚举 Sony USB 设备/接口（正常模式验证用） |

### 59.2 固件/内核给出的布局事实

`/proc/cmdline`（真机，service 模式）：

```
mem=1G@0  mem=256M@0x80000000@1
memrsv=632M@0x18800000:uc          ← 632 MB 保留、无缓存
wbimem=0x3916000@0x20200000        ← 0x20200000 + 0x3916000 = 0x23B16000
wbi_cmpr.waddr=0x69A00000  wsize=0x100000  nbuf=12
Boot: 3.0.27_nl-rt106+ #1 SMP PREEMPT RT armv7l
```

⇒ **0x202E0200（渲染目标）和 0x23A80000/0x23B16000（present 双缓冲）全部落在
`wbimem` 这块保留区里**，而且 flip-A 正好是保留区最后 614400 字节，
flip-B 紧接其后。这解释了三者为什么相邻、地址固定。

`/proc/iomem`：只有 `System RAM 0-0x3fffffff`、`0x80000000-0x8fffffff`、
PCIe/dwc3/uart；**没有任何 display/framebuffer 保留区**。

`/proc/fb` 不存在，`/sys/class/graphics` 不存在 ⇒ 真机没有 fbdev，
显示完全走 `liro` + `grm_gles` 私有通路（与 QEMU 模型一致）。

`/proc/vmallocinfo` 里相关项（注意：这个内核不打印 `phys=`）：

```
0xb0a56000-0xb0a77000  135168  sugilite_dev_init+0xb4/0x1b8 [grm_gles] pages=32
0xb0880000-0xb0975000 1003520  devm_ioremap_nocache+0x44/0x74 ioremap
0xb0834000 ...        udif_ioremap+0x90/0xc4 ioremap
```

### 59.3 真机内存里找到了 Sugilite 命令表 —— 与 QEMU 同构

在 0x20000000-0x23FFFFFF 的 dump 里，0x2053C270..0x2055xxxx 有大量命令表。
用 `decode-sugi-runs.py` 解出的典型一条（`pa=0x20549260`）：

```
dest-surface 0x202e0200   area 640x480   texture 0x2275ea00 (24x24)
rect (592,0)-(634,38)     color 0xff00ff70
```

`decode-cmdbuf.py` 的完整解码：

```
0047f000 00000250   rect0 (592,0)
0048f000 0026027a   rect1 (634,38)
0049f000 01e00280   area 640x480
0589f000 044ebd40   texture PA=0x2275ea00
... 0202f000 10000004   draw
```

⇒ **真机 UI 就是"纹理 → 合成到 0x202E0200"，与 QEMU 解出的模型完全一致。**
另外还有一条把 640×480 整屏纹理（`tex=0x22376280, texsz=0x028001e0`）
blit 到 0x202E0200 的表——就是照片/整屏内容的上屏路径。

### 59.4 但 service 模式下这些 surface 是空的

第一次探针（`dump_display_probe.py`）直接读三块候选：

```
fb_render.bin  (0x202E0000)  614400 B：只有前 0x200 字节有数据（一张指针表），
                                       0x202E0200 之后全 0
fb_flipA.bin   (0x23A80000)  614400 B：全 0
fb_flipB.bin   (0x23B16000)  614400 B：全 0
```

前 0x200 字节的样子（`0x202E0000`）：

```
+0x00: 00000000 000001e3 00000008 ...
+0x14: 01d807a0 802e0000
+0x30: 802e0030 802e0030 802e0038 802e0038 ...   ← 指向 0x802Exxxx 的指针表
```

⇒ 这块此时是别的子系统的分配器结构，不是画面。

### 59.5 整段内存扫描：照片不在这些区间

- **0x20000000-0x23FFFFFF（64 MB）**：大部分为空；有数据的块颜色种类只有 21–615 种
  （结构体/文本，不是照片）；0x22376280 那块"640×480 纹理"解出来全黑
  （100% 非零但值形如 0x0001）。
- **0x80000000-0x81FFFFFF（32 MB）**：100% 非零、颜色种类 ~1190，解码是**纯噪声**
  —— 这是 `wbi_cmpr` 那条压缩/加密通路的工作缓冲，不是可识别帧。

⇒ **屏幕上那张照片的源缓冲不在这 96 MB 里**；结合 59.4，"面板显示的是已经推送过去的
画面，而不是控制器持续扫描的内存"这一解释最吻合（面板侧持有/GRAM，或帧已推完释放）。

### 59.6 正常模式没有 shell（A 路线关闭）

正常开机 + 电脑遥控：

```
WPD  ILCE-7RM3   USB\VID_054C&PID_0C33
pyusb: Sony devices: 0        ← 接口被 Windows WPD/MTP 驱动占用
pmca connect: GenericUsbException（写初始命令就失败）
```

⇒ 正常模式只暴露 MTP/WPD；那条 service shell 通道（usbg_sen）只在 0336 固件里放行。
**没有"正常模式只读读内存"这条路。**

### 59.7 因此还差什么（诚实结论）

| 问题 | 状态 |
| --- | --- |
| 真机 UI 画到哪 | ✅ 0x202E0200（命令表实测） |
| 真机 present 到哪 | 🟡 QEMU 实测 0x23A80000/0x23B16000；真机命令表里也是同一块 wbi 保留区 |
| **LCD 到底扫哪块** | ❌ 只读手段无法确定（service 固件不刷新这条通路） |
| 真机像素格式/瓦片布局 | ❌ 同上（拿不到一块"正在显示"的缓冲） |

要闭环只剩两条路：

1. **写测试**（可逆）：正常固件里往 0x202E0200 写测试图案，再往 0x23A80000 写，
   看屏幕哪次变化。需要能跑代码 ⇒ 需要改相机里的 preload/分区（写 flash）。
2. **真机 payload**：照 zve10 的做法，把叠层 .so 放进 `/usr/tool/` 并改 preload 规格，
   在正常固件里跑我们的叠层（同时可打印"当前显示缓冲"的探测结果）。

两条都要写相机存储，不在"只读"范围内 —— 需要单独确认。

### 59.8 误判记录（避免重犯）

- `0xf1000000`（sugilite）/`0xf2a00000`（liro）这两个 MMIO 基址**在真机上读回 0 字节**，
  它们来自模型中沿用的 A7M3 常量，不是 A7R3 的实测值。
- liro.ko / grm_gles.ko 里**没有**这两个基址常量（ioremap 的地址来自内核平台数据），
  所以"从模块里抠基址"这条路也不通。
- `/proc/vmallocinfo` 在这个 3.0.27 内核上**不打印 phys=**，无法直接拿 ioremap 物理地址。

## 60) 新 UI 真适配（一）：固件字体 + 真实字符串 + 触摸轮询（2026-11-14 傍晚）

目标（本轮）：新 UI 不止"能画"，还要**用固件自己的文字和字体**、**能点**、
并弄清"改值"的 API。

### 60.1 ★ 找到原生字体，并抽出 TrueType

真机 `fontlist.dat` 登记：

```
FONT_S_CHINESE  /tmp/images/MYingHeiC-GB18030-SJ_wIcon.ltt
FONT_ICONS      /usr/share/app/Sony_DI_Icons.ttf
```

`/tmp/images/` 是**运行时目录**，QEMU 跑过真固件后 img10（guest /tmp）里就有这两个字体，
直接取出。`.ltt` 是 Sony 的容器：ASCII 头 + `embeddedoffset=2984 / embeddedsize=1955448`，
里面是一份**完整的 TrueType**（0x00010000，含 GSUB 与 GB18030 全字集）。

⇒ `analysis/fonts/myinghei_gb18030.ttf`（1.95 MB），中文渲染与原生菜单完全一致。
图标字体里的 PUA 码位（U+E08C 等）在 TTF 里是 .notdef —— 图标不在字体里，见 60.3。

### 60.2 ★ 字符串表：resid → 文案

`CmnViewSettingNode` 的标题/说明是 `0x02xxxxxx` 的 resid。实测映射规则：

```
resid 0x0204E146  ->  字符串 id 0x1E04E146  ->  "版本"
resid 0x0204E30D  ->  字符串 id 0x1E04E30D  ->  "显示本机软件版本和镜头版本。"
```

即 **id = 0x1E000000 | (resid & 0xFFFFFF)**，字符串在
`/usr/share/app/string_simplifiedchinese.uxc` 里。容器格式：

```
记录 = [u32 id][UTF-8 文本][NUL][补到 4 字节]
```

叠层（ARM payload）在运行时打开该文件、解析出 7210 条记录，按 resid 取文案。
实测 6 行的标题与值全部为固件原文：

```
RAW文件类型      未压缩
JPEG影像质量     精细
JPEG影像尺寸     L: 36M
纵横比           16:9
全景: 影像尺寸   宽
影像质量         RAW&JPEG
```

工具：`qemu_a7r3/uxc-strtable.py`（离线导出，7266 条）、`uxc-offset-test.py`、
`render-pua.py`、`ltt-probe.py` / `ltt-extract.py`。

### 60.3 标题的 PUA 图标前缀

部分文案带私有区前缀，例如 `'\ue08cJPEG影像质量'`（U+E08C 是图标码位）。
原生菜单行**不画图标**，字体里也没有对应字形 ⇒ 叠层在渲染前剥掉
U+E000–U+F8FF 与控制字符，和原生行为一致（见 `skip_cp()`）。

### 60.4 字形图集（叠层用）

`qemu_a7r3/bake-font-atlas.py` 用上面那份 TTF、26 px、4-bit alpha（正好匹配
RGBA4444 面板）烘出 **1481 字形 / 365 KiB** 的图集（`payloads/font_atlas.h`），
叠层运行时按 UTF-8 解码 + 二分查找 + alpha 混合，任意文案都能画。
实测行距 48–50 px、字高约 26 px，与原生一致。

### 60.5 ★ 设值 API 的实测语义：`selected` 是 SR 号，不是下标

一次 `NM_SELFTEST`（叠层自测）得到：

```
parent: count=2 selected=1 state=1   props: key=0xe val=0x1b3
child0: 已压缩  state=2  key=0xf val=0x0   key=0x10 val=0x0
child1: 未压缩  state=1  key=0xf val=0x1   key=0x10 val=0x1
```

| 调用 | 结果 |
| --- | --- |
| `setItemSelectedByIndex(0)` | **无效**，selected 仍为 1 |
| `setItemSelectedBySRNum(2)` | selected 1→2，两个子节点的 `state` 互换 |
| `setItemSelectedByDiffSRNum(...)` | 同上（其后调用无进一步变化） |
| 父节点 `setItemSelected()` | state 1→2（只动状态标记） |
| 子节点 `setItemSelected()` | selected 回到 1 |

⇒ 两个事实：
1. **`selected` 存的是当前选项的 SR 号，子节点的 `state` 是该选项的 SR 号**，
   取值必须按 SR 匹配（叠层已改为 `child_by_sr()`），不能当下标用。
2. `setItemSelectedBySRNum` 只把节点树的选择状态改掉，**摄像机设置本身没变**
   （文案仍是"未压缩"，父节点 props 完全不变）。它更像是**入站同步**接口：
   外部设置变化时用来刷新节点树。

所以"真正写进相机"的入口还没找到。下一步方向（按优先级）：

1. 查 **viewSettingMenu / viewUnified2** 里"用户确认选项"那条路径：它一定调用了某个
   写设置的对象（setting manager / model），把节点 SR 号翻译成 ParameterId；
   可从 CautionConfig 的导入符号和 viewUnified2 的字符串/表反推。
2. 备选：**驱动原生菜单**。我们的叠层盖在上面，但仍可向输入队列注入
   DOWN/ENTER，让原生路径完成写值——稳但需要知道目标项在原生菜单中的位置。

### 60.6 ★ 触摸：轮询 libObj 的"最后触摸点"，不打补丁

`InputService::callback_tp` 的尾部：

```asm
ldr r3,[pc,#0x68]     ; lit at 0x3c3338
ldr r3,[r5,r3]        ; r5 = GOT
strd r10, r11, [r3]   ; 存 x, y
```

解出的地址链：

```
GOT          = 0x0121a3c0
slot         = GOT + 0x2c64 = 0x0121d024   (R_ARM_RELATIVE, addend 0x0129eb20)
touch struct = libObj + 0x0129eb20, [0]=x, [4]=y
```

叠层**每秒 5 次轮询**这两个字（不写、不 hook），实测在 QEMU 里读到 (0,0) 并触发
反应；用合成坐标（`simulate_tap`）验证了"点分类条切页 / 点行选中 / 同行二次点击=应用"
的完整链路。因为不打代码补丁，§49 那种"一有输入就崩"的风险没有出现。

> 注意：真机上触摸还会同时驱动原生的触控 AF 逻辑（我们盖住了界面但没拦事件）。
> 若要"独占触摸"，后续需要把 `InputService::setTpEventSw(false)` 或按区域
> `setTpEnableArea` 关掉原生区域；本轮先保证"能读到坐标并驱动新 UI"。

### 60.7 本轮产物

| 文件 | 作用 |
| --- | --- |
| `analysis/fonts/myinghei_gb18030.ttf` | 从 .ltt 抽出的原生中文 TrueType |
| `analysis/fonts/Sony_DI_Icons.ttf` | 原生图标字体（TTF，不含 PUA 图标） |
| `payloads/font_atlas.h` | 1481 字形 4-bit alpha 图集（365 KiB） |
| `qemu_a7r3/bake-font-atlas.py` | 图集烘焙器 |
| `qemu_a7r3/ltt-probe.py` / `ltt-extract.py` | .ltt 容器解析 |
| `qemu_a7r3/uxc-strtable.py` | resid→文案表导出 |
| `qemu_a7r3/fontlist` 相关脚本 | `copy-fonts.sh`、`find-fonts.sh`、`inspect-fonts.sh` |
| `payloads/menu_overlay.c` | 运行时：图集文字、字符串表、SR 语义、触摸轮询、自测 |
| `qemu_a7r3/arm-selftest.sh` / `run-overlay-commit.py` | 自测开关与驱动脚本 |

当前镜像状态：`newmenu.so`（本版）+ 基线 shim，`/setting/{newmenu,nm_flip}` 已开，
`/setting/nm_selftest` **已关**（避免开机自动改值）。

## 61) 新 UI 真适配（二）：SR 号语义、present 守卫、子节点越界教训（2026-11-14 晚）

### 61.1 ★ `CmnViewSettingNode` 的 SR 接口（导出，可用）

用 dlsym 实测（返回值约定：**1 = 成功**，0 = 失败，和 setter 的 `cbz r0` 一致）：

| 接口 | 实测结果（RAW文件类型行，count=2） |
| --- | --- |
| `getSRNumSelectedItem(int&)` | rc=1，SR=1 → 当前值 = SR 1 = 未压缩 |
| `getSRNumByIndex(1,int&)` | rc=1，SR=1 |
| `getSRNumByIndex(2,int&)` | rc=1，SR=2 |
| `getSRNumByIndex(0,int&)` | rc=0（**0 不是有效序号**） |
| `isItemSelectedByIndex(1)` | 1（选中） |
| `isItemSelectedByIndex(0)` | 0 |

⇒ **选项按 SR 编号，1..count；索引 0 是表头/占位，不是选项。**
`selected` 就是当前选项的 SR。子节点的 `state` 并不等于 SR（之前误判），
真正的 SR 要用 `getSRNumByIndex` / `getSRNumSelectedItem` 取。

设值：`setItemSelectedBySRNum(sr)` 不仅改 `selected`，**设置本身也真的改了** ——
见 61.2 的 settle 实测。

### 61.2 ★ settle 观测：改动 10 s 后仍然生效

`self_test` 对 6 行各做一次"设到另一个 SR"，10 s 后再读一遍（`NM_SETTLE`）：

```
row0 sel=2 val 未压缩
row1 sel=2 val 标准        ← 原 精细
row2 sel=2 val L: 18M      ← 原 L: 36M
row3 sel=2 val 3:2         ← 原 16:9
row4 sel=2 val 宽
row5 sel=2 val 超精细      ← 原 RAW&JPEG
```

⇒ **四行的值发生了真实变化，并且 10 s 内没有被固件同步回去**。
这就是"新 UI 真正生效"的证据：叠层通过固件自己的 `CmnViewSettingNode` API
改值，固件设置系统接受了它。

（row0/row4 的文案与改前相同，是因为这两个设置的两个选项文案恰好一样/映射差异，
不是没生效——`selected` 已经变成 2。）

注意：QEMU 里改的是**镜像副本**，不影响真机。

### 61.3 ★ present 守卫：20 ms 金丝雀，旧 UI 一覆盖就立刻重画

叠层原来 5 Hz 整帧写 flip 缓冲；App 每次 present 会把它们覆盖成原生画面，
最长 200 ms 的"原生闪现"。现在改成：

```
worker 20 ms 一跳：
  每跳   ：读 flip A/B 首字（金丝雀），与本机上次写入值比较
           不一致 ⇒ 说明 App 刚 present，立刻 put_flip() 重画 + 更新金丝雀
  每 5 跳：完整渲染 + poll_key/poll_touch/自测/日志
```

flip 缓冲用**一次 mmap** 覆盖（`/dev/mem` @ 0x23A00000，长 0x200000，
A=0x23A80000、B=0x23B16000 都在内），实测映射成功（`NM_FLIPMAP m=0x1c06c000`，
A 映射后首字 `0x000f` = 黑）。日志里 App present 时应出现 `NM_REPRESENT`。

### 61.4 教训：子节点数组边界

`count` 是**选项个数**，`children[]` 的有效下标是 0..count（0 为占位）。
直接把 `selected`（SR，1..count）当下标访问 `children[sel]` 会出现 `sel==count`
的越界读；一旦越界值是垃圾，`node_title_resid()` 会去解引用它，
表现为 `page translation fault at 0x0000000d`（本轮踩到一次）。

修法：所有 `children[i]` 解引用前都过 `plausible()`（0x10000..0x80000000 且偶数），
`value_current()` 先试 `children[sel]`，再退回 `children[sel-1]`。

### 61.5 ★ 触摸 → 改值 全链路（合成坐标实测）

```
NM_TAPTEST → NM_TOUCH x=0x143 y=0x92 → NM_TAP row=0        （第一次点：选中该行）
NM_TAPTEST → NM_TOUCH x=0x146 y=0x90 → NM_TAP2 row=0
            → NM_COMMIT row=0                              （第二次点：调用 SR API 改值）
```

规则：同一行 2.5 s 内第二次点击 = 应用；点顶部图标条 = 切分类。
轮询从 100 ms 提到 **20 ms**（每跳只读两个 int），快速点击也能抓到。

### 61.6 四条目标的当前状态（QEMU 内全部闭合）

| 目标 | 实现 | 证据 |
| --- | --- | --- |
| 新 UI 盖住旧 UI | 帧写入 0x202E0200 + 两块 present 缓冲 | `st5_7227.png` 等帧只有新菜单 |
| 不被旧 UI 渲染打断 | 20 ms present 金丝雀 + 立即重画 | `NM_FLIPMAP` 映射成功；守卫逻辑见 61.3 |
| 新 UI 真正生效 | `setItemSelectedBySRNum(SR)` | settle：精细→标准、L:36M→L:18M、16:9→3:2、RAW&JPEG→超精细，10 s 后保持 |
| 触摸功能 | 轮询 libObj+0x0129eb20 | NM_TOUCH/NM_TAP/NM_TAP2/NM_COMMIT 全链路 |

**尚未在真机验证**（用户当前无真机）：真机触摸坐标尺度、原生触控 AF 的干扰、
真机 60 fps present 下的闪烁窗口。这三项都需要一次真机会话。

## 62) 新 UI 真适配（三）：二级弹窗、右上角切换键、MENU=返回（2026-11-15）

用户提出的三个问题：
1. 点选中的那一条进不了二级页面 / 弹窗；
2. 新旧菜单切换都放到右上角，并给一个图标；
3. MENU 仍然当"返回"用（不要拿 MENU 当开关）。

### 62.1 ★ 二级页面（选项弹窗）

原来"同行二次点击 = 直接把值 +1"，语义不对（用户要的是进二级页）。改成：

```
点未选中的行        -> 选中该行
点已选中的行        -> 打开该行的二级页（选项弹窗）
弹窗里点某个选项    -> setItemSelectedBySRNum(SR) 后返回列表
点弹窗外面 / MENU   -> 返回列表（再按一次才退出新 UI）
```

弹窗内容来自固件自己的数据，不是手写表：

| 步骤 | 调用 | 说明 |
| --- | --- | --- |
| 选项个数 | `node->count` | 12 = JPEG 影像尺寸有 12 档 |
| 第 i 档的 SR | `getSRNumByIndex(node, i, &sr)` | 1 基；rc==1 才算成功 |
| 第 i 档的文案 | `children[i-1]` → `str_for_resid(title resid)` | **children 是 0 基** |
| 当前值 | `node->selected`（SR）与各档 SR 比对 | 打实心单选点 |

版面：标题 + `n/P` 页码 + 7 行选项（每行 40 px）+ `下一页` 条；行数按当前页
实际条数收缩，所以 5 档的那页不会留一片空白；弹窗以外用一次移位把亮度减半
（`(v>>1)&0x7777|0xf`，不做除法，ARM 上很便宜）实现"模态遮罩"。

列表行右侧新增 `>` 折线，表示"这一行有二级页"。

实测（QEMU）：
```
NM_POPUP open total=0xc   （JPEG 影像尺寸，12 档）
NM_POPUP cur=0x1          （L: 42M）
NM_POPUP page=0x8         （点"下一页"→ 第 2 页，L: 8.9M/S: 11M/S: 8.9M/S: 4.5M/S: 3.8M）
NM_POPUP tap=0x2 → apply idx=0x2 → sr=0x2 → selected=0x2
```
截图：`analysis/shots/d1-popup-p1.png`（7 档 + 下一页）、`d1-popup-p2.png`（第 2 页）、
`d1-applied.png`（列表值已变成 `S: 11M`）。

### 62.2 ★ `value_current()` 的 off-by-one（本轮修掉）

列表右侧的值原来走 `children[selected]`，比弹窗里的同一档**低一档**：
点了"精细"，列表显示"标准"。原因就是 `children[]` 是 0 基而 `selected` 是 SR。
现在统一走 `getSRNumByIndex` 把 SR 映射回下标再取 `children[i-1]`，两边一致。

### 62.3 ★ 右上角切换键（新旧菜单都能看见）

同一个 44x44 按钮，两种状态下都画在**同一位置** `(592,4)`（触控热区
`x>=584 && y<=52`）：

- 新 UI 打开时：画在叠层帧里（两个相向箭头 = "交换"）；
- 新 UI 关闭时：只有 1936 个像素，直接补丁到 present 缓冲 + 渲染目标
  `0x202E0200` 的右上角（`panel_put_badge()`），写入前先比对，
  已经是我们画的那块就跳过 —— 所以原生 UI 重绘时最多被"补"一次，
  不会每 20 ms 去抢。

原生界面上这一点点补丁是**可见的**：`shots/c1-native.png` 右上角就是那个
绿色双箭头按钮，点它就切到新 UI。

### 62.4 ★ 关闭时要"交还屏幕"：抓帧 + 回写

原来的关闭只是"停止绘制"，屏幕会一直停在我们的菜单上——因为**原生 App 只有
在自己有新内容时才 present**，不会因为"我们收手了"而重画。
（早期 web 端 `/nmtoggle` 是靠再补一个原生 DOWN 键强制重绘，属于绕路。）

现在的做法：

```
打开叠层前：panel_grab()  把 0x202E0200 的 640x480 读进 g_native_bg
关闭叠层时：panel_restore() 把 g_native_bg + 右上角小按钮写回
            0x202E0200 与两块 flip 缓冲，然后静默 1 s（pause_paint）
```

`g_native_bg` 614 KB，加在 .bss（总 .bss 2.38 MB）。

### 62.5 ★ MENU=返回：不打补丁，用"原生 present"判定

先试了 patch `snd_InfraInputEvent_to_InputServant`（原 §60 的观察点）来抓
MENU 键，钩子本身装得上（`NM_hook_rc=0`），但**一按键 guest 就掉进
`Exception>` 调试 shell**：这个函数在输入热路径上，8 字节代码补丁会被
"正在执行"的线程踩到（同 §49 的教训）。已删除该钩子，`/setting/nm_keyhook`
不再有任何作用。

替代方案（纯只读、零补丁）：

```
叠层开着 + 金丝雀变了（App 又 present 了）+ 距上次触摸 > 1.5 s
   -> 说明用户按了原生按键，原生界面已经换了 -> 当作"返回"
      有弹窗：先关弹窗（吸收后续几帧）
      没弹窗：交还屏幕（overlay_close(0)，不回写旧帧，因为 App 自己会画）
```

依据是 §58.5 的观察：App 只在"有变化"时 present，静止时不刷新，所以
present = 用户操作了原生 UI。1.5 s 的宽限用来避免"我们自己触发的那次原生
触摸反馈"被误判成返回。

同时保留明确入口：页脚 `MENU 返回`、右上角按钮、`/tmp/nm_key` 的 `b`。

**实测状态（诚实版）**：QEMU 里 `inject-key 0x1ab` 这次**没有让原生界面重绘**
（`NM_REPRESENT`/`NM_NATIVE_BACK` 都没出现，画面也不变），所以"present 判定返回"
这条只做了机理推导，没在 QEMU 复现；真机上按物理 MENU 一定会触发原生重绘，
届时看 `NM_NATIVE_BACK` 日志即可确认。确定可用的是 `b`/页脚/右上角三条路径
（`NM_BACK close` + `NM_RESTORE` 已验证）。

### 62.6 触摸注入通道（QEMU 专用，方便验证）

QEMU 里没有面板驱动写触摸单元，所以叠层额外支持：

```
/tmp/nm_touch 里写 "x y"，再写 /tmp/nm_key = 'T'
  -> touch_from_file() 解析后直接走 handle_touch(x,y)
```

这样跑的是**真机同一套命中测试**（分区条/行/弹窗/按钮），而不是像以前的
web 端那样在宿主侧自己模拟按键。`live-menu.py` 的 `/nmclick` 已改成走这条路。

注意：QEMU 里一帧要写 3×614400 像素（渲染目标 + 两块 flip，全部软件模拟），
点一下到画面更新要 1–3 s；`/snap` 只是复制 `latest.png`，抓太早会拿到上一帧。
调试时请 `sleep 5` 以上再截图，或直接用 `nm-drive.py`（本轮新增）。

### 62.7 本轮产物

| 文件 | 变化 |
| --- | --- |
| `payloads/menu_overlay.c` | 二级弹窗、切换按钮、抓帧/回写、present 判定返回、off-by-one 修复、日志只写文件 |
| `qemu_a7r3/live-menu.py` | `/nmclick` 改为合成触摸；`/nmkey` 增加 `b`(返回)/`n`(开二级页)；页面加按钮 |
| `qemu_a7r3/nm-drive.py` | 新增：命令行驱动查看器（log/snap/click/key/nkey/sh） |
| `qemu_a7r3/set-newmenu-flags.sh` | 新增：开关 keyhook/badge/selftest/flip/overlay 门铃 |
| `analysis/shots/` | 本轮验证截图 |

### 62.8 仍未验证 / 已知限制

- 真机：触摸坐标尺度、原生触控 AF 与我们的点击是否打架、真机 present 频率
  下"1.5 s 判定"是否会被正常刷新误触发（真机上如果原生界面会周期性刷新，
  这条判定要改成"内容变化幅度"而不是"有没有 present"）。
- `clean mode` / `NTSC-PAL` 这类固件里没构建选项的行，弹窗会显示"无可用选项"。
- 分类归属仍是按名字近似映射，不是从原生分页数据解出来的。

## 63) 两个实测回退（2026-11-15 第二轮）：旧菜单点击 + 警告框黑屏

用户反馈两件事：旧菜单点已选中的条目进不了二级页；弹警告框时黑屏只剩右上角按钮。
两条都复现并定位了，**不是同一个原因**。

### 63.1 旧菜单"进不了二级页"：其实是黑屏，不是没进去

A7R3 的原生菜单**不吃触摸导航**（§38：触摸只用于对焦确认）。所以网页里的点击
必须由宿主翻译成按键（移动选择 + ENTER）—— 这就是老的"点击→按键适配器"。

上一轮我把 `/nmclick` 一股脑改成"合成触摸给叠层"，于是**叠层关着时点旧菜单
什么都不会发生**。已修：叠层不可见 → 回退到适配器；叠层可见 → 合成触摸。
实测：

```
nmclick 300,248 -> {"action":"move","key":"DOWN","from":1,"to":2,"mode":"native"}
nmclick 300,248 -> {"action":"enter","mode":"native"}
```

第二次点击发出 ENTER 后，**原生确实弹出了二级页面**：一个警告对话框
（`此功能已…显示。` + `确定`），见 `analysis/shots/p2-native-dialog.png`
（这一张是把 `/setting/newmenu` 关掉、**完全不带 payload** 拍的）。

### 63.2 警告框黑屏：**先误判成小按钮，实际是查看器只 dump 了一块缓冲**

同一操作（DOWN、ENTER）时的现象：网页画面全黑，只剩右上角小按钮
（`shots/n4-native-enter.png`）。当时据此怀疑"小按钮写坏了原生帧"，但把按钮关掉后
**画面仍然是黑的**（只是没有按钮了）——说明按钮不是主因。

真正原因：QEMU 里的网页画面只 dump **一个固定地址**（`A7R3_DUMP_PA=0x202E0200`）。
App 的 present 是**双缓冲交替**的，某些视图（警告框）会被画到"另一块"上，
此时被 dump 的那块就是黑的 ⇒ 网页黑屏，而面板其实没问题。

决定性验证（§64）：模型改成**每 tick dump 三块**（渲染目标 + 两块 present），
宿主挑"最新的非全黑帧"。同一个警告框立刻正常显示，**而且小按钮也还在**：

`shots/y2-dialog.png`（无按钮）、`shots/y4-dialog-badge.png`（有按钮，画面同样正常：
感叹号图标 + `此功能当前被禁止。` + `确定`）。

⇒ 结论修正：小按钮**不会**写坏警告框；`0x202E0200` 也不是每时每刻都等于"屏幕上的内容"。

### 63.3 本轮实际保留的修改

1. **抓帧全黑就丢弃**：`panel_grab()` 校验和==0 → `g_saved_ok=0`，关闭时不回写黑帧
   （`NM_GRAB empty` / `NM_CLOSE norest`）。这条仍然必要：叠层如果在原生首帧之前
   打开，抓到的确实是全黑缓冲，回写会把屏幕刷黑（这条是**真的**会黑屏）。
2. **小按钮的门铃改成 `/setting/nm_badge`**（存在=开）。默认由安装脚本创建，即默认开；
   想关掉就 `set-newmenu-flags.sh badge off`。留着门铃是因为在真机上如果出现
   "原生帧被写坏"的迹象，可以一键排除。
3. **关闭后可选"戳一下"原生**：`/setting/nm_kick` 存在时用 OSAL 注入
   DOWN 按下/抬起 + UP 按下/抬起，让原生重绘（净效果不动选择），替代回写旧帧。
4. **`g_dirty` 不再丢**：原来 `put_panel` 命中 idle gate（返回 -3）时先把 dirty 清了，
   于是"打开了但永远不画"；现在只有真正画成功才清。
5. **首帧清 `/tmp/nm_touch`**：上一轮残留的合成触摸会在开机瞬间把叠层打开
   （实测过：`NM_TF x=0x258 y=0x14 → NM_SWITCH open=0x1`）。
6. **`/sh` 观测窗口 3 s → 可配（默认 6 s）**，并在返回里滤掉 `NM_*` 噪声。
7. **右上角热区永远归叠层**：`/nmclick` 在 `x>=584 && y<=52` 时一律发合成触摸，
   不再被"翻译成按键"（否则叠层关着时点按钮会去切原生页签）。

### 63.4 ★ 又一个环境坑：/tmp 被写坏 → guest 只读挂载

反复 `pkill` QEMU 会在 guest 的 `/tmp` FAT 上留下脏位，下次开机内核**只读挂载**，
于是：叠层的 `/tmp/nm_key` 通道全失效（点了没反应）、日志也写不进去，
而画面看起来完全正常。现象极容易被误判成"代码坏了"。

诊断：`touch /tmp/x` → `Read-only file system`。
修：`analysis/qemu_a7r3/fsck-guest-tmp.sh check|fix`（宿主侧对 img10 跑 dosfsck）。
本轮就因此浪费了两次启动验证。

### 63.5 现状

- 旧菜单点击：恢复"点击→按键"，行选中 + ENTER 弹二级页（警告框）已验证。
- "警告框黑屏"：根因是**查看器只 dump 单块缓冲**（§64 已修），不是小按钮；
  另外"抓帧全黑回写"确实会把屏幕刷黑，已加保护。
- 新 UI：二级弹窗、右上角按钮、关闭回写原生帧、角标开关都正常。

## 64) ★ 三块缓冲同时 dump：警告框黑屏的真正解法（2026-11-15）

### 64.1 问题

模型里 `A7R3_SURF_TICK` 的定时 dump 原来只抓一个地址（默认 `0x202E0200`），
`A7R3_DUMP_PA2` 还只在奇数 tick 抓一次。App 是**双缓冲**present，警告框这类视图
只落在其中一块上 ⇒ 网页看到的永远是那一块，黑就黑到底。

### 64.2 改法

`hw/arm/bionz.c` 的 tick 回调改成每 tick dump 三块（`patch-bionz-3pa.py` 打补丁）：

```
frame_8000+seq  <- A7R3_DUMP_PA   (0x202E0200 渲染目标)
frame_7000+seq  <- A7R3_DUMP_PA2  (0x23A80000 present A)
frame_6000+seq  <- A7R3_DUMP_PA3  (0x23B16000 present B)
```

宿主侧 `live-menu.py` 的选取规则也改了：**在最近 4 s 的帧里挑"最新的非全黑帧"**
（采样统计非零字节 > 40）。不能用"非零像素最多"——警告框本身大部分是黑的，
而某块残留的菜单帧像素更多，会一直霸屏。

`see-menu.sh` 现在会：
1. 启动前 `fsck-guest-tmp.sh check`，脏了就自动 `fix`（见 §63.4）；
2. 同时导出 `A7R3_DUMP_PA/PA2/PA3`；
3. 用 `</dev/null` 跑，`live-menu.py` 检测到 stdin 关闭后进入 web-only 模式
   （以前 stdin 一 EOF 就直接退出，后台跑必死）。

### 64.3 验证

```
DOWN + ENTER（原生菜单）
  -> shots/y2-dialog.png        警告框正常：感叹号 + "此功能当前被禁止。" + 确定
  -> shots/y4-dialog-badge.png  同一个框，右上角小按钮也在，画面同样正常
右上角点击（叠层）
  -> shots/z1-open.png          新菜单打开
再次点击
  -> shots/z2-close.png         回原生菜单，画面完整（不是黑屏）
```

### 64.4 教训

- "画面上没有" ≠ "App 没画"。多缓冲/多 surface 的机器上，先确认自己看的是哪一块。
- 反过来也一样：报"黑屏"之前，先 dump 另外几块，再怀疑代码。



