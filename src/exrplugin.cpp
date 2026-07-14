// exrplugin.cpp
// IrfanView format plugin (EXR.dll) that displays multilayer OpenEXR files and
// lets the user switch layers in-place inside IrfanView via a global hotkey
// (Ctrl+Alt+Right / Ctrl+Alt+Left).
//
// Exports (undecorated, see exrplugin.def):
//   int   GetPlugInInfo(char* version, char* formats)
//   void* ReadEXR   (const char*    filename, void*, void*, wchar_t*, wchar_t*, int*, int*)
//   void* ReadEXR_W (const wchar_t* filename, void*, void*, wchar_t*, wchar_t*, int*, int*)
//
// Decode/tonemap logic is ported from exrlayers.cpp with the required fixes
// (UINT channels, Unicode-safe *FromMemory loading, non-finite depth -> black,
// size_t pixel arithmetic).

#define _CRT_SECURE_NO_WARNINGS

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

// DROPFILES lives in ShlObj_core.h in current SDKs (not shellapi.h). We only
// need this small, stable ABI struct for building an HDROP; define it directly
// to avoid pulling in the heavy shell/COM headers. WM_DROPFILES comes from
// windows.h.
#ifndef _DROPFILES_DEFINED_LOCAL
#define _DROPFILES_DEFINED_LOCAL
typedef struct _DROPFILES_LOCAL {
  DWORD pFiles;  // offset of the file list
  POINT pt;      // drop point
  BOOL fNC;      // is pt in non-client area
  BOOL fWide;    // TRUE => wide characters in the file list
} DROPFILES_LOCAL;
#endif

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cstdint>
#include <cmath>
#include <cwchar>
#include <string>
#include <vector>
#include <map>
#include <memory>
#include <algorithm>
#include <cctype>

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"

#define TINYEXR_IMPLEMENTATION
#define TINYEXR_USE_MINIZ 0
#define TINYEXR_USE_STB_ZLIB 1
#include "tinyexr.h"

// tinyexr's implementation references stbi_zlib_compress (used only on its
// save/compress path, which we never call) when TINYEXR_USE_STB_ZLIB=1. That
// symbol is provided by stb_image_write.h's implementation, so include it to
// satisfy the linker even though the DLL does not write images.
#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

// ---------------------------------------------------------------------------
// 8x8 bitmap font (public domain "font8x8_basic", dhepper) for ASCII 0x20-0x7F.
// Each glyph is 8 bytes (one per row); bit 0 (LSB) is the leftmost pixel.
// ---------------------------------------------------------------------------
static const unsigned char kFont8x8[96][8] = {
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x20 space
    {0x18,0x3C,0x3C,0x18,0x18,0x00,0x18,0x00}, // !
    {0x36,0x36,0x00,0x00,0x00,0x00,0x00,0x00}, // "
    {0x36,0x36,0x7F,0x36,0x7F,0x36,0x36,0x00}, // #
    {0x0C,0x3E,0x03,0x1E,0x30,0x1F,0x0C,0x00}, // $
    {0x00,0x63,0x33,0x18,0x0C,0x66,0x63,0x00}, // %
    {0x1C,0x36,0x1C,0x6E,0x3B,0x33,0x6E,0x00}, // &
    {0x06,0x06,0x03,0x00,0x00,0x00,0x00,0x00}, // '
    {0x18,0x0C,0x06,0x06,0x06,0x0C,0x18,0x00}, // (
    {0x06,0x0C,0x18,0x18,0x18,0x0C,0x06,0x00}, // )
    {0x00,0x66,0x3C,0xFF,0x3C,0x66,0x00,0x00}, // *
    {0x00,0x0C,0x0C,0x3F,0x0C,0x0C,0x00,0x00}, // +
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x06}, // ,
    {0x00,0x00,0x00,0x3F,0x00,0x00,0x00,0x00}, // -
    {0x00,0x00,0x00,0x00,0x00,0x0C,0x0C,0x00}, // .
    {0x60,0x30,0x18,0x0C,0x06,0x03,0x01,0x00}, // /
    {0x3E,0x63,0x73,0x7B,0x6F,0x67,0x3E,0x00}, // 0
    {0x0C,0x0E,0x0C,0x0C,0x0C,0x0C,0x3F,0x00}, // 1
    {0x1E,0x33,0x30,0x1C,0x06,0x33,0x3F,0x00}, // 2
    {0x1E,0x33,0x30,0x1C,0x30,0x33,0x1E,0x00}, // 3
    {0x38,0x3C,0x36,0x33,0x7F,0x30,0x78,0x00}, // 4
    {0x3F,0x03,0x1F,0x30,0x30,0x33,0x1E,0x00}, // 5
    {0x1C,0x06,0x03,0x1F,0x33,0x33,0x1E,0x00}, // 6
    {0x3F,0x33,0x30,0x18,0x0C,0x0C,0x0C,0x00}, // 7
    {0x1E,0x33,0x33,0x1E,0x33,0x33,0x1E,0x00}, // 8
    {0x1E,0x33,0x33,0x3E,0x30,0x18,0x0E,0x00}, // 9
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x00}, // :
    {0x00,0x0C,0x0C,0x00,0x00,0x0C,0x0C,0x06}, // ;
    {0x18,0x0C,0x06,0x03,0x06,0x0C,0x18,0x00}, // <
    {0x00,0x00,0x3F,0x00,0x00,0x3F,0x00,0x00}, // =
    {0x06,0x0C,0x18,0x30,0x18,0x0C,0x06,0x00}, // >
    {0x1E,0x33,0x30,0x18,0x0C,0x00,0x0C,0x00}, // ?
    {0x3E,0x63,0x7B,0x7B,0x7B,0x03,0x1E,0x00}, // @
    {0x0C,0x1E,0x33,0x33,0x3F,0x33,0x33,0x00}, // A
    {0x3F,0x66,0x66,0x3E,0x66,0x66,0x3F,0x00}, // B
    {0x3C,0x66,0x03,0x03,0x03,0x66,0x3C,0x00}, // C
    {0x1F,0x36,0x66,0x66,0x66,0x36,0x1F,0x00}, // D
    {0x7F,0x46,0x16,0x1E,0x16,0x46,0x7F,0x00}, // E
    {0x7F,0x46,0x16,0x1E,0x16,0x06,0x0F,0x00}, // F
    {0x3C,0x66,0x03,0x03,0x73,0x66,0x7C,0x00}, // G
    {0x33,0x33,0x33,0x3F,0x33,0x33,0x33,0x00}, // H
    {0x1E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // I
    {0x78,0x30,0x30,0x30,0x33,0x33,0x1E,0x00}, // J
    {0x67,0x66,0x36,0x1E,0x36,0x66,0x67,0x00}, // K
    {0x0F,0x06,0x06,0x06,0x46,0x66,0x7F,0x00}, // L
    {0x63,0x77,0x7F,0x7F,0x6B,0x63,0x63,0x00}, // M
    {0x63,0x67,0x6F,0x7B,0x73,0x63,0x63,0x00}, // N
    {0x1C,0x36,0x63,0x63,0x63,0x36,0x1C,0x00}, // O
    {0x3F,0x66,0x66,0x3E,0x06,0x06,0x0F,0x00}, // P
    {0x1E,0x33,0x33,0x33,0x3B,0x1E,0x38,0x00}, // Q
    {0x3F,0x66,0x66,0x3E,0x36,0x66,0x67,0x00}, // R
    {0x1E,0x33,0x07,0x0E,0x38,0x33,0x1E,0x00}, // S
    {0x3F,0x2D,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // T
    {0x33,0x33,0x33,0x33,0x33,0x33,0x3F,0x00}, // U
    {0x33,0x33,0x33,0x33,0x33,0x1E,0x0C,0x00}, // V
    {0x63,0x63,0x63,0x6B,0x7F,0x77,0x63,0x00}, // W
    {0x63,0x63,0x36,0x1C,0x1C,0x36,0x63,0x00}, // X
    {0x33,0x33,0x33,0x1E,0x0C,0x0C,0x1E,0x00}, // Y
    {0x7F,0x63,0x31,0x18,0x4C,0x66,0x7F,0x00}, // Z
    {0x1E,0x06,0x06,0x06,0x06,0x06,0x1E,0x00}, // [
    {0x03,0x06,0x0C,0x18,0x30,0x60,0x40,0x00}, // backslash
    {0x1E,0x18,0x18,0x18,0x18,0x18,0x1E,0x00}, // ]
    {0x08,0x1C,0x36,0x63,0x00,0x00,0x00,0x00}, // ^
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xFF}, // _
    {0x0C,0x0C,0x18,0x00,0x00,0x00,0x00,0x00}, // `
    {0x00,0x00,0x1E,0x30,0x3E,0x33,0x6E,0x00}, // a
    {0x07,0x06,0x06,0x3E,0x66,0x66,0x3B,0x00}, // b
    {0x00,0x00,0x1E,0x33,0x03,0x33,0x1E,0x00}, // c
    {0x38,0x30,0x30,0x3E,0x33,0x33,0x6E,0x00}, // d
    {0x00,0x00,0x1E,0x33,0x3F,0x03,0x1E,0x00}, // e
    {0x1C,0x36,0x06,0x0F,0x06,0x06,0x0F,0x00}, // f
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x1F}, // g
    {0x07,0x06,0x36,0x6E,0x66,0x66,0x67,0x00}, // h
    {0x0C,0x00,0x0E,0x0C,0x0C,0x0C,0x1E,0x00}, // i
    {0x30,0x00,0x30,0x30,0x30,0x33,0x33,0x1E}, // j
    {0x07,0x06,0x66,0x36,0x1E,0x36,0x67,0x00}, // k
    {0x0E,0x0C,0x0C,0x0C,0x0C,0x0C,0x1E,0x00}, // l
    {0x00,0x00,0x33,0x7F,0x7F,0x6B,0x63,0x00}, // m
    {0x00,0x00,0x1F,0x33,0x33,0x33,0x33,0x00}, // n
    {0x00,0x00,0x1E,0x33,0x33,0x33,0x1E,0x00}, // o
    {0x00,0x00,0x3B,0x66,0x66,0x3E,0x06,0x0F}, // p
    {0x00,0x00,0x6E,0x33,0x33,0x3E,0x30,0x78}, // q
    {0x00,0x00,0x3B,0x6E,0x66,0x06,0x0F,0x00}, // r
    {0x00,0x00,0x3E,0x03,0x1E,0x30,0x1F,0x00}, // s
    {0x08,0x0C,0x3E,0x0C,0x0C,0x2C,0x18,0x00}, // t
    {0x00,0x00,0x33,0x33,0x33,0x33,0x6E,0x00}, // u
    {0x00,0x00,0x33,0x33,0x33,0x1E,0x0C,0x00}, // v
    {0x00,0x00,0x63,0x6B,0x7F,0x7F,0x36,0x00}, // w
    {0x00,0x00,0x63,0x36,0x1C,0x36,0x63,0x00}, // x
    {0x00,0x00,0x33,0x33,0x33,0x3E,0x30,0x1F}, // y
    {0x00,0x00,0x3F,0x19,0x0C,0x26,0x3F,0x00}, // z
    {0x38,0x0C,0x0C,0x07,0x0C,0x0C,0x38,0x00}, // {
    {0x18,0x18,0x18,0x00,0x18,0x18,0x18,0x00}, // |
    {0x07,0x0C,0x0C,0x38,0x0C,0x0C,0x07,0x00}, // }
    {0x6E,0x3B,0x00,0x00,0x00,0x00,0x00,0x00}, // ~
    {0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00}, // 0x7F
};

// ---------------------------------------------------------------------------
// Small helpers (ported from exrlayers.cpp)
// ---------------------------------------------------------------------------

static std::string ToLowerAscii(const std::string &s) {
  std::string t = s;
  for (auto &c : t) c = (char)tolower((unsigned char)c);
  return t;
}

static float Clamp01(float x) {
  if (x < 0.0f) x = 0.0f;
  if (x > 1.0f) x = 1.0f;
  return x;
}

// Treat NaN/Inf as 0.
static float Clean(float x) { return std::isfinite(x) ? x : 0.0f; }

// linear -> sRGB, clamped to [0,1]
static float SrgbEncode(float x) {
  x = std::max(x, 0.0f);
  float v = (x <= 0.0031308f) ? (x * 12.92f)
                              : (1.055f * std::pow(x, 1.0f / 2.4f) - 0.055f);
  return Clamp01(v);
}

static unsigned char To8(float v01) {
  int iv = (int)std::lround(Clamp01(v01) * 255.0f);
  if (iv < 0) iv = 0;
  if (iv > 255) iv = 255;
  return (unsigned char)iv;
}

// Linear-interpolation percentile (numpy-style) over a pre-sorted ascending vector.
static float PercentileSorted(const std::vector<float> &sorted, double p) {
  if (sorted.empty()) return 0.0f;
  double pos = (p / 100.0) * (double)(sorted.size() - 1);
  size_t lo = (size_t)std::floor(pos);
  size_t hi = (size_t)std::ceil(pos);
  if (hi >= sorted.size()) hi = sorted.size() - 1;
  double frac = pos - (double)lo;
  return (float)(sorted[lo] + (sorted[hi] - sorted[lo]) * frac);
}

// ---------------------------------------------------------------------------
// Layer model
// ---------------------------------------------------------------------------

struct Layer {
  std::string name;                            // "" for top-level beauty
  std::map<std::string, const float *> comps;  // comp name -> pixel data
};

static bool IsColorLayer(const Layer &layer) {
  return layer.comps.count("R") || layer.comps.count("G") || layer.comps.count("B");
}

static bool IsDepthLikeOrdering(const std::string &name, const Layer &layer) {
  std::string lower = ToLowerAscii(name);
  if (lower.find("depth") != std::string::npos ||
      lower.find("position") != std::string::npos ||
      lower.find("normal") != std::string::npos) {
    return true;
  }
  if (layer.comps.size() == 1 && layer.comps.begin()->first == "Z") return true;
  return false;
}

static bool IsDepthLikeTonemap(const std::string &layerName, const std::string &compName) {
  if (ToLowerAscii(layerName).find("depth") != std::string::npos) return true;
  if (compName == "Z") return true;
  return false;
}

static void SortCaseInsensitive(std::vector<std::string> &v) {
  std::sort(v.begin(), v.end(), [](const std::string &a, const std::string &b) {
    return ToLowerAscii(a) < ToLowerAscii(b);
  });
}

// Display name for a raw layer name: "" (top-level beauty) shows as "beauty".
static std::string DisplayName(const std::string &raw) {
  return raw.empty() ? std::string("beauty") : raw;
}

// ---------------------------------------------------------------------------
// Decoded-file cache
// ---------------------------------------------------------------------------

struct DecodedFile {
  std::wstring path;
  uint64_t mtime = 0;
  int width = 0;
  int height = 0;
  std::vector<std::string> names;                     // ordered layer names ("" = beauty)
  std::vector<std::vector<unsigned char>> layerPix;   // per-layer bottom-up BGRX
};

// ---------------------------------------------------------------------------
// Global state (guard with CRITICAL_SECTION)
// ---------------------------------------------------------------------------

static CRITICAL_SECTION g_cs;
static bool g_csInit = false;
static int g_index = 0;
static std::wstring g_path;
static int g_count = 0;
static std::vector<std::string> g_names;
static std::string g_currentLayerName;  // sticky layer selection (display name)
static std::shared_ptr<DecodedFile> g_cache;  // guarded by g_cs

static HMODULE g_hSelf = nullptr;
static LONG g_initOnce = 0;
static HANDLE g_hookThread = nullptr;

// Stock-plugin fallback (EXR_original_backup.dll) state, guarded by g_cs.
typedef void *(*ReadEXRW_fn)(const wchar_t *, void *, void *, wchar_t *, wchar_t *, int *, int *);
static bool g_backupTried = false;
static HMODULE g_backupMod = nullptr;
static ReadEXRW_fn g_backupReadW = nullptr;

// RAII guard for tiny critical-section regions; guarantees LeaveCriticalSection
// even if the protected code throws (so a throw can never leave g_cs held).
struct CsGuard {
  explicit CsGuard(CRITICAL_SECTION *cs) : cs_(cs) { EnterCriticalSection(cs_); }
  ~CsGuard() { LeaveCriticalSection(cs_); }
  CsGuard(const CsGuard &) = delete;
  CsGuard &operator=(const CsGuard &) = delete;
  CRITICAL_SECTION *cs_;
};

static void EnsureCsInit() {
  // Safe to call before DllMain has run in the unlikely event; DllMain also
  // initializes it. Double init is avoided via g_csInit under a simple guard.
  if (!g_csInit) {
    InitializeCriticalSection(&g_cs);
    g_csInit = true;
  }
}

// ---------------------------------------------------------------------------
// File reading + mtime
// ---------------------------------------------------------------------------

static uint64_t FileTimeToU64(const FILETIME &ft) {
  return ((uint64_t)ft.dwHighDateTime << 32) | (uint64_t)ft.dwLowDateTime;
}

static bool GetFileMtime(const std::wstring &path, uint64_t &out) {
  WIN32_FILE_ATTRIBUTE_DATA fad;
  if (!GetFileAttributesExW(path.c_str(), GetFileExInfoStandard, &fad)) return false;
  out = FileTimeToU64(fad.ftLastWriteTime);
  return true;
}

static bool ReadWholeFileW(const std::wstring &path, std::vector<unsigned char> &out) {
  HANDLE hf = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                          nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
  if (hf == INVALID_HANDLE_VALUE) return false;
  LARGE_INTEGER sz;
  if (!GetFileSizeEx(hf, &sz) || sz.QuadPart <= 0) {
    CloseHandle(hf);
    return false;
  }
  out.resize((size_t)sz.QuadPart);
  size_t got = 0;
  bool ok = true;
  while (got < out.size()) {
    DWORD chunk = (DWORD)std::min<size_t>(out.size() - got, 1u << 24);
    DWORD rd = 0;
    if (!ReadFile(hf, out.data() + got, chunk, &rd, nullptr) || rd == 0) {
      ok = false;
      break;
    }
    got += rd;
  }
  CloseHandle(hf);
  return ok && got == out.size();
}

// ---------------------------------------------------------------------------
// Tone-map a single layer into a bottom-up BGRX buffer (row 0 = bottom image row)
// ---------------------------------------------------------------------------

static void TonemapLayerToBGRX(const Layer &layer, const std::string &layerName,
                               int w, int h, std::vector<unsigned char> &buf) {
  buf.assign((size_t)w * (size_t)h * 4u, 0);

  auto putRGB = [&](int x, int y, unsigned char r, unsigned char g, unsigned char b) {
    size_t bufRow = (size_t)(h - 1 - y);
    size_t o = (bufRow * (size_t)w + (size_t)x) * 4u;
    buf[o + 0] = b;
    buf[o + 1] = g;
    buf[o + 2] = r;
    buf[o + 3] = 0xFF;
  };

  if (IsColorLayer(layer)) {
    auto find = [&](const char *k) -> const float * {
      auto it = layer.comps.find(k);
      return it == layer.comps.end() ? nullptr : it->second;
    };
    const float *rc = find("R");
    const float *gc = find("G");
    const float *bc = find("B");
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        size_t idx = (size_t)y * (size_t)w + (size_t)x;
        float r = rc ? Clean(rc[idx]) : 0.0f;
        float g = gc ? Clean(gc[idx]) : 0.0f;
        float b = bc ? Clean(bc[idx]) : 0.0f;
        putRGB(x, y, To8(SrgbEncode(r)), To8(SrgbEncode(g)), To8(SrgbEncode(b)));
      }
    }
    return;
  }

  // Scalar layer.
  const std::string &compName = layer.comps.begin()->first;
  const float *a = layer.comps.begin()->second;

  std::vector<float> finiteVals;
  finiteVals.reserve((size_t)w * (size_t)h);
  for (size_t idx = 0; idx < (size_t)w * (size_t)h; idx++) {
    float v = a[idx];
    if (std::isfinite(v)) finiteVals.push_back(v);
  }

  if (IsDepthLikeTonemap(layerName, compName)) {
    std::sort(finiteVals.begin(), finiteVals.end());
    float lo = PercentileSorted(finiteVals, 1.0);
    float hi = PercentileSorted(finiteVals, 99.0);
    float range = hi - lo;
    if (range < 1e-12f) range = 1e-12f;
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        size_t idx = (size_t)y * (size_t)w + (size_t)x;
        float orig = a[idx];
        unsigned char g8;
        if (!std::isfinite(orig)) {
          // Non-finite depth (e.g. Z = +Inf background) -> FAR/BLACK.
          g8 = 0;
        } else {
          float n = Clamp01((orig - lo) / range);
          n = 1.0f - n;  // near = bright
          g8 = To8(n);
        }
        putRGB(x, y, g8, g8, g8);
      }
    }
  } else {
    float mx = 0.0f;
    for (float v : finiteVals) mx = std::max(mx, v);
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        size_t idx = (size_t)y * (size_t)w + (size_t)x;
        float v = Clean(a[idx]);
        float n = (mx > 1.0f) ? Clamp01(v / mx) : Clamp01(v);
        n = SrgbEncode(n);
        unsigned char g8 = To8(n);
        putRGB(x, y, g8, g8, g8);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Decode an EXR file (all layers) into a DecodedFile. Unicode-safe (memory API).
// Returns true on success; on failure writes a wide error into errOut.
// ---------------------------------------------------------------------------

static bool DecodeFile(const std::wstring &path, uint64_t mtime, DecodedFile &out,
                       std::wstring &errOut) {
  std::vector<unsigned char> bytes;
  if (!ReadWholeFileW(path, bytes)) {
    errOut = L"EXR: cannot open/read file";
    return false;
  }

  EXRVersion version;
  int ret = ParseEXRVersionFromMemory(&version, bytes.data(), bytes.size());
  if (ret != 0) {
    errOut = L"EXR: not a valid OpenEXR file";
    return false;
  }

  // Multipart is not supported by our tinyexr path yet; signal the caller to
  // delegate to the stock plugin by returning false.
  if (version.multipart) {
    errOut = L"EXR: multipart (delegating to stock plugin)";
    return false;
  }

  const char *err = nullptr;
  EXRHeader header;
  EXRImage image;
  InitEXRHeader(&header);
  InitEXRImage(&image);

  ret = ParseEXRHeaderFromMemory(&header, &version, bytes.data(), bytes.size(), &err);
  if (ret != 0) {
    errOut = L"EXR: failed to parse header";
    if (err) FreeEXRErrorMessage(err);
    return false;
  }
  for (int c = 0; c < header.num_channels; c++) {
    if (header.pixel_types[c] == TINYEXR_PIXELTYPE_HALF)
      header.requested_pixel_types[c] = TINYEXR_PIXELTYPE_FLOAT;
    else
      header.requested_pixel_types[c] = header.pixel_types[c];
  }
  ret = LoadEXRImageFromMemory(&image, &header, bytes.data(), bytes.size(), &err);
  if (ret != 0) {
    errOut = L"EXR: failed to load image";
    if (err) FreeEXRErrorMessage(err);
    FreeEXRHeader(&header);
    return false;
  }

  // From here `image` and `header` are allocated and MUST be freed on every
  // path. Wrap the heavy processing so a std::bad_alloc still frees them and
  // degrades to a decode failure (the caller then falls back / returns NULL).
  bool ok = true;
  try {
    if (image.images == nullptr) {
      errOut = L"EXR: tiled images are not supported";  // -> fallback
      ok = false;
    }

    int w = image.width;
    int h = image.height;

    if (ok && (w <= 0 || h <= 0)) {
      errOut = L"EXR: invalid image dimensions";
      ok = false;
    }

    if (ok) {
      // Materialize per-channel float data (convert UINT to float in our code).
      std::vector<std::vector<float>> convertedStorage;
      // Reserve so pointers into convertedStorage stay valid.
      convertedStorage.reserve(header.num_channels);
      std::vector<const float *> channelData(header.num_channels);
      for (int c = 0; c < header.num_channels; c++) {
        if (header.pixel_types[c] == TINYEXR_PIXELTYPE_FLOAT) {
          channelData[c] = reinterpret_cast<const float *>(image.images[c]);
        } else if (header.pixel_types[c] == TINYEXR_PIXELTYPE_UINT) {
          const uint32_t *up = reinterpret_cast<const uint32_t *>(image.images[c]);
          std::vector<float> bufv((size_t)w * (size_t)h);
          for (size_t idx = 0; idx < bufv.size(); idx++) bufv[idx] = (float)up[idx];
          convertedStorage.push_back(std::move(bufv));
          channelData[c] = convertedStorage.back().data();
        } else {
          std::vector<float> bufv((size_t)w * (size_t)h, 0.0f);
          convertedStorage.push_back(std::move(bufv));
          channelData[c] = convertedStorage.back().data();
        }
      }

      // Group channels into layers (split on LAST '.').
      std::map<std::string, Layer> layers;
      for (int c = 0; c < header.num_channels; c++) {
        std::string chName = header.channels[c].name;
        std::string layerName, compName;
        size_t dot = chName.rfind('.');
        if (dot != std::string::npos) {
          layerName = chName.substr(0, dot);
          compName = chName.substr(dot + 1);
        } else {
          layerName = "";
          compName = chName;
        }
        Layer &layer = layers[layerName];
        layer.name = layerName;
        layer.comps[compName] = channelData[c];
      }

      // Order: beauty, then sorted others, then depth-like last.
      std::vector<std::string> beautyBucket, normalBucket, depthBucket;
      for (auto &kv : layers) {
        const std::string &name = kv.first;
        if (name.empty()) beautyBucket.push_back(name);
        else if (IsDepthLikeOrdering(name, kv.second)) depthBucket.push_back(name);
        else normalBucket.push_back(name);
      }
      SortCaseInsensitive(normalBucket);
      SortCaseInsensitive(depthBucket);

      std::vector<std::string> orderedNames;
      orderedNames.insert(orderedNames.end(), beautyBucket.begin(), beautyBucket.end());
      orderedNames.insert(orderedNames.end(), normalBucket.begin(), normalBucket.end());
      orderedNames.insert(orderedNames.end(), depthBucket.begin(), depthBucket.end());

      out.path = path;
      out.mtime = mtime;
      out.width = w;
      out.height = h;
      out.names.clear();
      out.layerPix.clear();
      out.names.reserve(orderedNames.size());
      out.layerPix.reserve(orderedNames.size());

      for (const std::string &layerName : orderedNames) {
        Layer &layer = layers[layerName];
        std::vector<unsigned char> pix;
        TonemapLayerToBGRX(layer, layerName, w, h, pix);
        out.names.push_back(layerName);
        out.layerPix.push_back(std::move(pix));
      }

      if (out.names.empty()) {
        errOut = L"EXR: no layers found";
        ok = false;
      }
    }
  } catch (...) {
    errOut = L"EXR: out of memory while decoding";
    ok = false;
  }

  // Cleanup tinyexr allocations (always).
  FreeEXRImage(&image);
  FreeEXRHeader(&header);

  return ok;
}

// ---------------------------------------------------------------------------
// Caption overlay onto a bottom-up BGRX pixel buffer
// ---------------------------------------------------------------------------

static void DrawCaption(unsigned char *px, int w, int h, const std::string &text) {
  if (w <= 0 || h <= 0 || text.empty()) return;

  const int scale = 2;
  const int glyphW = 8 * scale;  // 16
  const int glyphH = 8 * scale;  // 16
  const int pad = 3;

  auto setPx = [&](int x, int y, unsigned char r, unsigned char g, unsigned char b) {
    if (x < 0 || x >= w || y < 0 || y >= h) return;
    size_t bufRow = (size_t)(h - 1 - y);
    size_t o = (bufRow * (size_t)w + (size_t)x) * 4u;
    px[o + 0] = b;
    px[o + 1] = g;
    px[o + 2] = r;
    px[o + 3] = 0xFF;
  };
  auto darkenPx = [&](int x, int y) {
    if (x < 0 || x >= w || y < 0 || y >= h) return;
    size_t bufRow = (size_t)(h - 1 - y);
    size_t o = (bufRow * (size_t)w + (size_t)x) * 4u;
    px[o + 0] = (unsigned char)((px[o + 0] * 2) / 5);  // ~40% -> 60% black overlay
    px[o + 1] = (unsigned char)((px[o + 1] * 2) / 5);
    px[o + 2] = (unsigned char)((px[o + 2] * 2) / 5);
  };

  int barW = pad * 2 + (int)text.size() * glyphW;
  int barH = pad * 2 + glyphH;
  if (barW > w) barW = w;
  if (barH > h) barH = h;

  // Dark background bar in the top-left (image coords y=0 is top).
  for (int y = 0; y < barH; y++)
    for (int x = 0; x < barW; x++) darkenPx(x, y);

  // White glyphs.
  for (size_t i = 0; i < text.size(); i++) {
    unsigned char ch = (unsigned char)text[i];
    if (ch < 0x20 || ch > 0x7F) ch = '?';
    const unsigned char *glyph = kFont8x8[ch - 0x20];
    int x0 = pad + (int)i * glyphW;
    int y0 = pad;
    for (int row = 0; row < 8; row++) {
      unsigned char bits = glyph[row];
      for (int col = 0; col < 8; col++) {
        if (bits & (1 << col)) {
          for (int dy = 0; dy < scale; dy++)
            for (int dx = 0; dx < scale; dx++)
              setPx(x0 + col * scale + dx, y0 + row * scale + dy, 0xFF, 0xFF, 0xFF);
        }
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Build the returned DIB (HGLOBAL) for a given layer index of a decoded file.
// Reads only its arguments (no globals), so it needs no lock: the caller holds
// a shared_ptr keeping `df` alive.
// ---------------------------------------------------------------------------

static void *BuildDIB(const DecodedFile &df, int idx, int count) {
  int w = df.width;
  int h = df.height;
  size_t pixBytes = (size_t)w * (size_t)h * 4u;
  size_t total = 40 + pixBytes;

  HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, total);
  if (!hg) return nullptr;
  void *p = GlobalLock(hg);
  if (!p) {
    GlobalFree(hg);
    return nullptr;
  }

  BITMAPINFOHEADER bih;
  memset(&bih, 0, sizeof(bih));
  bih.biSize = 40;
  bih.biWidth = w;
  bih.biHeight = h;  // positive => bottom-up
  bih.biPlanes = 1;
  bih.biBitCount = 32;
  bih.biCompression = BI_RGB;  // 0
  memcpy(p, &bih, sizeof(bih));

  unsigned char *px = (unsigned char *)p + 40;
  memcpy(px, df.layerPix[idx].data(), pixBytes);

  // Caption "[i+1/count] name".
  std::string layerName = DisplayName(df.names[idx]);
  char capbuf[512];
  std::snprintf(capbuf, sizeof(capbuf), "[%d/%d] %s", idx + 1, count, layerName.c_str());
  DrawCaption(px, w, h, capbuf);

  GlobalUnlock(hg);
  return hg;  // return the HGLOBAL handle (IrfanView takes ownership)
}

// ---------------------------------------------------------------------------
// Hotkey handling
// ---------------------------------------------------------------------------

static bool EndsWithExrCI(const std::wstring &p) {
  if (p.size() < 4) return false;
  std::wstring ext = p.substr(p.size() - 4);
  for (auto &c : ext) c = (wchar_t)towlower(c);
  return ext == L".exr";
}

static void PostDropReload(HWND target, const std::wstring &path) {
  size_t pathChars = path.size() + 1;  // include terminating null
  size_t total = sizeof(DROPFILES_LOCAL) + pathChars * sizeof(wchar_t) + sizeof(wchar_t);  // + extra null
  HGLOBAL hg = GlobalAlloc(GHND, total);  // GHND = MOVEABLE | ZEROINIT
  if (!hg) return;
  DROPFILES_LOCAL *df = (DROPFILES_LOCAL *)GlobalLock(hg);
  if (!df) {
    GlobalFree(hg);
    return;
  }
  df->pFiles = sizeof(DROPFILES_LOCAL);
  df->fWide = TRUE;
  wchar_t *dst = (wchar_t *)((unsigned char *)df + sizeof(DROPFILES_LOCAL));
  memcpy(dst, path.c_str(), pathChars * sizeof(wchar_t));
  // Trailing double-null guaranteed by GHND zero-init.
  GlobalUnlock(hg);
  if (!PostMessageW(target, WM_DROPFILES, (WPARAM)hg, 0)) {
    GlobalFree(hg);  // only free if the post failed (otherwise receiver owns it)
  }
}

// Advance/retreat the layer index and re-drop the same path. Returns true if
// the key was consumed.
static bool HandleSwitch(int dir) {
  HWND fg = GetForegroundWindow();
  if (!fg) return false;
  DWORD pid = 0;
  GetWindowThreadProcessId(fg, &pid);
  if (pid != GetCurrentProcessId()) return false;

  std::wstring path;
  bool act = false;
  {
    // Tiny locked region (RAII): just advance the index and read the path, so
    // this never waits behind a decode (which now runs outside the lock).
    CsGuard g(&g_cs);
    if (g_count > 1 && EndsWithExrCI(g_path)) {
      g_index = ((g_index + dir) % g_count + g_count) % g_count;
      // Update the sticky target so the newly chosen pass persists across files.
      if (g_index >= 0 && g_index < (int)g_names.size())
        g_currentLayerName = DisplayName(g_names[g_index]);
      path = g_path;
      act = true;
    }
  }

  if (!act) return false;
  PostDropReload(fg, path);
  return true;
}

static LRESULT CALLBACK LowLevelKbdProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
    const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)lParam;
    if (k->vkCode == VK_RIGHT || k->vkCode == VK_LEFT) {
      bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
      bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
      if (ctrl && alt) {
        int dir = (k->vkCode == VK_RIGHT) ? +1 : -1;
        try {
          if (HandleSwitch(dir)) return 1;  // swallow the key
        } catch (...) {
          // Never let an exception escape the hook callback.
        }
      }
    }
  }
  return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

static DWORD WINAPI HookThreadProc(LPVOID) {
  HHOOK hook = SetWindowsHookExW(WH_KEYBOARD_LL, LowLevelKbdProc, g_hSelf, 0);
  if (!hook) return 0;
  MSG msg;
  while (GetMessage(&msg, nullptr, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessage(&msg);
  }
  UnhookWindowsHookEx(hook);
  return 0;
}

static void OneTimeInit() {
  if (InterlockedCompareExchange(&g_initOnce, 1, 0) != 0) return;
  // Pin the DLL so it is never unloaded.
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                     (LPCWSTR)&OneTimeInit, &g_hSelf);
  // Start the hotkey hook thread.
  g_hookThread = CreateThread(nullptr, 0, HookThreadProc, nullptr, 0, nullptr);
}

// ---------------------------------------------------------------------------
// Core read routine (shared by ReadEXR and ReadEXR_W)
// ---------------------------------------------------------------------------

static void SafeWriteWide(wchar_t *dst, const wchar_t *src, int capGuess) {
  // We do not know the caller's buffer size; write a short, bounded string.
  if (!dst) return;
  int i = 0;
  for (; src[i] && i < capGuess - 1; i++) dst[i] = src[i];
  dst[i] = 0;
}

// Delegate to the stock OpenEXR plugin (renamed EXR_original_backup.dll, sitting
// next to our EXR.dll) for files tinyexr cannot handle (DWAA/DWAB, tiled,
// multipart, corrupt). Loads the backup once, lazily. Returns the backup's
// HGLOBAL (IrfanView owns it) or NULL. *backupAvail reports whether a usable
// backup ReadEXR_W was found (so the caller knows whose error message to keep).
static void *TryBackupFallback(const wchar_t *filename, void *a2, void *a3,
                               wchar_t *statusText, wchar_t *formatText,
                               int *widthOut, int *heightOut, bool *backupAvail) {
  bool tried;
  ReadEXRW_fn fn;
  {
    CsGuard g(&g_cs);
    tried = g_backupTried;
    fn = g_backupReadW;
  }

  if (!tried) {
    // Resolve backup path from our own module and load it OUTSIDE the lock so
    // the LL keyboard hook never blocks behind LoadLibrary.
    HMODULE mod = nullptr;
    ReadEXRW_fn resolved = nullptr;
    wchar_t modpath[1024];
    DWORD n = GetModuleFileNameW(g_hSelf, modpath, 1024);
    if (n > 0 && n < 1024) {
      std::wstring p(modpath);
      size_t slash = p.find_last_of(L"\\/");
      std::wstring dir = (slash == std::wstring::npos) ? std::wstring() : p.substr(0, slash + 1);
      std::wstring backup = dir + L"EXR_original_backup.dll";
      mod = LoadLibraryW(backup.c_str());
      if (mod) resolved = (ReadEXRW_fn)GetProcAddress(mod, "ReadEXR_W");
    }
    {
      CsGuard g(&g_cs);
      if (!g_backupTried) {
        g_backupTried = true;
        g_backupMod = mod;
        g_backupReadW = resolved;
      } else if (mod && mod != g_backupMod) {
        FreeLibrary(mod);  // lost a race; keep the first winner
      }
      fn = g_backupReadW;
    }
  }

  if (backupAvail) *backupAvail = (fn != nullptr);
  if (!fn) return nullptr;

  void *r = fn(filename, a2, a3, statusText, formatText, widthOut, heightOut);
  if (r) {
    // Fallback file shows as a single "layer"; the hotkey won't try to cycle.
    // Leave g_currentLayerName untouched so sticky selection survives across a
    // fallback file back to the next scanline multilayer EXR.
    CsGuard g(&g_cs);
    g_path = filename ? filename : L"";
    g_count = 1;
    g_names.clear();
  }
  return r;
}

static void *ReadCore(const wchar_t *filename, void *a2, void *a3,
                      wchar_t *statusText, wchar_t *formatText,
                      int *widthOut, int *heightOut) {
  EnsureCsInit();
  OneTimeInit();

  if (!filename || !filename[0]) {
    SafeWriteWide(statusText, L"EXR: empty filename", 64);
    return nullptr;
  }
  std::wstring path = filename;

  uint64_t mtime = 0;
  GetFileMtime(path, mtime);  // best-effort; 0 if unavailable

  // --- Tiny lock: cache lookup. Keep a shared_ptr so the data stays alive
  //     while we build the DIB outside the lock. ---
  std::shared_ptr<DecodedFile> local;
  {
    CsGuard g(&g_cs);
    if (g_cache && g_cache->path == path && g_cache->mtime == mtime &&
        !g_cache->layerPix.empty()) {
      local = g_cache;
    }
  }

  // --- Decode OUTSIDE any lock on a cache miss. ---
  if (!local) {
    auto decoded = std::make_shared<DecodedFile>();
    std::wstring errOut;
    if (DecodeFile(path, mtime, *decoded, errOut)) {
      CsGuard g(&g_cs);
      if (g_cache && g_cache->path == path && g_cache->mtime == mtime &&
          !g_cache->layerPix.empty()) {
        local = g_cache;  // another thread already inserted it
      } else {
        g_cache = decoded;
        local = decoded;
      }
    } else {
      // tinyexr can't handle this file -> delegate to the stock plugin.
      bool backupAvail = false;
      void *fb = TryBackupFallback(filename, a2, a3, statusText, formatText,
                                   widthOut, heightOut, &backupAvail);
      if (fb) return fb;
      if (backupAvail) return nullptr;  // backup ran and wrote its own status
      SafeWriteWide(statusText, errOut.empty() ? L"EXR: decode failed" : errOut.c_str(), 96);
      return nullptr;
    }
  }

  // --- Layer selection (sticky by name / forced). Read the env var outside
  //     the lock; the lock only touches the shared state. ---
  wchar_t envbuf[32];
  DWORD en = GetEnvironmentVariableW(L"EXRLAYER_FORCE_INDEX", envbuf, 32);
  bool forced = (en > 0 && en < 32);
  int forcedIdx = forced ? _wtoi(envbuf) : 0;

  int count = (int)local->names.size();
  int idx = 0;
  {
    CsGuard g(&g_cs);
    g_path = path;
    g_count = count;
    g_names = local->names;
    if (forced) {
      // 1) Forced index (test hook): use it AND update the sticky target.
      if (forcedIdx < 0) forcedIdx = 0;
      if (forcedIdx >= count) forcedIdx = count - 1;
      g_index = forcedIdx;
      g_currentLayerName = DisplayName(g_names[g_index]);
    } else if (!g_currentLayerName.empty()) {
      // 2) Sticky: if a layer with the same display name exists here, show it;
      //    otherwise fall back to beauty (index 0).
      int found = -1;
      for (int i = 0; i < count; i++) {
        if (DisplayName(g_names[i]) == g_currentLayerName) { found = i; break; }
      }
      if (found >= 0) {
        g_index = found;
      } else {
        g_index = 0;
        g_currentLayerName = DisplayName(g_names[0]);
      }
    } else {
      // 3) First-ever call: beauty.
      g_index = 0;
      g_currentLayerName = DisplayName(g_names[0]);
    }
    idx = g_index;
  }

  // --- Build the DIB OUTSIDE the lock (memcpy + caption) from the shared_ptr. ---
  void *dib = BuildDIB(*local, idx, count);
  if (!dib) {
    SafeWriteWide(statusText, L"EXR: out of memory", 64);
    return nullptr;
  }

  if (widthOut) *widthOut = local->width;
  if (heightOut) *heightOut = local->height;

  if (formatText) {
    wchar_t fbuf[64];
    std::swprintf(fbuf, 64, L"EXR - layer %d/%d", idx + 1, count);
    SafeWriteWide(formatText, fbuf, 64);
  }

  return dib;
}

// ---------------------------------------------------------------------------
// Exported entry points
// ---------------------------------------------------------------------------

extern "C" int GetPlugInInfo(char *version, char *formats) {
  if (version) strcpy(version, "1.0");
  if (formats) strcpy(formats, "EXR Format - EXR");
  return 0;
}

extern "C" void *ReadEXR_W(const wchar_t *filename, void *a2, void *a3,
                           wchar_t *statusText, wchar_t *formatText,
                           int *widthOut, int *heightOut) {
  // Exception boundary: nothing may propagate out of the DLL into IrfanView.
  try {
    return ReadCore(filename, a2, a3, statusText, formatText, widthOut, heightOut);
  } catch (...) {
    SafeWriteWide(statusText, L"EXR: internal error", 64);
    return nullptr;
  }
}

extern "C" void *ReadEXR(const char *filename, void *a2, void *a3,
                         wchar_t *statusText, wchar_t *formatText,
                         int *widthOut, int *heightOut) {
  // Exception boundary: nothing may propagate out of the DLL into IrfanView.
  try {
    std::wstring wide;
    if (filename && filename[0]) {
      int need = MultiByteToWideChar(CP_ACP, 0, filename, -1, nullptr, 0);
      if (need > 0) {
        wide.resize(need);
        MultiByteToWideChar(CP_ACP, 0, filename, -1, &wide[0], need);
        if (!wide.empty() && wide.back() == L'\0') wide.pop_back();
      }
    }
    return ReadCore(wide.c_str(), a2, a3, statusText, formatText, widthOut, heightOut);
  } catch (...) {
    SafeWriteWide(statusText, L"EXR: internal error", 64);
    return nullptr;
  }
}

// ---------------------------------------------------------------------------
// DllMain
// ---------------------------------------------------------------------------

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID reserved) {
  (void)hInst;
  (void)reserved;
  switch (reason) {
    case DLL_PROCESS_ATTACH:
      DisableThreadLibraryCalls(hInst);
      EnsureCsInit();
      break;
    default:
      break;
  }
  return TRUE;
}
