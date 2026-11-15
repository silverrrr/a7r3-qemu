#!/bin/bash
# 用只读分区 dump 拼出 QEMU（cxd90045）能启动的 eMMC 镜像。
#
#   bash make-emmc-image.sh [-d DUMPS] [-o OUT] [--layout FILE] [--verify]
#
# 产物 = 0x80000 的 boot 区（boot0 + boot1）+ 2 GiB 用户区，MBR 在 0x80000。
# guest 内核是从用户区的 MBR 建出 /dev/nflashaN 的，所以必须先把 MBR 写进
# 镜像，再把每个分区放到 MBR 里它自己的起始 LBA 上；只 truncate + dd 是不行的。
#
# 默认布局是 A7R3（ILCE-7RM3，CXD90045）：
#   - 25 个 DOS 分区，编号 = nflasha 号（p4 是扩展分区容器，p5..p25 是逻辑分区）；
#   - 大小取自固件包里的 0300_partconf/partinf.conf；
#   - 真机上 size=0 的分区（8/9/14/19..25）在镜像里给 8 MiB 或 4 MiB 占位，
#     这样 nflasha 编号不会因为少一个分区而整体前移；
#   - p4 在镜像里是扩展分区容器，所以 nflasha4（12 MiB MPR 备份）没有位置，
#     它的 dump 会被忽略；启动不需要它的内容，只需要这个设备存在且可写。
#
# 其他机型：从你的机器上取 start/size（/proc/partitions、
# /sys/block/mmcblk0/mmcblk0pN/{start,size}，或 partinf.conf），
# 写成 --layout 文件（每行：编号 起始LBA 扇区数 [类型]），例如：
#   1 2048 16256 0x0b
#   2 18432 40960 0x0b
#   3 59392 98304 0x0b
#   4 157696 3000000 0x05
#   5 159744 122880 0x83
#
# 依赖：bash、sfdisk、dd、truncate、stat、od、cmp（Linux / WSL）。
set -euo pipefail

BOOT_OFF=524288                 # 0x80000：两个 256 KiB boot 区
USER_SECTORS=4194304            # 2 GiB 用户区（512 B 扇区）
DUMPS=./dumps
OUT=./qemu-emmc-2g.img
LAYOUT=
VERIFY=0

# 默认表（A7R3）：nflashaN -> "扇区数 类型"。
# 1..3 是主分区，p4 是扩展分区容器；5 起是逻辑分区。
SIZES=(
  "" "16256 0x0b" "40960 0x0b" "98304 0x0b" "3000000 0x05"
  "122880 0x83" "32768 0x83" "16384 0x83" "16384 0x83" "16384 0x83"
  "24576 0x0b" "1024000 0x0b" "81920 0x0b" "8192 0x83" "8192 0x83"
  "614400 0x83" "286720 0x83" "204800 0x83" "40960 0x0b"
  "8192 0x83" "8192 0x83" "8192 0x83" "8192 0x83" "8192 0x83"
  "8192 0x83" "8192 0x83"
)

# 真机上确实存在的分区（没有 dump 时提示“占位”而不是“设备上不存在”）。
# 4 不在这里：镜像里 p4 是扩展分区容器，nflasha4 的 MPR 备份没有位置放。
REAL=(1 2 3 5 6 7 10 11 12 13 15 16 17 18 23)

usage() {
  cat <<'EOF'
用只读分区 dump 拼出 QEMU 用的 eMMC 镜像。

  bash make-emmc-image.sh [-d DUMPS] [-o OUT] [--layout FILE] [--verify]

  -d DUMPS        nflashaN.bin 所在目录（默认 ./dumps）
  -o OUT          输出镜像（默认 ./qemu-emmc-2g.img）
  --layout FILE   自定义分区表：每行 "编号 起始LBA 扇区数 [类型]"
                  （编号必须和 guest 期望的一致；不要删中间的分区）
  --verify        组装后逐分区比对前 4 KiB
  -h              显示本帮助
EOF
  exit "${1:-1}"
}

while [ $# -gt 0 ]; do
  case "$1" in
    -d) DUMPS="$2"; shift 2 ;;
    -o) OUT="$2"; shift 2 ;;
    --layout) LAYOUT="$2"; shift 2 ;;
    --verify) VERIFY=1; shift ;;
    -h|--help) usage 0 ;;
    *) echo "unknown argument: $1" >&2; usage 1 ;;
  esac
done

for tool in sfdisk dd truncate stat od cmp; do
  command -v "$tool" >/dev/null 2>&1 || { echo "missing tool: $tool" >&2; exit 1; }
done
[ -d "$DUMPS" ] || { echo "dump directory not found: $DUMPS" >&2; exit 1; }

TMP_USER=$(mktemp)
TMP_MBR=$(mktemp)
TMP_INPUT=$(mktemp)
trap 'rm -f "$TMP_USER" "$TMP_MBR" "$TMP_INPUT"' EXIT

truncate -s $((USER_SECTORS * 512)) "$TMP_USER"

# ---- 1) 写 MBR ----------------------------------------------------------
if [ -n "$LAYOUT" ]; then
  [ -f "$LAYOUT" ] || { echo "layout file not found: $LAYOUT" >&2; exit 1; }
  {
    echo "label: dos"
    echo "unit: sectors"
    echo
    while read -r n start size type rest; do
      case "$n" in ''|'#'*) continue ;; esac
      [ -n "${type:-}" ] || type=0x83
      echo "$n : start=$start, size=$size, type=$type"
    done < "$LAYOUT"
  } > "$TMP_INPUT"
else
  {
    echo "label: dos"
    echo "unit: sectors"
    echo
    for n in $(seq 1 $(( ${#SIZES[@]} - 1 ))); do
      read -r size type <<< "${SIZES[$n]}"
      echo "$n : size=$size, type=$type"
    done
  } > "$TMP_INPUT"
fi
sfdisk "$TMP_USER" < "$TMP_INPUT" >/dev/null

sfdisk -d "$TMP_USER" > "$TMP_MBR"

# ---- 2) 从 MBR 里读回 start/size（唯一事实来源）-------------------------
declare -a START SIZE TYPE
INDICES=
while read -r line; do
  case "$line" in *" : start="*) ;; *) continue ;; esac
  n=$(printf '%s\n' "$line" | sed -n 's/^.*[^0-9]\([0-9][0-9]*\) : start=.*/\1/p')
  s=$(printf '%s\n' "$line" | sed -n 's/.*start=[[:space:]]*\([0-9][0-9]*\).*/\1/p')
  z=$(printf '%s\n' "$line" | sed -n 's/.*size=[[:space:]]*\([0-9][0-9]*\).*/\1/p')
  t=$(printf '%s\n' "$line" | sed -n 's/.*type=\([0-9a-fA-F][0-9a-fA-F]*\).*/\1/p')
  [ -n "$n" ] && [ -n "$s" ] && [ -n "$z" ] || continue
  START[$n]=$s
  SIZE[$n]=$z
  TYPE[$n]=${t:-83}
  INDICES="$INDICES $n"
done < "$TMP_MBR"

[ -n "$INDICES" ] || { echo "could not parse the partition table back" >&2; exit 1; }

# ---- 3) 组装最终镜像 ----------------------------------------------------
truncate -s $((BOOT_OFF + USER_SECTORS * 512)) "$OUT"
dd if="$TMP_USER" of="$OUT" bs=1M iflag=fullblock oflag=seek_bytes \
   seek=$BOOT_OFF conv=sparse,notrunc status=none

echo "== nflasha  ->  image offset (0x80000 + lba*512)"
for n in $(printf '%s\n' $INDICES | sort -n); do
  f="$DUMPS/nflasha$n.bin"
  off=$((BOOT_OFF + START[$n] * 512))
  if [ "${TYPE[$n]}" = "5" ] || [ "${TYPE[$n]}" = "05" ]; then
    if [ -f "$f" ]; then
      echo "  nflasha$n: dump ignored (this slot is the extended container)" >&2
    fi
    printf '  nflasha%-2s lba=%-8s size=%-9s off=0x%08x  extended container (MPR dump not writable here)\n' \
      "$n" "${START[$n]}" "$((SIZE[$n] * 512))" "$off"
    continue
  fi
  if [ -f "$f" ]; then
    isz=$(stat -c %s "$f")
    psz=$((SIZE[$n] * 512))
    if [ "$isz" -gt "$psz" ]; then
      echo "  nflasha$n: dump $isz B > partition $psz B" >&2
      exit 1
    fi
    if [ $((isz % 512)) -ne 0 ]; then
      echo "  nflasha$n: dump size $isz is not a multiple of 512" >&2
      exit 1
    fi
    if [ "$isz" -ne "$psz" ]; then
      echo "  nflasha$n: WARNING dump $isz B, partition $psz B (short dump?)" >&2
    fi
    dd if="$f" of="$OUT" bs=1M iflag=fullblock oflag=seek_bytes \
       seek=$off conv=sparse,notrunc status=none
    printf '  nflasha%-2s lba=%-8s size=%-9s off=0x%08x  %s\n' \
      "$n" "${START[$n]}" "$psz" "$off" "$(basename "$f")"
  else
    if [ -n "$LAYOUT" ]; then
      tag="placeholder (no dump)"
    else
      case " ${REAL[*]} " in
        *" $n "*) tag="placeholder (missing dump)" ;;
        *)        tag="placeholder (absent on the real device)" ;;
      esac
    fi
    printf '  nflasha%-2s lba=%-8s size=%-9s off=0x%08x  %s\n' \
      "$n" "${START[$n]}" "$((SIZE[$n] * 512))" "$off" "$tag"
  fi
done

# ---- 4) 校验 ------------------------------------------------------------
sig=$(dd if="$OUT" bs=1 skip=$((BOOT_OFF + 510)) count=2 status=none | od -An -tx1 | tr -d ' \n')
if [ "$sig" != "55aa" ]; then
  echo "bad MBR signature at 0x$((BOOT_OFF + 510)): $sig" >&2
  exit 1
fi

if [ "$VERIFY" -eq 1 ]; then
  echo "== verify first 4 KiB of each dumped partition"
  for n in $(printf '%s\n' $INDICES | sort -n); do
    f="$DUMPS/nflasha$n.bin"
    [ -f "$f" ] || continue
    if [ "${TYPE[$n]}" = "5" ] || [ "${TYPE[$n]}" = "05" ]; then
      continue
    fi
    off=$((BOOT_OFF + START[$n] * 512))
    if cmp -s -n 4096 -i "$off":0 "$OUT" "$f"; then
      echo "  nflasha$n ok"
    else
      echo "  nflasha$n MISMATCH at 0x$(printf '%x' "$off")" >&2
      exit 1
    fi
  done
fi

echo "final: $OUT $(stat -c %s "$OUT") bytes"
echo "MBR at 0x80000; mount offset = 524288 + lba*512"
