#!/bin/bash
. $(dirname $0)/common.inc

nm mold | grep '__tsan_init' && skip

cat <<EOF | $CC -o $t/a.o -c -xc -
void foo() {}
EOF

cat <<EOF | $CC -o $t/b.o -c -xc -
int main() {}
EOF

not $CC -B. -o $t/exe1 $t/a.o $t/a.o $t/b.o |&
  grep 'duplicate symbol.*: foo$'

not $CC -B. -o $t/exe2 $t/a.o $t/a.o $t/b.o -Wl,-gc-sections |&
  grep 'duplicate symbol.*: foo$'
