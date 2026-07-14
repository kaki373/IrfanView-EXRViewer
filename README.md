# IrfanView EXR Viewer

A drop-in replacement for IrfanView's EXR plugin (`EXR.dll`) that displays
**multilayer OpenEXR** files and lets you **switch layers / render passes
in-place, inside the same IrfanView window**, with a hotkey.

![beauty](docs/img/screenshot-beauty.png)
![depth](docs/img/screenshot-depth.png)

## Features

- **In-window layer switching** — `Ctrl+Alt+Right` / `Ctrl+Alt+Left` cycle
  through the layers of a multilayer EXR (beauty, diffuse, specular, depth, …)
  without opening a new window. The current layer is shown as a `[n/N] name`
  caption in the top-left.
- **Sticky layer across a sequence** — pick a pass (e.g. `depth`), then use
  IrfanView's normal *next image* key (`Right` / `Space` / `PageDown`) to walk
  an EXR image sequence: every frame keeps showing that same pass. An EXR that
  lacks the pass falls back to `beauty`.
- **Never worse than stock** — files the bundled decoder can't handle
  (DWAA/DWAB compression, tiled, multipart) are transparently delegated to the
  original OpenEXR plugin, so they still display (beauty only, no switching).
- **No runtime dependencies** — the prebuilt DLL is statically linked and
  imports only `USER32`/`KERNEL32` (no VC++ redistributable needed).

## How it works

The plugin decodes the EXR with [tinyexr](https://github.com/syoyo/tinyexr),
tone-maps each layer to a 32-bpp DIB, and keeps a global "current layer" index.
The hotkey advances that index and posts a `WM_DROPFILES` message to IrfanView's
own window; IrfanView reloads the file in place and calls the plugin again,
which now returns the newly selected layer. A low-level keyboard hook (installed
once; the DLL pins itself) captures the hotkey only while an IrfanView window is
in the foreground and an `.exr` is loaded.

## Install (prebuilt)

Requires **64-bit IrfanView** (`i_view64.exe`).

1. Download / clone this repo.
2. Run `dist/install.bat` **as Administrator**. It backs up the stock
   `EXR.dll` to `EXR_original_backup.dll` and installs the new one.

Manual alternative: in `C:\Program Files\IrfanView\Plugins`, rename the existing
`EXR.dll` to `EXR_original_backup.dll` (keep it — the fallback uses it), then
copy `dist/EXR.dll` into that folder.

Uninstall: run `dist/uninstall.bat` as Administrator (restores the stock DLL).

> **Keep `EXR_original_backup.dll` next to `EXR.dll`.** The DWAA/tiled/multipart
> fallback loads it from the same folder.

## Usage

- Open an `.exr` in IrfanView → it shows the beauty layer with a `[1/4] beauty`
  caption.
- `Ctrl+Alt+Right` / `Ctrl+Alt+Left` → next / previous layer (same window).
- Switch to a pass, then press IrfanView's *next image* key to browse a
  sequence while staying on that pass.

## Build from source

Needs 64-bit MSVC (Visual Studio 2022 or Build Tools, "Desktop development with
C++"). From a normal Command Prompt:

```bat
build.bat
```

This produces `dist\EXR.dll`. The build is `/MT` (static CRT) so the output has
no VC++ runtime dependency. Single-header dependencies are vendored in
`third_party/`.

## Limitations

- 64-bit IrfanView only (the DLL is x64).
- DWAA/DWAB, tiled, and multipart EXRs are shown via the stock fallback (beauty
  only, no layer switching).
- Tone-mapping is fixed: color passes use linear→sRGB; depth-like passes are
  percentile-normalized with near = bright and a non-finite (Inf) background
  mapped to black.
- Environment variable `EXRLAYER_FORCE_INDEX` forces a specific layer index
  (used for testing).

## 日本語

IrfanView の EXR プラグイン差し替え版です。マルチレイヤ EXR を開き、
`Ctrl+Alt+←/→` で同じウィンドウのままレイヤ（beauty / diffuse / specular /
depth など）を切り替えられます。あるレイヤ（例: depth）に切り替えた状態で、
IrfanView 通常の「次の画像」キーで連番を送ると、各フレームも同じレイヤを
表示します。DWAA/タイル/マルチパートなど本プラグインのデコーダで扱えない
ファイルは、退避した純正プラグインへ自動フォールバックして表示します。
導入は `dist/install.bat` を管理者実行（詳細は `dist/README.txt`）。**64bit 版
IrfanView 専用**です。

## License

MIT — see [LICENSE](LICENSE). Bundles third-party single-header libraries under
their own permissive licenses; see
[third_party/THIRD_PARTY_NOTICES.md](third_party/THIRD_PARTY_NOTICES.md).
Not affiliated with or endorsed by IrfanView.
