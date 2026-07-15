// exrplugin.cpp
// IrfanView format plugin (EXR.dll) that displays multilayer OpenEXR files and
// lets the user switch layers in-place inside IrfanView via a global hotkey
// (Ctrl+Alt+Right / Ctrl+Alt+Left).
//
// Decoder: real OpenEXR (static libs), fed from in-memory file bytes through a
// custom Imf::IStream (Unicode-safe, no fopen). Metadata (layers) is parsed up
// front; pixels are decoded LAZILY, one layer at a time, and tone-mapped to a
// cached bottom-up BGRX buffer. Handles DWAA/DWAB, tiled, and multipart EXRs
// and exposes every part's layers for switching.
//
// Exports (undecorated, see exrplugin.def):
//   int   GetPlugInInfo(char* version, char* formats)
//   void* ReadEXR   (const char*    filename, void*, void*, wchar_t*, wchar_t*, int*, int*)
//   void* ReadEXR_W (const wchar_t* filename, void*, void*, wchar_t*, wchar_t*, int*, int*)

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
#include <thread>
#include <algorithm>
#include <cctype>

// ---- OpenEXR (static) ----
#include <ImfMultiPartInputFile.h>
#include <ImfInputPart.h>
#include <ImfChannelList.h>
#include <ImfFrameBuffer.h>
#include <ImfHeader.h>
#include <ImfPixelType.h>
#include <ImfIO.h>
#include <ImfThreading.h>
#include <ImathBox.h>
#include <Iex.h>

// Baked ACES 2.0 SDR sRGB output transform (ACEScg/AP1 input) as a 33^3 3D LUT.
#include "aces_lut.h"

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
// Small helpers
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

// Linear-interpolation percentile (numpy-style) computed with std::nth_element
// instead of a full sort. Reorders `v` in place. Same result as sorting.
static float PercentileNth(std::vector<float> &v, double p) {
  size_t n = v.size();
  if (n == 0) return 0.0f;
  double pos = (p / 100.0) * (double)(n - 1);
  size_t lo = (size_t)std::floor(pos);
  size_t hi = (size_t)std::ceil(pos);
  if (hi >= n) hi = n - 1;
  double frac = pos - (double)lo;

  std::nth_element(v.begin(), v.begin() + lo, v.end());
  float vlo = v[lo];
  float vhi = vlo;
  if (hi != lo)  // element at sorted-position lo+1 = min of the upper partition
    vhi = *std::min_element(v.begin() + (lo + 1), v.end());
  return (float)(vlo + (vhi - vlo) * frac);
}

// Precomputed linear -> sRGB 8-bit lookup table (built once in OneTimeInit).
// 13-bit index resolution; banding is invisible at 8-bit output.
static const int kSrgbLutSize = 8192;
static unsigned char g_srgbLut[kSrgbLutSize];

static void BuildSrgbLut() {
  for (int i = 0; i < kSrgbLutSize; i++) {
    float x = (float)i / (float)(kSrgbLutSize - 1);
    g_srgbLut[i] = To8(SrgbEncode(x));
  }
}

static inline unsigned char SrgbLut8(float x) {
  if (x <= 0.0f) return g_srgbLut[0];
  if (x >= 1.0f) return g_srgbLut[kSrgbLutSize - 1];
  int idx = (int)(x * (float)(kSrgbLutSize - 1) + 0.5f);  // round, not truncate
  if (idx < 0) idx = 0;
  if (idx >= kSrgbLutSize) idx = kSrgbLutSize - 1;
  return g_srgbLut[idx];
}

// ---------------------------------------------------------------------------
// ACES 2.0 SDR sRGB output transform via the baked 33^3 3D LUT (aces_lut.h).
// Input is ACEScg (AP1) linear; output is final 8-bit sRGB.
// ---------------------------------------------------------------------------

// Per-channel log2 shaper -> normalized [0,1] grid coordinate.
static inline float AcesShape(float x) {
  x = Clean(x);  // NaN/Inf -> 0
  const float lo = exp2f(kAcesLutMin);
  const float hi = exp2f(kAcesLutMax);
  if (x < lo) x = lo;
  if (x > hi) x = hi;
  float n = (log2f(x) - kAcesLutMin) / (kAcesLutMax - kAcesLutMin);
  return Clamp01(n);
}

// Tetrahedral lookup of an ACEScg triplet, producing 8-bit sRGB output.
static void AcesLut8(float r, float g, float b, unsigned char &oR, unsigned char &oG,
                     unsigned char &oB) {
  const int N = kAcesLutN;
  float pr = AcesShape(r) * (float)(N - 1);
  float pg = AcesShape(g) * (float)(N - 1);
  float pb = AcesShape(b) * (float)(N - 1);

  int i0r = (int)floorf(pr), i0g = (int)floorf(pg), i0b = (int)floorf(pb);
  if (i0r < 0) i0r = 0; if (i0r > N - 2) i0r = N - 2;
  if (i0g < 0) i0g = 0; if (i0g > N - 2) i0g = N - 2;
  if (i0b < 0) i0b = 0; if (i0b > N - 2) i0b = N - 2;
  float fr = pr - (float)i0r, fg = pg - (float)i0g, fb = pb - (float)i0b;

  // Corner base offsets in the flat LUT (index = ((r*N+g)*N+b)*3 + ch).
  const size_t sR = (size_t)N * N * 3, sG = (size_t)N * 3, sB = 3;
  const size_t base = (((size_t)i0r * N + i0g) * N + i0b) * 3;
  const size_t o000 = base, o100 = base + sR, o010 = base + sG, o001 = base + sB;
  const size_t o110 = base + sR + sG, o101 = base + sR + sB, o011 = base + sG + sB;
  const size_t o111 = base + sR + sG + sB;

  unsigned char *out[3] = {&oR, &oG, &oB};
  for (int ch = 0; ch < 3; ch++) {
    float C000 = kAcesLut[o000 + ch], C100 = kAcesLut[o100 + ch];
    float C010 = kAcesLut[o010 + ch], C001 = kAcesLut[o001 + ch];
    float C110 = kAcesLut[o110 + ch], C101 = kAcesLut[o101 + ch];
    float C011 = kAcesLut[o011 + ch], C111 = kAcesLut[o111 + ch];
    float o;
    if (fr > fg) {
      if (fg > fb)      o = C000 + fr * (C100 - C000) + fg * (C110 - C100) + fb * (C111 - C110);
      else if (fr > fb) o = C000 + fr * (C100 - C000) + fb * (C101 - C100) + fg * (C111 - C101);
      else              o = C000 + fb * (C001 - C000) + fr * (C101 - C001) + fg * (C111 - C101);
    } else {
      if (fb > fg)      o = C000 + fb * (C001 - C000) + fg * (C011 - C001) + fr * (C111 - C011);
      else if (fb > fr) o = C000 + fg * (C010 - C000) + fb * (C011 - C010) + fr * (C111 - C011);
      else              o = C000 + fg * (C010 - C000) + fr * (C110 - C010) + fb * (C111 - C110);
    }
    int iv = (int)(o + 0.5f);
    if (iv < 0) iv = 0;
    if (iv > 255) iv = 255;
    *out[ch] = (unsigned char)iv;
  }
}

static void SortCaseInsensitive(std::vector<std::string> &v) {
  std::sort(v.begin(), v.end(), [](const std::string &a, const std::string &b) {
    return ToLowerAscii(a) < ToLowerAscii(b);
  });
}

// A layer's channels: component name -> full channel name (e.g. "R" -> "R",
// "red" -> "VRaySamplerInfo_World.red").
typedef std::map<std::string, std::string> CompMap;

// Find the full channel name for a role, matching component names case-
// insensitively against the given candidates. Returns "" if none present.
static std::string FindRole(const CompMap &comps, const char *a, const char *b) {
  for (const auto &kv : comps) {
    std::string lc = ToLowerAscii(kv.first);
    if (lc == a || (b && lc == b)) return kv.second;
  }
  return std::string();
}

// Color layer if it has any of R/G/B or red/green/blue (case-insensitive).
static bool IsColorComps(const CompMap &comps) {
  return !FindRole(comps, "r", "red").empty() ||
         !FindRole(comps, "g", "green").empty() ||
         !FindRole(comps, "b", "blue").empty();
}

// Ordering-time depth-like test: name matches depth|position|normal, OR the
// layer's only component is "Z".
static bool IsDepthLikeOrdering(const std::string &name, const CompMap &comps) {
  std::string lower = ToLowerAscii(name);
  if (lower.find("depth") != std::string::npos ||
      lower.find("position") != std::string::npos ||
      lower.find("normal") != std::string::npos) {
    return true;
  }
  if (comps.size() == 1 && comps.begin()->first == "Z") return true;
  return false;
}

// Tone-mapping-time depth-like test: layer name contains "depth", or the scalar
// component is exactly "Z".
static bool IsDepthLikeTonemap(const std::string &layerName, const std::string &compName) {
  if (ToLowerAscii(layerName).find("depth") != std::string::npos) return true;
  if (compName == "Z") return true;
  return false;
}

// ---------------------------------------------------------------------------
// Lazy-decode cache model
// ---------------------------------------------------------------------------

struct LayerDesc {
  int part = 0;
  int dispW = 0, dispH = 0;   // display window dims = the DIB / output size
  int ox = 0, oy = 0;         // data-window origin within the display window
  std::string display;        // ordered display name (part-prefixed if multipart)
  bool isColor = false;
  std::string rName, gName, bName;  // full channel names for color roles
  std::string scalarName;           // full channel name for scalar layers
  bool depthLike = false;           // scalar depth tonemap
};

struct DecodedFile {
  std::wstring path;
  uint64_t mtime = 0;
  std::vector<unsigned char> bytes;                    // whole file (for lazy decode)
  std::vector<LayerDesc> layers;                       // ordered
  std::vector<std::vector<unsigned char>> bgrx;        // per-layer tone-mapped BGRX (lazy)
  std::vector<int> lru;                                // decoded indices, oldest..newest
  bool acesMode = true;                                // color mode the cached BGRX was built with
};

// ---------------------------------------------------------------------------
// Global state (guard with CRITICAL_SECTION)
// ---------------------------------------------------------------------------

static CRITICAL_SECTION g_cs;
static bool g_csInit = false;
static int g_index = 0;
static std::wstring g_path;
static int g_count = 0;
static std::vector<std::string> g_names;       // display names of current file
static std::string g_currentLayerName;         // sticky selection (display name)
static std::shared_ptr<DecodedFile> g_cache;   // single cache slot, guarded by g_cs
static bool g_reloadPending = false;           // one hotkey reload in flight at a time
static ULONGLONG g_reloadStamp = 0;            // tick of last posted reload (stall recovery)
static bool g_acesView = true;                 // color view transform: true=ACES 2.0, false=sRGB

static HMODULE g_hSelf = nullptr;
static LONG g_initOnce = 0;
static HANDLE g_hookThread = nullptr;

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
  if (!g_csInit) {
    InitializeCriticalSection(&g_cs);
    g_csInit = true;
  }
}

static int DecodeThreads() {
  unsigned n = std::thread::hardware_concurrency();
  if (n == 0) n = 4;
  if (n > 8) n = 8;
  return (int)n;
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
// In-memory Imf::IStream over a cached byte buffer (Unicode-safe: no fopen).
// ---------------------------------------------------------------------------

class MemIStream : public Imf::IStream {
 public:
  MemIStream(const char *name, const unsigned char *data, size_t size)
      : Imf::IStream(name), data_(data), size_(size), pos_(0) {}

  bool isMemoryMapped() const override { return true; }

  char *readMemoryMapped(int n) override {
    if (n < 0 || pos_ + (size_t)n > size_)
      throw IEX_NAMESPACE::InputExc("Unexpected end of file (memory-mapped read).");
    char *r = (char *)(data_ + pos_);
    pos_ += (size_t)n;
    return r;
  }

  bool read(char c[], int n) override {
    if (n < 0 || pos_ + (size_t)n > size_)
      throw IEX_NAMESPACE::InputExc("Unexpected end of file.");
    std::memcpy(c, data_ + pos_, (size_t)n);
    pos_ += (size_t)n;
    return pos_ < size_;  // false when the last byte was read
  }

  uint64_t tellg() override { return (uint64_t)pos_; }
  void seekg(uint64_t pos) override { pos_ = (size_t)pos; }
  int64_t size() override { return (int64_t)size_; }

 private:
  const unsigned char *data_;
  size_t size_;
  size_t pos_;
};

// ---------------------------------------------------------------------------
// Tone-map decoded float channels of a single layer into a bottom-up BGRX
// buffer (buffer row 0 = bottom image row).
// ---------------------------------------------------------------------------

// Decode float channels of the DATA window (w x h) and composite them into a
// DISPLAY-sized (dispW x dispH) bottom-up BGRX buffer at offset (ox,oy), with
// black outside the data region. For the common case dispW==w, dispH==h,
// ox==oy==0 this is byte-identical to filling a data-sized buffer.
static void TonemapToBGRX(int w, int h, int dispW, int dispH, int ox, int oy, bool isColor,
                          const float *rc, const float *gc, const float *bc,
                          const float *scalar, bool depthLike, bool aces,
                          std::vector<unsigned char> &buf) {
  buf.assign((size_t)dispW * (size_t)dispH * 4u, 0);  // zero => black margins

  auto putRGB = [&](int x, int y, unsigned char r, unsigned char g, unsigned char b) {
    int X = x + ox, Y = y + oy;
    if (X < 0 || X >= dispW || Y < 0 || Y >= dispH) return;  // clip to display window
    size_t bufRow = (size_t)(dispH - 1 - Y);
    size_t o = (bufRow * (size_t)dispW + (size_t)X) * 4u;
    buf[o + 0] = b;
    buf[o + 1] = g;
    buf[o + 2] = r;
    buf[o + 3] = 0xFF;
  };

  if (isColor) {
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        size_t idx = (size_t)y * (size_t)w + (size_t)x;
        float r = rc ? Clean(rc[idx]) : 0.0f;
        float g = gc ? Clean(gc[idx]) : 0.0f;
        float b = bc ? Clean(bc[idx]) : 0.0f;
        if (aces) {
          unsigned char R, G, B;
          AcesLut8(r, g, b, R, G, B);  // ACEScg(AP1) -> ACES 2.0 SDR sRGB
          putRGB(x, y, R, G, B);
        } else {
          putRGB(x, y, SrgbLut8(r), SrgbLut8(g), SrgbLut8(b));  // plain linear->sRGB
        }
      }
    }
    return;
  }

  // Scalar layer.
  const float *a = scalar;
  std::vector<float> finiteVals;
  finiteVals.reserve((size_t)w * (size_t)h);
  for (size_t idx = 0; idx < (size_t)w * (size_t)h; idx++) {
    float v = a[idx];
    if (std::isfinite(v)) finiteVals.push_back(v);
  }

  if (depthLike) {
    float lo = PercentileNth(finiteVals, 1.0);
    float hi = PercentileNth(finiteVals, 99.0);
    float range = hi - lo;
    if (range < 1e-12f) range = 1e-12f;
    for (int y = 0; y < h; y++) {
      for (int x = 0; x < w; x++) {
        size_t idx = (size_t)y * (size_t)w + (size_t)x;
        float orig = a[idx];
        unsigned char g8;
        if (!std::isfinite(orig)) {
          g8 = 0;  // non-finite depth (e.g. Z = +Inf background) -> FAR/BLACK
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
        unsigned char g8 = SrgbLut8(n);
        putRGB(x, y, g8, g8, g8);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Parse metadata only (no pixels): open the file over MemIStream, iterate all
// parts + channels, build the ordered layer list. Returns false on failure.
// ---------------------------------------------------------------------------

static bool ParseMetadata(DecodedFile &df, std::wstring &errOut) {
  try {
    MemIStream mis("exr", df.bytes.data(), df.bytes.size());
    Imf::MultiPartInputFile mpf(mis, DecodeThreads());
    int nparts = mpf.parts();
    bool multi = nparts > 1;

    for (int p = 0; p < nparts; p++) {
      const Imf::Header &hdr = mpf.header(p);

      // Skip deep parts (not supported by flat InputPart reads).
      if (hdr.hasType()) {
        const std::string &t = hdr.type();
        if (t == "deepscanline" || t == "deeptile") continue;
      }

      const Imath::Box2i dw = hdr.dataWindow();
      const Imath::Box2i disp = hdr.displayWindow();
      int w = dw.max.x - dw.min.x + 1;
      int h = dw.max.y - dw.min.y + 1;
      int dispW = disp.max.x - disp.min.x + 1;
      int dispH = disp.max.y - disp.min.y + 1;
      if (w <= 0 || h <= 0 || dispW <= 0 || dispH <= 0) continue;
      int ox = dw.min.x - disp.min.x;  // data-window origin within display window
      int oy = dw.min.y - disp.min.y;

      std::string partName = hdr.hasName() ? hdr.name() : ("part" + std::to_string(p));

      // Group channels into layers (split on LAST '.').
      std::map<std::string, CompMap> layers;
      const Imf::ChannelList &chans = hdr.channels();
      for (Imf::ChannelList::ConstIterator it = chans.begin(); it != chans.end(); ++it) {
        std::string full = it.name();
        std::string ln, cn;
        size_t dot = full.rfind('.');
        if (dot != std::string::npos) {
          ln = full.substr(0, dot);
          cn = full.substr(dot + 1);
        } else {
          ln = "";
          cn = full;
        }
        layers[ln][cn] = full;
      }

      // Order within the part: beauty, sorted others, depth-like last.
      std::vector<std::string> beautyB, normalB, depthB;
      for (auto &kv : layers) {
        const std::string &ln = kv.first;
        if (ln.empty()) beautyB.push_back(ln);
        else if (IsDepthLikeOrdering(ln, kv.second)) depthB.push_back(ln);
        else normalB.push_back(ln);
      }
      SortCaseInsensitive(normalB);
      SortCaseInsensitive(depthB);
      std::vector<std::string> ordered;
      ordered.insert(ordered.end(), beautyB.begin(), beautyB.end());
      ordered.insert(ordered.end(), normalB.begin(), normalB.end());
      ordered.insert(ordered.end(), depthB.begin(), depthB.end());

      for (const std::string &ln : ordered) {
        const CompMap &comps = layers[ln];
        LayerDesc d;
        d.part = p;
        d.dispW = dispW;
        d.dispH = dispH;
        d.ox = ox;
        d.oy = oy;
        d.isColor = IsColorComps(comps);
        if (d.isColor) {
          d.rName = FindRole(comps, "r", "red");
          d.gName = FindRole(comps, "g", "green");
          d.bName = FindRole(comps, "b", "blue");
        } else {
          const std::string &cn = comps.begin()->first;
          d.scalarName = comps.begin()->second;
          d.depthLike = IsDepthLikeTonemap(ln, cn);
        }
        std::string base = ln.empty() ? "beauty" : ln;
        d.display = multi ? (partName + "/" + base) : base;
        df.layers.push_back(std::move(d));
      }
    }

    if (df.layers.empty()) {
      errOut = L"EXR: no displayable layers";
      return false;
    }
    df.bgrx.assign(df.layers.size(), std::vector<unsigned char>());
    return true;
  } catch (const std::exception &) {
    errOut = L"EXR: failed to parse header";
    return false;
  } catch (...) {
    errOut = L"EXR: failed to parse header";
    return false;
  }
}

// ---------------------------------------------------------------------------
// Lazily decode + tone-map one layer into df.bgrx[idx] (heavy; call OUTSIDE the
// lock). No-op if already cached. Returns false on failure.
// ---------------------------------------------------------------------------

// Mark `idx` most-recently-used and evict the least-recent decoded layers past
// the cap (each BGRX buffer is ~w*h*4 bytes). Bounds memory on big multilayer
// files. Called only from the (single) decode path.
static void TouchLru(DecodedFile &df, int idx) {
  const size_t kCap = 12;
  auto &lru = df.lru;
  for (auto it = lru.begin(); it != lru.end(); ++it) {
    if (*it == idx) { lru.erase(it); break; }
  }
  lru.push_back(idx);
  while (lru.size() > kCap) {
    int victim = lru.front();
    lru.erase(lru.begin());
    if (victim != idx && victim >= 0 && victim < (int)df.bgrx.size()) {
      df.bgrx[victim].clear();
      df.bgrx[victim].shrink_to_fit();
    }
  }
}

static bool EnsureLayerDecoded(DecodedFile &df, int idx, std::wstring &errOut) {
  if (idx < 0 || idx >= (int)df.layers.size()) {
    errOut = L"EXR: bad layer index";
    return false;
  }
  if (!df.bgrx[idx].empty()) {  // already decoded
    TouchLru(df, idx);
    return true;
  }

  const LayerDesc &L = df.layers[idx];
  try {
    MemIStream mis("exr", df.bytes.data(), df.bytes.size());
    Imf::MultiPartInputFile mpf(mis, DecodeThreads());
    Imf::InputPart part(mpf, L.part);
    const Imf::Header &hdr = part.header();
    const Imath::Box2i dw = hdr.dataWindow();
    int w = dw.max.x - dw.min.x + 1;
    int h = dw.max.y - dw.min.y + 1;
    size_t npix = (size_t)w * (size_t)h;
    int64_t originOff = (int64_t)dw.min.x + (int64_t)dw.min.y * (int64_t)w;

    std::vector<float> rbuf, gbuf, bbuf, sbuf;
    Imf::FrameBuffer fb;
    auto addSlice = [&](const std::string &chan, std::vector<float> &buf) -> const float * {
      buf.assign(npix, 0.0f);
      char *base = (char *)buf.data() - originOff * (int64_t)sizeof(float);
      fb.insert(chan, Imf::Slice(Imf::FLOAT, base, sizeof(float),
                                 sizeof(float) * (size_t)w));
      return buf.data();
    };

    const float *rc = nullptr, *gc = nullptr, *bc = nullptr, *sc = nullptr;
    if (L.isColor) {
      if (!L.rName.empty()) rc = addSlice(L.rName, rbuf);
      if (!L.gName.empty()) gc = addSlice(L.gName, gbuf);
      if (!L.bName.empty()) bc = addSlice(L.bName, bbuf);
    } else {
      sc = addSlice(L.scalarName, sbuf);
    }

    part.setFrameBuffer(fb);
    part.readPixels(dw.min.y, dw.max.y);

    std::vector<unsigned char> out;
    TonemapToBGRX(w, h, L.dispW, L.dispH, L.ox, L.oy, L.isColor, rc, gc, bc, sc,
                  L.depthLike, df.acesMode, out);
    df.bgrx[idx] = std::move(out);
    TouchLru(df, idx);
    return true;
  } catch (const std::exception &) {
    errOut = L"EXR: failed to decode layer";
    return false;
  } catch (...) {
    errOut = L"EXR: failed to decode layer";
    return false;
  }
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

  for (int y = 0; y < barH; y++)
    for (int x = 0; x < barW; x++) darkenPx(x, y);

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
// Build the returned DIB (HGLOBAL) from a layer's cached BGRX buffer.
// ---------------------------------------------------------------------------

static void *BuildDIB(const std::vector<unsigned char> &bgrx, int w, int h, int idx,
                      int count, const std::string &displayName, bool isColor, bool aces) {
  size_t pixBytes = (size_t)w * (size_t)h * 4u;
  if (bgrx.size() < pixBytes) return nullptr;
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
  memcpy(px, bgrx.data(), pixBytes);

  char capbuf[512];
  if (isColor) {
    std::snprintf(capbuf, sizeof(capbuf), "[%d/%d] %s  %s", idx + 1, count,
                  displayName.c_str(), aces ? "ACES" : "sRGB");
  } else {
    std::snprintf(capbuf, sizeof(capbuf), "[%d/%d] %s", idx + 1, count, displayName.c_str());
  }
  DrawCaption(px, w, h, capbuf);

  GlobalUnlock(hg);
  return hg;  // HGLOBAL handle; IrfanView takes ownership
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
  size_t total = sizeof(DROPFILES_LOCAL) + pathChars * sizeof(wchar_t) + sizeof(wchar_t);
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
  GlobalUnlock(hg);
  if (!PostMessageW(target, WM_DROPFILES, (WPARAM)hg, 0)) {
    GlobalFree(hg);  // only free if the post failed (else receiver owns it)
  }
}

static bool HandleSwitch(int dir) {
  HWND fg = GetForegroundWindow();
  if (!fg) return false;
  DWORD pid = 0;
  GetWindowThreadProcessId(fg, &pid);
  if (pid != GetCurrentProcessId()) return false;

  std::wstring path;
  bool act = false;
  bool doPost = false;
  {
    // Tiny locked region (RAII): advance index + read path; never waits behind
    // a decode (which runs outside the lock).
    CsGuard g(&g_cs);
    if (g_count > 1 && EndsWithExrCI(g_path)) {
      g_index = ((g_index + dir) % g_count + g_count) % g_count;
      if (g_index >= 0 && g_index < (int)g_names.size())
        g_currentLayerName = g_names[g_index];
      path = g_path;
      act = true;
      // Coalesce: only post a reload if none is already in flight. If one is
      // pending, we still advanced g_index so the pending reload lands on the
      // latest target -- no queue buildup on key auto-repeat / mashing.
      // Auto-recovery: if the previous reload was posted long ago but never
      // cleared (e.g. the WM_DROPFILES was lost because the window was
      // minimized / lost focus), post again so switching can't stall forever.
      ULONGLONG now = GetTickCount64();
      if (!g_reloadPending || (now - g_reloadStamp > 800)) {
        g_reloadPending = true;
        g_reloadStamp = now;
        doPost = true;
      }
    }
  }

  if (!act) return false;
  if (doPost) PostDropReload(fg, path);
  return true;  // swallow the key regardless (we handled it by advancing)
}

// Flip the ACES/sRGB view transform and reload the current file so color layers
// re-render in the new mode. Reuses the reload-coalescing logic; the cached
// pixels are invalidated on the main thread in ReadCore (mode-mismatch), so we
// never touch the decode-owned BGRX buffers from the hook thread.
static bool HandleAcesToggle() {
  HWND fg = GetForegroundWindow();
  if (!fg) return false;
  DWORD pid = 0;
  GetWindowThreadProcessId(fg, &pid);
  if (pid != GetCurrentProcessId()) return false;

  std::wstring path;
  bool act = false;
  bool doPost = false;
  {
    CsGuard g(&g_cs);
    if (!g_path.empty() && EndsWithExrCI(g_path)) {
      act = true;  // swallow the key regardless of coalescing
      ULONGLONG now = GetTickCount64();
      if (!g_reloadPending || (now - g_reloadStamp > 800)) {
        // Flip only when we actually post a reload, so holding Ctrl+Alt+A
        // (auto-repeat) doesn't ping-pong the mode on every key-down.
        g_acesView = !g_acesView;
        path = g_path;
        g_reloadPending = true;
        g_reloadStamp = now;
        doPost = true;
      }
    }
  }

  if (!act) return false;
  if (doPost) PostDropReload(fg, path);
  return true;  // swallow the key
}

static LRESULT CALLBACK LowLevelKbdProc(int nCode, WPARAM wParam, LPARAM lParam) {
  if (nCode == HC_ACTION && (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN)) {
    const KBDLLHOOKSTRUCT *k = (const KBDLLHOOKSTRUCT *)lParam;
    bool ctrl = (GetAsyncKeyState(VK_CONTROL) & 0x8000) != 0;
    bool alt = (GetAsyncKeyState(VK_MENU) & 0x8000) != 0;
    if (ctrl && alt) {
      try {
        if (k->vkCode == VK_RIGHT || k->vkCode == VK_LEFT) {
          int dir = (k->vkCode == VK_RIGHT) ? +1 : -1;
          if (HandleSwitch(dir)) return 1;  // swallow the key
        } else if (k->vkCode == 'A') {
          if (HandleAcesToggle()) return 1;  // swallow the key
        }
      } catch (...) {
        // Never let an exception escape the hook callback.
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
  // Size OpenEXR's global worker pool so readPixels decompresses blocks in
  // parallel (without this it runs single-threaded regardless of the count
  // passed to MultiPartInputFile).
  Imf::setGlobalThreadCount(DecodeThreads());
  BuildSrgbLut();
  GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN | GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                     (LPCWSTR)&OneTimeInit, &g_hSelf);
  g_hookThread = CreateThread(nullptr, 0, HookThreadProc, nullptr, 0, nullptr);
}

// ---------------------------------------------------------------------------
// Core read routine (shared by ReadEXR and ReadEXR_W)
// ---------------------------------------------------------------------------

static void SafeWriteWide(wchar_t *dst, const wchar_t *src, int capGuess) {
  if (!dst) return;
  int i = 0;
  for (; src[i] && i < capGuess - 1; i++) dst[i] = src[i];
  dst[i] = 0;
}

static void *ReadCore(const wchar_t *filename, void *a2, void *a3, wchar_t *statusText,
                      wchar_t *formatText, int *widthOut, int *heightOut) {
  (void)a2;
  (void)a3;
  EnsureCsInit();
  OneTimeInit();

  // A hotkey reload (if any) has now been delivered; clear the coalescing flag
  // so the next Ctrl+Alt+arrow can post again.
  {
    CsGuard g(&g_cs);
    g_reloadPending = false;
  }

  if (!filename || !filename[0]) {
    SafeWriteWide(statusText, L"EXR: empty filename", 64);
    return nullptr;
  }
  std::wstring path = filename;

  uint64_t mtime = 0;
  GetFileMtime(path, mtime);  // best-effort; 0 if unavailable

  // --- Effective color view transform: persistent g_acesView, overridable per
  //     call by env EXRLAYER_FORCE_ACES ("0"/"1") for deterministic tests. ---
  bool effAces;
  {
    CsGuard g(&g_cs);
    effAces = g_acesView;
  }
  wchar_t acesEnv[8];
  DWORD aen = GetEnvironmentVariableW(L"EXRLAYER_FORCE_ACES", acesEnv, 8);
  if (aen > 0 && aen < 8) effAces = (_wtoi(acesEnv) != 0);

  // --- Tiny lock: cache lookup; keep a shared_ptr so data stays alive. ---
  std::shared_ptr<DecodedFile> local;
  {
    CsGuard g(&g_cs);
    if (g_cache && g_cache->path == path && g_cache->mtime == mtime &&
        !g_cache->layers.empty()) {
      local = g_cache;
    }
  }

  // --- On a miss: read bytes + parse metadata OUTSIDE any lock. ---
  if (!local) {
    auto decoded = std::make_shared<DecodedFile>();
    decoded->path = path;
    decoded->mtime = mtime;
    decoded->acesMode = effAces;  // first decode uses the effective mode
    if (!ReadWholeFileW(path, decoded->bytes)) {
      SafeWriteWide(statusText, L"EXR: cannot open/read file", 96);
      return nullptr;
    }
    std::wstring errOut;
    if (!ParseMetadata(*decoded, errOut)) {
      SafeWriteWide(statusText, errOut.empty() ? L"EXR: parse failed" : errOut.c_str(), 96);
      return nullptr;
    }
    CsGuard g(&g_cs);
    if (g_cache && g_cache->path == path && g_cache->mtime == mtime &&
        !g_cache->layers.empty()) {
      local = g_cache;  // another thread beat us
    } else {
      g_cache = decoded;
      local = decoded;
    }
  }

  // --- Layer selection (sticky by name / forced / beauty). ---
  wchar_t envbuf[32];
  DWORD en = GetEnvironmentVariableW(L"EXRLAYER_FORCE_INDEX", envbuf, 32);
  bool forced = (en > 0 && en < 32);
  int forcedIdx = forced ? _wtoi(envbuf) : 0;

  int count = (int)local->layers.size();
  int idx = 0;
  {
    CsGuard g(&g_cs);
    g_path = path;
    g_count = count;
    g_names.clear();
    g_names.reserve(count);
    for (const LayerDesc &L : local->layers) g_names.push_back(L.display);

    if (forced) {
      if (forcedIdx < 0) forcedIdx = 0;
      if (forcedIdx >= count) forcedIdx = count - 1;
      g_index = forcedIdx;
      g_currentLayerName = g_names[g_index];
    } else if (!g_currentLayerName.empty()) {
      int found = -1;
      for (int i = 0; i < count; i++) {
        if (g_names[i] == g_currentLayerName) { found = i; break; }
      }
      if (found >= 0) {
        g_index = found;
      } else {
        g_index = 0;
        g_currentLayerName = g_names[0];
      }
    } else {
      g_index = 0;
      g_currentLayerName = g_names[0];
    }
    idx = g_index;
  }

  // --- Color-mode reconcile: if the effective view transform differs from the
  //     mode the cached pixels were tone-mapped with, drop them so they rebuild
  //     in the new mode (done on this thread, so it can't race the decode). ---
  if (local->acesMode != effAces) {
    for (auto &b : local->bgrx) {
      b.clear();
      b.shrink_to_fit();
    }
    local->lru.clear();
    local->acesMode = effAces;
  }

  // --- Heavy: lazily decode the selected layer OUTSIDE the lock. ---
  std::wstring derr;
  if (!EnsureLayerDecoded(*local, idx, derr)) {
    SafeWriteWide(statusText, derr.empty() ? L"EXR: decode failed" : derr.c_str(), 96);
    return nullptr;
  }

  const LayerDesc &L = local->layers[idx];
  void *dib = BuildDIB(local->bgrx[idx], L.dispW, L.dispH, idx, count, L.display,
                       L.isColor, local->acesMode);
  if (!dib) {
    SafeWriteWide(statusText, L"EXR: out of memory", 64);
    return nullptr;
  }

  if (widthOut) *widthOut = L.dispW;
  if (heightOut) *heightOut = L.dispH;
  if (formatText) {
    wchar_t fbuf[96];
    std::swprintf(fbuf, 96, L"EXR %hs [layer %d/%d]", L.display.c_str(), idx + 1, count);
    SafeWriteWide(formatText, fbuf, 96);
  }

  return dib;
}

// ---------------------------------------------------------------------------
// Exported entry points
// ---------------------------------------------------------------------------

// Marker string so the installer can recognize a previously-installed copy of
// THIS plugin (vs the genuine stock EXR.dll) and never back ours up as "stock".
// Exported + referenced so the linker keeps it in the binary.
extern "C" __declspec(dllexport) const char EXRViewerPluginTag[] =
    "IrfanView_EXR_Layer_Plugin_MARKER_kaki373";

extern "C" int GetPlugInInfo(char *version, char *formats) {
  (void)EXRViewerPluginTag;  // keep the marker referenced
  if (version) strcpy(version, "1.0");
  if (formats) strcpy(formats, "EXR Format - EXR");
  return 0;
}

extern "C" void *ReadEXR_W(const wchar_t *filename, void *a2, void *a3, wchar_t *statusText,
                           wchar_t *formatText, int *widthOut, int *heightOut) {
  try {
    return ReadCore(filename, a2, a3, statusText, formatText, widthOut, heightOut);
  } catch (...) {
    SafeWriteWide(statusText, L"EXR: internal error", 64);
    return nullptr;
  }
}

extern "C" void *ReadEXR(const char *filename, void *a2, void *a3, wchar_t *statusText,
                         wchar_t *formatText, int *widthOut, int *heightOut) {
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
