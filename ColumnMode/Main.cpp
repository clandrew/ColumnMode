#include "stdafx.h"
#include "Main.h"
#include "Program.h"
#include "PluginManager.h"

#define MAX_LOADSTRING 100

// Global Variables:
HINSTANCE hInst;                                // current instance
WCHAR szTitle[MAX_LOADSTRING];                  // The title bar text
WCHAR szWindowClass[MAX_LOADSTRING];            // the main window class name
wchar_t const* szChildWindowClass = L"ColumnModeChildWindowClass";

bool g_done;
WindowHandles g_windowHandles;

// Forward declarations of functions included in this code module:
void MyRegisterClass(HINSTANCE hInstance);
BOOL                InitInstance(HINSTANCE, int);
LRESULT CALLBACK    TopLevelWndProc(HWND, UINT, WPARAM, LPARAM);
LRESULT CALLBACK    DocumentWndProc(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    About(HWND, UINT, WPARAM, LPARAM);
INT_PTR CALLBACK    DocumentProperties(HWND, UINT, WPARAM, LPARAM);

int APIENTRY wWinMain(_In_ HINSTANCE hInstance,
	_In_opt_ HINSTANCE hPrevInstance,
	_In_ LPWSTR    lpCmdLine,
	_In_ int       nCmdShow)
{
	// Initialize global strings
	LoadStringW(hInstance, IDS_APP_TITLE, szTitle, MAX_LOADSTRING);
	LoadStringW(hInstance, IDC_COLUMNMODE, szWindowClass, MAX_LOADSTRING);
	MyRegisterClass(hInstance);

	// Perform application initialization:
	g_windowHandles = {};
	if (!InitInstance(hInstance, nCmdShow))
	{
		return FALSE;
	}

	HACCEL hAccelTable = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDC_COLUMNMODE));

	VerifyBool(AddClipboardFormatListener(g_windowHandles.TopLevel));

	MSG msg;

	LARGE_INTEGER performanceFrequency;
	VerifyBool(QueryPerformanceFrequency(&performanceFrequency));

	LARGE_INTEGER previousCounter;
	VerifyBool(QueryPerformanceCounter(&previousCounter));

	// Message loop
	g_done = false;
	ColumnMode::FindTool& findTool = GetFindTool();
	HWND findDialogHwnd = NULL;
	while (!g_done)
	{
		const HWND allWindowsForCurrentThread = nullptr;
		while (PeekMessage(&msg, allWindowsForCurrentThread, 0, 0, PM_REMOVE))
		{
			if (findTool.TryGetFindDialogHwnd(&findDialogHwnd) && IsDialogMessage(findDialogHwnd, &msg))
			{
				// msg has already been processed by Find dialog
				continue;
			}
			if (!TranslateAccelerator(msg.hwnd, hAccelTable, &msg))
			{
				TranslateMessage(&msg);
				DispatchMessage(&msg);
			}
		}

		LARGE_INTEGER currentCounter;
		VerifyBool(QueryPerformanceCounter(&currentCounter));

		LONGLONG elapsedTicks = currentCounter.QuadPart - previousCounter.QuadPart;
		LONGLONG elapsedMilliseconds = (elapsedTicks * 1000) / performanceFrequency.QuadPart;
		if (elapsedMilliseconds >= 16)
		{
			Draw(g_windowHandles);

			previousCounter = currentCounter;

			Update();
		}
	}

	RemoveClipboardFormatListener(g_windowHandles.TopLevel);

	return (int)msg.wParam;
}

void MyRegisterClass(HINSTANCE hInstance)
{
	{
		// Top-level window
		WNDCLASSEXW wcex{};

		wcex.cbSize = sizeof(WNDCLASSEX);

		wcex.style = CS_HREDRAW | CS_VREDRAW;
		wcex.lpfnWndProc = TopLevelWndProc;
		wcex.hInstance = hInstance;
		wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_COLUMNMODE));
		wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
		wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_COLUMNMODE);
		wcex.lpszClassName = szWindowClass;
		wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

		(void)RegisterClassExW(&wcex);
	}
	{
		// Document window
		WNDCLASSEXW wcex;

		wcex.cbSize = sizeof(WNDCLASSEX);

		wcex.style = CS_HREDRAW | CS_VREDRAW | CS_DBLCLKS;
		wcex.lpfnWndProc = DocumentWndProc;
		wcex.cbClsExtra = 0;
		wcex.cbWndExtra = 0;
		wcex.hInstance = hInstance;
		wcex.hIcon = LoadIcon(hInstance, MAKEINTRESOURCE(IDI_COLUMNMODE));
		wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wcex.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
		wcex.lpszMenuName = MAKEINTRESOURCEW(IDC_COLUMNMODE);
		wcex.lpszClassName = szChildWindowClass;
		wcex.hIconSm = LoadIcon(wcex.hInstance, MAKEINTRESOURCE(IDI_SMALL));

		(void)RegisterClassExW(&wcex);
	}
}

BOOL InitInstance(HINSTANCE hInstance, int nCmdShow)
{
	hInst = hInstance; // Store instance handle in our global variable

	g_windowHandles.TopLevel = CreateWindowW(
		szWindowClass,
		szTitle,
		WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
		CW_USEDEFAULT, 0,
		CW_USEDEFAULT, 0,
		nullptr,
		nullptr,
		hInstance,
		nullptr);

	if (!g_windowHandles.TopLevel)
	{
		return FALSE;
	}

	g_windowHandles.Document = CreateWindowEx(
		0,
		szChildWindowClass,
		(LPCTSTR)NULL,
		WS_CHILD | WS_BORDER | WS_HSCROLL | WS_VSCROLL,
		0, 0, 0, 0,
		g_windowHandles.TopLevel,
		0,
		hInstance,
		NULL);

	if (!g_windowHandles.Document)
	{
		return FALSE;
	}

	g_windowHandles.StatusBarLabel = CreateWindow(L"STATIC", L"ColumnModeStatusBarLabel", WS_CHILD | WS_BORDER, 0, 0, 0, 0, g_windowHandles.TopLevel, 0, hInstance, NULL);

	if (!g_windowHandles.StatusBarLabel)
	{
		return FALSE;
	}

	InitManagers(hInstance, g_windowHandles);
	
	// ShowWindow will start putting messages (e.g., WM_SIZE) through. So it's okay to do this first before initializing graphics,
	// just make sure everything doesn't hard rely on swap chains being created yet.
	ShowWindow(g_windowHandles.TopLevel, nCmdShow);
	ShowWindow(g_windowHandles.Document, SW_SHOW);
	ShowWindow(g_windowHandles.StatusBarLabel, SW_SHOW);

	// Helpful to ShowWindows before setting up the graphics, since graphics will set up a swap chain sized to the client rect.
	// Unshown window has zero-size client rect. This saves a swap chain resize.
	InitGraphics(g_windowHandles);

	UpdateWindow(g_windowHandles.TopLevel);
	UpdateWindow(g_windowHandles.Document);
	UpdateWindow(g_windowHandles.StatusBarLabel);

	return TRUE;
}

LRESULT CALLBACK DocumentWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	// Creation of the child window will immediately start firing events, even before CreateWindow(child window) has returned to let us store the the variable.
	// Therefore, don't try to do anything until that's done.
	if (g_windowHandles.Document == 0)
		return DefWindowProc(hWnd, message, wParam, lParam);

	switch (message)
	{
	case WM_PAINT:
	{
		Draw(g_windowHandles);
	}
	break;
	case WM_LBUTTONDBLCLK:
	{
		OnMouseLeftButtonDblClick(g_windowHandles, lParam);
	}
	break;
	case WM_LBUTTONDOWN:
	{
		OnMouseLeftButtonDown(g_windowHandles, lParam);
	}
	break;
	case WM_LBUTTONUP:
	{
		OnMouseLeftButtonUp(g_windowHandles);
	}
	break;
	case WM_MOUSEMOVE:
	{
		OnMouseMove(g_windowHandles, wParam, lParam);
	}
	break;
	case WM_MOUSELEAVE:
	{
		OnMouseLeaveClientArea();
	}
	break;
	case WM_SIZE:
	{
		OnWindowResize(g_windowHandles);
	}
	break;
	case  WM_HSCROLL:
	{
		OnHorizontalScroll(g_windowHandles, wParam);
	}
	break;
	case WM_MOUSEWHEEL:
	{
		OnMouseWheel(g_windowHandles, wParam);
	}
	case  WM_VSCROLL:
	{
		OnVerticalScroll(g_windowHandles, wParam);
	}
	break;
	case WM_CLIPBOARDUPDATE:
	{
		OnClipboardContentsChanged(g_windowHandles);
		return 0;
	}
	break;
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

LRESULT CALLBACK TopLevelWndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_COMMAND:
	{
		int wmId = LOWORD(wParam);
		// Parse the menu selections:
		switch (wmId)
		{
		case ID_FILE_NEW:
			OnNew(g_windowHandles);
			break;
		case ID_FILE_SAVE:
			OnSave(g_windowHandles);
			break;
		case ID_FILE_SAVEAS:
			OnSaveAs(g_windowHandles);
			break;
		case ID_FILE_OPEN:
			OnOpen(g_windowHandles);
			break;
		case ID_EDIT_UNDO:
			OnUndo(g_windowHandles);
			break;
		case ID_EDIT_CUT:
			OnCut(g_windowHandles);
			break;
		case ID_EDIT_COPY:
			OnCopy(g_windowHandles);
			break;
		case ID_EDIT_PASTE:
			OnPaste(g_windowHandles);
			break;
		case ID_EDIT_DELETE:
			OnDelete(g_windowHandles);
			break;
		case ID_EDIT_FIND:
			OnFind(hInst, hWnd);
			break;
		case ID_OPTIONS_DIAGRAMMODE:
			OnDiagramMode(g_windowHandles);
			break;
		case ID_OPTIONS_TEXTMODE:
			OnTextMode(g_windowHandles);
			break;
		case ID_FILE_REFRESH:
			OnRefresh(g_windowHandles);
			break;
		case ID_FILE_PROPERTIES:
			DialogBox(hInst, MAKEINTRESOURCE(IDD_DOCUMENTPROPERTIES), hWnd, DocumentProperties);
			break;
		case ID_FILE_PRINT:
			OnPrint(g_windowHandles);
			break;
		case IDM_ABOUT:
			DialogBox(hInst, MAKEINTRESOURCE(IDD_ABOUTBOX), hWnd, About);
			break;
		case ID_PLUGINS_RESCAN:
			OnPluginRescan(g_windowHandles);
			break;
		case IDM_EXIT:
			DestroyWindow(hWnd);
			break;
		default:
			if (!OnMaybePluginSelected(g_windowHandles, LOWORD(wParam)))
			{
				return DefWindowProc(hWnd, message, wParam, lParam);
			}
		}

		
	}
	break;
	case WM_KEYDOWN:
	{
		// Parent window handles keyboard messages because normally it's the parent window that automatically gets focus.
		OnKeyDown(g_windowHandles, wParam);
	}
	break;
	case WM_KEYUP:
	{
		OnKeyUp(g_windowHandles, wParam);
	}
	break;
	case WM_SIZE:
	{
		// Resize the child window.
		RECT rect;
		VerifyBool(GetClientRect(hWnd, &rect));

		int bottomMarginThickness = 20;

		const LONG clientWidth = rect.right - rect.left;
		const LONG clientHeight = rect.bottom - rect.top;
		const LONG documentHeight = clientHeight - bottomMarginThickness;

		VerifyBool(MoveWindow(
			g_windowHandles.Document,
			0,
			0,
			clientWidth,
			documentHeight,
			TRUE));

		VerifyBool(MoveWindow(
			g_windowHandles.StatusBarLabel,
			0,
			documentHeight,
			clientWidth,
			clientHeight,
			TRUE));
	}
	break;
	case WM_CLOSE:
	{
		OnClose(g_windowHandles);
		return 0; // Indicate the message as handled, so we can control whether DestroyWindow is called.
	}
	break;
	case WM_DESTROY:
		g_done = true;
		PostQuitMessage(0);
		break;
	default:
		return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return DefWindowProc(hWnd, message, wParam, lParam);
}

// Message handler for about box.
INT_PTR CALLBACK About(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
		return (INT_PTR)TRUE;

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK || LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}

// Message handler for document properties dialog.
INT_PTR CALLBACK DocumentProperties(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	UNREFERENCED_PARAMETER(lParam);
	switch (message)
	{
	case WM_INITDIALOG:
	{
		OnInitializeDocumentProperties(hDlg);

		return (INT_PTR)TRUE;
	}

	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK)
		{
			OnConfirmDocumentProperties(g_windowHandles, hDlg, wParam);
			return (INT_PTR)TRUE;
		}
		else if (LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return (INT_PTR)TRUE;
		}
		break;
	}
	return (INT_PTR)FALSE;
}
