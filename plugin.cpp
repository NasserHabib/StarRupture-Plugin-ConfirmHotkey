#include "plugin.h"
#include "plugin_helpers.h"
#include "plugin_config.h"
#include "ModCore.h"
#include <windows.h>

// Global plugin self pointer
static IPluginSelf* g_self = nullptr;

IPluginSelf* GetSelf() { return g_self; }

// Plugin metadata
#ifndef MODLOADER_BUILD_TAG
#define MODLOADER_BUILD_TAG "dev"
#endif

static PluginInfo s_pluginInfo = {
	"RecyclerHotkey",
	MODLOADER_BUILD_TAG,
	"Nasser",
	"Adds a hotkey to trigger the Recycle button in the Recycler UI.",
	PLUGIN_INTERFACE_VERSION
};

static bool IsClientBinary()
{
	wchar_t path[MAX_PATH] = {};
	if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return false;
	const wchar_t* name = wcsrchr(path, L'\\');
	return _wcsicmp(name ? name + 1 : path, L"StarRupture-Win64-Shipping.exe") == 0;
}

extern "C" {

	__declspec(dllexport) PluginInfo* GetPluginInfo()
	{
		return &s_pluginInfo;
	}

	__declspec(dllexport) bool PluginInit(IPluginSelf* self)
	{
		g_self = self;

		LOG_INFO("RecyclerHotkey: Plugin initializing...");

		RecyclerHotkeyConfig::Config::Initialize(self);

		if (!IsClientBinary())
		{
			LOG_WARN("RecyclerHotkey: Not running on the game client. Plugin will stay loaded but inactive.");
			return true;
		}

		if (!RecyclerHotkeyConfig::Config::IsEnabled())
		{
			LOG_WARN("RecyclerHotkey: Plugin is disabled in config.");
			return true;
		}

		if (!ModCore::Initialize(self))
		{
			LOG_ERROR("RecyclerHotkey: ModCore failed to initialize.");
			return false;
		}

		LOG_INFO("RecyclerHotkey: Plugin initialized successfully.");

		return true;
	}

	__declspec(dllexport) void PluginShutdown()
	{
		LOG_INFO("RecyclerHotkey: Plugin shutting down...");

		ModCore::Shutdown();

		g_self = nullptr;
	}

} // extern "C"
