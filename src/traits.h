/*
    NanoGUI was developed by Wenzel Jakob <wenzel.jakob@epfl.ch>.
    The widget drawing code is based on the NanoVG demo application
    by Mikko Mononen.

    All rights reserved. Use of this source code is governed by a
    BSD-style license that can be found in the LICENSE.txt file.
*/

#pragma once

#include <stdexcept>
#include <type_traits>

/// Listing of various field types that can be used as variables in shaders
enum class VariableType
{
    Invalid = 0,
    Int8,
    UInt8,
    Int16,
    UInt16,
    Int32,
    UInt32,
    Int64,
    UInt64,
    Float16,
    Float32,
    Float64,
    Bool
};

/// Convert from a C++ type to an element of \ref VariableType
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4702)
#endif
template <typename T>
constexpr VariableType get_type()
{
    if constexpr (std::is_same_v<T, bool>)
        return VariableType::Bool;

    if constexpr (std::is_integral_v<T>)
    {
        if constexpr (sizeof(T) == 1)
            return std::is_signed_v<T> ? VariableType::Int8 : VariableType::UInt8;
        else if constexpr (sizeof(T) == 2)
            return std::is_signed_v<T> ? VariableType::Int16 : VariableType::UInt16;
        else if constexpr (sizeof(T) == 4)
            return std::is_signed_v<T> ? VariableType::Int32 : VariableType::UInt32;
        else if constexpr (sizeof(T) == 8)
            return std::is_signed_v<T> ? VariableType::Int64 : VariableType::UInt64;
    }
    else if constexpr (std::is_floating_point_v<T>)
    {
        if constexpr (sizeof(T) == 2)
            return VariableType::Float16;
        else if constexpr (sizeof(T) == 4)
            return VariableType::Float32;
        else if constexpr (sizeof(T) == 8)
            return VariableType::Float64;
    }
    else
    {
        return VariableType::Invalid;
    }
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
}

/// Return the size in bytes associated with a specific variable type
inline size_t type_size(VariableType type)
{
    switch (type)
    {
    case VariableType::UInt8:
    case VariableType::Int8:
    case VariableType::Bool: return 1;

    case VariableType::UInt16:
    case VariableType::Int16:
    case VariableType::Float16: return 2;

    case VariableType::UInt32:
    case VariableType::Int32:
    case VariableType::Float32: return 4;

    case VariableType::UInt64:
    case VariableType::Int64:
    case VariableType::Float64: return 8;

    default: throw std::invalid_argument("Unknown type!");
    }
}

/// Return the name (e.g. "uint8") associated with a specific variable type
inline const char *type_name(VariableType type)
{
    switch (type)
    {
    case VariableType::Bool: return "bool";
    case VariableType::UInt8: return "uint8";
    case VariableType::Int8: return "int8";
    case VariableType::UInt16: return "uint16";
    case VariableType::Int16: return "int16";
    case VariableType::UInt32: return "uint32";
    case VariableType::Int32: return "int32";
    case VariableType::UInt64: return "uint64";
    case VariableType::Int64: return "int64";
    case VariableType::Float16: return "float16";
    case VariableType::Float32: return "float32";
    case VariableType::Float64: return "float64";
    default: return "invalid";
    }
}
