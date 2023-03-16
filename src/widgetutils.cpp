#include "widgetutils.h"
#include "common.h"
#include <nanogui/widget.h>
#include <spdlog/spdlog.h>

using namespace nanogui;

int next_visible_child(const Widget *w, int start_index, EDirection direction, bool must_be_enabled)
{
    spdlog::trace("next_visible_child({})", start_index);
    if (!w->child_count())
        return -1;

    int dir = direction == Forward ? 1 : -1;

    int found_index = start_index;
    if (!(0 <= start_index && start_index < w->child_count()))
        start_index = dir > 0 ? -1 : w->child_count();

    for (int inc = 1; inc < w->child_count(); ++inc)
    {
        int i = mod(start_index - dir * inc, w->child_count());
        if (!w->child_at(i)->visible() || !(w->child_at(i)->enabled() || !must_be_enabled))
            continue;

        found_index = i;
    }

    return found_index;
}

int nth_visible_child_index(const Widget *w, int n)
{
    if (n < 0)
        return -1;

    int last_visible = -1;
    for (int i = 0; i < w->child_count(); ++i)
    {
        if (w->child_at(i)->visible())
        {
            last_visible = i;
            if (n == 0)
                break;

            --n;
        }
    }
    return last_visible;
}