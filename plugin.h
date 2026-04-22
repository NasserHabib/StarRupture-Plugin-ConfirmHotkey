#pragma once

#include <windows.h>
#include "plugin_interface.h"

// Plugin exports — symbol visibility driven by ConfirmHotkey.def
extern "C" {
	PluginInfo* GetPluginInfo();
	bool PluginInit(IPluginSelf* self);
	void PluginShutdown();
}
