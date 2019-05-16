#pragma once

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