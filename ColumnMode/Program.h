#pragma once

void VerifyBool(BOOL b);

struct WindowHandles
{
	HWND TopLevel;
	HWND Document;
	HWND StatusBarLabel;
	HWND FindTool;
};

void InitManagers(HINSTANCE hinstance, WindowHandles windowHandles);
void InitGraphics(WindowHandles windowHandles);
void Draw(WindowHandles windowHandles);
void Update();
void OnWindowResize(WindowHandles windowHandles);
void OnClose(WindowHandles windowHandles);

// Input
void OnMouseMove(WindowHandles windowHandles, WPARAM wParam, LPARAM lParam);
void OnMouseLeftButtonDown(WindowHandles windowHandles, LPARAM lParam);
void OnMouseLeftButtonUp(WindowHandles windowHandles);
void OnMouseLeftButtonDblClick(WindowHandles windowHandles, LPARAM lParam);
void OnKeyDown(WindowHandles windowHandles, WPARAM wParam);
void OnKeyUp(WindowHandles windowHandles, WPARAM wParam);
void OnHorizontalScroll(WindowHandles windowHandles, WPARAM wParam);
void OnVerticalScroll(WindowHandles windowHandles, WPARAM wParam);
void OnMouseWheel(WindowHandles windowHandles, WPARAM wParam);
void OnMouseLeaveClientArea();

// Menu bar functions
void OnNew(WindowHandles windowHandles);
void OnOpen(WindowHandles windowHandles);
void OnSave(WindowHandles windowHandles);
void OnSaveAs(WindowHandles windowHandles);
void OnUndo(WindowHandles windowHandles);
void OnDelete(WindowHandles windowHandles);
void OnCut(WindowHandles windowHandles);
void OnCopy(WindowHandles windowHandles);
void OnPaste(WindowHandles windowHandles);
void OnRefresh(WindowHandles windowHandles);
void OnPrint(WindowHandles windowHandles);
void OnPluginRescan(WindowHandles windowHandles, bool skipRescan=false);
void OnThemesRescan(WindowHandles windowHandles, bool skipRescan = false);
void OnCreateTheme(HWND hwnd, HINSTANCE hInst);
bool OnMaybeDynamicMenuItemSelected(WindowHandles windowHandles, int id);
void OnDiagramMode(WindowHandles windowHandles);
void OnTextMode(WindowHandles windowHandles);
void OnFind(HINSTANCE hinstance, WindowHandles* pWindowHandles);

// Document properties dialog
void OnInitializeDocumentProperties(HWND hDlg);
void OnConfirmDocumentProperties(WindowHandles windowHandles, HWND hDlg, WPARAM wParam);

void OnClipboardContentsChanged(WindowHandles windowHandles);

// Dynamic Menu Item selection
bool OnMaybePluginSelected(WindowHandles windowHandles, int id);
bool OnMaybeThemeSelected(WindowHandles windowHandles, int id);

void PromptToSaveUnsavedChanges();

struct Drag
{
	D2D1_POINT_2F Location;
	BOOL OverlaysText;
	BOOL IsTrailing;
	DWRITE_HIT_TEST_METRICS HitTest;
};

void GetMouseInfo(LPARAM lParam, _Out_ Drag& mouseInfo);

std::wstring const& GetAllText();

//Assumes single line selection
void SetSelection(int startCharIndex, int length, DWRITE_HIT_TEST_METRICS* hitTest = nullptr);

enum class ScrollToStyle
{
	TOP,
	CENTER,
	BOTTOM
};
void ScrollTo(UINT index, ScrollToStyle scrollStyle = ScrollToStyle::CENTER);

FindTool& GetFindTool();