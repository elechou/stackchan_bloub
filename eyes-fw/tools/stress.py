"""Cycle through every expression quickly and report the firmware's frame statistics.

    python tools/stress.py [SECONDS] [PORT]

Presses 'n' (next expression) about once a second, so each 10 s window holds
many transitions, the large ones included, then runs the on-device pixel check.
"""
import os, re, sys, time
sys.path.append(os.path.join(os.path.expanduser("~"), ".platformio", "penv", "Lib", "site-packages"))
import serial

seconds = float(sys.argv[1]) if len(sys.argv) > 1 else 60
port = sys.argv[2] if len(sys.argv) > 2 else "COM6"
s = serial.Serial(); s.port = port; s.baudrate = 115200; s.timeout = 0.02; s.dtr = False; s.rts = False; s.open()
buf = b""; t0 = time.time(); last_key = 0; seen = set(); frames = late = 0; worst = 0
def lines():
    global buf
    buf += s.read(4096)
    while b"\n" in buf:
        line, buf = buf.split(b"\n", 1)
        yield line.decode("ascii", "replace").strip()
while time.time() - t0 < seconds:
    if time.time() - last_key > 1.1:
        s.write(b"n"); last_key = time.time()
    for line in lines():
        if line.startswith("expr "):
            seen.add(line[5:])
        m = re.match(r"stat: up [\d.]+ s, (\d+) frames in 10 s, work avg (\d+) us, max (\d+) us, late (\d+)", line)
        if m:
            print(line)
            frames += int(m.group(1)); late += int(m.group(4)); worst = max(worst, int(m.group(3)))
print("expressions seen: %d  frames: %d  late frames: %d  worst frame: %d us" % (len(seen), frames, late, worst))
s.write(b"v"); t1 = time.time(); done = False
while time.time() - t1 < 12 and not done:
    for line in lines():
        if line.startswith("verify:"):
            print(line); done = True
s.close()
