# qemu/bionz.c —— cxd90045 机器模型

这是为本项目写的 QEMU 设备/机器模型，用于运行 **CXD90045**（A7R3 / ZV-E10 这一代
BIONZ XR 之前的平台）的固件。它包含：

- `cxd90045` 机器：eMMC（MTD 布局）、DDR、UART、DDRC、SIO/GPIO/HWTIMER 等；
- **Sugilite 2D 图形引擎**：解析 `(op<<16)|0xf000` 命令表，
  支持 0x0112 目标面、0x0589 纹理、0x0047/0x0048 矩形、0x0049 面积、
  0x0105 填充色、0x0202/0x00000281 绘制等，并做 RGBA4444 + Morton 瓦片写入；
- **blit/present**：`0x0c00..0x0c18` 寄存器（src/dst/w/h/fmt/kick）；
- 若干调试开关（环境变量）：`A7R3_SUGI_EXEC`、`A7R3_SURF_MORTON`、
  `A7R3_SURF_DIR`、`A7R3_SURF_TICK`、`A7R3_CMD_DEC`、`A7R3_BLIT_LOG`、
  `A7R3_DUMP_PA`/`A7R3_DUMP_PA2`、`A7R3_OVERLAY` 等。

## 集成到 QEMU

QEMU 4.x（本项目用 4.2 系列）树内：

1. 把 `bionz.c` 放到 `hw/arm/bionz.c`；
2. `hw/arm/Makefile.objs` 加 `obj-$(CONFIG_CXD90045) += bionz.o`；
3. `default-configs/arm-softmmu.mak` 加 `CONFIG_CXD90045=y`；
4. 重新 `./configure --target-list=arm-softmmu` 并 `make -j`。

启动示例（路径换成你自己的固件）：

```bash
qemu-system-arm -M cxd90045 -smp 2 \
  -kernel vmlinux-console.bin -initrd initrd-qemu.img \
  -drive if=mtd,file=emmc.img,format=raw \
  -device loader,file=av-cam.bin,addr=0x035c6000,force-raw=on \
  -serial unix:/tmp/ser.sock,server=on,wait=off -display none
```

## 许可

本文件是 QEMU 的机器模型，运行时链接 QEMU，故按 **GPL-2.0-or-later** 分发
（见仓库根目录 `LICENSE` 的例外条款）。
