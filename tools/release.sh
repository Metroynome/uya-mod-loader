#!/bin/sh
# Release policy is an allowlist: no local settings, test modules, or map packs.
set -eu
export TZ=UTC LC_ALL=C
cd "$(dirname "$0")/.."
root=$PWD
action=${1:-package}
clean_published() {
    # Only fixed, generated paths; never delete release policy or instructions.
    rm -rf release/uya release/zips
    rm -f release/SC*-*.pnach release/horizon_unix_update_cmaps.sh release/horizon_windows_update_cmaps.ps1 release/files.json
}
check_archives() {
    (cd release/zips && sha256sum -c SHA256SUMS.txt)
    for label in ntscu pal ntscj all; do
        zip=$root/release/zips/uya-mod-loader-$label.zip
        unzip -tq "$zip"
        expected=$root/build/release/$label
        [ -d "$expected" ] || { echo 'Run make release before checking archives' >&2; exit 1; }
        unzip -Z1 "$zip" | LC_ALL=C sort > build/release/actual.txt
        (cd "$expected" && find . -type f | sed 's|^./||' | LC_ALL=C sort) > build/release/expected.txt
        cmp build/release/actual.txt build/release/expected.txt
        # Comparing each stream also rejects accidental stale archives.
        while IFS= read -r path; do
            unzip -p "$zip" "$path" > build/release/check.tmp
            cmp build/release/check.tmp "$expected/$path"
            if [ "$label" = all ]; then cmp build/release/check.tmp "release/$path"; fi
        done < build/release/expected.txt
        echo "Validated uya-mod-loader-$label.zip"
    done
    }
if [ "$action" = check ]; then check_archives; exit; fi
if [ "$action" = clean ]; then clean_published; exit; fi
[ "$action" = package ] || exit 1
mkdir -p build/release
for label in ntscu pal ntscj all; do
    case "$label" in all) regions='ntscu pal ntscj';; *) regions=$label;; esac
    bundle=$root/build/release/$label
    rm -rf "$bundle"; mkdir -p "$bundle/uya/config" "$bundle/uya/mods/utils"
    cp release/README.md "$bundle/"
    cp tools/horizon/horizon_unix_update_cmaps.sh tools/horizon/horizon_windows_update_cmaps.ps1 "$bundle/"
    printf '0\n' > "$bundle/uya/maploader-online.txt"
    printf '# Release defaults: no test/example modules.\n# Restart the game after editing. See README.md.\n' > "$bundle/uya/config/modules.txt"
    if [ "$label" != ntscj ]; then cp hostfs/uya/mods/utils/mapscan.irx "$bundle/uya/mods/utils/"; fi
    for region in $regions; do
        case "$region" in ntscu) id=1;; pal) id=2;; ntscj) id=3;; esac
        while read -r r serial crc mode supported; do
            [ "$r" = "$region" ] && [ "$supported" = yes ] || continue
            { printf '[UYA Mod Loader]\nauthor=Metroynome\n'; cat "build/dist/mod-loader.$region.pnach"; } > "$bundle/${serial}_${crc}.pnach"
        done < release/crcs.txt
        while read -r name contexts; do
            case "$name" in ''|'#'*) continue;; hello|menu-example|campaign-test|multiplayer-test) echo 'Test modules cannot be release defaults' >&2; exit 1;; esac
            case "$contexts" in campaign|local|campaign,local) ;; *) echo 'Release defaults must stay offline' >&2; exit 1;; esac
            row=$(awk -F'|' -v n="$name" '$2==n' build/catalog.txt)
            [ -n "$row" ] || { echo "Unknown release module: $name" >&2; exit 1; }
            supported=$(printf '%s\n' "$row" | cut -d'|' -f5)
            case " $supported " in *" $region "*) ;; *) continue;; esac
            cp "hostfs/uya/mods/$name.$region.bin" "$bundle/uya/mods/"
            [ "$region" != ntscj ] || contexts=campaign
            printf '%s.%s %s\n' "$name" "$region" "$contexts" >> "$bundle/uya/config/modules.txt"
        done < release/modules.txt
    done
    if [ -f hostfs/uya/mods/debug/exceptionhandler.bin ]; then cp hostfs/uya/mods/debug/exceptionhandler.bin "$bundle/uya/mods/utils/"; fi
    # Stable file order, timestamps and permissions produce reproducible ZIPs.
    find "$bundle" -type f -exec chmod 644 {} \;
    chmod 755 "$bundle/horizon_unix_update_cmaps.sh"
    find "$bundle" -exec touch -t 198001010000.00 {} \;
    rm -f "build/release/uya-mod-loader-$label.zip"
    (cd "$bundle" && find . -type f | sed 's|^./||' | LC_ALL=C sort | TZ=UTC zip -X -q "$root/build/release/uya-mod-loader-$label.zip" -@)
done
clean_published
mkdir -p release/zips
cp -R build/release/all/. release/
cp build/release/*.zip release/zips/
(cd release/zips && sha256sum *.zip > SHA256SUMS.txt)
check_archives
