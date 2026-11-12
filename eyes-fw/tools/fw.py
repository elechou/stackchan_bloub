#!/usr/bin/env python3
"""Build, flash, watch and undo the StackChan eyes firmware.

    python tools/fw.py build              compile -> build/eyes.bin
    python tools/fw.py flash [PORT]       write it to the robot
    python tools/fw.py monitor [PORT]     print what the firmware says
    python tools/fw.py log [SECONDS] [PORT]  reboot the robot and print its boot log
    python tools/fw.py key KEYS [PORT]    type keys into its console (s i n + -)
    python tools/fw.py shot OUT.png [PORT]  read the panel's memory back as a picture
    python tools/fw.py restore [PORT]     put the factory firmware back
    python tools/fw.py sim                build the desktop simulator

It uses the compiler and esptool that PlatformIO keeps in ~/.platformio.
The image is flashed at offset 0, in place of the second-stage bootloader,
and must stay under 32 KB so the partition table and everything after it
(the factory application, its settings) are never touched.
"""
import glob
import os
import re
import subprocess
import sys
import time

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build")
PIO = os.path.join(os.path.expanduser("~"), ".platformio")
PIO_PY = os.path.join(PIO, "penv", "Scripts", "python.exe")
if not os.path.exists(PIO_PY):
    PIO_PY = os.path.join(PIO, "penv", "bin", "python")
ESPTOOL = os.path.join(PIO, "packages", "tool-esptoolpy", "esptool.py")
GCC = os.path.join(PIO, "packages", "toolchain-xtensa-esp32s3", "bin", "xtensa-esp32s3-elf-gcc")
SIZE = os.path.join(PIO, "packages", "toolchain-xtensa-esp32s3", "bin", "xtensa-esp32s3-elf-size")

DEFAULT_PORT = "COM6"
BOOTLOADER_SLOT = 0x8000  # the partition table starts here
BACKUP_DIR = os.path.join(os.path.dirname(ROOT), "backup")
# The only bytes this firmware ever replaces, saved before the first flash.
FACTORY_BOOTLOADER = os.path.join(BACKUP_DIR, "factory_bootloader_0x0000-0x8000.bin")
FACTORY_FULL = os.path.join(BACKUP_DIR, "factory_full_16MB.bin")

SOURCES = [
    "src/hal/start.S",
    "src/hal/hal.c",
    "src/hal/i2c.c",
    "src/hal/lcd.c",
    "src/board/board.c",
    "src/core/fmath.c",
    "src/core/eyes.c",
    "src/core/raster.c",
    "src/core/raster_lut.c",
    "src/main.c",
]

CFLAGS = [
    "-std=gnu99", "-O2", "-g",
    "-ffreestanding", "-ffunction-sections", "-fdata-sections",
    "-mlongcalls", "-mtext-section-literals",
    "-Wall", "-Wextra", "-Wdouble-promotion", "-Werror=implicit-function-declaration",
]


def run(cmd, **kw):
    r = subprocess.run(cmd, **kw)
    if r.returncode != 0:
        sys.exit(r.returncode)
    return r


def esptool(port, *args):
    return [PIO_PY, ESPTOOL, "--chip", "esp32s3", "-p", port, *args]


def build():
    os.makedirs(BUILD, exist_ok=True)
    run([sys.executable, os.path.join(ROOT, "tools", "gen_lut.py"), os.path.join(ROOT, "src", "core", "raster_lut.c")])
    objs = []
    for src in SOURCES:
        obj = os.path.join(BUILD, src.replace("/", "_") + ".o")
        run([GCC, *CFLAGS, "-c", os.path.join(ROOT, src), "-o", obj])
        objs.append(obj)
    elf = os.path.join(BUILD, "eyes.elf")
    run([GCC, "-nostdlib", "-nostartfiles", "-Wl,--gc-sections",
         "-Wl,-T," + os.path.join(ROOT, "ld", "eyes.ld"),
         "-Wl,-Map=" + os.path.join(BUILD, "eyes.map"),
         *objs, "-lgcc", "-o", elf])
    out = os.path.join(BUILD, "eyes.bin")
    # Same flash settings as the factory bootloader's own header: DIO, 80 MHz, 16 MB.
    run([PIO_PY, ESPTOOL, "--chip", "esp32s3", "elf2image", "--flash_mode", "dio", "--flash_freq", "80m",
         "--flash_size", "16MB", "-o", out, elf], stdout=subprocess.DEVNULL)
    run([SIZE, elf])
    n = os.path.getsize(out)
    print("build/eyes.bin: %d bytes (%.1f%% of the %d-byte bootloader slot)" % (n, 100.0 * n / BOOTLOADER_SLOT, BOOTLOADER_SLOT))
    if n > BOOTLOADER_SLOT:
        sys.exit("image too large: it would overwrite the partition table")
    return out


def flash(port):
    out = build()
    run(esptool(port, "--before", "default_reset", "--after", "hard_reset", "write_flash",
                "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep", "0x0", out))


def restore(port):
    """Write the factory bootloader back. Nothing else was ever changed."""
    if os.path.exists(FACTORY_BOOTLOADER):
        with open(FACTORY_BOOTLOADER, "rb") as f:
            boot = f.read()
    elif os.path.exists(FACTORY_FULL):
        with open(FACTORY_FULL, "rb") as f:
            boot = f.read(BOOTLOADER_SLOT)
    else:
        sys.exit("no backup found in " + BACKUP_DIR)
    if len(boot) != BOOTLOADER_SLOT or boot[0] != 0xE9:
        sys.exit("the backup does not look like a bootloader, refusing to write it")
    tmp = os.path.join(BUILD, "factory_bootloader.bin")
    os.makedirs(BUILD, exist_ok=True)
    with open(tmp, "wb") as f:
        f.write(boot)
    run(esptool(port, "--before", "default_reset", "--after", "hard_reset", "write_flash",
                "--flash_mode", "keep", "--flash_freq", "keep", "--flash_size", "keep", "0x0", tmp))
    print("factory bootloader restored; the robot boots its original firmware again")


def open_port(port):
    try:
        import serial
    except ImportError:  # borrow PlatformIO's copy of pyserial
        sys.path.append(os.path.join(PIO, "penv", "Lib", "site-packages"))
        import serial
    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.2
    # Leave DTR/RTS alone: on this chip they are the reset and boot-mode lines.
    s.dtr = False
    s.rts = False
    s.open()
    return s


def monitor(port, seconds=None, keys="", reset=False):
    s = open_port(port)
    if reset:
        # RTS is the chip's reset line on the built-in USB serial port.
        s.rts = True
        time.sleep(0.1)
        s.rts = False
    t0 = time.time()
    sent = False
    try:
        while seconds is None or time.time() - t0 < seconds:
            if keys and not sent and time.time() - t0 > 0.5:
                s.write(keys.encode())
                sent = True
            data = s.read(4096)
            if data:
                sys.stdout.write(data.decode("utf-8", "replace"))
                sys.stdout.flush()
    except KeyboardInterrupt:
        pass
    finally:
        s.close()


def shot(port, out_png):
    from PIL import Image
    s = open_port(port)
    s.reset_input_buffer()
    s.write(b"s")
    rows = {}
    buf = b""
    t0 = time.time()
    done = False
    while time.time() - t0 < 60 and not done:
        buf += s.read(4096)
        while b"\n" in buf:
            line, buf = buf.split(b"\n", 1)
            line = line.decode("ascii", "replace").strip()
            m = re.match(r"R(\d+):(.*)", line)
            if m:
                px = []
                for tok in m.group(2).split():
                    n, v = tok.split("*")
                    px.extend([int(v)] * int(n))
                rows[int(m.group(1))] = px
            elif line == "SHOT END":
                done = True
    s.close()
    good = [y for y, px in rows.items() if len(px) == 320]
    print("rows received: %d of 240 (%d complete)" % (len(rows), len(good)))
    im = Image.new("L", (320, 240), 0)
    for y in good:
        for x, v in enumerate(rows[y]):
            im.putpixel((x, y), min(255, v * 255 // 63))
    im.save(out_png)
    print("saved", out_png)
    return len(good)


def sim():
    os.makedirs(BUILD, exist_ok=True)
    srcs = ["sim/sim_main.c", "src/core/eyes.c", "src/core/raster.c", "src/core/raster_lut.c", "src/core/fmath.c"]
    run(["clang", "-O2", "-Wall", "-Wextra", "-std=c99", "-o", os.path.join(BUILD, "sim.exe"),
         *[os.path.join(ROOT, s) for s in srcs], "-lm"])
    print("build/sim.exe")


def main():
    a = sys.argv[1:]
    cmd = a[0] if a else "build"
    if cmd == "build":
        build()
    elif cmd == "flash":
        flash(a[1] if len(a) > 1 else DEFAULT_PORT)
    elif cmd == "restore":
        restore(a[1] if len(a) > 1 else DEFAULT_PORT)
    elif cmd == "monitor":
        port = a[1] if len(a) > 1 else DEFAULT_PORT
        monitor(port, float(a[2]) if len(a) > 2 else None)
    elif cmd == "log":
        monitor(a[2] if len(a) > 2 else DEFAULT_PORT, float(a[1]) if len(a) > 1 else 8.0, reset=True)
    elif cmd == "key":
        monitor(a[2] if len(a) > 2 else DEFAULT_PORT, float(a[3]) if len(a) > 3 else 3.0, a[1])
    elif cmd == "shot":
        shot(a[2] if len(a) > 2 else DEFAULT_PORT, a[1])
    elif cmd == "sim":
        sim()
    else:
        sys.exit(__doc__)


if __name__ == "__main__":
    main()
