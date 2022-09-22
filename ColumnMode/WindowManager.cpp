#include "stdafx.h"
#include "Program.h"
#include "WindowManager.h"

using namespace ColumnMode;

void WindowManager::Init(HINSTANCE hInst, WindowHandles primaryWindows)
{
	m_hInstance = hInst;
	h_PrimaryWindows = primaryWindows;
}

ATOM ColumnMode::WindowManager::CreateWindowClass(WNDCLASS windowClass)
{
	windowClass.hInstance = m_hInstance;
	return RegisterClass(&windowClass);
}

ATOM ColumnMode::WindowManager::CreateWindowClassEx(WNDCLASSEX windowClass)
{
	windowClass.hInstance = m_hInstance;
	return RegisterClassEx(&windowClass);
}

HRESULT ColumnMode::WindowManager::CreateNewWindow(CreateWindowArgs args, HWND* pHwnd)
{
	assert(pHwnd != nullptr);
	*pHwnd = CreateWindowEx(
		args.exWindowStyle,
		MAKEINTATOM(args.windowClass),
		args.windowName,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT, CW_USEDEFAULT,
		args.width, args.height,
		NULL,
		NULL,
		m_hInstance,
		NULL
	);
	if (*pHwnd == NULL)
	{
		DWORD err = GetLastError();
		WCHAR buff[64];
		swprintf_s(buff, L"CreateWindowEx failed with err: %d", err);
		MessageBox(NULL, buff, L"CreateNewWindow failed", MB_OK | MB_ICONERROR);
	}
	return *pHwnd == NULL ? E_FAIL : S_OK;
}
