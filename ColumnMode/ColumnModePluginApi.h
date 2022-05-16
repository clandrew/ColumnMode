#pragma once

namespace ColumnMode
{
	constexpr UINT c_ColumnModePluginApiVersion = 1;

#pragma region ColumnModeCallbacks

	
	typedef HRESULT(APIENTRY* PFN_CB_OPENWINDOW)(_In_ LPCWSTR);

	struct ColumnModeCallbacks
	{
		PFN_CB_OPENWINDOW pfnMessagePopup;
	};

#pragma endregion

#pragma region PluginFunctions

	//File operations
	typedef HRESULT(APIENTRY* PFN_PF_ONSAVE)(LPCWSTR);
	typedef HRESULT(APIENTRY* PFN_PF_ONSAVEAS)(LPCWSTR);

	//Plugin Life cycle
	typedef HRESULT(APIENTRY* PFN_PF_ONLOADCOMPLETED)(HANDLE);	//Called after OpenColumnModePlugin
	typedef HRESULT(APIENTRY* PFN_PF_ONSHUTDOWN)(HANDLE);		//Called before the plugin is unloaded

	struct PluginFunctions
	{
		PFN_PF_ONSAVE pfnOnSave;
		PFN_PF_ONSAVEAS pfnOnSaveAs;

		PFN_PF_ONLOADCOMPLETED pfnOnLoadCompleted;
		PFN_PF_ONSHUTDOWN pfnOnShutdown;
	};

#pragma endregion

	struct OpenPluginArgs
	{
		_In_ UINT apiVersion;
		_In_ ColumnModeCallbacks* pCallbacks;	// Contains function pointers to talk to ColumnMode
		_Out_ PluginFunctions* pPluginFuncs;	// Fill in the entries that you want ColumnMode to tell you about
		_Out_ HANDLE hPlugin;
	};
}


extern "C" HRESULT WINAPI OpenColumnModePlugin(_Inout_ ColumnMode::OpenPluginArgs* args);
typedef HRESULT(WINAPI* PFN_OPENCOLUMNMODEPLUGIN)(_Inout_ ColumnMode::OpenPluginArgs* args);