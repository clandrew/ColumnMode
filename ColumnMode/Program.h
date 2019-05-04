#pragma once

void VerifyBool(BOOL b);

struct WindowHandles
{
	HWND TopLevel;
	HWND Document;
	HWND StatusBarLabel;
};

void InitGraphics(WindowHandles windowHandles);
void Draw(WindowHandles windowHandles);
void Update();
void OnWindowResize(WindowHandles windowHandles);

// Input
void OnMouseMove(WindowHandles windowHandles, WPARAM wParam, LPARAM lParam);
void OnMouseLeftButtonDown(WindowHandles windowHandles, LPARAM lParam);
void OnMouseLeftButtonUp(WindowHandles windowHandles);
void OnKeyDown(WindowHandles windowHandles, WPARAM wParam);
void OnKeyUp(WindowHandles windowHandles, WPARAM wParam);
void OnHorizontalScroll(WindowHandles windowHandles, WPARAM wParam);
void OnVerticalScroll(WindowHandles windowHandles, WPARAM wParam);
void OnMouseWheel(WindowHandles windowHandles, WPARAM wParam);

// Menu bar functions
void OnNew(WindowHandles windowHandles);
void OnOpen(WindowHandles windowHandles);
void OnSave();
void OnSaveAs(WindowHandles windowHandles);
void OnUndo(WindowHandles windowHandles);
void OnDelete(WindowHandles windowHandles);
void OnCut(WindowHandles windowHandles);
void OnCopy(WindowHandles windowHandles);
void OnPaste(WindowHandles windowHandles);
void OnRefresh(WindowHandles windowHandles);

void OnClipboardContentsChanged(WindowHandles windowHandles);