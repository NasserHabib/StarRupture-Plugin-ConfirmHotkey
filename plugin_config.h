#pragma once

#include "plugin_interface.h"

namespace RecyclerHotkeyConfig
{
	// Define config schema
	static const ConfigEntry CONFIG_ENTRIES[] = {
		{
			"General",
			"Enabled",
			ConfigValueType::Boolean,
			"true",
			"Enable or disable the Recycler Hotkey plugin"
		},
		{
			"PluginSettings",
			"RecycleHotkey",
			ConfigValueType::String,
			"R",
			"The hotkey used to trigger the Recycle button (e.g., R, F, etc.)"
		}
	};

	static const ConfigSchema SCHEMA = {
		CONFIG_ENTRIES,
		sizeof(CONFIG_ENTRIES) / sizeof(ConfigEntry)
	};

	// Type-safe config accessor class
	class Config
	{
	public:
		static void Initialize(IPluginSelf* self)
		{
			s_self = self;

			// Initialize config from schema - creates file with defaults if missing
			if (s_self)
			{
				s_self->config->InitializeFromSchema(s_self, &SCHEMA);
			}
		}

		static bool IsEnabled()
		{
			return s_self ? s_self->config->ReadBool(s_self, "General", "Enabled", true) : true;
		}

		static const char* GetRecycleHotkey()
		{
			static char buffer[64];
			if (s_self && s_self->config->ReadString(s_self, "PluginSettings", "RecycleHotkey", buffer, sizeof(buffer), "R"))
			{
				return buffer;
			}
			return "R";
		}

	private:
		inline static IPluginSelf* s_self = nullptr;
	};
}
