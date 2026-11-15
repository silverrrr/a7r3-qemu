#!/bin/bash
# 一条命令看菜单：起 QEMU + 网页实时画面 + 按键面板
#
#   bash see-menu.sh [port]
#
# 打开浏览器访问 http://localhost:<port>/  （默认 8765）
#   - 画面 = 固件 present 缓冲（0x23B16000），也就是"屏幕上"的样子
#   - 「新菜单叠层」那排按钮直接驱动 /tmp/nm_key（选择/应用/切分类）
#   - 上面那排按钮走 guest 的 /usr/tool/inject-key（原生按键）
#   - 点画面 = 原生触摸映射（切页/移动选择/ENTER）
#
# 需要：payload 已装进镜像（install-newmenu.sh），/setting/{newmenu,nm_flip} 存在。
# 注意：guest /tmp 只有 12 MB，长期跑请偶尔清理 nm_key / hook.log。
set -u
PORT="${1:-8765}"
cd "$(dirname "$0")" || exit 1

# Repeated hard kills leave the guest's /tmp FAT dirty; the kernel then mounts it
# read-only and the overlay's /tmp channel silently stops working.  Repair it
# before every run (QEMU must not be running).
if [ -f fsck-guest-tmp.sh ]; then
    if ! bash fsck-guest-tmp.sh check >/dev/null 2>&1; then
        echo "[*] guest /tmp dirty, repairing"
        bash fsck-guest-tmp.sh fix >/dev/null 2>&1 || true
    fi
fi

# Dump three surfaces: the app's render target and both present buffers.  Some
# views (caution dialogs) only show up in one of them; live-menu.py picks the
# newest non-blank recent frame, so the web view no longer goes black on those.
exec sleep 3600 | \
    A7R3_SURF_TICK=300 \
    A7R3_DUMP_PA=0x202e0200 \
    A7R3_DUMP_PA2=0x23a80000 \
    A7R3_DUMP_PA3=0x23b16000 \
    python3 live-menu.py --port "$PORT" --duration 3600
