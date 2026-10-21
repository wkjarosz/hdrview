//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//
#pragma once

// Defines HDRVIEW_TEST_ASAN in an AddressSanitizer build, where the tests write no JPEG XL.
// ASan reports spurious stack-use-after-scope errors inside libjxl's encoder, in SizeHeader::Set ->
// FixedAspectRatios (lib/jxl/headers.cc) and in QuantizeRoundtripYBlockAC: in-bounds reads of function-local
// constexpr arrays the optimizer emitted into .rodata. No suppressions file can filter that class.
#if defined(__SANITIZE_ADDRESS__)
#define HDRVIEW_TEST_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define HDRVIEW_TEST_ASAN 1
#endif
#endif
