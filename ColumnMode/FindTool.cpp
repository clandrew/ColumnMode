#include "stdafx.h"
#include "resource.h"
#include "FindTool.h"
#include "Program.h"

using namespace ColumnMode;

extern FindTool g_findTool;

WNDPROC lpfnOrigEditBoxCallback;

LRESULT ColumnMode::FindToolCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		//TODO set up initial search query?
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == ID_NEXT)
		{
			g_findTool.UpdateStringFromDialog(hDlg, IDC_FIND_EDITBOX);
			g_findTool.FindNext();
			return (INT_PTR)TRUE;
		}
		else if (LOWORD(wParam) == ID_PREVIOUS)
		{
			g_findTool.UpdateStringFromDialog(hDlg, IDC_FIND_EDITBOX);
			g_findTool.FindPrev();
			return (INT_PTR)TRUE;
		}
		else if (LOWORD(wParam) == IDCANCEL)
		{
			return g_findTool.CloseDialog();
		}
		break;
	case WM_KEYDOWN:
		switch (wParam)
		{
		case VK_RETURN:
			g_findTool.UpdateStringFromDialog(hDlg, IDC_FIND_EDITBOX);
			g_findTool.FindNext();
			return (INT_PTR)TRUE;
		}
	}
	return (INT_PTR)FALSE;
}

LRESULT ColumnMode::FindEditBoxCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_KEYDOWN:
		switch (wParam)
		{
		case VK_RETURN:
			g_findTool.UpdateStringFromDialog(hDlg, IDC_FIND_EDITBOX);
			g_findTool.FindNext();
			return (INT_PTR)TRUE;
		}
	}
	return CallWindowProc(lpfnOrigEditBoxCallback, hDlg, message, wParam, lParam);
}

bool ColumnMode::FindTool::FindNext()
{
	std::wstring& text = GetAllText();
	if (m_currentIndex == UINT_MAX) { m_currentIndex = 0; }
	size_t index = text.find(m_currentSearch.data(), m_currentIndex);
	if (index == -1)
	{
		MessageBox(NULL, L"Search string not found", L"Error", MB_ICONINFORMATION);
		m_currentIndex = 0;
	}
	else
	{
		m_currentIndex = (UINT)(index + 1);
		SetSelection((int)index, (int)m_currentSearch.length()-1);
	}
	return true;
}

bool ColumnMode::FindTool::FindPrev()
{
	std::wstring& text = GetAllText();
	if (m_currentIndex == 0) { m_currentIndex = UINT_MAX; }
	size_t index = text.rfind(m_currentSearch.data(), m_currentIndex);
	if (index == std::wstring::npos)
	{
		MessageBox(NULL, L"Search string not found", L"Error", MB_ICONINFORMATION);
		m_currentIndex = UINT_MAX;
	}
	else
	{
		m_currentIndex = (UINT)(index - 1);
		SetSelection((int)index, (int)m_currentSearch.length() - 1);
	}
	return true;
}

bool ColumnMode::FindTool::UpdateStringFromDialog(HWND hDlg, int textBoxIdentifier)
{
	HWND dlgHwnd = GetDlgItem(hDlg, textBoxIdentifier);
	wchar_t textBuffer[64]{};
	int numChars = Static_GetText(dlgHwnd, textBuffer, _countof(textBuffer));
	m_currentSearch.assign(textBuffer, numChars);
	return true;
}

void ColumnMode::FindTool::EnsureDialogCreated(HINSTANCE hInst, HWND hWnd)
{
	if (!m_hwndFindDialog)
	{
		m_hwndFindDialog = CreateDialog(hInst, MAKEINTRESOURCE(IDD_FIND_DIALOG), hWnd, ColumnMode::FindToolCallback);
		HWND editBox = GetDlgItem(m_hwndFindDialog, IDC_FIND_EDITBOX);
		lpfnOrigEditBoxCallback = (WNDPROC)SetWindowLong(editBox, GWLP_WNDPROC, (LONG)FindEditBoxCallback);
	}
	ShowWindow(m_hwndFindDialog, SW_SHOW);
	SetFocus(GetDlgItem(m_hwndFindDialog, IDC_FIND_EDITBOX));
}

INT_PTR ColumnMode::FindTool::CloseDialog()
{
	if (m_hwndFindDialog)
	{
		INT_PTR result = 0;
		EndDialog(m_hwndFindDialog, result);
		return result;
	}
	return (INT_PTR)false;
}
