#include "stdafx.h"
#include <ShlObj.h>	//Get access to %APPDATA%
#include <strsafe.h>

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
	ScanForPlugins();
}

ColumnMode::PluginManager::~PluginManager()
{
	for (Plugin& plugin : m_plugins)
	{
		plugin.OnShutdown();
	}
}

void ColumnMode::PluginManager::Init(ColumnModeCallbacks callbacks)
{
	m_pluginCallbacks = callbacks;
}

HRESULT ColumnMode::PluginManager::ScanForPlugins()
{
	m_availablePlugins.clear();
	std::filesystem::directory_iterator dirIter(m_modulesRootPath, std::filesystem::directory_options::none);
	for (auto& dir : dirIter)
	{
		if (dir.is_directory())
		{
			std::wstring dirStr = dir.path().filename().wstring();
			m_availablePlugins.push_back(std::move(dirStr));
		}
	}
	std::sort(m_availablePlugins.begin(), m_availablePlugins.end());

	return S_OK;
}

HRESULT ColumnMode::PluginManager::LoadPluginByName(LPCWSTR pluginName)
{
	std::filesystem::path path(m_modulesRootPath);
	path.append(pluginName)	//Plugins should be in a folder of the plugin name
		.append(pluginName)	//Plugin is a DLL file of the plugin name
		.replace_extension(L".dll");

	HMODULE pluginModule = LoadLibrary(path.c_str());
	if (pluginModule == NULL)
	{
		MessageBox(NULL, path.c_str(), L"Plugin Not Found", MB_OK | MB_ICONERROR);
		return E_INVALIDARG;
	}

	PFN_OPENCOLUMNMODEPLUGIN pfnOpenPlugin = 
		reinterpret_cast<PFN_OPENCOLUMNMODEPLUGIN>(GetProcAddress(pluginModule, "OpenColumnModePlugin"));

	PluginFunctions pluginFuncs = { 0 };
	OpenPluginArgs args;
	args.apiVersion = c_ColumnModePluginApiVersion;
	args.hPlugin = NULL;
	args.pPluginFuncs = &pluginFuncs;
	args.pCallbacks = &m_pluginCallbacks;

	HRESULT hr = (*pfnOpenPlugin)(&args);
	if (FAILED(hr))
	{
		MessageBox(NULL, pluginName, L"Failed to open plugin", MB_OK | MB_ICONERROR);
		return E_FAIL;
	}

	Plugin p(args.hPlugin, pluginModule, pluginFuncs, pluginName);
	if (FAILED(p.OnLoadCompleted()))
	{
		MessageBox(NULL, pluginName, L"OnLoadCompleted() failed", MB_OK | MB_ICONERROR);
	}
	m_plugins.push_back(std::move(p));

	return S_OK;
}

HRESULT ColumnMode::PluginManager::UnloadPlugin(Plugin* plugin)
{
	plugin->OnShutdown();
	if (!FreeLibrary(plugin->m_hPluginDll))
	{
		return E_FAIL;
	}
	m_plugins.remove(*plugin);
	return S_OK;
}

Plugin* ColumnMode::PluginManager::GetPluginByName(LPCWSTR pluginName)
{
	for (Plugin& p : m_plugins)
	{
		if (p.m_name.compare(pluginName) == 0)
		{
			return &p;
		}
	}
	return nullptr;
}

bool PluginManager::GetPlugin(LPCWSTR pluginName, Plugin** ppPlugin)
{
	assert(ppPlugin != nullptr);
	*ppPlugin = nullptr;
	for (Plugin& p : m_plugins)
	{
		if (p.m_name.compare(pluginName) == 0)
		{
			*ppPlugin = &p;
			return true;
		}
	}
	return false;
}

#define DEFINE_PLUGINMANAGER_FUNCTION_CALL_ALL(name, parameterList, parameterNames)\
void PluginManager::PF_##name##_ALL parameterList \
{\
	for (const Plugin& p : m_plugins) \
	{\
		if(p.m_pluginFuncs.pfn##name == nullptr) continue;\
		HRESULT hr;\
		try{hr = (*p.m_pluginFuncs.pfn##name)parameterNames;}\
		catch(...){hr = E_FAIL;}\
		if (FAILED(hr))\
		{\
			WCHAR msg[128];\
			swprintf_s(msg, L"Failed plugin call: %s::%s", p.m_name.c_str(), L#name);\
			OutputDebugString(msg);\
		}\
	}\
}

#include "PluginManagerFunctions.inl"
#undef DEFINE_PLUGINMANAGER_FUNCTION_CALL_ALL

//--------------------------------------------------------------------------------------------------------
//------------------------------------ Plugin Methods below this line ------------------------------------
//--------------------------------------------------------------------------------------------------------

#define DEFINE_PLUGIN_FUNCTION_CALL(name, parameterList, parameterNames) \
HRESULT Plugin::##name parameterList \
{\
	if(m_pluginFuncs.pfn##name == nullptr) { return S_FALSE; }\
	try{ return (*m_pluginFuncs.pfn##name)parameterNames;} \
	catch (...) {return E_FAIL;}\
}

#include "PluginFunctions.inl"
#undef DEFINE_PLUGIN_FUNCTION_CALL
