#pragma once

namespace ColumnMode
{
	class Plugin;
	class PluginManager
	{
	public:
		PluginManager();
		HRESULT LoadPlugin(LPCWSTR pluginName);
	private:
		HRESULT EnsureModulePathExists();
		std::vector<Plugin> m_plugins;
		LPCWSTR m_moduleDirectoryName = L"Modules";
	};

	class Plugin
	{
	public:
	private:
		HANDLE m_hPlugin;
		HMODULE m_hPluginDll;
		PluginFunctions m_pluginFuncs;
	};
}