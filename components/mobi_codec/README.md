# PalmDOC Decoder

Only the bounded read-only PalmDOC algorithm from libmobi 0.12 is included.
Upstream: https://github.com/bfabiszewski/libmobi
Commit: `906274205c11944b628da1c553b255acb1af7c55`.

`palm_doc.c` is an adapted subset of `src/compression.c` and `src/buffer.c`.
It uses caller-owned buffers, rejects invalid/zero-distance back references,
and has no HUFF/CDIC, encryption, file loading, writing or XML dependencies.
The original copyright and LGPL-3.0-or-later terms are retained. See
`COPYING` and `COPYING.LESSER`. The firmware's original code has separate terms.

Before distributing a statically linked firmware, supply the corresponding
source, notices and relinking materials required by LGPL section 4, including
the applicable application object code/toolchain information. Do not apply
personal-study restrictions to libmobi or its independently granted rights.
Local build/test success does not establish release-license compliance.
