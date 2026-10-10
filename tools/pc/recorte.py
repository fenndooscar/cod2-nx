# cod2 - crops a region out of a screenshot at full resolution so the text in it can be read.
#
# Why it exists: read_image downsizes anything wider than ~1500 px, and the guest's UI text is drawn
# at 2560x1440, so a whole-screen capture comes back illegible. Cropping first keeps the pixels.
#
# Usage: python tools/pc/recorte.py <entrada.png> <x> <y> <ancho> <alto> <salida.png>
import sys
from pathlib import Path

from PIL import Image

entrada, x, y, ancho, alto, salida = (
    sys.argv[1],
    int(sys.argv[2]),
    int(sys.argv[3]),
    int(sys.argv[4]),
    int(sys.argv[5]),
    sys.argv[6],
)
im = Image.open(entrada).convert("RGB")
recorte = im.crop((x, y, x + ancho, y + alto))
Path(salida).parent.mkdir(parents=True, exist_ok=True)
recorte.save(salida)
print(f"{salida} {recorte.size[0]}x{recorte.size[1]}")
