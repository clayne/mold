#!/bin/bash
. $(dirname $0)/common.inc

cat <<EOF | $CC -o $t/a.o -c -xc - -g -gdwarf32 -Wl,--compress-debug-sections=zlib || skip
void foo() {}
EOF

cat <<EOF | $CC -o $t/b.o -c -xc - -g -gdwarf64 -Wl,--compress-debug-sections=zstd || skip
int main() {}
EOF

# Test if DWARF32 precedes DWARF64 in the output .debug_info
MOLD_DEBUG=1 $CC -B. -o $t/exe1 $t/a.o $t/b.o -g -Wl,-Map=$t/map1
grep -A10 -F '/a.o:(.debug_info)' $t/map1 | grep -F '/b.o:(.debug_info)'

MOLD_DEBUG=1 $CC -B. -o $t/exe2 $t/b.o $t/a.o -g -Wl,-Map=$t/map2
grep -A10 -F '/a.o:(.debug_info)' $t/map2 | grep -F '/b.o:(.debug_info)'
