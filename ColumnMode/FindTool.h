#pragma once

namespace ColumnMode
{
	LRESULT CALLBACK FindToolCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT CALLBACK FindEditBoxCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

	class FindTool
	{
	public:
		bool FindNext();
		bool FindPrev();
		bool UpdateStringFromDialog(HWND hDlg, int textBoxIdentifier);
		void EnsureDialogCreated(HINSTANCE hInst, HWND hWnd);
		INT_PTR CloseDialog();

	private:
		UINT m_currentIndex;
		std::wstring m_currentSearch;
		HWND m_hwndFindDialog = NULL;  // Window handle of dialog box 
	};
}
