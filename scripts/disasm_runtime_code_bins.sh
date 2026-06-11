#!/usr/bin/env bash
set -euo pipefail

ROOT="/mnt/c/users/vladimir/desktop/TLLCoopResearch"
REPORTS="$ROOT/reports"

dump_one() {
    local bin_name="$1"
    local vma="$2"
    local out_name="$3"
    objdump \
        -D \
        -b binary \
        -m i386:x86-64 \
        --adjust-vma="$vma" \
        "$REPORTS/$bin_name" > "$REPORTS/$out_name"
}

dump_one "code_140BBFD70.bin" 0x140BBFD70 "code_140BBFD70.asm.txt"
dump_one "code_140B44EC0.bin" 0x140B44EC0 "code_140B44EC0.asm.txt"
dump_one "code_141685C30.bin" 0x141685C30 "code_141685C30.asm.txt"
dump_one "code_140B42310.bin" 0x140B42310 "code_140B42310.asm.txt"
dump_one "code_141C8D740.bin" 0x141C8D740 "code_141C8D740.asm.txt"
dump_one "code_140B347C0.bin" 0x140B347C0 "code_140B347C0.asm.txt"
dump_one "code_140A90550.bin" 0x140A90550 "code_140A90550.asm.txt"
dump_one "code_140A9AAD0.bin" 0x140A9AAD0 "code_140A9AAD0.asm.txt"
dump_one "code_140B1E400.bin" 0x140B1E400 "code_140B1E400.asm.txt"
dump_one "code_140B1EC04.bin" 0x140B1EC04 "code_140B1EC04.asm.txt"
dump_one "code_140B1F0B2.bin" 0x140B1F0B2 "code_140B1F0B2.asm.txt"
dump_one "code_140B21DE9.bin" 0x140B21DE9 "code_140B21DE9.asm.txt"
dump_one "code_140B21F17.bin" 0x140B21F17 "code_140B21F17.asm.txt"
