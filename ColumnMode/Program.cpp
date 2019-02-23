#include "stdafx.h"
#include "Program.h"
#include "Resource.h"

const float g_fontSize = 12.0f;

bool g_isFileLoaded;
std::wstring g_fileName;

ComPtr<IDXGISwapChain> g_swapChain;
ComPtr<ID2D1Factory1> g_d2dFactory;
ComPtr<ID2D1DeviceContext> g_hwndRenderTarget;
ComPtr<ID2D1SolidColorBrush> g_redBrush;
ComPtr<ID2D1SolidColorBrush> g_blackBrush;
ComPtr<ID2D1SolidColorBrush> g_whiteBrush;
ComPtr<ID2D1SolidColorBrush> g_yellowBrush;
ComPtr<ID2D1SolidColorBrush> g_selectionBrush;
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

class LayoutInfo
{
public:

	D2D1_RECT_F GetLayoutRectangleInScreenSpace() const
	{
		return m_layoutRectangleInScreenSpace;
	}

	D2D1_RECT_F GetLayoutRectangleInScreenSpaceLockedToPixelCenters() const
	{
		return m_layoutRectangleInScreenSpaceLockedToPixelCenters;
	}

	int GetLayoutWidth() const
	{
		return static_cast<int>(m_layoutMetrics.width);
	}

	int GetLayoutHeight() const
	{
		return static_cast<int>(m_layoutMetrics.height);
	}

	D2D1_POINT_2F GetPosition() const
	{
		return m_layoutPosition;
	}

	// Setters

	void RefreshLayoutMetrics()
	{
		VerifyHR(g_textLayout->GetMetrics(&m_layoutMetrics));
		RefreshLayoutRectangleInScreenSpace();
	}

	void SetPosition(D2D1_POINT_2F const& pt)
	{
		m_layoutPosition = pt;
		RefreshLayoutRectangleInScreenSpace();
	}

	void AdjustPositionX(float amount)
	{
		m_layoutPosition.x += amount;
		RefreshLayoutRectangleInScreenSpace();
	}

	void AdjustPositionY(float amount)
	{
		m_layoutPosition.x += amount;
		RefreshLayoutRectangleInScreenSpace();
	}

private:

	void RefreshLayoutRectangleInScreenSpace()
	{
		m_layoutRectangleInScreenSpace = D2D1::RectF(
				m_layoutPosition.x + m_layoutMetrics.left,
				m_layoutPosition.y + m_layoutMetrics.top,
				m_layoutPosition.x + m_layoutMetrics.left + m_layoutMetrics.width,
				m_layoutPosition.y + m_layoutMetrics.top + m_layoutMetrics.height);

		m_layoutRectangleInScreenSpaceLockedToPixelCenters = D2D1::RectF(
			floor(m_layoutRectangleInScreenSpace.left) + 0.5f,
			floor(m_layoutRectangleInScreenSpace.top) + 0.5f,
			floor(m_layoutRectangleInScreenSpace.right) + 0.5f,
			floor(m_layoutRectangleInScreenSpace.bottom) + 0.5f);
	}

	DWRITE_TEXT_METRICS m_layoutMetrics;
	D2D1_POINT_2F m_layoutPosition;
	D2D1_RECT_F m_layoutRectangleInScreenSpace;
	D2D1_RECT_F m_layoutRectangleInScreenSpaceLockedToPixelCenters;
};

LayoutInfo g_layoutInfo;

struct Action
{
	enum ActionType
	{
		WriteCharacter,
		Backspace,
		DeleteBlock,
		PasteToPosition,
		PasteToBlock
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

UINT g_marchingAntsIndex;
std::vector<ComPtr<ID2D1StrokeStyle>> g_marchingAnts;

bool g_isDragging;

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

void VerifyHR(HRESULT hr)
{
	if (FAILED(hr))
		__debugbreak();
}

void VerifyBool(BOOL b)
{
	if (!b)
		__debugbreak();
}

void VerifyErrno(errno_t e)
{
	if (e != 0)
		__debugbreak();
}

void VerifyNonZero(UINT_PTR p)
{
	if (p == 0)
		__debugbreak();
}



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

static void EnableMenuItem(HWND hwnd, int id)
{
	HMENU menu = GetMenu(hwnd);
	EnableMenuItem(menu, id, MF_ENABLED);
}

static void DisableMenuItem(HWND hwnd, int id)
{
	HMENU menu = GetMenu(hwnd);
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

D2D1_SIZE_U GetWindowSize(HWND hwnd)
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

	g_layoutInfo.RefreshLayoutMetrics();
}

static void SetCaretCharacterIndex(UINT32 newCharacterIndex)
{
	g_caretCharacterIndex = newCharacterIndex;
	VerifyHR(g_textLayout->HitTestTextPosition(g_caretCharacterIndex, FALSE, &g_caretPosition.x, &g_caretPosition.y, &g_caretMetrics));
}

static void SetScrollPositions(HWND hwnd)
{
	auto targetSize = g_hwndRenderTarget->GetSize();

	{
		SCROLLINFO scrollInfo = {};
		scrollInfo.cbSize = sizeof(SCROLLINFO);
		scrollInfo.fMask = SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL;
		scrollInfo.nMin = 0;
		scrollInfo.nMax = g_layoutInfo.GetLayoutWidth();
		scrollInfo.nPage = static_cast<int>(targetSize.width);
		SetScrollInfo(hwnd, SB_HORZ, &scrollInfo, TRUE);
	}
	{
		SCROLLINFO scrollInfo = {};
		scrollInfo.cbSize = sizeof(SCROLLINFO);
		scrollInfo.fMask = SIF_RANGE | SIF_PAGE | SIF_DISABLENOSCROLL;
		scrollInfo.nMin = 0;
		scrollInfo.nMax = g_layoutInfo.GetLayoutHeight();
		scrollInfo.nPage = static_cast<int>(targetSize.height);
		SetScrollInfo(hwnd, SB_VERT, &scrollInfo, TRUE);

		g_verticalScrollLimit = scrollInfo.nMax - scrollInfo.nPage;
	}
}

static void UpdatePasteEnablement(HWND hwnd)
{
	OpenClipboard(0);
	bool enable = GetClipboardData(CF_UNICODETEXT) != 0;
	CloseClipboard();

	if (enable)
		EnableMenuItem(hwnd, ID_EDIT_PASTE);
	else
		DisableMenuItem(hwnd, ID_EDIT_PASTE);
}

void LoadFile(HWND hwnd, wchar_t const* fileName)
{
	g_hasTextSelectionRectangle = false;
	g_caretCharacterIndex = 0;
	g_marchingAntsIndex = 0;
	g_layoutInfo.SetPosition(D2D1::Point2F(20, 20));
	g_fileName = fileName;

	std::wifstream f(g_fileName);

	bool validLength = true;

	std::vector<std::wstring> lines;
	while (f.good())
	{
		if (lines.size() == INT_MAX)
		{
			validLength = false;
			break;
		}

		std::wstring line;
		std::getline(f, line);
		if (line.length() > INT_MAX)
		{
			validLength = false;
			break;
		}

		lines.push_back(line);

		g_maxLineLength = max(g_maxLineLength, static_cast<int>(line.length()));
	}

	if (!validLength)
	{
		MessageBox(nullptr, L"File couldn't be loaded because a line or line count is too high.", L"ColumnMode", MB_OK);
		return;
	}

	g_allText = L"";
	for (int i = 0; i < static_cast<int>(lines.size()); ++i)
	{
		int spaceToAdd = static_cast<int>(g_maxLineLength - lines[i].length());
		for (int j = 0; j < spaceToAdd; ++j)
		{
			lines[i].push_back(L' ');
		}
		g_textLineStarts.push_back(static_cast<int>(g_allText.length()));
		g_allText.append(lines[i]);

		if (i != static_cast<int>(lines.size()) - 1)
		{
			g_allText.push_back('\n');
		}
	}

	RecreateTextLayout();

	SetCaretCharacterIndex(0);

	SetScrollPositions(hwnd);

	g_isFileLoaded = true;

	EnableMenuItem(hwnd, ID_FILE_SAVE);
	UpdatePasteEnablement(hwnd);
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

static void DisableTextSelectionRectangle(HWND hwnd)
{
	DisableMenuItem(hwnd, ID_EDIT_COPY);
	DisableMenuItem(hwnd, ID_EDIT_CUT);
	DisableMenuItem(hwnd, ID_EDIT_DELETE);
	g_hasTextSelectionRectangle = false;
}

static void EnableTextSelectionRectangle(HWND hwnd)
{
	EnableMenuItem(hwnd, ID_EDIT_COPY);
	EnableMenuItem(hwnd, ID_EDIT_CUT);
	EnableMenuItem(hwnd, ID_EDIT_DELETE);
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

	PutInOrder(&r.Top, &r.Bottom);
	PutInOrder(&r.Left, &r.Right);

	return r;
}

void InitGraphics(HWND hwnd)
{
	g_isFileLoaded = false;
	g_isDragging = false;
	g_hasTextSelectionRectangle = false;
	g_caretBlinkState = 0;
	g_isShiftDown = false;

	InitializeKeyOutput();

	D2D1_FACTORY_OPTIONS factoryOptions = {};
	factoryOptions.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
	VerifyHR(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, __uuidof(ID2D1Factory1), &factoryOptions, &g_d2dFactory));

	auto windowSize = GetWindowSize(hwnd);

	DXGI_SWAP_CHAIN_DESC swapChainDescription = {};
	swapChainDescription.BufferCount = 2;
	swapChainDescription.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
	swapChainDescription.BufferDesc.Width = windowSize.width;
	swapChainDescription.BufferDesc.Height = windowSize.height;
	swapChainDescription.OutputWindow = hwnd;
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

	ComPtr<ID2D1Device> d2dDevice;
	VerifyHR(g_d2dFactory->CreateDevice(dxgiDevice.Get(), &d2dDevice));

	VerifyHR(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, &g_hwndRenderTarget));

	SetTargetToBackBuffer();

	VerifyHR(g_hwndRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Red), &g_redBrush));
	VerifyHR(g_hwndRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Black), &g_blackBrush));
	VerifyHR(g_hwndRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::White), &g_whiteBrush));
	VerifyHR(g_hwndRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Orange, 0.15f), &g_yellowBrush));
	VerifyHR(g_hwndRenderTarget->CreateSolidColorBrush(D2D1::ColorF(D2D1::ColorF::Navy, 0.2f), &g_selectionBrush));
	
	for(int i=0; i<4; ++i)
	{
		D2D1_STROKE_STYLE_PROPERTIES strokeStyleProperties = D2D1::StrokeStyleProperties();
		strokeStyleProperties.dashStyle = D2D1_DASH_STYLE_DASH;
		strokeStyleProperties.dashOffset = -static_cast<float>(i) * 1.0f;
		ComPtr<ID2D1StrokeStyle> strokeStyle;
		VerifyHR(g_d2dFactory->CreateStrokeStyle(strokeStyleProperties, nullptr, 0, &strokeStyle));
		g_marchingAnts.push_back(strokeStyle);
	}

	VerifyHR(DWriteCreateFactory(DWRITE_FACTORY_TYPE_ISOLATED, __uuidof(IDWriteFactory), &g_dwriteFactory));

	VerifyHR(g_dwriteFactory->CreateTextFormat(L"Courier New", nullptr, DWRITE_FONT_WEIGHT_NORMAL, DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, g_fontSize, L"en-us", &g_textFormat));
	VerifyHR(g_textFormat->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP));

	DisableMenuItem(hwnd, ID_FILE_SAVE);
	DisableMenuItem(hwnd, ID_EDIT_UNDO);
	DisableMenuItem(hwnd, ID_EDIT_CUT);
	DisableMenuItem(hwnd, ID_EDIT_COPY);
	DisableMenuItem(hwnd, ID_EDIT_PASTE);
	DisableMenuItem(hwnd, ID_EDIT_CUT);
	DisableMenuItem(hwnd, ID_EDIT_DELETE);
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

void Draw(HWND hwnd)
{
	g_hwndRenderTarget->BeginDraw();
	g_hwndRenderTarget->Clear(D2D1::ColorF(D2D1::ColorF::LightGray));

	if (g_isFileLoaded)
	{
		DrawDocument();	
	}
	
	VerifyHR(g_hwndRenderTarget->EndDraw());

	VerifyHR(g_swapChain->Present(1, 0));
}

float ClampToRange(float value, float min, float max)
{
	if (value < min)
		return min;

	if (value > max)
		return max;

	return value;
}

void OnMouseLeftButtonDown(HWND hwnd, LPARAM lParam)
{
	if (!g_isFileLoaded)
		return;

	g_isDragging = true;
	DisableTextSelectionRectangle(hwnd);

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
		SetCaretCharacterIndex(g_start.HitTest.textPosition);
	}
}

void OnMouseLeftButtonUp(HWND hwnd)
{
	if (!g_isFileLoaded)
		return;

	g_isDragging = false;
}

static void UpdateTextSelectionRectangle()
{
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

void OnMouseMove(HWND hwnd, WPARAM wParam, LPARAM lParam)
{		
	if (g_isDragging)
	{
		int xPos = GET_X_LPARAM(lParam);
		int yPos = GET_Y_LPARAM(lParam);

		g_current.Location.x = static_cast<float>(xPos);
		g_current.Location.y = static_cast<float>(yPos);

		D2D1_RECT_F layoutRectangleInScreenSpace = g_layoutInfo.GetLayoutRectangleInScreenSpace();
		g_current.Location.x = ClampToRange(g_current.Location.x, layoutRectangleInScreenSpace.left, layoutRectangleInScreenSpace.right);
		g_current.Location.y = ClampToRange(g_current.Location.y, layoutRectangleInScreenSpace.top, layoutRectangleInScreenSpace.bottom);

		D2D1_POINT_2F layoutPosition = g_layoutInfo.GetPosition();
		VerifyHR(g_textLayout->HitTestPoint(
			g_current.Location.x - layoutPosition.x,
			g_current.Location.y - layoutPosition.y,
			&g_current.IsTrailing, 
			&g_current.OverlaysText, 
			&g_current.HitTest));

		if (g_start.OverlaysText || g_current.OverlaysText)
		{
			EnableTextSelectionRectangle(hwnd);
			UpdateTextSelectionRectangle();
		}
		else
		{
			DisableTextSelectionRectangle(hwnd);
		}

		if (g_current.OverlaysText)
		{
			SetCaretCharacterIndex(g_current.HitTest.textPosition);
		}
	}
}

void OnWindowResize(HWND hwnd)
{
	g_hwndRenderTarget->SetTarget(nullptr);

	auto windowSize = GetWindowSize(hwnd);
	VerifyHR(g_swapChain->ResizeBuffers(2, windowSize.width, windowSize.height, DXGI_FORMAT_B8G8R8A8_UNORM, 0));
	
	SetTargetToBackBuffer();
	
	SetScrollPositions(hwnd);
}

static void FinalizeSelectionBoxMovement(HWND hwnd)
{
	RecreateTextLayout();

	VerifyHR(g_textLayout->HitTestTextPosition(g_start.HitTest.textPosition, FALSE, &g_start.Location.x, &g_start.Location.y, &g_start.HitTest));
	VerifyHR(g_textLayout->HitTestTextPosition(g_current.HitTest.textPosition, FALSE, &g_current.Location.x, &g_current.Location.y, &g_current.HitTest));

	UpdateTextSelectionRectangle();
}

void AddAction(HWND hwnd, Action const& a)
{
	EnableMenuItem(hwnd, ID_EDIT_UNDO);

	if (g_undoBuffer.size() == 5) // Stack limit
	{
		g_undoBuffer.erase(g_undoBuffer.begin());
	}
	g_undoBuffer.push_back(a);
}

static void DeleteBlock(HWND hwnd)
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
	AddAction(hwnd, a);

	for (int y = selection.Top; y <= selection.Bottom; y++)
	{
		for (int x = selection.Left; x <= selection.Right; x++)
		{
			TrySetCharacter(y, x, L' ');
		}
	}

}

static bool TryMoveSelectedBlock(HWND hwnd, WPARAM wParam)
{
	if (!g_hasTextSelectionRectangle)
		return false;

	SignedRect selection = GetTextSelectionRegion();

	if (wParam == 37) //  selection.Left
	{
		if (static_cast<int>(selection.Left) <= 0)
			return false;

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

		SetCaretCharacterIndex(g_caretCharacterIndex - 1);
	}
	else if (wParam == 38) //  up
	{
		if (selection.Top <= 0)
			return false;

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
		SetCaretCharacterIndex(g_textLineStarts[caretRow] + caretColumn);
	}
	else if (wParam == 39)
	{
		if (static_cast<int>(selection.Right) >= g_maxLineLength - 1)
			return false;

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

		SetCaretCharacterIndex(g_caretCharacterIndex + 1);
	}
	else if (wParam == 40) // down
	{
		if (static_cast<int>(selection.Bottom) >= static_cast<int>(g_textLineStarts.size()) - 1)
			return false;

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
		SetCaretCharacterIndex(g_textLineStarts[caretRow] + caretColumn);
	}

	FinalizeSelectionBoxMovement(hwnd);
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

void TryMoveCaretDirectional(HWND hwnd, WPARAM wParam)
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
				SetCaretCharacterIndex(g_textLineStarts[caretRow] + caretColumn);
			}
		}
		else
		{
			caretColumn--;
			SetCaretCharacterIndex(g_textLineStarts[caretRow] + caretColumn);
		}
	}
	else if (wParam == 38) // up
	{
		if (caretRow == 0) {}
		else
		{
			caretRow--;
			SetCaretCharacterIndex(g_textLineStarts[caretRow] + caretColumn);
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
				SetCaretCharacterIndex(g_textLineStarts[caretRow] + caretColumn);
			}
		}
		else
		{
			caretColumn++;
			SetCaretCharacterIndex(g_textLineStarts[caretRow] + caretColumn);
		}
	}
	else if (wParam == 40) // down
	{
		if (caretRow == g_textLineStarts.size() - 1) {}
		else
		{
			caretRow++;
			SetCaretCharacterIndex(g_textLineStarts[caretRow] + caretColumn);
		}
	}
}

void WriteCharacterAtCaret(wchar_t chr)
{
	// Put a letter at the caret
	g_allText[g_caretCharacterIndex] = chr;
	RecreateTextLayout();

	SetCaretCharacterIndex(g_caretCharacterIndex+1);
}

void OnKeyDown(HWND hwnd, WPARAM wParam)
{
	if (!g_isFileLoaded)
		return;

	g_caretBlinkState = 0;

	if (g_keyOutput[wParam].Valid)
	{
		DisableTextSelectionRectangle(hwnd);
		
		Action a;
		a.Type = Action::WriteCharacter;
		std::vector<wchar_t> line;
		line.push_back(g_allText[g_caretCharacterIndex]);
		a.OverwrittenChars.push_back(line);
		a.TextPosition = g_caretCharacterIndex;

		AddAction(hwnd, a);

		wchar_t chr = g_isShiftDown ? g_keyOutput[wParam].Uppercase : g_keyOutput[wParam].Lowercase;
		WriteCharacterAtCaret(chr);
	}
	else if (wParam == 8) // Backspace
	{
		DisableTextSelectionRectangle(hwnd);

		if (g_caretCharacterIndex > 0)
		{
			SetCaretCharacterIndex(g_caretCharacterIndex - 1);
		}

		Action a;
		a.Type = Action::Backspace;
		std::vector<wchar_t> line;
		line.push_back(g_allText[g_caretCharacterIndex]);
		a.OverwrittenChars.push_back(line);
		a.TextPosition = g_caretCharacterIndex;
		AddAction(hwnd, a);

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
			SetCaretCharacterIndex(g_textLineStarts[caretRow] + caretColumn);
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
			TryMoveSelectedBlock(hwnd, wParam);
		}
		else if (g_isCtrlDown)
		{
			TryMoveViewWithKeyboard(wParam);
		}
		else
		{
			TryMoveCaretDirectional(hwnd, wParam);
		}
	}
	else if (wParam == 46) // Delete key
	{
		DisableTextSelectionRectangle(hwnd);
		DeleteBlock(hwnd);
		RecreateTextLayout();
	}
}

void OnKeyUp(HWND hwnd, WPARAM wParam)
{
	if (wParam == 16) // Shift key
	{
		g_isShiftDown = false;
	}
	else if (wParam == 17)
	{
		g_isCtrlDown = false;
	}
}

void Update()
{
	g_marchingAntsIndex = (g_marchingAntsIndex + 1) % (5 * g_marchingAnts.size());
	g_caretBlinkState = (g_caretBlinkState + 1) % 50;
}

void OnHorizontalScroll(HWND hwnd, WPARAM wParam)
{
	WORD scrollPosition = HIWORD(wParam);
	WORD type = LOWORD(wParam);

	if (type != SB_THUMBPOSITION && type != TB_THUMBTRACK)
		return;

	g_layoutInfo.AdjustPositionX(-static_cast<float>(scrollPosition));

	UpdateTextSelectionRectangle();

	SCROLLINFO scrollInfo{};
	scrollInfo.cbSize = sizeof(scrollInfo);
	scrollInfo.fMask = SIF_POS;
	scrollInfo.nPos = scrollPosition;
	SetScrollInfo(hwnd, SB_HORZ, &scrollInfo, TRUE);

	InvalidateRect(hwnd, nullptr, FALSE);
}

void OnVerticalScroll(HWND hwnd, WPARAM wParam)
{
	WORD scrollPosition = HIWORD(wParam);	
	WORD type = LOWORD(wParam);

	if (type != SB_THUMBPOSITION && type != TB_THUMBTRACK)
		return;

	g_layoutInfo.AdjustPositionY(-static_cast<float>(scrollPosition));

	UpdateTextSelectionRectangle();
	
	SCROLLINFO scrollInfo{};
	scrollInfo.cbSize = sizeof(scrollInfo);
	scrollInfo.fMask = SIF_POS;
	scrollInfo.nPos = scrollPosition;
	SetScrollInfo(hwnd, SB_VERT, &scrollInfo, TRUE);

	InvalidateRect(hwnd, nullptr, FALSE);
}

void OnMouseWheel(HWND hwnd, WPARAM wParam)
{
	if (!g_isFileLoaded)
		return;

	if (g_verticalScrollLimit < 0)
		return; // Whole document fits in window

	SCROLLINFO scrollInfo{};
	scrollInfo.cbSize = sizeof(scrollInfo);
	scrollInfo.fMask = SIF_POS;
	GetScrollInfo(hwnd, SB_VERT, &scrollInfo);

	int wheelDelta = GET_WHEEL_DELTA_WPARAM(wParam);
	int additionalScrollAmount = wheelDelta;
	int newScrollAmount = scrollInfo.nPos - additionalScrollAmount;

	if (newScrollAmount < 0) 
		newScrollAmount = 0;
	
	if (newScrollAmount > g_verticalScrollLimit)
		newScrollAmount = g_verticalScrollLimit;

	g_layoutInfo.AdjustPositionY(-static_cast<float>(newScrollAmount));

	UpdateTextSelectionRectangle();

	scrollInfo.nPos = newScrollAmount;
	SetScrollInfo(hwnd, SB_VERT, &scrollInfo, TRUE);
}

void OnOpen(HWND hwnd)
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
	ofn.hwndOwner = hwnd;
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

	if (GetOpenFileName(&ofn) == TRUE)
	{
		LoadFile(hwnd, ofn.lpstrFile);
	}
}

void OnSave()
{
	g_isCtrlDown = false;

	{
		std::wofstream out(g_fileName);
		out << g_allText;
	}

	MessageBox(nullptr, L"Save completed.", L"ColumnMode", MB_OK);
}

void OnUndo(HWND hwnd)
{
	auto const& top = g_undoBuffer.back();
	
	// Pop item off stack
	if (top.Type == Action::WriteCharacter)
	{
		g_allText[top.TextPosition] = top.OverwrittenChars[0][0];
		RecreateTextLayout();
		SetCaretCharacterIndex(top.TextPosition);
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
		SetCaretCharacterIndex(newTextPosition);
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

		SetCaretCharacterIndex(top.TextPosition);
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

		SetCaretCharacterIndex(top.TextPosition);

		g_start = top.DragData[0];
		g_current = top.DragData[1];
		EnableTextSelectionRectangle(hwnd);
		UpdateTextSelectionRectangle();
	}
	else
	{
		assert(top.Type == Action::DeleteBlock);

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
		SetCaretCharacterIndex(top.TextPosition);

		g_start = top.DragData[0];
		g_current = top.DragData[1];
		EnableTextSelectionRectangle(hwnd);
		UpdateTextSelectionRectangle();
	}

	g_undoBuffer.pop_back();

	if (g_undoBuffer.size() == 0)
		DisableMenuItem(hwnd, ID_EDIT_UNDO);
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
	GlobalUnlock(hMem);

	OpenClipboard(0);
	EmptyClipboard();
	SetClipboardData(CF_UNICODETEXT, hMem);
	CloseClipboard();
}

void OnDelete(HWND hwnd)
{
	DeleteBlock(hwnd);

	RecreateTextLayout();
}

void OnCut(HWND hwnd)
{
	CopySelectionToClipboard();

	DeleteBlock(hwnd);

	RecreateTextLayout();
}

void OnCopy(HWND hwnd)
{
	CopySelectionToClipboard();
}

void OnPaste(HWND hwnd)
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

	AddAction(hwnd, a);

	RecreateTextLayout();
}

void OnClipboardContentsChanged(HWND hwnd)
{
	UpdatePasteEnablement(hwnd);
}