#include "Theme.h"
#include <unordered_map>

namespace who::ui {
namespace {

struct BtnState {
    BtnTone tone = BtnTone::Neutral;
    bool hover = false;
    const Fonts* fonts = nullptr;
    UINT dpi = 96;
    std::wstring title; // ownerdraw 不走窗口文本，附着时缓存
};

std::unordered_map<HWND, BtnState>& BtnRegistry() { // 绘制全在 UI 线程消息内 → 免锁
    static std::unordered_map<HWND, BtnState> m;
    return m;
}

HWND g_focusEdit = nullptr;

LRESULT CALLBACK BtnSubProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, ULONG_PTR ref) {
    auto* st = reinterpret_cast<BtnState*>(ref);
    switch (msg) {
    case WM_MOUSEMOVE:
        if (!st->hover) {
            st->hover = true;
            TRACKMOUSEEVENT tme{}; tme.cbSize = sizeof(tme); tme.dwFlags = TME_LEAVE; tme.hwndTrack = h;
            TrackMouseEvent(&tme);
            InvalidateRect(h, nullptr, FALSE);
        }
        break;
    case WM_MOUSELEAVE:
        st->hover = false;
        InvalidateRect(h, nullptr, FALSE);
        break;
    case WM_SETFOCUS:
    case WM_KILLFOCUS:
        InvalidateRect(h, nullptr, FALSE); // 焦点环重画
        break;
    }
    return DefSubclassProc(h, msg, w, l);
}

LRESULT CALLBACK EditFocusSubProc(HWND h, UINT msg, WPARAM w, LPARAM l, UINT_PTR, ULONG_PTR) {
    switch (msg) {
    case WM_SETFOCUS:  g_focusEdit = h; InvalidateRect(GetParent(h), nullptr, FALSE); break;
    case WM_KILLFOCUS: if (g_focusEdit == h) { g_focusEdit = nullptr; InvalidateRect(GetParent(h), nullptr, FALSE); } break;
    case WM_DESTROY:   if (g_focusEdit == h) g_focusEdit = nullptr; break;
    }
    return DefSubclassProc(h, msg, w, l);
}

COLORREF LevelColor(StatusLevel lvl) {
    switch (lvl) {
    case StatusLevel::Ok: return col::Ok;
    case StatusLevel::Warn: return col::Warn;
    case StatusLevel::Error: return col::Error;
    default: return col::Info;
    }
}

void DrawColorDot(HDC dc, COLORREF c, int x, int y, int d) { // 实心圆点，无框无投影
    const HPEN pen = static_cast<HPEN>(GetStockObject(NULL_PEN));
    const HPEN oldPen = (HPEN)SelectObject(dc, pen);
    const HBRUSH oldBr = (HBRUSH)SelectObject(dc, Brush(c));
    Ellipse(dc, x, y, x + d, y + d);
    SelectObject(dc, oldPen); SelectObject(dc, oldBr);
}

} // namespace

int Scale(int v, UINT dpi) noexcept { return MulDiv(v, (int)dpi, 96); }

HBRUSH Brush(COLORREF c) noexcept {
    static std::unordered_map<COLORREF, HBRUSH> cache; // 进程级，色板有限集
    const auto it = cache.find(c);
    if (it != cache.end()) return it->second;
    return cache.emplace(c, CreateSolidBrush(c)).first->second;
}

std::wstring kindText(who::LockKind k) {
    switch (k) {
    case who::LockKind::FileHandle:    return L"文件句柄";
    case who::LockKind::MemoryMap:     return L"内存映射";
    case who::LockKind::ServiceModule: return L"服务模块";
    case who::LockKind::ProcessModule: return L"模块加载";
    default:                           return L"—";
    }
}

std::wstring engineText(std::uint8_t e) {
    const bool rm = e & who::EngineRM, ht = e & who::EngineHT, st = e & who::EngineStub, deep = e & who::EngineDeep;
    std::wstring s;
    auto cat = [&](const wchar_t* t) { if (!s.empty()) s += L"+"; s += t; };
    if (rm) cat(L"RM"); if (ht) cat(L"HT"); if (deep) cat(L"深扫"); if (st) cat(L"Stub");
    if (s.empty()) s = L"—";
    return s;
}

Fonts Fonts::Create(UINT dpi) {
    auto mk = [&](int px, int weight) {
        return CreateFontW(-Scale(px, dpi), 0, 0, 0, weight, 0, 0, 0, DEFAULT_CHARSET, OUT_TT_PRECIS,
                           CLIP_DEFAULT_PRECIS, CLEARTYPE_NATURAL_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
                           L"Microsoft YaHei");
    };
    Fonts f;
    f.brand = mk(22, FW_BOLD); f.subtitle = mk(13, FW_NORMAL); f.label = mk(14, FW_NORMAL);
    f.body = mk(15, FW_NORMAL); f.bodyBold = mk(15, FW_BOLD); f.button = mk(15, FW_NORMAL);
    f.status = mk(14, FW_NORMAL); f.cell = mk(15, FW_NORMAL); f.cellBold = mk(15, FW_SEMIBOLD);
    f.header = mk(14, FW_BOLD);
    return f;
}

void Fonts::Destroy() noexcept {
    for (HFONT* p : { &brand, &subtitle, &label, &body, &bodyBold, &button, &status, &cell, &cellBold, &header }) {
        if (*p) { DeleteObject(*p); *p = nullptr; }
    }
}

void DrawBrand(HDC dc, const RECT& rc, const Fonts& f, UINT dpi, const wchar_t* subtitle) {
    const HANDLE oldFont = SelectObject(dc, f.brand);
    SetBkMode(dc, TRANSPARENT);
    SetTextColor(dc, col::Ink);
    SIZE szBrand{}; GetTextExtentPoint32W(dc, L"WhoLock", 7, &szBrand);
    const int x = Scale(20, dpi);
    const int y = (rc.top + rc.bottom - szBrand.cy) / 2;
    TextOutW(dc, x, y, L"WhoLock", 7);
    if (subtitle && *subtitle) {
        SelectObject(dc, f.subtitle);
        SetTextColor(dc, col::InkSoft);
        SIZE szSub{}; GetTextExtentPoint32W(dc, subtitle, (int)wcslen(subtitle), &szSub);
        TextOutW(dc, x + szBrand.cx + Scale(14, dpi),
                 y + (szBrand.cy - szSub.cy) / 2, subtitle, (int)wcslen(subtitle));
    }
    SelectObject(dc, oldFont);
}

void DrawSeparatorH(HDC dc, int x1, int x2, int y) noexcept {
    const RECT r{ x1, y, x2, y + 1 };
    FillRect(dc, &r, Brush(col::Hairline));
}

void DrawEditFrame(HDC dc, const RECT& rcEditClient, bool focused) noexcept {
    const RECT outer{ rcEditClient.left - 1, rcEditClient.top - 1, rcEditClient.right + 1, rcEditClient.bottom + 1 };
    FrameRect(dc, &outer, Brush(focused ? col::Accent : col::Hairline));
}

void DrawStatus(HDC dc, const RECT& rc, const Fonts& f, UINT dpi, StatusLevel lvl, const std::wstring& text) {
    FillRect(dc, &rc, Brush(col::Window)); // 先铺底：ownerdraw static 的 DC 背景未初始化会露黑块
    const HANDLE oldFont = SelectObject(dc, f.status);
    SetBkMode(dc, TRANSPARENT);
    const int lineH = Scale(19, dpi); // 紧凑：14px 字配 19px 行距，4 行文案恰好在 82px 状态带内
    const int dot = Scale(6, dpi);
    size_t pos = 0; int ln = 0;
    int y = rc.top + Scale(3, dpi);
    const COLORREF cLevel = LevelColor(lvl);
    while (pos <= text.size() && y + Scale(16, dpi) <= rc.bottom + Scale(16, dpi)) {
        size_t nl = text.find(L'\n', pos);
        if (nl == std::wstring::npos) nl = text.size();
        std::wstring line = text.substr(pos, nl - pos);
        pos = (nl < text.size()) ? nl + 1 : text.size() + 1;
        const bool first = (ln == 0);
        SetTextColor(dc, first ? cLevel : col::InkSoft);
        if (first) DrawColorDot(dc, cLevel, rc.left, y + (lineH - dot) / 2, dot);
        const RECT lr{ rc.left + Scale(16, dpi), y, rc.right - Scale(8, dpi), y + lineH };
        DrawTextW(dc, line.c_str(), -1, const_cast<RECT*>(&lr), DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
        y += lineH;
        ++ln;
    }
    SelectObject(dc, oldFont);
}

void DrawAttachedButton(LPDRAWITEMSTRUCT dis) {
    auto& reg = BtnRegistry();
    const auto it = reg.find(dis->hwndItem);
    BtnState fallback; // 未附着 → 中性兜底，不闪退不画崩
    BtnState& st = it == reg.end() ? fallback : it->second;
    HDC dc = dis->hDC; const RECT& r = dis->rcItem;
    const bool disabled = (dis->itemState & ODS_DISABLED) != 0;
    const bool pressed = (dis->itemState & ODS_SELECTED) != 0;
    const bool focused = !disabled && (GetFocus() == dis->hwndItem);
    SetBkMode(dc, TRANSPARENT);

    COLORREF bg, border, text;
    switch (st.tone) {
    case BtnTone::Primary:
        bg = disabled ? col::DisabledBg : (pressed ? col::AccentPressed : (st.hover ? col::AccentHover : col::Accent));
        border = bg; text = disabled ? col::InkDisabled : col::Surface;
        break;
    case BtnTone::Danger:
        bg = disabled ? col::DisabledBg : (pressed ? col::DangerPressed : (st.hover ? col::DangerHover : col::Surface));
        border = disabled ? col::Hairline : col::Danger; text = disabled ? col::InkDisabled : col::Danger;
        break;
    default: // Neutral
        bg = disabled ? col::DisabledBg : (pressed ? col::NeutralPressed : (st.hover ? col::NeutralHover : col::Surface));
        border = disabled ? col::Hairline : (st.hover ? col::InkSoft : col::Hairline);
        text = disabled ? col::InkDisabled : col::Ink;
    }
    FillRect(dc, &r, Brush(bg));
    const RECT frame{ r.left, r.top, r.right - 1, r.bottom - 1 };
    FrameRect(dc, &frame, Brush(border));
    if (focused) { // 键盘焦点环：1px 品牌蓝内环（替代系统虚线矩形）
        const RECT ring{ r.left + 2, r.top + 2, r.right - 3, r.bottom - 3 };
        FrameRect(dc, &ring, Brush(col::Accent));
    }
    const HFONT useFont = st.fonts ? st.fonts->button : static_cast<HFONT>(GetStockObject(DEFAULT_GUI_FONT));
    const HANDLE oldFont = SelectObject(dc, useFont);
    DrawTextW(dc, st.title.c_str(), -1, const_cast<RECT*>(&r), DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
    SelectObject(dc, oldFont);
}

void AttachFlatButton(HWND btn, BtnTone tone, const Fonts* f, UINT dpi) {
    auto& reg = BtnRegistry();
    auto& st = reg[btn];
    st.tone = tone; st.fonts = f; st.dpi = dpi; st.hover = false;
    wchar_t buf[128]; GetWindowTextW(btn, buf, 127); st.title = buf;
    SetWindowSubclass(btn, BtnSubProc, 1, reinterpret_cast<ULONG_PTR>(&st));
}

void AttachEditFocus(HWND edit) { SetWindowSubclass(edit, EditFocusSubProc, 3, 0); }
bool EditIsFocused(HWND edit) noexcept { return g_focusEdit == edit; }

void StyleListView(HWND lv, const Fonts& f, UINT dpi, int rowPx) {
    // 统一走 commctrl 官方包装宏（直接 SendMessageW 传 LVM_* 在本 SDK 有宏名重叠风险）
    ListView_SetBkColor(lv, col::Surface);
    const HWND hdr = ListView_GetHeader(lv);
    if (hdr) SendMessageW(hdr, WM_SETFONT, reinterpret_cast<WPARAM>(f.header), TRUE);
    // 行高 rowPx：空图列技巧（无图标，不产生色块贴图）；重建前先销毁旧表（DPI 切换二次调用防泄漏）
    if (HIMAGELIST oldIm = ListView_GetImageList(lv, LVSIL_SMALL)) ImageList_Destroy(oldIm);
    HIMAGELIST im = ImageList_Create(1, Scale(rowPx, dpi), ILC_COLOR32, 1, 1);
    ListView_SetImageList(lv, im, LVSIL_SMALL);
    const DWORD ex = ListView_GetExtendedListViewStyle(lv);
    ListView_SetExtendedListViewStyle(lv, ex | LVS_EX_DOUBLEBUFFER | LVS_EX_FULLROWSELECT | LVS_EX_TRACKSELECT);
}

LRESULT ListCustomDraw(LPNMLVCUSTOMDRAW cd, const ListDrawCtx& ctx) {
    switch (cd->nmcd.dwDrawStage) {
    case CDDS_PREPAINT: {
        FillRect(cd->nmcd.hdc, &cd->nmcd.rc, Brush(col::Surface));
        if (!ctx.rows || ctx.rows->empty()) { // 空态文案：无数据不白屏
            const HANDLE oldFont = SelectObject(cd->nmcd.hdc, ctx.f ? ctx.f->body : (HFONT)nullptr);
            SetBkMode(cd->nmcd.hdc, TRANSPARENT);
            SetTextColor(cd->nmcd.hdc, col::InkSoft);
            RECT rc = cd->nmcd.rc;
            DrawTextW(cd->nmcd.hdc, ctx.emptyHint, -1, &rc, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            if (oldFont) SelectObject(cd->nmcd.hdc, oldFont);
            return CDRF_SKIPDEFAULT;
        }
        return CDRF_NOTIFYITEMDRAW;
    }
    case CDDS_ITEMPREPAINT: {
        HDC dc = cd->nmcd.hdc;
        const size_t row = (size_t)cd->nmcd.dwItemSpec;
        if (!ctx.rows || row >= ctx.rows->size()) return CDRF_DODEFAULT;
        const RECT& rr = cd->nmcd.rc;
        const bool selected = (cd->nmcd.uItemState & CDIS_SELECTED) != 0;
        const bool hot = !selected && (cd->nmcd.uItemState & CDIS_HOT) != 0;
        FillRect(dc, &rr, Brush(selected ? col::AccentSoftBg : (hot ? col::NeutralHover : (row % 2 ? col::RowAlt : col::Surface))));
        DrawSeparatorH(dc, rr.left, rr.right, rr.bottom - 1); // 行间 1px hairline

        const HWND list = cd->nmcd.hdr.hwndFrom;
        const auto& rv = (*ctx.rows)[row];
        std::wstring_view cells[6] = { rv.pid, rv.name, rv.kind, rv.eng, rv.path, rv.occ };
        COLORREF colors[6] = { col::Ink, col::Ink, col::InkSoft,
                               rv.eng == L"RM+HT" ? col::Accent : col::InkSoft, col::InkSoft, col::InkSoft };
        const int rowH = rr.bottom - rr.top;
        UINT dpi = ctx.dpi;
        // 列几何：累加列宽（LVM_GETSUBITEMRECT 在本环境返回不可靠，截图取证；列宽法确定性）
        std::array<int, 6> colx{}, colw{};
        for (int c = 0; c < 6; ++c) {
            colw[c] = static_cast<int>(SendMessageW(list, LVM_GETCOLUMNWIDTH, (WPARAM)c, 0));
            colx[c] = (c == 0) ? 0 : colx[c - 1] + colw[c - 1];
        }
        // 文字垂直居中：先量高再摆位
        TEXTMETRICW tm{};
        HFONT cellFont = (selected && ctx.f) ? ctx.f->cellBold : (ctx.f ? ctx.f->cell : (HFONT)GetStockObject(DEFAULT_GUI_FONT));
        const HANDLE oldFont = SelectObject(dc, cellFont);
        GetTextMetricsW(dc, &tm);
        SetBkMode(dc, TRANSPARENT);
        for (int c = 0; c < 6; ++c) {
            if (colw[c] > 0 && !cells[c].empty()) {
                SetTextColor(dc, colors[c]);
                const RECT tr{ colx[c] + Scale(6, dpi), rr.top + (rowH - tm.tmHeight) / 2 - Scale(2, dpi),
                               colx[c] + colw[c] - Scale(6, dpi), rr.bottom };
                std::wstring hold(cells[c]); // string_view 生命周期：DrawText 前物化
                DrawTextW(dc, hold.c_str(), -1, const_cast<RECT*>(&tr),
                          DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS | DT_NOPREFIX);
                }
        }
        SelectObject(dc, oldFont);
        return CDRF_SKIPDEFAULT;
    }
    default: return CDRF_DODEFAULT;
    }
}

} // namespace who::ui
