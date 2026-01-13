//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

//! Endianness indicator
enum class Endian
{
    Little   = 0,
    Intel    = Little,
    Big      = 1,
    Motorola = Big
};

//! Returns 1 if architecture is little endian, 0 in case of big endian.
inline bool is_little_endian()
{
    unsigned int x = 1;
    char        *c = (char *)&x;
    return bool((int)*c);
}

inline Endian host_endian() { return is_little_endian() ? Endian::Little : Endian::Big; }

#if defined(_MSC_VER)
#pragma intrinsic(_byteswap_ushort)
#pragma intrinsic(_byteswap_ulong)
#pragma intrinsic(_byteswap_uint64)

#define byte_swap_16 _byteswap_ushort
#define byte_swap_32 _byteswap_ulong
#define byte_swap_64 _byteswap_uint64
#else
#define byte_swap_16 __builtin_bswap16
#define byte_swap_32 __builtin_bswap32
#define byte_swap_64 __builtin_bswap64
#endif

template <typename T>
inline T swap_bytes(T value)
{
    if constexpr (sizeof(T) == 1)
    {
        return value;
    }
    else if constexpr (sizeof(T) == 2)
    {
        uint16_t swapped = byte_swap_16(*reinterpret_cast<uint16_t *>(&value));
        return *reinterpret_cast<T *>(&swapped);
    }
    else if constexpr (sizeof(T) == 4)
    {
        uint32_t swapped = byte_swap_32(*reinterpret_cast<uint32_t *>(&value));
        return *reinterpret_cast<T *>(&swapped);
    }
    else if constexpr (sizeof(T) == 8)
    {
        uint64_t swapped = byte_swap_64(*reinterpret_cast<uint64_t *>(&value));
        return *reinterpret_cast<T *>(&swapped);
    }
    else
    {
        static_assert(sizeof(T) == 1 || sizeof(T) == 2 || sizeof(T) == 4 || sizeof(T) == 8,
                      "Unsupported type size for byte swapping.");
    }
}

#undef byte_swap_16
#undef byte_swap_32
#undef byte_swap_64

/*!
 * @brief Read a value of type T from a byte array and convert to host endianness.
 *
 * Reads sizeof(T) bytes from the given pointer and interprets them as type T,
 * performing byte swapping if the data endianness differs from the machine's.
 *
 * @tparam T The type to read (e.g., float, double, uint32_t)
 * @param ptr Pointer to the byte array to read from
 * @param data_endian The endianness of the data in the byte array
 * @return The value of type T in host endianness
 */
template <typename T>
T read_as(const unsigned char *ptr, Endian data_endian)
{
    T value;
    memcpy(&value, ptr, sizeof(T));

    // Only swap bytes if necessary
    if (data_endian != host_endian())
        value = swap_bytes(value);

    return value;
}

/*!
 * @brief Read an array of values of type T from a byte array and convert to host endianness.
 *
 * Reads count * sizeof(T) bytes from the input pointer and interprets them as an array of type T,
 * performing byte swapping on each element if the data endianness differs from the machine's.
 *
 * @tparam T The type to read (e.g., float, double, uint32_t, int32_t)
 * @param output Pointer to the output array where results will be written
 * @param input Pointer to the input byte array to read from
 * @param count Number of elements to read
 * @param data_endian The endianness of the data in the input byte array
 */
template <typename T>
void read_array(T *output, const unsigned char *input, size_t count, Endian data_endian)
{
    // First, copy all bytes at once
    memcpy(output, input, count * sizeof(T));

    // Only swap bytes if necessary
    if (data_endian != host_endian())
        for (size_t i = 0; i < count; i++) output[i] = swap_bytes(output[i]);
}

/*!
 * @brief Write a value of type T to a byte array with specified endianness.
 *
 * Writes sizeof(T) bytes to the given pointer, performing byte swapping if the target
 * endianness differs from the machine's endianness.
 *
 * @tparam T The type to write (e.g., float, double, uint32_t)
 * @param ptr Pointer to the byte array to write to
 * @param value The value to write
 * @param target_endian The desired endianness for the data in the byte array
 */
template <typename T>
void write_as(unsigned char *ptr, T value, Endian target_endian)
{
    // Swap bytes if target endianness doesn't match machine endianness
    if (target_endian != host_endian())
        value = swap_bytes(value);

    memcpy(ptr, &value, sizeof(T));
}

/*!
 * @brief Write an array of values of type T to a byte array with specified endianness.
 *
 * Writes count * sizeof(T) bytes to the output pointer, performing byte swapping on each
 * element if the target endianness differs from the machine's.
 *
 * @tparam T The type to write (e.g., float, double, uint32_t, int32_t)
 * @param output Pointer to the output byte array to write to
 * @param input Pointer to the input array of values
 * @param count Number of elements to write
 * @param target_endian The desired endianness for the data in the output byte array
 */
template <typename T>
void write_array(unsigned char *output, const T *input, size_t count, Endian target_endian)
{
    // First, copy all bytes at once
    memcpy(output, input, count * sizeof(T));

    // Only swap bytes if necessary
    if (target_endian != host_endian())
    {
        T *output_typed = reinterpret_cast<T *>(output);
        for (size_t i = 0; i < count; i++) output_typed[i] = swap_bytes(output_typed[i]);
    }
}
