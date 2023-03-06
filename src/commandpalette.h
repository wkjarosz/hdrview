
#pragma once

#include "dialog.h"
#include "filterablelist.h"
#include "menu.h"
#include "searchbox.h"

NAMESPACE_BEGIN(nanogui)

class CommandPalette : public Dialog
{
public:
    class PaletteItem;
    class SortableList;

    CommandPalette(Widget *parent, const std::vector<MenuItem *> &commands = {},
                   const std::vector<std::vector<std::string>> &aliases = {{}});

    /// Add a command based on a MenuItem to the palette
    void add_command(const MenuItem *command, const std::vector<std::string> &aliases = {});

    void add_command(const std::vector<std::string> &aliases, int icon, int flags,
                     const std::function<void()> &callback, const std::function<void(bool)> &change_callback,
                     bool pushed = false, const std::vector<Shortcut> &shortcuts = {{0, 0}},
                     const std::string &tooltip = "");

    virtual void draw(NVGcontext *ctx) override;
    virtual bool keyboard_event(int key, int scancode, int action, int modifiers) override;

protected:
    void highlight_first_item();
    void update_geometry();
    void scroll_to_ensure_visible(int idx);

    SearchBox                 *m_search_box  = nullptr;
    SortableList              *m_commandlist = nullptr;
    VScrollPanel              *m_vscroll     = nullptr;
    int                        m_current     = -1; ///< The currently selected item
    std::vector<PaletteItem *> m_commands;
};

NAMESPACE_END(nanogui)
