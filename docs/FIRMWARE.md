# 从你自己的相机取固件（本仓库不附带任何固件）

> 这里只讲**怎么拿到你自己设备上的固件**，仓库里**不含**任何 Sony 固件、镜像、
> 字体、图标或其它专有数据。请只对**你拥有**的机器操作，取到的数据不要分发。
> 本文不涉及绕过签名、解锁或修改引导链的内容。

QEMU 需要一个 2 GB 的 raw eMMC 镜像 + 三个启动文件。它们都来自你自己的相机：

```
qemu-emmc-2g.img     由下面 dump 出来的分区按偏移拼成
vmlinux-console.bin  nflasha3 (/system) 里的内核
initrd-qemu.img      nflasha3 里的 initrd
av-cam.bin           nflasha3 里的相机固件（QEMU 加载到 0x035c6000）
```

## 1. 前置

- 一台 A7R3（ILCE-7RM3），USB 线，电量充足；
- Python 3 + `libusb`（Windows 上还需要 libusb 驱动）；
- [ma1co/Sony-PMCA-RE](https://github.com/ma1co/Sony-PMCA-RE)：提供 service mode 的
  终端（`SenserPlatformBackend.interactiveShell`）。仓库的
  `tools/real-device/` 里的脚本就是基于它写的。

## 2. 只读分区 dump（推荐）

相机切到 service mode 后，会从普通 MSC 身份重新枚举成 Senser（`02A9`/`0336`），
PMCA 给你一个**只读读取**用的 shell。用它把分区 `dd` 到 `/log`（500 MB，专门当
dump 暂存区），再用 PMCA 的 FileControl 把文件拉回电脑：

```sh
# 在 service shell 里（示例：/usr 分区）
dd if=/dev/nflasha15 of=/log/part15.bin bs=1M
# 其它分区同理：/dev/nflasha2、nflasha3、nflasha7、nflasha10 …
```

注意事项（踩过的坑）：

| 事项 | 说明 |
| --- | --- |
| 只读 | 只用 `dd if=…` 读，**不要写** flash；不要在 boot0 上做任何事 |
| 一次一件事 | service 会话很脆，dump 完直接断电，不要调 `auth.stop()` |
| 分块 | `/log` 只有 500 MB，大分区要分段（脚本里是每 64 MB 一段） |
| 校验 | 拉回电脑后 `sha256sum` 对齐，避免 USB 传输损坏 |
| 传输 | PMCA FileControl 拉 `/log/*.bin`；SD 卡方式也可以（`dump_boot0_sd.py` 用过） |

分区表（A7R3，取自实测；`img-part.py` 可以打印）：

| 分区 | 大小 | 文件系统 / 挂载点 | 内容 |
| --- | --- | --- | --- |
| nflasha1 | 8 MB | vfat（Updater） | 更新器 |
| nflasha2 | 20 MB | vfat → `/setting` | 设置 |
| nflasha3 | 48 MB | vfat → `/system` | **内核 / initrd / av-cam.bin** |
| nflasha4 | 12 MB | raw | MPR Flash Backup |
| nflasha5 | 60 MB | WBI → `/wbi` | 热启动镜像 |
| nflasha6 | 16 MB | vfat | Jiritsu 区 |
| nflasha7 | 8 MB | ext2 → `/rootfs` | 根文件系统 |
| nflasha10 | 12 MB | vfat → `/tmp` | 临时 |
| nflasha11 | 500 MB | vfat → `/log` | 日志 / dump 暂存 |
| nflasha12 | 40 MB | vfat → `/cert` | 证书 |
| nflasha13 | 4 MB | vfat | UpdaterWork |
| nflasha15 | 300 MB | ext2 → `/usr` | **主应用 / UI / 字体 / 资源** |
| nflasha16 | 140 MB | raw → `/android/system` | Android 系统 |
| nflasha17 | 100 MB | vfat → `/android/data` | Android 数据 |
| nflasha18 | 20 MB | vfat → `/lens` | 镜头数据 |
| nflasha23 | 2 MB | prfile → `/PMBP` | PMBP |

## 3. 组装 QEMU 用的 eMMC 镜像

分区在镜像里的位置（LBA，512 B/扇区；装载偏移 = `524288 + lba*512`，
`payloads/scripts/img-part.py` 可以打印完整表）：

```
设备       LBA 起点        镜像内偏移     大小
nflasha1     2048          0x00180000     8 MB
nflasha2    18432          0x00980000    20 MB   /setting
nflasha3    59392          0x01D80000    48 MB   /system
nflasha5   159744          0x13800000    60 MB
nflasha6   284672          0x22C00000    16 MB
nflasha7   319488          0x27000000     8 MB   /rootfs
nflasha10  374784          0x2DC00000    12 MB   /tmp
nflasha11  401408          0x31000000   500 MB   /log
nflasha15 1531904          0xBB000000   300 MB   /usr
```

用 `truncate` + `dd conv=notrunc` 把它们拼成 2 GB：

```sh
truncate -s 2G qemu-emmc-2g.img
dd if=part2.bin  of=qemu-emmc-2g.img bs=512 seek=18432   conv=notrunc
dd if=part3.bin  of=qemu-emmc-2g.img bs=512 seek=59392   conv=notrunc
dd if=part7.bin  of=qemu-emmc-2g.img bs=512 seek=319488  conv=notrunc
dd if=part10.bin of=qemu-emmc-2g.img bs=512 seek=374784  conv=notrunc
dd if=part15.bin of=qemu-emmc-2g.img bs=512 seek=1531904 conv=notrunc
```

`vmlinux.bin` / `initrd.img` / `av-cam.bin` 从 `nflasha3` 的 vfat 里直接拷出来
（挂载：`mount -o loop,offset=$((524288 + 59392*512)) part3.bin /mnt`），
QEMU 用 `-kernel` / `-initrd` 传内核和 initrd，`av-cam.bin` 用
`-device loader,file=av-cam.bin,addr=0x035c6000,force-raw=on` 加载。

## 4. 另一条路：官方更新包

Sony 官网会发布相机更新包（你自己下载，版权归 Sony）。里面有 updater 和
`/system` 里的东西，但没有 boot0/EXBL，也**无法重打包**回相机
（CXD90045 的更新包是签名加密的 —— 本项目不做这件事）。
它只适合拿来取内核/资源做研究，不能当 eMMC 镜像用。

## 5. 不要做的事

- 不要尝试重打包固件走官方升级（签名会失败，且有变砖风险）；
- 不要写 `nflasha4`/boot0 之外的引导数据；
- **不要把你 dump 出来的东西提交到这个仓库或再分发**（.gitignore 已排除
  `*.img`、`dumps/`、`*.bin`、字体与 UXC 资源）。
