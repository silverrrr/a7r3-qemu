/* a7r3-qemu: QEMU machine model for the Sony CXD90045 platform.
 *
 * The Sugilite 2D software rasterizer in this file (soft_draw / soft_exec and
 * the opcode decoding, see cxd90045_sugi_*) was ported from
 *     A7M3-Firmware-QEMU-Boot-Experiment
 *     https://github.com/Derek2011cc/A7M3-Firmware-QEMU-Boot-Experiment
 *     License: MIT
 * with adaptations for the A7R3 (CXD90045) command lists, textures, Morton
 * tiling and the blit/present registers.  The rest is original work.
 *
 * This file runs as part of QEMU and is therefore distributed under
 * GPL-2.0-or-later (see ../LICENSE).
 */

/* QEMU model of Sony BIONZ image processors */

#include "qemu/osdep.h"
#include "qemu/timer.h"

/* SUGITIME: coarse host-side accounting of the software GPU model. */
#include <time.h>
static uint64_t g_sugi_exec_ns, g_sugi_blit_ns, g_sugi_dump_ns;
static unsigned g_sugi_exec_n;
static inline uint64_t sugi_now_ns(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}
static void sugi_time_report(void)
{
    fprintf(stderr, "SUGITIME exec_ms=%llu blit_ms=%llu dump_ms=%llu n=%u\n",
            (unsigned long long)(g_sugi_exec_ns / 1000000ULL),
            (unsigned long long)(g_sugi_blit_ns / 1000000ULL),
            (unsigned long long)(g_sugi_dump_ns / 1000000ULL), g_sugi_exec_n);
}

#include "cpu.h"
#include "exec/address-spaces.h"
#include "hw/sysbus.h"
#include "hw/block/flash.h"
#include "hw/boards.h"
#include "hw/char/pl011.h"
#include "hw/cpu/a9mpcore.h"
#include "hw/cpu/arm11mpcore.h"
#include "hw/hw.h"
#include "hw/irq.h"
#include "hw/loader.h"
#include "hw/sd/sdhci.h"
#include "qapi/error.h"
#include "sysemu/block-backend.h"
#include "sysemu/sysemu.h"
#include "target/arm/arm-tcm.h"

//////////////////////////// CXD4108 ////////////////////////////
#define CXD4108_NAND_BASE 0x00000000
#define CXD4108_DDR_BASE 0x20000000
#define CXD4108_DDR_SIZE 0x04000000
#define CXD4108_SDHCI_BASE 0x50000000
#define CXD4108_USB_BASE 0x70200000
#define CXD4108_DMA_BASE(i) (0x70500000 - (i) * 0x100000)
#define CXD4108_NUM_DMA 2
#define CXD4108_DMA_NUM_CHANNEL 8
#define CXD4108_PL320_BASE 0x70800000
#define CXD4108_UART_BASE(i) (0x74200000 + (i) * 0x100000)
#define CXD4108_NUM_UART 3
#define CXD4108_HWTIMER_BASE(i) (0x76000000 + (i) * 0x10000)
#define CXD4108_NUM_HWTIMER 8
#define CXD4108_SIO_BASE(i) (0x76100000 + (i) * 0x100000)
#define CXD4108_NUM_SIO 4
#define CXD4108_INTC_BASE(i) (0x76500000 + (i) * 0x100000)
#define CXD4108_SYSV_BASE 0x76700000
#define CXD4108_GPIO_BASE(i) (0x76710000 + (i) * 0x10000)
#define CXD4108_NUM_GPIO 6
#define CXD4108_GPIOEASY_BASE 0x76780000
#define CXD4108_GPIOSYS_BASE 0x76790000
#define CXD4108_MISCCTRL_BASE 0x767b0000
#define CXD4108_ADC_BASE(i) (0x76b00000 + (i + 1) * 0x100000)
#define CXD4108_NUM_ADC 2
#define CXD4108_CLKBLK_BASE 0x77400000
#define CXD4108_CAM_SYNC_BASE 0x78100000
#define CXD4108_SDC_BASE 0x78200000
#define CXD4108_JPEG_BASE 0x78c00000
#define CXD4108_RC_BASE(i) (0x79200000 + (i) * 0x100000)
#define CXD4108_NUM_RC 2
#define CXD4108_CPYFB_BASE 0x79700000
#define CXD4108_VIP_BASE 0x79800000
#define CXD4108_AUDIO_BASE 0x79900000
#define CXD4108_DDR_ALIAS_BASE 0xa0000000
#define CXD4108_BOOTROM_BASE 0xffff0000
#define CXD4108_BOOTROM_SIZE 0x00002000
#define CXD4108_SRAM_BASE 0xffff2000
#define CXD4108_SRAM_SIZE 0x00002000

#define CXD4108_IRQ_CH_UART 0
#define CXD4108_IRQ_CH_TIMER 2
#define CXD4108_IRQ_CH_DMA 3
#define CXD4108_IRQ_CH_SIO 7
#define CXD4108_IRQ_CH_SDHCI 8
#define CXD4108_IRQ_CH_USB 13
#define CXD4108_IRQ_CH_ADC 17
#define CXD4108_IRQ_CH_GPIO 19
#define CXD4108_IRQ_CH_PL320 21
#define CXD4108_IRQ_CH_VIDEO 23
#define CXD4108_IRQ_CH_IMGMC 27
#define CXD4108_IRQ_CH_IMGV 29
#define CXD4108_IRQ_CH_SYSV 30
#define CXD4108_IRQ_CH_AUDIO 31
#define CXD4108_IRQ_GPIO_NAND 15

#define CXD4108_TEXT_OFFSET 0x00408000
#define CXD4108_INITRD_OFFSET 0x0062e000

//////////////////////////// CXD4115 ////////////////////////////
#define CXD4115_NAND_BASE 0x00000000
#define CXD4115_DDR_BASE 0x10000000
#define CXD4115_DDR_SIZE 0x10000000
#define CXD4115_DMA_BASE 0x78008000
#define CXD4115_DMA_NUM_CHANNEL 8
#define CXD4115_USB_BASE 0x78020000
#define CXD4115_LDEC_BASE 0x78090000
#define CXD4115_ONA_BASE 0x78098000
#define CXD4115_HWTIMER_BASE(i) (0x7a000000 + (i) * 0x20)
#define CXD4115_NUM_HWTIMER 3
#define CXD4115_SIO_BASE(i) (0x7a008000 + (i) * 0x200)
#define CXD4115_NUM_SIO 5
#define CXD4115_UART_BASE(i) (0x7a050000 + (i) * 0x1000)
#define CXD4115_NUM_UART 3
#define CXD4115_GPIO_BASE(i) (0x7a400000 + (i) * 0x100)
#define CXD4115_NUM_GPIO 8
#define CXD4115_BOOTCON_BASE 0x7f000000
#define CXD4115_SRAM_BASE 0xfff00000
#define CXD4115_SRAM_SIZE 0x00008000
#define CXD4115_MPCORE_BASE 0xfffd0000
#define CXD4115_BOOTROM_BASE 0xffff0000
#define CXD4115_BOOTROM_SIZE 0x00002000

#define CXD4115_NUM_IRQ 256
#define CXD4115_IRQ_OFFSET 32
#define CXD4115_IRQ_GPIO_RISE(i) (32 + (i))
#define CXD4115_IRQ_GPIO_FALL(i) (112 + (i))
#define CXD4115_IRQ_UART(i) (152 + (i))
#define CXD4115_IRQ_HWTIMER(i) (155 + (i))
#define CXD4115_IRQ_DMA(i) (168 + (i))
#define CXD4115_IRQ_SIO(i) (188 + (i))
#define CXD4115_IRQ_USB0 233
#define CXD4115_IRQ_USB1 234
#define CXD4115_IRQ_GPIO_NAND 22

#define CXD4115_TYPEID_OFFSET 0x00007d24
#define CXD4115_TEXT_OFFSET 0x00208000
#define CXD4115_INITRD_OFFSET 0x0062e000

//////////////////////////// CXD4132 ////////////////////////////
#define CXD4132_NAND_BASE 0x00000000
#define CXD4132_DDR_BASE 0x80000000
#define CXD4132_DDR_SIZE 0x20000000
#define CXD4132_SRAM_BASE 0xa0000000
#define CXD4132_SRAM_SIZE 0x00400000
#define CXD4132_USB_BASE 0xf0040000
#define CXD4132_BOOTCON_BASE 0xf0100000
#define CXD4132_DMA_BASE 0xf2001000
#define CXD4132_DMA_NUM_CHANNEL 4
#define CXD4132_MENO_BASE 0xf2002000
#define CXD4132_HWTIMER_BASE(i) (0xf2008000 + (i) * 0x20)
#define CXD4132_NUM_HWTIMER 5
#define CXD4132_SIO_BASE(i) (0xf2010000 + (i) * 0x200)
#define CXD4132_NUM_SIO 5
#define CXD4132_UART_BASE(i) (0xf2038000 + (i) * 0x1000)
#define CXD4132_NUM_UART 3
#define CXD4132_GPIO_BASE(i) (0xf3000000 + (i) * 0x100)
#define CXD4132_NUM_GPIO 16
#define CXD4132_MISCCTRL_BASE(i) (0xf3060000 + (i) * 0x10)
#define CXD4132_MPCORE_BASE 0xf8000000
#define CXD4132_BOOTROM_BASE 0xffff0000
#define CXD4132_BOOTROM_SIZE 0x00006000

#define CXD4132_NUM_IRQ 256
#define CXD4132_IRQ_OFFSET 32
#define CXD4132_IRQ_UART(i) (160 + (i))
#define CXD4132_IRQ_HWTIMER(i) (163 + (i))
#define CXD4132_IRQ_DMA(i) (176 + (i))
#define CXD4132_IRQ_MENO 180
#define CXD4132_IRQ_NAND 183
#define CXD4132_IRQ_SIO(i) (196 + (i))
#define CXD4132_IRQ_USB 222

#define CXD4132_CMDLINE_OFFSET 0x00013000
#define CXD4132_TEXT_OFFSET 0x00018000
#define CXD4132_INITRD_OFFSET 0x00408000

#define CXD4132_CMDLINE \
    "lpj=622592 " \
    "console=ttyAM0,115200n8 " \
    "amba2.console=1 " \
    "ip=off " \
    "initrd=0x80408000,0x00700000 " \
    "root=/dev/ram0 " \
    "boottime=0x20000@0x833C0000 " \
    "klog.size=0x20000 " \
    "klog.addr=0x833E0080 " \
    "mem=64M@0x80000000@0 " \
    "memrsv=32K@0x80000000 " \
    "memrsv=0x1270000@0x82D90000 "

//////////////////////////// CXD90014 ////////////////////////////
#define CXD90014_BOSS_SRAM_BASE 0x00000000
#define CXD90014_BOSS_IO_BASE 0x00011000
#define CXD90014_NAND_REG_BASE 0x00020000
#define CXD90014_NAND_DATA_BASE 0x10000000
#define CXD90014_DDR_BASE 0x80000000
#define CXD90014_DDR_SIZE 0x40000000
#define CXD90014_SRAM_BASE 0xc0000000
#define CXD90014_SRAM_SIZE 0x01000000
#define CXD90014_BOOTCON_BASE 0xc0005030
#define CXD90014_DDMC_BASE 0xf0104000
#define CXD90014_USB_HDMAC_BASE 0xf0204000
#define CXD90014_USB_BASE 0xf0210000
#define CXD90014_UART_BASE(i) (0xf2000000 + (i) * 0x1000)
#define CXD90014_NUM_UART 3
#define CXD90014_HWTIMER_BASE(i) (0xf2403000 + (i) * 0x100)
#define CXD90014_NUM_HWTIMER 4
#define CXD90014_SIO_BASE(i) (0xf2405000 + (i) * 0x200)
#define CXD90014_NUM_SIO 5
#define CXD90014_BOSS_CLKRST_BASE 0xf29000d0
#define CXD90014_GPIO_BASE(i) (0xf2910000 + (i) * 0x100)
#define CXD90014_NUM_GPIO 18
#define CXD90014_MISCCTRL_BASE(i) (0xf2915000 + (i) * 0x10)
#define CXD90014_USB_OTG_BASE 0xf2920000
#define CXD90014_MPCORE_BASE 0xf8000000
#define CXD90014_BOOTROM_BASE 0xffff0000
#define CXD90014_BOOTROM_SIZE 0x00006000

#define CXD90014_NUM_IRQ 256
#define CXD90014_IRQ_OFFSET 32
#define CXD90014_IRQ_UART(i) (150 + (i))
#define CXD90014_IRQ_HWTIMER(i) (153 + (i))
#define CXD90014_IRQ_BOSS 170
#define CXD90014_IRQ_SIO(i) (201 + (i))
#define CXD90014_IRQ_USB 227

#define CXD90014_BOOT_BLOCK_OFFSET 0x00000000
#define CXD90014_BOOTROM_BLOCK_OFFSET 0x00000600
#define CXD90014_TEXT_OFFSET 0x00038000
#define CXD90014_INITRD_OFFSET 0x00628000

//////////////////////////// CXD90045 ////////////////////////////
/* A7M3 (CXD90045): the kernel is linked with PAGE_OFFSET 0x60000000 and
 * PHYS_OFFSET 0, so it is loaded at physical DDR0_BASE + TEXT_OFFSET and
 * its link addresses (0x60108000+) are virtual. */
#define CXD90045_DDR0_BASE 0x00000000
/* Local experiment: a simplified memory model.  The 4.03 updater/recovery
 * boot parameters (nflasha1/BOOT/KEMCO.TXT) reserve only 54 MiB at 0x20000000
 * with the "uc" attribute and add a second bank at 0x80000000.  This
 * machine deliberately reserves the whole 512 MiB "device" window instead;
 * that is enough to boot and to make Sony's arch_phys_to_noncache() work
 * for register addresses like 0x2002104c. */
#define CXD90045_DDR0_SIZE 0x40000000
#define CXD90045_DDR1_BASE 0x80000000
#define CXD90045_DDR1_SIZE 0x10000000
/* LIRO co-processor register window (av-cam.bin polls these after
 * entry_main).  Writes are stored; status registers can return all-ones so
 * the firmware leaves its wait loops. */
#define CXD90045_LIRO_BASE 0xf2a00000
#define CXD90045_LIRO_SIZE 0x00023000
#define CXD90045_LIRO_NUM_REGS (CXD90045_LIRO_SIZE / 4)
/* Sugilite graphics engine BAR, from grm_gles.ko: ioremap 0xf1000000,
 * size 0x4000; request_threaded_irq(76, ...).  Kicks complete immediately. */
#define CXD90045_SUGI_BASE 0xf1000000
#define CXD90045_SUGI_SIZE 0x00004000
#define CXD90045_SUGI_NUM_REGS (CXD90045_SUGI_SIZE / 4)
#define CXD90045_SUGI_NUM_BLOCKS (CXD90045_SUGI_SIZE / 0x1000)
#define CXD90045_IRQ_SUGI 76


#define CXD90045_DDRC_BASE(i) (0xf0104000 + (i) * 0x1000)
#define CXD90045_SRAM_BASE 0xfe000000
#define CXD90045_SRAM_SIZE 0x01000000
#define CXD90045_BOOTCON_BASE 0xfe005030
#define CXD90045_SDHCI_BASE 0xf0304000
#define CXD90045_UART_BASE(i) (0xf2000000 + (i) * 0x1000)
#define CXD90045_NUM_UART 4
#define CXD90045_HWTIMER_BASE(i) (0xf2403000 + (i) * 0x100)
#define CXD90045_NUM_HWTIMER 4
#define CXD90045_SIO_BASE(i) (0xf2405000 + (i) * 0x200)
#define CXD90045_NUM_SIO 5
#define CXD90045_GPIO_BASE(i) (0xf2910000 + (i) * 0x100)
#define CXD90045_NUM_GPIO 18
#define CXD90045_MISCCTRL_BASE(i) (0xf2915000 + (i) * 0x10)
#define CXD90045_MPCORE_BASE 0xf8000000
#define CXD90045_BOOTROM_BASE 0xffff0000
#define CXD90045_BOOTROM_SIZE 0x00006000

#define CXD90045_NUM_IRQ 256
#define CXD90045_IRQ_OFFSET 32
#define CXD90045_IRQ_UART(i) ((i) == 3 ? 120 : (150 + (i)))
#define CXD90045_IRQ_HWTIMER(i) (153 + (i))
#define CXD90045_IRQ_SIO(i) (201 + (i))
#define CXD90045_IRQ_SDHCI 227

#define CXD90045_TEXT_OFFSET 0x00108000
#define CXD90045_INITRD_OFFSET 0x00700000
#define CXD90045_ATAGS_ADDR (CXD90045_DDR0_BASE + 0x00010000)
#define CXD90045_CMDLINE "console=ttyAM0,115200 root=/dev/ram0 rw ignore_loglevel earlyprintk initcall_debug ip=off log_buf_len=4M memrsv=1M@0 memrsv=0x8000@0xF00000 memrsv=0x1A5E000@0x02E68000 memrsv=0xEEEB000@0x09915000 memrsv=632M@0x18800000:uc memrsv=14M@0x80000000:uc memrsv=242M@0x80e00000:uc klog.addr=0x1F000000 klog.size=0x00080000 alog.addr=0x1F080000 alog.size=0x00080000"

#define NAND_SECTOR_SIZE 0x200
#define NAND_PAGE_SIZE 0x1000

static hwaddr cxd_init_loader2(BlockBackend *drive)
{
    char boot_block[NAND_SECTOR_SIZE];
    uint32_t loader_offset, loader_size, loader_base;
    void *loader_buffer;

    if (blk_pread(drive, 0, boot_block, sizeof(boot_block)) < 0) {
        hw_error("%s: Cannot read boot block\n", __func__);
    }

    if (*(uint32_t *) boot_block != *(uint32_t *) "EXBL") {
        hw_error("%s: Wrong boot block signature\n", __func__);
    }

    loader_offset = (*(uint32_t *) (boot_block + 0x40)) * NAND_SECTOR_SIZE;
    loader_size = (*(uint32_t *) (boot_block + 0x44)) * NAND_SECTOR_SIZE;
    loader_base = *(uint32_t *) (boot_block + 0x50);

    loader_buffer = g_malloc(loader_size);
    if (blk_pread(drive, loader_offset, loader_buffer, loader_size) < 0) {
        hw_error("%s: Cannot read loader2\n", __func__);
    }
    rom_add_blob_fixed("loader2", loader_buffer, loader_size, loader_base);
    g_free(loader_buffer);

    return loader_base;
}

static hwaddr cxd90014_init_loader2(BlockBackend *drive)
{
    char page[0x600];
    uint32_t pages_per_block, block_offset, loader_offset, loader_n_pages, loader_base;
    int i;

    for (i = 0; i < 3; i++) {
        if (blk_pread(drive, i * NAND_PAGE_SIZE, page, sizeof(page)) < 0) {
            hw_error("%s: Cannot read bootrom block\n", __func__);
        }
        rom_add_blob_fixed("bootrom_block", page, sizeof(page), CXD90014_SRAM_BASE + CXD90014_BOOTROM_BLOCK_OFFSET + i * sizeof(page));
    }

    pages_per_block = 1 << (*(uint32_t *) (page + 0x08));
    block_offset = (*(uint32_t *) (page + 0x0c)) * pages_per_block * NAND_PAGE_SIZE;

    if (blk_pread(drive, block_offset, page, sizeof(page)) < 0) {
        hw_error("%s: Cannot read boot block\n", __func__);
    }

    if (*(uint32_t *) page != *(uint32_t *) "EXBL") {
        hw_error("%s: Wrong boot block signature\n", __func__);
    }

    rom_add_blob_fixed("boot_block", page, sizeof(page), CXD90014_SRAM_BASE + CXD90014_BOOT_BLOCK_OFFSET);

    loader_offset = block_offset + (*(uint32_t *) (page + 0x40)) * NAND_PAGE_SIZE;
    loader_n_pages = (*(uint32_t *) (page + 0x44));
    loader_base = *(uint32_t *) (page + 0x50);

    for (i = 0; i < loader_n_pages; i++) {
        if (blk_pread(drive, loader_offset + i * NAND_PAGE_SIZE, page, sizeof(page)) < 0) {
            hw_error("%s: Cannot read loader2\n", __func__);
        }
        rom_add_blob_fixed("loader2", page, sizeof(page), loader_base + i * sizeof(page));
    }

    return loader_base;
}

static hwaddr cxd90045_init_loader2(BlockBackend *drive)
{
    char boot_block[0x800];
    uint32_t boot_base, boot_size, loader_base;
    void *boot_buffer;

    if (blk_pread(drive, 0, boot_block, sizeof(boot_block)) < 0) {
        hw_error("%s: Cannot read boot block\n", __func__);
    }

    if (*(uint32_t *) boot_block != *(uint32_t *) "EXBL") {
        hw_error("%s: Wrong boot block signature\n", __func__);
    }

    boot_base = (*(uint32_t *) (boot_block + 0x6c));
    boot_size = (*(uint32_t *) (boot_block + 0x7c));
    loader_base = *(uint32_t *) (boot_block + 0x78);

    boot_buffer = g_malloc(boot_size);
    if (blk_pread(drive, 0, boot_buffer, boot_size) < 0) {
        hw_error("%s: Cannot read boot partition\n", __func__);
    }
    rom_add_blob_fixed("boot", boot_buffer, boot_size, boot_base);
    g_free(boot_buffer);

    return loader_base;
}

static void cxd_init_cmdline(const char *default_cmdline, const char *cmdline, hwaddr base)
{
    const char *header = "kemco ";
    const char *spacer = " ";
    const char *footer = " *";
    const size_t len = strlen(header) + strlen(default_cmdline) + strlen(spacer) + strlen(cmdline) + strlen(footer);
    char *buf = g_malloc(len + 1);
    strcpy(buf, header);
    strcat(buf, default_cmdline);
    strcat(buf, spacer);
    strcat(buf, cmdline);
    strcat(buf, footer);
    rom_add_blob_fixed("cmdline", buf, len, base);
    g_free(buf);
}

static void cxd_add_const_reg(const char *name, hwaddr base, uint32_t value)
{
    MemoryRegion *mem = g_new(MemoryRegion, 1);
    uint32_t *buffer = g_new(uint32_t, 1);
    *buffer = value;
    memory_region_init_ram_ptr(mem, NULL, name, sizeof(uint32_t), buffer);
    memory_region_set_readonly(mem, true);
    memory_region_add_subregion(get_system_memory(), base, mem);
}

static void cxd_write_bootloader(hwaddr base, hwaddr target)
{
    int i;
    uint32_t loader[] = {
        0xe51ff004, // ldr pc, [pc, #-4]
        target,
    };
    for (i = 0; i < ARRAY_SIZE(loader); i++) {
        loader[i] = tswap32(loader[i]);
    }
    rom_add_blob_fixed("bootloader", loader, sizeof(loader), base);
}

/* Local experiment: minimal ATAGS for the ARM32 Linux boot protocol. */
static void cxd90045_write_atags(uint32_t addr, uint32_t initrd_start,
                                 uint32_t initrd_size, const char *cmdline)
{
    uint32_t buffer[256];
    uint32_t *p = buffer;
    int len = strlen(cmdline) + 1;
    int words = (len + 3) / 4;

    /* ATAG layout: size first, then tag (struct tag_header). */
    *p++ = 5;         /* ATAG_CORE */
    *p++ = 0x54410001;
    *p++ = 0;
    *p++ = 4096;
    *p++ = 0;

    /* struct tag_mem32 { __u32 size; __u32 start; } */
    *p++ = 4;         /* ATAG_MEM: DDR0 at 0x60000000, 1 GiB */
    *p++ = 0x54410002;
    *p++ = CXD90045_DDR0_SIZE;
    *p++ = CXD90045_DDR0_BASE;

    *p++ = 4;         /* ATAG_MEM: DDR1, 256 MiB at 0x80000000 */
    *p++ = 0x54410002;
    *p++ = CXD90045_DDR1_SIZE;
    *p++ = CXD90045_DDR1_BASE;

    *p++ = 2 + words; /* ATAG_CMDLINE */
    *p++ = 0x54410009;
    memset(p, 0, words * 4);
    memcpy(p, cmdline, len);
    p += words;

    *p++ = 4;         /* ATAG_INITRD2 (0x54420005; 0x54410005 is deprecated ATAG_INITRD) */
    *p++ = 0x54420005;
    *p++ = initrd_start;
    *p++ = initrd_size;

    *p++ = 0;         /* ATAG_NONE */
    *p++ = 0;

    rom_add_blob_fixed("atags", buffer, (p - buffer) * 4, addr);
}

/* Local experiment: set r0/r1/r2 for the ARM32 Linux boot protocol. */
static void cxd_write_bootloader_with_machine(hwaddr base, hwaddr target,
                                              uint32_t machine, uint32_t atags)
{
    int i;
    uint32_t loader[] = {
        0xe3a00000, /* mov r0, #0 */
        0xe59f200c, /* ldr r2, [pc, #12] */
        0xe59f1004, /* ldr r1, [pc, #4] */
        0xe51ff004, /* ldr pc, [pc, #-4] */
        target,
        machine,
        atags,
    };
    for (i = 0; i < ARRAY_SIZE(loader); i++) {
        loader[i] = tswap32(loader[i]);
    }
    rom_add_blob_fixed("bootloader", loader, sizeof(loader), base);
}

static void cxd4108_init(MachineState *machine)
{
    DriveInfo *dinfo;
    BlockBackend *drive;
    MemoryRegion *mem, *ddr, *container;
    DeviceState *dev;
    Object *cpu;
    BusState *bus;
    qemu_irq irq[32][16];
    qemu_irq gpio_irq[16];
    qemu_irq vsync;
    int i, j, k;

    dinfo = drive_get(IF_MTD, 0, 0);
    drive = dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;

    for (i = 0; i < machine->smp.cpus; i++) {
        cpu = object_new(machine->cpu_type);
        object_property_set_bool(cpu, "reset-hivecs", true, &error_fatal);
        if (i != 0) {
            object_property_set_bool(cpu, "start-powered-off", true, &error_fatal);

            container = g_new(MemoryRegion, 1);
            memory_region_init(container, NULL, "container", UINT64_MAX);
            mem = g_new(MemoryRegion, 1);
            memory_region_init_alias(mem, NULL, "sysmem", get_system_memory(), 0, UINT64_MAX);
            memory_region_add_subregion(container, 0, mem);
            object_property_set_link(cpu, "memory", OBJECT(container), &error_fatal);
            ARM_CPU(cpu)->tcmtr = 0x10001;
            arm_tcm_init(ARM_CPU(cpu), g_new(arm_tcm_mem, 1));
        }
        qdev_realize(DEVICE(cpu), NULL, &error_fatal);

        dev = qdev_new("bionz_intc");
        qdev_prop_set_uint32(dev, "len-enabled-channels", 1);
        qdev_prop_set_uint8(dev, "enabled-channels[0]", CXD4108_IRQ_CH_PL320);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4108_INTC_BASE(i));
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ));
        for (j = 0; j < 32; j++) {
            for (k = 0; k < 16; k++) {
                if (i == 0) {
                    irq[j][k] = qdev_get_gpio_in(dev, j * 16 + k);
                } else {
                    irq[j][k] = qemu_irq_split(irq[j][k], qdev_get_gpio_in(dev, j * 16 + k));
                }
            }
        }
    }

    ddr = g_new(MemoryRegion, 1);
    memory_region_init_ram(ddr, NULL, "ddr", CXD4108_DDR_SIZE, &error_fatal);
    memory_region_add_subregion(get_system_memory(), CXD4108_DDR_BASE, ddr);

    mem = g_new(MemoryRegion, 1);
    memory_region_init_alias(mem, NULL, "ddr.alias", ddr, 0, CXD4108_DDR_SIZE);
    memory_region_add_subregion(get_system_memory(), CXD4108_DDR_ALIAS_BASE, mem);

    mem = g_new(MemoryRegion, 1);
    memory_region_init_ram(mem, NULL, "sram", CXD4108_SRAM_SIZE, &error_fatal);
    memory_region_add_subregion(get_system_memory(), CXD4108_SRAM_BASE, mem);

    mem = g_new(MemoryRegion, 1);
    memory_region_init_ram(mem, NULL, "bootrom", CXD4108_BOOTROM_SIZE, &error_fatal);
    memory_region_set_readonly(mem, true);
    memory_region_add_subregion(get_system_memory(), CXD4108_BOOTROM_BASE, mem);

    for (i = 0; i < CXD4108_NUM_GPIO; i++) {
        dev = qdev_new("bionz_gpio");
        qdev_prop_set_uint8(dev, "version", 1);
        qdev_prop_set_uint8(dev, "num-gpio", 16);
        dev->id = g_strdup_printf("gpio%d", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4108_GPIO_BASE(i));
    }

    dev = qdev_new("bionz_gpio");
    qdev_prop_set_uint8(dev, "version", 1);
    qdev_prop_set_uint8(dev, "num-gpio", 16);
    dev->id = "gpioe";
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4108_GPIOEASY_BASE);

    dev = qdev_new("bionz_gpiosys");
    dev->id = "gpios";
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4108_GPIOSYS_BASE);
    for (i = 0; i < 16; i++) {
        gpio_irq[i] = qdev_get_gpio_in(dev, i);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i, irq[CXD4108_IRQ_CH_GPIO][i]);
    }

    dev = qdev_new("onenand");
    qdev_prop_set_int32(dev, "shift", 1);
    qdev_prop_set_drive(dev, "drive", drive);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4108_NAND_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, gpio_irq[CXD4108_IRQ_GPIO_NAND]);

    dev = qdev_new(TYPE_SYSBUS_SDHCI);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4108_SDHCI_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD4108_IRQ_CH_SDHCI][0]);
    bus = qdev_get_child_bus(dev, "sd-bus");

    dinfo = drive_get(IF_MTD, 0, 1);
    dev = qdev_new(TYPE_SD_CARD);
    qdev_prop_set_bit(dev, "emmc", true);
    qdev_prop_set_drive(dev, "drive", dinfo ? blk_by_legacy_dinfo(dinfo) : NULL);
    qdev_realize_and_unref(dev, bus, &error_fatal);

    dev = qdev_new("inventra_usb");
    qdev_prop_set_bit(dev, "dynfifo", true);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4108_USB_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD4108_IRQ_CH_USB][0]);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1, irq[CXD4108_IRQ_CH_USB][1]);

    for (i = 0; i < CXD4108_NUM_DMA; i++) {
        dev = qdev_new("bionz_dma");
        qdev_prop_set_uint32(dev, "version", 1);
        qdev_prop_set_uint32(dev, "num-channel", CXD4108_DMA_NUM_CHANNEL);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4108_DMA_BASE(i));
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), CXD4108_DMA_NUM_CHANNEL, irq[CXD4108_IRQ_CH_DMA][i]);
    }

    dev = qdev_new("pl320");
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4108_PL320_BASE);
    for (i = 0; i < 2; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i, irq[CXD4108_IRQ_CH_PL320][i]);
    }

    for (i = 0; i < CXD4108_NUM_UART; i++) {
        pl011_create(CXD4108_UART_BASE(i), irq[CXD4108_IRQ_CH_UART][i], serial_hd(i));
    }

    for (i = 0; i < CXD4108_NUM_HWTIMER; i++) {
        dev = qdev_new("bionz_hwtimer");
        qdev_prop_set_uint32(dev, "freq", 2e6);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4108_HWTIMER_BASE(i));
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD4108_IRQ_CH_TIMER][i]);
    }

    for (i = 0; i < CXD4108_NUM_SIO; i++) {
        dev = qdev_new("bionz_sio");
        dev->id = g_strdup_printf("sio%d", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4108_SIO_BASE(i));
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, CXD4108_SIO_BASE(i) + 0x80000);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD4108_IRQ_CH_SIO][i]);
    }

    for (i = 0; i < CXD4108_NUM_ADC; i++) {
        dev = qdev_new("bionz_adc");
        dev->id = g_strdup_printf("adc%d", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4108_ADC_BASE(i));
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD4108_IRQ_CH_ADC][i]);
    }

    dev = qdev_new("arm_power");
    qdev_prop_set_uint64(dev, "cpuid", 1);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4108_CLKBLK_BASE + 0x60);

    dev = qdev_new("bionz_cam_sync");
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4108_CAM_SYNC_BASE);

    dev = qdev_new("bionz_jpeg");
    qdev_prop_set_uint32(dev, "base", CXD4108_DDR_BASE);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4108_JPEG_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, CXD4108_JPEG_BASE + 0x800);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD4108_IRQ_CH_IMGMC][5]);

    for (i = 0; i < CXD4108_NUM_RC; i++) {
        dev = qdev_new("bionz_rc");
        qdev_prop_set_uint32(dev, "base", CXD4108_DDR_BASE);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4108_RC_BASE(i) + 0x1000);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, CXD4108_RC_BASE(i) + 0x2000);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD4108_IRQ_CH_IMGMC][3 + i]);
    }

    dev = qdev_new("bionz_cpyfb");
    qdev_prop_set_uint32(dev, "base", CXD4108_DDR_BASE);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4108_CPYFB_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, CXD4108_CPYFB_BASE + 0x80000);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD4108_IRQ_CH_VIDEO][4]);

    dev = qdev_new("bionz_vip");
    object_property_set_link(OBJECT(dev), "memory", OBJECT(ddr), &error_fatal);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4108_VIP_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, CXD4108_VIP_BASE + 0x800);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD4108_IRQ_CH_VIDEO][0]);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1, irq[CXD4108_IRQ_CH_VIDEO][1]);
    vsync = qdev_get_gpio_in(dev, 0);

    dev = qdev_new("bionz_sysv");
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4108_SYSV_BASE);
    for (i = 0; i < 10; i++) {
        if (i < 3) {
            sysbus_connect_irq(SYS_BUS_DEVICE(dev), i, irq[CXD4108_IRQ_CH_IMGV][i]);
        } else {
            sysbus_connect_irq(SYS_BUS_DEVICE(dev), i, irq[CXD4108_IRQ_CH_SYSV][i - 3]);
        }
    }
    qdev_connect_gpio_out(dev, 0, vsync);

    dev = qdev_new("bionz_audio");
    qdev_prop_set_uint32(dev, "base", CXD4108_DDR_BASE);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4108_AUDIO_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, CXD4108_AUDIO_BASE + 0x1000);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD4108_IRQ_CH_AUDIO][0]);

    if (machine->kernel_filename) {
        load_image_targphys(machine->kernel_filename, CXD4108_DDR_BASE + CXD4108_TEXT_OFFSET, CXD4108_DDR_SIZE - CXD4108_TEXT_OFFSET);
        load_image_targphys(machine->initrd_filename, CXD4108_DDR_BASE + CXD4108_INITRD_OFFSET, CXD4108_DDR_SIZE - CXD4108_INITRD_OFFSET);
        cxd_write_bootloader(CXD4108_BOOTROM_BASE, CXD4108_DDR_BASE + CXD4108_TEXT_OFFSET);
    } else if (bios_name) {
        load_image_targphys(bios_name, CXD4108_BOOTROM_BASE, CXD4108_BOOTROM_SIZE);
    } else if (drive) {
        cxd_write_bootloader(CXD4108_BOOTROM_BASE, cxd_init_loader2(drive));
    }

    cxd_add_const_reg("miscctrl_mode", CXD4108_MISCCTRL_BASE, 0x101);

    cxd_add_const_reg("sdc_para4", CXD4108_SDC_BASE + 0xc, 0x80000000);
}

static void cxd4115_init(MachineState *machine)
{
    DriveInfo *dinfo;
    BlockBackend *drive;
    MemoryRegion *mem;
    DeviceState *dev;
    Object *cpu;
    qemu_irq irq[CXD4115_NUM_IRQ - CXD4115_IRQ_OFFSET];
    qemu_irq gpio_irq[24];
    int i, j, k;

    dinfo = drive_get(IF_MTD, 0, 0);
    drive = dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;

    cpu = object_new(machine->cpu_type);
    object_property_set_bool(cpu, "reset-hivecs", true, &error_fatal);
    qdev_realize(DEVICE(cpu), NULL, &error_fatal);

    mem = g_new(MemoryRegion, 1);
    memory_region_init_ram(mem, NULL, "ddr", CXD4115_DDR_SIZE, &error_fatal);
    memory_region_add_subregion(get_system_memory(), CXD4115_DDR_BASE, mem);

    mem = g_new(MemoryRegion, 1);
    memory_region_init_ram(mem, NULL, "sram", CXD4115_SRAM_SIZE, &error_fatal);
    memory_region_add_subregion(get_system_memory(), CXD4115_SRAM_BASE, mem);

    mem = g_new(MemoryRegion, 1);
    memory_region_init_ram(mem, NULL, "bootrom", CXD4115_BOOTROM_SIZE, &error_fatal);
    memory_region_set_readonly(mem, true);
    memory_region_add_subregion(get_system_memory(), CXD4115_BOOTROM_BASE, mem);

    dev = qdev_new(TYPE_ARM11MPCORE_PRIV);
    qdev_prop_set_uint32(dev, "num-cpu", 1);
    qdev_prop_set_uint32(dev, "num-irq", CXD4115_NUM_IRQ);
    qdev_prop_set_uint32(DEVICE(&ARM11MPCORE_PRIV(dev)->mptimer), "freq", 156e6);
    qdev_prop_set_uint32(DEVICE(&ARM11MPCORE_PRIV(dev)->wdtimer), "freq", 156e6);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4115_MPCORE_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ));
    for (i = 0; i < CXD4115_NUM_IRQ - CXD4115_IRQ_OFFSET; i++) {
        irq[i] = qdev_get_gpio_in(dev, i);
    }

    for (i = 0; i < CXD4115_NUM_GPIO; i++) {
        dev = qdev_new("bionz_gpio");
        qdev_prop_set_uint8(dev, "version", 1);
        dev->id = g_strdup_printf("gpio%d", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4115_GPIO_BASE(i));
        if (i == 0) {
            for (j = 0; j < 20; j++) {
                k = (j < 16) ? j : j + 4;
                gpio_irq[k] = qdev_get_gpio_in(dev, j);
                sysbus_connect_irq(SYS_BUS_DEVICE(dev), j, qemu_irq_split(
                    irq[CXD4115_IRQ_GPIO_RISE(k) - CXD4115_IRQ_OFFSET],
                    qemu_irq_invert(irq[CXD4115_IRQ_GPIO_FALL(k) - CXD4115_IRQ_OFFSET])
                ));
            }
        }
    }

    dev = qdev_new("onenand");
    qdev_prop_set_uint16(dev, "manufacturer_id", NAND_MFR_SAMSUNG);
    qdev_prop_set_int32(dev, "shift", 1);
    qdev_prop_set_drive(dev, "drive", drive);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4115_NAND_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, gpio_irq[CXD4115_IRQ_GPIO_NAND]);

    dev = qdev_new("bionz_dma");
    qdev_prop_set_uint32(dev, "version", 1);
    qdev_prop_set_uint32(dev, "num-channel", CXD4115_DMA_NUM_CHANNEL);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4115_DMA_BASE);
    for (i = 0; i < CXD4115_DMA_NUM_CHANNEL; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i, irq[CXD4115_IRQ_DMA(i) - CXD4115_IRQ_OFFSET]);
    }

    dev = qdev_new("inventra_usb");
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4115_USB_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD4115_IRQ_USB0 - CXD4115_IRQ_OFFSET]);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 1, irq[CXD4115_IRQ_USB1 - CXD4115_IRQ_OFFSET]);

    dev = qdev_new("bionz_ldec");
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4115_LDEC_BASE);

    for (i = 0; i < CXD4115_NUM_HWTIMER; i++) {
        dev = qdev_new("bionz_hwtimer");
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4115_HWTIMER_BASE(i));
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD4115_IRQ_HWTIMER(i) - CXD4115_IRQ_OFFSET]);
    }

    for (i = 0; i < CXD4115_NUM_SIO; i++) {
        dev = qdev_new("bionz_sio");
        dev->id = g_strdup_printf("sio%d", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4115_SIO_BASE(i));
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, CXD4115_SIO_BASE(i) + 0x100);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD4115_IRQ_SIO(i) - CXD4115_IRQ_OFFSET]);
    }

    for (i = 0; i < CXD4115_NUM_UART; i++) {
        pl011_create(CXD4115_UART_BASE(i), irq[CXD4115_IRQ_UART(i) - CXD4115_IRQ_OFFSET], serial_hd(i));
    }

    dev = qdev_new("bionz_bootcon");
    qdev_prop_set_chr(dev, "chardev", serial_hd(0));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4115_BOOTCON_BASE);

    if (machine->kernel_filename) {
        load_image_targphys(machine->kernel_filename, CXD4115_DDR_BASE + CXD4115_TEXT_OFFSET, CXD4115_DDR_SIZE - CXD4115_TEXT_OFFSET);
        load_image_targphys(machine->initrd_filename, CXD4115_DDR_BASE + CXD4115_INITRD_OFFSET, CXD4115_DDR_SIZE - CXD4115_INITRD_OFFSET);
        cxd_write_bootloader(CXD4115_BOOTROM_BASE, CXD4115_DDR_BASE + CXD4115_TEXT_OFFSET);
    } else if (bios_name) {
        load_image_targphys(bios_name, CXD4115_BOOTROM_BASE, CXD4115_BOOTROM_SIZE);
    } else if (drive) {
        cxd_write_bootloader(CXD4115_BOOTROM_BASE, cxd_init_loader2(drive));
        uint32_t typeid = 1;
        rom_add_blob_fixed("typeid", &typeid, sizeof(typeid), CXD4115_SRAM_BASE + CXD4115_TYPEID_OFFSET);
    }

    cxd_add_const_reg("ona_reset", CXD4115_ONA_BASE, 1);
}

static void cxd4132_init(MachineState *machine)
{
    DriveInfo *dinfo;
    BlockBackend *drive;
    MemoryRegion *mem;
    DeviceState *dev;
    Object *cpu;
    qemu_irq irq[CXD4132_NUM_IRQ - CXD4132_IRQ_OFFSET];
    int i;

    dinfo = drive_get(IF_MTD, 0, 0);
    drive = dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;

    cpu = object_new(machine->cpu_type);
    object_property_set_bool(cpu, "reset-hivecs", true, &error_fatal);
    qdev_realize(DEVICE(cpu), NULL, &error_fatal);

    mem = g_new(MemoryRegion, 1);
    memory_region_init_ram(mem, NULL, "ddr", CXD4132_DDR_SIZE, &error_fatal);
    memory_region_add_subregion(get_system_memory(), CXD4132_DDR_BASE, mem);

    mem = g_new(MemoryRegion, 1);
    memory_region_init_ram(mem, NULL, "sram", CXD4132_SRAM_SIZE, &error_fatal);
    memory_region_add_subregion(get_system_memory(), CXD4132_SRAM_BASE, mem);

    mem = g_new(MemoryRegion, 1);
    memory_region_init_ram(mem, NULL, "bootrom", CXD4132_BOOTROM_SIZE, &error_fatal);
    memory_region_set_readonly(mem, true);
    memory_region_add_subregion(get_system_memory(), CXD4132_BOOTROM_BASE, mem);

    dev = qdev_new(TYPE_ARM11MPCORE_PRIV);
    qdev_prop_set_uint32(dev, "num-cpu", 1);
    qdev_prop_set_uint32(dev, "num-irq", CXD4132_NUM_IRQ);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4132_MPCORE_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ));
    for (i = 0; i < CXD4132_NUM_IRQ - CXD4132_IRQ_OFFSET; i++) {
        irq[i] = qdev_get_gpio_in(dev, i);
    }

    for (i = 0; i < CXD4132_NUM_GPIO; i++) {
        dev = qdev_new("bionz_gpio");
        qdev_prop_set_uint8(dev, "version", 2);
        dev->id = g_strdup_printf("gpio%d", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4132_GPIO_BASE(i));
    }

    dev = qdev_new("onenand");
    qdev_prop_set_uint16(dev, "manufacturer_id", NAND_MFR_SAMSUNG);
    qdev_prop_set_int32(dev, "shift", 1);
    qdev_prop_set_drive(dev, "drive", drive);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4132_NAND_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD4132_IRQ_NAND - CXD4132_IRQ_OFFSET]);

    dev = qdev_new("synopsys_usb");
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4132_USB_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD4132_IRQ_USB - CXD4132_IRQ_OFFSET]);

    dev = qdev_new("bionz_bootcon");
    qdev_prop_set_chr(dev, "chardev", serial_hd(0));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4132_BOOTCON_BASE);

    dev = qdev_new("bionz_dma");
    qdev_prop_set_uint32(dev, "version", 2);
    qdev_prop_set_uint32(dev, "num-channel", CXD4132_DMA_NUM_CHANNEL);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4132_DMA_BASE);
    for (i = 0; i < CXD4132_DMA_NUM_CHANNEL; i++) {
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), i, irq[CXD4132_IRQ_DMA(i) - CXD4132_IRQ_OFFSET]);
    }

    dev = qdev_new("bionz_meno");
    if (drive) {
        qdev_prop_set_string(dev, "drive_name", blk_name(drive));
    }
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4132_MENO_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD4132_IRQ_MENO - CXD4132_IRQ_OFFSET]);

    for (i = 0; i < CXD4132_NUM_HWTIMER; i++) {
        dev = qdev_new("bionz_hwtimer");
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4132_HWTIMER_BASE(i));
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD4132_IRQ_HWTIMER(i) - CXD4132_IRQ_OFFSET]);
    }

    for (i = 0; i < CXD4132_NUM_SIO; i++) {
        dev = qdev_new("bionz_sio");
        dev->id = g_strdup_printf("sio%d", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD4132_SIO_BASE(i));
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, CXD4132_SIO_BASE(i) + 0x100);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD4132_IRQ_SIO(i) - CXD4132_IRQ_OFFSET]);
    }

    for (i = 0; i < CXD4132_NUM_UART; i++) {
        pl011_create(CXD4132_UART_BASE(i), irq[CXD4132_IRQ_UART(i) - CXD4132_IRQ_OFFSET], serial_hd(i));
    }

    if (machine->kernel_filename) {
        load_image_targphys(machine->kernel_filename, CXD4132_DDR_BASE + CXD4132_TEXT_OFFSET, CXD4132_DDR_SIZE - CXD4132_TEXT_OFFSET);
        load_image_targphys(machine->initrd_filename, CXD4132_DDR_BASE + CXD4132_INITRD_OFFSET, CXD4132_DDR_SIZE - CXD4132_INITRD_OFFSET);
        cxd_init_cmdline(CXD4132_CMDLINE, machine->kernel_cmdline, CXD4132_DDR_BASE + CXD4132_CMDLINE_OFFSET);
        cxd_write_bootloader(CXD4132_BOOTROM_BASE, CXD4132_DDR_BASE + CXD4132_TEXT_OFFSET);
    } else if (bios_name) {
        load_image_targphys(bios_name, CXD4132_BOOTROM_BASE, CXD4132_BOOTROM_SIZE);
    } else if (drive) {
        cxd_write_bootloader(CXD4132_BOOTROM_BASE, cxd_init_loader2(drive));
    }

    cxd_add_const_reg("miscctrl_readdone", CXD4132_MISCCTRL_BASE(1), 1);
    cxd_add_const_reg("miscctrl_typeid", CXD4132_MISCCTRL_BASE(2), 0x301);
}

static void cxd90014_init(MachineState *machine)
{
    DriveInfo *dinfo;
    BlockBackend *drive;
    MemoryRegion *mem;
    DeviceState *dev;
    Object *cpu;
    qemu_irq irq[CXD90014_NUM_IRQ - CXD90014_IRQ_OFFSET];
    qemu_irq boss_irq;
    int i;

    dinfo = drive_get(IF_MTD, 0, 0);
    drive = dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;

    cpu = object_new(machine->cpu_type);
    object_property_set_bool(cpu, "has_el3", false, &error_fatal);
    object_property_set_bool(cpu, "reset-hivecs", true, &error_fatal);
    qdev_realize(DEVICE(cpu), NULL, &error_fatal);

    mem = g_new(MemoryRegion, 1);
    memory_region_init_ram(mem, NULL, "ddr", CXD90014_DDR_SIZE, &error_fatal);
    memory_region_add_subregion(get_system_memory(), CXD90014_DDR_BASE, mem);

    mem = g_new(MemoryRegion, 1);
    memory_region_init_ram(mem, NULL, "sram", CXD90014_SRAM_SIZE, &error_fatal);
    memory_region_add_subregion(get_system_memory(), CXD90014_SRAM_BASE, mem);

    mem = g_new(MemoryRegion, 1);
    memory_region_init_ram(mem, NULL, "bootrom", CXD90014_BOOTROM_SIZE, &error_fatal);
    memory_region_set_readonly(mem, true);
    memory_region_add_subregion(get_system_memory(), CXD90014_BOOTROM_BASE, mem);

    dev = qdev_new(TYPE_A9MPCORE_PRIV);
    qdev_prop_set_uint32(dev, "num-cpu", 1);
    qdev_prop_set_uint32(dev, "num-irq", CXD90014_NUM_IRQ);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD90014_MPCORE_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ));
    for (i = 0; i < CXD90014_NUM_IRQ - CXD90014_IRQ_OFFSET; i++) {
        irq[i] = qdev_get_gpio_in(dev, i);
    }

    dev = qdev_new("bionz_boss");
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD90014_BOSS_SRAM_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, CXD90014_BOSS_IO_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 2, CXD90014_BOSS_CLKRST_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD90014_IRQ_BOSS - CXD90014_IRQ_OFFSET]);
    boss_irq = qdev_get_gpio_in(dev, 0);

    for (i = 0; i < CXD90014_NUM_GPIO; i++) {
        dev = qdev_new("bionz_gpio");
        qdev_prop_set_uint8(dev, "version", 3);
        dev->id = g_strdup_printf("gpio%d", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD90014_GPIO_BASE(i));
    }

    dev = qdev_new("bionz_nand");
    qdev_prop_set_drive(dev, "drive", drive);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD90014_NAND_REG_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, CXD90014_NAND_DATA_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, boss_irq);

    dev = qdev_new("bionz_bootcon");
    qdev_prop_set_chr(dev, "chardev", serial_hd(0));
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD90014_BOOTCON_BASE);

    dev = qdev_new("fujitsu_usb");
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD90014_USB_BASE);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, CXD90014_USB_HDMAC_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD90014_IRQ_USB - CXD90014_IRQ_OFFSET]);

    for (i = 0; i < CXD90014_NUM_UART; i++) {
        pl011_create(CXD90014_UART_BASE(i), irq[CXD90014_IRQ_UART(i) - CXD90014_IRQ_OFFSET], serial_hd(i));
    }

    for (i = 0; i < CXD90014_NUM_HWTIMER; i++) {
        dev = qdev_new("bionz_hwtimer");
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD90014_HWTIMER_BASE(i));
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD90014_IRQ_HWTIMER(i) - CXD90014_IRQ_OFFSET]);
    }

    for (i = 0; i < CXD90014_NUM_SIO; i++) {
        dev = qdev_new("bionz_sio");
        dev->id = g_strdup_printf("sio%d", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD90014_SIO_BASE(i));
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, CXD90014_SIO_BASE(i) + 0x100);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD90014_IRQ_SIO(i) - CXD90014_IRQ_OFFSET]);
    }

    if (machine->kernel_filename) {
        load_image_targphys(machine->kernel_filename, CXD90014_DDR_BASE + CXD90014_TEXT_OFFSET, CXD90014_DDR_SIZE - CXD90014_TEXT_OFFSET);
        load_image_targphys(machine->initrd_filename, CXD90014_DDR_BASE + CXD90014_INITRD_OFFSET, CXD90014_DDR_SIZE - CXD90014_INITRD_OFFSET);
        cxd_write_bootloader(CXD90014_BOOTROM_BASE, CXD90014_DDR_BASE + CXD90014_TEXT_OFFSET);
    } else if (bios_name) {
        load_image_targphys(bios_name, CXD90014_BOOTROM_BASE, CXD90014_BOOTROM_SIZE);
    } else if (drive) {
        cxd_write_bootloader(CXD90014_BOOTROM_BASE, cxd90014_init_loader2(drive));
    }

    cxd_add_const_reg("miscctrl_typeid", CXD90014_MISCCTRL_BASE(0), 0x500);
    cxd_add_const_reg("miscctrl_mode", CXD90014_MISCCTRL_BASE(1), 0x0c010003);

    cxd_add_const_reg("ddmc_ctl_int_status", CXD90014_DDMC_BASE + 0x128, 0x10);

    cxd_add_const_reg("fusb_otg_usb_id_ext", CXD90014_USB_OTG_BASE + 0x10, 2);
}

/* Experimental LIRO register window: see docs/LIRO-AND-MEMORY.md.
 * Writes are stored; reads of never-written registers return zero, or
 * all-ones when A7R3_LIRO_AUTOREADY is set in the environment. */
typedef struct CXD90045LiroState {
    uint32_t regs[CXD90045_LIRO_NUM_REGS];
    uint8_t written[CXD90045_LIRO_NUM_REGS];
    unsigned trace_count;
} CXD90045LiroState;

static bool cxd90045_liro_trace(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        enabled = getenv("A7R3_LIRO_TRACE") != NULL ||
                  getenv("A7M3_LIRO_TRACE") != NULL;
    }
    return enabled;
}

static bool cxd90045_liro_autoready(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        enabled = getenv("A7R3_LIRO_AUTOREADY") != NULL ||
                  getenv("A7M3_LIRO_AUTOREADY") != NULL;
    }
    return enabled;
}

static bool cxd90045_liro_status_reg(uint32_t offset)
{
    uint32_t low = offset & 0xfff;
    return low == 0x004 || low == 0x02c;
}

static uint64_t cxd90045_liro_read(void *opaque, hwaddr offset, unsigned size)
{
    CXD90045LiroState *s = opaque;
    uint32_t idx = offset >> 2;
    uint32_t value;
    uint32_t pc = 0, lr = 0;

    if (idx >= CXD90045_LIRO_NUM_REGS) {
        return 0;
    }
    if (s->written[idx]) {
        value = s->regs[idx];
    } else if (cxd90045_liro_autoready() &&
               cxd90045_liro_status_reg((uint32_t)offset)) {
        value = 0xffffffffu;
    } else {
        value = 0;
    }
    if (current_cpu) {
        pc = ARM_CPU(current_cpu)->env.regs[15];
        lr = ARM_CPU(current_cpu)->env.regs[14];
    }
    if (cxd90045_liro_trace() && s->trace_count < 4000 &&
        pc >= 0x635c0000 && pc < 0x65000000) {
        fprintf(stderr,
                "A7R3-LIRO RD[%u] pc=%08x lr=%08x off=%04x val=%08x size=%u\n",
                s->trace_count, pc, lr, (unsigned)offset, value, size);
        s->trace_count++;
    }
    return value;
}

static void cxd90045_liro_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned size)
{
    CXD90045LiroState *s = opaque;
    uint32_t idx = offset >> 2;
    uint32_t pc = 0, lr = 0;

    if (idx >= CXD90045_LIRO_NUM_REGS) {
        return;
    }
    /* The register at instance offset 0x030 is a "start and self-clear"
     * bit: firmware writes 1, then polls until the register reads 0. */
    if ((offset & 0xfff) == 0x030) {
        s->regs[idx] = 0;
    } else {
        s->regs[idx] = (uint32_t)value;
    }
    s->written[idx] = 1;
    if (current_cpu) {
        pc = ARM_CPU(current_cpu)->env.regs[15];
        lr = ARM_CPU(current_cpu)->env.regs[14];
    }
    if (cxd90045_liro_trace() && s->trace_count < 4000 &&
        pc >= 0x635c0000 && pc < 0x65000000) {
        fprintf(stderr,
                "A7R3-LIRO WR[%u] pc=%08x lr=%08x off=%04x val=%08x size=%u\n",
                s->trace_count, pc, lr, (unsigned)offset, (unsigned)value, size);
        s->trace_count++;
    }
}

static const MemoryRegionOps cxd90045_liro_ops = {
    .read = cxd90045_liro_read,
    .write = cxd90045_liro_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

#define TYPE_CXD90045_SUGI "cxd90045_sugi"
#define CXD90045_SUGI(obj) \
    OBJECT_CHECK(CXD90045SugiState, (obj), TYPE_CXD90045_SUGI)

typedef struct CXD90045SugiState {
    SysBusDevice parent_obj;
    MemoryRegion mmio;
    qemu_irq irq;
    uint32_t regs[CXD90045_SUGI_NUM_REGS];
    uint32_t status[CXD90045_SUGI_NUM_BLOCKS];
    uint8_t pending[CXD90045_SUGI_NUM_BLOCKS];
    uint32_t irq_status;
    unsigned trace_count;
} CXD90045SugiState;

static bool cxd90045_sugi_trace(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        enabled = getenv("A7R3_SUGI_TRACE") != NULL ||
                  getenv("A7M3_SUGI_TRACE") != NULL;
    }
    return enabled;
}

static bool cxd90045_sugi_ack_off(uint32_t low)
{
    return low == 0xc18 || low == 0x01c || low == 0x03c || low == 0x044;
}

static uint64_t cxd90045_sugi_read(void *opaque, hwaddr offset, unsigned size)
{
    CXD90045SugiState *s = opaque;
    uint32_t idx = offset >> 2;
    uint32_t low = offset & 0xfff;
    uint32_t val;

    if (idx >= CXD90045_SUGI_NUM_REGS) {
        return 0;
    }
    val = s->regs[idx];
    if (low == 0x034 && s->irq_status) {
        val = s->irq_status;
    }
    if (cxd90045_sugi_trace() && s->trace_count < 4000) {
        uint32_t pc = 0, lr = 0;
        CPUState *cpu = current_cpu;
        if (cpu) {
            pc = ARM_CPU(cpu)->env.regs[15];
            lr = ARM_CPU(cpu)->env.regs[14];
        }
        fprintf(stderr, "A7R3-SUGI RD[%u] pc=%08x lr=%08x off=%04x val=%08x "
                "size=%u\n", s->trace_count, pc, lr, (unsigned)offset, val,
                size);
        s->trace_count++;
    }
    return val;
}


static void cxd90045_sugi_surface_dump(unsigned idx);

/* --- optional presentation overlay (A7R3_OVERLAY=<raw>) --- */
static const uint16_t *cxd90045_overlay_pixels(void)
{
    static uint16_t *pixels;
    static int tried;
    const char *path = getenv("A7R3_OVERLAY");
    FILE *f;
    size_t want = 640u * 480u * 2u;

    if (tried) {
        return pixels;
    }
    tried = 1;
    if (!path || !*path) {
        return NULL;
    }
    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "A7R3-OVERLAY open failed: %s\n", path);
        return NULL;
    }
    pixels = g_malloc(want);
    if (fread(pixels, 1, want, f) != want) {
        fprintf(stderr, "A7R3-OVERLAY short read: %s\n", path);
        g_free(pixels);
        pixels = NULL;
    }
    fclose(f);
    if (pixels) {
        fprintf(stderr, "A7R3-OVERLAY loaded %s\n", path);
    }
    return pixels;
}

static void cxd90045_sugi_blit(CXD90045SugiState *s)
{
    uint64_t t0 = sugi_now_ns();
    uint32_t src = s->regs[0x0c00 >> 2] << 2;
    uint32_t dst = s->regs[0x0c04 >> 2] << 2;
    uint32_t wh = s->regs[0x0c08 >> 2];
    uint32_t fmt = s->regs[0x0c10 >> 2];
    unsigned w = wh & 0xffff;
    unsigned h = wh >> 16;
    unsigned y;
    uint8_t *buf;

    if (getenv("A7R3_BLIT_LOG")) {
        static unsigned blit_log_n;
        if (blit_log_n < 5000) {
            blit_log_n++;
            fprintf(stderr,
                    "CMDBLIT t=%lld n=%u src=%08x dst=%08x %ux%u fmt=%08x %s\n",
                    (long long)qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL),
                    blit_log_n, (unsigned)src, (unsigned)dst, w, h,
                    (unsigned)fmt,
                    (!w || !h || w > 2048 || h > 2048 || fmt != 0x4401 ||
                     src == dst) ? "skip" : "copy");
        }
    }

    if (!w || !h || w > 2048 || h > 2048 || fmt != 0x4401 || src == dst) {
        return;
    }
    buf = g_malloc(w * 2);
    for (y = 0; y < h; y++) {
        cpu_physical_memory_read(src + y * w * 2, buf, w * 2);
        cpu_physical_memory_write(dst + y * w * 2, buf, w * 2);
    }
    g_free(buf);
    g_sugi_blit_ns += sugi_now_ns() - t0;
    if (cxd90045_sugi_trace()) {
        fprintf(stderr, "A7R3-SUGI BLIT src=%08x dst=%08x %ux%u fmt=%08x\n",
                src, dst, w, h, fmt);
    }
    /* Dump the composed surface at present time (final menu frames). */
    if (getenv("A7R3_BLIT_DUMP")) {
        static unsigned blit_n;
        cxd90045_sugi_surface_dump(9000 + (blit_n++ % 500));
    }
}

/* --- software rasterizer for the observed 2D command lists --- */
static bool cxd90045_sugi_exec_enabled(void)
{
    static int enabled = -1;
    if (enabled < 0) {
        enabled = getenv("A7R3_SUGI_EXEC") != NULL;
    }
    return enabled;
}

typedef struct {
    uint32_t surf, tex, texw, texh, color;
    uint32_t r47, r48;
    uint32_t areaw, areah;
    float u[4];
    int has_u;
    int has_color; /* SUGI_COLOR_FILL: 0x0105 color register seen */
    unsigned draws;
} SugiSoftState;
static SugiSoftState sugi_soft;

static bool cxd90045_sugi_pa_ok(hwaddr a, unsigned n)
{
    return (a + n <= 0x40000000ULL) ||
           (a >= 0x80000000ULL && a + n <= 0x90000000ULL);
}

static hwaddr cxd90045_sugi_dump_pa(void)
{
    static hwaddr pa;
    if (!pa) {
        const char *s = getenv("A7R3_DUMP_PA");
        pa = s && *s ? (hwaddr)strtoull(s, NULL, 0) : 0x202e0200ULL;
    }
    return pa;
}

static void cxd90045_sugi_surface_dump(unsigned idx)
{
    const char *dir = getenv("A7R3_SURF_DIR");
    char path[256];
    FILE *f;
    static unsigned char surf[640 * 480 * 2];
    if (!dir) {
        return;
    }
    if (!cxd90045_sugi_pa_ok(cxd90045_sugi_dump_pa(), sizeof(surf))) {
        return;
    }
    uint64_t t0 = sugi_now_ns();
    cpu_physical_memory_read(cxd90045_sugi_dump_pa(), surf, sizeof(surf));
    if (getenv("A7R3_SURF_MORTON")) {
        static unsigned char lin[640 * 480 * 2];
        unsigned x, y;
        for (y = 0; y < 480; y++) {
            for (x = 0; x < 640; x++) {
                unsigned yy = 479u - y;
                unsigned z = (x & 1) | ((yy & 1) << 1) | ((x & 2) << 1) |
                             ((yy & 2) << 2) | ((x & 4) << 2) |
                             ((yy & 4) << 3);
                unsigned off = (((yy >> 3) * 80 + (x >> 3)) * 64 + z) * 2;
                lin[y * 1280 + x * 2] = surf[off];
                lin[y * 1280 + x * 2 + 1] = surf[off + 1];
            }
        }
        memcpy(surf, lin, sizeof(surf));
    }
    {
        const uint16_t *ov = cxd90045_overlay_pixels();
        if (ov) {
            unsigned i;
            uint16_t *dst16 = (uint16_t *)surf;
            for (i = 0; i < 640u * 480u; i++) {
                if (ov[i] & 0x000fu) {
                    dst16[i] = ov[i];
                }
            }
        }
    }
    snprintf(path, sizeof(path), "%s/frame_%04u.raw", dir, idx);
    f = fopen(path, "wb");
    if (!f) {
        return;
    }
    fwrite(surf, 1, sizeof(surf), f);
    fclose(f);
    g_sugi_dump_ns += sugi_now_ns() - t0;
}

static void cxd90045_sugi_soft_draw(unsigned idx)
{
    int vx0 = (int16_t)(sugi_soft.r47 & 0xffff);
    int vy0 = (int16_t)(sugi_soft.r47 >> 16);
    int vx1 = (int16_t)(sugi_soft.r48 & 0xffff);
    int vy1 = (int16_t)(sugi_soft.r48 >> 16);
    int x0, y0, x1, y1, ox0, oy0, ox1, oy1;
    int tw = (int)sugi_soft.texw, th = (int)sugi_soft.texh;
    int center = getenv("A7R3_SUGI_CENTER") != NULL ||
                 getenv("A7R3_SUGI_CENTERSCALE") != NULL;
    int cscale = getenv("A7R3_SUGI_CENTERSCALE") != NULL;
    int hybrid = getenv("A7R3_SUGI_HYBRID") != NULL;
    int cw = sugi_soft.areaw ? (int)sugi_soft.areaw : 640;
    int ch = sugi_soft.areah ? (int)sugi_soft.areah : 480;
    uint32_t surf = sugi_soft.surf, tex = sugi_soft.tex;
    static uint16_t srcrow[4096];
    static unsigned char dstrow[2048];
    int y;

    if (sugi_soft.draws++ >= 512) {
        return;
    }
    {
        const char *skip = getenv("A7R3_SUGI_SKIP_TEX");
        if (skip && *skip) {
            char sbuf[128];
            char *tok;
            size_t sl = strlen(skip);
            if (sl >= sizeof(sbuf)) {
                sl = sizeof(sbuf) - 1;
            }
            memcpy(sbuf, skip, sl);
            sbuf[sl] = 0;
            for (tok = strtok(sbuf, ","); tok; tok = strtok(NULL, ",")) {
                unsigned long sv = strtoul(tok, NULL, 16);
                if ((uint32_t)sv == tex) {
                    return;
                }
            }
        }
    }
    if (center) {
        x0 = (vx0 + cw) >> 1;
        x1 = (vx1 + cw) >> 1;
        y0 = (vy0 + ch) >> 1;
        y1 = (vy1 + ch) >> 1;
    } else if (getenv("A7R3_SUGI_HALF")) {
        x0 = vx0 >> 1;
        y0 = vy0 >> 1;
        x1 = vx1 >> 1;
        y1 = vy1 >> 1;
    } else {
        x0 = vx0; y0 = vy0; x1 = vx1; y1 = vy1;
    }
    {
        const char *tdir = getenv("A7R3_TEX_DIR");
        static unsigned tex_dumps;
        if (tdir && tex_dumps < 256 && tw > 0 && th > 0 &&
            tw <= 2048 && th <= 2048 &&
            cxd90045_sugi_pa_ok(tex, (hwaddr)tw * th * 2)) {
            char tpath[256];
            unsigned char *tbuf = g_malloc((size_t)tw * th * 2);
            snprintf(tpath, sizeof(tpath), "%s/tex_%04u_%dx%d.raw",
                     tdir, sugi_soft.draws, tw, th);
            cpu_physical_memory_read(tex, tbuf, (size_t)tw * th * 2);
            FILE *tf = fopen(tpath, "wb");
            if (tf) {
                fwrite(tbuf, 1, (size_t)tw * th * 2, tf);
                fclose(tf);
                tex_dumps++;
            }
            g_free(tbuf);
        }
    }
    if (sugi_soft.draws <= 300) {
        fprintf(stderr, "SUGI-SD[%u] surf=%08x tex=%08x %dx%d "
                "r47=%08x r48=%08x col=%08x%s\n",
                sugi_soft.draws, surf, tex, tw, th,
                sugi_soft.r47, sugi_soft.r48, sugi_soft.color,
                center ? " C" : "");
    }
    if (x1 <= x0) { int t = x0; x0 = x1; x1 = t; }
    if (y1 <= y0) { int t = y0; y0 = y1; y1 = t; }
    ox0 = x0; oy0 = y0; ox1 = x1; oy1 = y1;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > 640) x1 = 640;
    if (y1 > 480) y1 = 480;
    if (x1 <= x0 || y1 <= y0) {
        return;
    }
    if (tw <= 0 || th <= 0 || tw > 2048 || th > 2048) {
        return;
    }
    if (!cxd90045_sugi_pa_ok(surf, 640 * 480 * 2)) {
        return;
    }
    if (!cxd90045_sugi_pa_ok(tex, (hwaddr)tw * th * 2)) {
        return;
    }
    for (y = y0; y < y1; y++) {
        int v;
        int x;
        hwaddr drow = surf + (hwaddr)y * 640 * 2;
        if (center && (!cscale || (hybrid && (oy1 - oy0) > th + (th >> 1)))) {
            v = y - oy0;
            if (v >= 0) v %= th;
        } else {
            v = (int)((uint64_t)(y - oy0) * th / (oy1 - oy0));
            if (v >= th) v = th - 1;
        }
        cpu_physical_memory_read(tex + (hwaddr)v * tw * 2, srcrow, tw * 2);
        cpu_physical_memory_read(drow, dstrow, 640 * 2);
        for (x = x0; x < x1; x++) {
            int u;
            uint16_t pix;
            unsigned a;
            if (center && (!cscale || (hybrid && (ox1 - ox0) > tw + (tw >> 1)))) {
                u = x - ox0;
                if (u >= 0) u %= tw;
            } else {
                u = (int)((uint64_t)(x - ox0) * tw / (ox1 - ox0));
                if (u >= tw) u = tw - 1;
            }
            pix = srcrow[u];
            a = pix & 0xF;
            if (a == 0) {
                continue;
            }
            if (a < 0xF) {
                uint16_t dp = (uint16_t)(dstrow[x * 2] |
                                         (dstrow[x * 2 + 1] << 8));
                unsigned sr = (pix >> 12) & 0xF, sg = (pix >> 8) & 0xF;
                unsigned sb = (pix >> 4) & 0xF;
                unsigned dr = (dp >> 12) & 0xF, dg = (dp >> 8) & 0xF;
                unsigned db = (dp >> 4) & 0xF;
                unsigned rr = (sr * a + dr * (15 - a) + 7) / 15;
                unsigned rg = (sg * a + dg * (15 - a) + 7) / 15;
                unsigned rb = (sb * a + db * (15 - a) + 7) / 15;
                pix = (uint16_t)((rr << 12) | (rg << 8) | (rb << 4) | 0xF);
            }
            dstrow[x * 2] = pix & 0xff;
            dstrow[x * 2 + 1] = pix >> 8;
        }
        cpu_physical_memory_write(drow, dstrow, 640 * 2);
    }
    cxd90045_sugi_surface_dump(sugi_soft.draws);
}

static void cxd90045_sugi_unit_draw(unsigned idx)
{
    int vx0 = (int16_t)(sugi_soft.r47 & 0xffff);
    int vy0 = (int16_t)(sugi_soft.r47 >> 16);
    int vx1 = (int16_t)(sugi_soft.r48 & 0xffff);
    int vy1 = (int16_t)(sugi_soft.r48 >> 16);
    int cw = sugi_soft.areaw ? (int)sugi_soft.areaw : 640;
    int ch = sugi_soft.areah ? (int)sugi_soft.areah : 480;
    uint32_t surf = sugi_soft.surf ? sugi_soft.surf : 0x202E0200u;
    int x0 = (vx0 + cw) >> 1, x1 = (vx1 + cw) >> 1;
    int y0 = (vy0 + ch) >> 1, y1 = (vy1 + ch) >> 1;
    uint16_t pix;
    int x, y;
    unsigned char row[640 * 2];

    if (x1 <= x0) { int t = x0; x0 = x1; x1 = t; }
    if (y1 <= y0) { int t = y0; y0 = y1; y1 = t; }
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > 640) x1 = 640;
    if (y1 > 480) y1 = 480;
    if (x1 <= x0 || y1 <= y0)
        return;
    if (!cxd90045_sugi_pa_ok(surf, 640 * 480 * 2))
        return;
    if (sugi_soft.has_color) {
        unsigned c = (unsigned)sugi_soft.color;
        unsigned r = (c >> 28) & 0xf, g = (c >> 20) & 0xf;
        unsigned b = (c >> 12) & 0xf, a = (c >> 4) & 0xf; /* SUGI_COLOR_FILL */
        pix = (uint16_t)((r << 12) | (g << 8) | (b << 4) | a);
    } else if (sugi_soft.has_u) {
        unsigned r = (unsigned)(sugi_soft.u[0] * 15.0f + 0.5f);
        unsigned g = (unsigned)(sugi_soft.u[1] * 15.0f + 0.5f);
        unsigned b = (unsigned)(sugi_soft.u[2] * 15.0f + 0.5f);
        unsigned a = (unsigned)(sugi_soft.u[3] * 15.0f + 0.5f);
        if (r > 15) r = 15;
        if (g > 15) g = 15;
        if (b > 15) b = 15;
        if (a > 15) a = 15;
        pix = (uint16_t)((r << 12) | (g << 8) | (b << 4) | a);
    } else {
        pix = 0xFFFF;
    }
    if (getenv("A7R3_BLACK_BACKDROP") && pix == 0xFFFF &&
        (x1 - x0) >= 64 && (y1 - y0) >= 64) {
        static int bd_n; /* SUGI_BLACK_BACKDROP */
        if (bd_n < 50) {
            bd_n++;
            fprintf(stderr, "UDSKIP rect=%d,%d-%d,%d %s\n", x0, y0, x1, y1,
                    (x0 == 0 && x1 == 640) ? "black" : "skip");
        }
        if (x0 == 0 && x1 == 640)
            pix = 0x000F;
        else
            return;
    } else if (getenv("A7R3_BLACK_FILL") && pix == 0xFFFF &&
        (x1 - x0) >= 64 && (y1 - y0) >= 64) {
        static int blk_n; /* SUGI_BLACK_FILL */
        if (blk_n < 50) {
            blk_n++;
            fprintf(stderr, "UDSKIP rect=%d,%d-%d,%d black\n", x0, y0, x1, y1);
        }
        pix = 0x000F;
    } else if (!getenv("A7R3_ALLOW_WHITE") && pix == 0xFFFF &&
        (x1 - x0) >= 16 && (y1 - y0) >= 16) {
        /* SUGI_FILL_POLICY2: the Sugilite unit-draw parameters are only
         * partially modelled; these big "white" fills are a stale u[]
         * uniform, not the real page color.  Full-width fills are the page
         * backdrop (drawn before content) -> paint black; the smaller ones
         * are drawn over content -> skip. */
        static int skip_n; /* SUGI_SKIP_WHITE */
        if (skip_n < 50) {
            skip_n++;
            fprintf(stderr, "UDSKIP rect=%d,%d-%d,%d %s\n", x0, y0, x1, y1,
                    (x0 == 0 && x1 == 640) ? "black" : "skip");
        }
        if (x0 == 0 && x1 == 640)
            pix = 0x000F;
        else
            return;
    }
    if (getenv("A7R3_UD_LOG") && (x1 - x0) >= 64 && (y1 - y0) >= 16) {
        /* UDLOG_SMALL */
        static int ud_n;
        if (ud_n < 300) {
            ud_n++;
            fprintf(stderr, "UDLOG surf=%08x rect=%d,%d-%d,%d pix=%04x has_u=%d "
                    "col=%08x u=%.3f,%.3f,%.3f,%.3f\n",
                    (unsigned)surf, x0, y0, x1, y1, pix, sugi_soft.has_u,
                    (unsigned)sugi_soft.color, sugi_soft.u[0], sugi_soft.u[1],
                    sugi_soft.u[2], sugi_soft.u[3]);
        }
    }
    if (getenv("A7R3_SURF_MORTON") && surf == 0x202e0200u &&
        sugi_soft.areaw == 640 && sugi_soft.areah == 480) {
        unsigned char tile[128];
        for (y = y0; y < y1; y++) {
            unsigned yy = 479u - (unsigned)y;
            unsigned ytile = yy >> 3;
            unsigned yin = yy & 7;
            unsigned gx;
            for (gx = (unsigned)x0; gx < (unsigned)x1; ) {
                unsigned gx0 = gx & ~7u;
                unsigned gx1 = (gx | 7u) + 1;
                unsigned tx = gx0 >> 3;
                hwaddr base = surf +
                    (hwaddr)(((ytile * 80 + tx) * 64) * 2);
                unsigned cx;
                cpu_physical_memory_read(base, tile, 128);
                for (cx = gx; cx < (unsigned)x1 && cx < gx1; cx++) {
                    unsigned xin = cx & 7;
                    unsigned z = (xin & 1) | ((yin & 1) << 1) |
                                 ((xin & 2) << 1) | ((yin & 2) << 2) |
                                 ((xin & 4) << 2) | ((yin & 4) << 3);
                    {
                        uint16_t oldp = (uint16_t)(tile[z * 2] |
                                                   (tile[z * 2 + 1] << 8));
                        uint16_t out = pix;
                        unsigned sa = pix & 15;
                        if (sugi_soft.has_color && sa) {
                            unsigned sr = (pix >> 12) & 15, sg = (pix >> 8) & 15;
                            unsigned sb = (pix >> 4) & 15;
                            unsigned dr = (oldp >> 12) & 15, dg = (oldp >> 8) & 15;
                            unsigned db = (oldp >> 4) & 15;
                            unsigned rr = (sr * sa + dr * (15 - sa) + 7) / 15;
                            unsigned rg = (sg * sa + dg * (15 - sa) + 7) / 15;
                            unsigned rb = (sb * sa + db * (15 - sa) + 7) / 15;
                            out = (uint16_t)((rr << 12) | (rg << 8) |
                                             (rb << 4) | 15);
                        }
                        tile[z * 2] = out & 0xff;
                        tile[z * 2 + 1] = out >> 8;
                    }
                }
                cpu_physical_memory_write(base, tile, 128);
                gx = gx1;
            }
        }
    } else {
        for (y = y0; y < y1; y++) {
            cpu_physical_memory_read(surf + (hwaddr)y * 640 * 2, row, sizeof(row));
            for (x = x0; x < x1; x++) {
                uint16_t oldp = (uint16_t)(row[x * 2] | (row[x * 2 + 1] << 8));
                uint16_t out = pix;
                unsigned sa = pix & 15;
                if (sugi_soft.has_color && sa) {
                    unsigned sr = (pix >> 12) & 15, sg = (pix >> 8) & 15;
                    unsigned sb = (pix >> 4) & 15;
                    unsigned dr = (oldp >> 12) & 15, dg = (oldp >> 8) & 15;
                    unsigned db = (oldp >> 4) & 15;
                    unsigned rr = (sr * sa + dr * (15 - sa) + 7) / 15;
                    unsigned rg = (sg * sa + dg * (15 - sa) + 7) / 15;
                    unsigned rb = (sb * sa + db * (15 - sa) + 7) / 15;
                    out = (uint16_t)((rr << 12) | (rg << 8) | (rb << 4) | 15);
                }
                row[x * 2] = out & 0xff;
                row[x * 2 + 1] = out >> 8;
            }
            cpu_physical_memory_write(surf + (hwaddr)y * 640 * 2, row, sizeof(row));
        }
    }
    if (getenv("A7R3_SUGI_TRACE"))
        fprintf(stderr, "SUGI-UD[%u] surf=%08x rect=%d,%d-%d,%d pix=%04x\n",
                idx, (unsigned)surf, x0, y0, x1, y1, pix);
    if (!getenv("A7R3_NO_UNIT_DUMP"))
        cxd90045_sugi_surface_dump(sugi_soft.draws++);
}

static void cxd90045_sugi_soft_exec(unsigned idx, uint32_t base)
{
    uint32_t buf[1024];
    int i;
    int cmds = 0, draws = 0;
    if (!cxd90045_sugi_exec_enabled()) {
        return;
    }
    if (!cxd90045_sugi_pa_ok((hwaddr)base, sizeof(buf))) {
        return;
    }
    cpu_physical_memory_read((hwaddr)base, buf, sizeof(buf));
    for (i = 0; i + 1 < 1024; i++) {
        if ((buf[i] & 0xffff) == 0xf000) {
            cmds++;
            if ((buf[i] >> 16) == 0x0202)
                draws++;
        }
    }
    if (getenv("A7R3_CMD_DUMP") && cmds > 0) { /* A7R3_CMD_DEC2 */
        const char *cd = getenv("A7R3_SURF_DIR");
        if (cd) {
            char cp[256];
            FILE *cf;
            snprintf(cp, sizeof(cp), "%s/cmdbuf_%04u_%08x.bin", cd, idx,
                     (unsigned)base);
            cf = fopen(cp, "wb");
            if (cf) {
                fwrite(buf, 1, sizeof(buf), cf);
                fclose(cf);
            }
        }
    }
    if (getenv("A7R3_SUGI_TRACE")) {
        fprintf(stderr, "SUGI-OPS[%u] base=%08x cmds=%d draws=%d\n",
                idx, (unsigned)base, cmds, draws);
        if (idx >= 11 && idx < 40) {
            int k;
            fprintf(stderr, "SUGI-LIST[%u]:", idx);
            for (k = 0; k + 1 < 296; k += 2) {
                fprintf(stderr, " %08x:%08x", buf[k], buf[k + 1]);
                if (buf[k] == 0x00000281 &&
                    cxd90045_sugi_pa_ok((hwaddr)buf[k + 1], 32)) {
                    uint32_t nb[8];
                    int q;
                    cpu_physical_memory_read((hwaddr)buf[k + 1], nb,
                                             sizeof(nb));
                    fprintf(stderr, "\nSUGI-NEST2[%u] ptr=%08x:", idx,
                            buf[k + 1]);
                    for (q = 0; q < 8; q++)
                        fprintf(stderr, " %08x", nb[q]);
                }
            }
            fprintf(stderr, "\n");
        }
    }
    for (i = 0; i + 1 < 1024; i++) {
        uint32_t w = buf[i];
        uint32_t op, val;
        if (w == 0x00000281u) {
            if (getenv("A7R3_NEST_LOG")) {
                static int nn;
                if (nn < 120 &&
                    cxd90045_sugi_pa_ok((hwaddr)buf[i + 1], 64)) {
                    uint32_t nb[16];
                    int q;
                    nn++;
                    cpu_physical_memory_read((hwaddr)buf[i + 1], nb,
                                             sizeof(nb));
                    fprintf(stderr, "NESTLOG[%u] ptr=%08x:", idx,
                            (unsigned)buf[i + 1]);
                    for (q = 0; q < 16; q++)
                        fprintf(stderr, " %08x", nb[q]);
                    fprintf(stderr, "\n");
                }
            }
            cxd90045_sugi_unit_draw(idx);
            continue;
        }
        if ((w & 0xffff) != 0xf000) {
            continue;
        }
        op = w >> 16;
        val = buf[i + 1];
        switch (op) {
        case 0x0112:
            sugi_soft.surf = val << 4;
            break;
        case 0x0589:
            sugi_soft.tex = val << 3;
            break;
        case 0x0586:
        case 0x0587:
            sugi_soft.texw = val >> 16;
            sugi_soft.texh = val & 0xffff;
            break;
        case 0x0049:
            sugi_soft.areaw = val & 0xffff;
            sugi_soft.areah = val >> 16;
            break;
        case 0x0047:
            sugi_soft.r47 = val;
            break;
        case 0x0048:
            sugi_soft.r48 = val;
            break;
        case 0x0105:
            sugi_soft.color = val;
            sugi_soft.has_color = 1;
            break;
        case 0x0451:
            memcpy(&sugi_soft.u[0], &val, 4);
            sugi_soft.has_u = 1;
            break;
        case 0x0452:
            memcpy(&sugi_soft.u[1], &val, 4);
            break;
        case 0x0453:
            memcpy(&sugi_soft.u[2], &val, 4);
            break;
        case 0x0454:
            memcpy(&sugi_soft.u[3], &val, 4);
            break;
        case 0x0216:
            if (getenv("A7R3_SUGI_TRACE")) {
                static int cb_dumps;
                if (cb_dumps < 4 && cxd90045_sugi_pa_ok((hwaddr)val, 64)) {
                    uint32_t cb[16];
                    int q;
                    cb_dumps++;
                    cpu_physical_memory_read((hwaddr)val, cb, sizeof(cb));
                    fprintf(stderr, "SUGI-CB[%u] ptr=%08x:", idx,
                            (unsigned)val);
                    for (q = 0; q < 16; q++)
                        fprintf(stderr, " %08x", cb[q]);
                    fprintf(stderr, "\n");
                }
            }
            break;
        case 0x0202:
            cxd90045_sugi_soft_draw(idx);
            break;
        case 0x0000:
            if (getenv("A7R3_SUGI_TRACE") && idx >= 11 && idx < 48 &&
                cxd90045_sugi_pa_ok((hwaddr)val, 32)) {
                uint32_t nb[8];
                int q;
                cpu_physical_memory_read((hwaddr)val, nb, sizeof(nb));
                fprintf(stderr, "SUGI-NEST[%u] ptr=%08x:", idx,
                        (unsigned)val);
                for (q = 0; q < 8; q++)
                    fprintf(stderr, " %08x", nb[q]);
                fprintf(stderr, "\n");
            }
            break;
        default:
            break;
        }
        i++;
    }
    if (getenv("A7R3_CMD_DEC")) {
        static unsigned dec_n;
        unsigned n281 = 0, nsurf = 0, ntex = 0;
        uint32_t dsurf = 0, area = 0, r47 = 0, r48 = 0, tex = 0;
        for (i = 0; i + 1 < 1024; i++) {
            uint32_t w2 = buf[i];
            if (w2 == 0x00000281u) {
                n281++;
                continue;
            }
            if ((w2 & 0xffff) != 0xf000) {
                continue;
            }
            switch (w2 >> 16) {
            case 0x0112:
                dsurf = buf[i + 1] << 4;
                nsurf++;
                break;
            case 0x0049:
                area = buf[i + 1];
                break;
            case 0x0047:
                r47 = buf[i + 1];
                break;
            case 0x0048:
                r48 = buf[i + 1];
                break;
            case 0x0589:
                tex = buf[i + 1] << 3;
                ntex++;
                break;
            default:
                break;
            }
            i++;
        }
        if (dec_n < 800) {
            dec_n++;
            fprintf(stderr,
                    "CMDDEC t=%lld n=%u idx=%u base=%08x cmds=%d draws=%d "
                    "term=%u surf=%08x nsurf=%u area=%ux%u r47=%08x r48=%08x "
                    "tex=%08x ntex=%u head=%08x,%08x,%08x,%08x,%08x,%08x,"
                    "%08x,%08x\n",
                    (long long)qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL),
                    dec_n, idx, (unsigned)base, cmds, draws, n281,
                    (unsigned)dsurf, nsurf, (unsigned)(area & 0xffffu),
                    (unsigned)(area >> 16), (unsigned)r47, (unsigned)r48,
                    (unsigned)tex, ntex, buf[0], buf[1], buf[2], buf[3],
                    buf[4], buf[5], buf[6], buf[7]);
        }
    }
}

static unsigned int cxd90045_sugi_cmd_count;
static void cxd90045_sugi_capture_cmd(uint32_t word_addr)
{
    unsigned int idx = cxd90045_sugi_cmd_count++;
    if (getenv("A7R3_SUGI_TRACE") && cxd90045_sugi_cmd_count < 64) {
        uint32_t buf[16];
        int i;
        if (cxd90045_sugi_pa_ok((hwaddr)word_addr, sizeof(buf))) {
            cpu_physical_memory_read((hwaddr)word_addr, buf, sizeof(buf));
            fprintf(stderr, "SUGI-CAP[%u] addr=%08x:", idx,
                    (unsigned)word_addr);
            for (i = 0; i < 8; i++)
                fprintf(stderr, " %08x", buf[i]);
            fprintf(stderr, "\n");
        }
    }
    {
        uint64_t t0 = sugi_now_ns();
        cxd90045_sugi_soft_exec(idx, word_addr);
        g_sugi_exec_ns += sugi_now_ns() - t0;
        if ((++g_sugi_exec_n % 8) == 0)
            sugi_time_report();
    }
    if (getenv("A7R3_KICK_DUMP")) {
        static unsigned kick_n;
        if ((kick_n++ % 5) == 4)
            cxd90045_sugi_surface_dump(8000 + (kick_n % 400));
    }
}



static uint32_t cxd90045_sugi_last_cmd;

static void cxd90045_sugi_write(void *opaque, hwaddr offset, uint64_t value,
                                unsigned size)
{
    CXD90045SugiState *s = opaque;
    uint32_t idx = offset >> 2;
    uint32_t low = offset & 0xfff;
    uint32_t block = (offset >> 12) & (CXD90045_SUGI_NUM_BLOCKS - 1);

    if (idx >= CXD90045_SUGI_NUM_REGS) {
        return;
    }
    s->regs[idx] = (uint32_t)value;
    if (offset == 0x0c18 && (uint32_t)value) {
        cxd90045_sugi_blit(s);
    }

    /* debug: the guest asks for a host-side dump of the composed surface by
     * writing a magic value to BAR+0x3000 (A7R3_DEBUG_DUMP).  Needed to show
     * pixels a payload wrote into PA 0x202E0200 after the last blit. */
    if (offset == 0x3000 && (uint32_t)value == 0xa7d00001u) {
        static unsigned dbg_dump_n;
        cxd90045_sugi_surface_dump(8000 + (dbg_dump_n++ % 500));
    }
    if (offset == 0x1804) {
        cxd90045_sugi_last_cmd = (uint32_t)value;
    }
    if (offset == 0x1800 && value) {
        if (getenv("A7R3_CMD_DEC")) { /* A7R3_CMD_DEC2 */
            static unsigned sub_n;
            if (sub_n < 400) {
                sub_n++;
                fprintf(stderr, "CMDSUB t=%lld n=%u addr=%08x len=%08x\n",
                        (long long)qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL),
                        sub_n, (unsigned)cxd90045_sugi_last_cmd,
                        (unsigned)value);
            }
        }
        cxd90045_sugi_capture_cmd(cxd90045_sugi_last_cmd);
    }

    if ((offset == 0x00c0 && (value & 0x20000000u)) ||
        (offset == 0x1800 && (value & 0x4u)) ||
        (offset == 0x0c18 && value == 1)) {
        /* Kick: complete immediately.  The driver reads BAR+0x34 and acks
         * bit30, bit26 and the low nibble.  The status register the handler
         * reads is fixed at block 0, so publish every completion there. */
        s->status[block] = 0xc0000001;
        s->pending[block] = 1;
        s->irq_status |= s->status[block];
        qemu_irq_raise(s->irq);
    } else if (cxd90045_sugi_ack_off(low)) {
        s->pending[block] = 0;
        s->irq_status = 0;
        qemu_irq_lower(s->irq);
    }

    if (cxd90045_sugi_trace() && s->trace_count < 4000) {
        uint32_t pc = 0, lr = 0;
        CPUState *cpu = current_cpu;
        if (cpu) {
            pc = ARM_CPU(cpu)->env.regs[15];
            lr = ARM_CPU(cpu)->env.regs[14];
        }
        fprintf(stderr, "A7R3-SUGI WR[%u] pc=%08x lr=%08x off=%04x val=%08x "
                "size=%u\n", s->trace_count, pc, lr, (unsigned)offset,
                (unsigned)value, size);
        s->trace_count++;
    }
}

static const MemoryRegionOps cxd90045_sugi_ops = {
    .read = cxd90045_sugi_read,
    .write = cxd90045_sugi_write,
    .valid.min_access_size = 4,
    .valid.max_access_size = 4,
    .endianness = DEVICE_NATIVE_ENDIAN,
};

static void cxd90045_sugi_reset(DeviceState *dev)
{
    CXD90045SugiState *s = CXD90045_SUGI(dev);
    memset(s->regs, 0, sizeof(s->regs));
    memset(s->status, 0, sizeof(s->status));
    memset(s->pending, 0, sizeof(s->pending));
    s->irq_status = 0;
}


/* --- optional periodic surface dump (A7R3_SURF_TICK=<ms>) --- */
typedef struct {
    QEMUTimer *timer;
    unsigned seq;
} CXD90045SugiTick;


/* Dump one configured surface (A7R3_DUMP_PA<n>) with the same tiling the panel
 * uses.  Kept separate from cxd90045_sugi_surface_dump() so several surfaces
 * can be captured per tick: the app alternates present targets and some views
 * only show up in one of them. */
static void cxd90045_sugi_dump_pa_env(const char *envname, unsigned idx)
{
    const char *dir = getenv("A7R3_SURF_DIR");
    const char *s = getenv(envname);
    static unsigned char buf[640 * 480 * 2];
    static unsigned char lin[640 * 480 * 2];
    char path[256];
    FILE *f;
    hwaddr pa;
    unsigned x, y;

    if (!dir || !s || !*s) {
        return;
    }
    pa = (hwaddr)strtoull(s, NULL, 0);
    if (!cxd90045_sugi_pa_ok(pa, sizeof(buf))) {
        return;
    }
    cpu_physical_memory_read(pa, buf, sizeof(buf));
    if (getenv("A7R3_SURF_MORTON")) {
        for (y = 0; y < 480; y++) {
            for (x = 0; x < 640; x++) {
                unsigned yy = 479u - y;
                unsigned z = (x & 1) | ((yy & 1) << 1) | ((x & 2) << 1) |
                             ((yy & 2) << 2) | ((x & 4) << 2) |
                             ((yy & 4) << 3);
                unsigned off = (((yy >> 3) * 80 + (x >> 3)) * 64 + z) * 2;
                lin[y * 1280 + x * 2] = buf[off];
                lin[y * 1280 + x * 2 + 1] = buf[off + 1];
            }
        }
        memcpy(buf, lin, sizeof(buf));
    }
    snprintf(path, sizeof(path), "%s/frame_%04u.raw", dir, idx);
    f = fopen(path, "wb");
    if (f) {
        fwrite(buf, 1, sizeof(buf), f);
        fclose(f);
    }
}

static void cxd90045_sugi_tick_cb(void *opaque)
{
    CXD90045SugiTick *t = opaque;
    const char *ms = getenv("A7R3_SURF_TICK");
    long period = ms ? strtol(ms, NULL, 0) : 0;

    if (period <= 0) {
        return;
    }
    {
        /* every configured surface, every tick (see cxd90045_sugi_dump_pa_env) */
        cxd90045_sugi_surface_dump(8000 + (t->seq % 500));
        cxd90045_sugi_dump_pa_env("A7R3_DUMP_PA2", 7000 + (t->seq % 500));
        cxd90045_sugi_dump_pa_env("A7R3_DUMP_PA3", 6000 + (t->seq % 500));
    }
    if (0) {
        const char *s2 = getenv("A7R3_DUMP_PA2");
        if (s2 && *s2 && (t->seq & 1)) {
            static unsigned char sub[640 * 480 * 2];
            static unsigned char sub_lin[640 * 480 * 2];
            hwaddr pa2 = (hwaddr)strtoull(s2, NULL, 0);
            const char *dir2 = getenv("A7R3_SURF_DIR");
            if (dir2 && cxd90045_sugi_pa_ok(pa2, sizeof(sub))) {
                char path2[256];
                FILE *f2;
                cpu_physical_memory_read(pa2, sub, sizeof(sub));
                if (getenv("A7R3_SURF_MORTON")) {
                    unsigned x, y;
                    for (y = 0; y < 480; y++) {
                        for (x = 0; x < 640; x++) {
                            unsigned yy = 479u - y;
                            unsigned z = (x & 1) | ((yy & 1) << 1) |
                                         ((x & 2) << 1) | ((yy & 2) << 2) |
                                         ((x & 4) << 2) | ((yy & 4) << 3);
                            unsigned off =
                                (((yy >> 3) * 80 + (x >> 3)) * 64 + z) * 2;
                            sub_lin[y * 1280 + x * 2] = sub[off];
                            sub_lin[y * 1280 + x * 2 + 1] = sub[off + 1];
                        }
                    }
                    memcpy(sub, sub_lin, sizeof(sub));
                }
                snprintf(path2, sizeof(path2), "%s/frame_%04u.raw", dir2,
                         7000 + (t->seq % 500));
                f2 = fopen(path2, "wb");
                if (f2) {
                    fwrite(sub, 1, sizeof(sub), f2);
                    fclose(f2);
                }
            }
        } else {
            cxd90045_sugi_surface_dump(8000 + (t->seq % 500));
        }
    }
    t->seq++;
    timer_mod(t->timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + period);
}

static CXD90045SugiTick *cxd90045_sugi_tick_start(void)
{
    const char *ms = getenv("A7R3_SURF_TICK");
    CXD90045SugiTick *t;
    long period = ms ? strtol(ms, NULL, 0) : 0;

    if (period <= 0) {
        return NULL;
    }
    t = g_new0(CXD90045SugiTick, 1);
    t->timer = timer_new_ms(QEMU_CLOCK_VIRTUAL, cxd90045_sugi_tick_cb, t);
    timer_mod(t->timer, qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + period);
    return t;
}

static void cxd90045_sugi_realize(DeviceState *dev, Error **errp)
{
    CXD90045SugiState *s = CXD90045_SUGI(dev);

    memory_region_init_io(&s->mmio, OBJECT(dev), &cxd90045_sugi_ops, s,
                          TYPE_CXD90045_SUGI, CXD90045_SUGI_SIZE);
    sysbus_init_mmio(SYS_BUS_DEVICE(dev), &s->mmio);
    sysbus_init_irq(SYS_BUS_DEVICE(dev), &s->irq);
    if (getenv("A7R3_SURF_TICK")) {
        cxd90045_sugi_tick_start();
    }
}

static void cxd90045_sugi_class_init(ObjectClass *klass, void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    dc->realize = cxd90045_sugi_realize;
    dc->reset = cxd90045_sugi_reset;
}

static const TypeInfo cxd90045_sugi_info = {
    .name = TYPE_CXD90045_SUGI,
    .parent = TYPE_SYS_BUS_DEVICE,
    .instance_size = sizeof(CXD90045SugiState),
    .class_init = cxd90045_sugi_class_init,
};

static void cxd90045_sugi_register_types(void)
{
    type_register_static(&cxd90045_sugi_info);
}

type_init(cxd90045_sugi_register_types)

/* PCSAMPLE: periodic guest-PC sampling for hotspot analysis. */
static QEMUTimer *cxd_pc_timer;
static void cxd90045_pc_sample(void *opaque)
{
    QEMUTimer *t = cxd_pc_timer;
    CPUState *cs;
    (void)opaque;
    CPU_FOREACH(cs) {
        CPUARMState *env = &ARM_CPU(cs)->env;
        fprintf(stderr, "PCSAMPLE cpu=%d pc=%08x lr=%08x\n", cs->cpu_index,
                (unsigned)env->regs[15], (unsigned)env->regs[14]);
    }
    timer_mod(t, qemu_clock_get_ns(QEMU_CLOCK_REALTIME) + 5000000LL);
}

static void cxd90045_init(MachineState *machine)
{
    DriveInfo *dinfo;
    BlockBackend *drive;
    MemoryRegion *mem;
    DeviceState *dev;
    Object *cpu;
    BusState *bus;
    qemu_irq irq[CXD90045_NUM_IRQ - CXD90045_IRQ_OFFSET];
    CXD90045LiroState *liro_state;
    int i;

    dinfo = drive_get(IF_MTD, 0, 0);
    drive = dinfo ? blk_by_legacy_dinfo(dinfo) : NULL;

    cpu = object_new(machine->cpu_type);
    object_property_set_bool(cpu, "has_el3", false, &error_fatal);
    object_property_set_bool(cpu, "reset-hivecs", true, &error_fatal);
    qdev_realize(DEVICE(cpu), NULL, &error_fatal);

    /* Optional LIRO co-processor: run av-cam.bin on a second ARM core. */
    {
        const char *liro_file = getenv("A7R3_LIRO_FILE");
        if (liro_file) {
            MemoryRegion *lmem = g_new(MemoryRegion, 1);
            MemoryRegion *lcont = g_new(MemoryRegion, 1);
            MemoryRegion *lalias = g_new(MemoryRegion, 1);
            MemoryRegion *llow = g_new(MemoryRegion, 1);
            MemoryRegion *lvec = g_new(MemoryRegion, 1);
            Object *lcpu = object_new(machine->cpu_type);
            CPUState *lcs;
            memory_region_init(lcont, NULL, "liro-as", UINT64_MAX);
            memory_region_init_alias(lalias, NULL, "liro-sys",
                                     get_system_memory(), 0, UINT64_MAX);
            memory_region_add_subregion(lcont, 0, lalias);
            memory_region_init_ram(llow, NULL, "liro-low",
                                   0x00200000, &error_fatal);
            {
                uint8_t *lp = memory_region_get_ram_ptr(llow);
                if (lp) {
                    lp[0xe800] = 0x70;   /* thumb: bx lr */
                    lp[0xe801] = 0x47;
                }
            }
            memory_region_add_subregion_overlap(lcont, 0x00000000, llow, 1);
            memory_region_init_ram(lvec, NULL, "liro-vec",
                                   0x00001000, &error_fatal);
            memory_region_add_subregion_overlap(lcont, 0xffff0000, lvec, 1);
            object_property_set_link(lcpu, "memory", OBJECT(lcont),
                                     &error_fatal);
            memory_region_init_ram(lmem, NULL, "liro-code",
                                   0x02000000, &error_fatal);
            memory_region_add_subregion(get_system_memory(),
                                        0x63000000, lmem);
            load_image_targphys(liro_file, 0x635c6000, 0x02000000);
            object_property_set_bool(lcpu, "reset-hivecs", true,
                                     &error_fatal);
            qdev_realize(DEVICE(lcpu), NULL, &error_fatal);
            lcs = CPU(lcpu);
            cpu_reset(lcs);
            ARM_CPU(lcpu)->env.regs[15] = 0x635c6044;
            ARM_CPU(lcpu)->env.thumb = 0;
            /* The mask ROM normally enables VFP before the firmware entry;
             * without it the first vpush/vldr raises an undefined
             * instruction exception.  Grant CP10/CP11 full access. */
            ARM_CPU(lcpu)->env.cp15.cpacr_el1 = 0x00f00000;
            ARM_CPU(lcpu)->env.cp15.cpacr_el1 |= 0x00f00000;
            ARM_CPU(lcpu)->env.vfp.xregs[8] = 0x40000000;
            arm_rebuild_hflags(&ARM_CPU(lcpu)->env);
            lcs->halted = 0;
            fprintf(stderr, "A7R3-LIRO: loaded %s at 0x635c6000, "
                    "pc=0x635c6044 (arm)\n", liro_file);
        }
    }

    mem = g_new(MemoryRegion, 1);
    memory_region_init_ram(mem, NULL, "ddr0", CXD90045_DDR0_SIZE, &error_fatal);
    memory_region_add_subregion(get_system_memory(), CXD90045_DDR0_BASE, mem);

    mem = g_new(MemoryRegion, 1);
    memory_region_init_ram(mem, NULL, "ddr1", CXD90045_DDR1_SIZE, &error_fatal);
    memory_region_add_subregion(get_system_memory(), CXD90045_DDR1_BASE, mem);

    liro_state = g_new0(CXD90045LiroState, 1);
    mem = g_new(MemoryRegion, 1);
    memory_region_init_io(mem, NULL, &cxd90045_liro_ops, liro_state,
                          "liro-regs", CXD90045_LIRO_SIZE);
    memory_region_add_subregion(get_system_memory(), CXD90045_LIRO_BASE, mem);

    mem = g_new(MemoryRegion, 1);
    memory_region_init_ram(mem, NULL, "sram", CXD90045_SRAM_SIZE, &error_fatal);
    memory_region_add_subregion(get_system_memory(), CXD90045_SRAM_BASE, mem);

    mem = g_new(MemoryRegion, 1);
    /* The Linux kernel uses high vectors at 0xffff0000 and copies its
     * vector page there, so this must be writable RAM after reset. */
    memory_region_init_ram(mem, NULL, "vectors", CXD90045_BOOTROM_SIZE, &error_fatal);
    memory_region_add_subregion(get_system_memory(), CXD90045_BOOTROM_BASE, mem);

    dev = qdev_new(TYPE_A9MPCORE_PRIV);
    qdev_prop_set_uint32(dev, "num-cpu", 1);
    qdev_prop_set_uint32(dev, "num-irq", CXD90045_NUM_IRQ);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD90045_MPCORE_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, qdev_get_gpio_in(DEVICE(cpu), ARM_CPU_IRQ));
    for (i = 0; i < CXD90045_NUM_IRQ - CXD90045_IRQ_OFFSET; i++) {
        irq[i] = qdev_get_gpio_in(dev, i);
    }

    dev = qdev_new(TYPE_CXD90045_SUGI);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD90045_SUGI_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0,
                       irq[CXD90045_IRQ_SUGI - CXD90045_IRQ_OFFSET]);

    for (i = 0; i < CXD90045_NUM_GPIO; i++) {
        dev = qdev_new("bionz_gpio");
        qdev_prop_set_uint8(dev, "version", 3);
        dev->id = g_strdup_printf("gpio%d", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD90045_GPIO_BASE(i));
    }

    dev = qdev_new("bionz_bootcon");
    /* Local port: serial0 is reserved for the PL011 UART used as Linux console. */
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD90045_BOOTCON_BASE);

    dev = qdev_new(TYPE_SYSBUS_SDHCI);
    qdev_prop_set_uint8(dev, "uhs", UHS_II);
    sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
    sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD90045_SDHCI_BASE);
    sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD90045_IRQ_SDHCI - CXD90045_IRQ_OFFSET]);
    bus = qdev_get_child_bus(dev, "sd-bus");

    dev = qdev_new(TYPE_SD_CARD);
    qdev_prop_set_bit(dev, "emmc", true);
    qdev_prop_set_bit(dev, "high_capacity", true);
    qdev_prop_set_uint32(dev, "boot_size", 0x40000);
    qdev_prop_set_drive(dev, "drive", drive);
    qdev_realize_and_unref(dev, bus, &error_fatal);

    for (i = 0; i < CXD90045_NUM_UART; i++) {
        pl011_create(CXD90045_UART_BASE(i), irq[CXD90045_IRQ_UART(i) - CXD90045_IRQ_OFFSET], serial_hd(i));
    }

    for (i = 0; i < CXD90045_NUM_HWTIMER; i++) {
        dev = qdev_new("bionz_hwtimer");
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD90045_HWTIMER_BASE(i));
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD90045_IRQ_HWTIMER(i) - CXD90045_IRQ_OFFSET]);
    }

    for (i = 0; i < CXD90045_NUM_SIO; i++) {
        dev = qdev_new("bionz_sio");
        dev->id = g_strdup_printf("sio%d", i);
        sysbus_realize_and_unref(SYS_BUS_DEVICE(dev), &error_fatal);
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 0, CXD90045_SIO_BASE(i));
        sysbus_mmio_map(SYS_BUS_DEVICE(dev), 1, CXD90045_SIO_BASE(i) + 0x100);
        sysbus_connect_irq(SYS_BUS_DEVICE(dev), 0, irq[CXD90045_IRQ_SIO(i) - CXD90045_IRQ_OFFSET]);
    }

    if (machine->kernel_filename) {
        ssize_t initrd_size;
        /*
         * Sony's kernel expects the bootloader (EXBL) to have populated the
         * initial page tables, including the page holding swapper_pg_dir.
         * Without a bootloader, setup_arch faults reading the ATAGS.
         * The kernel is linked for PAGE_OFFSET 0x60000000 / PHYS_OFFSET 0;
         * before setup_arch reads the ATAGS it does __va(0x00010000), which
         * is VA 0x60010000.  Prepopulate that one 1 MiB section (the head
         * code maps the kernel image sections itself).
         */
        {
            uint32_t pgd[4096];
            memset(pgd, 0, sizeof(pgd));
            pgd[0x600] = (CXD90045_DDR0_BASE & 0xfff00000) | 0x00011c0e;
            uint32_t readback = 0;
            address_space_write(&address_space_memory,
                                CXD90045_DDR0_BASE + 0x00104000,
                                MEMTXATTRS_UNSPECIFIED, pgd, sizeof(pgd));
            address_space_read(&address_space_memory,
                               CXD90045_DDR0_BASE + 0x00105800,
                               MEMTXATTRS_UNSPECIFIED, &readback, 4);
            fprintf(stderr, "A7M3: wrote initial pgd at 0x%08x "
                    "(pgd[0x600]=0x%08x readback=0x%08x)\n",
                    (unsigned)(CXD90045_DDR0_BASE + 0x00104000),
                    (unsigned)pgd[0x600], (unsigned)readback);
        }
        load_image_targphys(machine->kernel_filename, CXD90045_DDR0_BASE + CXD90045_TEXT_OFFSET, CXD90045_DDR0_SIZE - CXD90045_TEXT_OFFSET);
        initrd_size = load_image_targphys(machine->initrd_filename, CXD90045_DDR0_BASE + CXD90045_INITRD_OFFSET, CXD90045_DDR0_SIZE - CXD90045_INITRD_OFFSET);
        cxd90045_write_atags(CXD90045_ATAGS_ADDR,
                             CXD90045_DDR0_BASE + CXD90045_INITRD_OFFSET,
                             initrd_size, CXD90045_CMDLINE);
        cxd_write_bootloader_with_machine(CXD90045_BOOTROM_BASE,
                                          CXD90045_DDR0_BASE + CXD90045_TEXT_OFFSET,
                                          0x11a, CXD90045_ATAGS_ADDR);
    } else if (bios_name) {
        load_image_targphys(bios_name, CXD90045_BOOTROM_BASE, CXD90045_BOOTROM_SIZE);
    } else if (drive) {
        cxd_write_bootloader(CXD90045_BOOTROM_BASE, cxd90045_init_loader2(drive));
    }

    cxd_add_const_reg("miscctrl_mode", CXD90045_MISCCTRL_BASE(1), 0x28);

    cxd_add_const_reg("emmc0", CXD90045_SDHCI_BASE + 0x124, 0x1000000);
    cxd_add_const_reg("emmc1", CXD90045_SDHCI_BASE + 0x130, 0x1fff);

    cxd_add_const_reg("ddrc0", CXD90045_DDRC_BASE(0) + 0x148, 0x20000000);
    cxd_add_const_reg("ddrc1", CXD90045_DDRC_BASE(1) + 0x148, 0x20000000);

    cxd_add_const_reg("unknown0", 0xf2908008, 1);
    cxd_add_const_reg("unknown1", 0xf290c008, 1);

    if (getenv("A7R3_PCSAMPLE")) {
        cxd_pc_timer = timer_new_ns(QEMU_CLOCK_REALTIME,
                                    cxd90045_pc_sample, NULL);
        timer_mod(cxd_pc_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_REALTIME) + 5000000LL);
    }
}

static void cxd4108_machine_init(MachineClass *mc)
{
    mc->desc = "Sony BIONZ CXD4108";
    mc->init = cxd4108_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm926");
    mc->max_cpus = 2;
    mc->default_cpus = 2;
    mc->ignore_memory_transaction_failures = true;
}

DEFINE_MACHINE("cxd4108", cxd4108_machine_init)

static void cxd4115_machine_init(MachineClass *mc)
{
    mc->desc = "Sony BIONZ CXD4115";
    mc->init = cxd4115_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm11mpcore");
    mc->ignore_memory_transaction_failures = true;
}

DEFINE_MACHINE("cxd4115", cxd4115_machine_init)

static void cxd4132_machine_init(MachineClass *mc)
{
    mc->desc = "Sony BIONZ CXD4132";
    mc->init = cxd4132_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("arm11mpcore");
    mc->ignore_memory_transaction_failures = true;
}

DEFINE_MACHINE("cxd4132", cxd4132_machine_init)

static void cxd90014_machine_init(MachineClass *mc)
{
    mc->desc = "Sony BIONZ CXD90014";
    mc->init = cxd90014_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a5");
    mc->max_cpus = 2;
    mc->default_cpus = 2;// main + boss
    mc->ignore_memory_transaction_failures = true;
}

DEFINE_MACHINE("cxd90014", cxd90014_machine_init)

static void cxd90045_machine_init(MachineClass *mc)
{
    mc->desc = "Sony BIONZ CXD90045";
    mc->init = cxd90045_init;
    mc->default_cpu_type = ARM_CPU_TYPE_NAME("cortex-a5");
    mc->ignore_memory_transaction_failures = true;
    mc->max_cpus = 2;
}

DEFINE_MACHINE("cxd90045", cxd90045_machine_init)
