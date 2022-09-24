#include "stdafx.h"
#include "../External/json.hpp"
#include "Theme.h"
#include "Verify.h"
#include "utf8Conversion.h"

namespace ColumnMode
{
	NLOHMANN_JSON_SERIALIZE_ENUM(THEME_COLOR, {
	{THEME_COLOR::UI_BACKGROUND, "UI_BACKGROUND"},
	{THEME_COLOR::UI_PAPER, "UI_PAPER"},
	{THEME_COLOR::UI_PAPER_BORDER, "UI_PAPER_BORDER"},
	{THEME_COLOR::UI_MARGIN, "UI_MARGIN"},
	{THEME_COLOR::UI_CURRENT_LINE_HIGHLIGHT, "UI_CURRENT_LINE_HIGHLIGHT"},
	{THEME_COLOR::UI_CARET, "UI_CARET"},
	{THEME_COLOR::UI_DRAG_RECT, "UI_DRAG_RECT"},
	{THEME_COLOR::UI_SELECTION, "UI_SELECTION"},
	{THEME_COLOR::UI_SELECTION_BORDER, "UI_SELECTION_BORDER"},

	{THEME_COLOR::TEXT_DEFAULT, "TEXT_DEFAULT"},
	{THEME_COLOR::TEXT_KEYWORD, "TEXT_KEYWORD"},
	{THEME_COLOR::TEXT_COMMENT, "TEXT_COMMENT"},
	{THEME_COLOR::TEXT_FUNCTION, "TEXT_FUNCTION"},
	{THEME_COLOR::TEXT_OPERATOR, "TEXT_OPERATOR"},
	{THEME_COLOR::TEXT_FLOW_CONTROL, "TEXT_FLOW_CONTROL"},
	{THEME_COLOR::TEXT_STRING, "TEXT_STRING"},
	{THEME_COLOR::TEXT_PAIRED_SYMBOL, "TEXT_PAIRED_SYMBOL"},
		})
}

using namespace ColumnMode;
using namespace D2D1;
using json = nlohmann::json;

#pragma region JSON_Helpers
namespace nlohmann
{
	template<>
	struct adl_serializer<D2D1::ColorF>
	{
		static void to_json(json& j, const D2D1::ColorF& color)
		{
			j = json{ {"r", color.r}, {"g", color.g}, {"b", color.b}, {"a", color.a} };
		}

		static void from_json(const json& j, D2D1::ColorF& color)
		{
			if (j.is_null())
			{
				color = D2D1::ColorF::Magenta;
			}
			else
			{
				j.at("r").get_to(color.r);
				j.at("g").get_to(color.g);
				j.at("b").get_to(color.b);
				j.at("a").get_to(color.a);
			}
		}
	};

	template <>
	struct adl_serializer<std::wstring>
	{
		static void to_json(json& j, const std::wstring& str)
		{
			j = to_utf8(str);
		}

		static void from_json(const json& j, std::wstring& str)
		{
			str = from_utf8(j.get<std::string>());
		}
	};
}


void ColumnMode::to_json(json& j, const Theme& t)
{
	j["name"] = t.m_name;
	for (auto it : t.m_colorMap)
	{
		j["color_map"].push_back(json{ {"theme.id", it.first}, {"theme.color", it.second}});
	}
}

void ColumnMode::from_json(const json& j, Theme& t)
{
	j.at("name").get_to(t.m_name);
	const json& colorMap = j.at("color_map");


	for (auto& it : colorMap.items())
	{
		json& item = (json&)(it.value());
		ColumnMode::THEME_COLOR tc;
		ColorF col = ColorF::Magenta;
		item.at("theme.id").get_to(tc);
		item.at("theme.color").get_to(col);

		t.m_colorMap.insert_or_assign(tc, col);
	}
}
#pragma endregion

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

	out.m_name = L"ColumnMode Classic";
}

ColorF Theme::GetColor(THEME_COLOR color)
{
	if (auto it = m_colorMap.find(color); it != m_colorMap.end())
	{
		return it->second;
	}
	return ColorF::Magenta;
}

void Theme::SetColor(THEME_COLOR color, ColorF newColor)
{
	// Get around []'s requirement for a default ctor for ColorF
	m_colorMap.insert_or_assign(color, newColor);
}

#pragma region ThemeManager

ThemeManager::ThemeManager()
{
	EnsureThemePathExists();
	ScanForThemes();
}

bool ColumnMode::ThemeManager::LoadTheme(std::wstring themeName, Theme& out)
{
	std::filesystem::path path(m_themesRootPath);
	path.append(themeName)	//Plugins should be in a folder of the plugin name
		.replace_extension(L".cmt"); // Column Mode Theme

	if (std::filesystem::exists(path))
	{
		std::ifstream file(path);
		json data = json::parse(file);
		out = data;
	}
	else
	{
		Theme::CreateDefaultTheme(out);
		SaveTheme(out);
	}
	return false;
}

bool ColumnMode::ThemeManager::SaveTheme(Theme& theme)
{
	json data = theme;
	std::filesystem::path path(m_themesRootPath);
	path.append(theme.GetName())	//Plugins should be in a folder of the plugin name
		.replace_extension(L".cmt"); // Column Mode Theme
	
	std::ofstream file(path);
	file << data.dump(2);
	file.close();

	return true;
}

void ColumnMode::ThemeManager::ScanForThemes()
{
	m_availableThemes.clear();
	std::filesystem::directory_iterator dirIter(m_themesRootPath, std::filesystem::directory_options::none);
	for (auto& file : dirIter)
	{
		if (!file.is_directory())
		{
			// Themes are single files, currently not supporting folders of themes
			std::wstring dirStr = file.path().filename().replace_extension(L"").wstring();
			m_availableThemes.push_back(std::move(dirStr));
		}
	}
	std::sort(m_availableThemes.begin(), m_availableThemes.end());
}

void ThemeManager::EnsureThemePathExists()
{
	PWSTR appdataRoaming = nullptr;
	VerifyHR(SHGetKnownFolderPath(FOLDERID_RoamingAppData, KF_FLAG_CREATE, NULL /*currentUser*/, &appdataRoaming));

	m_themesRootPath.assign(appdataRoaming)
		.append(L"ColumnMode")			//Probably best to load from Resources, but need a HINSTANCE
		.append(L"Themes");

	std::filesystem::create_directories(m_themesRootPath);

	CoTaskMemFree(appdataRoaming);
}

#pragma endregion



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


