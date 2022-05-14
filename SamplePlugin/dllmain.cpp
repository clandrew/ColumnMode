// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

constexpr LPCWSTR g_pluginName = L"SamplePlugin";

HRESULT APIENTRY OnSave(LPCWSTR filepath)
{
    MessageBox(NULL, filepath, g_pluginName, MB_OK);

    return E_FAIL;
}

HRESULT WINAPI OpenColumnModePlugin(_Inout_ ColumnMode::OpenPluginArgs* args)
{
    args->pPluginFuncs->pfnOnSave = OnSave;
    MessageBox(NULL, L"OpenColumnModePlugin() Called", g_pluginName, MB_OK);
    return S_OK;
}

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}

