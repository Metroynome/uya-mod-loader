#!/usr/bin/env python3
"""Build regional loader patches and fixed-address modules using the EE toolchain."""
import argparse
import json
import os
from pathlib import Path
import re
import shutil
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
REGIONS = {"ntscu": 1, "pal": 2, "ntscj": 3}
LOW, HIGH = 0xf2f80, 0xff000
MODES = {"campaign": 1, "multiplayer": 2, "both": 3}
NAME = re.compile(r"[a-z][a-z0-9_-]{0,39}\Z")


def run(args, **kw):
    subprocess.run([str(a) for a in args], check=True, **kw)


MANIFEST_PATHS = ("config/modules.txt", "modules.txt")

def module_list_path():
    root = ROOT / "hostfs/uya"
    return next((root / name for name in MANIFEST_PATHS if (root / name).is_file()), root / "config/modules.txt")


def variants(name, config):
    library = config.get("library", "none")
    if library == "both":
        return [(name + "-campaign", "librac3", "campaign", config.get("campaign_sources", ["campaign.c"])),
                (name + "-multiplayer", "libuya", "multiplayer", config.get("multiplayer_sources", ["multiplayer.c"]))]
    if library not in ("none", "librac3", "libuya"):
        raise ValueError(f"{name}: invalid library {library}")
    mode = config.get("mode", {"librac3": "campaign", "libuya": "multiplayer"}.get(library, "both"))
    if mode not in MODES or (library == "librac3" and mode != "campaign") or (library == "libuya" and mode != "multiplayer"):
        raise ValueError(f"{name}: library and mode do not match")
    sources = config.get("sources", ["main.c"])
    if not sources:
        raise ValueError(f"{name}: no sources")
    return [(name, library, mode, sources)]


def configs():
    result = []
    for folder in sorted((ROOT / "modules").iterdir()):
        if not folder.is_dir() or not (folder / "module.json").is_file():
            continue
        if not NAME.fullmatch(folder.name):
            raise ValueError(f"Invalid module folder: {folder.name}")
        cfg = json.loads((folder / "module.json").read_text())
        regions = cfg.get("regions", list(REGIONS))
        if not regions or set(regions) - REGIONS.keys():
            raise ValueError(f"{folder.name}: invalid regions")
        for variant in variants(folder.name, cfg):
            result.append((folder, regions, variant))
    names = [v[2][0] for v in result]
    if len(names) != len(set(names)):
        raise ValueError("Module output names collide")
    return result


def new_module(name, library):
    if not NAME.fullmatch(name):
        raise ValueError("Use a lowercase name starting with a letter, up to 40 letters/digits/_/-")
    folder = ROOT / "modules" / name
    if folder.exists():
        raise ValueError(f"Module already exists: {folder}")
    cfg = {"library": library, "regions": ["ntscu", "pal"] if library in ("libuya", "both") else list(REGIONS)}
    items = variants(name, cfg)
    folder.mkdir(parents=True)
    (folder / "module.json").write_text(json.dumps(cfg, indent=2) + "\n")
    (folder / "Makefile").write_text("include ../../tools/module.mk\n")
    for output, lib, mode, sources in items:
        include = f"/* Include <{lib}/...h> for this mode's game functions. */\n" if lib != "none" else ""
        if lib != "none":
            include += f"#include <{lib}/stdio.h>\n"
        if lib == "libuya":
            include += '#include <libuya/uya.h>\n'
        source = '#include "module.h"\n' + include + f'''
/* This pointer is updated by the loader on mode/context transitions.
 * No Mod Menu dependency. API 3 exposes loader->context and optional
 * setLifecycle(activate, deactivate) / restrictContexts(mask). */
static const ModuleApi *loader;
int moduleInit(const ModuleApi *api)
{{
    loader = api;
    loader->print("{output}: initialized (region %u, mode %u)\\n", api->region, api->mode);
    return 0;
}}
void moduleUpdate(void)
{{
    /* Called only in this module's declared mode. Check that the level/player
     * is ready before using game-library addresses. Keep persistent state here
     * in module BSS, never in hard-coded game memory. */
}}
'''
        if lib != "none":
            source = source.replace("loader->print(", "printf(")
        if lib == "libuya":
            source = source.replace('void moduleUpdate(void)\n{', 'void moduleUpdate(void)\n{\n    uyaPreUpdate();')
            source = source.replace('in module BSS, never in hard-coded game memory. */',
                                    'in module BSS, never in hard-coded game memory. */\n    uyaPostUpdate();')
        (folder / sources[0]).write_text(source)
    module_list_path().parent.mkdir(parents=True, exist_ok=True)
    with module_list_path().open("a") as f:
        for output, lib, mode, sources in items:
            f.write(f"{output} disabled // choose campaign, local, online, or a supported combination ({mode})\n")
    print(f"Created {folder}; set its mode in the module list, then rebuild.")


class Builder:
    def __init__(self, region):
        self.region = region
        self.work = ROOT / "build" / region
        self.work.mkdir(parents=True, exist_ok=True)
        self.prefix = os.environ.get("EE_PREFIX", "ee-")
        sdk = os.environ.get("PS2SDK")
        if not sdk:
            raise ValueError("Set PS2SDK and put the EE compiler on PATH (see README.md)")
        self.sdk = Path(sdk)
        self.flags = ["-std=gnu99", "-Os", "-G0", "-Wall", "-W", "-ffreestanding", "-fno-builtin",
                      "-fno-pic", "-mno-abicalls", "-ffunction-sections", "-fdata-sections", "-D_EE",
                      f"-DUYA_MODULE_REGION={REGIONS[region]}", f'-DUYA_REGION_NAME="{region}"',
                      f"-D{region.upper()}", f"-DUYA_{region.upper()}", f"-DRAC3_{region.upper()}",
                      "-I" + str(ROOT), "-I" + str(self.sdk / "ee/include"),
                      "-I" + str(self.sdk / "common/include"), "-I" + str(self.work / "include")]
        if region == "ntscu":
            self.flags += ["-DNTSC", "-DUYA_NTSC", "-DRAC3_NTSC"]
        self.libraries = {}

    def compile(self, source, target, extra=()):
        target.parent.mkdir(parents=True, exist_ok=True)
        run([self.prefix + "gcc", *self.flags, *extra, "-c", source, "-o", target])
        return target

    def symbols(self, elf):
        output = subprocess.check_output([self.prefix + "nm", str(elf)], text=True)
        return {s[2]: int(s[0], 16) for line in output.splitlines() if len(s := line.split()) == 3 and re.fullmatch(r"[0-9a-fA-F]+", s[0])}

    def link(self, objects, script, elf, extra=()):
        run([self.prefix + "gcc", "-nostdlib", "-nostartfiles", "-G0", "-Wl,--gc-sections", *extra, "-Wl,-T," + str(ROOT / script),
             "-Wl,-Map," + str(elf.with_suffix(".map")), "-o", elf, *objects, "-lgcc"])
        symbols = self.symbols(elf)
        padding = ["--pad-to=" + hex(symbols["_image_end"])] if "_image_end" in symbols else []
        run([self.prefix + "objcopy", "-O", "binary", *padding, elf, elf.with_suffix(".bin")])
        return elf.with_suffix(".bin")

    def library(self, name):
        if name == "none":
            return []
        if name in self.libraries:
            return [self.libraries[name]]
        default = ROOT.parent / "librac3" if name == "librac3" else ROOT.parent.parent / "Horizon/horizon-uya-patch/libuya"
        container_source = Path("/opt/uya-mod-loader-deps") / ("librac3" if name == "librac3" else "horizon-uya-patch/libuya")
        if container_source.is_dir():
            default = container_source
        source = Path(os.environ.get(name.upper() + "_DIR", default)).resolve()
        regional = "ntsc" if name == "libuya" and self.region == "ntscu" else self.region
        if not (source / ("Makefile." + regional)).is_file():
            raise ValueError(f"{source}: no {self.region} library support. Supply {name.upper()}_DIR or restrict module.json regions; addresses are never guessed.")
        headers = self.work / "include" / name
        shutil.copytree(source / "include", headers, dirs_exist_ok=True)
        copied = self.work / "libraries" / name
        shutil.copytree(source, copied, dirs_exist_ok=True, ignore=shutil.ignore_patterns(".git", "obj", "lib", "test"))
        run(["make", "-f", "Makefile." + regional, "clean"], cwd=copied)
        run(["make", "-f", "Makefile." + regional, "EE_PREFIX=" + self.prefix], cwd=copied)
        archive = copied / "lib" / (name + regional + ".a")
        if not archive.is_file():
            raise ValueError(f"Library did not produce {archive}")
        self.libraries[name] = archive
        return [archive]

    def loader(self):
        objects = [self.compile(ROOT / f, self.work / (f + ".o")) for f in ("entry.S", "main.c", "platform.c", "strings.c", "config.c", "campaign-write.S")]
        elf = self.work / "mod-loader.elf"
        binary = self.link(objects, "loader.ld", elf)
        symbols = self.symbols(elf)
        if symbols["_payload_end"] > LOW or symbols["_payload_end"] != symbols["_bss_start"]:
            raise ValueError("Loader overlaps modules or contains persistent BSS")
        data = binary.read_bytes()
        if len(data) > LOW - 0xf0000:
            raise ValueError("Loader overlaps module arena")
        data += bytes((-len(data)) % 4)
        lines = ["// UYA Mod Loader: campaign + multiplayer; no heap edits."]
        for offset in range(0, len(data), 4):
            word, = struct.unpack_from("<I", data, offset)
            lines.append(f"patch=1,EE,{0xf0000+offset:08X},word,{word:08X}")
        # Campaign template and Horizon multiplayer return hooks. Both leave
        # the original addiu sp, sp, 32 delay slot intact. Guard the surrounding
        # epilogue so a patch cannot overwrite the other resident SDK layout.
        hooks = [(0x12c994, "campaignEntry")]
        if self.region != "ntscj":  # No verified NTSC-J multiplayer layout.
            hooks.append((0x139e94, "multiplayerEntry"))
        for site, entry in hooks:
            jump = 0x08000000 | (symbols[entry] >> 2)
            # Check ld s0, 0(sp) and addiu sp, sp, 32 around the replaced jr ra.
            # Surrounding instructions remain valid after installing our jump.
            for count, offset, value in [(4, -4, 0), (3, -2, 0xdfb0),
                                          (2, 4, 0x20), (1, 6, 0x27bd)]:
                lines.append(f"patch=1,EE,E0{count:02X}{value:04X},extended,{site+offset:08X}")
            lines.append(f"patch=1,EE,2{site:07X},extended,{jump:08X}")
        release = ROOT / "release"
        release.mkdir(exist_ok=True)
        (release / f"mod-loader.{self.region}.pnach").write_text("\n".join(lines) + "\n")

    def modules(self, entries, selected=None):
        layout_path = self.work / "layout.json"
        layout = None
        if selected and layout_path.is_file():
            layout = json.loads(layout_path.read_text())
            expected = [(v[0], v[1], v[2]) for _, regions, v in entries if self.region in regions]
            if [(x["name"], x["library"], x["mode"]) for x in layout] != expected:
                raise ValueError("Module configuration changed; run make from the repository root first")
        elif selected:
            print(f"{self.region}: no layout yet; building all modules to establish addresses")
            selected = None
        address = LOW
        report = []
        output = ROOT / "hostfs/uya/mods"
        output.mkdir(parents=True, exist_ok=True)
        for folder, regions, (name, library, mode, sources) in entries:
            if self.region not in regions:
                continue
            if selected and name not in selected:
                continue
            reservation = next((x for x in layout if x["name"] == name), None) if layout else None
            if reservation:
                address = int(reservation["address"], 16)
            libs = self.library(library)
            objects = []
            for i, source in enumerate(sources):
                source = (folder / source).resolve()
                if folder.resolve() not in source.parents or source.suffix not in (".c", ".S"):
                    raise ValueError(f"Invalid source path: {source}")
                objects.append(self.compile(source, self.work / "modules" / name / f"{i}.o"))
            elf = self.work / "modules" / name / (name + ".elf")
            binary = self.link(objects + libs, "module.ld", elf, [f"-Wl,--defsym,MODULE_ADDRESS={address},--defsym,MODULE_REGION={REGIONS[self.region]},--defsym,MODULE_MODES={MODES[mode]}"])
            header = struct.unpack("<10I", binary.read_bytes()[:40])
            end = address + header[5]
            if header[3] != address or end > HIGH or len(binary.read_bytes()) != 40 + header[4]:
                raise ValueError(f"{name}: invalid module image/layout")
            if reservation and header[5] > reservation["bytes"]:
                raise ValueError(f"{name} grew beyond its reserved space; run make from the repository root and deploy all modules")
            if selected:
                # Validate every selected variant before publishing any binary.
                report.append({"name": name, "binary": binary})
                continue
            shutil.copyfile(binary, output / (name + "." + self.region + ".bin"))
            report.append({"name": name, "library": library, "mode": mode, "address": hex(address), "bytes": header[5]})
            address = (end + 63) & ~63
        if selected:
            dest = ROOT / "release/hostfs/uya/mods"
            dest.mkdir(parents=True, exist_ok=True)
            for item in report:
                filename = item["name"] + "." + self.region + ".bin"
                shutil.copyfile(item["binary"], output / filename)
                shutil.copyfile(item["binary"], dest / filename)
            print(f"{self.region}: rebuilt {len(report)} selected modules at their reserved addresses")
            return
        built_names = {item["name"] + "." + self.region + ".bin" for item in report}
        for old in output.glob("*." + self.region + ".bin"):
            if old.name not in built_names:
                old.unlink()
        (self.work / "layout.json").write_text(json.dumps(report, indent=2) + "\n")
        print(f"{self.region}: {len(report)} modules, {address-LOW}/{HIGH-LOW} arena bytes")
        dest = ROOT / "release/hostfs/uya/mods"
        dest.mkdir(parents=True, exist_ok=True)
        for old in dest.glob("*." + self.region + ".bin"):
            old.unlink()
        for item in report:
            shutil.copyfile(output / (item["name"] + "." + self.region + ".bin"), dest / (item["name"] + "." + self.region + ".bin"))



def package_debug():
    """Include the optional payload only while it exists in the source hostfs."""
    source = ROOT / "hostfs/uya/mods/debug/exceptionhandler.bin"
    destination = ROOT / "release/hostfs/uya/mods/debug/exceptionhandler.bin"
    destination.parent.mkdir(parents=True, exist_ok=True)
    if source.is_file():
        shutil.copyfile(source, destination)
    elif destination.exists():
        destination.unlink()  # Do not leave a previously enabled handler behind.

def package_maps():
    """Package local map assets without adding them to version control."""
    source = ROOT / "hostfs/uya"
    destination = ROOT / "release/hostfs/uya"
    if any(source.glob("*.version")):
        run([sys.executable, ROOT / "tools/index_maps.py", source])
    for pattern in ("*.version", "*.wad", "*.world", "*.sound", "*.map",
                    "*.bg", "*.thumb", "*.code", "maps.*.idx"):
        for file in source.glob(pattern):
            shutil.copyfile(file, destination / file.name)


def clean(module=None):
    if module:
        names = [v[0] for folder, _, v in configs() if folder.name == module]
        if not names:
            raise ValueError(f"Unknown module folder: {module}")
        for region in REGIONS:
            for name in names:
                shutil.rmtree(ROOT / "build" / region / "modules" / name, ignore_errors=True)
                for base in (ROOT / "hostfs/uya/mods", ROOT / "release/hostfs/uya/mods"):
                    path = base / (name + "." + region + ".bin")
                    if path.exists():
                        path.unlink()
        # Keep layout reservations so a single-module rebuild cannot collide.
    else:
        for name in ("build", "release"):
            shutil.rmtree(ROOT / name, ignore_errors=True)
        for region in REGIONS:
            for path in (ROOT / "hostfs/uya/mods").glob("*." + region + ".bin"):
                path.unlink()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)
    new = sub.add_parser("new", help="create a disabled module with its own folder")
    new.add_argument("name")
    new.add_argument("--library", choices=["none", "librac3", "libuya", "both"], default="none")
    build = sub.add_parser("build", help="build loader and all declared modules")
    build.add_argument("--region", choices=["all", *REGIONS], default="all")
    build.add_argument("--module", help="build one source module folder using the existing layout")
    clean_parser = sub.add_parser("clean", help="remove generated output, preserving sources and debug payload")
    clean_parser.add_argument("--module")
    sub.add_parser("check", help="validate module manifests without a compiler")
    args = parser.parse_args()
    if args.command == "new":
        new_module(args.name, args.library)
    elif args.command == "clean":
        clean(args.module)
    elif args.command == "check":
        print(f"Validated {len(configs())} module images")
    else:
        entries = configs()
        selected = None
        if args.module:
            matching = [e for e in entries if e[0].name == args.module]
            if not matching:
                raise ValueError(f"Unknown module folder: {args.module}")
            selected = {e[2][0] for e in matching}
        for region in REGIONS if args.region == "all" else [args.region]:
            if selected and not any(region in e[1] for e in matching):
                if args.region != "all":
                    raise ValueError(f"{args.module} does not support {region}")
                continue
            builder = Builder(region)
            if not selected:
                builder.loader()
            builder.modules(entries, selected)
        dest = ROOT / "release/hostfs/uya/modules.txt"
        dest.parent.mkdir(parents=True, exist_ok=True)
        # Discard obsolete generated singular-name lists.
        for name in ("module.txt", "config/module.txt"):
            old = dest.parent / name
            if old.is_file(): old.unlink()
        for name in MANIFEST_PATHS:
            source = ROOT / "hostfs/uya" / name
            target = dest.parent / name
            if source.is_file():
                target.parent.mkdir(parents=True, exist_ok=True)
                shutil.copyfile(source, target)
            elif target.is_file():
                target.unlink() # Remove stale generated lists that would shadow the fallback.
        shutil.copyfile(ROOT / "hostfs/uya/maploader-online.txt", dest.parent / "maploader-online.txt")
        (dest.parent / "config").mkdir(parents=True, exist_ok=True)
        package_debug()
        package_maps()


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        sys.exit(str(exc))
