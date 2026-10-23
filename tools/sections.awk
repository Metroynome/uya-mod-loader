# Old EE ld cannot combine garbage collection with retained relocations.
# Reuse the normal link's live input sections, then discard everything else.
# Usage: awk -f tools/sections.awk module.map module.ld > live.ld
NR == FNR {
    if ($0 ~ /^\.(text|rodata|data|bss)[ \t]/) { group=$1; next }
    if ($0 ~ /^[^ \t]/) { group=""; next }
    if (!group) next
    if ($0 ~ /^ (\.[^ \t]+|COMMON)[ \t]*$/) { section=$1; next }
    if ($0 ~ /^ (\.[^ \t]+|COMMON)[ \t]+0x/) {
        section=$1; file=$4; size=$3
    } else if (section && $1 ~ /^0x/ && NF==3) {
        file=$3; size=$2
    } else next
    if (size !~ /^0x0+$/ && file ~ /\.o\)?$/) {
        # A leading wildcard matches archive members as well as object paths.
        sub(/^.*\(/,"",file); sub(/\)$/, "",file); sub(/^.*\//,"",file)
        live[group]=live[group] " *" file "(" section ")"
    }
    section=""
    next
}
/^[ \t]*\.text / { print "    .text : {" live[".text"] " }"; next }
/^[ \t]*\.rodata / { print "    .rodata ALIGN(16) : {" live[".rodata"] " }"; next }
/^[ \t]*\.data / { print "    .data ALIGN(16) : {" live[".data"] " . = ALIGN(16); }"; next }
/^[ \t]*\.bss / { print "    .bss ALIGN(16) (NOLOAD) : {" live[".bss"] " }"; next }
/\/DISCARD\// { print "    /DISCARD/ : { *(.text .text.* .rodata .rodata.* .data .data.* .sdata .sdata.* .bss .bss.* .sbss .sbss.* COMMON) }" }
{ print }
