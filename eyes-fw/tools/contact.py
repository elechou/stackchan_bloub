"""Turn the simulator's PGM frames into PNG contact sheets."""
import glob, os, sys
from PIL import Image, ImageDraw

def sheet(files, out, cols, label=True, scale=1):
    ims = [Image.open(f).convert("L") for f in files]
    w, h = ims[0].size
    w2, h2 = w * scale, h * scale
    rows = (len(ims) + cols - 1) // cols
    pad = 6
    canvas = Image.new("RGB", (cols * (w2 + pad) + pad, rows * (h2 + pad) + pad), (40, 44, 60))
    d = ImageDraw.Draw(canvas)
    for i, im in enumerate(ims):
        x = pad + (i % cols) * (w2 + pad)
        y = pad + (i // cols) * (h2 + pad)
        if scale != 1:
            im = im.resize((w2, h2), Image.NEAREST)
        canvas.paste(im.convert("RGB"), (x, y))
        if label:
            name = os.path.splitext(os.path.basename(files[i]))[0]
            d.text((x + 4, y + 3), name, fill=(120, 200, 255))
    canvas.save(out)
    print(out, canvas.size)

if __name__ == "__main__":
    mode, src, out = sys.argv[1], sys.argv[2], sys.argv[3]
    files = sorted(glob.glob(os.path.join(src, "*.pgm")))
    if mode == "sheet":
        sheet(files, out, 6)
    elif mode == "strip":
        a, b, step = int(sys.argv[4]), int(sys.argv[5]), int(sys.argv[6])
        sheet(files[a:b:step], out, int(sys.argv[7]) if len(sys.argv) > 7 else 6)
    elif mode == "zoom":
        sheet(files[int(sys.argv[4]):int(sys.argv[4]) + 1], out, 1, False, 3)
