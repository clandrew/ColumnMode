#include "stdafx.h"
#include "Program.h"
#include "Resource.h"
#include "Verify.h"
#include "LayoutInfo.h"
#include "PluginManager.h"
#include "WindowManager.h"
#include "Theme.h"

void OpenImpl(WindowHandles windowHandles, LPCWSTR fileName);

const float g_fontSize = 12.0f;
const int g_tabLength = 4;

bool g_isFileLoaded;
std::wstring g_fileFullPath;
std::wstring g_fileName;
std::wstring g_windowTitleFileNamePrefix;
bool g_hasUnsavedChanges;

ColumnMode::PluginManager g_pluginManager;
ColumnMode::WindowManager g_windowManager;
ColumnMode::ThemeManager g_themeManager;
ColumnMode::FindTool g_findTool;

ComPtr<ID2D1Factory1> g_d2dFactory;

bool g_needsDeviceRecreation;

// Device dependent resources
ComPtr<IDXGISwapChain> g_swapChain;
ComPtr<ID2D1Device> g_d2dDevice;
ComPtr<ID2D1DeviceContext> g_hwndRenderTarget;

ColumnMode::BrushCache g_brushCache;
ColumnMode::Theme g_theme;

UINT g_marchingAntsIndex;
std::vector<ComPtr<ID2D1StrokeStyle>> g_marchingAnts;

ComPtr<IDWriteFactory> g_dwriteFactory;
ComPtr<IDWriteTextFormat> g_textFormat;
ComPtr<IDWriteTextLayout> g_textLayout;

D2D1_SIZE_U g_documentWindowSize;

LayoutInfo g_layoutInfo;

struct Action
{
	enum ActionType
	{
		WriteCharacter_DiagramMode,
		WriteCharacter_TextMode,
		WriteTab,
		Backspace_DiagramMode,
		Backspace_TextMode,
		DeleteBlock,
		DeleteCharacter_TextMode,
		PasteToPosition,
		PasteToBlock,
		MoveBlockLeft,
		MoveBlockUp,
		MoveBlockRight,
		MoveBlockDown,
		TextModeCarriageReturn
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

enum class Mode
{
	DiagramMode,
	TextMode
};

class Status
{
	int m_caretRow;
	int m_caretColumn;
	Mode m_mode;

	void RefreshStatusBar(HWND statusBarLabelHwnd)
	{
		// Show label of row and column numbers, 1-indexed.
		std::wstringstream label;
		label << L"Row: " << (m_caretRow + 1) << "        Col: " << (m_caretColumn + 1);
		label << L"        Mode: " << (m_mode == Mode::TextMode ? L"Text" : L"Diagram");

		Static_SetText(statusBarLabelHwnd, label.str().c_str());
	}

public:
	Status()
		: m_caretRow(0)
		, m_caretColumn(0)
		, m_mode(Mode::DiagramMode)
	{
	}

	void CaretPositionChanged(int caretRow, int caretColumn, HWND statusBarLabelHwnd)
	{
		m_caretRow = caretRow;
		m_caretColumn = caretColumn;
		RefreshStatusBar(statusBarLabelHwnd);
	}

	void SetMode(Mode newMode, HWND statusBarLabelHwnd)
	{
		m_mode = newMode;
		RefreshStatusBar(statusBarLabelHwnd);
	}

	Mode GetMode() const
	{
		return m_mode;
	}

} g_status;

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

// true if there is a valid character position at row, column. 
// outCharacterPosition will be set if true, else it will be the value passed in
bool GetCharacterPositionFromRowAndColumn(int row, int column, UINT32& outCharacterPosition)
{
	if (!IsValidPosition(row, column))
	{
		return false;
	}
	outCharacterPosition = g_textLineStarts[row] + column;
	return true;
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

	int topVisibleLine = g_layoutInfo.GetVisibleLineTop();
	int bottomVisibleLine = topVisibleLine + (int)roundf((float)g_documentWindowSize.height / g_layoutInfo.GetLineHeight());
	bottomVisibleLine -= 1; //Scrollbar takes up one line on bottom

	if (caretRow < g_layoutInfo.GetVisibleLineTop())
	{
		ScrollTo(newCharacterIndex, ScrollToStyle::TOP);
	}
	else if (caretRow > bottomVisibleLine)
	{
		ScrollTo(newCharacterIndex, ScrollToStyle::BOTTOM);
	}

	g_status.CaretPositionChanged(caretRow, caretColumn, statusBarLabelHwnd);
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
	EnableMenuItem(windowHandles, ID_EDIT_FIND);

	UpdatePasteEnablement(windowHandles);
}

void UpdateWindowTitle(WindowHandles windowHandles)
{
	std::wstring windowTitle = g_windowTitleFileNamePrefix;

	if (g_hasUnsavedChanges)
		windowTitle.append(L" *");

	SetWindowText(windowHandles.TopLevel, windowTitle.c_str());
}

void SetCurrentFileNameAndUpdateWindowTitle(WindowHandles windowHandles, wchar_t const* fileName)
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
	g_brushCache.Reset(nullptr);
	g_marchingAnts.clear();
}

void CreateDeviceDependentResources(WindowHandles windowHandles)
{
	g_documentWindowSize = GetWindowSize(windowHandles.Document);

	DXGI_SWAP_CHAIN_DESC swapChainDescription = {};
	swapChainDescription.BufferCount = 2;
	swapChainDescription.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	swapChainDescription.BufferDesc.Width = g_documentWindowSize.width;
	swapChainDescription.BufferDesc.Height = g_documentWindowSize.height;
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

	g_brushCache.Reset(g_hwndRenderTarget.Get());
	g_themeManager.LoadTheme(L"ColumnMode Classic", g_theme);
	OnThemesRescan(windowHandles, /*skip rescan*/ true);

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

void InitManagers(HINSTANCE hInstance, WindowHandles windowHandles)
{
	using namespace ColumnMode;
	g_windowManager.Init(hInstance, windowHandles);

	ColumnModeCallbacks callbacks {0};
	callbacks.pfnRegisterWindowClass = [](WNDCLASS wc) { return g_windowManager.CreateWindowClass(wc); };
	callbacks.pfnRegisterWindowClassEx = [](WNDCLASSEX wc) { return g_windowManager.CreateWindowClassEx(wc); };
	callbacks.pfnOpenWindow = [](CreateWindowArgs args, HWND* hwnd) { return g_windowManager.CreateNewWindow(args, hwnd); };
	callbacks.pfnRecommendEditMode = [](HANDLE hPlugin, ColumnMode::EDIT_MODE editMode) {
		Mode mode = static_cast<Mode>(editMode);
		if (mode == g_status.GetMode())
		{
			return S_OK;
		}
		std::wstring msg = L"A Plugin recommends using ";
		msg.append(editMode == EDIT_MODE::TextMode ? L"Text Mode" : L"Diagram Mode");
		msg.append(L" for this file.\nSwitch to that mode?");
		int res = MessageBox(NULL, msg.c_str(), L"Change edit mode?", MB_YESNO);
		if (res == IDYES)
		{
			switch (editMode)
			{
			case EDIT_MODE::DiagramMode: OnDiagramMode(g_windowManager.GetWindowHandles()); break;
			case EDIT_MODE::TextMode: OnTextMode(g_windowManager.GetWindowHandles()); break;
			}
		}
		return S_OK;
	};
	
	g_pluginManager.Init(callbacks);
	OnPluginRescan(windowHandles, /*skip rescan*/ true);
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
	DisableMenuItem(windowHandles, ID_EDIT_FIND);
	DisableMenuItem(windowHandles, ID_FILE_PROPERTIES);
	DisableMenuItem(windowHandles, ID_FILE_PRINT);
	
	Static_SetText(windowHandles.StatusBarLabel, L"");

#define LOCAL_DEBUGGING 0

#if LOCAL_DEBUGGING 
	// For when you want to instant-open a document (and, optionally, set some settings) on program load.
	OpenImpl(windowHandles, L"C:\\Users\\SomeUser\\Documents\\doc.txt");
	OnTextMode(windowHandles);
#endif
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

	g_hwndRenderTarget->FillRectangle(paper, g_brushCache.GetBrush(g_theme.GetColor(ColumnMode::THEME_COLOR::UI_PAPER)));

	D2D1_RECT_F marginArea = D2D1::RectF(
		layoutRectangleInScreenSpace.left - margin,
		layoutRectangleInScreenSpace.top,
		layoutRectangleInScreenSpace.left,
		layoutRectangleInScreenSpace.bottom);
	g_hwndRenderTarget->FillRectangle(marginArea, g_brushCache.GetBrush(g_theme.GetColor(ColumnMode::THEME_COLOR::UI_MARGIN)));

	g_hwndRenderTarget->DrawRectangle(paper, g_brushCache.GetBrush(g_theme.GetColor(ColumnMode::THEME_COLOR::UI_PAPER_BORDER)));
	
	// Highlight current line
	{
		g_hwndRenderTarget->FillRectangle(D2D1::RectF(
			layoutRectangleInScreenSpace.left,
			layoutRectangleInScreenSpace.top + g_caretPosition.y,
			layoutRectangleInScreenSpace.right,
			layoutRectangleInScreenSpace.top + g_caretPosition.y + g_caretMetrics.height),
			g_brushCache.GetBrush(g_theme.GetColor(ColumnMode::THEME_COLOR::UI_CURRENT_LINE_HIGHLIGHT)));
	}
	
	D2D1_POINT_2F layoutPosition = g_layoutInfo.GetPosition();

	g_hwndRenderTarget->DrawTextLayout(layoutPosition, g_textLayout.Get(), g_brushCache.GetBrush(g_theme.GetColor(ColumnMode::THEME_COLOR::TEXT_DEFAULT)));

	// Draw caret
	if (g_caretBlinkState <= 25)
	{
		g_hwndRenderTarget->FillRectangle(D2D1::RectF(
			layoutPosition.x + g_caretPosition.x,
			layoutPosition.y + g_caretPosition.y,
			layoutPosition.x + g_caretPosition.x + (g_status.GetMode() == Mode::DiagramMode ? g_caretMetrics.width : 2),
			layoutPosition.y + g_caretPosition.y + g_caretMetrics.height),
			g_brushCache.GetBrush(g_theme.GetColor(ColumnMode::THEME_COLOR::UI_CARET)));
	}

	if (g_isDragging)
	{
		g_hwndRenderTarget->DrawRectangle(D2D1::RectF(g_start.Location.x, g_start.Location.y, g_current.Location.x, g_current.Location.y), 
			g_brushCache.GetBrush(g_theme.GetColor(ColumnMode::THEME_COLOR::UI_DRAG_RECT)));
	}

	if (g_hasTextSelectionRectangle)
	{
		g_hwndRenderTarget->FillRectangle(g_textSelectionRectangle, g_brushCache.GetBrush(g_theme.GetColor(ColumnMode::THEME_COLOR::UI_SELECTION)));
		g_hwndRenderTarget->DrawRectangle(g_textSelectionRectangle, 
			g_brushCache.GetBrush(g_theme.GetColor(ColumnMode::THEME_COLOR::UI_SELECTION_BORDER)),
			1.0f, g_marchingAnts[g_marchingAntsIndex / 5].Get());
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
	g_hwndRenderTarget->Clear(g_theme.GetColor(ColumnMode::THEME_COLOR::UI_BACKGROUND));

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

void GetMouseInfo(LPARAM lParam, Drag& mouseInfo)
{
	int xPos = GET_X_LPARAM(lParam);
	int yPos = GET_Y_LPARAM(lParam);

	mouseInfo.Location.x = static_cast<float>(xPos);
	mouseInfo.Location.y = static_cast<float>(yPos);

	D2D1_RECT_F layoutRectangleInScreenSpace = g_layoutInfo.GetLayoutRectangleInScreenSpace();
	mouseInfo.Location.x = ClampToRange(mouseInfo.Location.x, layoutRectangleInScreenSpace.left, layoutRectangleInScreenSpace.right);
	mouseInfo.Location.y = ClampToRange(mouseInfo.Location.y, layoutRectangleInScreenSpace.top, layoutRectangleInScreenSpace.bottom);

	D2D1_POINT_2F layoutPosition = g_layoutInfo.GetPosition();
	VerifyHR(g_textLayout->HitTestPoint(
		mouseInfo.Location.x - layoutPosition.x,
		mouseInfo.Location.y - layoutPosition.y,
		&mouseInfo.IsTrailing,
		&mouseInfo.OverlaysText,
		&mouseInfo.HitTest));
}

void OnMouseLeftButtonDown(WindowHandles windowHandles, LPARAM lParam)
{
	if (!g_isFileLoaded)
		return;

	g_isDragging = true;
	DisableTextSelectionRectangle(windowHandles);

	GetMouseInfo(lParam, g_start);

	g_current.Location.x = g_start.Location.x;
	g_current.Location.y = g_start.Location.y;

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

void OnMouseLeftButtonDblClick(WindowHandles windowHandles, LPARAM lParam)
{
	if (g_status.GetMode() == Mode::TextMode)
	{
		Drag mouseInfo;
		GetMouseInfo(lParam, mouseInfo);
		if (mouseInfo.OverlaysText)
		{
			int left, right;
			left = right = mouseInfo.HitTest.textPosition;

			// Do nothing if we selected whitespace
			if (g_allText[left] == ' ') return;

			int row, col;
			GetRowAndColumnFromCharacterPosition(g_caretCharacterIndex, &row, &col);

			int lineStart = g_textLineStarts[row];
			int lineEnd = g_textLineStarts[row + 1] - 1;

			//Look left
			while (left >= lineStart && g_allText[left-1] != ' ') { left--; };
			//look right
			while (right < lineEnd && g_allText[right+1] != ' ') { right++; };

			SetSelection(left, right - left);
		}
	}
}

//assumes single line selection
void SetSelection(int startCharIndex, int length, DWRITE_HIT_TEST_METRICS* pHitTest)
{
	int row, col;
	GetRowAndColumnFromCharacterPosition(startCharIndex, &row, &col);
	
	DWRITE_HIT_TEST_METRICS hitTest;
	if (pHitTest)
	{
		hitTest = *pHitTest;
	}
	else
	{
		FLOAT hitLeft, hitTop;
		VerifyHR(g_textLayout->HitTestTextPosition(startCharIndex, false, &hitLeft, &hitTop, &hitTest));
	}

	g_start.HitTest = hitTest;
	g_start.Location.x = static_cast<float>(col);
	g_start.Location.y = static_cast<float>(row);

	g_current.HitTest = hitTest;
	g_current.HitTest.textPosition += length;
	g_current.HitTest.left += length * g_caretMetrics.width;
	g_current.Location.x = static_cast<float>(col + length);
	g_current.Location.y = static_cast<float>(row);

	EnableTextSelectionRectangle(g_windowManager.GetWindowHandles());
	UpdateTextSelectionRectangle();
}

void ScrollTo(UINT index, ScrollToStyle scrollStyle)
{
	HWND documentHwnd = g_windowManager.GetWindowHandles().Document;

	DWRITE_HIT_TEST_METRICS hitTest;
	FLOAT hitLeft, hitTop;
	VerifyHR(g_textLayout->HitTestTextPosition(index, false, &hitLeft, &hitTop, &hitTest));

	SCROLLINFO scrollInfo{};
	scrollInfo.cbSize = sizeof(scrollInfo);
	scrollInfo.fMask = SIF_POS;
	VerifyBool(GetScrollInfo(documentHwnd, SB_VERT, &scrollInfo));

	float scrollAmount = hitTop;
	switch(scrollStyle)
	{
	case ScrollToStyle::TOP: break;
	case ScrollToStyle::CENTER: scrollAmount -= .5f * g_documentWindowSize.height; break;
	case ScrollToStyle::BOTTOM: scrollAmount -= (g_documentWindowSize.height - g_layoutInfo.GetLineHeight()); break; // Account for scrollbar
	} 

	if (scrollAmount < 0)
		scrollAmount = 0;

	if (scrollAmount > g_verticalScrollLimit)
		scrollAmount = g_verticalScrollLimit;

	g_layoutInfo.SetPositionY(-scrollAmount);

	UpdateTextSelectionRectangle();

	scrollInfo.nPos = scrollAmount;
	SetScrollInfo(documentHwnd, SB_VERT, &scrollInfo, TRUE);
	InvalidateRect(documentHwnd, nullptr, FALSE);
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
		GetMouseInfo(lParam, g_current);

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

	g_documentWindowSize = GetWindowSize(windowHandles.Document);
	VerifyHR(g_swapChain->ResizeBuffers(2, g_documentWindowSize.width, g_documentWindowSize.height, DXGI_FORMAT_B8G8R8A8_UNORM, 0));
	
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

		int xBoundary;
		if (g_status.GetMode() == Mode::DiagramMode)
		{
			xBoundary = selection.Right;
		}
		else if (g_status.GetMode() == Mode::TextMode)
		{
			xBoundary = selection.Right-1;
		}

		for (int x = selection.Left - 1; x <= xBoundary; ++x)
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

		int yBoundary;
		if (g_status.GetMode() == Mode::DiagramMode)
		{
			yBoundary = selection.Bottom;
		}
		else if (g_status.GetMode() == Mode::TextMode)
		{
			yBoundary = selection.Bottom-1;
		}

		for (int y = selection.Top - 1; y <= yBoundary; ++y)
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

		int xBoundary;
		if (g_status.GetMode() == Mode::DiagramMode)
		{
			xBoundary = selection.Left - 1;
		}
		else if (g_status.GetMode() == Mode::TextMode)
		{
			xBoundary = selection.Left;
		}

		for (int x = static_cast<int>(selection.Right); x >= xBoundary; --x)
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

		int yBoundary;
		if (g_status.GetMode() == Mode::DiagramMode)
		{
			yBoundary = selection.Top - 1;
		}
		else if (g_status.GetMode() == Mode::TextMode)
		{
			yBoundary = selection.Top;
		}

		for (int y = static_cast<int>(selection.Bottom); y >= yBoundary; --y)
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
		g_layoutInfo.AdjustPositionY(-100);
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

void OnEnterPressed(WindowHandles windowHandles)
{
	int caretRow, caretColumn;
	GetRowAndColumnFromCharacterPosition(g_caretCharacterIndex, &caretRow, &caretColumn);

	if (g_status.GetMode() == Mode::DiagramMode)
	{
		if (caretRow < static_cast<int>(g_textLineStarts.size()) - 1)
		{
			caretRow++;
			caretColumn = 0;
			SetCaretCharacterIndex(g_textLineStarts[caretRow] + caretColumn, windowHandles.StatusBarLabel);
		}
	}
	else if (g_status.GetMode() == Mode::TextMode)
	{
		// Insert a line
		std::wstring copiedLine;
		bool isAtLastLine = caretRow == g_textLineStarts.size() - 1;
		int copiedLineStartIndex = g_textLineStarts[caretRow] + caretColumn;

		if (isAtLastLine)
		{
			int copiedLineEndIndex = (int)g_allText.length();
			int copiedLineLength = copiedLineEndIndex - copiedLineStartIndex - 1;
			copiedLine = g_allText.substr(g_textLineStarts[caretRow] + caretColumn, copiedLineLength); // Need to append padding here?

			g_allText.replace(copiedLineStartIndex, copiedLineLength, copiedLineLength, L' ');

			g_allText.append(L"\n");
			g_textLineStarts.push_back((int)g_allText.length());
			g_allText.append(copiedLine);
		}
		else
		{
			int copiedLineEndIndex = g_textLineStarts[caretRow + 1];
			int copiedLineLength = copiedLineEndIndex - copiedLineStartIndex - 1;
			copiedLine = g_allText.substr(g_textLineStarts[caretRow] + caretColumn, copiedLineLength);
			std::wstring copiedLineWithPadding = copiedLine;
			for (int i = copiedLineLength; i < g_maxLineLength; ++i)
			{
				copiedLineWithPadding.append(L" ");
			}
			copiedLineWithPadding.push_back(L'\n');

			g_allText.replace(copiedLineStartIndex, copiedLineLength, copiedLineLength, L' ');

			g_allText.insert(g_textLineStarts[caretRow + 1], copiedLineWithPadding);

			// All the text line starts have to be updated because we inserted a new row.
			g_textLineStarts.insert(g_textLineStarts.begin() + caretRow + 1, g_textLineStarts[caretRow + 1]);

			for (size_t i = caretRow + 1 + 1; i < g_textLineStarts.size(); ++i)
			{
				g_textLineStarts[i] += g_maxLineLength + 1;
			}
		}

		Action a{};
		a.Type = Action::TextModeCarriageReturn;
		std::vector<wchar_t> overwrittenChars;
		for (size_t i = 0; i < copiedLine.length(); ++i)
		{
			overwrittenChars.push_back(copiedLine[i]);
		}
		a.OverwrittenChars.push_back(overwrittenChars);
		a.TextPosition = g_textLineStarts[caretRow] + caretColumn;
		a.BlockTop = caretRow + 1;
		AddAction(windowHandles, a);

		RecreateTextLayout();

		caretRow++;
		caretColumn = 0;
		SetCaretCharacterIndex(g_textLineStarts[caretRow] + caretColumn, windowHandles.StatusBarLabel);
	}
}

void CheckModifierKeys()
{
	g_isShiftDown = GetKeyState(VK_SHIFT) & 0x8000;
	g_isCtrlDown = GetKeyState(VK_CONTROL) & 0x8000;
}

void OnKeyDown(WindowHandles windowHandles, WPARAM wParam)
{
	if (!g_isFileLoaded)
		return;

	g_caretBlinkState = 0;
	CheckModifierKeys(); // modifiers could be pressed when a message went to a different handler
	if (g_keyOutput[wParam].Valid)
	{
		DisableTextSelectionRectangle(windowHandles);

		wchar_t chr = g_isShiftDown ? g_keyOutput[wParam].Uppercase : g_keyOutput[wParam].Lowercase;

		if (g_status.GetMode() == Mode::DiagramMode)
		{
			Action a;
			a.Type = Action::WriteCharacter_DiagramMode;

			std::vector<wchar_t> line;
			line.push_back(g_allText[g_caretCharacterIndex]);
			a.OverwrittenChars.push_back(line);
			a.TextPosition = g_caretCharacterIndex;
			AddAction(windowHandles, a);

			WriteCharacterAtCaret(chr, windowHandles);
		}
		else if (g_status.GetMode() == Mode::TextMode)
		{
			int caretRow, caretColumn;
			GetRowAndColumnFromCharacterPosition(g_caretCharacterIndex, &caretRow, &caretColumn);

			Action a{};
			a.Type = Action::WriteCharacter_TextMode;
			AddAction(windowHandles, a);

			int startIndex = g_caretCharacterIndex;
			int endIndex;
			if (caretRow == g_textLineStarts.size() - 1)
			{
				endIndex = static_cast<int>(g_allText.length());
			}
			else
			{
				endIndex = g_textLineStarts[caretRow + 1] - 1;
			}

			// Move all characters to the right
			for (int i = endIndex - 1 ; i > startIndex; --i)
			{
				g_allText[i] = g_allText[i - 1];
			}

			// Put a letter at the caret
			g_allText[g_caretCharacterIndex] = chr;
			RecreateTextLayout();

			caretColumn++;
			SetCaretCharacterIndex(g_textLineStarts[caretRow] + caretColumn, windowHandles.StatusBarLabel);
		}
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
		if (g_status.GetMode() == Mode::DiagramMode)
		{
			DisableTextSelectionRectangle(windowHandles);
			if (g_caretCharacterIndex > 0)
			{
				SetCaretCharacterIndex(g_caretCharacterIndex - 1, windowHandles.StatusBarLabel);
			}

			Action a;
			a.Type = Action::Backspace_DiagramMode;
			std::vector<wchar_t> line;
			line.push_back(g_allText[g_caretCharacterIndex]);
			a.OverwrittenChars.push_back(line);
			a.TextPosition = g_caretCharacterIndex;
			AddAction(windowHandles, a);

			g_allText[g_caretCharacterIndex] = L' ';
		}
		else if (g_status.GetMode() == Mode::TextMode)
		{
			int startIndex, endIndex;
			if (g_hasTextSelectionRectangle)
			{
				//DeleteBlock(windowHandles);
				SignedRect selection = GetTextSelectionRegion();
				DisableTextSelectionRectangle(windowHandles);
				UINT characterPos = 0;
				Action a;
				a.Type = Action::Backspace_TextMode;

				if (GetCharacterPositionFromRowAndColumn(selection.Top, selection.Left, characterPos))
				{
					SetCaretCharacterIndex(characterPos, windowHandles.StatusBarLabel);
				}
				for (int row = selection.Top; row <= selection.Bottom; row++)
				{
					startIndex = g_textLineStarts[row] + selection.Left;
					if (row == g_textLineStarts.size() - 1)
					{
						endIndex = static_cast<int>(g_allText.length());
					}
					else
					{
						endIndex = g_textLineStarts[row + 1] - 1;
					}

					std::vector<wchar_t> line;
					for (int i = startIndex; i < endIndex; i++)
					{
						line.push_back(g_allText[i]);
					}
					a.OverwrittenChars.push_back(line);

					int delta = selection.Right - selection.Left + 1;
					for (int i = startIndex; i < endIndex - 1; i++)
					{
						if (i + delta < endIndex - 1)
						{
							g_allText[i] = g_allText[i+delta];
						}
						else
						{
							g_allText[i] = L' ';
						}
						
					}
					g_allText[endIndex - 1] = L' ';
				}
				a.TextPosition = characterPos;
				AddAction(windowHandles, a);
			}
			else
			{
				if (g_caretCharacterIndex > 0)
				{
					int caretRow, caretColumn;
					GetRowAndColumnFromCharacterPosition(g_caretCharacterIndex, &caretRow, &caretColumn);
					startIndex = g_caretCharacterIndex-1;
					if (caretRow == g_textLineStarts.size() - 1)
					{
						endIndex = static_cast<int>(g_allText.length());
					}
					else
					{
						endIndex = g_textLineStarts[caretRow + 1] - 1;
					}
					if (caretColumn == 0)
					{
						//don't delete the newline and mess up the document.
						//TODO: cause this to move everything up a line?
						return;
					}
					SetCaretCharacterIndex(g_caretCharacterIndex - 1, windowHandles.StatusBarLabel);
					Action a;
					a.Type = Action::Backspace_TextMode;
					std::vector<wchar_t> line;
					for (int i = startIndex; i < endIndex; i++)
					{
						line.push_back(g_allText[i]);
					}
					
					a.OverwrittenChars.push_back(line);
					a.TextPosition = startIndex;

					// Move all characters to the left
					for (int i = startIndex; i < endIndex-1; i++) 
					{
						g_allText[i] = g_allText[i + 1];
					}
					g_allText[endIndex-1] = L' ';

					AddAction(windowHandles, a);
				}
			}
		}

		
		RecreateTextLayout();
	}
	else if (wParam == 13) // Enter
	{
		OnEnterPressed(windowHandles);
	}
	else if (wParam == VK_ESCAPE)
	{
		DisableTextSelectionRectangle(windowHandles);
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
	else if (wParam == 45) // Insert Key
	{
		if (g_status.GetMode() == Mode::TextMode)
		{
			OnDiagramMode(windowHandles);
		}
		else
		{
			OnTextMode(windowHandles);
		}
		//TODO: Pull the label creation into it's own method so we don't need to "set" caret index just to update the label text
		SetCaretCharacterIndex(g_caretCharacterIndex, windowHandles.StatusBarLabel);
	}
	else if (wParam == 46) // Delete key
	{
		if(g_hasTextSelectionRectangle)
		{
			DisableTextSelectionRectangle(windowHandles);
			DeleteBlock(windowHandles);
		}
		else if (g_status.GetMode() == Mode::TextMode)
		{
			if (g_caretCharacterIndex > 0)
			{
				int startIndex, endIndex, caretRow, caretColumn;
				GetRowAndColumnFromCharacterPosition(g_caretCharacterIndex, &caretRow, &caretColumn);
				startIndex = g_caretCharacterIndex;
				if (caretRow == g_textLineStarts.size() - 1)
				{
					endIndex = static_cast<int>(g_allText.length());
				}
				else
				{
					endIndex = g_textLineStarts[caretRow + 1] - 1;
				}
				Action a;
				a.Type = Action::DeleteCharacter_TextMode;
				std::vector<wchar_t> line;
				for (int i = startIndex; i < endIndex; i++)
				{
					line.push_back(g_allText[i]);
				}

				a.OverwrittenChars.push_back(line);
				a.TextPosition = startIndex;

				// Move all characters to the left
				for (int i = startIndex; i < endIndex - 1; i++)
				{
					g_allText[i] = g_allText[i + 1];
				}
				g_allText[endIndex - 1] = L' ';

				AddAction(windowHandles, a);
			}
		}
		RecreateTextLayout();
	}
}

void OnKeyUp(WindowHandles windowHandles, WPARAM wParam)
{
	CheckModifierKeys(); // modifiers could be pressed when a message went to a different handler
	if (wParam == 19) // Pause
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

void OpenImpl(WindowHandles windowHandles, LPCWSTR fileName)
{
	PromptToSaveUnsavedChanges();
	SetCurrentFileNameAndUpdateWindowTitle(windowHandles, fileName);

	LoadOrCreateFileResult loadFileResult = LoadOrCreateFileContents(fileName);

	InitializeDocument(windowHandles, loadFileResult);

	EnableMenuItem(windowHandles, ID_FILE_REFRESH);
	EnableMenuItem(windowHandles, ID_FILE_SAVE);
	g_pluginManager.PF_OnOpen_ALL(fileName);
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
		OpenImpl(windowHandles, ofn.lpstrFile);
	}
}

void OnSave(WindowHandles windowHandles)
{
	g_isCtrlDown = false;

	{
		std::wofstream out(g_fileFullPath);
		out << g_allText;
	}

#if _DEBUG
	//MessageBox(nullptr, L"Save completed.", L"ColumnMode", MB_OK);
#endif

	g_hasUnsavedChanges = false;
	UpdateWindowTitle(windowHandles);
	g_pluginManager.PF_OnSave_ALL(g_fileFullPath.c_str());
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
		g_pluginManager.PF_OnSaveAs_ALL(ofn.lpstrFile);
	}
}

void OnUndo(WindowHandles windowHandles)
{
	auto const& top = g_undoBuffer.back();
	
	// Pop item off stack
	if (top.Type == Action::WriteCharacter_DiagramMode)
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
	else if (top.Type == Action::Backspace_DiagramMode)
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
	else if (top.Type == Action::Backspace_TextMode)
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

		UINT newTextPosition = top.TextPosition;

		if (top.TextPosition < g_allText.length() - 1)
		{
			newTextPosition++;
		}
		SetCaretCharacterIndex(newTextPosition, windowHandles.StatusBarLabel);
	}
	else if (top.Type == Action::DeleteCharacter_TextMode)
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
	else if (top.Type == Action::TextModeCarriageReturn)
	{
		int del = top.BlockTop;

		bool isLastLine = del == g_textLineStarts.size() - 1;

		if (isLastLine)
		{
			int eraseStart = g_textLineStarts[del] - 1; // Delete the newline at the end of the last line
			int eraseEnd = static_cast<int>(g_allText.size());
			int eraseLength = eraseEnd - eraseStart;
			g_allText.erase(eraseStart, eraseLength);
			g_textLineStarts.erase(g_textLineStarts.end() - 1);
		}
		else
		{
			// Delete the inserted line - chucked in BlockTop
			int eraseStart = g_textLineStarts[del];
			int eraseEnd = g_textLineStarts[del + 1];
			int eraseLength = eraseEnd - eraseStart;
			g_allText.erase(eraseStart, eraseLength);
			g_textLineStarts.erase(g_textLineStarts.begin() + del);
			for (int i = del; i < g_textLineStarts.size(); ++i)
			{
				g_textLineStarts[i] -= g_maxLineLength + 1;
			}
		}

		// Restore the mangled line
		for (int i = 0; i < top.OverwrittenChars[0].size(); ++i)
		{
			g_allText[top.TextPosition + i] = top.OverwrittenChars[0][i];
		}

		RecreateTextLayout();
		SetCaretCharacterIndex(top.TextPosition, windowHandles.StatusBarLabel);
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
		if (y < selection.Bottom)
		{
			stringData.push_back(L'\n');
		}
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

void OnFind(HINSTANCE hInst, HWND hWnd)
{
	g_findTool.EnsureDialogCreated(hInst, hWnd);
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

	if (g_status.GetMode() == Mode::TextMode)
	{
		//move cursor to end of psate
		SetCaretCharacterIndex(a.TextPosition + static_cast<UINT32>(a.OverwrittenChars[0].size()), windowHandles.StatusBarLabel);
	}
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
	m_d2dContextForPrint->DrawTextLayout(D2D1::Point2F(0, 0), g_textLayout.Get(), g_brushCache.GetBrush(g_theme.GetColor(ColumnMode::THEME_COLOR::TEXT_DEFAULT)));
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

void OnPluginRescan(WindowHandles windowHandles, bool skipRescan)
{
	 HMENU toplevelMenu = GetMenu(windowHandles.TopLevel);

	 int pluginMenuIndex = 3;

#if _DEBUG
	 wchar_t str[255];
	 GetMenuString(toplevelMenu, pluginMenuIndex, str, 255, MF_BYPOSITION);
	 assert(wcscmp(str, L"Plugins") == 0);
#endif

	 HMENU pluginMenu = GetSubMenu(toplevelMenu, pluginMenuIndex);//Third menu in the list. Should probably just create it in code

	 int numItems = GetMenuItemCount(pluginMenu);
	 constexpr int seperatorIndex = 1;
	 for (int i = numItems-1; i > seperatorIndex ; i--)
	 {
		 HMENU m = (HMENU)(UINT_PTR)GetMenuItemID(pluginMenu, i);
		 DestroyMenu(m);
		 RemoveMenu(pluginMenu, i, MF_BYPOSITION);
	 }
	 
	 if (!skipRescan)
	 {
		 g_pluginManager.ScanForPlugins();
	 }
	 UINT id = ColumnMode::PluginManager::PLUGIN_MENU_ITEM_START_INDEX;
	 for (auto& str : g_pluginManager.GetAvailablePlugins())
	 {
		 AppendMenu(pluginMenu, MF_STRING, id, str.c_str());
		 //If already active:
		 if (g_pluginManager.IsPluginLoaded(str.c_str()))
		 {
			 CheckMenuItem(pluginMenu, id, MF_BYCOMMAND | MF_CHECKED);
		 }
		 id++;
	 }	 
}

void OnThemesRescan(WindowHandles windowHandles, bool skipRescan)
{
	HMENU toplevelMenu = GetMenu(windowHandles.TopLevel);
	HMENU themesMenu;
	int themesRescanMenuPos;
	FindMenuPos(toplevelMenu, ID_THEMES_RESCAN, themesMenu, themesRescanMenuPos);

	int separatorIndex = themesRescanMenuPos + 1;

	int numItems = GetMenuItemCount(themesMenu);
	for (int i = numItems - 1; i > separatorIndex; i--)
	{
		HMENU m = (HMENU)(UINT_PTR)GetMenuItemID(themesMenu, i);
		DestroyMenu(m);
		RemoveMenu(themesMenu, i, MF_BYPOSITION);
	}

	if (!skipRescan)
	{
		g_themeManager.ScanForThemes();
	}
	UINT id = ColumnMode::ThemeManager::THEME_MENU_ITEM_START_INDEX;
	for (auto& str : g_themeManager.GetAvailableThemes())
	{
		AppendMenu(themesMenu, MF_STRING, id, str.c_str());
		//If already active:
		if (g_theme.GetName().compare(str.c_str()) == 0)
		{
			CheckMenuItem(themesMenu, id, MF_BYCOMMAND | MF_CHECKED);
		}
		id++;
	}
}

LRESULT CALLBACK ThemeNameQueryCallback(HWND hDlg, UINT message, WPARAM wParam, LPARAM lParam)
{
	switch (message)
	{
	case WM_COMMAND:
		if (LOWORD(wParam) == IDOK)
		{
			HWND editBox = GetDlgItem(hDlg, IDC_THEMENAME_EDITBOX);
			assert(editBox != NULL);
			wchar_t textBuffer[64]{};
			int numChars = Static_GetText(editBox, textBuffer, _countof(textBuffer));
			ColumnMode::Theme theme;
			ColumnMode::Theme::CreateDefaultTheme(theme);
			theme.SetName(textBuffer);
			if (g_themeManager.SaveTheme(theme))
			{
				
				OpenImpl(g_windowManager.GetWindowHandles(), g_themeManager.GetThemeFilepath(theme).c_str());
			}
			EndDialog(hDlg, LOWORD(wParam));
			return TRUE;
		}
		else if (LOWORD(wParam) == IDCANCEL)
		{
			EndDialog(hDlg, LOWORD(wParam));
			return TRUE;
		}
		break;
	}
	return FALSE;
}

void OnCreateTheme(HWND hwnd, HINSTANCE hInst)
{
	DialogBox(hInst, MAKEINTRESOURCE(IDD_THEMENAMEQUERY), hwnd, ThemeNameQueryCallback);
}

bool OnMaybeDynamicMenuItemSelected(WindowHandles windowHandles, int id)
{
	static_assert(ColumnMode::ThemeManager::THEME_MENU_ITEM_START_INDEX > ColumnMode::PluginManager::PLUGIN_MENU_ITEM_START_INDEX);

	if (id >= ColumnMode::ThemeManager::THEME_MENU_ITEM_START_INDEX)
	{
		OnMaybeThemeSelected(windowHandles, id);
	}
	else if (id >= ColumnMode::PluginManager::PLUGIN_MENU_ITEM_START_INDEX)
	{
		return OnMaybePluginSelected(windowHandles, id);
	}
	return false;
}

bool OnMaybePluginSelected(WindowHandles windowHandles, int id)
{
	HMENU toplevelMenu = GetMenu(windowHandles.TopLevel);
	HMENU pluginMenu = GetSubMenu(toplevelMenu, 3);//Third menu in the list. Should probably just create it in code
	int numItems = GetMenuItemCount(pluginMenu);
	if (id - ColumnMode::PluginManager::PLUGIN_MENU_ITEM_START_INDEX > numItems)
	{
		return false;
	}
	WCHAR buff[64];
	int size = GetMenuString(pluginMenu, id, buff, 64, MF_BYCOMMAND);
	if (size > 0)
	{
		if (!g_pluginManager.IsPluginLoaded(buff))
		{
			if (SUCCEEDED(g_pluginManager.LoadPlugin(buff)))
			{
				//Only check it if we actually loaded successfully
				CheckMenuItem(pluginMenu, id, MF_BYCOMMAND | MF_CHECKED);
			}
		}
		else
		{
			if (SUCCEEDED(g_pluginManager.UnloadPlugin(buff)))
			{
				//uncheck after unload
				CheckMenuItem(pluginMenu, id, MF_BYCOMMAND | MF_UNCHECKED);
			}
		}
		//TODO: Should probably be able to disbale plugins :P
	}
	return true;
}

bool OnMaybeThemeSelected(WindowHandles windowHandles, int id)
{
	HMENU toplevelMenu = GetMenu(windowHandles.TopLevel);
	HMENU themesMenu;
	int themesRescanMenuPos;
	FindMenuPos(toplevelMenu, ID_THEMES_RESCAN, themesMenu, themesRescanMenuPos);
	int numItems = GetMenuItemCount(themesMenu);
	if (id - ColumnMode::ThemeManager::THEME_MENU_ITEM_START_INDEX > numItems)
	{
		return false;
	}
	WCHAR buff[64];
	int size = GetMenuString(themesMenu, id, buff, 64, MF_BYCOMMAND);
	if (size > 0 && g_theme.GetName().compare(buff) != 0)
	{
		g_themeManager.LoadTheme(buff, g_theme);
		OnThemesRescan(windowHandles, true); // Handle the check marking-ing in probably the worst way
	}
	return true;
}

void PromptToSaveUnsavedChanges()
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
				OnSave(g_windowManager.GetWindowHandles());
			}
			else
			{
				OnSaveAs(g_windowManager.GetWindowHandles());
			}
		}
		else
		{
			assert(dialogResult == IDNO);
		}
	}
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

void OnDiagramMode(WindowHandles windowHandles)
{
	HMENU menu = GetMenu(windowHandles.TopLevel);
	CheckMenuItem(menu, ID_OPTIONS_DIAGRAMMODE, MF_BYCOMMAND | MF_CHECKED);
	CheckMenuItem(menu, ID_OPTIONS_TEXTMODE, MF_BYCOMMAND | MF_UNCHECKED);
	g_status.SetMode(Mode::DiagramMode, windowHandles.StatusBarLabel);
}

void OnTextMode(WindowHandles windowHandles)
{
	HMENU menu = GetMenu(windowHandles.TopLevel);
	CheckMenuItem(menu, ID_OPTIONS_DIAGRAMMODE, MF_BYCOMMAND | MF_UNCHECKED);
	CheckMenuItem(menu, ID_OPTIONS_TEXTMODE, MF_BYCOMMAND | MF_CHECKED);
	g_status.SetMode(Mode::TextMode, windowHandles.StatusBarLabel);
}

void OnClose(WindowHandles windowHandles)
{
	PromptToSaveUnsavedChanges();

	DestroyWindow(windowHandles.TopLevel);
}

std::wstring& GetAllText()
{
	return g_allText;
}

ColumnMode::FindTool& GetFindTool()
{
	return g_findTool;
}