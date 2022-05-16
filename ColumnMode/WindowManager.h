#pragma once

namespace ColumnMode
{
	class WindowManager
	{
	public:
		void Init(HINSTANCE hInst, WindowHandles& primaryWindows);

	private:
		HINSTANCE m_hInstance;
		WindowHandles* h_pPrimaryWindows;
	};
}
