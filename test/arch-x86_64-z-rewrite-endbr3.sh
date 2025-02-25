#!/bin/bash
. $(dirname $0)/common.inc

test_cflags -fcf-protection || skip
[ "$QEMU" == '' ] || skip

# Check if Intel SDE CPU emulator is available
command -v sde64 >& /dev/null || skip

cat <<EOF | $CC -o $t/a.o -c -xc - -O -fcf-protection
#include <stdio.h>
int main() {
  printf("Hello world\n");
}
EOF

$CC -B. -o $t/exe $t/a.o -Wl,-z,rewrite-endbr
sde64 -cet 1 -- $t/exe | grep 'Hello world'
