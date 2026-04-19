# RecyclerHotkey — StarRupture Client Plugin

Maps a configurable hotkey (default `R`) to the primary confirm button in single-button interior UIs, so you don't have to mouse-click them after staging items.

**Currently supports:**

| Building | UI widget | Button | Handler method |
|---|---|---|---|
| Recycler | `UCrUW_RecyclingStatus` | **RECYCLE** | `HandleOnRecycleButtonClicked` |
| Analyzing Station | `UCrUW_Analyzer` | **CLAIM** | `HandleClaimClicked` |

**Scope rule — single-button UIs only.** The plugin deliberately avoids binding a hotkey inside any UI with multiple action buttons, because a shared hotkey in that context is unpredictable (the user can't know which button will fire). The Recycler's Blueprint-added **FULL RECYCLE** button is *not* bound — see `RecyclerHotkeyMultiTargetPlan.md` footnote for rationale.

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
ConfirmHotkey=R
```

Hotkey names resolve through the ModLoader's `IPluginInputEvents::RegisterKeybindByName` — any key string the loader recognizes (`R`, `F`, function keys, etc.) should work.

> **Upgrading from an older build?** The config key was renamed from `RecycleHotkey` to `ConfirmHotkey`. `InitializeFromSchema` will add the new key automatically on first launch after upgrade; the old `RecycleHotkey=...` line remains in the INI as an inert dangling entry (nothing reads it). Delete it if you want a clean file.

## How it works

On `Client Release` build + `Enabled=true` + running on `StarRupture-Win64-Shipping.exe`:

1. `PluginInit` stores `IPluginSelf*`, inits config, verifies client binary.
2. `ModCore::Initialize` registers the configured hotkey via `Input->RegisterKeybindByName(..., EModKeyEvent::Pressed, ...)`.
3. On keypress, the callback walks `UObject::GObjects` in a single pass, checking each object against the supported-widget classes in precedence order: **Recycler → Analyzer**. The first widget that's in the viewport wins — its `Handle*Clicked` method is invoked directly via the Dumper-7 SDK, wrapped in SEH for safety.
4. `PluginShutdown` unregisters the keybind and clears `IPluginSelf`.

No low-level hooks, no pattern scanning — pure typed-hook consumer. See the `claude_plugin_dev_guide.md` appendix on KeepTicking for the broader pattern.

## Adding a new target UI (future reference)

1. Confirm the widget class has **exactly one** `Handle*Clicked` method in the Dumper-7 SDK. If it has more than one, stop — don't add it. (A single-button widget in the native class that has Blueprint-added siblings also counts as multi-button; grep the `WBP_*` classes to check.)
2. Verify the class declaration is reachable from `ChimeraUI_classes.hpp` and the method body from `ChimeraUI_functions.cpp` (both already included in `ModCore.cpp`). Other modules need their own `#include`s.
3. Add a parallel `SafeInvokeX` helper in `ModCore.cpp` — one-liner SEH wrapper.
4. Add a new `if (Obj->IsA(SDK::UCrUW_X::StaticClass())) { ... }` block inside the GObjects scan loop. Order within the loop determines precedence if two target widgets are somehow in the viewport simultaneously.
5. Update the "Currently supports" table above.
6. If target count reaches ~4, refactor the scan loop into a `HotkeyTarget[]` table — noted in the plan.

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
- `ModCore.{h,cpp}` — hotkey registration, GObjects scan, per-target invocation.
- `plugin_config.h` — INI schema + typed accessors (`RecyclerHotkeyConfig::Config`).
- `plugin_helpers.h` — `LOG_*` macros and `GetHooks/GetConfig/GetScanner` wrappers.
- `dllmain.cpp` — stock Windows DLL entry.

## Disclaimer

Use at your own risk. Learning project — no warranty, no support.
