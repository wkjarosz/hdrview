//
// Copyright (C) Wojciech Jarosz <wjarosz@gmail.com>. All rights reserved.
// Use of this source code is governed by a BSD-style license that can
// be found in the LICENSE.txt file.
//

#pragma once

#include "fwd.h"

int next_visible_child(const nanogui::Widget *w, int index, EDirection direction, bool must_be_enabled = false);
int nth_visible_child_index(const nanogui::Widget *w, int n);