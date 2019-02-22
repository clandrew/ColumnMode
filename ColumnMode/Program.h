#pragma once

void VerifyHR(HRESULT hr);
void VerifyBool(BOOL b);
void VerifyErrno(errno_t e);
void VerifyNonZero(UINT_PTR p);

void InitGraphics(HWND hwnd);
void Draw(HWND hwnd);
void Update();
void OnWindowResize(HWND hwnd);

// Input
void OnMouseMove(HWND hwnd, WPARAM wParam, LPARAM lParam);
void OnMouseLeftButtonDown(LPARAM lParam);
void OnMouseLeftButtonUp(HWND hwnd);
void OnKeyDown(HWND hwnd, WPARAM wParam);
void OnKeyUp(HWND hwnd, WPARAM wParam);
void OnHorizontalScroll(HWND hwnd, WPARAM wParam);
void OnVerticalScroll(HWND hwnd, WPARAM wParam);
void OnMouseWheel(HWND hwnd, WPARAM wParam);

// Menu bar functions
void OnOpen(HWND hwnd);
void OnSave();
void OnUndo(HWND hwnd);
void OnCut(HWND hwnd);
void OnCopy(HWND hwnd);
void OnPaste(HWND hwnd);

void OnClipboardContentsChanged(HWND hwnd);