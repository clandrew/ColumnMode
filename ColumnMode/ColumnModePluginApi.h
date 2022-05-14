#pragma once

namespace ColumnMode
{
	constexpr UINT c_ColumnModePluginApiVersion = 1;

#pragma region ColumnModeCallbacks

	typedef HRESULT(APIENTRY* PFN_CB_MESSAGEPOPUP)(_In_ LPCWSTR);

	struct ColumnModeCallbacks
	{
		PFN_CB_MESSAGEPOPUP pfnMessagePopup;
	};

#pragma endregion

#pragma region PluginFunctions

	typedef HRESULT(APIENTRY* PFN_PF_ONSELECTION)();

	struct PluginFunctions
	{
		PFN_PF_ONSELECTION pfnOnSelection;
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