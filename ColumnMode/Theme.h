#pragma once

namespace ColumnMode
{
	enum class THEME_COLOR
	{
		// UI colors
		UI_BACKGROUND,
		UI_PAPER,
		UI_PAPER_BORDER,
		UI_MARGIN,
		UI_CURRENT_LINE_HIGHLIGHT,
		UI_CARET,
		UI_DRAG_RECT,
		UI_SELECTION,
		UI_SELECTION_BORDER,

		// Text Colors
		TEXT_DEFAULT,
		TEXT_KEYWORD,
		TEXT_COMMENT,
		TEXT_FUNCTION,
		TEXT_OPERATOR,
		TEXT_FLOW_CONTROL,
		TEXT_STRING,
		TEXT_PAIRED_SYMBOL,
	};

	class Theme
	{
	public:
		static bool LoadThemeFromFile(std::wstring filepath, Theme& out);
		static void CreateDefaultTheme(Theme& out);

	public:
		D2D1::ColorF GetColor(THEME_COLOR color);
		void SetColor(THEME_COLOR color, D2D1::ColorF newColor);
		D2D1::ColorF GetColor(THEME_COLOR color, UINT index);
		void SetColors(THEME_COLOR color, std::vector<D2D1::ColorF> newColors);
		
		bool SaveThemeToFile(std::wstring filepath);
	private:
		// Used for 1:! mappings of THEME_COLOR to colors
		std::unordered_map<THEME_COLOR, D2D1::ColorF> colorMap;
		// Used for things that should cycle colors (1:n), such as pairs of parens
		std::unordered_map<THEME_COLOR, std::vector<D2D1::ColorF>> m_colorVectorMap;
	};

	struct D2D1_ColorF_Hash
	{
		std::size_t operator()(D2D1::ColorF color) const noexcept
		{
			auto floatHash = std::hash<float>{};
			return floatHash(color.r) ^
				(floatHash(color.g) << 1) ^
				(floatHash(color.b) << 2) ^
				(floatHash(color.a) << 3);
		}
	};

	struct D2D1_ColorF_EqualTo
	{
		bool operator()(const D2D1::ColorF lhs, const D2D1::ColorF rhs) const noexcept
		{
			return lhs.r == rhs.r &&
				lhs.g == rhs.g &&
				lhs.b == rhs.b &&
				lhs.a == rhs.a;
		}
	};
	

	class BrushCache
	{
	public:
		void Reset(ID2D1DeviceContext* h_deviceContext);
		ID2D1SolidColorBrush* GetBrush(D2D1::ColorF color);
	private:
		std::unordered_map <D2D1::ColorF, ComPtr<ID2D1SolidColorBrush>, D2D1_ColorF_Hash, D2D1_ColorF_EqualTo> m_brushCache;
		ID2D1DeviceContext* h_pDeviceContext;
	};
}