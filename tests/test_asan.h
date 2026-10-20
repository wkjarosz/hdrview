//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
#pragma once

// AddressSanitizer reports spurious stack-use-after-scope errors inside libjxl's encoder, in SizeHeader::Set ->
// FixedAspectRatios (lib/jxl/headers.cc) and in QuantizeRoundtripYBlockAC: in-bounds reads of function-local
// constexpr arrays the optimizer emitted into .rodata. No suppressions file can filter that class, so the tests
// that write JPEG XL leave it out under ASan alone.
#if defined(__SANITIZE_ADDRESS__)
#define HDRVIEW_TEST_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HDRVIEW_TEST_ASAN 1
#endif
#endif
