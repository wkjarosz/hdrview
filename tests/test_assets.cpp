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
    // copied into the build tree by a platform-specific glob (MY_RESOURCE_FILES in CMakeLists.txt)
    HelloImGui::AddAssetsSearchPath(HDRVIEW_BUILD_TREE_ASSETS_DIR);
    CHECK(HelloImGui::AssetExists("dither-texture-256.png"));
    CHECK(HelloImGui::AssetExists("fonts/Roboto/Roboto-Regular.ttf"));
}

TEST_CASE("The sokol-shdc-generated image shader is resolvable by Shader::from_asset()")
{
    // from_asset() only looks the text up, so no live GL/Metal context is needed here
    HelloImGui::AddAssetsSearchPath(HDRVIEW_BUILD_TREE_ASSETS_DIR);
    CHECK_NOTHROW(Shader::from_asset("shaders/image-shader_frag"));
    CHECK_NOTHROW(Shader::from_asset("shaders/image-shader_vert"));
}

TEST_CASE("Build-tree assets are discoverable regardless of the process's current working directory")
{
    // ctest runs in the build tree root, where the lookup would succeed without a search path
    auto original_cwd = std::filesystem::current_path();
    std::filesystem::current_path(std::filesystem::temp_directory_path());
    HelloImGui::AddAssetsSearchPath(HDRVIEW_BUILD_TREE_ASSETS_DIR);
    CHECK(HelloImGui::AssetExists("dither-texture-256.png"));
    std::filesystem::current_path(original_cwd);
}
#endif
