# 工作总结与踩坑记录

> 这是仓库里最值得先读的一篇：做了什么、怎么验证的、哪些地方会浪费你几个小时。
> 完整的技术细节在 [A7R3_NOTES.md](A7R3_NOTES.md)（5000+ 行，按 `§编号` 索引）。

---

## 1. 目标

A7R3（ILCE-7RM3，CXD90045）的原生菜单是按键式的老式列表。目标是：

1. 在 QEMU 里跑起原厂固件（没有真机也能开发）；
2. 把一套 A7C2 风格的**新菜单叠在原生 UI 之上**；
3. 叠层**不被原生重绘打断**；
4. 新 UI **真的能改设置**；
5. **支持触摸**。

全部在 QEMU + 自备固件中完成；真机只做了只读验证。

## 2. 环境与方法

- 自研 QEMU 机器模型 `cxd90045`（`qemu/bionz.c`）：eMMC、DDR、UART、DDRC、
  Sugilite 2D 图形引擎、LIRO 显示引擎。
- guest 侧用 `LD_PRELOAD` 注入 `libefence.so`（shim），做观测/写屏/按键。
- 分析工具链：Python + `llvm-objdump`（ARM/Thumb 反汇编）、Pillow（帧/字体）。
- 真机（可选的只读验证）：service mode + `/dev/mem` 读取。

## 3. 里程碑（按时间顺序）

| # | 阶段 | 结论 |
| --- | --- | --- |
| 1 | 固件启动 | QEMU 能跑起 A7R3 固件并显示原生 UI；Sugilite 2D 命令流可解析 |
| 2 | 按键注入 | 走 OSAL 消息队列，`inject-key` 可驱动原生 UI |
| 3 | 菜单显示 | 原生菜单能打开、能翻页；白色大块是模型对 uniform 的误判，已修 |
| 4 | 实时观看 | 模型定时 dump panel 成帧，可在网页里实时看 |
| 5 | 性能 | 关掉 ulogio 日志洪泛后 CPU −48%、上下文切换 −93% |
| 6 | 设置树 | `CmnViewSettingNode` 框架与 ZV-E10 同源：456 个 root、属性表可读 |
| 7 | 叠层 | 自绘 640×480 菜单，写 panel + present 双缓冲 |
| 8 | 命令流解码 | 提交值只是门铃序号；真正 surface 在命令表与 blit 寄存器里 |
| 9 | 真只读探测 | 真机 UI 命令表与 QEMU 同构；无 fbdev；service 模式下缓冲为空 |
| 10 | 字体/字符串 | 从 `.ltt` 抽出原生 TrueType；resid→字符串表破解 |
| 11 | 真正生效 | `setItemSelectedBySRNum` 改设置，10 秒后仍保持 |
| 12 | 触摸 | 轮询 libObj 里的最后触摸点，点选/双击应用全链路打通 |
| 13 | 二级页 | 点已选中的行打开选项弹窗（固件 SR 表 → 单选列表），点选即改值 |
| 14 | 切换键 | 新旧菜单同一个右上角双箭头按钮；关闭时作为小补丁画在原生画面上 |
| 15 | 交还屏幕 | 打开前抓原生帧，关闭时回写；解决"收手后原生 App 不重绘" |
| 16 | MENU=返回 | 输入函数打补丁会崩（Exception shell），改用"原生 present"判定 |

## 4. 关键技术发现

### 4.1 显示通路

```
App 绘制 → Sugilite 2D 命令表（(op<<16)|0xf000，0x0112=目标面）
        → 渲染目标 0x202E0200（RGBA4444，8×8 Morton 瓦片，行翻转）
        → present：blit 0x202E0200 → 0x23A80000 / 0x23B16000（交替双缓冲）
```

- 面板缓冲的打包公式（Morton + 行翻转）见笔记 §44。
- 两块 flip 缓冲与渲染目标都落在固件 cmdline 的保留区
  `wbimem=0x3916000@0x20200000`（区间末尾正好是 flip-A）。
- **写渲染目标抢不过 App 的持续重绘；写 flip 缓冲只在 present 时被覆盖**，
  所以叠层应该写 flip（本项目两者都写）。

### 4.2 提交/门铃

`ioctl(0xC0048211)` 的参数只是一个 **4 字节 in/out 槽**（值 1/2，驱动消费后清零），
不是命令缓冲指针。命令在线程栈/堆里的表面描述符与 blit 寄存器里。

### 4.3 设置树

- 节点 40 字节：`{vptr, children, count, properties, parent, depth, selected,
  selectable, physical, state}`；属性表记录 12 字节 `{key, kind, value}`。
- **`count` 是记录条数，不是最后下标**（ZV-E10 的代码在这里会多读一条）。
- 标题是属性 key=0 的 **resid**（`0x02xxxxxx`）；选项按 **SR 号**编号，
  `selected` 存的就是当前 SR；选项数组 `children[0]` 是占位，1..count 才是选项。
- 真正改设置：`setItemSelectedBySRNum(SR)`（导出的固件 API，走它自己的
  “选中该项”虚函数），实测改完 10 s 仍保持。

### 4.4 字体与字符串

- `/usr/share/app/fontlist.dat` 指向 `/tmp/images/MYingHeiC-GB18030-SJ_wIcon.ltt`；
  `.ltt` 是容器：ASCII 头 + `embeddedoffset/size` 指向一份**完整 TrueType**。
- 字符串：`resid 0x02xxxxxx → 0x1E|low24`，在 `string_simplifiedchinese.uxc` 里；
  记录 = `[u32 id][UTF-8][NUL][4 字节对齐]`。
- 部分文案带私有区图标前缀（U+E08C…），字体里是 .notdef，原生菜单也不画图标 ⇒ 剥掉。

### 4.5 触摸

`InputService::callback_tp` 把坐标 `strd` 进一个 libObj 全局：
`libObj + 0x0129eb20`（经 GOT slot `0x0121d024`，R_ARM_RELATIVE addend `0x0129eb20`），
`[0]=x [4]=y`。轮询它即可，**不需要打代码补丁**。

### 4.6 二级页面与"交还屏幕"

- 二级页不是手写表：`count` 是档数，第 i 档的 SR 用 `getSRNumByIndex(node,i,&sr)`，
  文案在 `children[i-1]`（**0 基**，`selected` 是 SR，两者差一，这里出过一次
  off-by-one：点"精细"列表显示"标准"）。
- 模态弹窗的遮罩不用逐像素混合（除法太慢），直接 `(v>>1)&0x7777|0xf` 减半亮度。
- **原生 App 只在"自己有事"时 present**：叠层停止绘制不会让原生画面回来，
  所以打开前要把 `0x202E0200` 的 640×480 抓下来，关闭时回写（+ 右上角小按钮）。
- 想抓物理 MENU 键不要去 patch 输入函数（热路径 8 字节补丁会撕裂，guest 直接
  掉进 Exception shell）；改用"叠层开着时 App 又 present 了"这个只读信号当返回。
- **完整菜单不是手写表**：`CautionConfig.so` 导出 456 个 `cmnViewSettingNodeRoot*`，
  运行时解析 ELF 动态符号表枚举出来，节点地址用 `dlsym(name)` 拿（不要自己算
  `st_value`，偶发垃圾指针会把 App 打崩），再按前缀分 8 个页签、每页 6 行分页。
  GNU hash 的符号数必须走 chain 到链尾，用 `nbuckets+symoffset` 估会少一半。
- 原生菜单是**按键式**的（触摸只管对焦），网页点击要由宿主翻译成按键；
  否则点了旧菜单毫无反应。
- **"画面上没有"不等于"App 没画"**：App 是双缓冲 present，某些视图只画在其中
  一块缓冲上；只 dump 一个固定地址就会看到"黑屏"。现在每 tick dump 三块，宿主挑
  最新的非全黑帧，警告对话框就正常了。
- 反复 `pkill` QEMU 会让 guest 的 `/tmp` FAT 变脏、开机只读挂载，依赖 `/tmp` 的
  控制通道（按键文件）会静默失效 —— 先 `touch /tmp/x` 试一下。

## 5. 踩过的坑（重点）

### 5.1 QEMU / 模型

- **启动竞态**：guest 约 1/3 概率在启动早期 page translation fault；重试即可，
  不是你的改动导致的。跑脚本时务必带重试。
- 模型 dump 帧时要按 guest 的瓦片布局还原（Morton + 行翻转），
  否则看到的是 8 列竖条。
- 面板内容会被 App 重绘覆盖：模型只在 blit 时 dump 会永远拍到原生画面，
  要加定时 dump（或叠层写 flip 后 dump）。
- 只模拟 2D 是不够的：文字/图标走的是纹理，需要按命令表里的纹理指针采样。

### 5.2 guest 逆向

- `/proc/self/maps` 一次 `read()` 只返回一页，必须循环读到 EOF；
  解析循环里 `continue` 前一定要 `p++`，否则死循环（表现为 UI 卡死）。
- `dlopen("/usr/lib/xxx.so", RTLD_LAZY)` 对**已加载**的库要加 `RTLD_NOLOAD`（值 5），
  否则可能返回 0。
- 函数地址是 **Thumb** 地址（最低位为 1）：`dlsym` 返回值直接用会跳飞；
  跳转/调用前要 `& ~1`，而 trampoline 返回时必须**置 1**，否则 CPU 切 ARM 态。
- 不要 inline hook 正在被多线程执行的函数：8 字节补丁可能被撕裂，直接崩。
  能用轮询/官方 API 就别打补丁（本项目的触摸就是轮询）。
- 导出符号的返回值约定不统一：`CmnViewSettingNode` 的 getter 用 **1=成功**，
  setter 用 `cbz r0`（0=成功）。别凭感觉写 `if (rc)`。
- `.so` 的 `.data.rel.ro`（vtable）在文件里是 0，指针在 `R_ARM_RELATIVE` 重定位里；
  静态看 vtable 要读重定位，不能直接读字节。
- `count` 的含义、`children[0]` 是占位、`state` ≠ SR 号 —— 这三件事各坑过一次。
  越界读 `children[count]` 会拿到垃圾指针，表现为 `page translation fault at 0x…`。

### 5.3 payload 构建

- freestanding ARM 构建：`clang --target=arm-linux-gnueabi -mfloat-abi=soft
  -fuse-ld=lld -nostdlib -ffreestanding`，不要链 libc。
- `-O2` 会把日志字符串字面量优化掉（inline 存立即数）⇒ 诊断版本要用 `-O1/-O0`，
  或给日志函数加 `noinline`。
- 写入 `/dev/mem` 前注意目标页对齐；写完要 `munmap`，不要长期持有映射。
- 负载里读文件用裸系统调用（`open/read/mmap2`），不要依赖 libc。

### 5.4 资源解码

- `.ltt` 不是字体本身，是真字体 + 头部；直接当 TTF 加载会失败。
- 字体的 PUA 码位（图标）全是 .notdef：图标不在字体里，别指望渲染出来。
- UXC 容器里的记录有 4 字节对齐填充，解析要按 `[u32 id][text][NUL][pad]` 走。

### 5.5 流程/工具

- **PowerShell 会吞掉/改写引号与 UTF-8**：复杂命令一律写成 `.sh` / `.py` 再执行，
  不要在 PowerShell 里拼多行 bash（本项目踩了十几次）。
- 镜像里的分区要用正确的 `offset` 挂载；`-o loop,ro,offset=…` 的偏移是
  `524288 + lba*512`。
- 真机 service 会话很脆：一次会话只做一件事，做完直接断电，不要调 `auth.stop()`。
- 真机只读验证要 `/dev/mem`；没有 fbdev，不要找 `/proc/fb`。

## 6. 还没做完

1. **真机验证**：触摸坐标尺度、原生触控 AF 干扰、真机 60 fps present 下的闪烁窗口，
   以及"MENU 按下 → 原生 present → 叠层自动返回"这条判定（QEMU 里注入的原生键
   不触发重绘，只能在真机确认）。
2. **完整菜单**：目前叠层只接了 48 行（8 分类 × 6 行）的示例，456 项的原生分页/分组
   数据还没完全解出来（分类归属按名字近似映射）。
3. **触摸独占**：现在只读坐标，原生触控 AF 仍会被触发（需要关掉原生触控区域）。
4. **present 钩子**：真机 60 fps 场景下，最稳的是在 present 提交前写源面。

## 7. 复现要点

```bash
# 1) 构建 payload（需要 ARM clang/lld）
bash payloads/scripts/build-newmenu.sh

# 2) 生成素材（字体/图标，需自备固件，仓库不附带）
python3 tools/ltt-extract.py <从你的固件里取出的 .ltt> out.ttf
python3 tools/bake-font-atlas.py --size 26 --bits 4

# 3) 装进镜像并启动
bash payloads/scripts/install-newmenu.sh on
bash payloads/scripts/set-newmenu-flags.sh badge on      # 原生画面上的切换按钮
python3 tools/run-overlay-commit.py --tag demo --menu 100 --stop 160

# 4) 实时看菜单 + 用坐标驱动（走真机同一套命中测试）
bash payloads/scripts/see-menu.sh 8765
python3 payloads/scripts/nm-drive.py click 600 20        # 点右上角按钮
python3 payloads/scripts/nm-drive.py click 300 248 300 248   # 选中再点 = 开二级页
python3 payloads/scripts/nm-drive.py snap popup

# 5) 解码固件资源 / 命令流 / RAM dump
python3 tools/uxc-strtable.py <string_simplifiedchinese.uxc> --json str_zh.json
python3 tools/decode-cmdbuf.py <命令表 dump>
```

> 素材与固件都不在本仓库中；工具会直接读取你本地的文件路径。
