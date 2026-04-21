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
#define MODLOADER_BUILD_TAG "v0.1.0"
#endif

static PluginInfo s_pluginInfo = {
	"ConfirmHotkey",
	MODLOADER_BUILD_TAG,
	"S4cobra",
	"Adds a hotkey that confirms the primary action in single-button interior UIs (Recycler, Analyzing Station).",
	PLUGIN_INTERFACE_VERSION
};

static bool IsClientBinary()
{
	wchar_t path[MAX_PATH] = {};
	if (!GetModuleFileNameW(nullptr, path, MAX_PATH)) return false;
	const wchar_t* name = wcsrchr(path, L'\\');
	name = name ? name + 1 : path;
	if (_wcsnicmp(name, L"StarRupture", 11) != 0) return false;
	if (wcsstr(name, L"Server") != nullptr) return false;
	return true;
}

extern "C" {

	__declspec(dllexport) PluginInfo* GetPluginInfo()
	{
		return &s_pluginInfo;
	}

	__declspec(dllexport) bool PluginInit(IPluginSelf* self)
	{
		g_self = self;

		LOG_INFO("ConfirmHotkey: Plugin initializing...");

		ConfirmHotkeyConfig::Config::Initialize(self);

		if (!IsClientBinary())
		{
			char exePath[MAX_PATH] = {};
			GetModuleFileNameA(nullptr, exePath, MAX_PATH);
			const char* basename = strrchr(exePath, '\\');
			basename = basename ? basename + 1 : exePath;
			LOG_WARN("ConfirmHotkey: Host executable '%s' is not a recognized client binary. Plugin will stay loaded but inactive.", basename);
			return true;
		}

		if (!ConfirmHotkeyConfig::Config::IsEnabled())
		{
			LOG_WARN("ConfirmHotkey: Plugin is disabled in config.");
			return true;
		}

		if (!ModCore::Initialize(self))
		{
			LOG_ERROR("ConfirmHotkey: ModCore failed to initialize.");
			return false;
		}

		LOG_INFO("ConfirmHotkey: Plugin initialized successfully.");

		return true;
	}

	__declspec(dllexport) void PluginShutdown()
	{
		LOG_INFO("ConfirmHotkey: Plugin shutting down...");

		ModCore::Shutdown();

		g_self = nullptr;
	}

} // extern "C"
