# Third-party notices

This project vendors the following single-header libraries. They are compiled
into `EXR.dll`; each remains under its own license.

## tinyexr (`tinyexr.h`, `exr_reader.hh`, `streamreader.hh`)
- Source: https://github.com/syoyo/tinyexr
- License: BSD 3-Clause
- Used for parsing and decoding OpenEXR files from memory.

## stb (`stb_image.h`, `stb_image_write.h`)
- Source: https://github.com/nothings/stb
- License: Public Domain (or MIT, at your option)
- `stb_image.h` provides the zlib inflate used by tinyexr's decode path
  (`TINYEXR_USE_STB_ZLIB`). `stb_image_write.h` is included only to satisfy a
  linker reference from tinyexr's (unused) save path.

Refer to each upstream repository for the full license text. This project's own
code is under the MIT License (see ../LICENSE).
