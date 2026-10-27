# Windows alpha 0.3 — stems y cola

## Comparacion de fuentes (2026-10-26)

- [Demucs](https://github.com/facebookresearch/demucs): HTDemucs produce cuatro
  fuentes con un modelo; FT aplica cuatro especialistas. Su README describe FT
  como cuatro veces mas lento, pero no es una medicion en este PC.
- [StemSplit/demucs-onnx](https://github.com/StemSplit/demucs-onnx) y
  [catalogo](https://stemsplit.github.io/demucs-onnx/models/): exportaciones compatibles
  con el motor actual, misma entrada fija de 343980 muestras y cuatro salidas.
  Se integra HTDemucs fp16weights, revision d54ed9eb60e258ea82131c6ee14578628816456a,
  SHA256 d05c269d0178d2a72ad484b10b11dd370193fc923201c3b27a99f848745db70a.
  Los pesos fp16 reducen descarga; calculan en fp32, no garantizan menor VRAM.
- [Spleeter](https://github.com/deezer/spleeter): cuatro stems y aceleracion GPU;
  sus cifras publicadas no son benchmarks de una GTX 1650. Cambiar a TensorFlow
  ampliaria las dependencias del ejecutable, por eso no se integra en esta alpha.
- [DirectML EP](https://onnxruntime.ai/docs/execution-providers/DirectML-ExecutionProvider.html):
  permite usar DirectX 12 en Windows. Se desactiva memory pattern y se usa ejecucion
  secuencial; el menu enumera adaptadores DXGI para elegir NVIDIA explicitamente.
  Es experimental: CPU se valida en CI, no existe runner con la GTX del usuario.

## Arquitectura

Una cola FIFO en el hilo UI conserva rutas y estados. Un trabajador de preparacion
procesa un trabajo a la vez y solo escribe caches. Un segundo trabajador carga stems
seleccionados y los entrega al UI mediante release/acquire. Solo se intercambian
vectores del mezclador con el dispositivo detenido y transporte pausado. El callback
no hace inferencia, red, acceso a disco ni espera por esos trabajadores.

Lista y preferencias en LocalAppData/DubStemMix/library.ini; cache dentro de la carpeta
de datos configurada. Restaurar la lista no inicia descargas automaticamente.
GPU fallida se muestra como Error; usuario selecciona CPU y Reintentar.
No hay crossfade entre canciones ni reproduccion automatica de la lista.

## MIDImix

El [manual oficial](https://cdn.inmusicbrands.com/akai/attachments/MIDIMIX/MIDImix-UserGuide-v1.0.pdf)
describe SEND ALL como envio de las posiciones actuales. No se infiere Play de rafagas
CC: seria ambiguo y alteraria el control de mezcla. Bank Left/Right usan notas 25/26
segun el perfil existente; Play tiene nota 20 como alternativa y aprendizaje de notas.
La asignacion solicitada SEND ALL -> Play sigue sin implementarse por esta limitacion.

## Validacion

CTest del DSP/mezclador; compilacion y arranque/captura Windows. --check-separation
prueba FT y HTDemucs con audio sintetico real, forma de salida, finitud, energia,
alineacion, cache y render de mezclador mientras ejecuta inferencia. Esto no sustituye
una escucha musical ni una prueba de baja latencia con Xenyx/MIDImix/GTX 1650.
