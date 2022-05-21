//This file is used to generate the function calls that we need to call PluginFunctions from the PluginManager.
//The first block is for declarations, the second is for definitions.
// 
//The declarations are used in PluginManager.h (DECLARE_PLUGIN_FUNCTION_CALL_ALL is defined to declare member functions)
// 
//The definitions are used in PluginManager.cpp (DEFINE_PLUGIN_FUNCTION_CALL_ALL is defined to call the matching function
// on every loaded plugin if it supports it.

//-------------------------------------------------------------------------------------------------
//Declarations
//name - This is the name without the pfn_PF_ prefix
//parameterTypeList - this is the list of parameter types surrounded by parens. EX: (), (UINT), (UINT, BOOL)

#pragma warning( push )
#pragma warning( disable : 4067 ) // unexpected tokens following preprocessor directive - expected a newline

#ifndef DECLARE_PLUGINMANAGER_FUNCTION_CALL_ALL(name, parameterTypeList)
#define DECLARE_PLUGINMANAGER_FUNCTION_CALL_ALL(name, parameterTypeList)
#endif // !DECLARE_PLUGINMANAGER_FUNCTION_CALL_ALL(name, parameterTypeList)

DECLARE_PLUGINMANAGER_FUNCTION_CALL_ALL(OnSave, (LPCWSTR))
DECLARE_PLUGINMANAGER_FUNCTION_CALL_ALL(OnSaveAs, (LPCWSTR))

#undef DECLARE_PLUGINMANAGER_FUNCTION_CALL_ALL

//-------------------------------------------------------------------------------------------------
//Definitions
//name - This is the name without the pfn_PF_ prefix
//parameterList - This is the named parameter list surrounded by parens. Do not include HANDLE for plugin handle here.
//	EX: (), (UINT foo), (UINT foo, BOOL bar)
//parameterNames - This is just the parameter names surrounded by parens. Use p.m_hPlugin if plugin handle required.
//	EX: (), (foo), (foo, bar), (p.m_hPlugin)

#ifndef DEFINE_PLUGINMANAGER_FUNCTION_CALL_ALL(name, parameterList, parameterNames)
#define DEFINE_PLUGINMANAGER_FUNCTION_CALL_ALL(name, parameterList, parameterNames)
#endif // !DEFINE_PLUGINMANAGER_FUNCTION_CALL_ALL(name, parameterList, parameterNames)

DEFINE_PLUGINMANAGER_FUNCTION_CALL_ALL(OnSave, (LPCWSTR fileName), (p.m_hPlugin, fileName))
DEFINE_PLUGINMANAGER_FUNCTION_CALL_ALL(OnSaveAs, (LPCWSTR fileName), (p.m_hPlugin, fileName))

#undef DEFINE_PLUGINMANAGER_FUNCTION_CALL_ALL

#pragma warning(pop)