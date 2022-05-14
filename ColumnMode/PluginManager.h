#pragma once

namespace ColumnMode
{
	struct Plugin;
	class PluginManager
	{
	public:
		PluginManager();
		HRESULT ScanForPlugins();
		HRESULT LoadPlugin(LPCWSTR pluginName);
		bool IsPluginLoaded(LPCWSTR pluginName);
		std::vector<std::wstring>& GetAvailablePlugins() { return m_availablePlugins; }

#define DECLARE_PLUGIN_FUNCTION_CALL_ALL(name, parameterTypeList) void PF_##name##_ALL parameterTypeList;
#include "PluginManagerFunctions.inl"
#undef DECLARE_PLUGIN_FUNCTION_CALL_ALL

	private:
		HRESULT EnsureModulePathExists();
		std::vector<Plugin> m_plugins;
		std::filesystem::path m_modulesRootPath;
		ColumnModeCallbacks m_pluginCallbacks;
		std::vector<std::wstring> m_availablePlugins;

	public:
		static const int PLUGIN_MENU_ITEM_START_INDEX = 4096;//chosen arbitrarily. hopefully the resource generator doesn't conflict
	};

	struct Plugin
	{
		Plugin(HANDLE hPlugin, HMODULE dll, PluginFunctions funcs, LPCWSTR name) :
			m_hPlugin(hPlugin), m_hPluginDll(dll), m_pluginFuncs(funcs), m_name(name)
		{
		}

		const HANDLE m_hPlugin;
		const HMODULE m_hPluginDll;
		const PluginFunctions m_pluginFuncs;
		std::wstring m_name;
	};
}