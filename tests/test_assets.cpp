//
// Copyright (C) Wojciech Jarosz. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#include <doctest/doctest.h>

#include "shader.h"

#ifdef HDRVIEW_BUILD_TREE_ASSETS_DIR
#include <filesystem>
#include <hello_imgui/hello_imgui_assets.h>

TEST_CASE("Build-tree assets required at startup are present after configure")
{
    // Both come from a platform-specific CMake glob (see MY_RESOURCE_FILES in CMakeLists.txt);
    // verify it actually populated the build tree. The dither texture degrades gracefully if
    // missing, but the Roboto font load is fatal.
    HelloImGui::AddAssetsSearchPath(HDRVIEW_BUILD_TREE_ASSETS_DIR);
    CHECK(HelloImGui::AssetExists("dither-texture-256.png"));
    CHECK(HelloImGui::AssetExists("fonts/Roboto/Roboto-Regular.ttf"));
}

TEST_CASE("The sokol-shdc-generated image shader is resolvable by Shader::from_asset()")
{
    // Shader::from_asset() only does text lookup (AssetExists + LoadAssetFileData), so this runs
    // headlessly with no live GL/Metal context needed. Exercises the full path from sokol-shdc's
    // CMake-time code generation (SokolShaderCompiler.cmake) through to runtime asset lookup.
    HelloImGui::AddAssetsSearchPath(HDRVIEW_BUILD_TREE_ASSETS_DIR);
    CHECK_NOTHROW(Shader::from_asset("shaders/image-shader_frag"));
    CHECK_NOTHROW(Shader::from_asset("shaders/image-shader_vert"));
}

TEST_CASE("Build-tree assets are discoverable regardless of the process's current working directory")
{
    // ctest's default working directory happens to be the build tree root, which would let asset
    // lookup pass by accident even without an explicit search path. Chdir elsewhere first so this
    // actually exercises HDRVIEW_BUILD_TREE_ASSETS_DIR / AddAssetsSearchPath rather than that
    // coincidence.
    auto original_cwd = std::filesystem::current_path();
    std::filesystem::current_path(std::filesystem::temp_directory_path());
    HelloImGui::AddAssetsSearchPath(HDRVIEW_BUILD_TREE_ASSETS_DIR);
    CHECK(HelloImGui::AssetExists("dither-texture-256.png"));
    std::filesystem::current_path(original_cwd);
}
#endif
