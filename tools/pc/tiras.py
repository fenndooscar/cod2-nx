# cod2 - slices a screenshot into readable strips so the on-screen text can be read.
#
# Why it exists: the PC build's only progress report is what the guest draws, and the log cannot say
# whether a menu, a loading screen or gameplay is on screen. A 2560x1440 capture has to be cut into
# strips to be legible.
#
# Usage: python tools/pc/tiras.py <entrada.png> <carpeta_salida> [filas]
import sys
from pathlib import Path

from PIL import Image

entrada = Path(sys.argv[1])
salida = Path(sys.argv[2])
filas = int(sys.argv[3]) if len(sys.argv) > 3 else 3
salida.mkdir(parents=True, exist_ok=True)

im = Image.open(entrada).convert("RGB")
ancho, alto = im.size
paso = alto // filas
for i in range(filas):
    y0 = i * paso
    y1 = alto if i == filas - 1 else (i + 1) * paso
    tira = im.crop((0, y0, ancho, y1))
    escala = min(1.0, 1400 / ancho)
    tira = tira.resize((int(ancho * escala), int((y1 - y0) * escala)), Image.LANCZOS)
    destino = salida / f"{entrada.stem}_tira{i}.png"
    tira.save(destino)
    print(destino)
