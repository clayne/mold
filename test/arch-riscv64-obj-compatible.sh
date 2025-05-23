#!/bin/bash
. $(dirname $0)/common.inc

set -x

cat <<EOF | $CC -o $t/a.o -c -xc -
void foo() {}
EOF

# Set `e_flags` to zero to simulate ABI incompatibility
dd if=/dev/zero of=$t/a.o bs=4 oflag=seek_bytes seek=48 count=1 conv=notrunc

cat <<EOF | $CC -o $t/b.o -c -xc -
void foo();
int main() {
  foo();
  return 0;
}
EOF

not $CC -B. -o $t/exe $t/a.o $t/b.o |&
  grep 'cannot link object files with different floating-point ABI'
