#!/bin/sh
set -eu
cd "$(dirname "$0")/.."
name=${1:-}; library=${2:-none}
case "$name" in ''|[!a-z]*|*[!a-z0-9_-]*) echo 'Use a lowercase module name starting with a letter' >&2; exit 1;; esac
[ "${#name}" -le 40 ] || exit 1
case "$library" in none|librac3) regions='ntscu pal ntscj';; libuya|both) regions='ntscu pal';; *) echo 'Unknown library' >&2; exit 1;; esac
folder=modules/$name
[ ! -e "$folder" ] || { echo 'Module already exists' >&2; exit 1; }
mkdir "$folder"
printf 'LIBRARY = %s\nREGIONS = %s\n' "$library" "$regions" > "$folder/Makefile.defs"
printf 'include ../../Makefile.eeglobal\n' > "$folder/Makefile"
list=hostfs/uya/config/modules.txt
[ -f "$list" ] || { [ ! -f hostfs/uya/modules.txt ] || list=hostfs/uya/modules.txt; }
mkdir -p "$(dirname "$list")"
if [ "$library" = both ]; then
    printf 'CAMPAIGN_OBJS = campaign.o\nMULTIPLAYER_OBJS = multiplayer.o\n' >> "$folder/Makefile.defs"
else
    printf 'EE_OBJS = main.o\n' >> "$folder/Makefile.defs"
fi
if [ "$library" = both ]; then variants='campaign multiplayer'; else variants=main; fi
for variant in $variants; do
    lib=$library; output=$name
    if [ "$variant" = campaign ]; then lib=librac3; output=$name-campaign; fi
    if [ "$variant" = multiplayer ]; then lib=libuya; output=$name-multiplayer; fi
    {
        printf '#include "module.h"\n'
        if [ "$lib" != none ]; then printf '#include <%s/stdio.h>\n' "$lib"; fi
        if [ "$lib" = libuya ]; then printf '#include <libuya/uya.h>\n'; fi
        cat <<'SOURCE'

/* No Mod Menu dependency. Use loader->context for context-specific behavior,
 * and the loader configuration API for optional settings (see CONFIG.md). */
static const ModuleApi* loader;

int moduleInit(const ModuleApi* api)
{
	loader = api;
SOURCE
        if [ "$lib" = none ]; then printer='loader->print'; else printer=printf; fi
        printf '\t%s("%s: initialized (region %%u, mode %%u)\\n", api->region, api->mode);\n' "$printer" "$output"
        printf '\treturn 0;\n}\n\nvoid moduleUpdate(void)\n{\n'
        if [ "$lib" = libuya ]; then printf '\tuyaPreUpdate();\n'; fi
        printf '\t/* Check level/player readiness before calling game functions. */\n'
        if [ "$lib" = libuya ]; then printf '\tuyaPostUpdate();\n'; fi
        printf '}\n'
    } > "$folder/$variant.c"
    printf '%s disabled // choose campaign, local, online, or a supported combination\n' "$output" >> "$list"
done
echo "Created $folder; enable it in $list, then rebuild all modules."
