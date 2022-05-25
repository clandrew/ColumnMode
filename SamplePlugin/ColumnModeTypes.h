#pragma once

namespace ColumnMode
{
	//WindowManager Types------------------------------------------------------------------------
	struct CreateWindowArgs
	{
		DWORD exWindowStyle;
		ATOM windowClass;	//Get the ATOM by calling CreateWindowClass
		LPCTSTR windowName;
		int width;
		int height;
	};
}