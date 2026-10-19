#pragma once

#include <windows.h>

namespace popup_surface {
bool RegisterShadowClass(HINSTANCE instance);
HWND CreateShadow(HINSTANCE instance, const RECT& body);
void UpdateShadow(HWND menu);
bool PaintCard(HWND menu, HDC content);
}
