#pragma once

namespace ColumnMode
{
	class WindowManager
	{
	public:
		void Init(HINSTANCE hInst, WindowHandles primaryWindows);
		ATOM CreateWindowClass(WNDCLASS windowClass);
		ATOM CreateWindowClassEx(WNDCLASSEX windowClass);
		HRESULT CreateNewWindow(CreateWindowArgs args, HWND* pHwnd);
		HRESULT RecommendEditMode(EDIT_MODE editMode);
		WindowHandles GetWindowHandles() { return h_PrimaryWindows; }

	private:
		HINSTANCE m_hInstance;
		WindowHandles h_PrimaryWindows;
	};
}
