#pragma once

#include "plugin_interface.h"

class ModCore
{
public:
    static bool Initialize(IPluginSelf* self);
    static void Shutdown();

private:
    static void OnConfirmHotkey(EModKey key, EModKeyEvent event);

    static IPluginSelf* s_self;
    static char         s_keyName[64];
};
