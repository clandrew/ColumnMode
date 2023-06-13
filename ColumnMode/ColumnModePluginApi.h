#pragma once

#include "ColumnModeTypes.h"

namespace ColumnMode
{
	constexpr UINT c_ColumnModePluginApiVersion = 2;

#pragma region ColumnModeCallbacks

	//windowing
	typedef ATOM(APIENTRY* PFN_CB_REGISTERWINDOWCLASS)(_In_ WNDCLASS);
	typedef ATOM(APIENTRY* PFN_CB_REGISTERWINDOWCLASSEX)(_In_ WNDCLASSEX);
	typedef HRESULT(APIENTRY* PFN_CB_OPENWINDOW)(_In_ CreateWindowArgs, _Inout_ HWND* pHwnd);

	//Column Mode Settings
	typedef HRESULT(APIENTRY* PFN_CB_RECOMMEND_EDIT_MODE)(HANDLE hPlugin, EDIT_MODE);

	struct ColumnModeCallbacks
	{
		PFN_CB_REGISTERWINDOWCLASS pfnRegisterWindowClass;
		PFN_CB_REGISTERWINDOWCLASSEX pfnRegisterWindowClassEx;
		PFN_CB_OPENWINDOW pfnOpenWindow;
		PFN_CB_RECOMMEND_EDIT_MODE pfnRecommendEditMode;
	};

#pragma endregion

#pragma region PluginFunctions

	//File operations
	typedef HRESULT(APIENTRY* PFN_PF_ONOPEN)(HANDLE, LPCWSTR);
	typedef HRESULT(APIENTRY* PFN_PF_ONSAVE)(HANDLE, LPCWSTR);
	typedef HRESULT(APIENTRY* PFN_PF_ONSAVEAS)(HANDLE, LPCWSTR);
	typedef HRESULT(APIENTRY* PFN_PF_ONTYPINGCOMPLETE)(HANDLE, const size_t numChars, const WCHAR* pAllText);

	//Plugin Life cycle
	typedef HRESULT(APIENTRY* PFN_PF_ONLOADCOMPLETED)(HANDLE);	//Called after OpenColumnModePlugin
	typedef HRESULT(APIENTRY* PFN_PF_ONSHUTDOWN)(HANDLE);		//Called before the plugin is unloaded

	struct PluginFunctions
	{
		PFN_PF_ONOPEN pfnOnOpen;
		PFN_PF_ONSAVE pfnOnSave;
		PFN_PF_ONSAVEAS pfnOnSaveAs;

		PFN_PF_ONLOADCOMPLETED pfnOnLoadCompleted;
		PFN_PF_ONSHUTDOWN pfnOnShutdown;

		// API version >= 2
		PFN_PF_ONTYPINGCOMPLETE pfnOnTypingComplete;
	};

#pragma endregion

	struct PluginDependency
	{
		UINT length; // number of WCHARs in pName
		WCHAR* pName;
	};

	struct OpenPluginArgs
	{
		_In_ UINT apiVersion;
		_In_ ColumnModeCallbacks* pCallbacks;	// Contains function pointers to talk to ColumnMode
		_Out_ PluginFunctions* pPluginFuncs;	// Fill in the entries that you want ColumnMode to tell you about
		_Out_ HANDLE hPlugin;
	};
}

/* 
Safe to not export if your plugin doesn't have run-time dll dependencies.
Called in the following pattern:
1. pCount is a valid pointer to any UINT and pDependencies is nullptr. - Plugin should set pCount to the number of dependencies.
2. pCount is a valid pointer, pDependencies is a valid pointer, each dependency's pName is nullptr. - Plugin should validate pCount is the right size, then populate the length fields of each dependency struct.
3. pCount is a valid pointer, pDependencies is a valid pointer, each dependency's pName is a valid pointer - Plugin should validate pCount, then foreach dependency: validate the length and then write the dependency name to the buffer (including extension).

Return value from each step should be S_OK if everything is valid and fields are being written as expected. If pCount or a length value is wrong, return E_INVALIDARG. Else, return E_FAIL.

Note that depenency dlls should be in the same directory as your plugin: %APPDATA%/ColumnMode/Plugins/your_plugin_name/
*/
extern "C" HRESULT WINAPI QueryColumnModePluginDependencies(_Inout_ UINT* pCount, _Inout_opt_count_(*pCount)   ColumnMode::PluginDependency* pDependencies);
typedef HRESULT(WINAPI* PFN_QUERYCOLUMNMODEPLUGINDEPENDENCIES)(_Inout_ UINT* pCount, _Inout_opt_count_(*pCount) ColumnMode::PluginDependency* pDependencies);

// Required export
extern "C" HRESULT WINAPI OpenColumnModePlugin(_Inout_ ColumnMode::OpenPluginArgs* args);
typedef HRESULT(WINAPI* PFN_OPENCOLUMNMODEPLUGIN)(_Inout_ ColumnMode::OpenPluginArgs* args);