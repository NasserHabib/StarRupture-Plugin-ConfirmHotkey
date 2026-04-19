#include "ModCore.h"
#include "plugin_helpers.h"
#include "plugin_config.h"

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

// SEH-wrapped click invocation — HandleOnRecycleButtonClicked hits UE reflection
// (ProcessEvent via Class->GetFunction), so a widget that's mid-teardown can
// null-deref. Same defensive idiom as KeepTicking's SafeGetActorLocation.
static bool SafeInvokeRecycle(SDK::UCrUW_RecyclingStatus* widget)
{
    __try { widget->HandleOnRecycleButtonClicked(); return true; }
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

    const char* keyName = RecyclerHotkeyConfig::Config::GetRecycleHotkey();
    strncpy_s(s_keyName, sizeof(s_keyName), keyName, _TRUNCATE);

    LOG_INFO("ModCore: Registering hotkey '%s' (Pressed)", s_keyName);
    self->hooks->Input->RegisterKeybindByName(s_keyName, EModKeyEvent::Pressed, &OnRecycleHotkey);
    LOG_INFO("ModCore: Hotkey registered successfully.");

    return true;
}

void ModCore::Shutdown()
{
    LOG_INFO("ModCore: Shutting down RecyclerHotkey...");
    if (s_self && s_self->hooks && s_self->hooks->Input && s_keyName[0] != '\0')
    {
        s_self->hooks->Input->UnregisterKeybindByName(s_keyName, EModKeyEvent::Pressed, &OnRecycleHotkey);
    }
    s_self = nullptr;
    s_keyName[0] = '\0';
}

void ModCore::OnRecycleHotkey(EModKey /*key*/, EModKeyEvent event)
{
    if (event != EModKeyEvent::Pressed) return;
    if (!SDK::UObject::GObjects) return;   // early-press guard: pre-engine-init keypress

    LOG_TRACE("ModCore: Hotkey triggered, searching for Recycler UI...");

    const int count = SDK::UObject::GObjects->Num();
    for (int i = 0; i < count; ++i)
    {
        SDK::UObject* Obj = SDK::UObject::GObjects->GetByIndex(i);
        if (!Obj || Obj->IsDefaultObject()) continue;
        if (!Obj->IsA(SDK::UCrUW_RecyclingStatus::StaticClass())) continue;

        auto* RecyclerUI = static_cast<SDK::UCrUW_RecyclingStatus*>(Obj);
        if (!RecyclerUI->IsInViewport()) continue;

        if (SafeInvokeRecycle(RecyclerUI))
            LOG_INFO("ModCore: Recycle confirmed via hotkey '%s'", s_keyName);
        else
            LOG_ERROR("ModCore: HandleOnRecycleButtonClicked crashed (widget %p)", static_cast<void*>(RecyclerUI));
        return;
    }

    LOG_DEBUG("ModCore: No active Recycler UI in viewport.");
}

#else

bool ModCore::Initialize(IPluginSelf* /*self*/)
{
    LOG_WARN("ModCore: RecyclerHotkey is a client-side plugin and will not run on this build.");
    return true;
}

void ModCore::Shutdown() {}

void ModCore::OnRecycleHotkey(EModKey /*key*/, EModKeyEvent /*event*/) {}

#endif
