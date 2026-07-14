# Third-party notices

`EXR.dll` statically links the following libraries. They are not vendored in
this repository (see [../BUILD.md](../BUILD.md) for how to obtain/build them);
each remains under its own license.

## OpenEXR
- Source: https://github.com/AcademySoftwareFoundation/openexr
- License: BSD 3-Clause
- Provides all EXR decoding (scanline/tiled, ZIP/PIZ/RLE/PXR24/B44/DWAA/DWAB,
  single- and multi-part).

## Imath
- Source: https://github.com/AcademySoftwareFoundation/Imath
- License: BSD 3-Clause
- Math types used by OpenEXR (half, boxes, etc.).

## libdeflate
- Source: https://github.com/ebiggers/libdeflate
- License: MIT
- Vendored inside OpenEXR 3.x for ZIP (de)compression.

The 8x8 bitmap font used for the on-image caption is `font8x8_basic` (public
domain, by Daniel Hepper), embedded directly in `src/exrplugin.cpp`.

This project's own code is under the MIT License (see ../LICENSE).
