#pragma once

namespace ColumnMode
{
	LRESULT CALLBACK FindToolDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);
	LRESULT CALLBACK FindEditBoxCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

	class FindTool
	{
	public:
		bool FindNext();
		bool FindPrev();
		bool HandleEnterPressed();
		bool UpdateStringFromDialog(HWND hDlg, int textBoxIdentifier);
		void EnsureDialogCreated(HINSTANCE hInst, HWND hWnd);
		bool TryGetFindDialogHwnd(HWND* pHwnd);
		INT_PTR CloseDialog();

	private:
		UINT m_currentIndex;
		std::wstring m_currentSearch;
		HWND m_hwndFindDialog = NULL;  // Window handle of dialog box
		bool m_searchingForward = true;
		HWND m_editBoxHwnd = NULL;
	};
}
