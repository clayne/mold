#!/bin/bash
. $(dirname $0)/common.inc

cat <<EOF | $CC -o $t/a.o -c -x assembler -
.globl main
main:
EOF

$CC -B. -o $t/exe $t/a.o

$CC -B. -o $t/exe $t/a.o -Wl,-z,cet-report=warning >& $t/log
grep 'a.o: -cet-report=warning: missing GNU_PROPERTY_X86_FEATURE_1_IBT' $t/log
grep 'a.o: -cet-report=warning: missing GNU_PROPERTY_X86_FEATURE_1_SHSTK' $t/log

not $CC -B. -o $t/exe $t/a.o -Wl,-z,cet-report=error >& $t/log
grep 'a.o: -cet-report=error: missing GNU_PROPERTY_X86_FEATURE_1_IBT' $t/log
grep 'a.o: -cet-report=error: missing GNU_PROPERTY_X86_FEATURE_1_SHSTK' $t/log
