#include <cstdint>
#include <iostream>
#include <vector>

void extract_psd_exif_xmp(std::istream &stream, std::vector<uint8_t> &exif, std::vector<uint8_t> &xmp);