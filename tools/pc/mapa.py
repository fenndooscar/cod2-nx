# cod2 - prints a coarse luminance map of a screenshot as text, so the layout of what the guest
# draws can be judged without looking at the image.
#
# Why it exists: the capture is 2560x1440 and read_image downscales it, so the on-screen text is not
# legible. The map says where the bright blocks and the text lines are; tools/pc/recorte.py then
# crops the region that matters at full resolution.
#
# Usage: python tools/pc/mapa.py <imagen.png> [columnas] [filas]
import sys
from pathlib import Path

import numpy as np
from PIL import Image

imagen = Path(sys.argv[1])
columnas = int(sys.argv[2]) if len(sys.argv) > 2 else 64
filas = int(sys.argv[3]) if len(sys.argv) > 3 else 36

im = Image.open(imagen).convert("L").resize((columnas, filas), Image.BOX)
a = np.asarray(im, dtype=np.float32)
rampa = " .:-=+*#%@"
lo, hi = float(a.min()), float(a.max())
print(f"{imagen.name}: {lo:.0f}..{hi:.0f}")
for y in range(filas):
    linea = ""
    for x in range(columnas):
        v = 0.0 if hi <= lo else (a[y, x] - lo) / (hi - lo)
        linea += rampa[min(len(rampa) - 1, int(v * len(rampa)))]
    print(f"{y:2d} |{linea}|")
