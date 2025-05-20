#pragma once

struct WindowHandles;

LRESULT CALLBACK FindToolDialogProc(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam);

class FindTool
{
public:
	bool FindNext();
	bool FindPrev();
	bool HandleEnterPressed();
	bool UpdateStringFromDialog(HWND hDlg, int textBoxIdentifier);
	void EnsureDialogCreated(HINSTANCE hinstance, WindowHandles* pWindowHandles);
	INT_PTR CloseDialog(HWND findDlgHwnd);

private:
	UINT m_currentIndex;
	std::wstring m_currentSearch;
	bool m_searchingForward = true;
	HWND m_editBoxHwnd = NULL;
};