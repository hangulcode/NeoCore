"""Report where each NeoCore console's dispatch loop starts within a 64-byte line.

Usage: python Tools/bench/dispatch_offset.py <neocore.exe>...

The dispatch sequence of RunInternal (load the instruction pointer, advance it, read the
opcode byte, bound-check it, jump through the table) is about 45 bytes long. Timings of
some benchmark cases change by up to 25% depending on whether that sequence starts at
offset 0/16 or 32/48 of a 64-byte line, so compare two NeoCore builds only at equal
offsets or when their per-round ranges separate. See docs/Benchmarking.md.

The four reported sites are the four RunInternal instantiations (timeout x debug); the
benchmark console runs the last one (no timeout, no debug).
"""
import re
import struct
import sys

PATTERN = re.escape(b'\x0f\xb6\x07\x3c') + b'.' + re.escape(b'\x0f\x87')  # movzx eax,[rdi]; cmp al,imm8; ja rel32
HEAD_BYTES = 12  # mov rdi,[r13+38h]; lea reg,[rdi+8]; mov [r13+38h],reg precede the opcode load


def dispatch_offsets(path):
    """Return [(virtual address, offset within 64 bytes)] for every dispatch site in the image."""
    data = open(path, 'rb').read()
    pe = struct.unpack_from('<I', data, 0x3c)[0]
    count = struct.unpack_from('<H', data, pe + 6)[0]
    optional = struct.unpack_from('<H', data, pe + 20)[0]
    base = struct.unpack_from('<Q', data, pe + 24 + 24)[0]
    sites = []
    for index in range(count):
        header = pe + 24 + optional + index * 40
        if data[header:header + 8].rstrip(b'\0') != b'.text':
            continue
        _, vaddr, rsize, raddr = struct.unpack_from('<IIII', data, header + 8)
        text = data[raddr:raddr + rsize]
        for match in re.finditer(PATTERN, text, re.S):
            head = base + vaddr + match.start() - HEAD_BYTES
            sites.append((head, head % 64))
    return sites


def benchmark_offset(path):
    """Offset of the dispatch loop that `neocore --file` runs (no timeout, no debug), or None."""
    sites = dispatch_offsets(path)
    return sites[-1][1] if sites else None


if __name__ == '__main__':
    for exe in sys.argv[1:]:
        sites = dispatch_offsets(exe)
        print(exe, ' '.join('%X(+%d)' % site for site in sites) or '(no dispatch site found)')
