#pragma once
#include "json.h"
#include <ImfHeader.h>

json exr_header_to_json(const Imf::Header &header);
