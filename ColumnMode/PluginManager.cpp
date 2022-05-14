#include "stdafx.h"
#include "PluginManager.h"
#include "Verify.h"
#include <ShlObj.h>	//Get access to %APPDATA%

using namespace ColumnMode;

HRESULT ColumnMode::PluginManager::EnsureModulePathExists()
{
	PWSTR appdataRoaming = nullptr;
	HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, NULL /*currentUser*/, &appdataRoaming);
	VerifyHR(hr);

	MessageBox(NULL, appdataRoaming, L"%APPDATA% filepath", MB_OK);

	CoTaskMemFree(appdataRoaming);
	return hr;
}

ColumnMode::PluginManager::PluginManager()
{
	EnsureModulePathExists();
}

HRESULT ColumnMode::PluginManager::LoadPlugin(LPCWSTR pluginName)
{
	return E_NOTIMPL;
}
