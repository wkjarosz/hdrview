/**
    \file opengl_check.h
*/
#pragma once

bool check_glerror(const char *cmd, const char *file, int line);

#if defined(NDEBUG)
#define CHK(cmd) cmd
#else
#define CHK(cmd)                                                                                                       \
    do                                                                                                                 \
    {                                                                                                                  \
        cmd;                                                                                                           \
        while (check_glerror(#cmd, __FILE__, __LINE__))                                                                \
        {                                                                                                              \
        }                                                                                                              \
    } while (false)
#endif