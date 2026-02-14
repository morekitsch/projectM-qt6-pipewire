#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="${BUILD_DIR:-$ROOT_DIR/build-appimage}"
APPDIR="${APPDIR:-$ROOT_DIR/AppDir}"
APP_NAME="qt6mplayer"
DESKTOP_FILE="$ROOT_DIR/packaging/linux/qt6mplayer.desktop"
ICON_FILE="$ROOT_DIR/packaging/linux/qt6mplayer.svg"
OUT_DIR="${OUT_DIR:-$ROOT_DIR/dist}"
TOOLS_DIR="${TOOLS_DIR:-$ROOT_DIR/tools/linuxdeploy}"
ARCH="${ARCH:-$(uname -m)}"
VERSION="${VERSION:-$(date +%Y.%m.%d)}"
export APPIMAGE_EXTRACT_AND_RUN="${APPIMAGE_EXTRACT_AND_RUN:-1}"
# Newer distro toolchains may emit RELR sections that old strip inside linuxdeploy can't handle.
# Disable stripping by default for compatibility (can be overridden with NO_STRIP=0).
export NO_STRIP="${NO_STRIP:-1}"
# Some optional Qt imageformat plugins depend on libs not installed on all systems.
# Exclude known problematic optional deps by default.
DEFAULT_EXCLUDED_LIBS="libjxrglue.so*;kimg_jxr.so*"
if [[ -n "${LINUXDEPLOY_EXCLUDED_LIBRARIES:-}" ]]; then
  export LINUXDEPLOY_EXCLUDED_LIBRARIES="${LINUXDEPLOY_EXCLUDED_LIBRARIES};${DEFAULT_EXCLUDED_LIBS}"
else
  export LINUXDEPLOY_EXCLUDED_LIBRARIES="${DEFAULT_EXCLUDED_LIBS}"
fi

require_cmd() {
  if ! command -v "$1" >/dev/null 2>&1; then
    echo "Missing required command: $1" >&2
    exit 1
  fi
}

download_tool() {
  local url="$1"
  local out="$2"
  if command -v curl >/dev/null 2>&1; then
    curl -fsSL "$url" -o "$out"
    return
  fi
  if command -v wget >/dev/null 2>&1; then
    wget -qO "$out" "$url"
    return
  fi
  echo "Missing downloader: install curl or wget." >&2
  exit 1
}

resolve_linuxdeploy_tools() {
  local arch_label="$1"
  mkdir -p "$TOOLS_DIR"

  if command -v linuxdeploy >/dev/null 2>&1; then
    LINUXDEPLOY_BIN="$(command -v linuxdeploy)"
  else
    local linuxdeploy_appimage="$TOOLS_DIR/linuxdeploy-${arch_label}.AppImage"
    if [[ ! -x "$linuxdeploy_appimage" ]]; then
      echo "Downloading linuxdeploy for ${arch_label}..."
      download_tool \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-${arch_label}.AppImage" \
        "$linuxdeploy_appimage"
      chmod +x "$linuxdeploy_appimage"
    fi
    ln -sf "$(basename "$linuxdeploy_appimage")" "$TOOLS_DIR/linuxdeploy"
    LINUXDEPLOY_BIN="$TOOLS_DIR/linuxdeploy"
  fi

  if command -v linuxdeploy-plugin-qt >/dev/null 2>&1; then
    LINUXDEPLOY_QT_PLUGIN_BIN="$(command -v linuxdeploy-plugin-qt)"
  else
    local qt_plugin_appimage="$TOOLS_DIR/linuxdeploy-plugin-qt-${arch_label}.AppImage"
    if [[ ! -x "$qt_plugin_appimage" ]]; then
      echo "Downloading linuxdeploy-plugin-qt for ${arch_label}..."
      download_tool \
        "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-${arch_label}.AppImage" \
        "$qt_plugin_appimage"
      chmod +x "$qt_plugin_appimage"
    fi
    ln -sf "$(basename "$qt_plugin_appimage")" "$TOOLS_DIR/linuxdeploy-plugin-qt"
    LINUXDEPLOY_QT_PLUGIN_BIN="$TOOLS_DIR/linuxdeploy-plugin-qt"
  fi

  export PATH="$TOOLS_DIR:$PATH"
}

resolve_appimage_runtime() {
  local arch_label="$1"
  local runtime_file="$TOOLS_DIR/runtime-${arch_label}"

  if [[ ! -s "$runtime_file" ]]; then
    echo "Downloading AppImage runtime for ${arch_label}..."
    download_tool \
      "https://github.com/AppImage/type2-runtime/releases/download/continuous/runtime-${arch_label}" \
      "$runtime_file"
    chmod +x "$runtime_file" || true
  fi

  export LDAI_RUNTIME_FILE="$runtime_file"
}

resolve_qmake_qt6() {
  local candidates=()

  if [[ -n "${QMAKE:-}" ]]; then
    candidates+=("$QMAKE")
  fi

  candidates+=("qmake6" "qmake-qt6" "/usr/lib/qt6/bin/qmake" "qmake")

  for candidate in "${candidates[@]}"; do
    local qmake_bin=""
    if [[ "$candidate" == */* ]]; then
      if [[ -x "$candidate" ]]; then
        qmake_bin="$candidate"
      fi
    else
      qmake_bin="$(command -v "$candidate" 2>/dev/null || true)"
    fi

    if [[ -z "$qmake_bin" ]]; then
      continue
    fi

    local qt_version
    qt_version="$("$qmake_bin" -query QT_VERSION 2>/dev/null || true)"
    if [[ "$qt_version" == 6* ]]; then
      export QMAKE="$qmake_bin"
      echo "Using Qt6 qmake: $QMAKE (QT_VERSION=$qt_version)"
      return 0
    fi
  done

  echo "Could not find a Qt6 qmake binary." >&2
  echo "Install Qt6 tools and ensure one of these exists: qmake6, qmake-qt6, /usr/lib/qt6/bin/qmake." >&2
  return 1
}

require_cmd cmake
require_cmd find

case "$ARCH" in
  x86_64|aarch64) ;;
  *)
    echo "Unsupported ARCH for automatic linuxdeploy download: $ARCH" >&2
    echo "Set ARCH to x86_64 or aarch64, or install linuxdeploy manually." >&2
    exit 1
    ;;
esac

LINUXDEPLOY_BIN=""
LINUXDEPLOY_QT_PLUGIN_BIN=""
resolve_linuxdeploy_tools "$ARCH"
resolve_appimage_runtime "$ARCH"
resolve_qmake_qt6

if [[ ! -f "$DESKTOP_FILE" ]]; then
  echo "Desktop file not found: $DESKTOP_FILE" >&2
  exit 1
fi

if [[ ! -f "$ICON_FILE" ]]; then
  echo "Icon file not found: $ICON_FILE" >&2
  exit 1
fi

echo "[1/5] Configure"
cmake -S "$ROOT_DIR" -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release

echo "[2/5] Build"
cmake --build "$BUILD_DIR"

echo "[3/5] Install into AppDir"
rm -rf "$APPDIR"
DESTDIR="$APPDIR" cmake --install "$BUILD_DIR" --prefix /usr

echo "[4/5] Bundle AppDir"
mkdir -p "$OUT_DIR"
export VERSION
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
"$LINUXDEPLOY_BIN" \
  --appdir "$APPDIR" \
  --desktop-file "$DESKTOP_FILE" \
  --icon-file "$ICON_FILE" \
  --executable "$APPDIR/usr/bin/$APP_NAME" \
  --plugin qt

echo "[5/5] Create AppImage"
"$LINUXDEPLOY_BIN" \
  --appdir "$APPDIR" \
  --desktop-file "$DESKTOP_FILE" \
  --icon-file "$ICON_FILE" \
  --executable "$APPDIR/usr/bin/$APP_NAME" \
  --output appimage

APPIMAGE_BASENAME="${APP_NAME}-${VERSION}-${ARCH}.AppImage"
GENERATED_APPIMAGE="$(find "$ROOT_DIR" -maxdepth 1 -type f -name '*.AppImage' -printf '%f\n' | sort | tail -n 1)"

if [[ -n "$GENERATED_APPIMAGE" && -f "$ROOT_DIR/$GENERATED_APPIMAGE" ]]; then
  mv -f "$ROOT_DIR/$GENERATED_APPIMAGE" "$OUT_DIR/$APPIMAGE_BASENAME"
  echo "AppImage created: $OUT_DIR/$APPIMAGE_BASENAME"
else
  echo "AppImage creation did not produce an output file." >&2
  exit 1
fi
