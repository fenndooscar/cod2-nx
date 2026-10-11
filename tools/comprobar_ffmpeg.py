"""Checks that the FFmpeg decoders this port enables are enabled in all three places at once.

Usage: python tools/comprobar_ffmpeg.py

Enabling an FFmpeg decoder here takes three edits that fail silently on their own:

  1. a `config_*.h` must end up with CONFIG_<NAME> at 1. Every platform config the vendored tree ships has it at 0,
     and the port's own `config_cod2_*.h` files turn on what they need with #undef/#define, so the answer is the
     value left after `config.h` and everything it includes has been read in order - not the value in any one file;
  2. every object of that component must be listed in the `add_library(libavcodec ...)` source list of
     `sdk/thirdparty/CMakeLists.txt`, because that list is explicit and is not generated from FFmpeg's Makefile;
  3. `ffmpeg-overlay/codec_list.c` must reference the decoder, or `avcodec_find_decoder` returns NULL and the port
     silently falls back to the guest's own decoder - no error, no warning, just the slow path.

The objects come from FFmpeg's own `libavcodec/Makefile` (`OBJS-$(CONFIG_X) += ...`) and the extra components from
`configure`'s `<name>_decoder_select="..."`, so this stays right when the vendored tree is updated.
"""
import os
import re
import sys

RAIZ = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
FFMPEG = os.path.join(RAIZ, 'sdk', 'thirdparty', 'FFmpeg')
CMAKELISTS = os.path.join(RAIZ, 'sdk', 'thirdparty', 'CMakeLists.txt')
OVERLAY = os.path.join(RAIZ, 'sdk', 'thirdparty', 'ffmpeg-overlay', 'codec_list.c')


def leer(p):
    with open(p, encoding='utf-8', errors='ignore') as f:
        return f.read()


def configs_finales(entrada='config.h', visto=None, valores=None):
    """The value CONFIG_* is left at after reading config.h and the files it includes, in order.

    This is the only answer that means anything here: the platform headers set a component to 0 and the port's
    config_cod2_*.h files #undef it and set it to 1 afterwards, so reading one file gives the wrong answer.
    """
    if visto is None:
        visto, valores = set(), {}
    ruta = os.path.join(FFMPEG, entrada)
    if entrada in visto or not os.path.isfile(ruta):
        return valores
    visto.add(entrada)
    for linea in leer(ruta).splitlines():
        m = re.match(r'\s*#\s*(define|undef)\s+(CONFIG_\w+)(?:\s+(\S+))?', linea)
        if m:
            if m.group(1) == 'undef':
                valores[m.group(2)] = '0'
            else:
                valores[m.group(2)] = m.group(3) or '1'
            continue
        m = re.match(r'\s*#\s*include\s+"([^"]+)"', linea)
        if m:
            configs_finales(m.group(1), visto, valores)
    return valores


def objetos_por_componente():
    objs = {}
    makefile = leer(os.path.join(FFMPEG, 'libavcodec', 'Makefile'))
    for m in re.finditer(r'^OBJS-\$\((CONFIG_\w+)\)\s*\+=\s*(.*)$', makefile, re.M):
        objs.setdefault(m.group(1), set()).update(m.group(2).split())
    return objs


def componentes_seleccionados():
    sel = {}
    for m in re.finditer(r'^(\w+)_decoder_select="([^"]*)"', leer(os.path.join(FFMPEG, 'configure')), re.M):
        sel[m.group(1)] = m.group(2).split()
    return sel


def fuentes_de_cmake():
    s = leer(CMAKELISTS)
    i = s.find('add_library(libavcodec STATIC')
    j = s.find('\n)', i)
    return set(os.path.basename(f) for f in re.findall(r'\$\{FFMPEG_DIR\}/([^\s\)]+\.c)', s[i:j]))


def main():
    if hasattr(sys.stdout, 'reconfigure'):
        sys.stdout.reconfigure(encoding='utf-8', errors='replace')

    valores = configs_finales()
    objs = objetos_por_componente()
    sel = componentes_seleccionados()
    fuentes = fuentes_de_cmake()

    registrados = re.findall(r'#if\s+(CONFIG_\w+)\s*\n\s*&(ff_\w+),', leer(OVERLAY))
    if not registrados:
        raise SystemExit('codec_list.c: no decoder found; the pattern changed')

    fallos = []
    for config, simbolo in registrados:
        componente = config[len('CONFIG_'):].lower()
        queridos = {componente} | set(sel.get(componente, []))
        propios = []
        for q in sorted(queridos):
            c = 'CONFIG_' + q.upper()
            if valores.get(c) != '1':
                propios.append('%s is %s, not 1' % (c, valores.get(c, 'undefined')))
            for o in sorted(objs.get(c, ())):
                if o.endswith('.o') and o[:-2] + '.c' not in fuentes:
                    propios.append('%s needs %s, missing from the libavcodec source list' % (c, o))
        print('%-22s %-28s %s' % (simbolo, config, 'ok' if not propios else 'FAIL: ' + '; '.join(propios)))
        fallos += ['%s: %s' % (simbolo, p) for p in propios]

    if fallos:
        print()
        for f in fallos:
            print('FAIL ' + f)
        raise SystemExit(1)
    print('\n%d decoder(s) enabled consistently' % len(registrados))


if __name__ == '__main__':
    main()
