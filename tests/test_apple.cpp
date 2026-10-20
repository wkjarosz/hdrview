//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include "test_apple.h"

#if defined(__APPLE__)

#include <CoreGraphics/CoreGraphics.h>
#include <ImageIO/ImageIO.h>
#include <cstdlib>
#include <sys/sysctl.h>

namespace hdrview_test
{

int apple_os_major()
{
    char   version[32] = {};
    size_t size        = sizeof(version);
    return sysctlbyname("kern.osproductversion", version, &size, nullptr, 0) == 0 ? std::atoi(version) : 0;
}

std::optional<AppleImage> apple_decode(const std::string &bytes, AppleDecode request)
{
    CFDataRef        data = CFDataCreate(nullptr, reinterpret_cast<const UInt8 *>(bytes.data()), (CFIndex)bytes.size());
    CGImageSourceRef source = data ? CGImageSourceCreateWithData(data, nullptr) : nullptr;

    CFMutableDictionaryRef options =
        CFDictionaryCreateMutable(nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    if (request != AppleDecode::Default)
    {
        // macOS 14 introduced the requests for a gain-map file's renditions
        if (__builtin_available(macOS 14.0, *))
            CFDictionarySetValue(options, kCGImageSourceDecodeRequest,
                                 request == AppleDecode::HDR ? kCGImageSourceDecodeToHDR : kCGImageSourceDecodeToSDR);
        else
        {
            CFRelease(options);
            if (source)
                CFRelease(source);
            if (data)
                CFRelease(data);
            return std::nullopt;
        }
    }
    CGImageRef image = source ? CGImageSourceCreateImageAtIndex(source, 0, options) : nullptr;

    std::optional<AppleImage> result;
    if (image)
    {
        const size_t       w = CGImageGetWidth(image), h = CGImageGetHeight(image);
        std::vector<float> rgba(w * h * 4);
        CGColorSpaceRef    space = CGColorSpaceCreateWithName(kCGColorSpaceExtendedLinearSRGB);
        CGContextRef       ctx   = CGBitmapContextCreate(rgba.data(), w, h, 32, w * 16, space,
                                                         kCGImageAlphaPremultipliedLast | kCGBitmapFloatComponents |
                                                             kCGBitmapByteOrder32Little);
        if (ctx)
        {
            // headroom for everything an HDR decode holds, whatever a context defaults to
            if (__builtin_available(macOS 15.0, *))
                CGContextSetEDRTargetHeadroom(ctx, 64.f);
            CGContextSetBlendMode(ctx, kCGBlendModeCopy);
            CGContextDrawImage(ctx, CGRectMake(0, 0, (CGFloat)w, (CGFloat)h), image);
            AppleImage out;
            out.size = int2{(int)w, (int)h};
            out.pixels.resize(w * h);
            // the bitmap's first row is the image's bottom one
            for (size_t y = 0; y < h; ++y)
                for (size_t x = 0; x < w; ++x)
                {
                    const size_t i        = ((h - 1 - y) * w + x) * 4;
                    out.pixels[y * w + x] = float4{rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3]};
                }
            result = std::move(out);
            CGContextRelease(ctx);
        }
        CGColorSpaceRelease(space);
        CGImageRelease(image);
    }
    CFRelease(options);
    if (source)
        CFRelease(source);
    if (data)
        CFRelease(data);
    return result;
}

} // namespace hdrview_test

#endif
