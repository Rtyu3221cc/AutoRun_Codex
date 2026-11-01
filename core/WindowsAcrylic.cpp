#include "WindowsAcrylic.h"

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace arc {

#ifdef Q_OS_WIN
namespace {

enum AccentState
{
    ACCENT_DISABLED = 0,
    ACCENT_ENABLE_GRADIENT = 1,
    ACCENT_ENABLE_TRANSPARENTGRADIENT = 2,
    ACCENT_ENABLE_BLURBEHIND = 3,
    ACCENT_ENABLE_ACRYLICBLURBEHIND = 4,
    ACCENT_ENABLE_HOSTBACKDROP = 5
};

struct AccentPolicy
{
    int AccentState;
    int AccentFlags;
    int GradientColor;
    int AnimationId;
};

struct WindowCompositionAttributeData
{
    int Attribute;
    PVOID Data;
    ULONG SizeOfData;
};

using SetWindowCompositionAttributeFn = BOOL(WINAPI *)(HWND, WindowCompositionAttributeData *);
constexpr int WCA_ACCENT_POLICY = 19;

} // namespace
#endif

bool applyWindowAcrylic(QWidget *widget, const QColor &tint, int tintAlpha)
{
    if (!widget)
        return false;
#ifdef Q_OS_WIN
    HWND hwnd = reinterpret_cast<HWND>(widget->winId());
    if (!hwnd)
        return false;

    static HMODULE user32 = LoadLibraryW(L"user32.dll");
    static auto setCompositionAttribute = user32
            ? reinterpret_cast<SetWindowCompositionAttributeFn>(
                      GetProcAddress(user32, "SetWindowCompositionAttribute"))
            : nullptr;
    if (!setCompositionAttribute)
        return false;

    AccentPolicy policy{};
    policy.AccentState = ACCENT_ENABLE_ACRYLICBLURBEHIND;
    policy.AccentFlags = 2;
    const int alpha = qBound(0, tintAlpha, 255);
    // GradientColor 是 ABGR 顺序
    policy.GradientColor = (alpha << 24) | (tint.blue() << 16) | (tint.green() << 8) | tint.red();
    policy.AnimationId = 0;

    WindowCompositionAttributeData data{};
    data.Attribute = WCA_ACCENT_POLICY;
    data.Data = &policy;
    data.SizeOfData = sizeof(policy);
    return setCompositionAttribute(hwnd, &data) != FALSE;
#else
    Q_UNUSED(tint);
    Q_UNUSED(tintAlpha);
    return false;
#endif
}

} // namespace arc
