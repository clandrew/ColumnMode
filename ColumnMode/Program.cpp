#include "stdafx.h"
#include "Program.h"
#include "Resource.h"
#include "Verify.h"
#include "LayoutInfo.h"
#include "PluginManager.h"

const float g_fontSize = 12.0f;
const int g_tabLength = 4;

bool g_isFileLoaded;
std::wstring g_fileFullPath;
std::wstring g_fileName;
std::wstring g_windowTitleFileNamePrefix;
bool g_hasUnsavedChanges;

ColumnMode::PluginManager g_pluginManager;

ComPtr<ID2D1Factory1> g_d2dFactory;

bool g_needsDeviceRecreation;

// Device dependent resources
ComPtr<IDXGISwapChain> g_swapChain;
ComPtr<ID2D1Device> g_d2dDevice;
ComPtr<ID2D1DeviceContext> g_hwndRenderTarget;
ComPtr<ID2D1SolidColorBrush> g_redBrush;
ComPtr<ID2D1SolidColorBrush> g_blackBrush;
ComPtr<ID2D1SolidColorBrush> g_whiteBrush;
ComPtr<ID2D1SolidColorBrush> g_lightGrayBrush;
ComPtr<ID2D1SolidColorBrush> g_yellowBrush;
ComPtr<ID2D1SolidColorBrush> g_selectionBrush;
UINT g_marchingAntsIndex;
std::vector<ComPtr<ID2D1StrokeStyle>> g_marchingAnts;

ComPtr<IDWriteFactory> g_dwriteFactory;
ComPtr<IDWriteTextFormat> g_textFormat;
ComPtr<IDWriteTextLayout> g_textLayout;

struct Drag
{
	D2D1_POINT_2F Location;
	BOOL OverlaysText;
	BOOL IsTrailing;
	DWRITE_HIT_TEST_METRICS HitTest;
};

LayoutInfo g_layoutInfo;

struct Action
{
	enum ActionType
	{
		WriteCharacter,
		WriteTab,
		Backspace,
		DeleteBlock,
		PasteToPosition,
		PasteToBlock,
		MoveBlockLeft,
		MoveBlockUp,
		MoveBlockRight,
		MoveBlockDown
	};
	ActionType Type;

	std::vector<std::vector<wchar_t>> OverwrittenChars;

	UINT32 TextPosition;

	int BlockLeft, BlockTop, BlockRight, BlockBottom;

	std::vector<Drag> DragData;
};
std::vector<Action> g_undoBuffer;

struct KeyOutput
{
	bool Valid;
	wchar_t Lowercase;
	wchar_t Uppercase;

	void Set(wchar_t value)
	{
		Valid = true;
		Lowercase = value;
		Uppercase = value;
	}

	void Set(wchar_t lower, wchar_t upper)
	{
		Valid = true;
		Lowercase = lower;
		Uppercase = upper;
	}
};
KeyOutput g_keyOutput[255];


bool g_isDragging;
bool g_isTrackingLeaveClientArea;

bool g_isDebugBreaking;

bool g_isShiftDown;
bool g_isCtrlDown;

Drag g_start;
Drag g_current;

bool g_hasTextSelectionRectangle;
D2D1_RECT_F g_textSelectionRectangle;

UINT32 g_caretCharacterIndex;
D2D1_POINT_2F g_caretPosition;
DWRITE_HIT_TEST_METRICS g_caretMetrics;
int g_caretBlinkState;

std::wstring g_allText;
std::vector<int> g_textLineStarts;
int g_maxLineLength = 0;

int g_verticalScrollLimit = 0;

void InitializeKeyOutput()
{
	memset(g_keyOutput, 0, sizeof(g_keyOutput));

	g_keyOutput[32].Set(L' ');

	for (int i = 48; i <= 57; ++i)
	{
		static wchar_t symbols[11] = L")!@#$%^&*(";
		g_keyOutput[i].Set(L'0' + (i-48), symbols[i - 48]);
	}

	for (int i = 65; i <= 90; ++i)
	{
		g_keyOutput[i].Set(L'a' + (i - 65), 'A' + (i - 65));
	}

	g_keyOutput[186].Set(L';', L':');
	g_keyOutput[187].Set(L'=', L'+');
	g_keyOutput[188].Set(L',', L'<');
	g_keyOutput[189].Set(L'-', L'_');
	g_keyOutput[190].Set(L'.', L'>');
	g_keyOutput[191].Set(L'/', L'?');
	g_keyOutput[192].Set(L'`', L'~');
	g_keyOutput[219].Set(L'[', L'{');
	g_keyOutput[220].Set(L'\\', L'|');
	g_keyOutput[221].Set(L']', L'}');
	g_keyOutput[222].Set(L'\'', L'"');
}

static void EnableMenuItem(WindowHandles windowHandles, int id)
{
	HMENU menu = GetMenu(windowHandles.TopLevel);
	EnableMenuItem(menu, id, MF_ENABLED);
}

static void DisableMenuItem(WindowHandles windowHandles, int id)
{
	HMENU menu = GetMenu(windowHandles.TopLevel);
	EnableMenuItem(menu, id, MF_DISABLED);
}

bool IsValidPosition(int row, int column)
{
	if (row < 0 || row >= static_cast<int>(g_textLineStarts.size()))
		return false;

	if (column < 0 || column >= g_maxLineLength)
		return false;

	return true;
}

wchar_t TryGetCharacter(int row, int column)
{
	if (!IsValidPosition(row, column))
		return L' ';
	
	return g_allText[g_textLineStarts[row] + column];
}

void TrySetCharacter(int row, int column, wchar_t value)
{
	if (!IsValidPosition(row, column))
		return;

	g_allText[g_textLineStarts[row] + column] = value;
}

void GetRowAndColumnFromCharacterPosition(UINT32 characterPosition, int* row, int* column)
{
	for (int i = 0; i < static_cast<int>(g_textLineStarts.size()); ++i)
	{
		if (i == g_textLineStarts.size() - 1)
		{
			*row = i;
			*column = characterPosition - g_textLineStarts[i];
			return;
		}
		else
		{
			assert(characterPosition <= INT_MAX);
			int pos = static_cast<int>(characterPosition);
			if (pos >= g_textLineStarts[i] && pos < g_textLineStarts[i + 1])
			{
				*row = i;
				*column = pos - g_textLineStarts[i];
				return;
			}
		}
	}
}

static D2D1_SIZE_U GetWindowSize(HWND hwnd)
{
	RECT clientRect{};
	VerifyBool(GetClientRect(hwnd, &clientRect));

	UINT32 windowWidth = clientRect.right - clientRect.left;
	UINT32 windowHeight = clientRect.bottom - clientRect.top;

	return D2D1::SizeU(windowWidth, windowHeight);
}

static void RecreateTextLayout()
{
	VerifyHR(g_dwriteFactory->CreateTextLayout(g_allText.data(), static_cast<UINT32>(g_allText.size()), g_textFormat.Get(), 0, 0, &g_textLayout));

	g_layoutInfo.RefreshLayoutMetrics(g_textLayout.Get());
}

static void SetCaretCharacterIndex(UINT32 newCharacterIndex, HWND statusBarLabelHwnd)
{
	float caretPositionX, caretPositionY;
	DWRITE_HIT_TEST_METRICS caretMetrics;
	VerifyHR(g_textLayout->HitTestTextPosition(newCharacterIndex, FALSE, &caretPositionX, &caretPositionY, &caretMetrics));

	int caretRow, caretColumn;
	GetRowAndColumnFromCharacterPosition(newCharacterIndex, &caretRow, &caretColumn);

	// Needed because DirectWrite will let you effectively select the new-lines at the end of each row,
	// which we don't allow here.
	if (caretColumn >= g_maxLineLength)
		return;

	// Commit the caret change
	g_caretCharacterIndex = newCharacterIndex;
	g_caretPosition.x = caretPositionX;
	g_caretPosition.y = caretPositionY;
	g_caretMetrics = caretMetrics;

	// Show label of row and column numbers, 1-indexed.
	std::wstringstream label;
	label << L"Row: " << (caretRow+1) << "        Col: " << (caretColumn+1);
	Static_SetText(statusBarLabelHwnd, label.str().c_str());
}

static void SetScrollPositions(WindowHandles windowHandles)
{
	auto targetSize = g_hwndRenderTarget->GetSize();

	{
		SCROLLINFO scrollInfo = {};
		scrollInfo.cbSize = sizeof(SCROLLINFO);
		scrollInfo.fMask = SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL;
		scrollInfo.nMin = 0;
		scrollInfo.nMax = g_layoutInfo.GetLayoutWidth();
		scrollInfo.nPage = static_cast<int>(targetSize.width);
		SetScrollInfo(windowHandles.Document, SB_HORZ, &scrollInfo, TRUE);
	}
	{
		SCROLLINFO scrollInfo = {};
		scrollInfo.cbSize = sizeof(SCROLLINFO);
		scrollInfo.fMask = SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL;
		scrollInfo.nMin = 0;
		scrollInfo.nMax = g_layoutInfo.GetLayoutHeight();
		scrollInfo.nPage = static_cast<int>(targetSize.height);
		SetScrollInfo(windowHandles.Document, SB_VERT, &scrollInfo, TRUE);

		g_verticalScrollLimit = scrollInfo.nMax - scrollInfo.nPage;
	}
}

static void UpdatePasteEnablement(WindowHandles windowHandles)
{
	VerifyBool(OpenClipboard(0));
	bool enable = GetClipboardData(CF_UNICODETEXT) != 0;
	VerifyBool(CloseClipboard());

	if (enable)
		EnableMenuItem(windowHandles, ID_EDIT_PASTE);
	else
		DisableMenuItem(windowHandles, ID_EDIT_PASTE);
}

struct LoadOrCreateFileResult
{
	int MaxLineLength;
	std::wstring AllText;
	std::vector<int> TextLineStarts;
};
LoadOrCreateFileResult LoadOrCreateFileContents(wchar_t const* fileName)
{
	LoadOrCreateFileResult result{};

	std::wifstream f(fileName);

	bool validLength = true;

	std::vector<std::wstring> lines;
	while (f.good())
	{
		if (lines.size() >= INT_MAX)
		{
			validLength = false;
			break;
		}

		std::wstring line;
		std::getline(f, line);
		if (line.length() >= INT_MAX)
		{
			validLength = false;
			break;
		}

		// Sanitize tabs.
		std::wstring sanitizedLine;
		for (size_t i = 0; i < line.length(); ++i)
		{
			wchar_t ch = line[i];
			if (ch == '\t')
			{
				for (int i = 0; i < g_tabLength; ++i)
				{
					sanitizedLine.push_back(' ');
				}
			}
			else
			{
				sanitizedLine.push_back(ch);
			}
		}

		lines.push_back(sanitizedLine);

		result.MaxLineLength = max(result.MaxLineLength, static_cast<int>(sanitizedLine.length()));
	}

	if (!validLength)
	{
		MessageBox(nullptr, L"File couldn't be loaded because a line or line count is too high.", L"ColumnMode", MB_OK);
		return result;
	}

	result.AllText = L"";
	result.TextLineStarts.clear();
	for (int i = 0; i < static_cast<int>(lines.size()); ++i)
	{
		int spaceToAdd = static_cast<int>(result.MaxLineLength - lines[i].length());
		for (int j = 0; j < spaceToAdd; ++j)
		{
			lines[i].push_back(L' ');
		}
		result.TextLineStarts.push_back(static_cast<int>(result.AllText.length()));
		result.AllText.append(lines[i]);

		if (i != static_cast<int>(lines.size()) - 1)
		{
			result.AllText.push_back('\n');
		}
	}

	return result;
}

void InitializeDocument(WindowHandles windowHandles, LoadOrCreateFileResult const& loadOrCreateFileResult)
{
	g_allText = std::move(loadOrCreateFileResult.AllText);
	g_maxLineLength = std::move(loadOrCreateFileResult.MaxLineLength);
	g_textLineStarts = std::move(loadOrCreateFileResult.TextLineStarts);

	g_hasTextSelectionRectangle = false;
	g_caretCharacterIndex = 0;
	g_marchingAntsIndex = 0;
	g_layoutInfo.SetPosition(D2D1::Point2F(20, 20));

	RecreateTextLayout();

	SetCaretCharacterIndex(0, windowHandles.StatusBarLabel);

	SetScrollPositions(windowHandles);

	g_isFileLoaded = true;

	EnableMenuItem(windowHandles, ID_FILE_SAVEAS);
	EnableMenuItem(windowHandles, ID_FILE_PROPERTIES);
	EnableMenuItem(windowHandles, ID_FILE_PRINT);

	UpdatePasteEnablement(windowHandles);
}

void UpdateWindowTitle(WindowHandles windowHandles)
{
	std::wstring windowTitle = g_windowTitleFileNamePrefix;

	if (g_hasUnsavedChanges)
		windowTitle.append(L" *");

	SetWindowText(windowHandles.TopLevel, windowTitle.c_str());
}

void SetCurrentFileNameAndUpdateWindowTitle(WindowHandles windowHandles, wchar_t* fileName)
{
	g_windowTitleFileNamePrefix = L"ColumnMode - ";
	if (fileName)
	{
		g_fileFullPath = fileName;

		size_t delimiterIndex = g_fileFullPath.find_last_of(L'\\');
		g_fileName = g_fileFullPath.substr(delimiterIndex + 1);

		EnableMenuItem(windowHandles, ID_FILE_REFRESH);
		EnableMenuItem(windowHandles, ID_FILE_SAVE);

		g_windowTitleFileNamePrefix.append(g_fileName);
	}
	else
	{
		g_fileFullPath = L"";
		DisableMenuItem(windowHandles, ID_FILE_REFRESH);
		DisableMenuItem(windowHandles, ID_FILE_SAVE);

		g_windowTitleFileNamePrefix.append(L"(New Document)");
	}
	UpdateWindowTitle(windowHandles);
}

void OnNew(WindowHandles windowHandles)
{
	LoadOrCreateFileResult createFileResult{};

	int defaultRowCount = 40;
	int defaultColumnCount = 100;

	createFileResult.MaxLineLength = defaultColumnCount;

	std::wstring blankLine;
	for (int i = 0; i < defaultColumnCount; ++i)
	{
		blankLine.push_back(L' ');
	}

	for (int i = 0; i < defaultRowCount; ++i)
	{
		createFileResult.TextLineStarts.push_back(i * (defaultColumnCount + 1));
		createFileResult.AllText.append(blankLine);

		if (i < defaultRowCount - 1)
		{
			createFileResult.AllText.push_back(L'\n');
		}
	}

	SetCurrentFileNameAndUpdateWindowTitle(windowHandles, nullptr);

	InitializeDocument(windowHandles, createFileResult);
}

void SetTargetToBackBuffer()
{
	ComPtr<IDXGISurface> swapChainBackBuffer;
	VerifyHR(g_swapChain->GetBuffer(0, IID_PPV_ARGS(&swapChainBackBuffer)));

	D2D1_BITMAP_PROPERTIES1 bitmapProperties =
		D2D1::BitmapProperties1(
			D2D1_BITMAP_OPTIONS_TARGET | D2D1_BITMAP_OPTIONS_CANNOT_DRAW,
			D2D1::PixelFormat(DXGI_FORMAT_B8G8R8A8_UNORM, D2D1_ALPHA_MODE_IGNORE));

	ComPtr<ID2D1Bitmap1> image;
	VerifyHR(g_hwndRenderTarget->CreateBitmapFromDxgiSurface(swapChainBackBuffer.Get(), bitmapProperties, &image));

	g_hwndRenderTarget->SetTarget(image.Get());
}

static void DisableTextSelectionRectangle(WindowHandles windowHandles)
{
	DisableMenuItem(windowHandles, ID_EDIT_COPY);
	DisableMenuItem(windowHandles, ID_EDIT_CUT);
	DisableMenuItem(windowHandles, ID_EDIT_DELETE);
	g_hasTextSelectionRectangle = false;
}

static void EnableTextSelectionRectangle(WindowHandles windowHandles)
{
	EnableMenuItem(windowHandles, ID_EDIT_COPY);
	EnableMenuItem(windowHandles, ID_EDIT_CUT);
	EnableMenuItem(windowHandles, ID_EDIT_DELETE);
	g_hasTextSelectionRectangle = true;
}

template<typename OrderableType>
static void PutInOrder(OrderableType* a, OrderableType* b)
{
	if (*a > *b)
	{
		OrderableType tmp = *a;
		*a = *b;
		*b = tmp;
	}
}

struct SignedRect
{
	int Left, Top, Right, Bottom;
};

static SignedRect GetTextSelectionRegion()
{
	SignedRect r;

	GetRowAndColumnFromCharacterPosition(g_start.HitTest.textPosition, &r.Top, &r.Left);
	GetRowAndColumnFromCharacterPosition(g_current.HitTest.textPosition, &r.Bottom, &r.Right);

	PutInOrder(&r.Left, &r.Right);
	PutInOrder(&r.Top, &r.Bottom);

	return r;
}

void ReleaseDeviceDependentResources()
{
	g_swapChain.Reset();
	g_d2dDevice.Reset();
	g_hwndRenderTarget.Reset();
	g_redBrush.Reset();
	g_blackBrush.Reset();
	g_whiteBrush.Reset();
	g_lightGrayBrush.Reset();
	g_yellowBrush.Reset();
	g_selectionBrush.Reset();
	g_marchingAnts.clear();
}

void CreateDeviceDependentResources(WindowHandles windowHandles)
{
	auto windowSize = GetWindowSize(windowHandles.Document);

	DXGI_SWAP_CHAIN_DESC swapChainDescription = {};
	swapChainDescription.BufferCount = 2;
	swapChainDescription.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	swapChainDescription.BufferDesc.Width = windowSize.width;
	swapChainDescription.BufferDesc.Height = windowSize.height;
	swapChainDescription.OutputWindow = windowHandles.Document;
	swapChainDescription.Windowed = TRUE;
	swapChainDescription.SampleDesc.Count = 1;
	swapChainDescription.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;

	ComPtr<ID3D11Device> d3dDevice;

	UINT deviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#ifdef _DEBUG
	deviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	VerifyHR(D3D11CreateDeviceAndSwapChain(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		deviceFlags,
		nullptr,
		0,
		D3D11_SDK_VERSION,
		&swapChainDescription,
		&g_swapChain,
		&d3dDevice,
		nullptr,
		nullptr));

	ComPtr<IDXGIDevice> dxgiDevice;
	VerifyHR(d3dDevice.As(&dxgiDevice));

	VerifyHR(g_d2dFactory->CreateDevice(dxgiDevice.Get(), &g_d2dDevice));

	VerifyHR(g_d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g_hwndRenderTarget));

	SetTargetToBackBuffer();

	VerifyHR(g_hwndRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Red), &g_redBrush));
	VerifyHR(g_hwndRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &g_blackBrush));
	VerifyHR(g_hwndRenderTarget->CreateSolidColorBrush(D2D1::ColorF(0.9f, 0.9f, 0.9f), &g_lightGrayBrush));
	VerifyHR(g_hwndRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &g_whiteBrush));
	VerifyHR(g_hwndRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Orange, 0.15f), &g_yellowBrush));
	VerifyHR(g_hwndRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Navy, 0.2f), &g_selectionBrush));

	for (int i = 0; i < 4; ++i)
	{
		D2D1_STROKE_STYLE_PROPERTIES strokeStyleProperties = D2D1::StrokeStyleProperties();
		strokeStyleProperties.dashStyle = D2D1_DASH_STYLE_DASH;
		strokeStyleProperties.dashOffset = -static_cast<float>(i) * 1.0f;
		ComPtr<ID2D1StrokeStyle> strokeStyle;
		VerifyHR(g_d2dFactory->CreateStrokeStyle(strokeStyleProperties, nullptr, 0, &strokeStyle));
		g_marchingAnts.push_back(strokeStyle);
	}
}

void InitGraphics(WindowHandles windowHandles)
{
	g_isFileLoaded = false;
	g_isDragging = false;
	g_isTrackingLeaveClientArea = false;
	g_hasTextSelectionRectangle = false;
	g_caretBlinkState = 0;
	g_isShiftDown = false;
	g_hasUnsavedChanges = false;
	g_needsDeviceRecreation = false;

	InitializeKeyOutput();

	D2D1_FACTORY_OPTIONS factoryOptions = {};
	factoryOptions.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
	VerifyHR(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &factoryOptions, &g_d2dFactory));

	CreateDeviceDependentResources(windowHandles);

	VerifyHR(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory), &g_dwriteFactory));

	VerifyHR(g_dwriteFactory->CreateTextFormat(
		L"Courier New", 
		nullptr, 
		DWRITE_FONT_WEIGHT_NORMAL, 
		DWRITE_FONT_STYLE_NORMAL, 
		DWRITE_FONT_STRETCH_NORMAL, 
		g_fontSize, 
		L"en-us", 
		&g_textFormat));

	VerifyHR(g_textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));

	DisableMenuItem(windowHandles, ID_FILE_SAVE);
	DisableMenuItem(windowHandles, ID_FILE_SAVEAS);
	DisableMenuItem(windowHandles, ID_FILE_REFRESH);
	DisableMenuItem(windowHandles, ID_EDIT_UNDO);
	DisableMenuItem(windowHandles, ID_EDIT_CUT);
	DisableMenuItem(windowHandles, ID_EDIT_COPY);
	DisableMenuItem(windowHandles, ID_EDIT_PASTE);
	DisableMenuItem(windowHandles, ID_EDIT_CUT);
	DisableMenuItem(windowHandles, ID_EDIT_DELETE);
	DisableMenuItem(windowHandles, ID_FILE_PROPERTIES);
	DisableMenuItem(windowHandles, ID_FILE_PRINT);
	
	Static_SetText(windowHandles.StatusBarLabel, L"");
}

void DrawDocument()
{
	D2D1_RECT_F layoutRectangleInScreenSpace = g_layoutInfo.GetLayoutRectangleInScreenSpaceLockedToPixelCenters();

	// Give the document a background that doesn't tightly hug the content
	float margin = 16.0f;
	D2D1_RECT_F paper = D2D1::RectF(
		layoutRectangleInScreenSpace.left - margin,
		layoutRectangleInScreenSpace.top,
		layoutRectangleInScreenSpace.right,
		layoutRectangleInScreenSpace.bottom);

	g_hwndRenderTarget->FillRectangle(paper, g_whiteBrush.Get());

	D2D1_RECT_F marginArea = D2D1::RectF(
		layoutRectangleInScreenSpace.left - margin,
		layoutRectangleInScreenSpace.top,
		layoutRectangleInScreenSpace.left,
		layoutRectangleInScreenSpace.bottom);
	g_hwndRenderTarget->FillRectangle(marginArea, g_lightGrayBrush.Get());

	g_hwndRenderTarget->DrawRectangle(paper, g_blackBrush.Get());
	
	// Highlight current line
	{
		g_hwndRenderTarget->FillRectangle(D2D1::RectF(
			layoutRectangleInScreenSpace.left,
			layoutRectangleInScreenSpace.top + g_caretPosition.y,
			layoutRectangleInScreenSpace.right,
			layoutRectangleInScreenSpace.top + g_caretPosition.y + g_caretMetrics.height), g_yellowBrush.Get());
	}
	
	D2D1_POINT_2F layoutPosition = g_layoutInfo.GetPosition();

	g_hwndRenderTarget->DrawTextLayout(layoutPosition, g_textLayout.Get(), g_blackBrush.Get());

	// Draw caret
	if (g_caretBlinkState <= 25)
	{
		g_hwndRenderTarget->FillRectangle(D2D1::RectF(
			layoutPosition.x + g_caretPosition.x,
			layoutPosition.y + g_caretPosition.y,
			layoutPosition.x + g_caretPosition.x + g_caretMetrics.width,
			layoutPosition.y + g_caretPosition.y + g_caretMetrics.height), g_blackBrush.Get());
	}

	if (g_isDragging)
	{
		g_hwndRenderTarget->DrawRectangle(D2D1::RectF(g_start.Location.x, g_start.Location.y, g_current.Location.x, g_current.Location.y), g_redBrush.Get());
	}

	if (g_hasTextSelectionRectangle)
	{
		g_hwndRenderTarget->FillRectangle(g_textSelectionRectangle, g_selectionBrush.Get());
		g_hwndRenderTarget->DrawRectangle(g_textSelectionRectangle, g_blackBrush.Get(), 1.0f, g_marchingAnts[g_marchingAntsIndex / 5].Get());
	}
}

void Draw(WindowHandles windowHandles)
{
	if (g_needsDeviceRecreation)
	{
		CreateDeviceDependentResources(windowHandles);
		g_needsDeviceRecreation = false;
	}

	g_hwndRenderTarget->BeginDraw();
	g_hwndRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::LightGray));

	if (g_isFileLoaded)
	{
		DrawDocument();	
	}
	
	VerifyHR(g_hwndRenderTarget->EndDraw());

	HRESULT presentHr = g_swapChain->Present(1, 0);
	if (FAILED(presentHr))
	{
		ReleaseDeviceDependentResources();
		g_needsDeviceRecreation = true;
	}
}

float ClampToRange(float value, float min, float max)
{
	if (value < min)
		return min;

	if (value > max)
		return max;

	return value;
}

void OnMouseLeftButtonDown(WindowHandles windowHandles, LPARAM lParam)
{
	if (!g_isFileLoaded)
		return;

	g_isDragging = true;
	DisableTextSelectionRectangle(windowHandles);

	int xPos = GET_X_LPARAM(lParam);
	int yPos = GET_Y_LPARAM(lParam);

	g_start.Location.x = static_cast<float>(xPos);
	g_start.Location.y = static_cast<float>(yPos);

	D2D1_RECT_F layoutRectangleInScreenSpace = g_layoutInfo.GetLayoutRectangleInScreenSpace();
	g_start.Location.x = ClampToRange(g_start.Location.x, layoutRectangleInScreenSpace.left, layoutRectangleInScreenSpace.right);
	g_start.Location.y = ClampToRange(g_start.Location.y, layoutRectangleInScreenSpace.top, layoutRectangleInScreenSpace.bottom);

	g_current.Location.x = g_start.Location.x;
	g_current.Location.y = g_start.Location.y;

	D2D1_POINT_2F layoutPosition = g_layoutInfo.GetPosition();
	VerifyHR(g_textLayout->HitTestPoint(
		g_start.Location.x - layoutPosition.x,
		g_start.Location.y - layoutPosition.y,
		&g_start.IsTrailing, 
		&g_start.OverlaysText, 
		&g_start.HitTest));

	if (g_start.OverlaysText)
	{
		// Move the caret
		SetCaretCharacterIndex(g_start.HitTest.textPosition, windowHandles.StatusBarLabel);
	}
}

void OnMouseLeftButtonUp(WindowHandles windowHandles)
{
	if (!g_isFileLoaded)
		return;

	g_isDragging = false;
}

// Updates g_textSelectionRectangle based on g_start and g_current.
static void UpdateTextSelectionRectangle()
{
	// g_textSelectionRectangle is kept properly ordered
	if (g_start.HitTest.left <= g_current.HitTest.left)
	{
		g_textSelectionRectangle.left = g_start.HitTest.left;
		g_textSelectionRectangle.right = g_current.HitTest.left + g_current.HitTest.width;
	}
	else
	{
		g_textSelectionRectangle.left = g_current.HitTest.left;
		g_textSelectionRectangle.right = g_start.HitTest.left + g_start.HitTest.width;
	}

	if (g_start.HitTest.top <= g_current.HitTest.top)
	{
		g_textSelectionRectangle.top = g_start.HitTest.top;
		g_textSelectionRectangle.bottom = g_current.HitTest.top + g_current.HitTest.height;
	}
	else
	{
		g_textSelectionRectangle.top = g_current.HitTest.top;
		g_textSelectionRectangle.bottom = g_start.HitTest.top + g_start.HitTest.height;
	}

	D2D1_POINT_2F layoutPosition = g_layoutInfo.GetPosition();

	g_textSelectionRectangle.left += layoutPosition.x;
	g_textSelectionRectangle.right += layoutPosition.x;

	g_textSelectionRectangle.top += layoutPosition.y;
	g_textSelectionRectangle.bottom += layoutPosition.y;
}

static int s_dbgIndex = 0;

void OnMouseMove(WindowHandles windowHandles, WPARAM wParam, LPARAM lParam)
{		
	if (!g_isFileLoaded)
		return;

	if (!g_isTrackingLeaveClientArea)
	{
		TRACKMOUSEEVENT mouseEventTracking = {};
		mouseEventTracking.cbSize = sizeof(mouseEventTracking);
		mouseEventTracking.dwFlags = TME_LEAVE;
		mouseEventTracking.hwndTrack = windowHandles.Document;
		TrackMouseEvent(&mouseEventTracking);

		g_isTrackingLeaveClientArea = true;
	}

	if (g_isDragging)
	{
		int xPos = GET_X_LPARAM(lParam);
		int yPos = GET_Y_LPARAM(lParam);

		g_current.Location.x = static_cast<float>(xPos);
		g_current.Location.y = static_cast<float>(yPos);

		D2D1_RECT_F layoutRectangleInScreenSpace = g_layoutInfo.GetLayoutRectangleInScreenSpace();
		float dontSelectNewlines = 1;
		g_current.Location.x = ClampToRange(g_current.Location.x, layoutRectangleInScreenSpace.left, layoutRectangleInScreenSpace.right - dontSelectNewlines);
		g_current.Location.y = ClampToRange(g_current.Location.y, layoutRectangleInScreenSpace.top, layoutRectangleInScreenSpace.bottom);

		D2D1_POINT_2F layoutPosition = g_layoutInfo.GetPosition();
		VerifyHR(g_textLayout->HitTestPoint(
			g_current.Location.x - layoutPosition.x,
			g_current.Location.y - layoutPosition.y,
			&g_current.IsTrailing, 
			&g_current.OverlaysText, 
			&g_current.HitTest));

		if (g_isDebugBreaking)
		{
			__debugbreak();
			g_isDebugBreaking = false;
		}

		if (g_start.OverlaysText || g_current.OverlaysText)
		{
			EnableTextSelectionRectangle(windowHandles);
			UpdateTextSelectionRectangle();
		}
		else
		{
			DisableTextSelectionRectangle(windowHandles);
		}

		if (g_current.OverlaysText)
		{
			SetCaretCharacterIndex(g_current.HitTest.textPosition, windowHandles.StatusBarLabel);
		}
	}
}

void OnMouseLeaveClientArea()
{
	if (g_isDragging)
	{
		g_isDragging = false;
	}
	g_isTrackingLeaveClientArea = false;
}

void OnWindowResize(WindowHandles windowHandles)
{
	if (!g_hwndRenderTarget)
		return;

	g_hwndRenderTarget->SetTarget(nullptr);

	auto windowSize = GetWindowSize(windowHandles.Document);
	VerifyHR(g_swapChain->ResizeBuffers(2, windowSize.width, windowSize.height, DXGI_FORMAT_B8G8R8A8_UNORM, 0));
	
	SetTargetToBackBuffer();
	
	SetScrollPositions(windowHandles);
}

static void FinalizeSelectionBoxMovement(WindowHandles windowHandles)
{
	RecreateTextLayout();

	VerifyHR(g_textLayout->HitTestTextPosition(g_start.HitTest.textPosition, FALSE, &g_start.Location.x, &g_start.Location.y, &g_start.HitTest));
	VerifyHR(g_textLayout->HitTestTextPosition(g_current.HitTest.textPosition, FALSE, &g_current.Location.x, &g_current.Location.y, &g_current.HitTest));

	UpdateTextSelectionRectangle();
}

void AddAction(WindowHandles windowHandles, Action const& a)
{
	EnableMenuItem(windowHandles, ID_EDIT_UNDO);

	if (g_undoBuffer.size() == 10) // Stack limit
	{
		g_undoBuffer.erase(g_undoBuffer.begin());
	}
	g_undoBuffer.push_back(a);

	g_hasUnsavedChanges = true;
	UpdateWindowTitle(windowHandles);
}

static void DeleteBlock(WindowHandles windowHandles)
{
	SignedRect selection = GetTextSelectionRegion();
	
	Action a;
	a.Type = Action::DeleteBlock;
	for (int y = selection.Top; y <= selection.Bottom; y++)
	{
		std::vector<wchar_t> overwrittenLine;
		for (int x = selection.Left; x <= selection.Right; x++)
		{
			overwrittenLine.push_back(TryGetCharacter(y, x));
		}
		a.OverwrittenChars.push_back(overwrittenLine);
	}

	a.BlockLeft = selection.Left;
	a.BlockTop = selection.Top;
	a.BlockRight = selection.Right;
	a.BlockBottom = selection.Bottom;
	a.TextPosition = g_current.HitTest.textPosition;
	a.DragData.push_back(g_start);
	a.DragData.push_back(g_current);
	AddAction(windowHandles, a);

	for (int y = selection.Top; y <= selection.Bottom; y++)
	{
		for (int x = selection.Left; x <= selection.Right; x++)
		{
			TrySetCharacter(y, x, L' ');
		}
	}
}

void AddMoveBlockAction(
	WindowHandles windowHandles, 
	SignedRect selection,
	Action::ActionType actionType,
	std::vector<wchar_t> overwrittenChars)
{
	Action a;
	a.Type = actionType;
	a.TextPosition = g_caretCharacterIndex;
	a.BlockTop = selection.Top;
	a.BlockLeft = selection.Left;
	a.BlockBottom = selection.Bottom;
	a.BlockRight = selection.Right;
	a.OverwrittenChars.push_back(overwrittenChars);
	a.DragData.push_back(g_start);
	a.DragData.push_back(g_current);
	AddAction(windowHandles, a);
}

static bool TryMoveSelectedBlock(WindowHandles windowHandles, WPARAM wParam)
{
	if (!g_hasTextSelectionRectangle)
		return false;

	SignedRect selection = GetTextSelectionRegion();

	if (wParam == 37) //  selection.Left
	{
		if (static_cast<int>(selection.Left) <= 0)
			return false;

		// Add action to undo buffer
		{
			std::vector<wchar_t> overwrittenChars;
			for (int y = selection.Top; y <= selection.Bottom; ++y)
			{
				int x = selection.Left - 1;
				wchar_t chr = TryGetCharacter(y, x);
				overwrittenChars.push_back(chr);
			}
			AddMoveBlockAction(windowHandles, selection, Action::MoveBlockLeft, overwrittenChars);
		}

		for (int x = selection.Left - 1; x <= selection.Right; ++x)
		{
			for (int y = selection.Top; y <= selection.Bottom; ++y)
			{
				wchar_t chr = TryGetCharacter(y, x + 1);
				TrySetCharacter(y, x, chr);
			}
		}

		g_start.HitTest.textPosition--;
		g_current.HitTest.textPosition--;

		SetCaretCharacterIndex(g_caretCharacterIndex - 1, windowHandles.StatusBarLabel);
	}
	else if (wParam == 38) //  up
	{
		if (selection.Top <= 0)
			return false;

		// Add action to undo buffer
		{
			std::vector<wchar_t> overwrittenChars;
			for (int x = selection.Left; x <= selection.Right; ++x)
			{
				int y = selection.Top - 1;
				wchar_t chr = TryGetCharacter(y, x);
				overwrittenChars.push_back(chr);
			}
			AddMoveBlockAction(windowHandles, selection, Action::MoveBlockUp, overwrittenChars);
		}

		for (int y = selection.Top - 1; y <= selection.Bottom; ++y)
		{
			for (int x = selection.Left; x <= selection.Right; ++x)
			{
				wchar_t chr = TryGetCharacter(y + 1, x);
				TrySetCharacter(y, x, chr);
			}
		}

		g_start.HitTest.textPosition = g_textLineStarts[selection.Top - 1] + selection.Left;
		g_current.HitTest.textPosition = g_textLineStarts[selection.Bottom - 1] + selection.Right;

		int caretRow, caretColumn;
		GetRowAndColumnFromCharacterPosition(g_caretCharacterIndex, &caretRow, &caretColumn);
		caretRow--;
		SetCaretCharacterIndex(g_textLineStarts[caretRow] + caretColumn, windowHandles.StatusBarLabel);
	}
	else if (wParam == 39) // right
	{
		if (static_cast<int>(selection.Right) >= g_maxLineLength - 1)
			return false;

		// Add action to undo buffer
		{
			std::vector<wchar_t> overwrittenChars;
			for (int y = selection.Top; y <= selection.Bottom; ++y)
			{
				int x = selection.Right + 1;
				wchar_t chr = TryGetCharacter(y, x);
				overwrittenChars.push_back(chr);
			}
			AddMoveBlockAction(windowHandles, selection, Action::MoveBlockRight, overwrittenChars);
		}

		for (int x = static_cast<int>(selection.Right); x >= static_cast<int>(selection.Left)-1; --x)
		{
			for (int y = static_cast<int>(selection.Top); y <= static_cast<int>(selection.Bottom); ++y)
			{
				wchar_t chr = TryGetCharacter(y, x);
				TrySetCharacter(y, x + 1, chr);
			}
		}

		g_start.HitTest.textPosition++;
		g_current.HitTest.textPosition++;

		SetCaretCharacterIndex(g_caretCharacterIndex + 1, windowHandles.StatusBarLabel);
	}
	else if (wParam == 40) // down
	{
		if (static_cast<int>(selection.Bottom) >= static_cast<int>(g_textLineStarts.size()) - 1)
			return false;

		// Add action to undo buffer
		{
			std::vector<wchar_t> overwrittenChars;
			for (int x = selection.Left; x <= selection.Right; ++x)
			{
				int y = selection.Bottom + 1;
				wchar_t chr = TryGetCharacter(y, x);
				overwrittenChars.push_back(chr);
			}
			AddMoveBlockAction(windowHandles, selection, Action::MoveBlockDown, overwrittenChars);
		}

		for (int y = static_cast<int>(selection.Bottom); y >= static_cast<int>(selection.Top)-1; --y)
		{
			for (int x = static_cast<int>(selection.Left); x <= static_cast<int>(selection.Right); ++x)
			{
				wchar_t chr = TryGetCharacter(y, x);
				TrySetCharacter(y + 1, x, chr);
			}
		}

		g_start.HitTest.textPosition = g_textLineStarts[selection.Top + 1] + selection.Left;
		g_current.HitTest.textPosition = g_textLineStarts[selection.Bottom + 1] + selection.Right;

		int caretRow, caretColumn;
		GetRowAndColumnFromCharacterPosition(g_caretCharacterIndex, &caretRow, &caretColumn);
		caretRow++;
		SetCaretCharacterIndex(g_textLineStarts[caretRow] + caretColumn, windowHandles.StatusBarLabel);
	}

	FinalizeSelectionBoxMovement(windowHandles);
	return true;
}

void TryMoveViewWithKeyboard(WPARAM wParam)
{
	if (wParam == 37)
	{
		g_layoutInfo.AdjustPositionX(100);
	}
	else if (wParam == 38)
	{
		g_layoutInfo.AdjustPositionY(100);
	}
	else if (wParam == 39)
	{
		g_layoutInfo.AdjustPositionX(-100);
	}
	else if (wParam == 40)
	{
		g_layoutInfo.AdjustPositionY(100);
	}
	UpdateTextSelectionRectangle();
}

void TryMoveCaretDirectional(WindowHandles windowHandles, WPARAM wParam)
{
	int caretRow, caretColumn;
	GetRowAndColumnFromCharacterPosition(g_caretCharacterIndex, &caretRow, &caretColumn);

	if (wParam == 37) // left
	{
		if (caretColumn == 0)
		{
			if (caretRow == 0) {}
			else
			{
				caretRow--;
				caretColumn = g_maxLineLength - 1;
				SetCaretCharacterIndex(g_textLineStarts[caretRow] + caretColumn, windowHandles.StatusBarLabel);
			}
		}
		else
		{
			caretColumn--;
			SetCaretCharacterIndex(g_textLineStarts[caretRow] + caretColumn, windowHandles.StatusBarLabel);
		}
	}
	else if (wParam == 38) // up
	{
		if (caretRow == 0) {}
		else
		{
			caretRow--;
			SetCaretCharacterIndex(g_textLineStarts[caretRow] + caretColumn, windowHandles.StatusBarLabel);
		}
	}
	else if (wParam == 39) // right
	{
		if (caretColumn == g_maxLineLength-1)
		{
			if (caretRow == g_textLineStarts.size() - 1) {}
			else
			{
				caretRow++;
				caretColumn = 0;
				SetCaretCharacterIndex(g_textLineStarts[caretRow] + caretColumn, windowHandles.StatusBarLabel);
			}
		}
		else
		{
			caretColumn++;
			SetCaretCharacterIndex(g_textLineStarts[caretRow] + caretColumn, windowHandles.StatusBarLabel);
		}
	}
	else if (wParam == 40) // down
	{
		if (caretRow == g_textLineStarts.size() - 1) {}
		else
		{
			caretRow++;
			SetCaretCharacterIndex(g_textLineStarts[caretRow] + caretColumn, windowHandles.StatusBarLabel);
		}
	}
}

void WriteCharacterAtCaret(wchar_t chr, WindowHandles windowHandles)
{
	// Put a letter at the caret
	g_allText[g_caretCharacterIndex] = chr;
	RecreateTextLayout();

	int caretRow, caretColumn;
	GetRowAndColumnFromCharacterPosition(g_caretCharacterIndex, &caretRow, &caretColumn);

	if (caretColumn < g_maxLineLength-1)
	{
		SetCaretCharacterIndex(g_caretCharacterIndex + 1, windowHandles.StatusBarLabel);
	}
	else
	{
		// Need to move to new line
		if (caretRow < g_textLineStarts.size() - 1)
		{
			caretRow++;
			caretColumn = 0;
			SetCaretCharacterIndex(g_textLineStarts[caretRow] + caretColumn, windowHandles.StatusBarLabel);
		}
	}
}

void OnKeyDown(WindowHandles windowHandles, WPARAM wParam)
{
	if (!g_isFileLoaded)
		return;

	g_caretBlinkState = 0;

	if (g_keyOutput[wParam].Valid)
	{
		DisableTextSelectionRectangle(windowHandles);
		
		Action a;
		a.Type = Action::WriteCharacter;
		std::vector<wchar_t> line;
		line.push_back(g_allText[g_caretCharacterIndex]);
		a.OverwrittenChars.push_back(line);
		a.TextPosition = g_caretCharacterIndex;

		AddAction(windowHandles, a);

		wchar_t chr = g_isShiftDown ? g_keyOutput[wParam].Uppercase : g_keyOutput[wParam].Lowercase;
		WriteCharacterAtCaret(chr, windowHandles);
	}
	else if (wParam == 9) // Tab
	{
		DisableTextSelectionRectangle(windowHandles);

		Action a;
		a.Type = Action::WriteTab;

		std::vector<wchar_t> overwrittenLine;
		for (int i = 0; i < g_tabLength; ++i)
		{
			if (g_caretCharacterIndex + i >= g_allText.length())
				break;

			if (g_allText[g_caretCharacterIndex + i] == L'\n')
				break;

			overwrittenLine.push_back(g_allText[g_caretCharacterIndex + i]);
		}
		a.OverwrittenChars.push_back(overwrittenLine);
		a.TextPosition = g_caretCharacterIndex;

		AddAction(windowHandles, a);

		for (int i = 0; i < overwrittenLine.size(); ++i)
		{
			WriteCharacterAtCaret(L' ', windowHandles);
		}
	}
	else if (wParam == 8) // Backspace
	{
		DisableTextSelectionRectangle(windowHandles);

		if (g_caretCharacterIndex > 0)
		{
			SetCaretCharacterIndex(g_caretCharacterIndex - 1, windowHandles.StatusBarLabel);
		}

		Action a;
		a.Type = Action::Backspace;
		std::vector<wchar_t> line;
		line.push_back(g_allText[g_caretCharacterIndex]);
		a.OverwrittenChars.push_back(line);
		a.TextPosition = g_caretCharacterIndex;
		AddAction(windowHandles, a);

		g_allText[g_caretCharacterIndex] = L' ';
		RecreateTextLayout();
	}
	else if (wParam == 13) // Enter
	{
		int caretRow, caretColumn;
		GetRowAndColumnFromCharacterPosition(g_caretCharacterIndex, &caretRow, &caretColumn);

		if (caretRow < static_cast<int>(g_textLineStarts.size()) - 1)
		{
			caretRow++;
			caretColumn = 0;
			SetCaretCharacterIndex(g_textLineStarts[caretRow] + caretColumn, windowHandles.StatusBarLabel);
		}
	}
	else if (wParam == 16) // Shift key
	{
		g_isShiftDown = true;
	}
	else if (wParam == 17)
	{
		g_isCtrlDown = true;
	}
	else if (wParam >= 37 && wParam <= 40)
	{
		if (g_isShiftDown)
		{
			TryMoveSelectedBlock(windowHandles, wParam);
		}
		else if (g_isCtrlDown)
		{
			TryMoveViewWithKeyboard(wParam);
		}
		else
		{
			TryMoveCaretDirectional(windowHandles, wParam);
		}
	}
	else if (wParam == 46) // Delete key
	{
		DisableTextSelectionRectangle(windowHandles);
		DeleteBlock(windowHandles);
		RecreateTextLayout();
	}
}

void OnKeyUp(WindowHandles windowHandles, WPARAM wParam)
{
	if (wParam == 16) // Shift key
	{
		g_isShiftDown = false;
	}
	else if (wParam == 17)
	{
		g_isCtrlDown = false;
	}
	else if (wParam == 19) // Pause
	{
		g_isDebugBreaking = !g_isDebugBreaking;
	}
}

void Update()
{
	if (g_marchingAnts.size() == 0)
		return;

	g_marchingAntsIndex = (g_marchingAntsIndex + 1) % (5 * g_marchingAnts.size());
	g_caretBlinkState = (g_caretBlinkState + 1) % 50;
}

void OnHorizontalScroll(WindowHandles windowHandles, WPARAM wParam)
{
	WORD scrollPosition = HIWORD(wParam);
	WORD type = LOWORD(wParam);

	if (type != SB_THUMBPOSITION && type != TB_THUMBTRACK)
		return;

	g_layoutInfo.SetPositionX(-static_cast<float>(scrollPosition));

	UpdateTextSelectionRectangle();

	SCROLLINFO scrollInfo{};
	scrollInfo.cbSize = sizeof(scrollInfo);
	scrollInfo.fMask = SIF_POS;
	scrollInfo.nPos = scrollPosition;
	SetScrollInfo(windowHandles.Document, SB_HORZ, &scrollInfo, TRUE);

	InvalidateRect(windowHandles.Document, nullptr, FALSE);
}

void OnVerticalScroll(WindowHandles windowHandles, WPARAM wParam)
{
	WORD scrollPosition = HIWORD(wParam);	
	WORD type = LOWORD(wParam);

	if (type != SB_THUMBPOSITION && type != TB_THUMBTRACK)
		return;

	g_layoutInfo.SetPositionY(-static_cast<float>(scrollPosition));

	UpdateTextSelectionRectangle();
	
	SCROLLINFO scrollInfo{};
	scrollInfo.cbSize = sizeof(scrollInfo);
	scrollInfo.fMask = SIF_POS;
	scrollInfo.nPos = scrollPosition;
	SetScrollInfo(windowHandles.Document, SB_VERT, &scrollInfo, TRUE);

	InvalidateRect(windowHandles.Document, nullptr, FALSE);
}

void OnMouseWheel(WindowHandles windowHandles, WPARAM wParam)
{
	if (!g_isFileLoaded)
		return;

	if (g_verticalScrollLimit < 0)
		return; // Whole document fits in window

	SCROLLINFO scrollInfo{};
	scrollInfo.cbSize = sizeof(scrollInfo);
	scrollInfo.fMask = SIF_POS;
	VerifyBool(GetScrollInfo(windowHandles.Document, SB_VERT, &scrollInfo));

	int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
	int additionalScrollAmount = wheelDelta;
	int newScrollAmount = scrollInfo.nPos - additionalScrollAmount;

	if (newScrollAmount < 0) 
		newScrollAmount = 0;
	
	if (newScrollAmount > g_verticalScrollLimit)
		newScrollAmount = g_verticalScrollLimit;

	g_layoutInfo.SetPositionY(-static_cast<float>(newScrollAmount));

	UpdateTextSelectionRectangle();

	scrollInfo.nPos = newScrollAmount;
	SetScrollInfo(windowHandles.Document, SB_VERT, &scrollInfo, TRUE);
}

void OnOpen(WindowHandles windowHandles)
{
	g_isCtrlDown = false;

	TCHAR documentsPath[MAX_PATH];

	VerifyHR(SHGetFolderPath(NULL,
		CSIDL_PERSONAL | CSIDL_FLAG_CREATE,
		NULL,
		0,
		documentsPath));

	OPENFILENAME ofn;       // common dialog box structure
	wchar_t szFile[MAX_PATH];

	ZeroMemory(&ofn, sizeof(ofn));
	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = windowHandles.TopLevel;
	ofn.lpstrFile = szFile;

	// Set lpstrFile[0] to '\0' so that GetOpenFileName does not 
	// use the contents of szFile to initialize itself.
	ofn.lpstrFile[0] = L'\0';
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = L"All\0*.*\0Text\0*.TXT\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = documentsPath;
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

	// Display the Open dialog box. 

	if (!!GetOpenFileName(&ofn))
	{
		SetCurrentFileNameAndUpdateWindowTitle(windowHandles, ofn.lpstrFile);

		LoadOrCreateFileResult loadFileResult = LoadOrCreateFileContents(ofn.lpstrFile);

		InitializeDocument(windowHandles, loadFileResult);

		EnableMenuItem(windowHandles, ID_FILE_REFRESH);
		EnableMenuItem(windowHandles, ID_FILE_SAVE);
	}
}

void OnSave(WindowHandles windowHandles)
{
	g_isCtrlDown = false;

	{
		std::wofstream out(g_fileFullPath);
		out << g_allText;
	}

	MessageBox(nullptr, L"Save completed.", L"ColumnMode", MB_OK);
	
	g_hasUnsavedChanges = false;
	UpdateWindowTitle(windowHandles);
}

void OnSaveAs(WindowHandles windowHandles)
{
	g_isCtrlDown = false;

	TCHAR documentsPath[MAX_PATH];

	VerifyHR(SHGetFolderPath(NULL,
		CSIDL_PERSONAL | CSIDL_FLAG_CREATE,
		NULL,
		0,
		documentsPath));

	OPENFILENAME ofn;
	ZeroMemory(&ofn, sizeof(ofn));

	wchar_t szFile[MAX_PATH];
	wcscpy_s(szFile, L"Untitled.txt");

	ofn.lStructSize = sizeof(ofn);
	ofn.hwndOwner = windowHandles.TopLevel;
	ofn.lpstrFile = szFile;
	ofn.nMaxFile = sizeof(szFile);
	ofn.lpstrFilter = L"All\0*.*\0Text\0*.TXT\0";
	ofn.nFilterIndex = 1;
	ofn.lpstrFileTitle = NULL;
	ofn.nMaxFileTitle = 0;
	ofn.lpstrInitialDir = documentsPath;
	ofn.lpstrDefExt = L"txt";
	ofn.Flags = OFN_PATHMUSTEXIST | OFN_OVERWRITEPROMPT;
	
	if (!!GetSaveFileName(&ofn))
	{
		std::wofstream out(ofn.lpstrFile);
		out << g_allText;

		g_hasUnsavedChanges = false;

		SetCurrentFileNameAndUpdateWindowTitle(windowHandles, ofn.lpstrFile);
	}
}

void OnUndo(WindowHandles windowHandles)
{
	auto const& top = g_undoBuffer.back();
	
	// Pop item off stack
	if (top.Type == Action::WriteCharacter)
	{
		g_allText[top.TextPosition] = top.OverwrittenChars[0][0];
		RecreateTextLayout();
		SetCaretCharacterIndex(top.TextPosition, windowHandles.StatusBarLabel);
	}
	else if (top.Type == Action::WriteTab)
	{
		for (int i = 0; i < top.OverwrittenChars[0].size(); ++i)
		{
			wchar_t ch = top.OverwrittenChars[0][i];
			g_allText[top.TextPosition + i] = ch;
		}

		RecreateTextLayout();
		SetCaretCharacterIndex(top.TextPosition, windowHandles.StatusBarLabel);
	}
	else if (top.Type == Action::Backspace)
	{
		g_allText[top.TextPosition] = top.OverwrittenChars[0][0];
		RecreateTextLayout();

		UINT newTextPosition = top.TextPosition;

		if (top.TextPosition < g_allText.length() - 1)
		{
			newTextPosition++;
		}
		SetCaretCharacterIndex(newTextPosition, windowHandles.StatusBarLabel);
	}
	else if (top.Type == Action::PasteToPosition)
	{
		int caretRow, caretColumn;
		GetRowAndColumnFromCharacterPosition(top.TextPosition, &caretRow, &caretColumn);
		
		for (int lineIndex = 0; lineIndex < static_cast<int>(top.OverwrittenChars.size()); lineIndex++)
		{
			for (int columnIndex = 0; columnIndex < static_cast<int>(top.OverwrittenChars[lineIndex].size()); ++columnIndex)
			{
				TrySetCharacter(
					caretRow + lineIndex,
					caretColumn + columnIndex,
					top.OverwrittenChars[lineIndex][columnIndex]);
			}
		}

		RecreateTextLayout();

		SetCaretCharacterIndex(top.TextPosition, windowHandles.StatusBarLabel);
	}
	else if (top.Type == Action::PasteToBlock)
	{
		for (int lineIndex = 0; lineIndex < static_cast<int>(top.OverwrittenChars.size()); lineIndex++)
		{
			for (int columnIndex = 0; columnIndex < static_cast<int>(top.OverwrittenChars[lineIndex].size()); ++columnIndex)
			{
				TrySetCharacter(
					top.BlockTop + lineIndex,
					top.BlockLeft + columnIndex,
					top.OverwrittenChars[lineIndex][columnIndex]);
			}
		}

		RecreateTextLayout();

		SetCaretCharacterIndex(top.TextPosition, windowHandles.StatusBarLabel);

		g_start = top.DragData[0];
		g_current = top.DragData[1];
		EnableTextSelectionRectangle(windowHandles);
		UpdateTextSelectionRectangle();
	}
	else if (top.Type == Action::DeleteBlock)
	{
		for (int lineIndex = 0; lineIndex < static_cast<int>(top.OverwrittenChars.size()); lineIndex++)
		{
			for (int columnIndex = 0; columnIndex < static_cast<int>(top.OverwrittenChars[lineIndex].size()); ++columnIndex)
			{
				TrySetCharacter(
					top.BlockTop + lineIndex,
					top.BlockLeft + columnIndex,
					top.OverwrittenChars[lineIndex][columnIndex]);
			}
		}

		RecreateTextLayout();
		SetCaretCharacterIndex(top.TextPosition, windowHandles.StatusBarLabel);

		g_start = top.DragData[0];
		g_current = top.DragData[1];
		EnableTextSelectionRectangle(windowHandles);
		UpdateTextSelectionRectangle();
	}
	else if (top.Type == Action::MoveBlockLeft)
	{
		// Shift to the right
		for (int y = top.BlockTop; y <= top.BlockBottom; ++y)
		{
			for (int x = top.BlockRight; x >= top.BlockLeft; --x)
			{
				wchar_t ch = TryGetCharacter(y, x - 1);
				TrySetCharacter(y, x, ch);
			}
		}

		// Fill in overwritten chars
		int idx = 0;
		for (int y = top.BlockTop; y <= top.BlockBottom; ++y)
		{
			int x = top.BlockLeft-1;
			wchar_t ch = top.OverwrittenChars[0][idx];
			TrySetCharacter(y, x, ch);
			++idx;
		}

		RecreateTextLayout();
		SetCaretCharacterIndex(top.TextPosition, windowHandles.StatusBarLabel);

		g_start = top.DragData[0];
		g_current = top.DragData[1];
		EnableTextSelectionRectangle(windowHandles);
		UpdateTextSelectionRectangle();
	}
	else if (top.Type == Action::MoveBlockUp)
	{
		// Shift down
		for (int x = top.BlockLeft; x <= top.BlockRight; ++x)
		{
			for (int y = top.BlockBottom; y >= top.BlockTop; --y)
			{
				wchar_t ch = TryGetCharacter(y - 1, x);
				TrySetCharacter(y, x, ch);
			}
		}

		// Fill in overwritten chars
		int idx = 0;
		for (int x = top.BlockLeft; x <= top.BlockRight; ++x)
		{
			int y = top.BlockTop-1;
			wchar_t ch = top.OverwrittenChars[0][idx];
			TrySetCharacter(y, x, ch);
			++idx;
		}

		RecreateTextLayout();
		SetCaretCharacterIndex(top.TextPosition, windowHandles.StatusBarLabel);

		g_start = top.DragData[0];
		g_current = top.DragData[1];
		EnableTextSelectionRectangle(windowHandles);
		UpdateTextSelectionRectangle();
	}
	else if (top.Type == Action::MoveBlockRight)
	{
		// Shift to the left
		for (int y = top.BlockTop; y <= top.BlockBottom; ++y)
		{
			for (int x = top.BlockLeft; x <= top.BlockRight; ++x)
			{
				wchar_t ch = TryGetCharacter(y, x + 1);
				TrySetCharacter(y, x, ch);
			}
		}

		// Fill in overwritten chars
		int idx = 0;
		for (int y = top.BlockTop; y <= top.BlockBottom; ++y)
		{
			int x = top.BlockRight + 1;
			wchar_t ch = top.OverwrittenChars[0][idx];
			TrySetCharacter(y, x, ch);
			++idx;
		}

		RecreateTextLayout();
		SetCaretCharacterIndex(top.TextPosition, windowHandles.StatusBarLabel);

		g_start = top.DragData[0];
		g_current = top.DragData[1];
		EnableTextSelectionRectangle(windowHandles);
		UpdateTextSelectionRectangle();
	}
	else if (top.Type == Action::MoveBlockDown)
	{
		// Shift up
		for (int x = top.BlockLeft; x <= top.BlockRight; ++x)
		{
			for (int y = top.BlockTop; y <= top.BlockBottom; ++y)
			{
				wchar_t ch = TryGetCharacter(y + 1, x);
				TrySetCharacter(y, x, ch);
			}
		}

		// Fill in overwritten chars
		int idx = 0;
		for (int x = top.BlockLeft; x <= top.BlockRight; ++x)
		{
			int y = top.BlockBottom+1;
			wchar_t ch = top.OverwrittenChars[0][idx];
			TrySetCharacter(y, x, ch);
			++idx;
		}

		RecreateTextLayout();
		SetCaretCharacterIndex(top.TextPosition, windowHandles.StatusBarLabel);

		g_start = top.DragData[0];
		g_current = top.DragData[1];
		EnableTextSelectionRectangle(windowHandles);
		UpdateTextSelectionRectangle();
	}
	else
	{
		assert(false); // Unexpected
	}

	g_undoBuffer.pop_back();

	if (g_undoBuffer.size() == 0)
		DisableMenuItem(windowHandles, ID_EDIT_UNDO);
}

void CopySelectionToClipboard()
{
	assert(g_hasTextSelectionRectangle);

	SignedRect selection = GetTextSelectionRegion();

	std::vector<wchar_t> stringData;

	for (int y = selection.Top; y <= selection.Bottom; y++)
	{
		for (int x = selection.Left; x <= selection.Right; x++)
		{
			stringData.push_back(TryGetCharacter(y, x));
		}
		stringData.push_back(L'\n');
	}

	size_t bufferSize = (stringData.size() + 1) * sizeof(wchar_t); // Account for null term

	HGLOBAL hMem = GlobalAlloc(GMEM_MOVEABLE, bufferSize);
	memcpy(GlobalLock(hMem), stringData.data(), bufferSize);
	(void)(GlobalUnlock(hMem)); // Returns lock status, not success/fail

	VerifyBool(OpenClipboard(0));
	VerifyBool(EmptyClipboard());
	SetClipboardData(CF_UNICODETEXT, hMem);
	VerifyBool(CloseClipboard());
}

void OnDelete(WindowHandles windowHandles)
{
	DeleteBlock(windowHandles);

	RecreateTextLayout();
}

void OnCut(WindowHandles windowHandles)
{
	CopySelectionToClipboard();

	DeleteBlock(windowHandles);

	RecreateTextLayout();
}

void OnCopy(WindowHandles windowHandles)
{
	CopySelectionToClipboard();
}

void OnPaste(WindowHandles windowHandles)
{
	std::wstring str;

	OpenClipboard(0);
	
	HANDLE hClipboardData = GetClipboardData(CF_UNICODETEXT);
	if (hClipboardData != 0)
	{
		wchar_t *pchData = (wchar_t*)GlobalLock(hClipboardData);
		str = pchData;
		GlobalUnlock(hClipboardData);
	}
	CloseClipboard();

	if (str.length() == 0)
		return;

	// Divide up into lines
	std::vector<std::wstring> lines;
	size_t start = 0;

	while (1)
	{
		size_t newlineIndex = str.find(L'\n', start);
		
		if (newlineIndex == std::wstring::npos)
		{
			lines.push_back(str.substr(start, str.length() - start));
			break;
		}
		else
		{
			size_t ignorableSuffixLength = 0;
			if (newlineIndex > 0 && str[newlineIndex - 1] == '\r')
			{
				ignorableSuffixLength = 1;
			}

			lines.push_back(str.substr(start, newlineIndex - start - ignorableSuffixLength));
			newlineIndex++;
			start = newlineIndex;
		}
	}

	Action a{};

	if (g_hasTextSelectionRectangle)
	{
		a.Type = Action::PasteToBlock;

		SignedRect selection = GetTextSelectionRegion();

		a.TextPosition = g_caretCharacterIndex;
		a.BlockLeft = selection.Left;
		a.BlockTop = selection.Top;
		a.BlockRight = selection.Right;
		a.BlockBottom = selection.Bottom;
		a.DragData.push_back(g_start);
		a.DragData.push_back(g_current);
		
		for (int lineIndex = 0; lineIndex < lines.size() && selection.Top + lineIndex <= selection.Bottom; ++lineIndex)
		{
			std::vector<wchar_t> overwrittenLine;
			for (int columnIndex = 0; columnIndex < lines[lineIndex].size() && selection.Left + columnIndex <= selection.Right; ++columnIndex)
			{
				int row = selection.Top + lineIndex;
				int col = selection.Left + columnIndex;
				overwrittenLine.push_back(TryGetCharacter(row, col));
				TrySetCharacter(row, col, lines[lineIndex][columnIndex]);
			}
			a.OverwrittenChars.push_back(overwrittenLine);
		}
	}
	else
	{
		a.Type = Action::PasteToPosition;

		int caretRow, caretColumn;
		GetRowAndColumnFromCharacterPosition(g_caretCharacterIndex, &caretRow, &caretColumn);

		a.TextPosition = g_caretCharacterIndex;
		
		for (int lineIndex = 0; lineIndex < lines.size(); ++lineIndex)
		{
			std::vector<wchar_t> overwrittenLine;
			for (int columnIndex = 0; columnIndex < lines[lineIndex].size(); ++columnIndex)
			{
				int row = caretRow + lineIndex;
				int col = caretColumn + columnIndex;
				overwrittenLine.push_back(TryGetCharacter(row, col));
				TrySetCharacter(row, col, lines[lineIndex][columnIndex]);
			}
			a.OverwrittenChars.push_back(overwrittenLine);
		}		
	}

	AddAction(windowHandles, a);

	RecreateTextLayout();
}

void OnRefresh(WindowHandles windowHandles)
{
	LoadOrCreateFileResult loadFileResult = LoadOrCreateFileContents(g_fileFullPath.c_str());

	InitializeDocument(windowHandles, loadFileResult);
}

void OnClipboardContentsChanged(WindowHandles windowHandles)
{
	UpdatePasteEnablement(windowHandles);
}

struct StreamAndHR
{
	ComPtr<IStream> Stream;
	HRESULT HR;
};
StreamAndHR GetPrintTicketFromDevmode(
	_In_ PCTSTR printerName,
	_In_reads_bytes_(devModesize) PDEVMODE devMode,
	WORD devModesize)
{
	HPTPROVIDER provider = nullptr;

	StreamAndHR result;

	// Allocate stream for print ticket.
	result.HR = CreateStreamOnHGlobal(nullptr, TRUE, &result.Stream);

	if (SUCCEEDED(result.HR))
	{
		result.HR = PTOpenProvider(printerName, 1, &provider);
	}

	// Get PrintTicket from DEVMODE.
	if (SUCCEEDED(result.HR))
	{
		result.HR = PTConvertDevModeToPrintTicket(provider, devModesize, devMode, kPTJobScope, result.Stream.Get());
	}

	if (provider)
	{
		PTCloseProvider(provider);
	}

	return result;
}

void OnPrint(WindowHandles windowHandles)
{
	float PAGE_WIDTH_IN_DIPS = 1000;
	float PAGE_HEIGHT_IN_DIPS = 1000;
	float m_pageHeight = 1000;
	float m_pageWidth = 1000;
	ComPtr<IStream> m_jobPrintTicketStream = {};
	IPrintDocumentPackageTarget* m_documentTarget = {};
	ComPtr<IWICImagingFactory2> m_wicFactory = {};
	ComPtr<ID2D1PrintControl> m_printControl = {};
	ComPtr<ID2D1DeviceContext> m_d2dContextForPrint;

	HRESULT hr = S_OK;
	WCHAR messageBuffer[512] = { 0 };

	CoInitialize(NULL);

	hr = CoCreateInstance(
		CLSID_WICImagingFactory,
		nullptr,
		CLSCTX_INPROC_SERVER,
		IID_PPV_ARGS(&m_wicFactory)
	);

	// Bring up Print Dialog and receive user print settings.
	PRINTDLGEX printDialogEx = { 0 };
	printDialogEx.lStructSize = sizeof(PRINTDLGEX);
	printDialogEx.Flags = PD_HIDEPRINTTOFILE | PD_NOPAGENUMS | PD_NOSELECTION | PD_NOCURRENTPAGE | PD_USEDEVMODECOPIESANDCOLLATE;
	printDialogEx.hwndOwner = windowHandles.TopLevel;
	printDialogEx.nStartPage = START_PAGE_GENERAL;

	HRESULT hrPrintDlgEx = PrintDlgEx(&printDialogEx);

	if (FAILED(hrPrintDlgEx))
	{
		std::wstringstream strm;
		strm << L"Error 0x" << std::hex << hrPrintDlgEx << L"ccured during printer selection and/or setup.";

		MessageBox(windowHandles.TopLevel, messageBuffer, L"Message", MB_OK);
		hr = hrPrintDlgEx;
	}
	else if (printDialogEx.dwResultAction == PD_RESULT_APPLY)
	{
		// User clicks the Apply button and later clicks the Cancel button.
		// For simpicity, this sample skips print settings recording.
		hr = E_FAIL;
	}
	else if (printDialogEx.dwResultAction == PD_RESULT_CANCEL)
	{
		// User clicks the Cancel button.
		hr = E_FAIL;
		return; // Nothing to do
	}

	// Retrieve DEVNAMES from print dialog.
	DEVNAMES* devNames = nullptr;
	if (SUCCEEDED(hr))
	{
		if (printDialogEx.hDevNames != nullptr)
		{
			devNames = reinterpret_cast<DEVNAMES*>(GlobalLock(printDialogEx.hDevNames));
			if (devNames == nullptr)
			{
				hr = HRESULT_FROM_WIN32(GetLastError());
			}
		}
		else
		{
			hr = E_HANDLE;
		}
	}

	// Retrieve user settings from print dialog.
	DEVMODE* devMode = nullptr;
	PCWSTR printerName = nullptr;
	if (SUCCEEDED(hr))
	{
		printerName = reinterpret_cast<PCWSTR>(devNames) + devNames->wDeviceOffset;

		if (printDialogEx.hDevMode != nullptr)
		{
			devMode = reinterpret_cast<DEVMODE*>(GlobalLock(printDialogEx.hDevMode));   // retrieve DevMode

			if (devMode)
			{
				// Must check corresponding flags in devMode->dmFields
				if ((devMode->dmFields & DM_PAPERLENGTH) && (devMode->dmFields & DM_PAPERWIDTH))
				{
					// Convert 1/10 of a millimeter DEVMODE unit to 1/96 of inch D2D unit
					m_pageHeight = devMode->dmPaperLength / 254.0f * 96.0f;
					m_pageWidth = devMode->dmPaperWidth / 254.0f * 96.0f;
				}
				else
				{
					// Use default values if the user does not specify page size.
					m_pageHeight = PAGE_HEIGHT_IN_DIPS;
					m_pageWidth = PAGE_WIDTH_IN_DIPS;
				}
			}
			else
			{
				hr = HRESULT_FROM_WIN32(GetLastError());
			}
		}
		else
		{
			hr = E_HANDLE;
		}
	}

	// Convert DEVMODE to a job print ticket stream.
	if (SUCCEEDED(hr))
	{
		StreamAndHR streamAndHR = GetPrintTicketFromDevmode(
			printerName,
			devMode,
			devMode->dmSize + devMode->dmDriverExtra // Size of DEVMODE in bytes, including private driver data.
		);

		hr = streamAndHR.HR;
		m_jobPrintTicketStream = streamAndHR.Stream;
	}

	// Create a factory for document print job.
	ComPtr<IPrintDocumentPackageTargetFactory> documentTargetFactory;
	if (SUCCEEDED(hr))
	{
		hr = ::CoCreateInstance(
			__uuidof(PrintDocumentPackageTargetFactory),
			nullptr,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&documentTargetFactory)
		);
	}

	// Initialize the print subsystem and get a package target.
	if (SUCCEEDED(hr))
	{
		std::wstring jobName = g_fileName;

		hr = documentTargetFactory->CreateDocumentPackageTargetForPrintJob(
			printerName,
			jobName.c_str(),
			nullptr, // job output stream; when nullptr, send to printer
			m_jobPrintTicketStream.Get(),
			&m_documentTarget
		);
	}

	// Create a new print control linked to the package target.
	if (SUCCEEDED(hr))
	{
		hr = g_d2dDevice->CreatePrintControl(
			m_wicFactory.Get(),
			m_documentTarget,
			nullptr,
			&m_printControl
		);
	}

	ComPtr<ID2D1CommandList> printedWork;
	VerifyHR(g_hwndRenderTarget->CreateCommandList(&printedWork));

	if (!m_d2dContextForPrint)
	{
		hr = g_d2dDevice->CreateDeviceContext(
			D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
			&m_d2dContextForPrint
		);
	}
	m_d2dContextForPrint->SetTarget(printedWork.Get());
	m_d2dContextForPrint->BeginDraw();
	m_d2dContextForPrint->Clear();
	m_d2dContextForPrint->DrawTextLayout(D2D1::Point2F(0, 0), g_textLayout.Get(), g_blackBrush.Get());
	VerifyHR(m_d2dContextForPrint->EndDraw());
	VerifyHR(printedWork->Close());

	VerifyHR(m_printControl->AddPage(printedWork.Get(), D2D1::SizeF(m_pageWidth, m_pageHeight), nullptr));

	// Close the print control to complete a print job.
	VerifyHR(m_printControl->Close());

	// Release resources.
	if (devMode)
	{
		GlobalUnlock(printDialogEx.hDevMode);
		devMode = nullptr;
	}
	if (devNames)
	{
		GlobalUnlock(printDialogEx.hDevNames);
		devNames = nullptr;
	}
	if (printDialogEx.hDevNames)
	{
		GlobalFree(printDialogEx.hDevNames);
	}
	if (printDialogEx.hDevMode)
	{
		GlobalFree(printDialogEx.hDevMode);
	}

	VerifyHR(hr);
}

void OnInitializeDocumentProperties(HWND hDlg)
{
	assert(g_textLineStarts.size() <= INT_MAX);
	int lineCount = static_cast<int>(g_textLineStarts.size());
	int columnCount = g_maxLineLength;

	wchar_t lineCountBuffer[32]{};
	_itow_s(lineCount, lineCountBuffer, 10);

	wchar_t columnCountBuffer[32]{};
	_itow_s(columnCount, columnCountBuffer, 10);
	
	HWND lineCountDlg = GetDlgItem(hDlg, IDC_LINECOUNT_EDITBOX);
	Static_SetText(lineCountDlg, lineCountBuffer);

	HWND columnCountDlg = GetDlgItem(hDlg, IDC_COLUMNCOUNT_EDITBOX);
	Static_SetText(columnCountDlg, columnCountBuffer);
}

bool GetValidTextBoxInteger(HWND hDlg, int textBoxIdentifier, long int* result)
{
	*result = 0;

	HWND dlgHwnd = GetDlgItem(hDlg, textBoxIdentifier);
	wchar_t textBuffer[32]{};
	Static_GetText(dlgHwnd, textBuffer, _countof(textBuffer));
	wchar_t* bufferEnd;

	long int newCount = wcstol(textBuffer, &bufferEnd, 10);
	if (newCount <= 0)
	{
		return false;
	}

	size_t bufferLength = wcslen(textBuffer);
	if (bufferLength > INT_MAX)
		return false;

	if (bufferEnd - textBuffer < static_cast<int>(bufferLength))
	{
		return false;
	}
	*result = newCount;
	return true;
}

std::wstring GetPaddingString(int length)
{
	std::wstring padding;

	for (int i = 0; i < length; ++i)
		padding.append(L" ");

	return padding;
}

std::wstring GetLineFromDelimiters(size_t lastNewline, size_t newlineIndex, size_t lengthLimit)
{
	size_t startIndex;
	size_t count;
	if (lastNewline == 0)
	{
		startIndex = 0;
		count = newlineIndex;
	}
	else
	{
		startIndex = lastNewline + 1;
		count = newlineIndex - lastNewline - 1;
	}

	if (count > lengthLimit)
		count = lengthLimit;

	std::wstring line = g_allText.substr(startIndex, count);
	return line;
}

void AdjustColumnCount(long int currentColumnCount, int newColumnCount)
{
	std::wstring newAllText;
	int newMaxLineLength;
	std::vector<int> newTextLineStarts;

	newMaxLineLength = newColumnCount;

	size_t lastNewline = 0;

	int difference = newColumnCount - currentColumnCount;
	std::wstring padding;
	if (difference > 0)
	{
		padding = GetPaddingString(difference);
	}

	while (lastNewline != -1 && lastNewline < g_allText.length())
	{
		newTextLineStarts.push_back(static_cast<int>(newAllText.length()));

		size_t newlineIndex = g_allText.find(L'\n', lastNewline + 1);

		if (difference < 0)
		{
			// Delete right side of line
			std::wstring clippedLine = GetLineFromDelimiters(lastNewline, newlineIndex, newColumnCount);
			clippedLine.push_back(L'\n');
			newAllText.append(clippedLine);
		}
		else
		{
			// Append to right side of line
			assert(difference > 0);
			assert(newlineIndex > lastNewline + 1);

			std::wstring extendedLine = GetLineFromDelimiters(lastNewline, newlineIndex, INT_MAX);
			extendedLine.append(padding);
			extendedLine.push_back(L'\n');;
			newAllText.append(extendedLine);
		}

		lastNewline = newlineIndex;
	}
	newAllText.pop_back(); // Remove terminating newline

	g_allText = newAllText;
	g_maxLineLength = newMaxLineLength;
	g_textLineStarts = newTextLineStarts;
}

void AdjustLineCount(long int currentLineCount, int newLineCount)
{
	if (newLineCount < currentLineCount)
	{
		// Trim lines
		int difference = currentLineCount - newLineCount;

		size_t newlineIndex = g_allText.length();
		for (int i = 0; i < difference; ++i)
		{
			// Don't include the last-found newline itself in our search
			size_t lastNewline = g_allText.rfind(L'\n', newlineIndex-1);
			newlineIndex = lastNewline;
		}
		g_allText = g_allText.substr(0, newlineIndex);
		g_textLineStarts.erase(g_textLineStarts.begin() + g_textLineStarts.size() - difference, g_textLineStarts.end());
	}
	else
	{
		assert(newLineCount > currentLineCount);
		int difference = newLineCount - currentLineCount;
		
		std::wstring blankLine = GetPaddingString(g_maxLineLength);	
		blankLine.push_back(L'\n');

		g_allText.push_back(L'\n');
		for (int i = 0; i < difference; ++i)
		{
			g_textLineStarts.push_back(static_cast<int>(g_allText.length()));
			g_allText.append(blankLine);
		}

		g_allText.pop_back();
	}
}

void OnConfirmDocumentProperties(WindowHandles windowHandles, HWND hDlg, WPARAM wParam)
{
	long newLineCount;
	if (!GetValidTextBoxInteger(hDlg, IDC_LINECOUNT_EDITBOX, &newLineCount))
	{
		MessageBox(hDlg, L"An invalid line count was specified.", L"ColumnMode", MB_OK);
		return;
	}

	long newColumnCount;
	if (!GetValidTextBoxInteger(hDlg, IDC_COLUMNCOUNT_EDITBOX, &newColumnCount))
	{
		MessageBox(hDlg, L"An invalid column count was specified.", L"ColumnMode", MB_OK);
		return;
	}

	long currentLineCount = static_cast<long>(g_textLineStarts.size());
	long currentColumnCount = g_maxLineLength;

	if (newLineCount < currentLineCount || newColumnCount < currentColumnCount)
	{
		int dialogResult = MessageBox(hDlg, L"The specified document size is smaller than the current size.\nSome clipping will occur.", L"ColumnMode", MB_OKCANCEL);
		if (dialogResult == IDCANCEL)
		{
			return;
		}
		assert(dialogResult == IDOK);
	}

	bool columnCountChanged = newColumnCount != currentColumnCount;
	bool lineCountChanged = newLineCount != currentLineCount;

	if (columnCountChanged || lineCountChanged)
	{
		if (columnCountChanged)
		{
			AdjustColumnCount(currentColumnCount, newColumnCount);
		}

		if (lineCountChanged)
		{
			AdjustLineCount(currentLineCount, newLineCount);
		}

		RecreateTextLayout();

		SetCaretCharacterIndex(0, windowHandles.StatusBarLabel);
		g_hasTextSelectionRectangle = false;

		SetScrollPositions(windowHandles);
		g_hasUnsavedChanges = true;
		UpdateWindowTitle(windowHandles);
	}

	EndDialog(hDlg, LOWORD(wParam));

}

void OnClose(WindowHandles windowHandles)
{
	if (g_hasUnsavedChanges)
	{
		std::wstring dialogText;
		if (g_fileName.length() > 0)
		{
			dialogText.append(L"Save changes to ");
			dialogText.append(g_fileName);
		}
		else
		{
			dialogText.append(L"Save this document");
		}
		dialogText.append(L"?");

		int dialogResult = MessageBox(
			nullptr,
			dialogText.c_str(),
			L"ColumnMode",
			MB_YESNOCANCEL);

		if (dialogResult == IDCANCEL)
			return;

		if (dialogResult == IDYES)
		{
			if (g_fileName.length() > 0)
			{
				OnSave(windowHandles);
			}
			else
			{
				OnSaveAs(windowHandles);
			}
		}
		else
		{
			assert(dialogResult == IDNO);
		}
	}

	DestroyWindow(windowHandles.TopLevel);
}