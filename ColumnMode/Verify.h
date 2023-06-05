#pragma once

void inline VerifyHR(HRESULT hr)
{
	if (FAILED(hr))
		__debugbreak();
}

#define BAIL_ON_FAIL_HR(hrFunc) {HRESULT hr = hrFunc; if(FAILED(hr)) { return hr;}}

void inline VerifyBool(BOOL b)
{
	if (!b)
		__debugbreak();
}