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
			"ConfirmHotkey",
			ConfigValueType::String,
			"E",
			"The hotkey used to confirm the primary action in single-button interior UIs (Recycler, Analyzing Station). E.g., R, F, etc."
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

		static const char* GetConfirmHotkey()
		{
			static char buffer[64];
			if (s_self && s_self->config->ReadString(s_self, "PluginSettings", "ConfirmHotkey", buffer, sizeof(buffer), "E"))
			{
				return buffer;
			}
			return "E";
		}

	private:
		inline static IPluginSelf* s_self = nullptr;
	};
}
