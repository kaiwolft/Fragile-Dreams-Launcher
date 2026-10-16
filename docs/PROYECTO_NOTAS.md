# Fragile Dreams: Farewell Ruins of the Moon — Proyecto launcher (notas para retomar)
Última sesión: 11–12 de octubre de 2026 (+ 12 oct noche: fondo con agua animada) (Dionixu's Launcher, basado en The Last Story y Pandora's Tower)

## Estructura (D:\PROYECTOS\Proyecto Fragile Dreams Farewell Ruins of the Moon)
- `Dolphin-x64\` Dolphin 2609a portable (`portable.txt`). Config en `User\Config`.
- `extraido\EU\` disco europeo R2GP99 extraído (DolphinTool). Archivos planos `DATA\files\FILE_xxxxxxxx` + `index.bin`.
- `juego\EU_ORIGINAL\` (enlaces duros al extraído) y `juego\EU_ESPANOL\` (enlaces + 68 archivos de la traducción + su index.bin).
  `main.dol` de ambas NO está enlazado: el launcher lo regenera en cada arranque.
- `juego\dol\original.dol` (EU) y `juego\dol\espanol.dol` (main.dol del parche, también EU: solo cambian textos de error del mando).
- `FragileDreamsESP11\` traducción al castellano 1.1 (xulikotony, CUE, TheKiller, Klint, Pacochan, GameZelda, IlDucci). Es para PAL.
- `codigos\classic_controller_vague_rant.txt` Gecko USA/EUR (GBAtemp, Vague Rant) · `codigos\parche_mando_eur.json` versión integrada.
- `launcher\` Electron (motor copiado de Pandora's Tower, `FragileDreams.exe`). `resources\app\main.js|preload.js|bridge.ps1|fd_logros.exe`,
  `ui\index.html|exit.html|toast.html`, `ui\assets\` (bg.jpg, lotus.png, icon.ico/png, cursor*.png, music.mp3), `ui\fonts\` (Cinzel, Cormorant),
  `data\config.json`, `data\parche_mando_eur.json`, `data\mapa_usa_eur.json`, `data\logros\` (set y progreso, se crean al descargar).
- `scripts\01..05` pasos reproducibles · `herramientas\*.py` análisis (ver abajo) · `para_claude\` copias para analizar (EU en `US\` por error de nombre, USA en `US_REF\`).
- Accesos directos: `FRAGILE DREAMS.lnk` en la carpeta y en el escritorio.

## Scripts
1. `01_EXTRAER_JUEGO` extrae el RVZ (se ejecutó con el europeo; quedó en extraido\US y el paso 2 lo renombra a EU).
2. `02_PREPARAR_JUEGO` crea juego\EU_ORIGINAL y EU_ESPANOL, copia los dol base.
3. `03_PROBAR` prueba sin launcher: texto, voz, método de mando (1 integrado = el bueno), control (mando/teclado).
4. `04_INSTALAR_LAUNCHER` copia el motor Electron de Pandora's Tower y crea accesos directos.
5. `05_DOL_USA_PARA_LOGROS` saca main.dol USA (R2GEXJ) a para_claude\US_REF (para el mapa de direcciones).

## Idiomas (verificado por el usuario: texto español + voces japonesas OK)
- Estructura en 0x80326208: +0x10 texto (0 japonés, 1 inglés, 3 francés, 5 alemán), +0x14 voz (1 inglés, 0 japonés; supuesto, falta confirmar inglés).
- Parches en main.dol (iguales en original y español):
  0x80056AEC 2C000000→48000028 (saltar idioma del Wii) · 0x80056B14 li r0,T · 0x80056B20 li r3,V ·
  0x801A0618 / 0x801A0628 li r0,T (opciones guardadas) · 0x801A05E8 li r7,V · 0x801A51A4 li r0,T (menú de título).
- Pantalla de la correa del Wiimote: tarea BOOT_SEQ (tabla de estados 0x802B5EE4, estado en +0x15C). Estado 2 = correa
  (imagen 576x432, A tras 1,5 s, sola a los 20,5 s). Parche 0x800999F4 807F0164→48000254 (b 0x80099C48 = salida al estado 4).
- Español = carpeta EU_ESPANOL con T=1 (el parche sustituye el texto inglés). SYSCONF.IPL.LNG=1.

## Mando clásico (verificado: funciona)
- El código Gecko NO funcionaba por -C EnableCheats; ahora va INTEGRADO en main.dol: 6 escrituras + rutinas C2 copiadas sobre
  funciones muertas del juego en 0x8025E590 (1888 bytes sin referencias; se comprueba MD5 antes de escribir).
  Intentos fallidos: sección nueva en 0x80001800 (se borra) y en 0x8042ABC0 / 0x80418C00 (Dolphin no carga secciones añadidas).
- Dolphin: EnableCheats=False (el codehandler no hace falta).
- WiimoteNew.ini: Classic emulado. Mando Xbox (posición Xbox): A atacar/aceptar, B cancelar, Y pista PF (X clásico), X ataque alt (Y),
  RT interactuar (R), LT primera persona (ZR), RB/L3 agacharse (L), LB/R3 centrar cámara (ZL), Start/Back zoom mapa, cruceta menús.
  Teclado: WASD, clic izq/E/Enter, Esc/Q/Retroceso, clic der/V, C/Ctrl, R/rueda, F, Shift, flechas, = y −.
- Linterna/cámara con mando = "mod de mando" (12 oct, 23:10) con expresiones de Dolphin en IR (sin tocar main.dol;
  parche de mando ORIGINAL v4). IR absoluto (Relative Input False, Total Yaw/Pitch 25/20). Cada dirección:
  (FP * relative(stick * FP, 1.4, ±1.0, $px|$py)) + ((!FP) * smooth(pow(stick, 1.6), 0.06, 0.3)),  FP = (botón ZR/LT > 0.5).
  · Tercera persona: curva -> poco stick apunta sin girar, a fondo gira la cámara; al soltar vuelve al frente en 0,3 s.
  · Primera persona (LT): el puntero se queda donde lo dejas.
  Historial: absoluto sin suavizar (errático), relativo (cámara pegada a un lado / giraba sola), relativo limitado 75%
  (igual en tercera persona), v5 IR oculto + rutina propia en main.dol (cámara muy mal). El juego gira la cámara mientras
  el puntero esté cerca del borde: en tercera persona el puntero NO puede quedarse de lado.
  Con teclado: la posición del ratón.
- Opciones del juego recomendadas: altavoz del mando NO, indicador de sonido SÍ, cursor de pantalla NO (con mando), subtítulos SÍ.

## Launcher (Dionixu's Launcher)
- Diseño según "Menú Fragile Dreams bajo la luna Concepto a utilizar.png": fondo = concepto con el menú borrado (inpaint OpenCV),
  anclado a la izquierda para que el logo no se corte. Menú JUGAR · LOGROS · AJUSTES · EXTRAS · CRÉDITOS · SALIR.
- Jugar: texto (es/en/fr/de), voces (ja/en), control (auto/mando/teclado) + opciones recomendadas. Ajustes: sonido, gráficos, idioma
  (launcher es/en), controles reasignables, general (intro, animaciones, luciérnagas, logros, aviso, probar aviso).
- Música «Alone» (Fragile Dreams OST 1-02). Sonidos sintetizados (campanas). Cursor propio: luz de luciérnaga (cursor*.png).
- Barra inferior navegable con mando (desde SALIR pulsar abajo). Minimizar/cerrar solo en ventana. Cierre con fundido a negro suave.
- Esc o L3+R3 en el juego: menú de pausa (exit.html) con volumen en vivo y volver al launcher (12 oct: antes F1).
  Esc ya no es "cancelar" en el juego (cancelar = Retroceso / Q); main.js ignora ESCAPE en asignaciones guardadas.
  bridge.ps1: A/B en posición Xbox (A 0x1000 aceptar, B 0x2000 volver). Al continuar, FOCUSPID enfoca la ventana visible
  más grande de Dolphin (la imagen) con AttachThreadInput, así no hace falta hacer clic.
  (12 oct, 2.º intento) Solo el foco no bastaba: RESUMEPID además hace un clic de botón lateral X1 (o X2) en el centro de
  la ventana, como el clic del usuario, y devuelve el cursor a su sitio. X1/X2 no están asignados en el juego.
- Transición launcher <-> juego (12 oct): al tener ventana el juego, main envía 'gameReady', la interfaz funde a negro (#veil)
  y pide 'hideLauncher' (respaldo 2,5 s). Al volver: 'prepReturn' (negro al instante, sin pantalla de la luna), se muestra la
  ventana y 'gameClosed' aclara el negro en 1,1 s. El último fotograma del launcher oculto es negro, así no se ve la luna.
- Icono de la ventana del juego: Dolphin-x64\Sys\Resources\dolphin_logo(.png/@2x) sustituidos por el loto
  (originales en dolphin_logo_original*.png).
- Icono de FragileDreams.exe: scripts\06_ICONO_EXE.bat aplica launcher\data\icono_exe.bin (5 PNG del loto en los huecos de los
  iconos de Electron). MD5 original 0A051D6D…, con icono 72C9C6F2…
- Paneles Jugar y Ajustes: ◄ ► cambian la opción (se guarda al momento); A / Enter confirma y baja a la siguiente fila.
- Fondo vivo (12 oct, Ajustes → General → FONDO VIVO): el agua del lago se mueve con un shader WebGL (ondas en perspectiva,
  destellos y banda de luz) solo dentro de `ui\assets\anim\agua_mascara.png` (máscara suavizada que excluye a Seto y Ren).
  Se pausa durante la partida, con la ventana oculta o con la opción apagada.
  La máscara excluye el final del logo ("…THE MOON", loto y línea; zona x 370–606, y 250–398 del fondo).
  Parpadeo de los personajes DESCARTADO tras 3 intentos (sprites de párpado sobre la ilustración; no convencía).

## Logros (pendiente de probar en partida)
- Set RetroAchievements juego 131 (42 logros, 290 pts) solo para USA (hash 462320776f8bb3e8a4ceb926b34268bf).
- El launcher pide usuario/contraseña una vez (no los guarda), descarga set e iconos (como TLS) y los evalúa sin conexión con
  `fd_logros.exe` = tls_logros.exe con el ID "SLSE" cambiado a "R2GP" (4 sitios).
- Traducción USA→EUR de direcciones (`data\mapa_usa_eur.json`, `herramientas\addrmap.py`): alineación de 450k instrucciones,
  6.472 parejas. Deltas: datos +0x660…+0x740, sdata/sbss +0x1AA0…+0x1AE0; montón MEM1 (≥0x804290E0 USA) +0x1AE0; MEM2 igual.
  Tras "I:" (AddAddress) la siguiente dirección es desplazamiento y no se traduce. Grupos "S" (no "0xS").
- Textos en español: `data\logros\traducciones.json` ({"es": {id: [título, descripción]}}), los usa la lista y el aviso en juego.
- Diagnóstico: `launcher\data\launcher.log` y `data\logros\activos.txt` (condiciones ya traducidas).

## Herramientas (herramientas\)
- `gen_cc.py` / `apply_cc.py`: convierten el Gecko EUR en parche estático (zona muerta 0x8025E590).
- `dead.py`: busca funciones sin referencias en main.dol. `addrmap.py`: mapa de direcciones USA→EUR. `d.py`: desensamblado con llvm-objdump.

## Texturas (12 oct)
- No existe paquete HD público del juego (búsqueda en foros de Dolphin, GitHub, Reddit). Se usará el volcado propio:
  Dolphin-x64\User\Dump\Textures\R2GP99 (1225 texturas, 66 MB, hecho con scripts\07). Volcado APAGADO
  (GFX.ini y el launcher fuerza DumpTextures = False en cada inicio). scripts\08 lo empaqueta si hace falta.
- Objetivo: iconos de botones de Xbox Series X con el estilo de los originales, en User\Load\Textures\R2GP99.
- Tampoco hay texturas de interfaz en The Spriters Resource (solo modelos de personajes).
- El volcado solo cubrió vídeo inicial, título y Ajustes. Iconos de mando encontrados: + / − de Ajustes (33x33).
  Hechos en HD 4x: tex1_33x33_ccab47d4948b332c_5 (+ -> Menú ☰) y tex1_33x33_ce3321b14816cdd3_5 (− -> Vista ⧉),
  mismo triángulo negro y posición del símbolo (herramientas: xbtn.py en la sesión). El launcher pone HiresTextures = True.
- Faltan los iconos que salen jugando (tutoriales, acciones): necesitan un volcado durante la partida.

## Cambio de plan (15 oct): el launcher como emulador
- Objetivo: que el propio launcher ejecute el juego con el core de Dolphin de libretro (dolphin_libretro.dll), para poder
  modificar más el juego (p. ej. el cursor solo en primera persona, que quedó a medias: la opción del cursor no se encontró aún;
  las opciones guardadas viven en 0x80326208, rutina 0x801A0580).
- Core actualizado a Dolphin reciente en oct 2025 (cscd98 y otros). Se publica solo en buildbot.libretro.com (bloqueado
  desde el entorno de Claude) -> scripts\09_DESCARGAR_CORE_DOLPHIN descarga en la PC:
  core\dolphin_libretro.dll y core\system\...\Sys (Dolphin.zip). Deja core\core_info.txt con SHA256.
- Motor propio: motor\fd_motor.exe (fuente motor\fd_motor.c, C puro). Compilado en el entorno de Claude con clang 18 +
  lld + cabeceras mingw-w64 (git) + libretro.h (libretro-common), sin CRT de arranque (entrada fd_entry, msvcrt importado).
  Ventana Win32 + OpenGL 3.3 core (el core pide GL Core 3.3, bottom_left), audio waveOut, XInput + teclado,
  puerto 0 = Wiimote + mando clásico (0x401), puntero (dolphin_ir_mode=2) calculado en el motor con la lógica del
  "mod de mando" (3ª persona tipo cámara, 1ª persona con LT relativa). Guardado = Dolphin-x64 (User compartido:
  mismas partidas y texturas). MEM1 accesible (retro_get_memory_data) para los mods.
  stdin: PAUSE/RESUME/VOLUME n/QUIT/FOCUS; stdout: READY/STARTED/COMBO/QUIT/ERROR. Log: motor\fd_motor.log.
  Prueba aislada: scripts\10_PROBAR_MOTOR.bat (ventana 1280x720, opciones motor\opciones_prueba.txt).
  Prueba 15 oct OK (RTX 5050, GL 3.3, 1920x1584 interno, 32 kHz, ~28 fps únicos = ritmo nativo del juego).
- Launcher conectado (15 oct): si existen motor\fd_motor.exe y core\dolphin_libretro.dll arranca el MOTOR (cfg.engine =
  'dolphin' fuerza Dolphin). Genera data\motor_opciones.txt (resolución -> dolphin_efb_scale 2/3/4/6, AA, anisotrópico,
  16:9, ubershaders síncronos = shader_compilation_mode 1 porque el core no tiene contextos compartidos WGL) y
  data\motor_controles.txt (asignaciones de Ajustes -> Controles: K<VK>, P<botón>, W rueda). Pausa = PAUSE/RESUME al
  motor, volumen = VOLUME, salir = QUIT. Logros: fd_logros.exe busca la RAM por "R2GP" en el proceso del motor.
  Registro: data\motor.log.
  Arranque lento (16 oct): 45 s en negro = el core compilaba en el hilo principal los 222 pipelines de
  User\Cache\R2GP99.uidcache (sin hilos de compilación: OpenGL en Windows no tiene contexto compartido) y con
  ubershaders (modo 1/2) además precompila todos los ubershaders (~200 ms cada uno). Ahora modo 0 (Síncrono, shaders
  específicos, caché en disco). El launcher espera a STARTED (15 imágenes) para fundirse, con la luna mientras tanto.
  Logros con el motor: OK (HOOKED, MEM1 encontrada).
- Teclado y ratón (15 oct): modo auto/mando/teclado (Ajustes -> Control); en auto cambia solo según lo último usado.
  Ratón = puntero del Wiimote (absoluto, dentro de la imagen), cursor luciérnaga del launcher (motor\cursor_luciernaga.h,
  48x48), rueda = centrar cámara, confinado a la imagen en pantalla completa (se suelta en pausa).
- Mods pedidos (15 oct): cámara totalmente manejable con stick derecho en 3ª y 1ª persona; movimiento 360° en 3ª persona
  (Seto se gira hacia donde va el stick izquierdo, sin andar de espaldas).
- Mod de juego v1 (16 oct) = codigos\parche_juego_eur.json (generador herramientas\gen_mod_juego.py), lo aplica
  buildDol después del parche del mando. Ingeniería inversa (EUR, de volcados de MEM1 con F9 + main.dol):
  * Mundo/campo = objeto 0x80304768. Jugador CF::CPlayer = campo+0x5458 = 0x80309BC0 (vptr en +0x3A24):
    posición +0x3894 (0x8030D454), rotación (pitch, yaw, roll) +0x38A0 (yaw = 0x8030D464; adelante = (sin y, cos y),
    girar a la derecha = yaw baja).
  * Controlador de cámara = campo+0x3CB8 = 0x80308420 (puntero también en r13-0x55E0): +0 objetivo (cabeza del
    jugador), +0x18 nodo CAMERA (0x80308438: ojo +4, pitch +0x10, yaw +0x14; puntero "cámara actual" r13-0x5348).
    Modo de cámara en r13-0x5588 (0x80413A18): 0 = 3ª persona, 1 = 1ª persona.
    3ª persona = 0x8000B20C: cámara "con correa": el ojo se queda donde está salvo que quede a <2 m o >3 m
    (+0x10B8/+0x10BC); altura del ojo = suelo + (+0x10A8), que se acerca a +0x100C (1,5) o +0x1010 (0,9 agachado).
    1ª persona = 0x8000A940: lee pitch/yaw del nodo, suma el giro por puntero en el borde, limita pitch y aplica.
    (Hay un modo de mirar con el stick en +0x1054 == 1 que el juego nunca activa.)
  * Movimiento: 0x800C9FC0 ya convierte el stick a un ángulo del mundo relativo a la cámara; 0x800D74F8 decide con
    0x800DC7E8 si gira hacia ese ángulo o si camina de espaldas (animaciones 4/5). Parche: 0x800DC814 bl -> li r3,1
    = siempre gira (movimiento 360°).
  * Ganchos: 0x8000B06C (1ª persona: pitch += Y*0,035, yaw -= X*0,05 por fotograma) y 0x8000B240 (3ª persona: gira
    el ojo alrededor del objetivo X*0,05 rad; Y sube/baja la altura del ojo 0,03 m, de -1,0 a +1,6 m). Código en
    0x8025E900 (zona muerta), memoria compartida 0x8025ECB0: +0 stick X, +4 stick Y, +8 activar, +12 contador que
    sube cada vez que corre la cámara, +16.. velocidades y límites (floats).
  * Motor: escribe el stick derecho (curva 0,4a+0,6a²) y activar=1 antes de cada fotograma; si el contador avanza
    = en juego: puntero/linterna al centro y el stick derecho ya no llega al core. Si no avanza (menús, escenas) el
    stick derecho vuelve a mover el puntero como antes. Con Dolphin el mod de cámara queda inactivo (nadie lo activa),
    el movimiento 360° sí funciona.
- Mod v2 (16 oct): en la prueba Seto seguía caminando de espaldas. Causa real: 0x800DC900 (actualización de la
  linterna, cada fotograma) gira el CUERPO hacia la luz cuando el giro del torso pasa del límite (r13-0x5370 +4).
  Se anulan esas dos llamadas SetRot (0x800DCAB0 y 0x800DCBC4 bctrl -> nop); torso y cabeza siguen limitados.
  Velocidad vertical de la cámara en 3ª persona 0,030 -> 0,055 m por fotograma.
  Pendiente: ocultar el cursor del menú de título con mando (solo teclado/ratón).
- Mod v3 (16 oct): los pies hacían pasos de giro sin parar = la misma rutina pedía la animación de giro (bandera 1024,
  0x800A9CB8) cada fotograma; anuladas sus dos llamadas (0x800DCABC, 0x800DCBD0).
- Motor: V-Sync real (--vsync 0|1 desde Ajustes; intervalo 1; a ~60 Hz el ritmo lo marca SwapBuffers, a 120/144 Hz
  sigue el limitador). Modo de pantalla: el launcher comparaba con 'window' pero el ajuste se llama 'win' -> siempre
  salía en pantalla completa. Ahora --fullscreen 1 (completa) / 2 (ventana sin bordes) / 0 (ventana, ajustada al 90 %
  del escritorio).
- Motor v7 (16 oct): en ventana el juego "desaparecía": con V-Sync (intervalo 1) y la ventana tapada por el launcher
  (que sigue encima hasta STARTED) SwapBuffers de NVIDIA se quedaba bloqueado -> 0 imágenes -> el usuario volvía y el
  launcher lo mataba (código 1). Ahora arranca con intervalo 0 y vsync_update() activa el V-Sync solo con la ventana
  al frente (lo quita en pausa/fondo); no presenta con la ventana minimizada; registra fotogramas cada 5 s al inicio.
- Motor v8 (16 oct): seguía "desapareciendo" en ventana: el juego sí corría (247 imágenes) pero al ocultarse el
  launcher la ventana quedaba DETRÁS de otras apps (Windows no deja a un proceso en segundo plano tomar el frente y una
  ventana de 1280x720 queda tapada). bring_front(): AttachThreadInput al hilo delantero + TOPMOST/NOTOPMOST +
  SetForegroundWindow. Se usa con la orden FOCUS (el launcher la manda al ocultarse) y automáticamente entre 2,5 y
  10,5 s después de STARTED si la ventana no está al frente.
- Mod v4/v5 (16 oct): velocidad vertical 3ª persona 0,080 m/fotograma. Cursor solo con teclado/ratón: gancho en
  0x80110CAC (justo después de leer KPADRead en el gestor del mando 0x8039C6B0): si SH+56 != 0 pone "puntero válido"
  (+420) y dpd_valid de la muestra (+526) a 0 -> el juego no dibuja el cursor. El motor pone SH+56 = 1 con mando y
  fuera del juego (menús, título, escenas); 0 con teclado/ratón o jugando.
- Motor v10/v11 (16 oct): en ventana/sin bordes la ventana va TOPMOST mientras es la activa (se quita al cambiar de
  app o en pausa). Teclado "no funcionaba": g_focus dependía de WM_ACTIVATEAPP, que no llega cuando la ventana se
  activa uniendo hilos (bring_front) -> ahora g_focus = GetForegroundWindow() == ventana en cada lectura.
  Ratón = cursor del juego: lazo cerrado leyendo KPADStatus.pos (0x8039C6B0+464/468, válido si +526 != 0) y
  corrigiendo la salida del puntero (ganancia 0,35; si no converge en 90 fotogramas se desactiva). En menús (fuera
  del juego) se oculta la luciérnaga de Windows y se ve solo el cursor del juego.
  Launcher: onGameExit ya no truena si la ventana fue destruida (cerrar el launcher con el juego abierto).
- Mod v6 / motor v14 (16 oct): el ratón no movía el cursor del juego: con el mando clásico emulado el parche de Vague
  Rant arma KPADStatus.pos con el stick derecho y el puntero IR del core nunca llega (pos ~0). Además la dirección del
  gestor del mando era 0x8039C6B0 (no 0x803AC6B0: lis 0x803A + addi -0x3950). Ahora el gancho 0x80110CAC copia
  SH-16 (activar, x, y) en pos (+464/+468) y marca válido (+526 = 2, +420 = 2); el motor lo llena con el ratón
  (-1..1) en modo teclado. La luciérnaga de Windows ya no se ve sobre el juego con teclado/ratón (solo el cursor
  del juego). Diagnóstico en motor.log: "cursor: ratón | KPAD | pantalla".
- Motor v15 (16 oct): cámara con ratón estilo Wii: con el cursor a más del 72 % hacia el borde, el motor manda giro de
  cámara por la memoria compartida (curva 0,4a+0,6a²). Horizontal siempre; vertical solo en 1ª persona (modo de cámara
  0x80413A18 == 1). Teclas IJKL tienen prioridad. En ventana solo si el ratón está sobre la imagen.

## Pendiente
1. Probar el launcher completo en la PC (música, mando en la barra inferior, cierre, idiomas fr/de, voz inglés).
2. Descargar logros y comprobar que salta "Flashlight"; si no, revisar launcher.log / activos.txt.
3. Ejecutar scripts\06_ICONO_EXE.bat (con el launcher cerrado) para el icono del loto en FragileDreams.exe.
4. Opcional: instaladores Windows/Linux como en Pandora's Tower (scripts 07 de PT).
5. Probar que ya no aparece la pantalla de la correa (parche 0x800999F4, 12 oct).

- Motor v16 (16 oct): linterna con mando en 3ª persona: el puntero ya no queda al centro; se proyecta (matriz de la
  cámara 0x80308460, proyección 0x803084E0/F4) un punto del suelo delante de Seto (2,5 m, acercándolo si queda
  debajo de la cámara) y se manda como posición del cursor (SH-16). Limitado a ±0,6 / -0,6..0,85 para no tocar los
  bordes que giran la cámara. Pendiente de probar.
- Texturas (16 oct): el motor SÍ vuelca texturas con GFX.ini DumpTextures (UICommon::Init lee User\Config).
  Volcado 2: 753 texturas (para_claude\texturas\volcado_1.zip). Botones encontrados: "☞ⒷAtrás" 131x23
  78fcd21581c534e9 (B del Wii = cajita con labio de gatillo), "ⒶContinuar☞" 159x22 8dae536c77b777e7, Ⓐ 32x32
  9c413ba1e786019c, páginas de tutorial 768x448 (122a6b55 Combate, 8e1ed0b1 Linterna "Mando de Wii", 933923a5
  Buscar II, a8eaf973 Buscar con dibujo del Wiimote, fb0f81f2 Objetos).
  Paquete v1: Atrás con B de Xbox en círculo (mismo trazo que el Ⓐ del juego) y tutorial Buscar con LT en la cajita.
  Pendiente: textos/dibujo del Wiimote en 8e1ed0b1 y a8eaf973; versión teclado/ratón (el launcher tendría que copiar
  el juego de texturas según el dispositivo al iniciar).
- Motor v17 (16 oct): ratón estilo PC. Jugando con teclado/ratón el cursor se oculta y queda en el centro de la ventana;
  el movimiento por fotograma (píxeles, suavizado 0,6) gira la cámara: yaw 0,0030 rad/px, pitch 0,0024 rad/px (1ª),
  altura 0,0045 m/px (3ª), × sensibilidad (Ajustes -> SENSIBILIDAD DEL RATÓN, mouse_sens 50–200 %). La linterna va
  donde mira Seto (como con mando); en 1ª persona apunta al centro. En menús/escenas el cursor vuelve a ser libre.
  "Invertir (ratón)" ya no invierte el cursor de los menús, solo la cámara.
- Controles (16 oct): un botón = una acción. RT, X (Y clásico) y L3/R3 libres; teclado sin E/Espacio/Shift/V/Ctrl/
  Retroceso por defecto (todo eran copias). Mapeo del parche de Vague Rant: A,R,Y clásicos -> A del Wii; B,ZR -> B;
  X -> 0x0080 (pista PF); L -> C; ZL -> Z; +/−/Home/cruceta directos.
- Texturas (16 oct): tutoriales "Linterna" (8e1ed0b1: "Ahora podrás iluminar") y "Buscar" (a8eaf973: "Mantén pulsado
  [LT] para examinar…" + dibujo de mando Xbox con LT marcado) rehechos con herramientas\ink.py (letras recortadas de
  la propia página + relleno del papel) y herramientas\xbox_sketch.py.
- 16 oct (tarde): packs de botones por dispositivo en launcher\data\texturas\{mando,teclado} (applyTexturePack copia
  a User\Load\Textures\R2GP99 al jugar). Aviso de brillo = panel 640x120 c7a89a1d (Ⓐ cian dentro). Teclado: Enter = +
  (saltar cinemáticas / menú), Tab = −, E = interactuar. El teclado "no funcionaba" porque la cinemática solo se salta
  con +. Motor v19: Alt/F10 ya no activan el menú de ventana. Motor v20: pantalla completa 1 px más alta que el monitor
  para evitar "independent flip" (Discord/OBS capturaban negro o imagen congelada).
- 60 FPS (16 oct): ritmo del juego = 0x80412270 (60 Hz) / intervalo de imagen 0x80412268 (2 en el campo = 30 fps;
  setter 0x8011C3F8). El juego ya usa intervalo 1 en algunas pantallas y escala la lógica con 300/fps (0x800084A4),
  y en PAL 50 Hz corre a 50. codigos\parche_60fps_eur.json cambia los "li 2" de 0x8011BA38, 0x8001DF6C, 0x8005109C,
  0x80051324 por 1. Opción Ajustes -> FOTOGRAMAS (60/30, por defecto 60). Motor v21: con intervalo 1 el giro del
  stick se escala ×0,5; el movimiento del ratón se acumula hasta que el juego lo consume (contador de cámara).
- 60 FPS v2 (16 oct): con el parche fijo la escena inicial (observatorio) se quedaba trabada -> los eventos cuentan
  fotogramas. Ya no se parchea main.dol: el motor (v22) pone el intervalo 0x80412268 = 1 solo mientras la cámara del
  jugador está activa y lo devuelve a 2 en escenas/menús (solo si el juego tenía 2). Opción "fps60 = on/off" en
  motor_controles.txt desde Ajustes.
- 60 FPS final (16 oct): (1) 0x8001BAAC nop: con intervalo 1 el bucle principal no llamaba 0x800C4B30 (pasa banderas
  +14664→+14668 y llama vtable[7] = actualización completa del actor; CPlayer vt 0x802B9C50, [7] = 0x800D9AC8) -> Seto
  no se movía. (2) Animación: avance = intervalo × 0,5 fotogramas (0x800E9A68); a 60 hay medios fotogramas. Pistas
  (24 B: +0 claves, +10 formato 1 = rotación s16/4096 rad, +12 n, +14 índice, +16 último tiempo) horneadas a 30 con
  Euler que saltan ±π/2π (ej. hueso 28 del caminar); Hermite 0x800EE6BC..0x800EE8C4 cruzaba el salto. Gancho en
  0x800EE8C4: rotaciones -> lineal entre claves, salto > 1,5 rad -> clave más cercana. Controlador de movimiento de
  Seto = *(jugador+0x570C) (+9632 huesos ×352 B, +9636 6 capas ×36 B). (3) Con fps60 el launcher pone
  dolphin_main_cpu_thread = disabled. Motor v25: F8 capturas, F10 traza. Parche de juego v10.
- Pendiente: ver BITACORA.md ("Pendiente al 16 oct, tarde").
