# PAL racdoor DNAS integration test

This PAL-only module embeds the raw worker from the sibling racdoor checkout at
`samples/uya-dnas-pal/uya-dnas-pal.bin`. It runs without Mod Menu or game libraries.
Build from the common parent mount so the assembler can read that artifact:

```
docker run --rm -v D:/Repos/Metroynome:/src -w /src/uya-mod-loader uya-mod-loader make -f Makefile.eeglobal REGION=pal MODULE=modules/racdoor-dnas-test image
```

Deploy the PAL loader PNACH for both campaign and multiplayer CRCs, and put
`racdoor-dnas-test all` in the isolated host filesystem's `uya/config/modules.txt`.
Copy `build/pal/publish/racdoor-dnas-test.pal.bin` to `uya/mods/`.
The sibling payload's source changes rebuild its raw image through Make.

The module retains the loader API pointer across transitions. Campaign ticks
only update telemetry. Multiplayer level 39 calls the worker, whose full stock
instruction guard prevents writes before the correct overlay is present.
Repeated calls report already patched. Overlay reloads may apply it again.

Module discovery points directly to an 11-word telemetry table:
magic 0x444e4153, size 44, initCount, campaignTicks, multiplayerTicks, attempts,
applied, lastResult, mode, context, level. Offsets are documented in main.c.
Result 0 means guard mismatch, 1 applied, 2 already patched.

This tests payload execution and loader lifetime. The loader PNACH repeatedly
installs separate campaign and multiplayer hooks; this does not prove that a
one-shot racdoor save bootstrap can install or maintain those hooks. It also
does not change DNS configuration. Stop before downloading a Horizon patch:
its low-memory placement may overlap this loader and needs separate checking.

2026-11-13: PAL build and relocation comparison passed. Live campaign startup
loaded the module at 0x000a5d40, telemetry at 0x000a6020, initCount 1, increasing
campaignTicks, and zero DNAS attempts. Multiplayer verification: loader restarted in mode 2 and reloaded the module at the same address; campaignTicks reset to 0. At capture, multiplayerTicks/attempts were 789, applied 1, lastResult 2, initCount 1, level 39, context 2. Both expected instruction changes were present. This proves payload execution after loader reinitialization, not preservation of campaign module state. The user subsequently reported passing DNAS and logging in successfully. This does not verify a standalone save bootstrap or Horizon patch memory compatibility.
