# Fuzzer artifacts

`libraw-claims-this-png.bin` is 156 bytes of corrupted PNG that LibRaw's parser reads as a 3072x2047
"Contax N Digital" -- and then spends ~38 seconds demosaicing an image that isn't there. It is kept as a
fixture because nothing synthesized reproduces it: the bytes have to be exactly the ones LibRaw's
identify pass happens to interpret as sensor metadata.

`tests/test_loader_limits.cpp` asserts that `is_raw_image()` declines it.
