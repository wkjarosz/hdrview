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

TEST_CASE("Build-tree assets required at startup are found from any working directory")
{
    // Copied into the build tree by a platform-specific glob (MY_RESOURCE_FILES in CMakeLists.txt). ctest runs in
    // the build tree's root, where the lookup would succeed without a search path, so this looks from elsewhere.
    const auto original_cwd = std::filesystem::current_path();
    std::filesystem::current_path(std::filesystem::temp_directory_path());
    HelloImGui::AddAssetsSearchPath(HDRVIEW_BUILD_TREE_ASSETS_DIR);
    CHECK(HelloImGui::AssetExists("dither-texture-256.png"));
    CHECK(HelloImGui::AssetExists("fonts/Roboto/Roboto-Regular.ttf"));
    // the sokol-shdc-generated image shader; from_asset() only looks the text up, so no GL/Metal context is needed
    CHECK_NOTHROW(Shader::from_asset("shaders/image-shader_frag"));
    CHECK_NOTHROW(Shader::from_asset("shaders/image-shader_vert"));
    std::filesystem::current_path(original_cwd);
}
#endif
