# Uniracers recomp — propuestas de mejora

Fecha: 2026-10-22. Estado de partida: panorámico funcionando en 1P hasta 512 px.
Corregidos estos días:

- el fantasma del rival;
- las pistas con zoom;
- el sprite aparcado en ultrapanorámico;
- la condición de carrera en los tours 2 y siguientes;
- la tabla circular en X.

Cada propuesta indica **esfuerzo** (bajo / medio / alto) y si **toca la
simulación**. Por defecto ninguna la toca: todo es presentación.

---

## Parte A — Fiabilidad y forma de trabajar

Los fallos de estos días estaban en sitios que nadie había probado:

- pistas con `$03ED ≠ 0`;
- tours después del primero;
- la pista circular;
- anchos por encima de 16:9.

Todas las validaciones automáticas recorrían solo las 5 pistas del tour 1 y
casi siempre a 16:9.

### A1. Validación de las 45 pistas — esfuerzo medio, prioridad máxima

- Una partida de prueba (SRAM) con todos los tours desbloqueados, guardada en
  `work/` y copiada al directorio de prueba en cada ejecución.
  **Hecho (2026-10-23):** `srm.py unlock` la genera
  (`work/unlock-tours/a1-unlocked.srm`); ver `docs/UNIRACERS_SAVE_FORMAT.md`.
- Un recorrido de menú que llegue a cualquier pista: PICK TRACK con el
  selector de nivel (Bronze/Silver…) y el de tour.
- Para cada pista, la sonda BG1 (`world_key_check.py`) y el registro de sprites
  de los dos monociclos (fantasma, aparcado, `$03ED`, ventana de descarte).
- Un informe en tabla: pista × ancho → celdas incorrectas, celdas tapadas,
  caídas del ancla y correcciones del sprite.

### A2. Matriz de anchos — esfuerzo bajo

Toda validación debe pasar por 16:9, 21:9, 2,39:1 y 512 px.

### A3. Script único de validación con referencias — esfuerzo bajo

**Hecho (2026-10-23):** `game/tools/validate.sh` ejecuta nativo, 16:9, 21:9,
512 px, las políticas `wrap`/`ring`/`source`/`ghost`/`clamp`, Monster,
Zoom Zoo y los logs, y compara contra `game/tools/validate_refs.txt`.
`--record` hace doble ejecución y marca `NONDET` cuando el frame no es
estable, cayendo a la puntuación de `world_key_check.py`.

- El hash histórico `4b138431…` es la política `wrap`; el 16:9 por defecto
  (`source`) es `fa228fef…`. Nativo sigue siendo `3417f91a…`.

### A4. Pruebas que no toquen la partida del usuario — esfuerzo bajo

**Hecho (2026-10-23):** `validate.sh` copia el ejecutable a un directorio
temporal con `saves/` vacío y un `config.ini` generado; no lee ni escribe
`game/build-local/saves/` ni el `config.ini` del usuario, y comprueba su
huella al final.

### A5. Volcado de diagnóstico con una tecla — esfuerzo medio

- Una tecla que escriba a un fichero los últimos ~100 s del historial del
  juego: monociclos, cámara, sprites, `$03ED`, `$0B4/$0300` y el estado del
  modo `source`.
- El jugador la pulsa al ver algo raro y sigue jugando. Así no hace falta
  reproducir el fallo ni coordinarse en directo, como pasó con el rival que
  parpadeaba.
- Hoy existe `work/ws-ghost-xhigh/gate/grab.py`, que necesita el juego abierto y
  el puerto 4377.

### A6. Opciones en `config.ini` — esfuerzo bajo

Una sección `[Widescreen]` con:

- `Aspect = auto | 16:9 | 21:9 | …`;
- `MaxWidth`;
- los interruptores que tenga sentido tocar como usuario.

Las variables `SNESRECOMP_WS_*` quedan para depuración.

### A6b. Menú de opciones dentro del juego — esfuerzo bajo/medio (requiere A6)

Un menú superpuesto que se abre con una tecla, congela el juego, cambia
opciones en caliente y las guarda en `config.ini`:

```
┌──────────── Panorámico ────────────┐
│ ▸ Proporción         auto  ◂ ▸     │
│   Ancho máximo        512          │
│   Rival en margen      ON          │
│   Margen de pista    source        │
│   Marcador ancho       ON          │
│   Indicador rival     OFF          │
│   RunAhead              1          │
│        [Guardar en config.ini]     │
└────────────────────────────────────┘
```

- **Base existente:**
  - el menú de estados guardados (F11) ya es un menú dibujado encima del
    juego (`snesrecomp/runner/src/snes_savestate_menu.c`, ~740 líneas);
  - la capa de dibujo es `snes_overlay_draw.c` (~200 líneas).
- **Requisito:** A6. Hoy las opciones se leen con `getenv` en cada
  fotograma, repartidas por `game_rtl.c`/`main.c`. Hay que reunirlas en una
  estructura de ajustes que el menú modifique.
- **En caliente:**
  - casi todo es solo dibujo: proporción y ancho (el ancho ya se recalcula
    cada fotograma), rival en margen, modo de pista y marcador;
  - la ventana de descarte escribe `$0421/$0423`. Al desactivarla restaura el
    valor original, pero hay que probar el cambio en mitad de una carrera.
- **Alternativa:** el lanzador `recomp-ui` (Dear ImGui). Se reabre en partida
  con Ctrl+L y aplica en caliente o reinicia con un estado guardado.
  - Este proyecto se creó con `--no-recomp-ui`: habría que añadir el
    submódulo.
  - Falta comprobar si admite páginas propias del juego; su documentación
    solo cubre pantalla, sonido y mandos.
  - Compatible con el menú propio: el lanzador para lo general, el menú para
    el panorámico.

### A7. Orden del proyecto — esfuerzo bajo

- **git** en el árbol de trabajo
  (`work/ws-native-map-spike/project/UniracersSNESRecomp`). Hoy no hay
  historial: los cambios se guardan como `.diff` a mano.
- Retirar las copias obsoletas (`work/ws-offwindow/project`,
  `work/ws-map-decode/project`) o marcarlas como de solo lectura.
- OpenSpec:
  - archivar los cambios terminados (hay 8 abiertos);
  - separar de `uniracers-ws-ghost-xhigh` lo que no es del rival (condición de
    carrera, tabla circular) en sus especificaciones.

### A8. Calidad del código — esfuerzo bajo

**Hecho (2026-10-23):** `game_rtl.c` baja a ~130 líneas y el puerto se divide
en `ws_frame`, `ws_hud`, `ws_cull`, `ws_oam_fix`, `ws_ring`, `ws_source`,
`ws_radar`, `ws_ghost_lane` y `ws_debug`. `WsInLiveRace`/`WsWideEnabled` viven
solo en `ws_frame.c`; `variables.h` nombra las direcciones y el cull es el
único escritor de WRAM en producción.

- Direcciones nombradas en `variables.h` (el antiguo `uniracers_ram.h`):
  - `$0411/$0413` X de los monociclos;
  - `$0419/$041D` cámara;
  - `$03ED` escala de la ventana de descarte;
  - `$0421/$0423` ventana de descarte;
  - `$1501–$1510` y `$1599` OAM;
  - `$0B4`, `$0EE9`, `$0300`;
  - `$7F:000F/800F` tabla de pista.

---

## Parte B — Pendientes del panorámico

| # | Pendiente | Esfuerzo | Nota |
|---|---|---|---|
| B1 | El rival aparece un instante donde no toca (tour 2 pista 1) | — | Aparcado hasta capturarlo con A5. Hipótesis en el `report.md` de `uniracers-ws-ghost-xhigh`. |
| B2 | Pasar de 512 px (hasta ~800, el límite del renderizador) | medio | Decidir el lado de los sprites por la posición en WRAM (`PpuWsSetOamLeftHints`). Antes, una prueba de cómo se ve BG2 repetido (su mapa mide 512 px). |
| B3 | La cuenta atrás de salida puede verse en 4:3 un instante | bajo | Lo apuntó la validación de `$0300`; nadie lo ha revisado. |
| B4 | 2 jugadores (pantalla partida horizontal) | alto | Fuera de alcance hasta ahora; ver C8. |

---

## Parte C — Experiencia de juego, cuando todo funcione

Ideas para que la versión recompilada sea *mejor* que jugar en la consola, sin
cambiar el juego.

### C1. Margen asimétrico hacia donde vas — esfuerzo medio

- En una carrera rápida importa ver lo que viene, no lo que dejas atrás.
- Con la velocidad del jugador se puede repartir el margen: más píxeles por
  delante y menos por detrás, con una transición suave al cambiar de sentido.
- La cámara del juego no se toca. Solo se mueve el recorte del cuadro ancho,
  sobre el que ya controlamos los márgenes.
- Riesgo: mareo si la transición es brusca. Debe poder desactivarse.

### C2. Indicador del rival fuera de pantalla — esfuerzo bajo/medio

- Una flecha pequeña en el borde, a la altura del rival, con su distancia
  ("+3,2 s" o un tamaño que cambie con la distancia).
- Tenemos la posición real del rival en memoria (`$0413/$0417`).
- Se dibuja como objeto extra con `PpuSetExtraObjects`, que ya existe en el
  framework.

### C3. Fantasma de tu mejor vuelta — esfuerzo medio, mucho valor

- Grabar posición, pose y sprite del jugador en cada fotograma de tu mejor
  carrera en cada pista, y en la siguiente dibujarlo como un monociclo
  translúcido (`PpuSetExtraObjects`).
- No toca la simulación: es solo dibujo a partir de datos grabados.
- Se combina bien con C4.

### C4. Tiempos y estadísticas por pista — esfuerzo bajo

- Mejor tiempo, mejor vuelta y parciales guardados por pista en un fichero
  junto al ejecutable.
- En carrera, la diferencia contra tu mejor tiempo en cada parcial.

### C5. Reinicio rápido y práctica — esfuerzo bajo

- Una tecla para "reiniciar esta carrera" (un estado guardado automático en la
  salida) y otra para "repetir este tramo" (un estado guardado manual).
- Comprobar si el rebobinado del framework (`snes_rewind`) ya cubre parte de
  esto.

### C6. Menos latencia — esfuerzo bajo (validar)

- `RunAhead` existe en `config.ini` y está a 0.
- Validar que con 1–2 fotogramas el panorámico sigue bien: el modo `source` y
  el registro de sprites usan estado por fotograma. Ofrecerlo como opción
  recomendada.

### C7. Más fluidez en monitores de 120/144 Hz — esfuerzo alto

**Por qué.** El juego calcula 60 fotogramas por segundo y eso no se puede
cambiar sin cambiar su velocidad. Uniracers es casi todo desplazamiento
horizontal rápido sobre fondos de mucho contraste. En la grabación de
Dragster el rival se movía unos 14 px por fotograma. A 60 Hz eso se ve como
tirones y estela en un LCD u OLED; a 120 Hz los saltos serían de unos 7 px.
Se notaría como pasar un juego moderno de 60 a 120.

**Cómo: interpolación solo de dibujo.** No es la interpolación genérica de
las teles, que estropea el pixel art. Aquí sabemos exactamente qué se mueve:

```
fotograma N ───── intermedio (t) ───── fotograma N+1
scroll BG1/BG2   s + t·(s' − s)
jugador, rival   x + t·(x' − x)
pose, HUD, paleta → los del fotograma N
```

- En carrera solo se mueven el scroll de la pista (BG1), el del fondo (BG2)
  y los dos monociclos (slots 98/99, posiciones en WRAM).
- BG1 se dibuja desde la tabla completa del modo `source`, así que en un
  intermedio no falta ninguna celda.
- A esa velocidad los pasos son de píxeles enteros; no hacen falta
  subpíxeles.
- No toca la simulación.

**Problemas y soluciones:**

| Problema | Solución |
|---|---|
| Hace falta el fotograma siguiente: +1 fotograma de latencia (~17 ms) | Combinar con `RunAhead = 1` (C6), o extrapolar (falla en cambios de dirección) |
| Saltos de cámara (caídas, reinicio de vuelta) | No interpolar si el salto es grande |
| Scroll distinto por línea (paralaje de BG2) | Interpolar por línea; el renderizador ya trabaja así |
| 144 Hz no es múltiplo de 60 | Interpolar con la fracción de tiempo exacta de cada refresco |
| El juego va a 60,1 Hz y el monitor a 60/120 exactos: tirón ocasional | VRR (G-Sync/FreeSync) |
| Renderizar 2–2,4 veces por fotograma de juego | Hoy el fotograma cuesta ~6 ms y el dibujo es una parte: hay que medirlo. El carril fantasma fue lento por guardar y restaurar estado, no por dibujar. |

**Alternativa barata: inserción de fotograma negro (BFI).**

- A 120 Hz se alterna cada fotograma real con uno negro.
- Reduce la estela de movimiento sin inventar fotogramas; esfuerzo bajo.
- Cuesta brillo y hay quien nota parpadeo.
- No da más fluidez, sí más nitidez en movimiento.

**Plan:**

1. Prueba rápida a 120 Hz: interpolar solo el scroll de BG1/BG2 y la X/Y de
   los dos monociclos. Comparar con 60 Hz en un monitor real y medir el coste.
2. Si convence: casos límite (saltos, paralaje, 144 Hz con fracción) y opción
   en `config.ini`, desactivada por defecto.
3. BFI como opción independiente.

### C8. Panorámico en 2 jugadores — esfuerzo alto

- La pantalla partida horizontal (arriba y abajo) aprovecha mal una pantalla
  ancha.
- Primer paso: ensanchar cada mitad igual que en 1P.
- Más ambicioso: componer las dos mitades una al lado de la otra.

### C9. Transiciones entre menú y carrera — esfuerzo bajo/medio

- Hoy el cuadro salta de 256 a 342+ px al entrar en carrera.
- Opciones:
  - animar el ancho unos fotogramas;
  - mantener el ancho en los menús con un borde decorativo (el patrón de
    caracoles del menú repetido o difuminado) en lugar de barras negras.

### C10. Presentación y shaders — esfuerzo bajo

**Ya disponible.** El host carga presets GLSL de estilo libretro/Snes9x
(`.glslp`, varias pasadas y parámetros; `snesrecomp/runner/src/desktop/glsl_shader.c`).
Se activa en `config.ini`:

```ini
OutputMethod = OpenGL     ; hoy: SDL
Shader = ruta/al/preset.glslp
```

Los presets salen del repositorio `glsl-shaders` de libretro (variante GLSL).

**Problema que resuelven: titileo al desplazarse.**

- Las 224 líneas se escalan por un factor no entero (×6,43 a 1440 px de alto),
  y el píxel 4:3 (`DisplayAspect = 4:3`) añade ×8/7 en horizontal.
- Con vecino más cercano (`LinearFiltering = 0`) unas columnas salen más
  anchas que otras, y en el desplazamiento rápido de Uniracers titilan.

**Recomendación, por orden:**

1. **`sharp-bilinear` / `pixellate`** (ligerísimo): escala entera con vecino
   más cercano y suavizado solo del resto. Píxeles nítidos e iguales, sin
   titileo. Primera opción y posible valor por defecto.
2. **CRT ligero**: `crt-lottes`, `crt-easymode` o `crt-guest-advanced` (este
   último pide más GPU).
   - El arte prerrenderizado (monociclos, tubos sombreados, degradados) gana
     con líneas de barrido y brillo.
   - **Sin curvatura** con panorámico: una tele curvada a 21:9 queda rara.
   - `crt-royale`: vistoso pero pesado, pensado para 4K.
3. **No recomendados:** `xBRZ` y `ScaleFX`. Suavizan las curvas de la pista,
   pero los sprites prerrenderizados y los degradados quedan como manchas.
   Funcionan con pixel art dibujado a mano.

**A vigilar:**

- No se ha probado nunca `OutputMethod = OpenGL` con el panorámico: hay que
  comprobar el ancho automático por ventana, el marcador y los márgenes.
- Los shaders actúan sobre la imagen final ensanchada; no afectan a la lógica
  del panorámico.
- Ningún shader quita la estela del LCD a 60 Hz; eso es C7 (120 Hz / BFI).

**Pasos:**

1. Bajar `sharp-bilinear` y `crt-lottes` a una carpeta del proyecto.
2. Validar el panorámico con OpenGL.
3. Dejar los presets listos en `config.ini` y, con A6b, poder elegirlos desde
   el menú.

**Otro ajuste de presentación:** elegir dónde va el marcador, en las esquinas
del cuadro ancho (como ahora) o en su sitio original.

### C10b. Shaders solo en una capa (p. ej. los tubos de la pista) — esfuerzo medio

Un preset de libretro solo ve la imagen final ya mezclada, sin saber qué
píxel es pista, fondo o sprite. El renderizador sí lo sabe, porque compone
las capas:

- BG1: la pista (tubos, bordillos, flechas);
- BG2: el fondo;
- BG3: el marcador;
- OBJ: los monociclos.

```
renderizador ──► imagen final  ──┐
             └─► máscara de capa ─┴─► shader propio: efecto donde máscara == BG1
```

**Pasos:**

1. **Máscara de capa:** al componer cada línea, anotar qué capa ganó en cada
   píxel. El compositor de márgenes del panorámico ya vuelve a dibujar líneas
   aisladas por capa, así que la información está disponible.
2. **Segunda textura** en el camino de OpenGL del host, junto a la imagen.
3. **Shader propio**, no un preset de libretro tal cual, que lea la máscara.
4. Más fino, si hace falta (tubos sí, flechas y bordillos no): filtrar por
   paleta o por número de tile. Las piezas de cada pista se conocen por la
   tabla `$7F`.

**Efectos con sentido en los tubos:**

- suavizado (xBRZ/ScaleFX) **solo en BG1**: curvas de tubo limpias sin
  estropear los monociclos prerrenderizados, que es el problema de esos
  filtros sobre toda la imagen (C10);
- brillo o reflejo especular que recorra el tubo, o realce del volumen del
  degradado;
- un glow suave para que la pista destaque sobre fondos cargados.

**Alternativa: tiles en alta resolución.** Sustituir los gráficos de los
tubos por versiones dibujadas en alta resolución, a modo de "paquete de
texturas". Encaja porque la pista se construye con piezas conocidas. Da el
mejor resultado, pero el trabajo es sobre todo de arte.

**Requisitos y coste:**

- `OutputMethod = OpenGL`, como en C10;
- código del framework (la máscara en el renderizador), un cambio en el host
  (el paso a OpenGL) y un shader propio.

### C11. Accesibilidad y comodidad — esfuerzo bajo

- Remapeo y perfiles de mando (incluido Steam Deck).
- Una opción para desbloquear todos los tours sin la partida guardada, para
  jugar o practicar cualquier pista. También es útil para A1.
  **Hecho (2026-10-23):** `[Emulation] UnlockAllTours = 1` sube a 3 el byte
  `$10D3` de la partida (permanente); cambio `uniracers-unlock-tours`.

---

## Parte D — Nuevas ideas (2026-10-22)

Ideas que no están en A/B/C. La columna de simulación indica si haría falta
tocar el juego, no solo la presentación.

| # | Idea | Tipo | Esfuerzo | ¿Simulación? |
|---|---|---|---|---|
| D1 | **Telemetría en vivo (OSD)**: scroll, cámara, ancla de `source`, `$03ED`, cull, OAM y frame time; tecla para mostrar/ocultar | Diagnóstico | Bajo/medio | No |
| D2 | **Grabador/reproductor de inputs** (`.movie` con SRAM + inputs + hashes) para reproducir un fallo exacto o compartir una partida | Herramientas | Medio | No |
| D3 | **Matriz CI local**: 45 pistas × 16:9/21:9/2,39:1/512, hashes y sonda; el ROM queda fuera del repo | Calidad | Bajo/medio | No |
| D4 | **Galería de pistas**: forma, índice, nombre y récord de las 45 | Contenido/UX | Bajo/medio | No |
| D5 | **Barra de progreso para pistas largas** (Dragster/Switcher 1024×16): tira de 128×16 con salida, meta, jugador y rival | Radar/UX | Bajo/medio | No |
| D6 | **Radar con zoom/orientación y marcador del fantasma** de tu mejor vuelta | Radar/UX | Medio | No |
| D7 | **HUD extendido**: velocidad, parciales, delta contra tu mejor vuelta, posición de vuelta | UX | Bajo/medio | No |
| D8 | **Jukebox/SPC + sound test**: escuchar la música de las pistas y probar efectos | Audio | Bajo/medio | No |
| D9 | **Photo mode / replay theater**: congelar, ocultar HUD, zoom y captura; reproducir una carrera grabada | Presentación | Medio (cámara libre: alto) | No |
| D10 | **Paletas/CRT/colorblind** y preset de “reducir movimiento” (sin margen asimétrico ni interpolación) | Accesibilidad | Bajo | No |
| D11 | **Online 2P / leaderboards / ghosts compartidos** | Online | Alto | No (netplay ya existe) |
| D12 | **Práctica con checkpoints**: reiniciar desde una curva, sin rival, vueltas infinitas | Juego | Medio/alto | Estado/save states |
| D13 | **Editor/randomizer de pistas** usando el mapa ya decodificado | Contenido | Alto | Datos/colisiones |
| D14 | **Modularizar `game_rtl.c`** en `widescreen`, `radar`, `oam_fix`, `source`, con un `UniracersSettings` único | Código | Bajo/medio | Hecho salvo `UniracersSettings` (siguiente cambio) |
| D15 | **Skins/paletas de los monociclos** vía shader o reemplazo de color | Mods | Bajo/medio | No |
| D16 | **Gestor de save data**: backup/restore, import/export y estados por pista | Calidad de vida | Bajo | No |

**D, por valor/esfuerzo:**

1. **D1 + D2** (telemetría y movies): convierten un fallo raro como B1 en algo
   reproducible sin depender de una sesión en directo.
2. **D3 + D4** (CI y galería): validan las 45 pistas a varios anchos y hacen
   visible el contenido ya decodificado.
3. **D5 + D6** (progreso y radar): el radar local de 32×32 es contexto; en
   Dragster/Switcher una tira de progreso y el fantasma aportan más.

**Ambiciosas:** D11 (online), D9 (photo/replay) y D13 (editor/randomizer).
D12 y D13 son las que más se acercan a tocar lógica de juego; D11 depende de
que netplay y rollback estén validados en este título.

---

## Orden recomendado

1. **A1 + A2 + A4.** Validar las 45 pistas a varios anchos sin tocar tu
   partida. Habría cazado todo lo de estos días.
2. **A5.** El volcado con una tecla, para los fallos raros como B1.
3. **A6, A6b, A7, A8.** Opciones en `config.ini` y su menú dentro del juego,
   git y limpieza; poco esfuerzo.
4. Experiencia: **C2** (indicador del rival) y **C4** (tiempos) por su
   relación valor/esfuerzo, luego **C3** (fantasma de tu mejor vuelta) y
   **C1** (margen hacia delante).
5. Lo ambicioso: B2 (más de 512 px), C7 (120 Hz) y C8 (2 jugadores).
6. Nuevas (Parte D): D1/D2 (telemetría y movies), D3/D4 (CI y galería),
   D5/D6 (progreso y radar). Después, D11/D9/D13 si apetece lo grande.
