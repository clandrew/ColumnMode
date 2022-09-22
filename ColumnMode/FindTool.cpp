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
		else if (LOWORD(wParam) == IDOK)
		{
			//Enter key pressed
			g_findTool.UpdateStringFromDialog(hDlg, IDC_FIND_EDITBOX);
			if (g_findTool.HandleEnterPressed())
			{
				return (INT_PTR)TRUE;
			}
		}
		break;
	}
	return (INT_PTR)FALSE;
}

bool ColumnMode::FindTool::FindNext()
{
	std::wstring& text = GetAllText();
	if (m_currentIndex == UINT_MAX) { m_currentIndex = 0; }
	else if (!m_searchingForward) { m_currentIndex+=2; }

	size_t index = text.find(m_currentSearch.data(), m_currentIndex);
	if (index == -1)
	{
		MessageBox(NULL, L"Search string not found", L"Error", MB_ICONINFORMATION);
		m_currentIndex = 0;
	}
	else
	{
		m_currentIndex = (UINT)(index + 1);
		ScrollTo(m_currentIndex, ScrollToStyle::CENTER);
		SetSelection((int)index, (int)m_currentSearch.length()-1);
	}
	m_searchingForward = true;
	return true;
}

bool ColumnMode::FindTool::FindPrev()
{
	std::wstring& text = GetAllText();
	if (m_currentIndex == 0) { m_currentIndex = UINT_MAX; }
	else if (m_searchingForward) { m_currentIndex-=2; }

	size_t index = text.rfind(m_currentSearch.data(), m_currentIndex);
	if (index == std::wstring::npos)
	{
		MessageBox(NULL, L"Search string not found", L"Error", MB_ICONINFORMATION);
		m_currentIndex = UINT_MAX;
	}
	else
	{
		m_currentIndex = (UINT)(index - 1);
		ScrollTo(m_currentIndex, ScrollToStyle::CENTER);
		SetSelection((int)index, (int)m_currentSearch.length() - 1);
	}
	m_searchingForward = false;
	return true;
}

bool ColumnMode::FindTool::HandleEnterPressed()
{
	HWND hwnd = GetFocus();
	if (hwnd == m_editBoxHwnd)
	{
		bool shiftPressed = GetKeyState(VK_SHIFT) & 0x8000;
		if (!shiftPressed)
		{
			FindNext();
		}
		else
		{
			FindPrev();
		}
		return true;
	}
	return false;
}

bool ColumnMode::FindTool::UpdateStringFromDialog(HWND hDlg, int textBoxIdentifier)
{
	//HWND editHwnd = GetDlgItem(hDlg, IDC_FIND_EDITBOX);
	wchar_t textBuffer[64]{};
	int numChars = Static_GetText(m_editBoxHwnd, textBuffer, _countof(textBuffer));
	m_currentSearch.assign(textBuffer, numChars);
	return true;
}

void ColumnMode::FindTool::EnsureDialogCreated(HINSTANCE hInst, HWND hWnd)
{
	if (!m_hwndFindDialog)
	{
		m_hwndFindDialog = CreateDialog(hInst, MAKEINTRESOURCE(IDD_FIND_DIALOG), hWnd, ColumnMode::FindToolCallback);
		m_editBoxHwnd = GetDlgItem(m_hwndFindDialog, IDC_FIND_EDITBOX);
	}
	if (!m_currentSearch.empty())
	{
		// initialize the edit box with the previous search string, but select all text so that it will be overwritten when you start typing
		Static_SetText(m_editBoxHwnd, m_currentSearch.data());
		SendMessage(m_editBoxHwnd, EM_SETSEL, 0, -1);
	}
	ShowWindow(m_hwndFindDialog, SW_SHOW);
	SetFocus(m_editBoxHwnd);
}

bool ColumnMode::FindTool::TryGetFindDialogHwnd(HWND* pHwnd)
{
	*pHwnd = m_hwndFindDialog;
	return m_hwndFindDialog != NULL;
}

INT_PTR ColumnMode::FindTool::CloseDialog()
{
	if (m_hwndFindDialog)
	{
		INT_PTR result = 0;
		EndDialog(m_hwndFindDialog, result);
		m_hwndFindDialog = NULL;
		m_editBoxHwnd = NULL;
		return result;
	}
	return (INT_PTR)false;
}
