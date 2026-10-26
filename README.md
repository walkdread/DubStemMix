> Interfaz 0.4: biblioteca integrada, consola inspirada en macOS, perillas con arcos y ventana ajustable.
> **Windows alpha 0.4:** lista persistente y cola de separacion mientras reproduce,
> modo Rapido (HTDemucs) / Calidad (FT), menus y transporte MIDI configurable.
> DirectML GPU es experimental; requiere prueba en hardware. SEND ALL no es Play.
> Consulta [LEEME Windows](docs/WINDOWS_PRIMEROS_PASOS.txt) para instrucciones actuales.

<p align="center"><img src="Design/AppIcon-1024.png" width="128" alt="DubStemMix icon"></p>

# DubStemMix para Windows

**Consola de dub reggae para la Akai MIDImix.** Carga los stems de un tema, ponlos en seis faders, usa dos más para los efectos y construye tu propia versión al estilo de King Tubby: quita la voz, manda la caja al eco, deja solo bajo y batería, deja caer el spring, dispara la sirena. Sin DAW, sin sesión que preparar y sin mapear nada.

Este repositorio es un **fork de trabajo** de [yoanbernabeu/DubStemMix](https://github.com/yoanbernabeu/DubStemMix). La rama `port/windows-alpha` es un port nativo a Windows x64 en C++/Win32, sin Electron y sin JUCE. El proyecto original de macOS sigue siendo la referencia funcional.

[![Windows alpha](https://github.com/walkdread/DubStemMix/actions/workflows/windows-alpha.yml/badge.svg?branch=port/windows-alpha)](https://github.com/walkdread/DubStemMix/actions/workflows/windows-alpha.yml)

> **Estado:** alpha 0.2. Todavía no es la versión completa de macOS. No la uses como única consola en una actuación hasta completar las pruebas de estabilidad con tu hardware.

## Novedades de la alpha 0.2

La consola ahora tiene un diseño oscuro inspirado en macOS, perillas y faders verticales.
**ABRIR CANCION** prepara una canción completa con ONNX Runtime CPU: descarga los modelos
con confirmación, verifica SHA256, muestra progreso y carga cuatro stems alineados.
El audio se procesa localmente. **Carpeta de datos** permite guardar modelos y stems en D:.

Los canales 5 y 6 vacíos son **Siren** y **Syndrum**; al cargarles una pista vuelven a ser
canales de audio. Se añaden pitch/rate/decay, HOLD/RISE/HIT/ROLL, plate/spring,
phaser/flanger, tiempo/feedback y gestos de delay. Consulta la guía incluida para el mapeo.

Extrae el ZIP completo: `onnxruntime.dll` y los runtimes incluidos deben quedar junto al
`.exe`. Conserva la versión anterior mientras pruebas la nueva. La separación inicial
puede tardar varios minutos y necesita hasta 663 MB de descarga. Máximo 10 minutos por canción.

## Qué funciona hoy

| Área | Estado |
|---|---|
| Compilación nativa Windows x64 | ✅ GCC/MinGW y ClangCL/MSVC |
| Seis pistas de stems | ✅ WAV, FLAC y MP3, convertidos a 48 kHz estéreo |
| Tres buses de efectos | ✅ delay, plate/spring y phaser/flanger |
| Instrumentos | ✅ Siren y Syndrum en canales 5/6 vacíos |
| Retornos y master | ✅ faders 7 y 8, master y limiter |
| Akai MIDImix | ✅ WinMM, pickup, LEDs de MUTE/SOLO |
| Selección de salida | ✅ WASAPI compartido |
| Transporte | ✅ reproducir, pausar y detener |
| Carga de stems | ✅ un archivo por pista, alineados desde el inicio |
| Persistencia de proyectos | ❌ pendiente |
| Grabación / exportación | ❌ pendiente |
| Separación automática de stems | ✅ ONNX CPU, descarga verificada, progreso y cancelación entre bloques |
| Plugins VST3 / AU | ❌ pendiente |
| ASIO | ❌ pendiente |
| Paridad visual con macOS | 🟡 consola oscura con perillas/faders; no paridad completa |

## Requisitos

- **Windows 10 22H2** o **Windows 11**, x64.
- 8 GB de RAM recomendados (la alpha limita el audio decodificado a 1 GB).
- Una interfaz de audio USB (la Xenyx 302 USB es la referencia del proyecto).
- **Akai MIDImix** para el control físico (opcional para probar con ratón/teclado).
- Git.
- Toolchain de compilación:
  - **Opción recomendada:** [w64devkit](https://github.com/skeeto/w64devkit) (GCC, Make, CMake, Ninja y shell POSIX en un solo paquete).
  - **Opción Visual Studio:** Visual Studio 2022 Build Tools con C++, Windows SDK y herramientas ClangCL.

### Toolchain portable en este equipo

`C:` tiene poco espacio libre, así que el toolchain se instala en `D:`:

```text
D:\tools\w64devkit\w64devkit          GCC 16.2, CMake 4.4.4, CTest, Ninja, Make, sh
D:\tools\cmake-4.4.4-windows-x86_64    CMake/CTest alternativos
```

Para usar `make` desde PowerShell en la sesión actual:

```powershell
$env:Path = "D:\tools\w64devkit\w64devkit\bin;$env:Path"
make doctor
```

Si el paquete se extrae en otra ruta, cambia `TOOLCHAIN` al invocar `make`:

```powershell
make TOOLCHAIN=C:/ruta/a/w64devkit build
```

## Inicio rápido

```powershell
git clone -b port/windows-alpha https://github.com/walkdread/DubStemMix.git
cd DubStemMix

# Comprobar que el compilador, CMake, Ninja y Git están disponibles
make doctor

# Configurar y compilar la app + los tests
make build

# Ejecutar los tests
make test

# Abrir la aplicación
make run
```

La primera configuración descarga **miniaudio 0.11.23** mediante CMake `FetchContent`; necesita conexión a Internet. Después queda en `build-windows/_deps/` y no se vuelve a descargar.

### Si clonas en una carpeta con espacios

La carpeta local de este proyecto es `D:\AKAI MIDIMIX\DubStemMix  Win` (con dos espacios). El `Makefile` usa rutas relativas y funciona, pero en PowerShell hay que citar la ruta:

```powershell
cd "D:\AKAI MIDIMIX\DubStemMix  Win"
make build
```

## Makefile: referencia completa

`make help` muestra esta misma lista. Todos los targets se pueden combinar con variables:

```powershell
make build JOBS=4
make CONFIG=Debug test
make TOOLCHAIN=C:/msys64/mingw64 build
```

| Target | Qué hace |
|---|---|
| `help` | Muestra la ayuda (target por defecto). |
| `doctor` | Comprueba CMake, CTest, Ninja, GCC/G++ y Git. |
| `configure` | Configura CMake en `build-windows/`. |
| `build` | Configura y compila `DubStemMix.exe` y los tests. |
| `rebuild` | `clean` + `build`. |
| `test` | Ejecuta la suite de CTest. |
| `run` | Compila y abre `build-windows/DubStemMix.exe`. |
| `package` | Instala en `dist/DubStemMix-Windows-alpha/` (exe, licencias y LEEME). |
| `zip` | Genera `dist/DubStemMix-Windows-x64-alpha.zip`. |
| `clean` | Borra `build-windows/`. |
| `distclean` | Borra `build-windows/` y `dist/`. |
| `info` | Muestra las variables de build. |
| `web-install` | Instala dependencias del sitio Astro en `web/` (requiere Node.js). |
| `web-dev` | Sirve la web en `http://localhost:4321/DubStemMix/`. |
| `web-build` | Compila la web estática en `web/dist/`. |

Variables principales:

| Variable | Valor por defecto | Uso |
|---|---|---|
| `TOOLCHAIN` | `D:/tools/w64devkit/w64devkit` | Raíz del toolchain portable. |
| `CMAKE` | `$(TOOLCHAIN)/bin/cmake.exe` | CMake a usar. |
| `CTEST` | `$(TOOLCHAIN)/bin/ctest.exe` | CTest a usar. |
| `NINJA` | `$(TOOLCHAIN)/bin/ninja.exe` | Generador Ninja. |
| `CC_BIN` | `$(TOOLCHAIN)/bin/gcc.exe` | Compilador C. |
| `CXX_BIN` | `$(TOOLCHAIN)/bin/g++.exe` | Compilador C++. |
| `BUILD_DIR` | `build-windows` | Carpeta de build. |
| `CONFIG` | `Release` | Configuración de CMake. |
| `GENERATOR` | `Ninja` | Generador de CMake. |
| `JOBS` | `2` | Paralelismo de compilación. |
| `DIST_DIR` | `dist/DubStemMix-Windows-alpha` | Paquete portable. |
| `ZIP_FILE` | `dist/DubStemMix-Windows-x64-alpha.zip` | ZIP final. |

## Compilación manual con CMake

Si prefieres no usar `make`, el proyecto se compila igual con CMake. Para el toolchain portable:

```powershell
$env:Path = "D:\tools\w64devkit\w64devkit\bin;$env:Path"
$env:CC  = "D:\tools\w64devkit\w64devkit\bin\gcc.exe"
$env:CXX = "D:\tools\w64devkit\w64devkit\bin\g++.exe"

cmake -S . -B build-windows -G Ninja -DCMAKE_BUILD_TYPE=Release -DDUB_BUILD_WINDOWS_APP=ON
cmake --build build-windows --parallel 2
ctest --test-dir build-windows -C Release --output-on-failure
cmake --install build-windows --prefix dist/DubStemMix-Windows-alpha
```

Para Visual Studio 2022 con ClangCL (el camino que usa GitHub Actions):

```powershell
cmake -S . -B build-windows -G "Visual Studio 17 2022" -A x64 -T ClangCL -DDUB_BUILD_WINDOWS_APP=ON
cmake --build build-windows --config Release --parallel 2
ctest --test-dir build-windows -C Release --output-on-failure
```

En MinGW, `CMakeLists.txt` añade automáticamente `-municode` (para `wWinMain`) y `-static` (para que el `.exe` no dependa de DLLs de GCC). La base 0.1 dependía solamente de DLLs del sistema; la 0.2 añade `onnxruntime.dll` y su runtime incluido. DLLs del sistema: `KERNEL32`, `USER32`, `GDI32`, `COMCTL32`, `COMDLG32`, `WINMM` y `msvcrt`.

## Primer uso

1. Conecta la **Akai MIDImix** y la interfaz de audio.
2. Abre `build-windows/DubStemMix.exe` (o `make run`).
3. Elige la salida de audio en la lista superior. El nombre lo da Windows; puede no coincidir literalmente con el nombre comercial de la interfaz.
4. Baja el volumen físico de escucha. Pulsa **Reproducir** y súbelo poco a poco.
5. Carga un archivo por pista: voz, batería, bajo y otros. Los stems deben ser de la misma canción y estar alineados desde el mismo instante inicial.
6. Ajusta faders, envíos y retornos. Si un control MIDI no responde, muévelo hasta alcanzar el valor que aparece en pantalla (pickup).

### Controles de la MIDImix

| Control | Función |
|---|---|
| Faders 1–6 | Volumen de las seis pistas. |
| Perillas, fila superior | Envío a delay. |
| Perillas, fila media | Envío a plate reverb. |
| Perillas, fila inferior | Envío a phaser. |
| Fader 7 | Retorno de delay. |
| Fader 8 | Retorno de reverb. |
| Master | Volumen general. |
| MUTE | Silencia la pista. |
| SOLO + MUTE | Mantén SOLO y pulsa MUTE para hacer solo. |
| Pickup | El control físico no salta: actúa al alcanzar el valor en pantalla. |

El retorno de phaser se ajusta en pantalla. Pausa conserva la posición y deja sonar las colas; Detener reinicia la posición y limpia los efectos. Cargar pistas reinicia la posición. Reconectar MIDI vuelve a abrir el controlador si lo conectaste después. Actualizar audio vuelve a enumerar las salidas y debe usarse con la reproducción detenida.

## Arquitectura

```text
Sources/
  DubDSP/                 Núcleo DSP en C11, compartido con macOS
  WindowsApp/
    Main.cpp              UI Win32, WASAPI, MIDI WinMM, diálogos
    Mixer.h               grafo de mezcla y parámetros atómicos
    Miniaudio.c           implementación de miniaudio
Tests/
  PortableDSP/            portabilidad del DSP en C
  WindowsCore/            pruebas del mixer sin hardware
CMakeLists.txt            build portable + app Windows opcional
Makefile                  atajos de build/test/run/package
docs/
  WINDOWS_PORT_ES.md      estrategia de migración y riesgos
  WINDOWS_PRIMEROS_PASOS.txt  guía de la alpha
web/                      sitio Astro del proyecto original
```

| Componente | Responsabilidad |
|---|---|
| `DubDSP` | Delay de cinta, plate/spring reverb, phaser, flanger, limiter, sub, auto-wah, sirena, Syndrum. Sin allocation ni locks en el hilo de audio. |
| `Mixer` | Seis strips, tres buses, retornos, master, limiter, suavizado de ganancias y posición de reproducción. |
| `Main.cpp` | Ventana, controles, selección de dispositivo, decodificación, MIDI, pickup y LEDs. |
| `Tests/PortableDSP` | Comprueba los efectos a 44.1/48/96 kHz y distintos tamaños de bloque. |
| `Tests/WindowsCore` | Comprueba el mixer sin abrir dispositivo de audio. |

### Decisiones de audio

- Un solo hilo de render; el callback no abre archivos, no decodifica, no reserva memoria y no espera locks.
- Formato interno fijo: **48 kHz, estéreo, float32**, quantum de 256 muestras.
- Los parámetros de UI/MIDI se publican con `std::atomic` y se suavizan en el render.
- Los stems se cargan completos en memoria; límite de 1 GB de audio decodificado.
- WASAPI en modo compartido; no hay ASIO ni promesa de latencia mínima en esta etapa.
- Los efectos viven en buses compartidos: cortar una pista no corta las colas.
- La MIDImix se detecta por nombre y se controla con WinMM. No se restauran faders físicos desde un proyecto.

## Alcance actual y limitaciones

Esta alpha **no** implementa todavía:

- Guardar/abrir proyectos `.dubstem` ni setlists `.dubset`.
- Grabación de sesiones, exportación WAV/MP3 ni tracklist.
- Plugins VST3/AU ni aislamiento de plugins.
- ASIO, baja latencia garantizada o soporte de 192 kHz.
- Bancos, envíos entre buses ni todas las páginas de macOS. REC ARM tiene gestos en los canales 5–8.
- Actualizador automático, instalador firmado o desinstalador.
- Validación prolongada de la alpha 0.2 con hardware físico. El usuario confirmó audio, MIDI y stems sincronizados en la alpha 0.1 con Windows 10, MIDImix y Xenyx 302 USB.

Los stems deben estar precargados y alineados. Si los stems tienen silencios iniciales distintos, la mezcla sonará desfasada. Para la primera prueba usa WAV sin silencios iniciales y con la misma frecuencia de muestreo.

## Roadmap

| Hito | Contenido | Estado |
|---|---|---|
| M1 | Base DSP portable, CMake, tests y app Windows mínima | ✅ |
| M2 | Paridad de audio y controles: parámetros de efectos, mute/solo, master, buses, UI más cercana a macOS | 🟡 en curso |
| M3 | Proyectos, setlists, carga/guardado, grabación WAV y exportación | ⏳ |
| M4 | Separación ONNX CPU | 🟡 preparación de una canción; cola de varias canciones pendiente |
| M5 | VST3, ASIO, instalador, firma y actualizaciones | ⏳ |

## Solución de problemas

**`make` no se reconoce como comando.**
Añade el toolchain al `PATH` de la sesión:

```powershell
$env:Path = "D:\tools\w64devkit\w64devkit\bin;$env:Path"
```

O invoca `make.exe` con su ruta completa.

**`undefined reference to WinMain`.**
`Main.cpp` usa `wWinMain`. Con MinGW hay que enlazar con `-municode`; `CMakeLists.txt` ya lo añade. Si usas otro build, añade `-municode` a las opciones de enlace.

**`C11 atomics required`.**
El proyecto necesita `_Atomic` de C11. Usa GCC/Clang reciente o Visual Studio 2022 con `-T ClangCL`; no uses el compilador C de MSVC clásico para `DubDSP`.

**No aparece la interfaz de audio.**
Pulsa **Detener**, luego **Actualizar audio** y selecciona la salida. WASAPI compartido no funciona bien si otra aplicación tiene el dispositivo en exclusiva. Cierra DAWs y reproductores que puedan estar bloqueándolo.

**No se detecta la MIDImix.**
Conecta el controlador, cierra otras aplicaciones MIDI y pulsa **Reconectar MIDI**. La alpha busca un dispositivo cuyo nombre contenga `MIDImix`.

**La primera configuración tarda.**
CMake descarga miniaudio con `FetchContent`. Es solo la primera vez; después queda en `build-windows/_deps/`.

**Error por espacios en la ruta.**
El `Makefile` usa rutas relativas, pero si ejecutas CMake a mano, entrecomilla todas las rutas que contengan espacios. La carpeta local actual tiene dos espacios en el nombre.

**Poco espacio en `C:`.**
Instala el toolchain en `D:` y pasa `TOOLCHAIN=...` a `make`. Los builds van a `build-windows/` dentro del proyecto.

**El antivirus bloquea el `.exe`.**
La alpha no está firmada digitalmente. Conserva el texto exacto del aviso y comprueba que descargaste el artifact del workflow de este repositorio; reporta el aviso antes de continuar.

## Documentación

- [Estrategia de migración a Windows](docs/WINDOWS_PORT_ES.md): decisiones, inventario, riesgos y criterios de aceptación.
- [Primeros pasos de la alpha](docs/WINDOWS_PRIMEROS_PASOS.txt): alcance, controles y límites.
- [Documentación del proyecto original](https://yoanbernabeu.github.io/DubStemMix/docs/): guía de usuario, efectos, stems, proyectos y directo.
- [Web del proyecto original](https://yoanbernabeu.github.io/DubStemMix/).

## Versión macOS original

El proyecto de **Yoan Bernabeu** sigue siendo la referencia funcional y de sonido. Es una app nativa Swift/SwiftUI + C para macOS 15+ (Apple Silicon), con separación de stems, grabación, proyectos, plugins y una interfaz mucho más completa.

- Repositorio original: [yoanbernabeu/DubStemMix](https://github.com/yoanbernabeu/DubStemMix)
- Build original: Xcode 26 / Swift 6.2, `swift run DubStemMix`, `swift test`.
- Este fork no está afiliado al autor original.

## Licencia y créditos

El proyecto es **MIT**. Consulta [LICENSE](LICENSE) y [NOTICE](NOTICE).

- DSP original: Yoan Bernabeu y colaboradores.
- Port Windows: walkdread y colaboradores.
- [miniaudio](https://github.com/mackron/miniaudio) 0.11.23, fijado en `f40cf03f80cdb7e741d43e53b7e706e8c1394bcf`; aviso en `licenses/MINIAUDIO-LICENSE.txt`.
- Fuentes, iconos y capturas: ver `Design/` y `web/`.

## Contribuir

- Trabaja sobre la rama `port/windows-alpha`.
- Mantén `Sources/DubDSP` compatible con la versión macOS: es código C compartido y su ABI no debe romperse sin una migración coordinada.
- Añade o actualiza pruebas en `Tests/PortableDSP` y `Tests/WindowsCore`.
- Ejecuta `make test` antes de abrir un pull request.
- Para cambios grandes (proyectos, grabación, ONNX, VST3, ASIO), abre primero un issue y documenta el alcance.
- No introduzcas Electron ni JUCE sin una decisión de arquitectura y licencia discutida en el repositorio.
