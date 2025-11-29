# cmake/ignore-brew.cmake Force CMake to use only system prefixes and prevent pkg-config from exposing Homebrew/MacPorts
# Loaded as a toolchain file in CI (use -DCMAKE_TOOLCHAIN_FILE=...).

set(CMAKE_PREFIX_PATH
    ""
    CACHE STRING "Clear prefix paths" FORCE
)
set(CMAKE_SYSTEM_PREFIX_PATH
    ""
    CACHE STRING "Clear system prefix paths" FORCE
)
set(CMAKE_IGNORE_PATH
    "/opt/homebrew;/opt/homebrew/Cellar;/usr/local;/usr/local/opt"
    CACHE STRING "Paths to ignore" FORCE
)
set(CMAKE_IGNORE_PREFIX_PATH
    "/opt/homebrew;/opt/homebrew/Cellar;/usr/local;/usr/local/opt"
    CACHE STRING "Paths to ignore" FORCE
)

set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/pkgconfig")
set(ENV{PKG_CONFIG_PATH} "")

set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE
    "NEVER"
    CACHE STRING "" FORCE
)
