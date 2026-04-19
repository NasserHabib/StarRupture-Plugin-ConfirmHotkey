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

static bool SafeInvokeClaim(SDK::UCrUW_Analyzer* widget)
{
    __try { widget->HandleClaimClicked(); return true; }
    __except (EXCEPTION_EXECUTE_HANDLER) { return false; }
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

    return true;
}

void ModCore::Shutdown()
{
    LOG_INFO("ModCore: Shutting down RecyclerHotkey...");
    if (s_self && s_self->hooks && s_self->hooks->Input && s_keyName[0] != '\0')
    {
        s_self->hooks->Input->UnregisterKeybindByName(s_keyName, EModKeyEvent::Pressed, &OnConfirmHotkey);
    }
    s_self = nullptr;
    s_keyName[0] = '\0';
}

void ModCore::OnConfirmHotkey(EModKey /*key*/, EModKeyEvent event)
{
    if (event != EModKeyEvent::Pressed) return;
    if (!SDK::UObject::GObjects) return;   // early-press guard: pre-engine-init keypress

    const int count = SDK::UObject::GObjects->Num();
    for (int i = 0; i < count; ++i)
    {
        SDK::UObject* Obj = SDK::UObject::GObjects->GetByIndex(i);
        if (!Obj || Obj->IsDefaultObject()) continue;

        // Target #1: Recycler's RECYCLE button.
        if (Obj->IsA(SDK::UCrUW_RecyclingStatus::StaticClass()))
        {
            auto* ui = static_cast<SDK::UCrUW_RecyclingStatus*>(Obj);
            // Not in viewport — skip to next object. Safe to `continue` here
            // because Recycler and Analyzer are disjoint hierarchies; this
            // object can't match the Analyzer check below.
            if (!ui->IsInViewport()) continue;

            if (SafeInvokeRecycle(ui))
                LOG_INFO("ModCore: Confirmed action on Recycler via hotkey '%s'", s_keyName);
            else
                LOG_ERROR("ModCore: HandleOnRecycleButtonClicked crashed (widget %p)", static_cast<void*>(ui));
            return;
        }

        // Target #2: Analyzing Station's CLAIM button.
        if (Obj->IsA(SDK::UCrUW_Analyzer::StaticClass()))
        {
            auto* ui = static_cast<SDK::UCrUW_Analyzer*>(Obj);
            if (!ui->IsInViewport()) continue;

            if (SafeInvokeClaim(ui))
                LOG_INFO("ModCore: Confirmed action on Analyzer via hotkey '%s'", s_keyName);
            else
                LOG_ERROR("ModCore: HandleClaimClicked crashed (widget %p)", static_cast<void*>(ui));
            return;
        }
    }

    LOG_INFO("ModCore: Hotkey '%s' pressed but no targeted widget (Recycler/Analyzer) in viewport.", s_keyName);
}

#else

bool ModCore::Initialize(IPluginSelf* /*self*/)
{
    LOG_WARN("ModCore: RecyclerHotkey is a client-side plugin and will not run on this build.");
    return true;
}

void ModCore::Shutdown() {}

void ModCore::OnConfirmHotkey(EModKey /*key*/, EModKeyEvent /*event*/) {}

#endif
