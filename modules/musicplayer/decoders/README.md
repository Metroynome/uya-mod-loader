# Audio decoders

`decoder.h` defines the PCM-frame interface. To add a format, add its decoder
here, register its header detection in `decoder.c`, add its extension to
`supported()` in `../custom.c`, and add its object to `../Makefile.defs`.

- WAV: PCM integer 8/16/24/32-bit, mono/stereo.
- MP3: minimp3, scalar C decoder (no SIMD).
- PS2: mono VAGp ADPCM files.

`minimp3.h` is unmodified third-party code from lieff/minimp3, revision
`ea99364f61c14656440e8d77e9c233ccf3124633`, distributed under its embedded CC0 license. Project formatting
rules apply to our wrappers, not that vendored file.
