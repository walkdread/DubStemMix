# DubStemMix: estrategia de migración a Windows

Base auditada: walkdread/DubStemMix, commit 172c98b1baf22f8fa7c5b3668fe774de9867e7bc.
Objetivo propuesto: Windows 10 22H2 x64; conservar macOS durante la migración.
Estado: primera base DSP, no aplicación Windows terminada.

## Decisión de arquitectura
Conservar Sources/DubDSP en C11, con su ABI C existente. Implementar el motor,
transporte MIDI y aplicación en C++ con límites explícitos entre plataforma y DSP.
JUCE es el candidato principal para dispositivos, MIDI e interfaz; fijar una versión
compatible con Windows 10 y validar su licencia antes de incorporarla. No se ha añadido
JUCE ni cambiado la licencia MIT del proyecto. Su licencia actual es AGPLv3/comercial;
no debe tratarse como una dependencia MIT. Si mantener una distribución enteramente
permisiva es indispensable, evaluar una combinación de bibliotecas de audio/MIDI y UI
por separado, asumiendo mayor integración y mantenimiento.

No intentar recompilar SwiftUI/AVFoundation para Windows. Mantener la aplicación Mac
actual como referencia funcional, migrar módulos progresivamente y evitar dos motores
DSP distintos. La interfaz Mac podrá usar el núcleo común mediante ABI C más adelante.

## Inventario basado en el código
| Componente | Situación | Acción |
|---|---|---|
| DubDSP | 15 tipos de efecto, C11, atomics | Reutilizar y probar; no reescribir su sonido |
| AudioEngine.swift / AudioDevices.swift | AVFoundation y dispositivos Apple | Motor C++ y backend WASAPI; ASIO cuando el driver lo permita |
| MidiMix.swift | CoreMIDI; perfil y eventos abstractos ya definidos | Conservar tabla y semántica; sustituir transporte |
| MixController.swift | Reglas de mezcla y control | Trasladar con pruebas de equivalencia |
| BusPortal.swift / dub_portal.c | Envíos entre buses con un bloque de retraso | Conservar contrato temporal, validar tamaño de bloque y orden |
| SwiftUI / AppModel | Interfaz y coordinación macOS | Nueva UI nativa C++ y capa de aplicación |
| PluginHost.swift | Audio Units | Diferir; Windows requiere hosting VST3 y adaptación de sesiones |
| StemSplit | ONNX, bindings Apple, decodificación AVFoundation | API C/C++ ONNX CPU y nuevo decodificador fuera del hilo de audio |
| Proyectos / setlists | Persistencia Swift | Documentar esquema, fixtures y rutas relativas antes de migrar |
| Recording / export | Grabación y LAME | Cola de audio hacia escritor; revisar empaquetado de dependencias |
| Actualización / distribución | .app, scripts shell | Artefactos Windows específicos, instalador y actualizador posteriores |

## Motor y concurrencia
- Un solo hilo de render ejecuta el grafo inicialmente. Float32 estéreo planar coincide
  con dub_effect_process. Todos los stems comparten reloj y posición en muestras.
- Construir buffers, efectos y plan de procesamiento antes de arrancar. El callback
  no abre archivos, no decodifica, no descarga modelos, no asigna memoria ni espera locks.
- Carga y decodificación en workers; para streaming, lectura anticipada en buffers
  acotados con política explícita de underrun. Al principio usar stems precargados
  con límite de memoria comunicado antes de cargar.
- UI y MIDI envían comandos acotados. Usar una cola SPSC por productor, o serializar
  productores fuera del callback; no compartir una SPSC entre UI y MIDI directamente.
  Definir comportamiento de saturación: coalescer parámetros continuos y preservar
  acciones críticas de parada. Aplicar eventos por offset en muestras cuando exista timestamp.
- Mantener suavizado de ganancias, soft takeover, mute/solo y colas de efectos.
  Los faders físicos y sends no deben restaurarse de un proyecto contra la posición real.
- Medidores mediante snapshots atómicos, refresco de UI independiente del audio.
  MIDI LEDs se envían fuera del callback. La grabación usa un ring y worker de disco;
  un desbordamiento se reporta, nunca bloquea el render.
- Cambios de dispositivo o sample rate requieren detener/repreparar de forma controlada.
  Reconexión MIDI no debe reconstruir el grafo ni cortar el audio.

## Riesgos concretos encontrados
1. dub_internal.h y dub_portal.c usan _Atomic float y _Atomic long long. El primer
   build Windows propone ClangCL para C11; verificar atomics lock-free en la máquina objetivo.
2. dub_portal.c fija PORTAL_STEP a 1/480: su suavizado equivale a 10 ms solo a 48 kHz.
   Resolver después con sample rate explícito y una prueba de regresión; no cambiarlo
   silenciosamente en esta base. El portal depende del bloque actual para leer el anterior:
   adaptar callbacks variables a un quantum fijo o revisar cuidadosamente ese contrato.
3. dub_effect_prepare limita internamente a 96 kHz. Rechazar o convertir tasas superiores;
   no anunciar 192 kHz mientras el motor y los efectos procesan con tasas distintas.
4. PANIC llama prepare desde process; auditar coste máximo de limpieza y todos los prepare,
   incluso si no asignan memoria. Ausencia de malloc no garantiza cumplir el deadline.
5. Los plugins AU guardados no tienen equivalencia automática con VST3. Preservar sus
   datos al abrir proyectos y marcar los no disponibles; nunca sustituirlos silenciosamente.
6. El aislamiento de plugins en macOS no se obtiene automáticamente por usar JUCE:
   requerirá proceso auxiliar, IPC y política de latencia/fallo. Diferirlo del primer MVP.
7. La inferencia ONNX compite por CPU y memoria. Empezar por CPU, limitar threads,
   medir antes de habilitar procesamiento mientras se actúa en vivo. GPU queda posterior.

## Entregas con criterios de aceptación
1. **Base DSP (esta entrega):** CMake separado del Swift package, pruebas C y workflow
   Linux/macOS/Windows. Verificar en Windows antes de declarar compatibilidad.
2. **Primer recorrido de audio:** abrir WAV, seis strips sincronizados, tres buses,
   master y selección de dispositivo. Comprobar pausa, seek, fin de stems, longitudes
   diferentes, sample rates, desconexión y reinicio del dispositivo.
3. **MIDImix:** portar perfil exacto y lógica de control; probar CC, Note On con
   velocidad cero, Note Off, bancos, pickup y LEDs. Confirmar en hardware real.
4. **MVP para directo:** UI, efectos integrados, proyectos, grabación WAV y recuperación
   de errores. Mantener la separación de stems externa hasta completar este recorrido.
5. **Paridad avanzada:** ONNX, exportaciones y sets; luego hosting VST3 y aislamiento.
6. **Distribución:** instalación en Windows 10 limpio, dependencias fijadas y avisos,
   desinstalación, firma y actualización. Un runner Windows Server no reemplaza Windows 10.

## Validación y rendimiento
Usar capturas de referencia del Mac y tolerancias numéricas para impulsos, ruido,
colas, limiter y cambios de parámetros. Preservar latencia algorítmica y respuesta
musical, no exigir igualdad bit a bit entre compiladores sin justificarla.
Probar 44.1/48/96 kHz y bloques 64/128/256/512; empezar la integración en 48 kHz/256.
A 48 kHz, 128 muestras dan 2.67 ms por callback; no es la latencia total. El limiter
incluye lookahead y el dispositivo añade buffers. Medir latencia de salida/control;
round trip solo si se incorpora entrada de audio.
Objetivo propuesto de aceptación: dos horas de reproducción con seis stems, buses y
movimiento MIDI intenso sin dropouts en la configuración de referencia. Medir peor
callback, percentiles, underruns y memoria; repetir con grabación. No prometer el
mismo buffer en cualquier PC o driver.

## Compilar la base DSP en Windows
Instalar Visual Studio 2022 Build Tools con C++, Windows SDK y herramientas ClangCL,
además de CMake. Desde un terminal de desarrollo:

```powershell
cmake -S . -B build-portable -G "Visual Studio 17 2022" -A x64 -T ClangCL
cmake --build build-portable --config Release
ctest --test-dir build-portable -C Release --output-on-failure
```

Esto produce una biblioteca y un ejecutable de prueba, no DubStemMix.exe.
El workflow portable-dsp.yml queda preparado, pero no ha sido ejecutado en GitHub.
Validación local: GCC C11 con optimización; 15 efectos × 3 tasas × 6 bloques,
procesamiento in-place, bypass del sub, retraso del portal y atomics lock-free.
También AddressSanitizer/UndefinedBehaviorSanitizer, con leak detection desactivada
porque LeakSanitizer falla bajo ptrace en este entorno. No equivale a una auditoría
completa de concurrencia, fugas ni latencia en Windows. CMake no está instalado aquí;
la compilación local se hizo directamente con cc.

## Fuentes
- https://github.com/walkdread/DubStemMix/tree/172c98b1baf22f8fa7c5b3668fe774de9867e7bc
- https://docs.juce.com/master/juce__audio__devices_8h.html
- https://github.com/juce-framework/JUCE/blob/master/LICENSE.md
- https://onnxruntime.ai/docs/get-started/with-cpp.html


## Actualización: primera aplicación alpha
Se ha implementado un primer recorrido funcional con UI Win32, miniaudio 0.11.23
(WASAPI compartido) y MIDI WinMM, sin incorporar JUCE. Así se puede evaluar el
núcleo en Windows sin decidir todavía adoptar JUCE. CMake fija miniaudio por SHA.
La lógica de render C++ es independiente de Win32 y tiene pruebas sin hardware.
El alcance y las instrucciones se detallan en WINDOWS_PRIMEROS_PASOS.txt.
El MVP completo de la estrategia sigue pendiente: esta alpha no implementa proyectos,
grabación, separación ONNX ni paridad de controles/efectos con macOS.
CMake se instaló posteriormente en el entorno y ambas pruebas pasan mediante CTest.
La nueva UI requiere compilación Windows; no se ha probado aquí con hardware físico.
