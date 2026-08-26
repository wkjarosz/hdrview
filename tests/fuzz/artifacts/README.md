# Fuzzer artifacts

Inputs found by `hdrview_fuzz_load_image` that make `load_image()` allocate or loop far out of
proportion to their size. Each declares enormous dimensions in its header; the guard that rejects
oversized images lives in `Image::finalize()`, which only runs once the loader has already decoded.

| file | declares | size on disk |
|---|---|---|
| `bomb-dds-4294967295sq.dds` | 4294967295 x 4294967295 | 222 B |
| `bomb-gif-19789x19789.gif` | 19789 x 19789 (391 Mpx) | 833 B |
| `bomb-gif-7850x57457.gif` | 7850 x 57457 (451 Mpx) | 111 B |
| `bomb-qoi-4x16776963.qoi` | 4 x 16776963 (67 Mpx) | 90 B |
| `slow-png-157b.png` | malformed | 157 B |

Reproduce with:

    ./build/asan/Debug/hdrview_fuzz_load_image -timeout=30 -rss_limit_mb=3072 tests/fuzz/artifacts/<file>
