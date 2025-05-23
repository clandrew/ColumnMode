#pragma once

namespace ColumnMode
{
	class Plugin;
	class PluginManager
	{
	public:
		PluginManager();
		~PluginManager();
		void Init(ColumnModeCallbacks callbacks);
		HRESULT ScanForPlugins();
		HRESULT LoadPluginByName(LPCWSTR pluginName);
		HRESULT UnloadPlugin(Plugin* plugin);
		Plugin* GetPluginByName(LPCWSTR pluginName); // Returns null if not loaded
		std::vector<std::wstring>& GetAvailablePlugins() { return m_availablePlugins; }

#define DECLARE_PLUGINMANAGER_FUNCTION_CALL_ALL(name, parameterTypeList) void PF_##name##_ALL parameterTypeList;
#include "PluginManagerFunctions.inl"
#undef DECLARE_PLUGINMANAGER_FUNCTION_CALL_ALL

	private:
		HRESULT EnsureModulePathExists();
		std::list<Plugin> m_plugins;
		std::filesystem::path m_modulesRootPath;
		ColumnModeCallbacks m_pluginCallbacks;
		std::vector<std::wstring> m_availablePlugins;

	public:
		static const int PLUGIN_MENU_ITEM_START_INDEX = 4096;//chosen arbitrarily. hopefully the resource generator doesn't conflict
	};

	class Plugin
	{
	public:
		Plugin(HANDLE hPlugin, HMODULE dll, PluginFunctions funcs, LPCWSTR name) :
			m_hPlugin(hPlugin), m_hPluginDll(dll), m_pluginFuncs(funcs), m_name(name)
		{
		}

#define DECLARE_PLUGIN_FUNCTION_CALL(name, parameterTypeList) HRESULT name parameterTypeList;
#include "PluginFunctions.inl"
#undef DECLARE_PLUGIN_FUNCTION_CALL

	public:
		const HANDLE m_hPlugin;
		const HMODULE m_hPluginDll;
		const PluginFunctions m_pluginFuncs;
		const std::wstring m_name;
	};

	static inline bool operator==(const Plugin& lhs, const Plugin& rhs) { return lhs.m_hPlugin == rhs.m_hPlugin; }
}