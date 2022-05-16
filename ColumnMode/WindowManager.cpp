#include "stdafx.h"
#include "Program.h"
#include "WindowManager.h"

using namespace ColumnMode;

void WindowManager::Init(HINSTANCE hInst, WindowHandles& primaryWindows)
{
	m_hInstance = hInst;
	h_pPrimaryWindows = &primaryWindows;
}