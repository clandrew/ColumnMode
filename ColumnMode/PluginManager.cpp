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

HRESULT LoadLibraryHelper(std::filesystem::path path, HMODULE& out_pluginModule)
{
	DWORD flags = LOAD_LIBRARY_SEARCH_DEFAULT_DIRS | LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR;
	out_pluginModule = LoadLibraryEx(path.c_str(), NULL, flags);
	if (out_pluginModule == NULL)
	{
		DWORD err = GetLastError();
		WCHAR buff[1024];
		std::swprintf(buff, 1024, _T("Plugin not found or DLL failed to load.\nError code: %d\nPath: %s"), err, path.c_str());
		MessageBox(NULL, buff, L"Error loading plugin DLL", MB_OK | MB_ICONERROR);

		return E_INVALIDARG;
	}
	return S_OK;
}

HRESULT LoadPluginDependenciesHelper(PFN_QUERYCOLUMNMODEPLUGINDEPENDENCIES pfnQueryDeps, std::filesystem::path pluginDir, HMODULE pluginModule)
{
	UINT numDeps = 0;
	std::wstring pluginName = pluginDir.filename(); //plugin name is the last part of the pluginDir

	//query num dependencies and create dependency list
	HRESULT hr = pfnQueryDeps(&numDeps, nullptr);
	if(FAILED(hr))
	{
		MessageBoxHelper_FormattedBody(MB_ICONERROR | MB_OK, L"Failed to get dependencies", L"%s returned failure code %d when calling QueryColumnModePluginDependencies (1st call).", pluginName, hr);
		return hr;
	}
	ColumnMode::PluginDependency defaultInit{ 0, nullptr };
	std::vector<ColumnMode::PluginDependency> deps = std::vector<ColumnMode::PluginDependency>(numDeps, defaultInit);

	// query sizes of dependency names and allocate space
	hr = pfnQueryDeps(&numDeps, deps.data());
	if (FAILED(hr))
	{
		MessageBoxHelper_FormattedBody(MB_ICONERROR | MB_OK, L"Failed to get dependencies", L"%s returned failure code %d when calling QueryColumnModePluginDependencies (2nd call).", pluginName, hr);
		return hr;
	}
	for (auto depIt = deps.begin(); depIt != deps.end(); depIt++)
	{
		depIt->pName = (WCHAR*)malloc((depIt->length +1) * sizeof(WCHAR)); //add one extra char of padding so that we can ensure null-terminated
		if (depIt->pName == nullptr)
		{
			hr = E_OUTOFMEMORY;
			goto cleanup_and_exit;
		}
	}

	// query dependency names
	hr = pfnQueryDeps(&numDeps, deps.data());
	if (FAILED(hr))
	{
		MessageBoxHelper_FormattedBody(MB_ICONERROR | MB_OK, L"Failed to get dependencies", L"%s returned failure code %d when calling QueryColumnModePluginDependencies (3rd call).", pluginName, hr);
		goto cleanup_and_exit;
	}

	//Now we need to free the plugin library in case it has a load time library that does weird checks for runtime dependencies in dllmain (looking at you dxcompiler.dll)
	FreeLibrary(pluginModule);
	//Finally load the requested libraries
	for (auto depIt = deps.begin(); depIt != deps.end(); depIt++)
	{
		depIt->pName[depIt->length] = L'\0'; //ensure null-terminated
		std::filesystem::path depFullPath = pluginDir / depIt->pName;
		HMODULE hm;
		if (FAILED(LoadLibraryHelper(depFullPath, hm)))
		{
			hr = E_FAIL;
			goto cleanup_and_exit;
			//maybe unload dlls?
		}
	}

	cleanup_and_exit:
	// free allocated strings
	for (auto depIt = deps.begin(); depIt != deps.end(); depIt++)
	{
		if (depIt->pName != nullptr)
		{
			free(depIt->pName);
			depIt->pName = nullptr;
		}
	}
	return hr;
}

HRESULT ColumnMode::PluginManager::LoadPlugin(LPCWSTR pluginName)
{
	std::filesystem::path path(m_modulesRootPath);
	path.append(pluginName)			//Plugins should be in a folder of the plugin name
		.append(pluginName)			//Plugin is a DLL file of the plugin name
		.replace_extension(L".dll");

	

	HMODULE pluginModule;
	BAIL_ON_FAIL_HR(LoadLibraryHelper(path, pluginModule));

	PFN_QUERYCOLUMNMODEPLUGINDEPENDENCIES pfnQueryDeps =
		reinterpret_cast<PFN_QUERYCOLUMNMODEPLUGINDEPENDENCIES>(GetProcAddress(pluginModule, "QueryColumnModePluginDependencies"));

	if (pfnQueryDeps != nullptr)
	{
		BAIL_ON_FAIL_HR(LoadPluginDependenciesHelper(pfnQueryDeps, m_modulesRootPath / pluginName, pluginModule));
	}
	BAIL_ON_FAIL_HR(LoadLibraryHelper(path, pluginModule));

	PFN_OPENCOLUMNMODEPLUGIN pfnOpenPlugin = 
		reinterpret_cast<PFN_OPENCOLUMNMODEPLUGIN>(GetProcAddress(pluginModule, "OpenColumnModePlugin"));

	if (pfnOpenPlugin == nullptr)
	{
		MessageBoxHelper_FormattedBody(MB_ICONERROR | MB_OK, L"Failed to open plugin", L"%s doesn't export the OpenColumnModePlugin function.", pluginName);
		return E_FAIL;
	}

	PluginFunctions pluginFuncs{};
	ZeroMemory(&pluginFuncs, sizeof(pluginFuncs));
	OpenPluginArgs args{};
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

HRESULT ColumnMode::PluginManager::UnloadPlugin(LPCWSTR pluginName)
{
	Plugin* plugin;
	if (GetPlugin(pluginName, &plugin))
	{
		plugin->OnShutdown();
		if (!FreeLibrary(plugin->m_hPluginDll))
		{
			return E_FAIL;
		}
		m_plugins.remove(*plugin);
		return S_OK;
	}
	return S_FALSE;
}

bool ColumnMode::PluginManager::IsPluginLoaded(LPCWSTR pluginName)
{
	for (Plugin& p : m_plugins)
	{
		if (p.m_name.compare(pluginName) == 0)
		{
			return true;
		}
	}
	return false;
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
