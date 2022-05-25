#pragma once

void inline VerifyHR(HRESULT hr)
{
	if (FAILED(hr))
		__debugbreak();
}

void inline VerifyBool(BOOL b)
{
	if (!b)
		__debugbreak();
}