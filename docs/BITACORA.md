# Bitácora del proyecto — Fragile Dreams: Farewell Ruins of the Moon (launcher + motor propio)

Registro de lo que se hizo, qué falló y cómo se resolvió. Lo técnico detallado (direcciones, offsets) está en
`PROYECTO_NOTAS.md`; aquí va la historia en orden.

---

## 1. Launcher (11–14 oct)

| Qué | Problema | Solución |
|---|---|---|
| Fondo animado | Los parpadeos de los personajes no se veían bien; el logo se movía con el agua | Solo se dejó el agua animada y se excluyó el logo de la máscara del agua |
| Logros | Textos en inglés | Traducción al español en `launcher\data\traducciones.json` |
| Pantalla de la correa del Wiimote | Aparecía al arrancar | Parche en main.dol `0x800999F4` (salta directo a la salida de ese estado) |
| Menú de volver al launcher | A y B al revés; F1 para abrirlo; había que hacer clic al reanudar | A/B como en Xbox, tecla Escape, y clic sintético con botón lateral X1 al reanudar (`RESUMEPID` del bridge) |
| Ajustes | A funcionaba como la cruceta | A aplica la opción y pasa a la siguiente fila |
| Transiciones | Se veían fotogramas de la animación de inicio al volver | Velo negro (`gameReady` / `prepReturn` / `gameClosed`) |
| Iconos | Ventana con icono de Dolphin; exe con icono de Electron | Loto en Dolphin (`Sys\Resources`) y en `FragileDreams.exe` (`scripts\06_ICONO_EXE.bat`) |
| Puntero con stick | Relativo: la cámara giraba sola; absoluto: errático; v5: "terrible" | "Mod de mando" por expresiones: 3ª persona tipo cámara, 1ª persona relativa (en Dolphin) |

## 2. Motor propio (15 oct)

- Pedido: que el launcher sea el emulador para poder modificar el juego.
- Se descargó el core libretro de Dolphin (`scripts\09_DESCARGAR_CORE_DOLPHIN.bat`) y se escribió `motor\fd_motor.c`
  (Win32 + OpenGL 3.3 + waveOut + XInput + teclado/ratón), compilado en la nube con clang + mingw sin CRT.
- **Errores de compilación:** `__mingw_fprintf` / `__ms_vsnprintf` sin definir y `pow` importado como dato →
  `__USE_MINGW_ANSI_STDIO=0`, `__ms_vsnprintf` propio y curva polinómica en vez de `pow`.
- **Arranque lento (45 s en negro):** el core compilaba ubershaders y la caché de pipelines en el hilo principal
  (OpenGL en Windows sin contextos compartidos) → modo de shaders 0 (síncrono) y el launcher espera a `STARTED`.
  Resultado: 3,3 s.

## 3. Mods del juego (16 oct)

### Cámara con el stick derecho
- **Problema:** el stick derecho no giraba la cámara: el juego solo la gira cuando el puntero del Wii toca el borde.
- **Investigación:** volcados de memoria con F9 + desensamblado de main.dol. Se localizaron el jugador
  (`0x80309BC0`), el controlador de cámara (`0x80308420`) y las funciones de cámara en 3ª persona (`0x8000B20C`,
  cámara "con correa") y 1ª persona (`0x8000A940`).
- **Solución:** parche `parche_juego_eur.json` con dos ganchos que leen el stick que el motor escribe en una memoria
  compartida (`0x8025ECB0`). 3ª persona: gira el ojo alrededor de Seto y sube/baja su altura; 1ª persona: yaw/pitch.
- **Ajustes:** velocidad vertical en 3ª persona 0,030 → 0,055 → 0,080 m por fotograma (a pedido).

### Movimiento 360°
- **Intento 1:** forzar "girar siempre" en `0x800DC814`. **No bastó:** Seto seguía caminando de espaldas.
- **Causa real:** la rutina de la linterna (`0x800DC900`) giraba el cuerpo hacia la luz en cada fotograma.
  Se anularon esas llamadas.
- **Efecto secundario:** los pies hacían pasos de giro sin parar (la misma rutina pedía la animación de giro) →
  también anulada. Resultado: movimiento 360° limpio.

### Cursor
- **Pedido:** con mando, sin cursor en el menú de inicio. Gancho tras `KPADRead` (`0x80110CAC`) que marca el puntero
  como "fuera de pantalla" cuando el motor lo pide (mando + fuera del juego).
- **Ratón no movía el cursor del juego:**
  1. Primer intento: corregir la salida del puntero leyendo dónde quedaba el cursor → no hizo nada.
  2. El diagnóstico en `motor.log` mostró que el juego nunca recibía el puntero: el parche del mando clásico arma la
     posición del cursor con el stick derecho, y además la dirección del gestor del mando estaba mal calculada
     (`0x8039C6B0`, no `0x803AC6B0`).
  3. Solución: el gancho copia la posición del ratón directamente en `KPADStatus.pos`. Ahora solo se ve el cursor
     del juego (la luciérnaga del launcher no se usa sobre la imagen).

## 4. Ventana, V-Sync y cierre (16 oct)

| Problema | Causa | Solución |
|---|---|---|
| Modo ventana siempre salía en pantalla completa | El launcher comparaba con `'window'` pero el ajuste se llama `'win'` | Modos 1 completa / 2 sin bordes / 0 ventana (ajustada al 90 % del escritorio) |
| V-Sync no hacía nada | El motor siempre usaba intervalo 0 | `--vsync` desde Ajustes |
| En ventana el juego "desaparecía" (0 imágenes) | V-Sync + ventana tapada por el launcher → `SwapBuffers` de NVIDIA bloqueado | Arrancar sin V-Sync y activarlo solo con la ventana al frente |
| Luego corría pero quedaba detrás de otras ventanas | Windows no deja tomar el frente a un proceso en segundo plano | `bring_front()` (unir hilos + TOPMOST) y ventana "siempre encima" mientras está activa |
| Teclado dejó de funcionar | `WM_ACTIVATEAPP` no llega al activar uniendo hilos → foco falso | El foco se calcula con `GetForegroundWindow()` |
| Error JS "Object has been destroyed" al salir | Cerrar el launcher con el juego abierto | Comprobar ventanas destruidas en `onGameExit` |
| Alt+F4 dejaba procesos vivos | Las ventanas ocultas de pausa y logros mantenían la app | Cerrar la ventana principal = salir; el motor se cierra si el launcher desaparece |
| Se llamaba "The Last Story" en el Administrador de tareas | El exe venía del launcher de The Last Story | `scripts\11_NOMBRE_EXE.bat` cambia el nombre interno a "Fragile Dreams" |

## 5. Lecciones

- Diagnosticar con registros antes de adivinar: los dos arreglos que fallaron (cursor, ventana) se resolvieron en
  cuanto el motor anotó los datos reales.
- Revisar las cuentas de direcciones (`lis` + `addi` con número negativo).
- Un cambio que bloquea (V-Sync) puede esconderse detrás de otro síntoma ("la ventana desaparece").

## Pendiente
1. Texturas de botones de Xbox y de teclado/ratón en lugar de las del Wii.
2. ~~Con teclado y ratón: cámara como en Wii~~ hecho (motor v15): cursor cerca del borde = girar; falta probar.
3. Borrar `motor\volcados` (~1 GB) cuando ya no hagan falta.

## 6. Tarde del 16 oct
| Problema | Causa | Solución |
|---|---|---|
| La linterna alumbraba al frente aunque Seto mirara a la cámara | La luz sigue al puntero y con mando quedaba al centro | El motor proyecta un punto delante de Seto y pone ahí el puntero |
| Botones repetidos (A, RT y X atacaban) | El parche del mando clásico manda varios botones al mismo del Wii | Un botón = una acción; RT, X, L3 y R3 libres |
| Cámara con ratón poco moderna | Solo giraba con el cursor en el borde | Mirar con el ratón (cursor capturado), sensibilidad en Ajustes |
| Texturas del Wii en tutoriales | Páginas completas con texto y dibujo del Wiimote | Texto rehecho con letras de la propia página; dibujos de mando Xbox y ratón |
| Seguía la Ⓐ en el aviso de brillo | Era un panel de texto completo (640x120) | Panel editado; packs mando/teclado según el control |
| "El teclado no funciona" | La cinemática solo se salta con +, que estaba en "=" | Enter = +, Tab = −, E = interactuar |
| Discord mostraba negro/imagen fija | Ventana del tamaño exacto del monitor → Windows se salta el compositor | Ventana 1 px más alta |
| 60 FPS por parche del DOL: la escena inicial se trababa | Los eventos cuentan cuadros/tiempos calculados para 30 | Parche retirado (se guarda en `codigos\parche_60fps_eur.json` como referencia) |
| 60 FPS dinámico (solo jugando): Seto dejaba de moverse con WASD | El motor sí recibía las teclas; el juego calcula velocidades al fijar el intervalo y al cambiarlo en caliente el movimiento queda en 0 | 60 FPS desactivado por defecto (30 FPS); queda como opción "experimental" en Ajustes. Pendiente investigar |
| (análisis 60 FPS) | En 0x8001BAA4 el juego solo llama al "cierre de fotograma" de los actores (0x800C4B30: pasa las banderas +14664 a +14668, las limpia y llama vtable[7]) si el intervalo NO es 1. A 60 FPS esas banderas nunca se limpiaban → Seto quedaba bloqueado. El resto de la lógica ya se escala sola (300/fps en ~340 sitios) | Parche v7: nop en 0x8001BAAC (siempre se llama). Además, si KPADRead devuelve 0 muestras se reutiliza la última en vez de borrar la entrada (el motor registra cuántas veces pasa). En prueba |
| 60 FPS: Seto "convulsiona" (torso, cabeza, brazos y piernas) | Capturas F8: en cuadros sueltos el modelo sale con la pose de otra animación (brazo arriba con el palo) y al siguiente vuelve a caminar normal. La lógica está bien: la traza F10 muestra posición y ciclo de pasos suaves. Con doble núcleo la imagen se repetía cada 2-3 cuadros; con un solo núcleo ya sale una imagen nueva por cuadro, pero el fallo sigue → es del propio juego: probablemente reutiliza el búfer de matrices de huesos antes de que la GPU termine de dibujar (a 30 tiene 2 refrescos de margen) | Un solo núcleo con 60 FPS (se queda: imagen nueva cada cuadro). Pendiente: localizar el doble búfer de matrices del juego. 60 FPS sigue siendo experimental |
| (causa real del temblor) | Las animaciones están horneadas a 30 fps (una clave por fotograma) con rotaciones Euler que a veces saltan ±π o ±2π entre claves (p. ej. hueso 28 del ciclo de caminar). A 30 FPS el juego solo evalúa fotogramas enteros (cae justo en las claves); a 60 avanza 0,5 y la interpolación Hermite cruzaba el salto "por el camino largo" → huesos girados media vuelta en cuadros sueltos | Parche v9, gancho en 0x800EE8C4 (muestreo de pistas de rotación): si las dos claves difieren más de 1,5 rad se toma la clave más cercana; si la curva se sale del rango de las claves (tangente arruinada por un salto vecino) se usa interpolación lineal. A 30 FPS el resultado es idéntico |
| Tras v9 aún temblaban un poco brazos y mano | La curva Hermite usa tangentes de las claves vecinas: un salto de ángulo en la clave de al lado la hacía pasarse hasta ~20° aunque el tramo fuera normal | Parche v10: en las pistas de rotación los medios fotogramas se interpolan en línea recta entre las dos claves (las claves son por fotograma, la diferencia no se ve); salto > 1,5 rad → clave más cercana. **60 FPS funcionando** |

### Resumen de los 60 FPS (16 oct)
1. El motor pone el intervalo de imagen (0x80412268) a 1 solo jugando y lo devuelve a 2 en escenas y menús (los eventos cuentan fotogramas).
2. Parche 0x8001BAAC (nop): el juego se saltaba la actualización de los personajes (0x800C4B30 → vtable[7]) con intervalo 1.
3. Parche 0x800EE8C4 (gancho): muestreo de animaciones sin "camino largo" en las rotaciones a medio fotograma.
4. Con 60 FPS el launcher pone el emulador en un solo núcleo (dolphin_main_cpu_thread = disabled): con doble núcleo la imagen se repetía cada 2-3 fotogramas.
5. Herramientas nuevas del motor: F8 = 30 capturas seguidas (volcados\capNN_XX.bmp), F10 = traza del jugador a 60 y a 30 (volcados\traza_NN.bin).

## Pendiente (al 16 oct, tarde)
1. Jugar un rato a 60 FPS por varias zonas: combate, enemigos, escenas, puertas y menús (vuelta a 30), y ver el rendimiento con un solo núcleo.
2. Probar el launcher completo: música, barra de mando, cierre, idiomas fr/de, voz en inglés.
3. Logros: descargarlos y comprobar que salta "Flashlight".
4. Ejecutar scripts\06_ICONO_EXE.bat (launcher cerrado) para el icono del exe.
5. Comprobar que ya no sale la pantalla de la correa del Wiimote.
6. Opcional: instaladores Windows/Linux.
7. Limpieza: borrar motor\volcados (memoria, capturas y trazas, >1 GB).

## 7. Logros (16 oct, tarde)
| Problema | Causa | Solución |
|---|---|---|
| Ningún logro saltaba (progreso de recuerdos en 0) | El set oficial de RetroAchievements ya trae, en cada logro, un grupo alternativo para la versión europea (comprueba 0x80000000 = 'R2GP') con sus propias direcciones. El launcher traducía TODO de USA a EUR, así que ese grupo quedaba desplazado dos veces (+0x740 de más) y leía memoria equivocada | main.js v30: si el logro ya trae el grupo PAL se usa tal cual; la traducción queda solo de respaldo. Comprobado con un volcado de memoria: la zona EUR (0x803083F7) vale 19 = observatorio; la doble traducción leía 0 |
