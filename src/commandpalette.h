
#pragma once

#include "dialog.h"
#include "filterablelist.h"
#include "menu.h"
#include "searchbox.h"

NAMESPACE_BEGIN(nanogui)

class Command : public MenuItem
{
public:
    Command(Widget *parent, const std::vector<std::string> &aliases, int button_icon = 0,
            const std::vector<Shortcut> &s = {{0, 0}}) :
        MenuItem(parent, aliases.front(), button_icon, s),
        aliases(aliases)
    {
        // empty
    }

    Command(const std::vector<std::string> &aliases, int icon, int flags, const std::function<void()> &callback,
            const std::function<void(bool)> &change_callback, bool pushed = false,
            const std::vector<Shortcut> &shortcuts = {{0, 0}}, const std::string &tooltip = "");

    virtual void draw(NVGcontext *ctx) override;

    std::vector<std::string> aliases;
    std::vector<int>         matches;
};

class CommandPalette : public Dialog
{
public:
    CommandPalette(Widget *parent, const std::vector<Command *> &commands = {});

    virtual void draw(NVGcontext *ctx) override;
    virtual bool keyboard_event(int key, int scancode, int action, int modifiers) override;

protected:
    void highlight_first_item();
    void update_geometry();
    void scroll_to_ensure_visible(int idx);

    SearchBox          *m_search_box      = nullptr;
    class SortableList *m_commandlist     = nullptr;
    VScrollPanel       *m_vscroll         = nullptr;
    int                 m_highlighted_idx = -1; ///< The index of the currently hovered/highlighted item
};

NAMESPACE_END(nanogui)
