#pragma once

void VerifyBool(BOOL b);

void InitGraphics(HWND hwnd);
void Draw(HWND hwnd);
void Update();
void OnWindowResize(HWND hwnd);

// Input
void OnMouseMove(HWND hwnd, WPARAM wParam, LPARAM lParam);
void OnMouseLeftButtonDown(HWND hwnd, LPARAM lParam);
void OnMouseLeftButtonUp(HWND hwnd);
void OnKeyDown(HWND hwnd, WPARAM wParam);
void OnKeyUp(HWND hwnd, WPARAM wParam);
void OnHorizontalScroll(HWND hwnd, WPARAM wParam);
void OnVerticalScroll(HWND hwnd, WPARAM wParam);
void OnMouseWheel(HWND hwnd, WPARAM wParam);

// Menu bar functions
void OnNew(HWND hwnd);
void OnOpen(HWND hwnd);
void OnSave();
void OnSaveAs(HWND hwnd);
void OnUndo(HWND hwnd);
void OnDelete(HWND hwnd);
void OnCut(HWND hwnd);
void OnCopy(HWND hwnd);
void OnPaste(HWND hwnd);
void OnRefresh(HWND hwnd);

void OnClipboardContentsChanged(HWND hwnd);