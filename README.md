# RecyclerHotkey — StarRupture Client Plugin

Maps a configurable hotkey (default `R`) to the "Confirm Recycle" button in the Recycler UI, so you don't have to mouse-click it after transferring items.

**Target:** Client only. The plugin stays loaded but inactive on the dedicated server binary.

---

## Installation

1. Build `Client Release|x64` (see below) or grab the built DLL.
2. Copy `RecyclerHotkey.dll` to `<game_dir>\Binaries\Win64\Plugins\` next to `dwmapi.dll`.
3. Launch the game once — `Plugins\config\RecyclerHotkey.ini` is generated with defaults.
4. Edit the INI if you want a different key.

> **Requires [StarRupture-ModLoader](https://github.com/AlienXAXS/StarRupture-ModLoader)** (the `dwmapi.dll` proxy loader) to be installed first.

## Config (`Plugins\config\RecyclerHotkey.ini`)

```ini
[General]
Enabled=true

[PluginSettings]
RecycleHotkey=R
```

Hotkey names resolve through the ModLoader's `IPluginInputEvents::RegisterKeybindByName` — any key string the loader recognizes (`R`, `F`, function keys, etc.) should work.

## How it works

On `Client Release` build + `Enabled=true` + running on `StarRupture-Win64-Shipping.exe`:

1. `PluginInit` stores `IPluginSelf*`, inits config, verifies client binary.
2. `ModCore::Initialize` registers the configured hotkey via `Input->RegisterKeybindByName(..., EModKeyEvent::Pressed, ...)`.
3. On keypress, the callback walks `UObject::GObjects` looking for a live `UCrUW_RecyclingStatus` widget (`IsInViewport() == true`), then calls its `HandleOnRecycleButtonClicked()` directly.
4. `PluginShutdown` unregisters the keybind and clears `IPluginSelf`.

No low-level hooks, no pattern scanning — pure typed-hook consumer. See the `claude_plugin_dev_guide.md` appendix on KeepTicking for the broader pattern.

## Building from source

Requires Visual Studio 2022 with the v143 toolset and the [StarRupture-Plugin-SDK](https://github.com/AlienXAXS/StarRupture-Plugin-SDK) (with the `StarRupture SDK` submodule initialized).

This project's `.vcxproj` must live inside a solution that imports the SDK's `Shared.props` — typically you add it to `StarRupture-Plugin-SDK.sln`:

1. Open `StarRupture-Plugin-SDK.sln`.
2. Right-click the solution → Add → Existing Project → select `RecyclerHotkey\RecyclerHotkey.vcxproj`.
3. Build the `Client Release|x64` configuration.
4. Output: `bin\x64\Client Release\plugins\RecyclerHotkey.dll`.

The `Debug`/`Release` (generic) and `Server Debug`/`Server Release` configurations exist so the solution stays buildable in those modes, but this plugin does nothing useful outside `Client *`.

## Files

- `plugin.{h,cpp}` — the three ABI exports + `IPluginSelf` lifecycle + client-binary guard.
- `ModCore.{h,cpp}` — hotkey registration, GObjects scan, click-handler invocation.
- `plugin_config.h` — INI schema + typed accessors (`RecyclerHotkeyConfig::Config`).
- `plugin_helpers.h` — `LOG_*` macros and `GetHooks/GetConfig/GetScanner` wrappers.
- `dllmain.cpp` — stock Windows DLL entry.

## Disclaimer

Use at your own risk. Learning project — no warranty, no support.
