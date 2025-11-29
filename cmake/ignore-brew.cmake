# cmake/ignore-brew.cmake Force CMake to use only system prefixes and prevent pkg-config from exposing Homebrew/MacPorts
# Loaded as a toolchain file in CI (use -DCMAKE_TOOLCHAIN_FILE=...).

# Whitelist system prefixes (only these prefixes will be considered first) set(CMAKE_PREFIX_PATH
# "/usr;/Library/Frameworks" CACHE STRING "Search prefixes" FORCE ) set(CMAKE_SYSTEM_PREFIX_PATH
# "/usr;/Library/Frameworks" CACHE STRING "System prefixes" FORCE )

# # Extra guard: ignore common package manager prefix roots set(CMAKE_IGNORE_PATH
# "/opt/homebrew;/opt/homebrew/Cellar;/usr/local;/opt/local" CACHE STRING "" FORCE )

# # Prevent pkg-config from finding Homebrew-installed .pc files PKG_CONFIG_LIBDIR should point only to system pkgconfig
# # dirs set(ENV{PKG_CONFIG_PATH} "") set(ENV{PKG_CONFIG_LIBDIR} "/usr/lib/pkgconfig:/usr/share/pkgconfig")

# # If cross-compiling is NOT the goal, make sure find_package won't fallback to undesired roots (leave these modes
# alone # unless you have cross compile needs) Optionally force find root behavior for packages: #
# set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE NEVER CACHE STRING "" FORCE)

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
