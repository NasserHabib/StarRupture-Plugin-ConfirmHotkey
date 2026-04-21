# ConfirmHotkey — StarRupture Client Plugin

> *Previously named `RecyclerHotkey`. Renamed because it's not Recycler-specific — it binds the primary confirm button in every single-button interior UI.*

Maps a configurable hotkey (default `E`) to the primary confirm button in single-button interior UIs, so you don't have to mouse-click them after staging items.

**Currently verified:**

| Building | Blueprint widget | Button | Primary action |
|---|---|---|---|
| Recycler | `WBP_Recycler_C` | **RECYCLE** | Recycle staged items |
| Analyzing Station | `WBP_Analyzer_C` | **CLAIM** | Claim a completed analysis |

Both widgets inherit from the game's generic confirm-action base class `SDK::UCrUW_Analyzer`. The plugin scans `UObject::GObjects` on each keypress, finds the first visible `UCrUW_Analyzer`-derived widget, and invokes its `ClaimButton->ButtonClicked()` (for the UI presentation — sound, animation) plus `HandleClaimClicked()` (for the gameplay effect). **Any future single-button interior UI the game ships that also inherits from `UCrUW_Analyzer` should work automatically, no plugin update required.**

**Scope rule — single-button UIs only.** The plugin deliberately avoids binding a hotkey inside any UI with multiple action buttons, because a shared hotkey in that context is unpredictable (the user can't know which button will fire). The Recycler's Blueprint-added **FULL RECYCLE** button is *not* bound — see `ConfirmHotkeyMultiTargetPlan.md` footnote for rationale.

**Target:** Client only. The plugin stays loaded but inactive on the dedicated server binary.

---

## Installation

1. Build `Client Release|x64` (see below) or grab the built DLL.
2. Copy `ConfirmHotkey.dll` to `<game_dir>\Binaries\Win64\Plugins\` next to `dwmapi.dll`.
3. Launch the game once — `Plugins\config\ConfirmHotkey.ini` is generated with defaults.
4. Edit the INI if you want a different key.

> **Requires [StarRupture-ModLoader](https://github.com/AlienXAXS/StarRupture-ModLoader)** (the `dwmapi.dll` proxy loader) to be installed first.

## Config (`Plugins\config\ConfirmHotkey.ini`)

```ini
[General]
Enabled=true

[PluginSettings]
ConfirmHotkey=E
```

Hotkey names resolve through the ModLoader's `IPluginInputEvents::RegisterKeybindByName` — any key string the loader recognizes (`R`, `F`, function keys, etc.) should work.

> **Upgrading from an older build?** The config key was renamed from `RecycleHotkey` to `ConfirmHotkey`. `InitializeFromSchema` will add the new key automatically on first launch after upgrade; the old `RecycleHotkey=...` line remains in the INI as an inert dangling entry (nothing reads it). Delete it if you want a clean file.

## How it works

On `Client Release` build + `Enabled=true` + running on a `StarRupture*-Win64-Shipping.exe` binary that doesn't contain "Server":

1. `PluginInit` stores `IPluginSelf*`, inits config, verifies client binary.
2. `ModCore::Initialize` registers the configured hotkey via `Input->RegisterKeybindByName(..., EModKeyEvent::Pressed, ...)` and subscribes to `UI->RegisterOnConfigChanged(...)` so the key can be rebound from the in-game config menu without a plugin reload.
3. On keypress, the callback walks `UObject::GObjects` in a single pass, filtering to non-CDO objects whose class `IsA SDK::UCrUW_Analyzer`. The first one whose `UWidget::IsVisible()` returns true wins — `ClaimButton->ButtonClicked()` fires for the sound and animation, then `HandleClaimClicked()` fires for the gameplay effect. Both paths are SEH-wrapped so a mid-teardown widget can't take the game down.
4. The success log captures the widget's actual Blueprint class name (`WBP_Recycler_C`, `WBP_Analyzer_C`, …) so you can tell from the log which building fired.
5. `PluginShutdown` unregisters the keybind and the config-change subscription, and clears `IPluginSelf`.

No low-level hooks, no pattern scanning — pure typed-hook consumer. See the `claude_plugin_dev_guide.md` appendix on KeepTicking for the broader pattern.

## Extending to new UIs

**Happy path (likely works with zero code changes):** the game adds a new single-button interior UI, its Blueprint widget inherits from `UCrUW_Analyzer`, and it follows the same `ClaimButton` + `HandleClaimClicked` convention. The plugin's `IsA` match picks it up automatically — just open that UI, press your hotkey, and check the log for a `Confirmed action on WBP_<new>_C` line. Add the building's row to the "Currently verified" table when you confirm it.

**If it doesn't inherit from `UCrUW_Analyzer`:** find the base class via the `[diag]` log (the plugin logs every `IsA` match at DEBUG level with the actual class name). Then add a second branch in `ModCore::OnConfirmHotkey`:

1. Confirm the new widget class has **exactly one** `Handle*Clicked` method in the Dumper-7 SDK. Multi-button widgets are out of scope by design — the existing single-button rule makes the hotkey semantics predictable.
2. Verify the class declaration is reachable from an `#include` already in `ModCore.cpp` (or add the relevant header + `_functions.cpp`).
3. Add a parallel `SafeInvokeX` helper — mirror `SafeInvokeClaim`'s shape: `ButtonClicked()` on the wrapper (if any) plus the direct handler call, both inside a single `__try`/`__except`.
4. Add a second `if (Obj->IsA(SDK::U<NewBase>::StaticClass())) { ... }` block inside the GObjects scan loop.
5. If target count reaches ~4 distinct base classes, refactor the scan loop into a `HotkeyTarget[]` table — see `ConfirmHotkeyMultiTargetPlan.md` for the pattern.

## Building from source

Requires Visual Studio 2022 with the v143 toolset and the [StarRupture-Plugin-SDK](https://github.com/AlienXAXS/StarRupture-Plugin-SDK) (with the `StarRupture SDK` submodule initialized).

This project's `.vcxproj` must live inside a solution that imports the SDK's `Shared.props` — typically you add it to `StarRupture-Plugin-SDK.sln`:

1. Open `StarRupture-Plugin-SDK.sln`.
2. Right-click the solution → Add → Existing Project → select `ConfirmHotkey\ConfirmHotkey.vcxproj`.
3. Build the `Client Release|x64` configuration.
4. Output: `bin\x64\Client Release\plugins\ConfirmHotkey.dll`.

The `Debug`/`Release` (generic) and `Server Debug`/`Server Release` configurations exist so the solution stays buildable in those modes, but this plugin does nothing useful outside `Client *`.

## Files

- `plugin.{h,cpp}` — the three ABI exports + `IPluginSelf` lifecycle + client-binary guard.
- `ModCore.{h,cpp}` — hotkey registration, GObjects scan, per-target invocation.
- `plugin_config.h` — INI schema + typed accessors (`ConfirmHotkeyConfig::Config`).
- `plugin_helpers.h` — `LOG_*` macros and `GetHooks/GetConfig/GetScanner` wrappers.
- `dllmain.cpp` — stock Windows DLL entry.

## Disclaimer

Use at your own risk. Learning project — no warranty, no support.
