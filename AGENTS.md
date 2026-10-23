# UYA Mod Loader agent rules

These rules apply throughout this repository. Follow the user's current instructions when they override a convention here.

## Formatting

- Use literal tabs for indentation, displayed as four spaces (tab width = 4). Spaces may be used for column alignment. Preserve literal tabs in Makefile recipes.
- Follow `.clang-format`: function opening braces on the next line, control-flow opening braces on the same line, and no compressed one-line functions or control flow.
- Keep code readable with descriptive names, blank lines between functions and logical sections, and comments explaining hooks, addresses, and non-obvious behavior.
- C file order: includes, defines, types/structs, function prototypes, global variables, functions.
- Header order: includes, defines, types/structs, global variables, function prototypes/external functions. Keep include guards around the contents.
- Use `u8`, `u16`, and `u32` for unsigned 8/16/32-bit values. Include `types.h` where needed; it uses PS2SDK types on target and fixed-width host types for tools/tests.
- Declare project structs as `typedef struct Name { ... } Name_t;`: plain struct tags, `_t` typedef names. Keep external SDK/library types unchanged.
- Preserve type dependencies and conditional compilation when reordering. Omit empty sections; do not add placeholder declarations or section boilerplate.

## Keep the project simple

- Prefer plain C and the existing Makefiles. Do not add Python code or dependencies to the project.
- Add no extra build tools, generators, wrappers, or scripts unless absolutely necessary. First check whether C, Make, or an existing tool can do the work. Explain the concrete need if an extra tool is unavoidable.
- Use `Makefile.defs` for module build settings and the shared `Makefile.eeglobal` rules. Avoid parallel build configuration or duplicated build logic.
- Keep the existing loader PNACH flow using `template/` and `bin2code`. Modules build as loader binaries, not PNACH files.
- Keep files few and purposeful. Do not introduce layers, helper frameworks, or new validation systems without a concrete need in the requested change.
- Keep logs and temporary investigation output outside the source tree. Use the existing ignored build/test locations; do not add disposable tests, logs, or generated outputs to source control. Retain useful existing regression checks.

## Loader and module conventions

- Modules must be usable without Mod Menu. The loader owns shared lifecycle, service discovery, and configuration persistence; Mod Menu provides optional UI.
- Use the existing loader API and service discovery rather than inventing shared fixed addresses. Read `module.h`, `ADDRESSES.md`, and the relevant module documentation before changing memory layout or public interfaces.
- Preserve API compatibility where practical. Update affected producers, consumers, and documentation together when a change requires a new contract.
- Preserve the declared regional and campaign/local/online support. Never reuse an address across regions or overlays without evidence. Do not present untested regions as verified.
- Match Horizon's menu elements and styling for tabs, buttons, lists, and controller glyphs. Consult `horizon-uya-patch/patch/config.c` and the existing Mod Menu implementation rather than inventing a new visual style.
- Keep game input blocked while Mod Menu is open and restore it on close/deactivation. Preserve cleanup when modules become inactive online or during context changes.
- Use "Start Game" in UI, APIs, and documentation for this action.

## Verification

- Use the existing Docker/Make workflow documented in `README.md`; do not replace it with a new build system.
- Build the affected supported regions. Run relevant existing tests when available; ignored local tests may not exist in a fresh checkout.
- For relocation or linking changes, run the existing relocation checks when available. For hooks, input, loading, or menu behavior, distinguish build/test results from actual PCSX2 verification.
- Formatting-only work should preserve behavior. Review the diff and avoid unrelated fixes.
- Do not commit, push, or publish a release unless requested. Do not rebuild release packages merely for documentation changes.
