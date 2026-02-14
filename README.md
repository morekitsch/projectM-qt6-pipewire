# qt6mplayer

Qt6 desktop shell for `projectM` with PipeWire audio input on Linux.

## For Users

### Download

1. Open the GitHub **Releases** page.
2. Download `qt6mplayer-<version>-<arch>.AppImage`.

### Run

```bash
chmod +x qt6mplayer-*.AppImage
./qt6mplayer-*.AppImage
```

### First-time Setup

1. Open **Settings**.
2. In **Audio Input**, click **Refresh**.
3. Pick one device and keep it.

The selected input is persisted and reused on next launches.

### Shortcuts

- `F11`: Toggle preview fullscreen.
- `Esc`: Exit fullscreen preview and redock.

### Notes

- If PipeWire is unavailable, app falls back to dummy audio input.
- Provide your own preset folder (for example `~/.projectM/presets`) and select it in the UI.

## For Developers

### Build From Source

#### Arch Linux

```bash
sudo pacman -S --needed cmake ninja gcc pkgconf qt6-base projectm libpipewire
cmake -S . -B build -G Ninja
cmake --build build
./build/qt6mplayer
```

#### Ubuntu/Debian (example)

```bash
sudo apt install cmake ninja-build g++ pkg-config qt6-base-dev libpipewire-0.3-dev
sudo apt install libprojectm-dev   # if available in your distro/repo
cmake -S . -B build -G Ninja
cmake --build build
./build/qt6mplayer
```

### Build AppImage

```bash
./scripts/build-appimage.sh
```

Output:

- `dist/qt6mplayer-<version>-<arch>.AppImage`

The script auto-downloads official `linuxdeploy` and `linuxdeploy-plugin-qt` AppImages into:

- `tools/linuxdeploy/`

if they are not already installed in `PATH`.

Compatibility note:

- The script sets `NO_STRIP=1` by default to avoid linuxdeploy strip failures on RELR-enabled binaries.
- You can override this with `NO_STRIP=0 ./scripts/build-appimage.sh` if you explicitly want stripping.

### Release On GitHub

This repo includes:

- `.github/workflows/release-appimage.yml`

Workflow behavior:

1. Push a tag like `v0.1.0`.
2. GitHub Actions builds the AppImage.
3. The AppImage is uploaded to that tag's Release automatically.

## Data Storage

Under `QStandardPaths::AppDataLocation`:

- `preset-metadata.json`
- `playlists/*.json`
- Qt settings under `projectm`

## Implemented Features

- Preset browser with search/favorites/metadata editing
- Playlist save/load/import/export and playback controls
- projectM OpenGL render path with fallback renderer
- Floatable/fullscreen preview dock and FPS overlay
- PipeWire audio input backend with dummy fallback
- Settings-tab audio device picker and debug panel
