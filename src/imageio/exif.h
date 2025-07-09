
#pragma once

#include "common.h"
#include "json.h"

json exif_to_json(const uint8_t *data_ptr, size_t data_size);