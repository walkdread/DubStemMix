# =============================================================================
# DubStemMix Windows - build, test, run and package
# =============================================================================
#
# Proyecto: consola de dub reggae para Akai MIDImix.
# Rama:     port/windows-alpha
#
# Uso rapido:
#   make doctor      comprueba el toolchain
#   make build       configura CMake y compila la app + tests
#   make test        ejecuta CTest
#   make run         compila y abre DubStemMix.exe
#   make package     genera dist/DubStemMix-Windows-alpha/
#   make zip         genera el ZIP portable
#
# Requisitos:
#   - Windows 10 22H2 / Windows 11 x64
#   - CMake >= 3.20
#   - Ninja (recomendado) o MinGW Make
#   - GCC >= 11, Clang >= 16 o MSVC 2022 con Windows SDK
#   - Git (CMake FetchContent descarga miniaudio la primera vez)
#
# En este equipo el toolchain portable vive en D: porque C: tiene poco espacio:
#   D:/tools/w64devkit/w64devkit      GCC 16.2, CMake 4.4.4, Ninja, Make, sh
#   D:/tools/cmake-4.4.4-windows-x86_64  CMake/CTest alternativos
#
# Si usas otro toolchain, sobreescribe las variables:
#   make TOOLCHAIN=C:/msys64/mingw64 build
#   make CMAKE=cmake CTEST=ctest NINJA=ninja CC=gcc CXX=g++ build
#
# Para Visual Studio (otro generador), usa CMake directamente; este Makefile
# esta optimizado para Ninja + MinGW/GCC y para el paquete portable.
# =============================================================================

.DEFAULT_GOAL := help
.PHONY: help all doctor configure build rebuild test run package zip clean distclean info web-install web-dev web-build

# ---------------------------------------------------------------------------
# Toolchain (sobreescribible desde la linea de comandos)
# ---------------------------------------------------------------------------
TOOLCHAIN ?= D:/tools/w64devkit/w64devkit
CMAKE     ?= $(TOOLCHAIN)/bin/cmake.exe
CTEST     ?= $(TOOLCHAIN)/bin/ctest.exe
NINJA     ?= $(TOOLCHAIN)/bin/ninja.exe
CC_BIN    ?= $(TOOLCHAIN)/bin/gcc.exe
CXX_BIN   ?= $(TOOLCHAIN)/bin/g++.exe

# Si las rutas portables no existen, intenta las herramientas del PATH.
ifeq ($(wildcard $(CMAKE)),)
CMAKE := cmake
endif
ifeq ($(wildcard $(CTEST)),)
CTEST := ctest
endif
ifeq ($(wildcard $(NINJA)),)
NINJA := ninja
endif
ifeq ($(wildcard $(CC_BIN)),)
CC_BIN := gcc
endif
ifeq ($(wildcard $(CXX_BIN)),)
CXX_BIN := g++
endif

# GNU Make necesita un shell POSIX para las recetas. w64devkit lo incluye.
SHELL := $(firstword $(wildcard $(TOOLCHAIN)/bin/sh.exe) $(wildcard /bin/sh.exe) sh)

# ---------------------------------------------------------------------------
# Build
# ---------------------------------------------------------------------------
BUILD_DIR ?= build-windows
CONFIG    ?= Release
GENERATOR ?= Ninja
JOBS      ?= 2
APP       ?= $(BUILD_DIR)/DubStemMix.exe
DIST_DIR  ?= dist/DubStemMix-Windows-alpha
ZIP_FILE  ?= dist/DubStemMix-Windows-x64-alpha.zip

# ---------------------------------------------------------------------------
# Ayuda
# ---------------------------------------------------------------------------
help:
	@echo "DubStemMix Windows - Makefile"
	@echo ""
	@echo "Uso: make <target> [VARIABLE=valor]"
	@echo ""
	@echo "Targets:"
	@echo "  help         Muestra esta ayuda (por defecto)."
	@echo "  doctor       Comprueba toolchain, compilador y dependencias."
	@echo "  configure    Configura CMake en $(BUILD_DIR)/."
	@echo "  build        Compila DubStemMix.exe y los tests."
	@echo "  rebuild      Limpia y vuelve a compilar."
	@echo "  test         Ejecuta la suite de CTest."
	@echo "  run          Compila y abre la aplicacion."
	@echo "  package      Genera $(DIST_DIR)/ con exe, licencias y LEEME."
	@echo "  zip          Genera $(ZIP_FILE)."
	@echo "  clean        Borra $(BUILD_DIR)/."
	@echo "  distclean    Borra build y dist."
	@echo "  info         Muestra las variables de build."
	@echo "  web-install  Instala dependencias de web/ (requiere Node.js)."
	@echo "  web-dev      Sirve la web en http://localhost:4321/DubStemMix/."
	@echo "  web-build    Compila la web estatica en web/dist/."
	@echo ""
	@echo "Ejemplos:"
	@echo "  make doctor"
	@echo "  make build JOBS=4"
	@echo "  make test"
	@echo "  make run"
	@echo "  make package"
	@echo "  make zip"
	@echo "  make clean"
	@echo ""
	@echo "Toolchain por defecto:"
	@echo "  TOOLCHAIN=$(TOOLCHAIN)"
	@echo "  CMAKE=$(CMAKE)"
	@echo "  CC=$(CC_BIN)"
	@echo "  CXX=$(CXX_BIN)"
	@echo ""
	@echo "Notas:"
	@echo "  - La primera configuracion descarga miniaudio con FetchContent."
	@echo "  - El paquete usa enlace estatico de GCC; no necesita DLLs extra."
	@echo "  - Para MSVC usa CMake directamente (ver README.md)."

all: build

# ---------------------------------------------------------------------------
# Diagnostico
# ---------------------------------------------------------------------------
doctor:
	@echo "== DubStemMix Windows - doctor =="
	@echo "Proyecto:  $(CURDIR)"
	@echo "Toolchain: $(TOOLCHAIN)"
	@echo ""
	@echo "CMake:"
	@@"$(CMAKE)" --version 2>/dev/null || echo "  [FALTA] CMake no encontrado"
	@echo ""
	@echo "CTest:"
	@@"$(CTEST)" --version 2>/dev/null || echo "  [FALTA] CTest no encontrado"
	@echo ""
	@echo "Ninja:"
	@@"$(NINJA)" --version 2>/dev/null || echo "  [FALTA] Ninja no encontrado"
	@echo ""
	@echo "Compilador C:"
	@@"$(CC_BIN)" --version 2>/dev/null || echo "  [FALTA] compilador C no encontrado"
	@echo ""
	@echo "Compilador C++:"
	@@"$(CXX_BIN)" --version 2>/dev/null || echo "  [FALTA] compilador C++ no encontrado"
	@echo ""
	@echo "Git:"
	@git --version 2>/dev/null || echo "  [FALTA] Git no encontrado"
	@echo ""
	@echo "Node/npm (opcional, solo para web/):"
	@npm --version 2>/dev/null || echo "  no instalado"
	@echo ""
	@echo "Resumen:"
	@command -v "$(CMAKE)" >/dev/null 2>&1 && echo "  [OK] CMake" || echo "  [FALTA] CMake"
	@command -v "$(CTEST)" >/dev/null 2>&1 && echo "  [OK] CTest" || echo "  [FALTA] CTest"
	@command -v "$(NINJA)" >/dev/null 2>&1 && echo "  [OK] Ninja" || echo "  [FALTA] Ninja"
	@command -v "$(CC_BIN)" >/dev/null 2>&1 && echo "  [OK] C compiler" || echo "  [FALTA] C compiler"
	@command -v "$(CXX_BIN)" >/dev/null 2>&1 && echo "  [OK] C++ compiler" || echo "  [FALTA] C++ compiler"

# ---------------------------------------------------------------------------
# Configuracion y compilacion
# ---------------------------------------------------------------------------
configure:
	@echo "== Configurando $(BUILD_DIR) ($(CONFIG), $(GENERATOR)) =="
	@"$(CMAKE)" -S . -B "$(BUILD_DIR)" -G "$(GENERATOR)" -DCMAKE_BUILD_TYPE="$(CONFIG)" -DCMAKE_C_COMPILER="$(CC_BIN)" -DCMAKE_CXX_COMPILER="$(CXX_BIN)" -DCMAKE_MAKE_PROGRAM="$(NINJA)" -DDUB_BUILD_WINDOWS_APP=ON

build: configure
	@echo "== Compilando DubStemMix =="
	@"$(CMAKE)" --build "$(BUILD_DIR)" --parallel "$(JOBS)"
	@echo "Listo: $(APP)"

rebuild: clean build

# ---------------------------------------------------------------------------
# Pruebas y ejecucion
# ---------------------------------------------------------------------------
test: build
	@echo "== Ejecutando CTest =="
	@"$(CTEST)" --test-dir "$(BUILD_DIR)" -C "$(CONFIG)" --output-on-failure

run: build
	@echo "== Abriendo $(APP) =="
	@"$(APP)"

# ---------------------------------------------------------------------------
# Paquete portable
# ---------------------------------------------------------------------------
package: build
	@echo "== Generando paquete en $(DIST_DIR) =="
	@rm -rf "$(DIST_DIR)"
	@"$(CMAKE)" --install "$(BUILD_DIR)" --prefix "$(CURDIR)/$(DIST_DIR)"
	@echo "Paquete listo: $(DIST_DIR)"

zip: package
	@echo "== Generando $(ZIP_FILE) =="
	@powershell -NoProfile -ExecutionPolicy Bypass -Command "Compress-Archive -Path '$(CURDIR)/$(DIST_DIR)/*' -DestinationPath '$(CURDIR)/$(ZIP_FILE)' -Force"
	@echo "ZIP listo: $(ZIP_FILE)"

# ---------------------------------------------------------------------------
# Limpieza e informacion
# ---------------------------------------------------------------------------
clean:
	@echo "== Borrando $(BUILD_DIR) =="
	@rm -rf "$(BUILD_DIR)"

distclean: clean
	@echo "== Borrando dist/ =="
	@rm -rf "dist"

info:
	@echo "TOOLCHAIN = $(TOOLCHAIN)"
	@echo "CMAKE     = $(CMAKE)"
	@echo "CTEST     = $(CTEST)"
	@echo "NINJA     = $(NINJA)"
	@echo "CC_BIN    = $(CC_BIN)"
	@echo "CXX_BIN   = $(CXX_BIN)"
	@echo "BUILD_DIR = $(BUILD_DIR)"
	@echo "CONFIG    = $(CONFIG)"
	@echo "GENERATOR = $(GENERATOR)"
	@echo "JOBS      = $(JOBS)"
	@echo "APP       = $(APP)"
	@echo "DIST_DIR  = $(DIST_DIR)"
	@echo "ZIP_FILE  = $(ZIP_FILE)"

# ---------------------------------------------------------------------------
# Sitio web / documentacion (opcional)
# ---------------------------------------------------------------------------
web-install:
	@command -v npm >/dev/null 2>&1 || { echo "npm no esta instalado. Instala Node.js 20+ para compilar web/."; exit 1; }
	npm --prefix web install

web-dev: web-install
	npm --prefix web run dev

web-build: web-install
	npm --prefix web run build
