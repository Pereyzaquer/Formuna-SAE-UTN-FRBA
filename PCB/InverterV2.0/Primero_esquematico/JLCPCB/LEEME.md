# Paquete de fabricación — JLCPCB

Generado desde `Primero_esquematico.kicad_pcb` con KiCad 10.
DRC al momento de generar: **0 errores, 0 elementos sin conectar**.

## Qué subir

| Archivo | Dónde va en JLCPCB |
|---|---|
| `GERBERS-Primero_esquematico.zip` | Botón **Add gerber file** (pedido de PCB) |
| `BOM-...csv` (elegí cuál, ver abajo) | Pestaña **SMT Assembly** → Add BOM File |
| `CPL-Primero_esquematico.csv` | Pestaña **SMT Assembly** → Add CPL File |

La carpeta `gerbers/` es el contenido del zip sin comprimir, por si lo querés revisar en un visor.

## Parámetros del pedido de PCB

| Campo | Valor |
|---|---|
| Base Material | FR-4 |
| **Layers** | **6** |
| Dimensions | **225,0 × 145,1 mm** |
| Via Covering | según tu criterio (Tented es lo habitual) |
| Min via / drill del diseño | 0,45 mm Ø / 0,25 mm taladro |
| Clearance mínimo del diseño | 0,127 mm |

El zip incluye las 6 capas de cobre, ambas máscaras, ambas serigrafías, ambas pastas,
el contorno (`Edge_Cuts`), los taladros PTH y NPTH por separado y sus mapas.
La serigrafía ya sale recortada sobre las aperturas de máscara.

## Dos BOM: elegí uno

La columna `LCSC Part #` **ya está completa en los dos**. Todos los números de parte
se verificaron contra el catálogo de JLCPCB: valor, encapsulado, tensión, dieléctrico,
tolerancia y stock real.

### `BOM-Primero_esquematico.csv` — todo (59 líneas, 316 componentes)

JLCPCB compra y coloca absolutamente todo.

- 31 líneas **Basic** (sin cargo de setup)
- 28 líneas **Extended** → **~US$84** de cargo de setup

### `BOM-JLC-solo-faltantes.csv` — solo lo que no tenés (30 líneas) ← recomendado

Deja afuera las 29 líneas que **ya compraste en DigiKey** (orden 101011610) y que cumplen
el requisito de tensión.

- 11 líneas Basic + 19 Extended → **~US$57** de setup
- Ahorro: **~US$27** de setup, más no volver a pagar componentes que ya tenés

Las 29 líneas excluidas las soldás vos con lo que ya compraste.

### Por qué conviene el reducido, más allá del precio

Diez de los valores que compraste son **±0,1 %**, y JLCPCB los reemplazaría por
genéricos **±1 %**:

| Valor | Pkg | Cant | Lo que compraste |
|---|---|---|---|
| 100 Ω | 0402 | 6 | ERA-2AEB101X |
| 100 k | 0402 | 1 | CPF0402B100KE1 |
| 20 k | 0402 | 1 | RT0402BRD0720KL |
| 220 k | 0402 | 3 | RT0402BRD07220KL |
| 37,4 k | 0402 | 7 | ERA-2AEB3742X |
| 4,42 k | 0402 | 8 | ERA-2AEB4421X |
| 7,68 k | 0402 | 8 | ERA-2AEB7681X |
| 820 Ω | 0402 | 3 | RG1005P-821-B-T5 |
| 100 k | 0805 | 24 | ERA-6AEB104V |
| 2,2 k | 0805 | 4 | ERA-6AEB222V |

Son los divisores y las resistencias de ganancia de las cadenas de medición. Pasar de
0,1 % a 1 % multiplica por diez el error sistemático de esas medidas.

`BOM-detalle.md` tiene la tabla completa línea por línea con marca, especificaciones,
Basic/Extended y si ya la tenés comprada.

## HAY QUE CORREGIR EL ESQUEMÁTICO: cuatro valores

Estos valores del esquemático no se pueden comprar como están. En los dos BOM ya puse
el valor corregido, pero **el esquemático sigue diciendo el valor viejo**.

| Esquemático | Designadores | Corregido a | Por qué |
|---|---|---|---|
| **246k** | R20, R34, R45, R60, R78, R89, R158 | **249k** | 246k no existe en JLCPCB en 0402, 0603 ni 0805. Y en DigiKey vos compraste **249k** (RC0402FR-07249KL), así que el esquemático es el que está desactualizado |
| **37k** | R21, R35, R46, R61, R79, R90, R159 | **37,4k** | 37k no es un valor E96. Compraste **37,4k** (ERA-2AEB3742X) |
| **5k** | R164, R169 (0402) · R14, R15, R16 (0603) | **4,99k** | 5k exacto solo existe como Extended y caro. 4,99k es E96, es Basic en 0603, y es lo que compraste (ERJ-2RKF4991X) |
| **4,8uF** | C19, C25, C36, C42, C85, C91 | **4,7uF** | 4,8 µF no existe en ninguna serie |

Las tres primeras cambian la relación de divisores en las cadenas de medición. Como son
cambios de entre 0,2 % y 1,2 %, y las resistencias de alrededor son ±1 %, el efecto se
calibra en firmware — pero **confirmá que es lo que querés antes de fabricar**.

## Capacitores: todos a 50 V, salvo dos que no existen

**18 de las 20 líneas de capacitores quedaron a 50 V o más.** Las dos que faltan no se
pueden: en 0603 esas capacidades simplemente no se fabrican a 50 V.

| Línea | Designadores | Máximo que existe en 0603 | Parte elegida |
|---|---|---|---|
| `10uF` 0603 | C113 | **35 V** | `C22367827` 35 V X5R |
| `4,7uF` 0603 | C19, C25, C36, C42, C85, C91 | **35 V** | `C48543506` 35 V X5R |

Verifiqué el catálogo entero: en 0603 las tensiones disponibles para 10 µF y 4,7 µF son
35 V, 25 V, 16 V, 10 V y 6,3 V. No hay 50 V. Es un límite físico del encapsulado.

Si querés 50 V sí o sí en esos siete componentes, **hay que cambiar el footprint a 0805**
en la placa:

- `10uF` 0805 50 V → `C440198` (Murata GRM21BR61H106KE43L, **Basic**)
- `4,7uF` 0805 50 V → `C7393930` (CCTC, Extended)

Mientras tanto: C113 va a +3,3 V, así que 35 V son más de 10× de margen. Los seis de
4,7 µF van tres a 5V_ISO y tres a +15 V; 35 V sobre 15 V son 2,3×, aceptable.

### El costo de subir a 50 V

Aplicar la regla movió cuatro líneas y encareció el pedido:

| Línea | Antes | Ahora | Efecto |
|---|---|---|---|
| `1uF` 0402 | C52923, 25 V, Basic | `C20539426`, 50 V | **deja de ser Basic** (+US$3) |
| `220nF` 0402 | C915854, 25 V | `C50767368`, 50 V | sigue Extended |
| `2.2uF` 0603 | C48986296, 25 V | `C7432769`, 50 V | sigue Extended |
| `10uF` 0805 | C15850, 25 V, Basic | `C440198`, 50 V, Basic | sigue Basic, pero US$0,13 c/u en vez de US$0,026 |

**A 50 V casi no hay X7R en estos tamaños, solo X5R.** Lo verifiqué uno por uno: `1uF`
0402, `220nF` 0402, `2.2uF` 0603, `10uF` 0805, `10uF` 0603 y `4,7uF` 0603 **no tienen
ninguna opción X7R** a la tensión pedida. X5R aguanta hasta 85 °C y X7R hasta 125 °C: si
la placa va a trabajar por encima de 85 °C, esto importa más que la tensión. La única
excepción es `1uF` 0603, donde sí hay X7R 50 V (`C6119857`, Extended) además del X5R
50 V Basic que elegí (`C15849`).

## Tres cosas más que conviene mirar

**Tres capacitores que compraste son de 25 V y ya no califican.** Con la regla de 50 V
pasaron al BOM de JLCPCB: `220nF` 0402 (C1005X7R1E224K050BE), `47nF` 0402
(GRM155R71E473KA88D) y `10uF` 0603 (C1608X5R1E106M080AC, aunque acá el techo es 35 V
igual). También el `4,7uF` que compraste es de 25 V **y además en 0805**, no en 0603.

**`10uF 0805`, 6 unidades.** Compraste 10 µF en 1206 y en 0603, ninguno en 0805. Van en el
BOM reducido para que los compre JLCPCB (`C440198`, Basic, 50 V).

**`1k 1/8W` (R4, 0603) y `220 1/4W` (R180, 0805)** piden más potencia que la estándar del
encapsulado (0603 es 1/10 W, 0805 es 1/8 W). Las partes que cumplen existen pero son
Extended y bastante más caras (`C160009` y `C441976`). Son **una unidad cada una**: pagar
~US$6 de setup por dos resistencias no cierra. Conviene sacarlas del ensamblado y
soldarlas a mano, salvo que verifiques que el encapsulado estándar alcanza.

## Dos sustituciones de dieléctrico que elegí yo

Para `1nF` en 0402 (C7, C8, C141-C144, C159) y en 0603 (C133-C140) hay **Basic en X7R**
pero no en C0G. Usé X7R y me ahorré dos cargos de Extended. X7R tiene peor coeficiente de
temperatura y de tensión que C0G, así que la frecuencia de corte de esos filtros se mueve
un ~10-15 % con la temperatura. Para filtros de antialias en las señales de sensado es
aceptable; si querés C0G sí o sí, los códigos son `C237166` (0402) y `C106246` (0603),
los dos Extended. Los dos son de 50 V.

## Notas

- Las rotaciones del CPL salen de KiCad. Como el ensamblado es solo de resistencias y
  capacitores — todos simétricos y sin polaridad — un error de rotación no tiene
  consecuencias. Si más adelante agregás integrados o diodos, ahí sí hay que verificar
  la convención de rotación de JLCPCB parte por parte.
- El origen de coordenadas del CPL y de los taladros es el mismo (absoluto, sin origen
  auxiliar definido), así que las posiciones coinciden con los gerbers.
- Se incluyen solo R y C de 0402, 0603 y 0805, como pediste. Quedan afuera los
  capacitores 1206 (6), 1210 (19), 2 WCAP-PSLC y las resistencias 1206 (12), más
  integrados, conectores, diodos, inductores y el módulo de potencia.
- El cargo de setup de Extended es de ~US$3 por número de parte distinto. Las cuentas de
  arriba usan ese valor; verificá el actual al cotizar.
