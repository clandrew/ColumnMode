#include "stdafx.h"

// Recursively look for the menu and position pair for the item identified by searchId
bool FindMenuPos(HMENU baseMenu, UINT searchId, HMENU& outRealBaseMenu, int& outPos)
{
	int myPos;
	if (baseMenu == NULL)
	{
		// Sorry, Wrong Number
		outRealBaseMenu = NULL;
		outPos = -1;
		return true;
	}
	for (myPos = GetMenuItemCount(baseMenu) - 1; myPos >= 0; myPos--)
	{
		int Status = GetMenuState(baseMenu, myPos, MF_BYPOSITION);
		HMENU mNewMenu;

		if (Status == 0xFFFFFFFF)
		{
			// That was not a legal Menu/Position-Combination
			outRealBaseMenu = NULL;
			outPos = -1;
			return true;
		}
		// Is this the real one?
		if (GetMenuItemID(baseMenu, myPos) == searchId)
		{
			// Yep!
			outRealBaseMenu = baseMenu;
			outPos = myPos;
			return true;
		}
		// Maybe a subMenu?
		mNewMenu = GetSubMenu(baseMenu, myPos);
		// This function will return NULL if ther is NO SubMenu
		if (mNewMenu != NULL)
		{
			// recursivly look for the right menu, depth first search
			bool found = FindMenuPos(mNewMenu, searchId, outRealBaseMenu, outPos);
			if (found)
				return true;	// return this loop
		}
	}
	return false; // iterate in the upper stackframe
}
