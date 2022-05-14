#include "stdafx.h"
#include <ShlObj.h>	//Get access to %APPDATA%

#include "PluginManager.h"
#include "Verify.h"

using namespace ColumnMode;

HRESULT ColumnMode::PluginManager::EnsureModulePathExists()
{
	PWSTR appdataRoaming = nullptr;
	HRESULT hr = SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, NULL /*currentUser*/, &appdataRoaming);
	VerifyHR(hr);

	m_modulesRootPath.assign(appdataRoaming)
		.append(L"ColumnMode")			//Probably best to load from Resources, but need a HINSTANCE
		.append(L"Plugins");

	std::filesystem::create_directories(m_modulesRootPath);

	CoTaskMemFree(appdataRoaming);
	return hr;
}

ColumnMode::PluginManager::PluginManager()
{
	EnsureModulePathExists();
}

HRESULT ColumnMode::PluginManager::LoadPlugin(LPCWSTR pluginName)
{
	std::filesystem::path path(m_modulesRootPath);
	path.append(pluginName)	//Plugins should be in a folder of the plugin name
		.append(pluginName)	//Plugin is a DLL file of the plugin name
		.replace_extension(L".dll");
	//MessageBox(NULL, path.c_str(), L"LoadPlugin()", MB_OK);

	HMODULE pluginModule = LoadLibrary(path.c_str());
	if (pluginModule == NULL)
	{
		MessageBox(NULL, path.c_str(), L"Plugin Not Found", MB_OK | MB_ICONERROR);
		return E_INVALIDARG;
	}



	return E_NOTIMPL;
}
