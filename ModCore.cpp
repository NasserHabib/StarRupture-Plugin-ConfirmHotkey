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

    // Defensive uppercase pass for hand-edited INIs. The in-game keybind
    // picker (ConfigValueType::Keybind) writes canonical strings, and the
    // loader's combo parser is case-insensitive for modifier tokens
    // (Ctrl/Shift/Alt) — but the base-key matcher's case sensitivity is
    // not contractually documented, so we normalize letters/digits to be
    // safe. Harmless for combos: "Shift+ALT+e" -> "SHIFT+ALT+E" still
    // resolves correctly.
    for (char* p = s_keyName; *p; ++p)
        *p = static_cast<char>(std::toupper(static_cast<unsigned char>(*p)));

    LOG_INFO("ModCore: Registering confirm hotkey '%s' (Pressed)", s_keyName);
    self->hooks->Input->RegisterKeybindByName(s_keyName, EModKeyEvent::Pressed, &OnConfirmHotkey);
    LOG_INFO("ModCore: Hotkey registered successfully.");

    // The modloader auto re-registers RegisterKeybindByName entries when
    // the user rebinds the key in the in-game config UI (interface v30+),
    // so no manual OnConfigChanged subscription is needed.

    return true;
}

void ModCore::Shutdown()
{
    LOG_INFO("ModCore: Shutting down ConfirmHotkey...");
    if (s_self && s_self->hooks)
    {
        if (s_self->hooks->Input && s_keyName[0] != '\0')
            s_self->hooks->Input->UnregisterKeybindByName(s_keyName, EModKeyEvent::Pressed, &OnConfirmHotkey);
    }
    s_self = nullptr;
    s_keyName[0] = '\0';
}

// ponytail: the loader fires a bare-name bind ("E") under ANY held modifier and
// still passes the key to the game, so bare E also fires on the game's native
// Shift+E. Suppress when a modifier is held but the user did not ask for one.
// Read the key from config at press time: F2 rebinds rewrite config and
// re-register without notifying us, so s_keyName can be stale. Ceiling:
// bare-key users lose Ctrl/Alt/Shift+<key> as triggers -- configure the combo
// if you want one. Delete if the loader ever grows a strict-modifier option.
static bool ModifierHeldButNotConfigured()
{
    std::string k = ConfirmHotkeyConfig::Config::GetConfirmHotkey();
    for (char& c : k) c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    const bool wantsMod = k.find("CTRL")  != std::string::npos || k.find("CONTROL") != std::string::npos
                       || k.find("SHIFT") != std::string::npos || k.find("ALT")     != std::string::npos;
    if (wantsMod) return false;
    return ((GetAsyncKeyState(VK_CONTROL) | GetAsyncKeyState(VK_SHIFT) | GetAsyncKeyState(VK_MENU)) & 0x8000) != 0;
}

void ModCore::OnConfirmHotkey(EModKey /*key*/, EModKeyEvent event)
{
    if (event != EModKeyEvent::Pressed) return;
    if (!SDK::UObject::GObjects) return;   // early-press guard: pre-engine-init keypress
    if (ModifierHeldButNotConfigured()) return;   // bare key + modifier held: let the game have it

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
