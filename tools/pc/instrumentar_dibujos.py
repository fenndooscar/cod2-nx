# cod2 - one-shot edit: instrument every silent exit of the native draw path.
#
# Why: `C6: dibujados=14700 rechazados=0` cannot be told apart from "the ring only offered 5 draws per
# frame". Eight exits returned false and nine returned true without touching any counter, so the arithmetic
# between the ring's draw count and what the renderer painted did not close anywhere.
#
# Line numbers are the ones read from the file before any of this; the two-line replacements are done from
# the bottom up so the earlier ones keep their numbers.
import sys

ruta = r'app/src/cod2_nativo_dibujos.cpp'
L = open(ruta, encoding='utf-8').read().split('\n')


def sustituir(linea, viejo, nuevo):
    i = linea - 1
    if L[i] != viejo:
        print('NO COINCIDE %d:\n  esperado: %r\n  leido:    %r' % (linea, viejo, L[i]))
        sys.exit(1)
    L[i] = nuevo


# --- los nueve `return true` que aceptan el dibujo sin pintarlo (de abajo arriba) ---------------------
for linea, sangria in [(3194, '        '), (3099, '      '), (2597, '        '), (2488, '        '),
                       (2465, '      '), (2447, '      '), (2371, '          '), (2246, '      '),
                       (2238, '      ')]:
    i = linea - 1
    if not L[i].startswith(sangria + 'return true;'):
        print('NO COINCIDE (true) %d: %r' % (linea, L[i]))
        sys.exit(1)
    L[i] = sangria + '++sin_pintar_;\n' + L[i]

# --- los ocho `return false` silenciosos: pasan por Rechazar y salen en "rechazos por causa" -----------
sustituir(2200, '      return false;',
          '      return Rechazar(300, "sin registros o sin vertex shader");')
sustituir(2214, '      return false;',
          '      return Rechazar(301, "destino de color sin pixel shader");')
sustituir(2291, '      return false;',
          '      return Rechazar(302, "sin entrada de vertices");')
sustituir(2897, '        return false;',
          '        return Rechazar(303, "el envio del bufer de subida fallo");')
sustituir(2944, '        return false;',
          '        return Rechazar(304, "no se pudo empezar el pase");')
sustituir(2963, '        return false;',
          '        return Rechazar(305, "no se pudo subir una textura");')
sustituir(3277, '      return false;',
          '      return Rechazar(306, "sin tuberia para esta clave");')
sustituir(3510, '        return false;',
          '        return Rechazar(307, "sin tuberia para esta clave (estado dinamico)");')

# --- y cuantos dibujos llegan, para que la resta cierre ------------------------------------------------
i = 2190  # `bool Dibujar(const PeticionDibujo& p) override {`
if 'bool Dibujar(const PeticionDibujo& p) override {' not in L[i]:
    print('NO COINCIDE (cabecera de Dibujar): %r' % L[i])
    sys.exit(1)
L[i] = L[i] + '\n    ++peticiones_;'

open(ruta, 'w', encoding='utf-8', newline='\n').write('\n'.join(L))
print('cod2_nativo_dibujos.cpp: 8 rechazos instrumentados, 9 salidas mudas contadas, peticiones contadas')
