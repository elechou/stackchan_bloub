# stackchan-eyes

给 M5Stack StackChan（CoreS3）用的裸机待机固件：全黑屏幕上只有两只白色眼睛，
在 bloub 的表情之间无限随机切换，表情之间平滑变形。

没有操作系统，没有 FreeRTOS，没有 ESP-IDF，没有 Arduino，没有中断，没有堆。
一个主循环，每秒 60 次：推进动画，重画变化的部分，等下一拍。

## 现在设备上是什么

`build/eyes.bin`，16 800 字节，
SHA-256 `0194443eb9079a01b8c6044c79db0a57db48f78faa74bea08393729affab3609`，
由 git tag `eyes-v3` 的源码编译而来（与 `docs/stress-test.md` 里的「第 3 版」是同一份）。

它烧在 Flash 偏移 0，也就是原来二级 bootloader 的位置，占 32 KB 槽位的 51%。
芯片的 ROM 把它整个读进内存后直接运行。**原厂的应用、设置、资源分区一个字节都没动**，
所以还原只需要把原厂 bootloader 写回去。

## 常用命令

都在本目录下运行，默认串口 COM6。

```
python tools/fw.py build            编译，生成 build/eyes.bin
python tools/fw.py flash            编译并烧录
python tools/fw.py restore          还原原厂固件（写回原厂 bootloader）
python tools/fw.py monitor          看固件的串口输出，Ctrl+C 退出
python tools/fw.py log 10           重启设备并打印 10 秒启动日志
python tools/fw.py key i            向固件发送按键（见下表）
python tools/fw.py shot out.png     把屏幕显存读回成图片
```

编译器和 esptool 用的是 PlatformIO 放在 `~/.platformio` 里的那一套
（`toolchain-xtensa-esp32s3` 8.4.0、`tool-esptoolpy`）。

串口按键：

| 键 | 作用 |
|---|---|
| `i` | 屏幕状态、电池和供电、当前表情 |
| `b` | 重放启动日志（电脑一般来不及看到第一遍） |
| `n` | 立即换下一个表情 |
| `+` `-` | 背光亮度加减 10（10~100，不保存，重启恢复默认） |
| `s` | 把屏幕显存整屏读回并打印，约 6 秒 |
| `v` | 把屏幕显存整屏读回，与刚画的一帧逐像素比对，约 6 秒 |

## 可以调的地方

`src/core/eyes.h`：

| 常量 | 现值 | 含义 |
|---|---|---|
| `EYES_SCALE_PX` | 136 | 眼睛所在的那个看不见的球的半径（像素）。越大眼睛越大。136 是所有表情连同视线漂移都不出屏的最大值 |
| `EYES_HOLD_MIN_S` / `MAX_S` | 3 / 7 | 每个表情停留的秒数范围 |
| `EYES_MORPH_S` | 0.70 | 两个表情之间变形的时长 |

`src/board/board.h`：`BOARD_BRIGHTNESS_DEFAULT`（现值 60）是开机背光亮度。

表情表在 `src/core/eyes.c` 的 `EYES_EXPR[]`，删掉或增加一行即可。

## 和 bloub 的关系

眼睛的几何模型（画在球面上的胶囊、切平面投影、每只眼自己的倾斜、眨眼是屏幕上的
竖向压扁）和每个表情的数值，从 bloub 的 `face.ts`、`expressions.ts`、`engine.ts`
原样移植，并用 bloub 自己的引擎核对过（见 `docs/stress-test.md`）。

有意不同的地方：

- **没有身体**。整屏纯黑，只画眼睛，所以和轮廓有关的一切都不存在。
- **18 个表情**：bloub 的 16 个静态表情，加上它状态表里两个只动眼睛的状态
  （`wink` 眨单眼、`wide` 睁大眼）。不想要这两个，删掉 `EYES_EXPR[]` 最后两行。
- **过渡曲线**。bloub 用 `1-(1-k)^5`，0.45 秒，起步就是全速。随机切换时眼睛可能要横跨
  半个屏幕，第一帧看起来像跳了一下。这里乘了一个 `(1+5k)`，变成从静止起步、同样的
  指数收尾、同样不过冲，时长 0.70 秒。想要 bloub 原味，把 `ease()` 改回去、
  `EYES_MORPH_S` 改成 0.45。
- **随机方式**：洗牌袋，18 个表情都出现一遍才会重复，且不会连续两次相同。
- **时间**：bloub 用绝对时间求值；这里每个振荡器自己累加相位，可以无限期运行而不丢精度。

## 目录

```
src/core/     表情、动画、抗锯齿光栅化。纯 C，不依赖硬件，电脑上也能编译
src/hal/      芯片：启动、时钟、看门狗、串口、GPIO、I2C、SPI 和屏幕驱动
src/board/    板子：电源芯片、IO 扩展、屏幕型号识别、身体控制器
src/main.c    主循环
ld/eyes.ld    链接脚本，全部放在内存里
sim/          电脑端仿真器，和固件跑同一份 src/core
tools/        fw.py（编译烧录）、stress.py（压测）、bloub_ref.ts（取 bloub 参考值）
docs/         压测记录
```

上一级目录（git 仓库根）里：

| 目录 | 内容 | git 里怎么放 |
|---|---|---|
| `bloub/` | 眼睛模型的来源，`tools/bloub_ref.ts` 用它核对移植 | 子模块，固定在核对时用的提交 |
| `StackChan/` | M5 原厂固件源码，对照寄存器和初始化序列用 | 子模块，固定在参考时的提交 |
| `backup/` | 原厂 bootloader 和部分 Flash 读出 | 不进 git |
| `_ref/` | 写驱动时对照用的 ESP-IDF 头文件和 M5 显示库源码（可以删） | 不进 git |

克隆后取子模块：`git submodule update --init`。

## 它对硬件做了什么

- 电源芯片 AXP2101、IO 扩展 AW9523 的寄存器写入与原厂固件逐字节相同，
  包括各路电压和充电电流（700 mA）。
- 屏幕初始化序列与原厂固件相同；按原厂的办法（读触摸芯片固件号）区分 ILI9342C 和 E。
- **关闭舵机供电**，头部不会被电机保持住，可以用手轻轻摆正。**关闭灯带**。
- 不碰 WiFi、蓝牙、摄像头、麦克风、喇叭、SD 卡、Flash。

## 需要知道的事

- **整片 Flash 备份决定不做。** 本固件只改写 Flash 前 32 KB，原厂 bootloader 已另存为
  `backup/factory_bootloader_0x0000-0x8000.bin`，`restore` 只需要它。`backup/` 里另有
  第一次烧录前读出的前 7 MB（`chunk_00` ~ `chunk_06`），用不到。`backup/` 不进 git，
  上面那个 32 KB 的文件要自己留好。真要补完整片备份：`bash ../backup/read_chunks.sh COM6`
  （从第 8 块续读，约 18 分钟）。
- **没有休眠也没有自动关机**，屏幕一直亮。USB 供电没问题；只靠电池会一直耗到电源芯片
  自己断电。长按电源键关机靠的是电源芯片的硬件功能，本固件没有验证过。
- 冷启动、长时间运行、电池供电、肉眼观感都还没测，详见 `docs/stress-test.md` 末尾。
- 设备卡死也能重新烧录：ESP32-S3 的下载模式在 ROM 里，`fw.py flash` 和 `restore`
  会通过 USB 自动进入（本次开发中每次烧录都是这样进的）。万一进不去，M5 的 CoreS3
  文档给的手动办法是长按复位键约 3 秒直到绿灯亮；这个办法我没有在这台设备上试过。
