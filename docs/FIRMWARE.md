# 从你自己的相机取固件（完整 dump 指南）

> 本仓库**不包含**任何 Sony 固件、eMMC 镜像、字体、图标或其它专有资产。
> 运行需要你自己合法获得的固件；Sony / ILCE / A7R3 等商标归 Sony 所有。
>
> 本文只讲对**你自己的机器**做**只读**读取，以及把读到的字节拼成 QEMU 镜像；
> 不涉及绕过签名、解锁、重打包或修改引导链。dump 出来的数据不要提交到本仓库、
> 不要分发（`.gitignore` 已排除 `*.bin`、`*.img`、`dumps/`、`*.log` 等）。

QEMU（`-M cxd90045`）需要下面四样东西，全部来自你自己的相机：

| 产物 | 来源 | 说明 |
| --- | --- | --- |
| `qemu-emmc-2g.img` | 第 2–7 节 dump 出来的分区按偏移拼成 | 2 GiB 用户区 + 512 KiB boot 区 |
| `vmlinux-console.bin` | `nflasha3` 的 `VMLINUX.BIN` | QEMU 版还要打适配补丁，见 §8 |
| `initrd-qemu.img` | `nflasha3` 的 `INITRD.IMG` | 同上 |
| `av-cam.bin` | `nflasha3` 的 `AV-CAM.BIN` | `-device loader,...,addr=0x035c6000` |

最短路径（细节在后面）：

```sh
# 1) service shell 里，一次一个分区；/log 是 500 MB 的暂存区
dd if=/dev/nflasha15 of=/log/nflasha15.bin bs=64k count=4800
# 2) 用 PMCA 的 FileControl 把 /log/nflasha15.bin 拉回电脑，核对大小 / SHA-256
#    2、3、7、10 同理（块数见 §4）
# 3) 拼镜像（会建 MBR、按偏移放数据、校验 MBR 签名）
bash payloads/scripts/make-emmc-image.sh -d /path/to/dumps -o qemu-emmc-2g.img --verify
# 4) 从 nflasha3 取内核 / initrd / av-cam（对单分区 dump 是 offset=0）
sudo mount -o loop,ro /path/to/dumps/nflasha3.bin /mnt/p3
```

---

## 1. 需要什么

- 一台 A7R3（ILCE-7RM3）；A7R3A / A7M3 见 §9；
- USB 线、电量充足（不要用副厂电池）；
- Python 3 + `libusb`（Windows 上用 Zadig 给 service 模式的新设备装 WinUSB/libusb 驱动）；
- [ma1co/Sony-PMCA-RE](https://github.com/ma1co/Sony-PMCA-RE)：提供 service mode 的终端
  （`SenserPlatformBackend.interactiveShell`）和文件拉取（FileControl）；
- Linux 或 WSL：拼镜像要用 `sfdisk` / `dd` / `truncate`；
- 磁盘空间：最小组合约 388 MiB，完整研究组合约 1.3 GiB；镜像本身是稀疏文件。

## 2. 分区表：`nflashaN` 是什么

A7R3 的分区表在固件包的 `0300_partconf/partinf.conf` 里（`start,size,type,flag`）。
真机上每个分区是一个块设备，Sony 的 `devno.ko` 把它们命名成 `/dev/nflashaN`。
在 QEMU 里，guest 内核是从 eMMC 用户区（镜像 `0x80000` 起）的 MBR 建出
`mmcblk0pN`，再映射成 `nflashaN` 的；启动日志里能看到：

```
nflasha: nflasha1 nflasha2 nflasha3 nflasha4 < nflasha5 ... nflasha25 >
```

`< >` 是 DOS 扩展分区的标记，所以镜像里的 p4 是扩展分区容器，p5 起是逻辑分区。
**这带来一个限制**：`nflasha4`（12 MiB MPR 备份）在镜像里没有数据区，见 §7.4。

### 2.1 A7R3 的分区（大小取自 partinf.conf）

| 分区 | 大小（字节） | 文件系统 / 挂载 | 内容 | 启动必需 |
| --- | ---: | --- | --- | :---: |
| nflasha1 | 8,323,072 | vfat | Updater（第二套启动对） | 否 |
| nflasha2 | 20,971,520 | vfat → `/setting` | `kemco.txt`（主内核参数）、`Backup.bin` 设置库、`updater/dat4` | **是** |
| nflasha3 | 50,331,648 | vfat → `/system` | `VMLINUX.BIN`、`INITRD.IMG`、`AV-CAM.BIN`、`BONOBO.BIN` 等 | **是** |
| nflasha4 | 12,582,912 | raw | MPR Flash Backup（CMMe 寄存器备份） | 否（但需可写） |
| nflasha5 | 62,914,560 | WBI1 → `/wbi` | 热启动镜像 | 否 |
| nflasha6 | 16,777,216 | vfat | Jiritsu 区（文件已删，数据可恢复） | 否 |
| nflasha7 | 8,388,608 | ext2 → `/rootfs` | `/sbin/init`、busybox、glibc | **是** |
| nflasha8/9/14 | 0 | — | partinf 里 size=0，真机不存在 | 否 |
| nflasha10 | 12,582,912 | vfat → `/tmp` | `/etc`、`/tmp/images` 字体（`.ltt`）、`kikilog.dat` | **是** |
| nflasha11 | 524,288,000 | vfat → `/log` | 日志 / dump 暂存区 | 否 |
| nflasha12 | 41,943,040 | vfat → `/cert` | 证书（几乎是空的） | 否 |
| nflasha13 | 4,194,304 | vfat | UpdaterWork（`/history/update.log`） | 否 |
| nflasha15 | 314,572,800 | ext2 → `/usr` | `im.elf`、Sony 库、kmod、UI 资源 | **是** |
| nflasha16 | 146,800,640 | raw | Android system | 否 |
| nflasha17 | 104,857,600 | vfat | Android data | 否 |
| nflasha18 | 20,971,520 | vfat → `/lens` | 镜头校正数据 | 否 |
| nflasha23 | 2,097,152 | prfile → `/PMBP` | 许可证文本 | 否 |

> 镜像里的槽位和真机不是逐字节相同：真机 size=0 的分区（8/9/14/19–22/24–25）
> 在镜像里给了 8 MiB 或 4 MiB 占位，`nflasha23` 的槽位也放大到 4 MiB；
> 数据仍然按真机 dump 的字节数写在槽位开头，多出来的部分是全零。
> 这样做的目的只是让 `nflashaN` 的编号连续（§7）。

### 2.2 为什么"最小集"是 2、3、7、10、15

不是按大小或重要性挑的，而是按冷启动到 UI 的**最小闭包**反推的：

- `nflasha3`：QEMU 的 `-kernel` / `-initrd` / `av-cam.bin` 都从这里取；
- `nflasha7`：initrd 切换过去的 rootfs，PID 1 和 busybox 在这里；
- `nflasha15`：`/usr`，`im.elf`、全部 Sony 库和 UI 资源；
- `nflasha2`：`/setting`，主内核参数 `kemco.txt`、设置库、service mode 的 `sen/smode` 门；
- `nflasha10`：`/tmp`，service mode 的 `/etc`、中文字体 `.ltt`，也是唯一方便写的暂存。

这五个合计 388 MiB。其余分区不在 UI 的读路径上：updater/recovery（1）、
WBI（5）、Jiritsu（6）、证书（12）、updater 工作区（13）、Android（16/17）、
镜头（18）、PMBP（23）都是按需才用；`nflasha11` 是 500 MB 的日志/暂存盘，
拼进镜像只会让镜像大一倍。`nflasha8/9/14` 在真机上根本不存在。

> 说明：`/sbin/init` 还会尝试挂载 11/12/18；缺了通常是 `mount` 报错后继续，
> 不影响已经挂好的分区。但"只有五个"的镜像没有单独做过完整验证，
> 保守做法是把 11/12/18 也一起 dump（见 §2.3）。

### 2.3 建议的 dump 组合

| 组合 | 分区 | 数据量 | 用途 |
| --- | --- | ---: | --- |
| 最小 | 2, 3, 7, 10, 15 | 388 MiB | 启动、进 UI、设置、中文字体 |
| 推荐 | 2, 3, 7, 10, 11, 12, 15, 18 | ~948 MiB | 和原厂一致，日志/证书/镜头路径齐全 |
| 研究全套 | 1–7, 10–13, 15–18, 23 | ~1.3 GiB | 还需要 updater / WBI / Jiritsu / Android / PMBP |

`nflasha4`（MPR）可以 dump 下来做离线分析，但放不进 QEMU 镜像（§7.4）。

## 3. 进入 service mode

正常固件模式只暴露 MTP/WPD，没有 shell。用 PMCA 把相机切到 service mode：

1. 相机用 USB 连电脑，确认能看到正常的 MSC/MTP 身份；
2. 用 PMCA 切 service（`SenserPlatformBackend`），相机会重新枚举成 Senser
   （`054C:02A9` / `054C:0336`），得到一个**只读读取用**的 shell；
3. 先在 shell 里确认环境（不改任何东西）：

```sh
id
uname -a
cat /proc/version
cat /proc/mounts
cat /proc/partitions
ls -l /dev/nflasha*
cat /sys/block                       # 有 /sys 的话
```

service 会话很脆，一次只做一件事；dump 完直接断电，**不要**调用 `auth.stop()`。

## 4. dump 方法 A：`/log` 中转 + PMCA 拉回

`/log` 就是 `nflasha11`（500 MB，vfat），service mode 下可写。流程是
"在相机里 dd 到 `/log` → PMCA 把文件拉回电脑 → 删除，再下一个"。
动手前先 `df /log` 看一下剩余空间。

```sh
# 相机里：一块一块地读，count 用 64 KiB 块数
dd if=/dev/nflasha15 of=/log/nflasha15.bin bs=64k count=4800
wc -c < /log/nflasha15.bin          # 必须是 314572800
```

`count = 大小 / 65536`，各分区的块数：

```
1=127   2=320   3=768   4=192   5=960   6=256   7=128
10=192  11=8000 12=640  13=64   15=4800 16=2240 17=1600 18=320  23=32
```

拉回电脑用 PMCA 的 FileControl（`cam.readFile(remote, local_file)`）：

```python
with open('nflasha15.bin', 'wb') as out:
    cam.readFile('/log/nflasha15.bin', out)
```

注意事项：

- **一次只放一个文件**：`/log` 只有 500 MB，`nflasha15` 就占了 300 MB；
  `nflasha11` 自己（500 MB）放不下，只能走 SD 卡（§5）。
- 传输可能静默损坏：拉回后核对 `stat -c %s`，并算 SHA-256；大分区建议
  dump 两遍、比较两个哈希。
- 长分区可以分块：`dd ... bs=64k skip=<块号> count=<块数>`，每次只拉 8 MiB，
  `wc -c` 核对后再 `rm`。分块不影响最终镜像，只要按顺序拼接。
- `/log` 里不要留旧文件；空间不足时 `dd` 会短写，`wc -c` 能发现。

## 5. dump 方法 B：SD 卡

适合大分区（`nflasha11`）或想少走 USB 的场景。

**卡必须是 FAT32**：这颗内核没有 exFAT 驱动；exFAT 卡在 service shell 里挂不上。
如果卡是 exFAT，先在电脑上格成 FAT32，或者在相机里用建好的节点格
（`/sbin/mkdosfs -F 32 -s 64 -n A7R3 /log/dev/mmca10p1`，**会清空整张卡**）。

A7R3 的两个卡槽（来自实测脚本）：

| 槽 | 设备 | major/minor | 挂载点示例 |
| --- | --- | --- | --- |
| 1 | `/dev/mmca10p1` | 250, 11 | `/log/sd1` |
| 2 | `/dev/mmcb30p1` | 250, 31 | `/log/sd2` |

service shell 的 `/dev` 里不一定有节点；在 `/log` 下挂一个 tmpfs 再建节点
（vfat 不支持设备节点，所以不能直接建在 `/log` 里）：

```sh
mkdir -p /log/dev /log/sd1
mount -t tmpfs tmpfs /log/dev
mknod /log/dev/mmca10p1 b 250 11
mount -t vfat /log/dev/mmca10p1 /log/sd1
cat /proc/mounts | grep sd1
```

然后按分区 dump，**写完先 `sync` 再 umount**：

```sh
for n in 15 2 3 7 10; do
  dd if=/dev/nflasha$n of=/log/sd1/nflasha$n.bin bs=64k
  wc -c < /log/sd1/nflasha$n.bin
done
sync
umount /log/sd1
```

断电、取卡、在电脑上把 `nflashaN.bin` 拷出来并核对大小 / SHA-256。

## 6. 校验

每个分区至少做三件事：

1. **大小**：必须等于 §2.1 的表（或 `/proc/partitions` 里的块数 × 1024）。
   短了就是没读完（`/log` 空间不足、USB 断开、`count` 写错）。
2. **SHA-256**：同一分区 dump 两遍，两个哈希必须一致（`nflasha15` 尤其要做）。
3. **能挂载、能读到已知文件**：

```sh
sudo mount -o loop,ro nflasha2.bin  /mnt/p2   # vfat：kemco.txt、Backup.bin
sudo mount -o loop,ro nflasha3.bin  /mnt/p3   # vfat：VMLINUX.BIN、INITRD.IMG
sudo mount -o loop,ro nflasha7.bin  /mnt/p7   # ext2：sbin/init、bin/busybox
sudo mount -o loop,ro nflasha10.bin /mnt/p10  # vfat：images/*.ltt、etc/
sudo mount -o loop,ro nflasha15.bin /mnt/p15  # ext2：bin/im.elf、lib/、kmod/
```

> 注意：`nflashaN.bin` 是**单个分区**的原始镜像，挂载时用 `offset=0`。
> 只有挂载"整个 eMMC 镜像里的某个分区"时才是 `offset=524288 + lba*512`（§7.3）。

`nflasha2` 是 vfat，文件系统可能是 FAT12/16，Linux 的 `vfat` 都能挂。
如果提示 "wrong fs type"，先确认大小对不对（截断的 dump 会挂不上）。

## 7. 拼 eMMC 镜像

镜像结构：

```
0x00000000  boot0（256 KiB）
0x00040000  boot1（256 KiB）
0x00080000  用户区：MBR + 25 个分区，共 2 GiB
```

总大小 `0x80000 + 2 GiB = 2,148,007,936` 字节。用户区的 MBR 是 guest 找分区的唯一依据，
**只 `truncate + dd` 不写 MBR，guest 一个分区都看不到**。

### 7.1 用脚本（推荐）

```sh
bash payloads/scripts/make-emmc-image.sh \
    -d /path/to/dumps -o qemu-emmc-2g.img --verify
```

脚本做这些事：

- 建 25 项 MBR（默认 A7R3 布局，大小来自 partinf；真机 size=0 的分区给占位）；
- 把有 `nflashaN.bin` 的分区稀疏写到 `0x80000 + lba*512`；
- 检查 dump 不大于分区、是 512 的倍数；短 dump 会警告；
- 校验 MBR 签名 `55aa`；`--verify` 再逐分区比对前 4 KiB；
- 打印每个分区的 LBA、大小、镜像内偏移。

输出示例（片段）：

```
  nflasha2  lba=18432    size=20971520  off=0x00980000  nflasha2.bin
  nflasha3  lba=59392    size=50331648  off=0x01d80000  nflasha3.bin
  nflasha4  lba=157696   size=1536000000 off=0x04d80000  extended container ...
  nflasha7  lba=319488   size=8388608   off=0x09c80000  nflasha7.bin
  nflasha10 lba=374784   size=12582912  off=0x0b780000  nflasha10.bin
  nflasha15 lba=1531904  size=314572800 off=0x2ec80000  nflasha15.bin
final: qemu-emmc-2g.img 2148007936 bytes
```

没有 dump 的分区会写成空占位——只要 MBR 还在，`nflashaN` 的编号就不会前移。

### 7.2 手工等价步骤

1. `truncate -s 2G user.img`；
2. 用 `sfdisk` 把下面这张表写进 `user.img`（`sfdisk user.img < table.txt`）：

```
label: dos
unit: sectors

1 : size=16256, type=0x0b
2 : size=40960, type=0x0b
3 : size=98304, type=0x0b
4 : size=3000000, type=0x05
5 : size=122880, type=0x83
6 : size=32768, type=0x83
7 : size=16384, type=0x83
8 : size=16384, type=0x83
9 : size=16384, type=0x83
10 : size=24576, type=0x0b
11 : size=1024000, type=0x0b
12 : size=81920, type=0x0b
13 : size=8192, type=0x83
14 : size=8192, type=0x83
15 : size=614400, type=0x83
16 : size=286720, type=0x83
17 : size=204800, type=0x83
18 : size=40960, type=0x0b
19 : size=8192, type=0x83
20 : size=8192, type=0x83
21 : size=8192, type=0x83
22 : size=8192, type=0x83
23 : size=8192, type=0x83
24 : size=8192, type=0x83
25 : size=8192, type=0x83
```

3. `truncate -s $((524288 + 4194304*512)) qemu-emmc-2g.img`；
4. 把 `user.img` 放到 `0x80000`：
   `dd if=user.img of=qemu-emmc-2g.img bs=1M oflag=seek_bytes seek=524288 conv=sparse,notrunc`；
5. 按 `sfdisk -d user.img` 打印的起始 LBA 写数据：

```sh
dd if=nflasha2.bin  of=qemu-emmc-2g.img bs=1M oflag=seek_bytes \
   seek=$((524288 + 18432*512))   conv=sparse,notrunc
```

镜像内偏移一律是 **`0x80000 + lba*512`**（`524288 + lba*512`）。以 `nflasha15` 为例：
`524288 + 1531904*512 = 0x2EC80000`。

### 7.3 检查镜像里的分区

```sh
# 打印 MBR（需要 root；普通用户看不了 loop）
sudo losetup -f --show -o 524288 qemu-emmc-2g.img
sudo sfdisk -d /dev/loopX

# 挂载镜像里的单个分区：offset = 524288 + lba*512，sizelimit = 分区大小
sudo mount -o loop,ro,offset=$((524288 + 1531904*512)),sizelimit=314572800 \
     qemu-emmc-2g.img /mnt/usr
ls /mnt/usr/bin/im.elf
```

仓库里的 `payloads/scripts/img-part-table.sh` 也可以打印已有镜像的表（里面写死了
镜像路径，按需改）。如果挂载失败，先检查 `sfdisk -d` 里的 LBA 和 `dd` 的目标偏移
是不是一致。

### 7.4 为什么 `nflasha4` 放不进去

镜像里 p4 被用作**扩展分区容器**（p5–p25 是逻辑分区），所以 `nflasha4` 这个槽位
被容器占用，没有地方放 12 MiB 的 MPR 备份。QEMU 启动只需要这个设备节点存在且可写
（`im.elf` 的 DFB 会写它），不需要里面的寄存器数据。`make-emmc-image.sh` 会忽略
`nflasha4.bin` 并打印提示；dump 下来的那份留着做离线分析即可。

## 8. 取内核 / initrd / av-cam

`nflasha3` 是 vfat，直接挂：

```sh
sudo mount -o loop,ro nflasha3.bin /mnt/p3
cp /mnt/p3/VMLINUX.BIN  vmlinux.bin
cp /mnt/p3/INITRD.IMG   initrd.img
cp /mnt/p3/AV-CAM.BIN   av-cam.bin
```

A7R3 3.10 的参考大小：`VMLINUX.BIN` 3,991,552 B（内嵌 `3.0.27_nl-rt106+`）、
`INITRD.IMG` 3,958,784 B（ext2）。大小对不上说明 dump 不对或版本不同。

**原样文件还不能直接进 UI**：本仓库的 QEMU 用的是打过适配补丁的
`vmlinux-console.bin` / `initrd-qemu.img`（内核若干处 NOP / ATAGS / 控制台，
initrd 里三处等长替换：`Backup_load=1`、`udate.zz`、`grm_gles.zz`），
`/usr` 里的 `grm_gles` / `liro` / `libIMDB` / `libmpr` 也有实验补丁。
这些是 QEMU 适配，不属于 dump；细节见 [A7R3_NOTES.md](A7R3_NOTES.md) 与
[SUMMARY.md](SUMMARY.md)。dump 的职责是把原厂字节完整取出来。

## 9. 不同机型

| 机型 | 与 A7R3 的关系 | 分区表 |
| --- | --- | --- |
| A7R3（ILCE-7RM3） | 基准 | 25 项，见 §2.1 |
| A7M3（ILCE-7M3） | 同代（CXD90045） | 编号 / 大小 / 挂载点基本一致，差别在内核、`av-cam.bin`、`/usr` |
| ZV-E10 | 同代但不同产品线 | `nflasha6` 是 25 MB 的 `/tmp/jiritsu`，`nflasha8/9/14` 是真实存在的分区 |
| A7C2 | 新一代（BIONZ XR） | 官方包直接给 `nflasha3/8/15`；`/tmp/jiritsu` 仍是 `nflasha6`；`nflasha1/2` 在实验里是合成的身份卷 |
| A7R3A（ILCE-7RM3A） | A7R3 换 236 万点 LCD、USB 3.2、去 logo | 预期同 A7R3；**以实测为准** |

**A7R3A**：面板和 A7R3 不同（1.44M → 2.36M 点），显示/面板初始化相关的部分
不要照搬；分区表本身大概率一致。A7R3A 没有对应型号的官方升级包（有用户报告它
报的版本是 1.00，普遍认为等价于 R3 的 3.10），所以直接 dump 是更干净的路。

**拿到自己机型的表**（在 service shell 里，只读）：

```sh
cat /proc/mounts                          # 谁挂在哪
cat /proc/partitions                      # 分区和块数
cat /sys/block/mmcblk0/mmcblk0p12/start   # 起始 LBA（如果有 /sys）
cat /sys/block/mmcblk0/mmcblk0p12/size    # 扇区数
dmesg | grep -i -E 'mmc|nflasha'
```

把结果写成 `--layout` 文件（每行：编号 起始LBA 扇区数 [类型]），再拼镜像：

```sh
bash payloads/scripts/make-emmc-image.sh -d dumps -o qemu-emmc-2g.img \
     --layout my-layout.txt --verify
```

> 自定义 layout 时，p4 如果是扩展分区（类型 `0x05`），p5 起的起始 LBA 要留出
> EBR 的位置；不确定就把真机的 `sfdisk -d` 结果直接抄过来。
>
> **不要删中间的分区**：guest 是按编号找 `nflashaN` 的，layout 里少一项，
> 后面的编号就会整体前移。真机上 size=0 的槽位要么保留占位大小，
> 要么按 §7 的默认表处理。

## 10. FAQ

**为什么最小集只有五个分区？**
因为它们是"内核 → rootfs → /usr → 设置 → 临时/字体"的最小闭包（§2.2），
不是"只有这五个有用"。

**缺了 11/12/18 会怎样？**
`/sbin/init` 会尝试挂载它们，挂不上就是一条 `mount` 报错，已经挂好的分区不受影响。
但这样组合没有单独做完整验证；要稳就把它们也 dump（§2.3）。

**为什么镜像里 `nflasha8/9/14` 有 8 MiB，真机上却是 0？**
镜像的 MBR 需要保持编号连续，脚本给真机 size=0 的槽位放了 8 MiB 占位。
guest 会看到这些节点，但里面是空的，mount 会失败——和真机上节点不存在等价。

**为什么 `dd` 的偏移要加 0x80000？**
镜像前 512 KiB 是 boot0 + boot1；`nflashaN` 在用户区里，MBR 也在用户区。
`seek` 是相对文件开头的，所以 `0x80000 + lba*512`。旧版文档里的
`seek=18432` 少了这个前缀，照做会整体错位。

**为什么挂载单个 `nflashaN.bin` 用 `offset=0`，挂镜像里的分区却要加偏移？**
前者是分区自身的原始镜像（文件第 0 字节就是分区第 0 字节）；后者是整个 eMMC
镜像，分区数据在镜像的某个偏移上。

**SD 卡为什么必须 FAT32？**
这颗内核没有 exFAT 驱动（`mount -t vfat` 对 exFAT 卡会失败）。FAT32 单文件上限
4 GiB，而相机 eMMC 上最大的分区是 500 MB 的 `/log`，放得下。

**dump 出来能刷回相机吗？**
不要。更新包和引导链有签名校验，本项目不做重打包/绕过；写 flash 有变砖风险。
dump 只用于离线分析和 QEMU。

**镜像能直接用官方的 2 GiB eMMC 全盘镜像吗？**
可以，但官方不提供；自己 dump 全盘要处理 boot0/boot1 和 2 GiB 用户区，本文
只讲分区级 dump —— 对 QEMU 来说已经够了。

## 11. 不要做的事

- 不要写 flash：只用 `dd if=…`，不要 `dd of=/dev/nflasha*`；
- 不要在 boot0 上做任何事；
- 不要在 service 会话里调用 `auth.stop()`；dump 完直接断电；
- 不要把 dump、镜像、字体、`.uxc` 提交到仓库或再分发；
- 不要尝试重打包固件走官方升级（签名会失败，且有变砖风险）；
- 不要用副厂电池或中途拔线做长 dump。
