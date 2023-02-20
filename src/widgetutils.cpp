#include "widgetutils.h"
#include <nanogui/widget.h>
#include <spdlog/spdlog.h>

using namespace nanogui;

int next_visible_child(const Widget *w, int index, EDirection direction)
{
    spdlog::trace("next_visible_child({})", index);
    if (!w->child_count())
        return -1;

    int dir = direction == Forward ? 1 : -1;

    // If the index does not exist, start at index 0.
    int start_index = std::max(0, index);

    int i = start_index;
    do {
        i = (i + w->child_count() + dir) % w->child_count();
    } while (!w->child_at(i)->visible() && i != start_index);

    return i;
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