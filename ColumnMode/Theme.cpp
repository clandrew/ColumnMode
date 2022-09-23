#include "stdafx.h"

#include "Theme.h"
#include "Verify.h"

using namespace ColumnMode;
using namespace D2D1;

void Theme::CreateDefaultTheme(Theme& out)
{
	out.SetColor(THEME_COLOR::UI_BACKGROUND, ColorF::LightGray);
	out.SetColor(THEME_COLOR::UI_PAPER, ColorF::White);
	out.SetColor(THEME_COLOR::UI_PAPER_BORDER, ColorF::Black);
	out.SetColor(THEME_COLOR::UI_MARGIN, ColorF::LightGray);
	out.SetColor(THEME_COLOR::UI_CURRENT_LINE_HIGHLIGHT, ColorF::LightYellow);
	out.SetColor(THEME_COLOR::UI_CARET, ColorF::Black);
	out.SetColor(THEME_COLOR::UI_DRAG_RECT, ColorF::Red);
	out.SetColor(THEME_COLOR::UI_SELECTION, ColorF(ColorF::Navy, .2f));
	out.SetColor(THEME_COLOR::UI_SELECTION_BORDER, ColorF::Black);

	out.SetColor(THEME_COLOR::TEXT_DEFAULT, ColorF::Black);
	out.SetColor(THEME_COLOR::TEXT_KEYWORD, ColorF::Blue);
	out.SetColor(THEME_COLOR::TEXT_COMMENT, ColorF::DarkSeaGreen);
	out.SetColor(THEME_COLOR::TEXT_FUNCTION, ColorF::Gold);
	out.SetColor(THEME_COLOR::TEXT_OPERATOR, ColorF::DarkGray);
	out.SetColor(THEME_COLOR::TEXT_FLOW_CONTROL, ColorF::DarkSalmon);
	out.SetColor(THEME_COLOR::TEXT_STRING, ColorF::ForestGreen);
	out.SetColor(THEME_COLOR::TEXT_PAIRED_SYMBOL, { ColorF::AliceBlue, ColorF::Orange, ColorF::Indigo });
}

ColorF Theme::GetColor(THEME_COLOR color)
{
	if (auto it = colorMap.find(color); it != colorMap.end())
	{
		return it->second;
	}
	return ColorF::Magenta;
}

void Theme::SetColor(THEME_COLOR color, ColorF newColor)
{
	// Get around []'s requirement for a default ctor for ColorF
	colorMap.insert_or_assign(color, newColor);
}


#pragma region BrushCache
void ColumnMode::BrushCache::Reset(ID2D1DeviceContext* pDeviceContext)
{
	m_brushCache.clear();
	h_pDeviceContext = pDeviceContext;
}

ID2D1SolidColorBrush* ColumnMode::BrushCache::GetBrush(D2D1::ColorF color)
{
	assert(h_pDeviceContext != nullptr);
	if (auto it = m_brushCache.find(color); it != m_brushCache.end())
	{
		return it->second.Get();
	}
	auto pair = m_brushCache.insert_or_assign(color, ComPtr<ID2D1SolidColorBrush>());
	VerifyHR(h_pDeviceContext->CreateSolidColorBrush(color, &pair.first->second));
	return pair.first->second.Get();
}
#pragma endregion
