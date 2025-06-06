#!/bin/bash
. $(dirname $0)/common.inc

[ "$(uname)" = FreeBSD ] && skip

cat <<EOF | $CC -o $t/b.o -c -xc - -fno-PIE
#include <stdio.h>
__attribute__((weak)) extern int foo;
int main() { printf("%p\n", &foo); }
EOF

not $CC -B. -o $t/exe3 $t/b.o -no-pie -Wl,-z,dynamic-undefined-weak |&
  grep 'cannot create a copy relocation for foo'
