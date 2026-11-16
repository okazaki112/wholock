#pragma once
// ui/Theme —— 视觉单一事实源：浅色产品风令牌、字体级差、通用绘制与控件行为。
// 绘制仅发生在 UI 线程消息内（hover/画刷缓存不加锁）；零第三方依赖（GDI+comctl32）。
#include <windows.h>
#include <commctrl.h>
#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "../domain/LockInfo.h"

namespace who::ui {

// ---------- 颜色令牌（浅色主题唯一色板；禁止在别处写 RGB 字面量） ----------
namespace col {
inline constexpr COLORREF Window     = RGB(248, 249, 251); // 窗口底
inline constexpr COLORREF Surface     = RGB(255, 255, 255); // 内容面/控件底
inline constexpr COLORREF Hairline     = RGB(224, 228, 232); // 1px 分隔线
inline constexpr COLORREF Ink          = RGB(24, 29, 35);    // 主文字
inline constexpr COLORREF InkSoft      = RGB(102, 112, 124); // 次要文字
inline constexpr COLORREF InkDisabled  = RGB(170, 177, 186); // 禁用文字
inline constexpr COLORREF Accent       = RGB(37, 99, 235);   // 品牌蓝（主操作）
inline constexpr COLORREF AccentHover  = RGB(30, 86, 208);
inline constexpr COLORREF AccentPressed = RGB(26, 73, 178);
inline constexpr COLORREF AccentSoftBg = RGB(232, 240, 254); // 选中行底
inline constexpr COLORREF NeutralHover   = RGB(244, 246, 249);
inline constexpr COLORREF NeutralPressed = RGB(233, 236, 240);
inline constexpr COLORREF Danger        = RGB(214, 44, 44); // 危险文字/框
inline constexpr COLORREF DangerHover    = RGB(248, 237, 237);
inline constexpr COLORREF DangerPressed  = RGB(240, 220, 220);
inline constexpr COLORREF RowAlt        = RGB(247, 248, 250); // 交替行
inline constexpr COLORREF DisabledBg     = RGB(246, 247, 249);
inline constexpr COLORREF Ok    = RGB(21, 128, 61);
inline constexpr COLORREF Warn  = RGB(180, 98, 7);
inline constexpr COLORREF Error = RGB(204, 32, 32);
inline constexpr COLORREF Info  = RGB(102, 112, 124);
} // namespace col

enum class BtnTone : int { Neutral = 0, Primary, Danger };

// ---------- 字体级差（微软雅黑；px 尺寸随 DPI 换算；用户偏好 ≥14px） ----------
struct Fonts {
    HFONT brand = nullptr, subtitle = nullptr, label = nullptr, body = nullptr, bodyBold = nullptr,
          button = nullptr, status = nullptr, cell = nullptr, cellBold = nullptr, header = nullptr;
    static Fonts Create(UINT dpi);
    void Destroy() noexcept;
};

int Scale(int v, UINT dpi) noexcept;
HBRUSH Brush(COLORREF c) noexcept; // 进程生命周期缓存

// 列表两列语义文字（ui 与 cli 共用单一事实源）
std::wstring kindText(who::LockKind k);
std::wstring engineText(std::uint8_t engines);

// ---------- 绘制原语 ----------
void DrawBrand(HDC dc, const RECT& rc, const Fonts& f, UINT dpi, const wchar_t* subtitle);
void DrawSeparatorH(HDC dc, int x1, int x2, int y) noexcept; // 1px hairline，1pt 细分隔线
void DrawEditFrame(HDC dc, const RECT& rcEditClient, bool focused) noexcept; // edit 自绘边框替代 native edge

enum class StatusLevel { Info, Ok, Warn, Error };
// 多行状态：首行=级别色圆点+着色文字，其余行次要灰；无任何投影
void DrawStatus(HDC dc, const RECT& rc, const Fonts& f, UINT dpi, StatusLevel lvl, const std::wstring& text);

// owner-draw 扁平按钮（hover 状态来自 AttachFlatButton 的内部跟踪）；hover/按下态由子类化维护
void DrawAttachedButton(LPDRAWITEMSTRUCT dis);

// ---------- 控件行为附着（SetWindowSubclass） ----------
void AttachFlatButton(HWND btn, BtnTone tone, const Fonts* f, UINT dpi);
void AttachEditFocus(HWND edit);
bool EditIsFocused(HWND edit) noexcept;

// ---------- ListView 全行 owner-draw（交替行/选中 accent 底/垂直居中/路径省略号/空态） ----------
struct RowView { std::wstring pid, name, kind, eng, path, occ; }; // v1.0 F4：occ=占用文件（目录/批量时定位到具体文件）
struct ListDrawCtx {
    const std::vector<RowView>* rows = nullptr;
    const Fonts* f = nullptr;
    UINT dpi = 96;
    const wchar_t* emptyHint = L"尚未扫描";
};
LRESULT ListCustomDraw(LPNMLVCUSTOMDRAW cd, const ListDrawCtx& ctx);
void StyleListView(HWND lv, const Fonts& f, UINT dpi, int rowPx = 34); // bold 表头/行高/双缓冲/白底 // bold 表头/行高/双缓冲/白底

} // namespace who::ui
