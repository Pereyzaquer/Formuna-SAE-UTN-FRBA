"""
Genera Code/ECU/include/state_font.h: la fuente suavizada de la franja de
estado de la pantalla.

Uso, desde Code/tools:
    python generar_fuente.py

Necesita Pillow (pip install pillow) y matplotlib, de donde se toma el
archivo de la fuente DejaVu Sans. DejaVu tiene licencia libre (derivada de
Bitstream Vera): se puede embeber en el firmware y publicar.

Por que una fuente propia: las fuentes que trae la libreria TFT_eSPI son de
1 bit, cada punto esta prendido o apagado, y en letras grandes los bordes
se ven escalonados. Esta es de 8 bits por punto (formato .vlw que entiende
TFT_eSPI): cada borde lleva su nivel de gris y la libreria lo mezcla con
el color de la franja, que es lo que la hace ver nitida.

Solo se incluyen los caracteres que usan los nombres de estado, para que
ocupe poco: unos 30 kB de flash.
"""
import os
import matplotlib
from PIL import Image, ImageDraw, ImageFont

TAMANO_PIXELES = 40          # alto de la fuente; mayusculas de ~29 puntos
CARACTERES = " -_0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ"

AQUI = os.path.dirname(os.path.abspath(__file__))
SALIDA = os.path.join(AQUI, "..", "ECU", "include", "state_font.h")
ARCHIVO_FUENTE = os.path.join(matplotlib.get_data_path(), "fonts", "ttf", "DejaVuSans.ttf")


def entero(valor):
    """Entero de 32 bits con signo, el byte mas significativo primero."""
    return (valor & 0xFFFFFFFF).to_bytes(4, "big")


def glifo(fuente, caracter):
    """Devuelve las medidas y el mapa de grises de un caracter."""
    avance = round(fuente.getlength(caracter))
    izquierda, arriba, derecha, abajo = fuente.getbbox(caracter, anchor="ls")
    ancho, alto = max(0, derecha - izquierda), max(0, abajo - arriba)

    grises = b""
    if ancho and alto:
        imagen = Image.new("L", (ancho, alto), 0)
        ImageDraw.Draw(imagen).text((-izquierda, -arriba), caracter, font=fuente,
                                    fill=255, anchor="ls")
        grises = imagen.tobytes()

    # dY es cuanto sobresale el glifo por arriba de la linea base (positivo
    # hacia arriba); dX, su corrimiento desde el cursor.
    return {"codigo": ord(caracter), "alto": alto, "ancho": ancho,
            "avance": avance, "dy": -arriba, "dx": izquierda, "grises": grises}


def main():
    fuente = ImageFont.truetype(ARCHIVO_FUENTE, TAMANO_PIXELES)
    ascenso, descenso = fuente.getmetrics()
    glifos = [glifo(fuente, c) for c in CARACTERES]

    datos = entero(len(glifos)) + entero(11) + entero(TAMANO_PIXELES) + entero(0)
    datos += entero(ascenso) + entero(descenso)
    for g in glifos:
        datos += b"".join(entero(v) for v in
                          (g["codigo"], g["alto"], g["ancho"], g["avance"], g["dy"], g["dx"], 0))
    for g in glifos:
        datos += g["grises"]

    lineas = [", ".join("0x%02X" % b for b in datos[i:i + 16]) for i in range(0, len(datos), 16)]
    with open(SALIDA, "w", encoding="utf-8", newline="\n") as archivo:
        archivo.write("/* Generado por Code/tools/generar_fuente.py. No editar a mano.\n")
        archivo.write("   Fuente DejaVu Sans, %d px, suavizada (8 bits por punto), formato\n" % TAMANO_PIXELES)
        archivo.write("   .vlw de TFT_eSPI. Caracteres: \"%s\". */\n" % CARACTERES)
        archivo.write("#pragma once\n#include <pgmspace.h>\n\n")
        archivo.write("const uint8_t STATE_FONT[] PROGMEM = {\n  ")
        archivo.write(",\n  ".join(lineas))
        archivo.write("\n};\n")
    print("Fuente generada: %s (%d bytes)" % (os.path.normpath(SALIDA), len(datos)))


if __name__ == "__main__":
    main()
