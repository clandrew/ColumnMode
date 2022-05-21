// dllmain.cpp : Defines the entry point for the DLL application.
#include "pch.h"

constexpr LPCWSTR g_pluginName = L"SamplePlugin";

ColumnMode::ColumnModeCallbacks g_callbacks;
ATOM g_windowClassAtom = 0;
HWND g_hwnd;

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
    switch (message)
    {
    case WM_LBUTTONUP: MessageBox(NULL, L"Clicked in SamplePlugin's Window!", L"SamplePlugin", MB_OK); break;
    }
    return DefWindowProc(hWnd, message, wParam, lParam);
}

HRESULT APIENTRY OnSave(HANDLE, LPCWSTR filepath)
{
    MessageBox(NULL, filepath, g_pluginName, MB_OK);

    return E_FAIL;
}

HRESULT APIENTRY OnLoadCompleted(HANDLE)
{
    WNDCLASS windowClass{};
    windowClass.lpfnWndProc = WndProc;
    windowClass.lpszClassName = L"SamplePluginWindowClass";
    g_windowClassAtom = (*g_callbacks.pfnRegisterWindowClass)(windowClass);

    if (g_windowClassAtom == 0)
    {
        return E_FAIL;
    }

    ColumnMode::CreateWindowArgs args{};
    args.exWindowStyle = 0;
    args.windowClass = g_windowClassAtom;
    args.windowName = L"SamplePlugin - Window";
    args.height = 500;
    args.width = 500;
    if (SUCCEEDED((*g_callbacks.pfnOpenWindow)(args, &g_hwnd)))
    {
        ShowWindow(g_hwnd, 1);
    }
    return S_OK;
}

HRESULT WINAPI OpenColumnModePlugin(_Inout_ ColumnMode::OpenPluginArgs* args)
{
    args->pPluginFuncs->pfnOnSave = OnSave;
    args->pPluginFuncs->pfnOnLoadCompleted = OnLoadCompleted;
    memcpy(&g_callbacks, args->pCallbacks, sizeof(ColumnMode::ColumnModeCallbacks));
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

