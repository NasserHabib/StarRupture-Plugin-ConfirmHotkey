#include "ModCore.h"
#include "plugin_helpers.h"
#include "plugin_config.h"

#include <cctype>
#include <cstring>

#if defined(MODLOADER_CLIENT_BUILD)
#include "Engine_classes.hpp"
#include "ChimeraUI_classes.hpp"
#include "UMG_functions.cpp"
#include "ChimeraUI_functions.cpp"
#endif

IPluginSelf* ModCore::s_self        = nullptr;
char         ModCore::s_keyName[64] = {0};

#if defined(MODLOADER_CLIENT_BUILD)

// SEH-wrapped click invocations — the Handle*Clicked methods hit UE reflection
// (ProcessEvent via Class->GetFunction), so a widget that's mid-teardown can
// null-deref. Same defensive idiom as KeepTicking's SafeGetActorLocation.
static bool SafeInvokeRecycle(SDK::UCrUW_RecyclingStatus* widget)
{
    __try { widget->HandleOnRecycleButtonClicked(); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
}

// The Analyzer's ClaimButton is a UCrUW_ActionButton wrapper. In practice its
// ButtonClicked() UFunction handles the presentation layer (sound via
// DA_SoundsTable, any press animation) but does NOT fan out to
// HandleClaimClicked on the parent widget — that wiring goes through a
// separate OnClicked path in the BP. So we call both: ButtonClicked for the
// feedback, then HandleClaimClicked for the gameplay effect. Order matters
// for perceived responsiveness (sound starts before the RPC round-trip).
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

// The Recycler's RecycleButton is a raw UButton — its click sound lives in
// WidgetStyle and is played by Slate on real input events, not by any
// OnClicked listener. Since we bypass Slate, manually play it via
// UGameplayStatics::PlaySound2D. Prefer ClickedSlateSound, fall back to
// PressedSlateSound. Silent no-op if neither is configured or the asset is
// missing — users still get the gameplay effect from HandleOn...Clicked.
static void SafePlayButtonClickSound(SDK::UButton* button)
{
    __try
    {
        if (!button) return;
        SDK::UObject* resource = button->WidgetStyle.ClickedSlateSound.ResourceObject;
        if (!resource) resource = button->WidgetStyle.PressedSlateSound.ResourceObject;
        if (!resource) return;

        SDK::UGameplayStatics::PlaySound2D(
            button,                                      // WorldContextObject
            static_cast<SDK::USoundBase*>(resource),     // Sound
            1.0f,                                        // VolumeMultiplier
            1.0f,                                        // PitchMultiplier
            0.0f,                                        // StartTime
            nullptr,                                     // ConcurrencySettings
            nullptr,                                     // OwningActor
            true);                                       // bIsUISound
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { /* silent; don't spam the log on every keypress */ }
}

bool ModCore::Initialize(IPluginSelf* self)
{
    LOG_INFO("ModCore: Initializing RecyclerHotkey...");
    s_self = self;

    if (!self->hooks->Input)
    {
        LOG_ERROR("ModCore: Input interface not available!");
        return false;
    }

    const char* keyName = RecyclerHotkeyConfig::Config::GetConfirmHotkey();
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
    LOG_INFO("ModCore: Shutting down RecyclerHotkey...");
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
    strncpy_s(newKey, sizeof(newKey), (newValue && *newValue) ? newValue : "R", _TRUNCATE);
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

    // Count non-CDO instances of each target class regardless of visibility.
    // Lets us distinguish "class never instanced" from "instanced but filtered
    // out by visibility check" in the log output.
    int recyclerTotal = 0;
    int analyzerTotal = 0;

    const int count = SDK::UObject::GObjects->Num();
    for (int i = 0; i < count; ++i)
    {
        SDK::UObject* Obj = SDK::UObject::GObjects->GetByIndex(i);
        if (!Obj || Obj->IsDefaultObject()) continue;

        // Target #1: Recycler's RECYCLE button.
        if (Obj->IsA(SDK::UCrUW_RecyclingStatus::StaticClass()))
        {
            auto* ui = static_cast<SDK::UCrUW_RecyclingStatus*>(Obj);
            ++recyclerTotal;
            // Use IsVisible (own-visibility enum) rather than IsInViewport:
            // nested sub-widgets inside a container never have the viewport
            // flag set even when they're displayed on screen.
            if (!ui->IsVisible()) continue;

            if (SafeInvokeRecycle(ui))
            {
                LOG_INFO("ModCore: Confirmed action on Recycler via hotkey '%s'", s_keyName);
                SafePlayButtonClickSound(ui->RecycleButton);
            }
            else
                LOG_ERROR("ModCore: HandleOnRecycleButtonClicked crashed (widget %p)", static_cast<void*>(ui));
            return;
        }

        // Target #2: Analyzing Station's CLAIM button.
        if (Obj->IsA(SDK::UCrUW_Analyzer::StaticClass()))
        {
            auto* ui = static_cast<SDK::UCrUW_Analyzer*>(Obj);
            ++analyzerTotal;
            if (!ui->IsVisible()) continue;

            // SafeInvokeClaim calls ClaimButton->ButtonClicked() for the
            // sound+animation AND HandleClaimClicked() for the gameplay
            // effect — both are needed, they live on separate BP paths.
            if (SafeInvokeClaim(ui))
                LOG_INFO("ModCore: Confirmed action on Analyzer via hotkey '%s'", s_keyName);
            else
                LOG_ERROR("ModCore: ClaimButton click crashed (widget %p)", static_cast<void*>(ui));
            return;
        }
    }

    LOG_INFO("ModCore: Hotkey '%s' pressed — no visible target. GObjects contains: Recycler=%d, Analyzer=%d instance(s) (none passed IsVisible).",
             s_keyName, recyclerTotal, analyzerTotal);
}

#else

bool ModCore::Initialize(IPluginSelf* /*self*/)
{
    LOG_WARN("ModCore: RecyclerHotkey is a client-side plugin and will not run on this build.");
    return true;
}

void ModCore::Shutdown() {}

void ModCore::OnConfirmHotkey(EModKey /*key*/, EModKeyEvent /*event*/) {}

void ModCore::OnConfigChanged(const char* /*section*/, const char* /*key*/, const char* /*newValue*/) {}

#endif
