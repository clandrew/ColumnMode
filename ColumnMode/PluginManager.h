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
		std::filesystem::path m_modulesRootPath;
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