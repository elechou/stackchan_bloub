"""Turn the simulator's PGM frames into PNG contact sheets.

    python tools/contact.py sheet SRC OUT.png [COLS]         every frame, labelled
    python tools/contact.py face  SRC OUT.png [COLS]         the same, set in the robot's front
    python tools/contact.py strip SRC OUT.png A B STEP [COLS] [face]   frames A..B, one in STEP
    python tools/contact.py zoom  SRC OUT.png N              frame N, three times larger

`face` shows what the eye sees rather than what the panel holds: the 320x240
glass in the middle of the 50 mm front (400 px at the panel's 8 px per mm).
The front is drawn a shade lighter than it is, with the glass outlined, so
that the picture shows where the eyes may go; on the robot both are black.
"""
import glob, os, sys
from PIL import Image, ImageDraw

FACE = 400          # the front, in panel pixels
FRONT = (26, 27, 32)
GLASS = (0, 0, 0)
EDGE = (52, 54, 64)
PAPER = (232, 232, 236)


def in_face(im):
    """A 320x240 frame, set in the front of the robot."""
    w, h = im.size
    face = Image.new("RGB", (FACE, FACE), PAPER)
    d = ImageDraw.Draw(face)
    d.rounded_rectangle((0, 0, FACE - 1, FACE - 1), radius=44, fill=FRONT)
    x, y = (FACE - w) // 2, (FACE - h) // 2
    d.rectangle((x - 1, y - 1, x + w, y + h), outline=EDGE, fill=GLASS)
    face.paste(im.convert("RGB"), (x, y))
    return face


def sheet(files, out, cols, label=True, scale=1.0, face=False):
    ims = [Image.open(f).convert("L") for f in files]
    if face:
        ims = [in_face(im) for im in ims]
    w, h = ims[0].size
    w2, h2 = int(w * scale), int(h * scale)
    rows = (len(ims) + cols - 1) // cols
    pad = 6
    canvas = Image.new("RGB", (cols * (w2 + pad) + pad, rows * (h2 + pad) + pad), PAPER if face else (40, 44, 60))
    d = ImageDraw.Draw(canvas)
    for i, im in enumerate(ims):
        x = pad + (i % cols) * (w2 + pad)
        y = pad + (i // cols) * (h2 + pad)
        if scale != 1:
            im = im.resize((w2, h2), Image.NEAREST if scale > 1 else Image.LANCZOS)
        canvas.paste(im.convert("RGB"), (x, y))
        if label:
            name = os.path.splitext(os.path.basename(files[i]))[0]
            d.text((x + 6, y + 4), name, fill=(120, 200, 255))
    canvas.save(out)
    print(out, canvas.size)


if __name__ == "__main__":
    mode, src, out = sys.argv[1], sys.argv[2], sys.argv[3]
    files = sorted(glob.glob(os.path.join(src, "*.pgm")))
    if mode == "sheet":
        sheet(files, out, int(sys.argv[4]) if len(sys.argv) > 4 else 6)
    elif mode == "face":
        sheet(files, out, int(sys.argv[4]) if len(sys.argv) > 4 else 5, scale=0.75, face=True)
    elif mode == "strip":
        a, b, step = int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6])
        cols = int(sys.argv[7]) if len(sys.argv) > 7 else 6
        face = len(sys.argv) > 8 and sys.argv[8] == "face"
        sheet(files[a:b:step], out, cols, scale=0.5 if face else 1.0, face=face)
    elif mode == "zoom":
        sheet(files[int(sys.argv[4]):int(sys.argv[4]) + 1], out, 1, False, 3)
    else:
        sys.exit(__doc__)
