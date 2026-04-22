#include "ModCore.h"
#include "plugin_helpers.h"
#include "plugin_config.h"

#include <cctype>
#include <cstring>
#include <string>

#if defined(MODLOADER_CLIENT_BUILD)
#include "Engine_classes.hpp"
#include "ChimeraUI_classes.hpp"
#include "UMG_functions.cpp"
#include "ChimeraUI_functions.cpp"
#endif

IPluginSelf* ModCore::s_self        = nullptr;
char         ModCore::s_keyName[64] = {0};

#if defined(MODLOADER_CLIENT_BUILD)

// StarRupture uses UCrUW_Analyzer as a generic "confirm primary action" base
// for multiple single-button interior UIs. Each building's UI is a Blueprint
// subclass (WBP_Recycler_C, WBP_Analyzer_C, …) that inherits from it and
// wires ClaimButton + HandleClaimClicked to its own action. Live diagnostic
// confirmed the SDK's UCrUW_RecyclingStatus class is not instantiated in
// practice, so we don't match on it — the Analyzer IsA check catches
// everything we care about.
//
// The wrapper's ButtonClicked() UFunction handles the presentation layer
// (sound via DA_SoundsTable, any press animation) but does NOT fan out to
// HandleClaimClicked on the parent widget — that wiring goes through a
// separate OnClicked path in the BP. So we call both: ButtonClicked for the
// feedback, then HandleClaimClicked for the gameplay effect. Order matters
// for perceived responsiveness (sound starts before the RPC round-trip).
// SEH-wrapped because the Handle* methods hit UE reflection (ProcessEvent
// via Class->GetFunction), so a widget that's mid-teardown can null-deref —
// same defensive idiom as KeepTicking's SafeGetActorLocation.
static bool SafeInvokeClaim(SDK::UCrUW_Analyzer* widget)
{
    __try
    {
        if (widget->ClaimButton)
            widget->ClaimButton->ButtonClicked();   // sound + animation
        widget->HandleClaimClicked();                // gameplay effect (server RPC)
        return true;
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// Mirrors the visible gray-out of the RECYCLE/CLAIM button: returns false
// when the BP has disabled the ActionButton wrapper's underlying UButton.
// The usual cause is "no staged item", but this also naturally covers any
// other disabled-state the game may add (cooldowns, full output inventory).
// Reading GetIsEnabled hits ProcessEvent, so SEH-wrap on the same grounds
// as SafeInvokeClaim.
static bool SafeCanClaim(SDK::UCrUW_Analyzer* widget)
{
    __try
    {
        if (!widget->ClaimButton) return false;
        if (!widget->ClaimButton->OverlayButton) return false;
        return widget->ClaimButton->OverlayButton->GetIsEnabled();
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

bool ModCore::Initialize(IPluginSelf* self)
{
    LOG_INFO("ModCore: Initializing ConfirmHotkey...");
    s_self = self;

    if (!self->hooks->Input)
    {
        LOG_ERROR("ModCore: Input interface not available!");
        return false;
    }

    const char* keyName = ConfirmHotkeyConfig::Config::GetConfirmHotkey();
    strncpy_s(s_keyName, sizeof(s_keyName), keyName, _TRUNCATE);

    // Loader's keybind registry is case-sensitive and the EModKey enum uses
    // uppercase names (A-Z, F1-F12, SPACE, etc.). Normalize so users can
    // write the key in any case in the INI without silent registration failure.
    for (char* p = s_keyName; *p; ++p)
        *p = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));

    LOG_INFO("ModCore: Registering confirm hotkey '%s' (Pressed)", s_keyName);
    self->hooks->Input->RegisterKeybindByName(s_keyName, EModKeyEvent::Pressed, &OnConfirmHotkey);
    LOG_INFO("ModCore: Hotkey registered successfully.");

    // Subscribe to config-change notifications so the hotkey can be rebound
    // from the in-game config menu (or a manual INI edit the loader detects)
    // without requiring a plugin reload.
    if (self->hooks->UI)
    {
        self->hooks->UI->RegisterOnConfigChanged(&OnConfigChanged);
        LOG_INFO("ModCore: Config-change callback registered (hotkey hot-reload enabled).");
    }

    return true;
}

void ModCore::Shutdown()
{
    LOG_INFO("ModCore: Shutting down ConfirmHotkey...");
    if (s_self && s_self->hooks)
    {
        if (s_self->hooks->Input && s_keyName[0] != '\0')
            s_self->hooks->Input->UnregisterKeybindByName(s_keyName, EModKeyEvent::Pressed, &OnConfirmHotkey);
        if (s_self->hooks->UI)
            s_self->hooks->UI->UnregisterOnConfigChanged(&OnConfigChanged);
    }
    s_self = nullptr;
    s_keyName[0] = '\0';
}

// Fires for any config change across the whole modloader (other plugins' too).
// Filter to our specific section+key before doing any work.
void ModCore::OnConfigChanged(const char* section, const char* key, const char* newValue)
{
    if (!section || !key) return;
    if (std::strcmp(section, "PluginSettings") != 0) return;
    if (std::strcmp(key, "ConfirmHotkey") != 0) return;
    if (!s_self || !s_self->hooks || !s_self->hooks->Input) return;

    char newKey[64] = {};
    strncpy_s(newKey, sizeof(newKey), (newValue && *newValue) ? newValue : "E", _TRUNCATE);
    for (char* p = newKey; *p; ++p)
        *p = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));

    if (std::strcmp(newKey, s_keyName) == 0)
        return;  // Same key after normalization — nothing to do.

    LOG_INFO("ModCore: Hotkey config changed from '%s' to '%s' — re-registering.", s_keyName, newKey);
    if (s_keyName[0] != '\0')
        s_self->hooks->Input->UnregisterKeybindByName(s_keyName, EModKeyEvent::Pressed, &OnConfirmHotkey);

    std::memcpy(s_keyName, newKey, sizeof(s_keyName));
    s_self->hooks->Input->RegisterKeybindByName(s_keyName, EModKeyEvent::Pressed, &OnConfirmHotkey);
    LOG_INFO("ModCore: Re-registered confirm hotkey as '%s'.", s_keyName);
}

void ModCore::OnConfirmHotkey(EModKey /*key*/, EModKeyEvent event)
{
    if (event != EModKeyEvent::Pressed) return;
    if (!SDK::UObject::GObjects) return;   // early-press guard: pre-engine-init keypress

    // Count non-CDO UCrUW_Analyzer-derived instances regardless of visibility.
    // Lets us distinguish "class never instanced" from "instanced but filtered
    // out by visibility check" in the no-match log line.
    int targetTotal = 0;

    const int count = SDK::UObject::GObjects->Num();
    for (int i = 0; i < count; ++i)
    {
        SDK::UObject* Obj = SDK::UObject::GObjects->GetByIndex(i);
        if (!Obj || Obj->IsDefaultObject()) continue;
        if (!Obj->IsA(SDK::UCrUW_Analyzer::StaticClass())) continue;

        auto* ui = static_cast<SDK::UCrUW_Analyzer*>(Obj);
        ++targetTotal;

        // Capture the actual BP class name (e.g. "WBP_Recycler_C",
        // "WBP_Analyzer_C") so the success log identifies which building fired.
        std::string className = ui->Class ? ui->Class->GetName() : std::string("UCrUW_Analyzer");
        LOG_DEBUG("ModCore: [diag] IsA match — class='%s' object='%s'",
                  className.c_str(), ui->GetFullName().c_str());

        // Use IsVisible (own-visibility enum) rather than IsInViewport: nested
        // sub-widgets inside a container never have the viewport flag set even
        // when they're displayed on screen.
        if (!ui->IsVisible()) continue;

        // Respect the UI's own disabled-state rather than reimplementing
        // "is anything staged?" — by reading the exact bit that drives the
        // visible gray-out, we stay correct across every reason the game
        // might disable the button, known or future.
        if (!SafeCanClaim(ui))
        {
            LOG_INFO("ModCore: Hotkey '%s' pressed on %s but ClaimButton is disabled — nothing to claim.",
                     s_keyName, className.c_str());
            return;
        }

        if (SafeInvokeClaim(ui))
            LOG_INFO("ModCore: Confirmed action on %s via hotkey '%s'",
                     className.c_str(), s_keyName);
        else
            LOG_ERROR("ModCore: ClaimButton click crashed on %s (widget %p)",
                      className.c_str(), static_cast<void*>(ui));
        return;
    }

    LOG_INFO("ModCore: Hotkey '%s' pressed — no visible target. GObjects contains %d UCrUW_Analyzer-derived instance(s) (none passed IsVisible).",
             s_keyName, targetTotal);
}

#endif
