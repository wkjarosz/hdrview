
#pragma once

#include "dialog.h"
#include "filterablelist.h"

NAMESPACE_BEGIN(nanogui)

/// Similar to MessageDialog but extensible and can be used with FormHelper class
class CommandPalette : public Dialog
{
public:
    CommandPalette(Widget *parent, const std::vector<MenuItem *> &commands = {});

    virtual bool keyboard_event(int key, int scancode, int action, int modifiers) override;
    // virtual void draw(NVGcontext *ctx) override;

protected:
    FilterableList *m_commandlist = nullptr;
};

NAMESPACE_END(nanogui)
