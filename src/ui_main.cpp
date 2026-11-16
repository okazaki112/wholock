// WhoLock · 文件占用定位器 —— 窗口程序入口（视觉层依赖 ui/Theme 单一事实源；零第三方）
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "application/ScanCoordinator.h"
#include "cli/CliMain.h"
#include "domain/CliArgs.h"
#include "domain/Merge.h"
#include "domain/Normalize.h"
#include "domain/RiskFlags.h"
#include "infrastructure/win32/DeepScanRunner.h" // F7 明细结果类型（ProcHandleResult）
#include "infrastructure/win32/DeepScanner.h"
#include "infrastructure/win32/Engines.h"
#include "infrastructure/win32/HandleTableScanner.h"
#include "infrastructure/win32/Privileges.h"
#include "infrastructure/win32/PrepareTargets.h"
#include "infrastructure/win32/ProcessOps.h"
#include "infrastructure/win32/Settings.h"
#include "infrastructure/win32/ShellIntegration.h"
#include "infrastructure/win32/WindowsPathMap.h"
#include "ui/Theme.h"
#include "version.h" // F2 单一事实源（CMake 生成）

#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "comdlg32.lib")

namespace {
constexpr int IDC_PATH = 101, IDC_SCAN = 102, IDC_LIST = 103, IDC_STATUS = 104,
              IDC_CANCEL = 105, IDC_KILL = 106, IDC_DANGER = 107, IDC_BROWSE = 108,
              IDC_DEEP = 109, IDC_HANDLES = 110, IDC_HREL = 111, IDC_ABOUT = 112,
              IDC_SHELL = 113;
constexpr wchar_t kClass[] = L"WhoLockMainWnd";
constexpr UINT WM_APP_FILL = WM_APP + 1;
constexpr UINT WM_APP_AUTOSCAN = WM_APP + 2;
constexpr UINT WM_APP_HDETAIL = WM_APP + 3; // v1.0 F7：句柄明细回填

who::ScanCoordinator* g_coord = nullptr;        // 每次扫描按引擎勾选重建（见 OnScan）
std::unique_ptr<who::ScanCoordinator> g_coordOwner;
who::win32::WindowsPathMap* g_pathmap = nullptr;
who::win32::HandleTableScanner* g_ht = nullptr;
who::win32::DeepScanner* g_deep = nullptr;
std::unique_ptr<std::thread> g_worker;
std::unique_ptr<std::thread> g_hWorker;          // 明细拉取线程（与扫描线程互斥于 g_scanning）
std::atomic<bool> g_cancel{ false };
bool g_scanning = false;

who::ui::Fonts g_fonts;
UINT g_dpi = 96;
std::vector<who::LockInfo> g_display;
std::vector<who::ui::RowView> g_rowsView;

// v1.0 F7/F8/F11
std::vector<who::HandleRow> g_handleRows;
std::uint32_t g_detailPid = 0;                   // 当前详情所属进程
std::wstring g_lastSingleTarget;                 // 单目标扫描时记录，供释放后删除
bool g_htFullPref = false;                       // settings.htFull

std::wstring g_statText = L"就绪 —— 选择目标后点「扫描占用」";
who::ui::StatusLevel g_statLvl = who::ui::StatusLevel::Info;
// 空态提示与状态栏文案各自独立：修复启动时列表区与底部状态栏显示同一句话
std::wstring g_emptyHint = L"将文件拖入窗口，或点「浏览」选择被占用文件";

int S(int v) { return who::ui::Scale(v, g_dpi); }

// ---- 紧凑布局度量（96DPI 基准值）----
// layout() 与 WM_PAINT 共用同一组常量，杜绝两处魔数各自漂移
constexpr int kTopPad = 8;       // 顶部留白（标题栏已含程序名，无自绘品牌区）
constexpr int kChkH = 22;        // 顶部复选框行高
constexpr int kEditH = 64;       // 多行路径框高（约 3 行可见）
constexpr int kBtnH = 30;        // 按钮高
constexpr int kActionBand = 36;  // 底部操作带高
constexpr int kAboveAction = 8;  // 明细列表底与操作带的间距

// 底部状态带高度 = 按状态文字实际行数动态收紧（上限 4 行，与 DrawStatus 截止行为一致）；
// 单行"就绪"提示时底部不再留大片空白
int statusBandH() {
    int rows = 1;
    for (const wchar_t c : g_statText) if (c == L'\n') ++rows;
    if (rows > 4) rows = 4;
    return S(7) + rows * S(19);
}

struct FillPacket {
    who::ScanCoordinator::Report rep;
    std::wstring status;
    who::ui::StatusLevel lvl;
};

void layout(HWND hwnd, int cw, int ch); // 前向声明：setStatus 行数变化时触发布局重排

void setStatus(HWND hwnd, std::wstring text, who::ui::StatusLevel lvl) {
    g_statText = std::move(text);
    g_statLvl = lvl;
    RECT rc; GetClientRect(hwnd, &rc);
    layout(hwnd, rc.right, rc.bottom); // 状态行数变化 → 底部带高度动态收紧/恢复
    InvalidateRect(hwnd, nullptr, FALSE); // 状态带由父窗 WM_PAINT 直绘，重画父窗即可
}

void rebuildTable(HWND hwnd) {
    HWND lv = GetDlgItem(hwnd, IDC_LIST);
    ListView_DeleteAllItems(lv);
    g_rowsView.clear();
    g_rowsView.reserve(g_display.size());
    for (const auto& li : g_display) {
        who::ui::RowView rv;
        rv.pid = std::to_wstring(li.pid);
        rv.name = li.name;
        rv.kind = who::ui::kindText(li.kind);
        rv.eng = who::ui::engineText(li.engines);
        rv.path = li.imagePath;
        rv.occ = li.occupiedFile.empty() ? std::wstring()
                 : li.occupiedFile + (li.occupiedMore ? L" (+" + std::to_wstring(li.occupiedMore) + L")" : L"");
        g_rowsView.push_back(rv);
        LVITEMW it{}; it.mask = LVIF_TEXT; it.iItem = ListView_GetItemCount(lv);
        it.pszText = const_cast<wchar_t*>(L""); // 单元文本全由 customdraw 接管
        ListView_InsertItem(lv, &it);
    }
    InvalidateRect(lv, nullptr, FALSE);
}

// 危险操作按钮与"选中行 × 危险模式勾选"实时联动：不满足条件直接置灰（视觉即护栏）
void updateActionButtons(HWND hwnd) {
    const bool danger = Button_GetCheck(GetDlgItem(hwnd, IDC_DANGER)) == BST_CHECKED;
    const int sel = ListView_GetNextItem(GetDlgItem(hwnd, IDC_LIST), -1, LVNI_SELECTED);
    const bool kill = danger && sel >= 0 && static_cast<size_t>(sel) < g_display.size();
    EnableWindow(GetDlgItem(hwnd, IDC_KILL), kill ? TRUE : FALSE);
    const int hsel = ListView_GetNextItem(GetDlgItem(hwnd, IDC_HANDLES), -1, LVNI_SELECTED);
    const bool hrel = danger && hsel >= 0 && static_cast<size_t>(hsel) < g_handleRows.size() &&
                      g_handleRows[hsel].type == 1; // 仅磁盘文件句柄可释放（与点击时护栏一致）
    EnableWindow(GetDlgItem(hwnd, IDC_HREL), hrel ? TRUE : FALSE);
}
} // namespace

// ---- v1.0 F7/F8/F10/F11 ----
namespace {
void fillHandles(HWND hwnd) {
    HWND hl = GetDlgItem(hwnd, IDC_HANDLES);
    ListView_DeleteAllItems(hl);
    for (const auto& r : g_handleRows) {
        wchar_t hb[32]; swprintf_s(hb, L"0x%llx", static_cast<unsigned long long>(r.handleValue));
        LVITEMW it{}; it.mask = LVIF_TEXT; it.iItem = ListView_GetItemCount(hl);
        it.pszText = hb;
        const int row = ListView_InsertItem(hl, &it);
        wchar_t ab[16]; swprintf_s(ab, L"0x%lx", r.grantedAccess);
        ListView_SetItemText(hl, row, 1, ab);
        const wchar_t* t = r.type == 1 ? L"磁盘文件" : r.type == 2 ? L"管道" : r.type == 3 ? L"其他" : L"—";
        ListView_SetItemText(hl, row, 2, const_cast<wchar_t*>(t));
        ListView_SetItemText(hl, row, 3, const_cast<wchar_t*>(r.path.c_str()));
    }
}

void requestHandleDetail(HWND hwnd, std::uint32_t pid) {
    if (g_hWorker && g_hWorker->joinable()) g_hWorker->join();
    g_detailPid = pid;
    HWND hMain = hwnd;
    g_hWorker = std::make_unique<std::thread>([=] {
        auto* pack = new who::win32::ProcHandleResult(who::win32::runProcessHandleScan(pid, 8000));
        PostMessageW(hMain, WM_APP_HDETAIL, 0, reinterpret_cast<LPARAM>(pack));
    });
}

// F11：单目标场景，释放成功后询问删除（YES 才删；删后 GetFileAttributes 验证消失）
void askDeleteAfterRelease(HWND hwnd, const std::wstring& how) {
    if (g_lastSingleTarget.empty()) return;
    const std::wstring q = how + L"成功。是否删除该文件？\n\n" + g_lastSingleTarget;
    if (MessageBoxW(hwnd, q.c_str(), L"释放后删除", MB_YESNO | MB_ICONQUESTION | MB_DEFBUTTON2 | MB_SETFOREGROUND) != IDYES)
        return;
    std::wstring err;
    if (who::win32::deleteFileVerified(g_lastSingleTarget, &err)) {
        setStatus(hwnd, L"已删除：" + g_lastSingleTarget, who::ui::StatusLevel::Ok);
        g_lastSingleTarget.clear();
    } else {
        setStatus(hwnd, L"删除失败：" + err, who::ui::StatusLevel::Error);
    }
}

void onReleaseHandle(HWND hwnd) {
    HWND hl = GetDlgItem(hwnd, IDC_HANDLES);
    const int sel = ListView_GetNextItem(hl, -1, LVNI_SELECTED);
    if (sel < 0 || static_cast<size_t>(sel) >= g_handleRows.size()) {
        setStatus(hwnd, L"先在下方明细选中一个【磁盘文件】句柄", who::ui::StatusLevel::Warn);
        return;
    }
    const auto& r = g_handleRows[sel];
    if (r.type != 1) {
        setStatus(hwnd, L"护栏：仅允许释放磁盘文件句柄（当前类型不允许）", who::ui::StatusLevel::Error);
        return;
    }
    if (Button_GetCheck(GetDlgItem(hwnd, IDC_DANGER)) != BST_CHECKED) {
        setStatus(hwnd, L"释放句柄已被护栏挡下：勾选「启用危险操作」后重试", who::ui::StatusLevel::Warn);
        return;
    }
    wchar_t hb[32]; swprintf_s(hb, L"0x%llx", static_cast<unsigned long long>(r.handleValue));
    std::wstring q = std::wstring(L"确认关闭该句柄？\n\n进程 PID: ") + std::to_wstring(g_detailPid) +
                     L"\n句柄: " + hb + L"\n路径: " + (r.path.empty() ? L"(未知)" : r.path) +
                     L"\n\n（不结束进程；对方逻辑若依赖该句柄可能出现异常）";
    if (MessageBoxW(hwnd, q.c_str(), L"释放句柄", MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2 | MB_SETFOREGROUND) != IDOK) {
        setStatus(hwnd, L"已取消释放操作", who::ui::StatusLevel::Info);
        return;
    }
    std::wstring err;
    if (who::win32::closeRemoteHandle(g_detailPid, r.handleValue, &err)) {
        setStatus(hwnd, L"句柄 " + std::wstring(hb) + L" 已释放；重扫验证占用是否解除", who::ui::StatusLevel::Ok);
        requestHandleDetail(hwnd, g_detailPid); // 刷新明细（该句柄应消失）
        askDeleteAfterRelease(hwnd, L"释放句柄");
    } else {
        setStatus(hwnd, L"释放失败：" + err, who::ui::StatusLevel::Error);
    }
}
} // namespace

// ==================== 扫描与释放 ====================
namespace {
who::ui::ListDrawCtx listCtx() {
    who::ui::ListDrawCtx c;
    c.rows = &g_rowsView; c.f = &g_fonts; c.dpi = g_dpi; c.emptyHint = g_emptyHint.c_str();
    return c;
}

void OnScan(HWND hwnd) {
    if (g_scanning) return;
    wchar_t buf[16384] = {};
    GetDlgItemTextW(hwnd, IDC_PATH, buf, 16383);
    // v1.0 F5：多行拆分（\r\n / \n），去首尾空白与包裹引号（拖放路径常带引号）
    std::vector<std::wstring> lines;
    {
        std::wstring text = buf;
        size_t pos = 0;
        while (pos <= text.size()) {
            size_t nl = text.find(L'\n', pos);
            if (nl == std::wstring::npos) nl = text.size();
            std::wstring line = text.substr(pos, nl - pos);
            pos = nl + 1;
            if (!line.empty() && line.back() == L'\r') line.pop_back();
            auto issp = [](wchar_t c) { return c == L' ' || c == L'\t'; };
            while (!line.empty() && issp(line.front())) line.erase(line.begin());
            while (!line.empty() && issp(line.back())) line.pop_back();
            if (line.size() >= 2 && line.front() == L'"' && line.back() == L'"')
                line = line.substr(1, line.size() - 2);
            if (!line.empty()) lines.push_back(std::move(line));
        }
    }
    if (lines.empty()) { setStatus(hwnd, L"请输入至少一个文件/目录路径", who::ui::StatusLevel::Warn); return; }
    std::wstring err;
    who::win32::PreparedTargets prep;
    if (!who::win32::prepareScanTargets(lines, *g_pathmap, prep, &err)) {
        setStatus(hwnd, err, who::ui::StatusLevel::Error);
        return;
    }
    if (g_worker && g_worker->joinable()) g_worker->join(); // 双保险

    // F11：单目标记录（释放成功后询问删除）；F6：按勾选重建引擎集合
    g_lastSingleTarget = prep.refs.size() == 1 ? prep.refs[0].originalPath : std::wstring();
    const bool deep = Button_GetCheck(GetDlgItem(hwnd, IDC_DEEP)) == BST_CHECKED;
    {
        auto scanners = who::win32::createEngines(
            who::EngineRM | who::EngineHT | (deep ? who::EngineDeep : 0), g_pathmap);
        g_ht = nullptr; g_deep = nullptr;
        for (auto& s : scanners) {
            if (s->engineBit() == who::EngineHT) g_ht = static_cast<who::win32::HandleTableScanner*>(s.get());
            else if (s->engineBit() == who::EngineDeep) g_deep = static_cast<who::win32::DeepScanner*>(s.get());
        }
        g_coordOwner = std::make_unique<who::ScanCoordinator>(std::move(scanners));
        g_coord = g_coordOwner.get();
    }

    std::wstring scanMsg = L"扫描中…（目标 " + std::to_wstring(prep.refs.size()) + L" 个文件"; // use-before-move
    if (prep.expandedDirs) scanMsg += L"，目录展开×" + std::to_wstring(prep.expandedDirs);
    if (deep) scanMsg += L"｜含深扫";
    if (!prep.note.empty()) scanMsg += L"｜" + prep.note;
    scanMsg += L"；可取消）";

    who::ScanQuery query;
    query.targets = std::move(prep.refs);
    g_scanning = true;
    EnableWindow(GetDlgItem(hwnd, IDC_SCAN), FALSE);
    ShowWindow(GetDlgItem(hwnd, IDC_CANCEL), SW_SHOW);
    setStatus(hwnd, scanMsg, who::ui::StatusLevel::Info);

    HWND hMain = hwnd;
    g_worker = std::make_unique<std::thread>([=] {
        const auto t0 = GetTickCount64();
        auto rep = std::make_unique<FillPacket>();
        rep->rep = g_coord->execute(query);
        const auto ms = GetTickCount64() - t0;
        for (auto& li : rep->rep.merged) { /* origin 位保留 */ (void)li; }
        if (rep->rep.allFailed()) {
            rep->lvl = who::ui::StatusLevel::Error;
            rep->status = L"全部引擎失败，结果不可用";
        } else if (!rep->rep.anyOk) {
            rep->lvl = who::ui::StatusLevel::Warn; rep->status = L"无可用引擎";
        } else {
            bool degraded = false;
            for (const auto& n : rep->rep.notes) if (n.ok && n.adminRecommended) degraded = true;
            rep->lvl = degraded ? who::ui::StatusLevel::Warn : who::ui::StatusLevel::Ok;
            rep->status = std::wstring(L"耗时 ") + std::to_wstring(ms) + L"ms ｜ 命中 " +
                          std::to_wstring(rep->rep.merged.size()) + L" 个进程";
            for (const auto& n : rep->rep.notes)
                rep->status += (n.ok ? (L"\n· " + n.engine + L" 命中×" + std::to_wstring(n.count))
                                     : (L"\n· " + n.engine + L" ✕ " + n.error));
            if (degraded) rep->status += L"\n· 非管理员运行：跨会话占用可能不全（右键以管理员运行）";
        }
        PostMessageW(hMain, WM_APP_FILL, 0, reinterpret_cast<LPARAM>(rep.release()));
    });
}

void finishScanUI(HWND hwnd) {
    g_scanning = false;
    g_cancel.store(false);
    EnableWindow(GetDlgItem(hwnd, IDC_SCAN), TRUE);
    ShowWindow(GetDlgItem(hwnd, IDC_CANCEL), SW_HIDE);
    if (g_worker && g_worker->joinable()) g_worker->join();
    g_worker.reset();
}

void OnKillSelected(HWND hwnd) {
    HWND lv = GetDlgItem(hwnd, IDC_LIST);
    const int sel = ListView_GetNextItem(lv, -1, LVNI_SELECTED);
    if (sel < 0 || static_cast<size_t>(sel) >= g_display.size()) {
        setStatus(hwnd, L"先在列表选中一个占用进程", who::ui::StatusLevel::Warn);
        return;
    }
    if (Button_GetCheck(GetDlgItem(hwnd, IDC_DANGER)) != BST_CHECKED) {
        setStatus(hwnd, L"结束进程已被护栏挡下：勾选「启用危险操作」后重试", who::ui::StatusLevel::Warn);
        return;
    }
    const auto& li = g_display[sel];
    const auto risk = who::classifyKill(li.pid, li.name, li.imagePath, GetCurrentProcessId());
    if (risk != who::KillRisk::Allowed) {
        const std::wstring why(who::riskText(risk));
        MessageBoxW(hwnd, (why + L"\nPID=" + std::to_wstring(li.pid) + L"，已按安全策略拒绝。").c_str(),
                    L"安全护栏", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
        setStatus(hwnd, L"护栏拒绝：" + why, who::ui::StatusLevel::Error);
        return;
    }
    std::wstring q = L"确认结束进程？\n\nPID: " + std::to_wstring(li.pid) + L"\n名称: " + li.name +
                     L"\n路径: " + (li.imagePath.empty() ? L"(未知)" : li.imagePath) +
                     L"\n\n占用方式：" + who::ui::kindText(li.kind) + L"；对方未保存数据可能丢失。";
    if (MessageBoxW(hwnd, q.c_str(), L"结束占用进程", MB_OKCANCEL | MB_ICONWARNING | MB_DEFBUTTON2 | MB_SETFOREGROUND) != IDOK) {
        setStatus(hwnd, L"已取消结束操作", who::ui::StatusLevel::Info);
        return;
    }
    std::wstring err;
    if (who::win32::terminateProcess(li.pid, &err)) {
        setStatus(hwnd, L"已结束 PID " + std::to_wstring(li.pid) + L"（" + li.name + L"）；重新扫描验证释放",
                  who::ui::StatusLevel::Ok);
        askDeleteAfterRelease(hwnd, L"结束进程"); // F11
    } else {
        setStatus(hwnd, L"结束失败：" + err, who::ui::StatusLevel::Error);
    }
}

// ==================== 布局 ====================
void layout(HWND hwnd, int cw, int ch) {
    const int rightMargin = S(16);
    const int wCancel = S(56), wScan = S(100), wBrowse = S(74), gap = S(8);
    // 顶部两行紧凑排列（无品牌区）：复选框行 → 路径框，自绘边框外扩 1px 后仍留 3px 间隙
    const int chkTop = S(kTopPad), chkH = S(kChkH);
    const int btnTop = chkTop + chkH + S(4), btnH = S(kBtnH);
    const int editH = S(kEditH);
    const int editMid = btnTop + (editH - btnH) / 2; // 按钮与路径框垂直居中
    const int xCancel = cw - rightMargin - wCancel;
    const int xScan = xCancel - gap - wScan;
    const int xBrowse = xScan - gap - wBrowse;
    MoveWindow(GetDlgItem(hwnd, IDC_PATH), S(16), btnTop, xBrowse - S(16) - S(10), editH, TRUE); // 紧凑：左侧不再预留已废弃的标签位
    MoveWindow(GetDlgItem(hwnd, IDC_BROWSE), xBrowse, editMid, wBrowse, btnH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_SCAN), xScan, editMid, wScan, btnH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_CANCEL), xCancel, editMid, wCancel, btnH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_DEEP), S(16), chkTop, S(340), chkH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_SHELL), S(366), chkTop, cw - S(366) - S(16), chkH, TRUE); // 宽度跟随窗口，避免窄窗被裁

    const int actionTop = ch - statusBandH() - S(kActionBand);
    const int listTop = btnTop + editH + S(10);
    const int detailBottom = actionTop - S(kAboveAction);
    const int areaH = detailBottom - listTop;
    const int mainH = (areaH - S(8)) * 55 / 100; // 主列表 : 明细 = 55 : 45
    const int detailTop = listTop + mainH + S(8);
    MoveWindow(GetDlgItem(hwnd, IDC_LIST), S(16), listTop, cw - S(32), mainH, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_HANDLES), S(16), detailTop, cw - S(32), detailBottom - detailTop, TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_ABOUT), cw - S(76), actionTop + S(3), S(60), S(30), TRUE); // 关于：底部操作栏右端
    MoveWindow(GetDlgItem(hwnd, IDC_DANGER), S(16), actionTop + S(7), S(240), S(22), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_KILL), S(268), actionTop + S(3), S(160), S(30), TRUE);
    MoveWindow(GetDlgItem(hwnd, IDC_HREL), S(444), actionTop + S(3), S(160), S(30), TRUE);

    // 列宽自适应：前 4 列固定，路径列/占用文件列瓜分剩余宽（窄窗不再溢出产生横向滚动与表头异常）
    const int fixed = S(70) + S(140) + S(92) + S(80);
    const int listW = cw - S(32);
    const int remain = listW - fixed;
    if (remain > S(280)) {
        const int w5 = (std::min)(S(230), remain * 35 / 100); // 占用文件列：≤35% 剩余宽且 ≤230
        SendMessageW(GetDlgItem(hwnd, IDC_LIST), LVM_SETCOLUMNWIDTH, 4, remain - w5);
        SendMessageW(GetDlgItem(hwnd, IDC_LIST), LVM_SETCOLUMNWIDTH, 5, w5);
    }
}
} // namespace

// ==================== 窗口过程 ====================
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM w, LPARAM l) {
    switch (msg) {
    case WM_CREATE: {
        g_dpi = GetDpiForWindow(hwnd);
        g_fonts = who::ui::Fonts::Create(g_dpi);

        auto mkWin = [&](DWORD exStyle, PCWSTR cls, PCWSTR txt, DWORD style, int x, int y, int cx, int cy, int id) {
            HWND c = CreateWindowExW(exStyle, cls, txt, WS_CHILD | WS_VISIBLE | style, x, y, cx, cy, hwnd,
                                     reinterpret_cast<HMENU>(static_cast<UINT_PTR>(id)), GetModuleHandleW(nullptr), nullptr);
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_fonts.body), TRUE);
            SendMessageW(c, WM_SETREDRAW, TRUE, 0);
            return c;
        };
        auto mkLabel = [&](const wchar_t* t, int x, int y, int w_, int h = 24) {
            auto c = mkWin(0, L"STATIC", t, SS_LEFT | SS_NOPREFIX, x, y, w_, h, 0);
            SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(g_fonts.label), TRUE);
            return c;
        };

        // 顶部 brand 走 WM_PAINT 自绘；此处标签/输入/按钮组
        HWND edit = mkWin(0, L"EDIT", L"", WS_TABSTOP | ES_MULTILINE | ES_WANTRETURN | ES_AUTOVSCROLL | WS_VSCROLL,
                          0, 0, 1, 1, IDC_PATH);
        SendMessageW(edit, EM_SETCUEBANNER, TRUE, reinterpret_cast<LPARAM>(L"每行一条文件/目录路径，也可直接拖入窗口"));
        who::ui::AttachEditFocus(edit); // 原生 3D 边框关闭：焦点蓝环由 Theme 自绘
        mkWin(0, L"BUTTON", L"浏览…", BS_OWNERDRAW, 0, 0, 1, 1, IDC_BROWSE);
        mkWin(0, L"BUTTON", L"扫描占用", BS_OWNERDRAW | WS_TABSTOP, 0, 0, 1, 1, IDC_SCAN);
        mkWin(0, L"BUTTON", L"取消", BS_OWNERDRAW, 0, 0, 1, 1, IDC_CANCEL);
        ShowWindow(GetDlgItem(hwnd, IDC_CANCEL), SW_HIDE);

        HWND list = mkWin(0, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SHOWSELALWAYS | WS_CLIPCHILDREN, 0, 0, 1, 1, IDC_LIST);
        // ↑ 列表自身也要 CLIPCHILDREN：customdraw PREPAINT 铺满客户区时不能盖掉 header 子窗
        for (int i = 0; i < 6; ++i) {
            const wchar_t* names[6] = { L"PID", L"进程名", L"占用方式", L"来源", L"可执行文件路径", L"占用文件" };
            const int widths[6] = { S(70), S(140), S(92), S(80), S(230), S(230) };
            LVCOLUMNW c{}; c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
            c.pszText = const_cast<wchar_t*>(names[i]); c.cx = widths[i]; c.iSubItem = i;
            ListView_InsertColumn(list, i, &c);
        }
        who::ui::StyleListView(list, g_fonts, g_dpi);

        auto chk = mkWin(0, L"BUTTON", L"启用危险操作（结束进程）", BS_AUTOCHECKBOX, 0, 0, 240, 26, IDC_DANGER);
        Button_SetCheck(chk, BST_UNCHECKED); // 三重护栏第一道：默认关（用户拍板）
        mkWin(0, L"BUTTON", L"结束选中进程", BS_OWNERDRAW, 0, 0, 160, 36, IDC_KILL);
        // 状态带不再用 ownerdraw static（WM_DRAWITEM 路由存疑），改由父窗 WM_PAINT 直绘

        // ---- v1.0 F6/F7/F8 控件 ----
        auto deep = mkWin(0, L"BUTTON", L"深度扫描(全表句柄·慢·可发现只读占用)", BS_AUTOCHECKBOX, 0, 0, 330, 26, IDC_DEEP);
        Button_SetCheck(deep, g_htFullPref ? BST_CHECKED : BST_UNCHECKED); // settings 恢复；勾选变更即存
        // v1.0 F12：Explorer 右键集成开关（状态=注册表实况；指向当前 exe，挪位后重勾即修复）
        auto shell = mkWin(0, L"BUTTON", L"Explorer 右键集成(当前用户注册表·可逆)", BS_AUTOCHECKBOX, 0, 0, 340, 26, IDC_SHELL);
        {
            const auto st = who::win32::queryShellIntegration();
            const bool on = st.fileKey && st.dirKey && !st.registeredExe.empty();
            Button_SetCheck(shell, on ? BST_CHECKED : BST_UNCHECKED);
        }
        HWND hl = mkWin(0, WC_LISTVIEWW, L"", LVS_REPORT | LVS_SHOWSELALWAYS | WS_CLIPCHILDREN, 0, 0, 1, 1, IDC_HANDLES);
        {
            const wchar_t* hnames[4] = { L"句柄", L"访问权限", L"类型", L"路径" };
            const int hwidths[4] = { S(90), S(100), S(90), S(420) };
            for (int i = 0; i < 4; ++i) {
                LVCOLUMNW c{}; c.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
                c.pszText = const_cast<wchar_t*>(hnames[i]); c.cx = hwidths[i]; c.iSubItem = i;
                ListView_InsertColumn(hl, i, &c);
            }
            who::ui::StyleListView(hl, g_fonts, g_dpi, 26); // 明细行密一些
        }
        mkWin(0, L"BUTTON", L"释放选中句柄", BS_OWNERDRAW, 0, 0, 160, 36, IDC_HREL);
        mkWin(0, L"BUTTON", L"关于", BS_OWNERDRAW, 0, 0, 60, 28, IDC_ABOUT);

        who::ui::AttachFlatButton(GetDlgItem(hwnd, IDC_BROWSE), who::ui::BtnTone::Neutral, &g_fonts, g_dpi);
        who::ui::AttachFlatButton(GetDlgItem(hwnd, IDC_SCAN), who::ui::BtnTone::Primary, &g_fonts, g_dpi);
        who::ui::AttachFlatButton(GetDlgItem(hwnd, IDC_CANCEL), who::ui::BtnTone::Neutral, &g_fonts, g_dpi);
        who::ui::AttachFlatButton(GetDlgItem(hwnd, IDC_KILL), who::ui::BtnTone::Danger, &g_fonts, g_dpi);
        who::ui::AttachFlatButton(GetDlgItem(hwnd, IDC_HREL), who::ui::BtnTone::Danger, &g_fonts, g_dpi);
        who::ui::AttachFlatButton(GetDlgItem(hwnd, IDC_ABOUT), who::ui::BtnTone::Neutral, &g_fonts, g_dpi);

        DragAcceptFiles(hwnd, TRUE);
        { RECT rc; GetClientRect(hwnd, &rc); layout(hwnd, rc.right, rc.bottom); } // 初排
        updateActionButtons(hwnd); // 初始禁用两个危险按钮（视觉即护栏）
        return 0;
    }
    case WM_APP_FILL: {
        auto* packet = reinterpret_cast<FillPacket*>(l);
        g_display = std::move(packet->rep.merged);
        g_emptyHint = g_display.empty() ? L"未发现占用者（只读共享句柄属 RM 盲区，详见 README 限制）" : L"";
        rebuildTable(hwnd);
        setStatus(hwnd, packet->status, packet->lvl);
        updateActionButtons(hwnd); // 列表重建后选中态已清空 → 危险按钮回禁用
        finishScanUI(hwnd);
        delete packet;
        return 0;
    }
    case WM_APP_AUTOSCAN:
        OnScan(hwnd);
        return 0;
    case WM_COMMAND:
        switch (LOWORD(w)) {
        case IDC_SCAN: OnScan(hwnd); return 0;
        case IDC_CANCEL:
            g_cancel.store(true);
            if (g_ht) g_ht->cancel();
            if (g_deep) g_deep->cancel(); // F6：取消传导给深扫读循环（子进程会被杀）
            setStatus(hwnd, L"取消请求已发出，等待枚举段边界…", who::ui::StatusLevel::Info);
            return 0;
        case IDC_KILL: OnKillSelected(hwnd); return 0;
        case IDC_HREL: onReleaseHandle(hwnd); return 0; // F8
        case IDC_DANGER: // 危险模式勾选变化 → 立即刷新危险按钮可用态
            updateActionButtons(hwnd);
            return 0;
        case IDC_DEEP: { // F6：勾选即持久化引擎偏好（危险模式永不入库，此值允许）
            auto s = who::win32::loadUiSettings();
            s.htFull = Button_GetCheck(GetDlgItem(hwnd, IDC_DEEP)) == BST_CHECKED;
            who::win32::saveUiSettings(s);
            return 0;
        }
        case IDC_SHELL: { // F12：Explorer 右键集成开关（HKCU，可逆）
            const bool want = Button_GetCheck(GetDlgItem(hwnd, IDC_SHELL)) == BST_CHECKED;
            std::wstring err;
            bool ok = false;
            if (want) ok = who::win32::enableShellIntegration(&err);
            else      ok = who::win32::disableShellIntegration(&err);
            if (!ok) { // 失败回滚勾选，状态明示
                Button_SetCheck(GetDlgItem(hwnd, IDC_SHELL), want ? BST_UNCHECKED : BST_CHECKED);
                setStatus(hwnd, L"右键集成失败：" + err, who::ui::StatusLevel::Error);
            } else {
                setStatus(hwnd, want ? L"已启用 Explorer 右键集成（当前用户；右键文件/目录→查找占用）"
                                     : L"已关闭 Explorer 右键集成（注册表键已清理）",
                          who::ui::StatusLevel::Ok);
            }
            return 0;
        }
        case IDC_ABOUT: { // F10
            const std::wstring txt = std::wstring(L"WhoLock v") + WHOLOCK_WSTR(WHOLOCK_VERSION_STR) +
                L"\n\n文件占用定位器 · 单 exe 免安装 · 仅系统 Win32 API · 无网络无遥测\n"
                L"引擎：RestartManager + 句柄表(候选门) + 子进程隔离深扫\n"
                L"安全：结束进程/释放句柄均带三重护栏（危险模式默认关、二次确认、系统进程拦死）\n"
                L"已知限制详见 README。";
            MessageBoxW(hwnd, txt.c_str(), L"关于 WhoLock", MB_OK | MB_ICONINFORMATION | MB_SETFOREGROUND);
            return 0;
        }
        case IDC_BROWSE: {
            wchar_t buf[4096] = {};
            GetDlgItemTextW(hwnd, IDC_PATH, buf, 4095);
            OPENFILENAMEW ofn{ sizeof(ofn) };
            ofn.hwndOwner = hwnd; ofn.lpstrFilter = L"所有文件\0*.*\0";
            ofn.lpstrFile = buf; ofn.nMaxFile = 4095;
            ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST | OFN_HIDEREADONLY;
            if (GetOpenFileNameW(&ofn)) { // 选定即自动扫描（少一步点击）
                SetDlgItemTextW(hwnd, IDC_PATH, buf);
                OnScan(hwnd);
            }
            return 0;
        }
        }
        return 0;
    case WM_APP_HDETAIL: {
        auto* pack = reinterpret_cast<who::win32::ProcHandleResult*>(l);
        if (pack->spawnFailed) {
            setStatus(hwnd, L"句柄明细拉取失败：" + pack->error, who::ui::StatusLevel::Error);
        } else {
            g_handleRows = std::move(pack->rows);
            fillHandles(hwnd);
            updateActionButtons(hwnd); // 明细刷新 → 释放句柄按钮联动（旧选中行已消失）
            std::wstring hmsg = L"明细：" + std::to_wstring(g_handleRows.size()) + L" 个句柄（PID " +
                                std::to_wstring(g_detailPid) + L"）";
            if (pack->timedOut) hmsg += L"｜超时截断，部分结果";
            setStatus(hwnd, hmsg, pack->timedOut ? who::ui::StatusLevel::Warn : who::ui::StatusLevel::Info);
        }
        delete pack;
        return 0;
    }
    case WM_NOTIFY: {
        auto* nm = reinterpret_cast<LPNMHDR>(l);
        if (nm->hwndFrom == GetDlgItem(hwnd, IDC_HANDLES)) {
            if (nm->code == LVN_ITEMCHANGED) { updateActionButtons(hwnd); return 0; }
            if (nm->code == NM_CUSTOMDRAW && g_handleRows.empty()) { // 空态提示（非空走默认文本绘制）
                static const std::vector<who::ui::RowView> kEmpty;
                who::ui::ListDrawCtx c;
                c.rows = &kEmpty; c.f = &g_fonts; c.dpi = g_dpi;
                c.emptyHint = L"在上方列表选中进程后，此处显示其句柄明细";
                return who::ui::ListCustomDraw(reinterpret_cast<LPNMLVCUSTOMDRAW>(l), c);
            }
            return 0;
        }
        if (nm->hwndFrom == GetDlgItem(hwnd, IDC_LIST)) {
            if (nm->code == LVN_ITEMCHANGED) { // F7：选中主列表行→异步拉该进程句柄明细
                auto* lv = reinterpret_cast<LPNMLISTVIEW>(l);
                if ((lv->uChanged & LVIF_STATE) && (lv->uNewState & LVIS_SELECTED) &&
                    lv->iItem >= 0 && static_cast<size_t>(lv->iItem) < g_display.size()) {
                    const auto pid = g_display[lv->iItem].pid;
                    if (pid != g_detailPid) requestHandleDetail(hwnd, pid);
                }
                updateActionButtons(hwnd); // 选中变化 → 危险按钮联动
                return 0;
            }
            if (nm->code == LVN_COLUMNCLICK) { // 列排序（同列切向；customdraw 全权接管外观）
                auto* lv = reinterpret_cast<LPNMLISTVIEW>(l);
                static int s_col = -1; static bool s_asc = true;
                if (s_col == lv->iSubItem) s_asc = !s_asc; else { s_col = lv->iSubItem; s_asc = true; }
                if (s_col >= 0) {
                    const int col = s_col; const bool asc = s_asc;
                    std::stable_sort(g_display.begin(), g_display.end(), [col, asc](const who::LockInfo& a, const who::LockInfo& b) {
                        auto key = [](const who::LockInfo& li, int c) -> std::wstring {
                            switch (c) {
                            case 0: return std::to_wstring(li.pid);
                            case 1: return li.name;
                            case 2: return who::ui::kindText(li.kind);
                            case 3: return who::ui::engineText(li.engines);
                            case 4: return li.imagePath;
                            case 5: return li.occupiedFile; // v1.0 F4 占用文件列可排序
                            default: return li.imagePath;
                            }
                        };
                        int cmp = _wcsicmp(key(a, col).c_str(), key(b, col).c_str());
                        if (col == 0) cmp = (a.pid < b.pid) ? -1 : (a.pid > b.pid ? 1 : 0);
                        return asc ? cmp < 0 : cmp > 0;
                    });
                    rebuildTable(hwnd);
                }
                return 0;
            }
            if (nm->code == NM_CUSTOMDRAW)
                return who::ui::ListCustomDraw(reinterpret_cast<LPNMLVCUSTOMDRAW>(l), listCtx());
        }
        return 0;
    }
    case WM_DRAWITEM:
        who::ui::DrawAttachedButton(reinterpret_cast<LPDRAWITEMSTRUCT>(l)); // 仅按钮（状态带已改父窗直绘）
        return TRUE;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC winDc = BeginPaint(hwnd, &ps);
        RECT rc; GetClientRect(hwnd, &rc);
        const int cw = rc.right, ch = rc.bottom;
        HDC mem = CreateCompatibleDC(winDc);
        HBITMAP bmp = CreateCompatibleBitmap(winDc, (std::max)(cw, 1), (std::max)(ch, 1)), oldBmp = (HBITMAP)SelectObject(mem, bmp);
        FillRect(mem, &rc, who::ui::Brush(who::ui::col::Window));
        // edit 自绘边框（focus 蓝）
        {
            RECT er; GetWindowRect(GetDlgItem(hwnd, IDC_PATH), &er);
            MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<POINT*>(&er), 2);
            who::ui::DrawEditFrame(mem, er, who::ui::EditIsFocused(GetDlgItem(hwnd, IDC_PATH)));
        }
        // list 外框 hairline
        {
            RECT lr; GetWindowRect(GetDlgItem(hwnd, IDC_LIST), &lr);
            MapWindowPoints(HWND_DESKTOP, hwnd, reinterpret_cast<POINT*>(&lr), 2);
            FrameRect(mem, &lr, who::ui::Brush(who::ui::col::Hairline));
        }
        // action 上分隔线（1pt hairline，与 layout() 的 kAboveAction 间距一致）
        who::ui::DrawSeparatorH(mem, 0, cw, ch - statusBandH() - S(kActionBand) - S(kAboveAction));
        // 状态带：父窗直绘（无子窗消息路由），高度随状态行数动态收紧
        { const RECT sr{ S(16), ch - statusBandH(), cw - S(16), ch - S(2) };
          who::ui::DrawStatus(mem, sr, g_fonts, g_dpi, g_statLvl, g_statText); }
        BitBlt(winDc, 0, 0, cw, ch, mem, 0, 0, SRCCOPY);
        SelectObject(mem, oldBmp); DeleteObject(bmp); DeleteDC(mem);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC: {
        auto dc = reinterpret_cast<HDC>(w);
        SetBkColor(dc, who::ui::col::Window); // OPAQUE 铺底（TRANSPARENT 在 CLIPCHILDREN 下露未初始化黑）
        SetTextColor(dc, who::ui::col::InkSoft);
        return reinterpret_cast<LRESULT>(who::ui::Brush(who::ui::col::Window));
    }
    case WM_CTLCOLOREDIT: {
        auto dc = reinterpret_cast<HDC>(w);
        SetTextColor(dc, who::ui::col::Ink);
        SetBkColor(dc, who::ui::col::Surface);
        return reinterpret_cast<LRESULT>(who::ui::Brush(who::ui::col::Surface));
    }
    case WM_SIZE:
        layout(hwnd, LOWORD(l), HIWORD(l));
        return 0;
    case WM_GETMINMAXINFO: {
        auto* mmi = reinterpret_cast<LPMINMAXINFO>(l);
        mmi->ptMinTrackSize.x = S(760); mmi->ptMinTrackSize.y = S(540); // 760：保证顶部两枚复选框完整可见
        return 0;
    }
    case WM_DPICHANGED: {
        g_dpi = HIWORD(w);
        g_fonts.Destroy();
        g_fonts = who::ui::Fonts::Create(g_dpi);
        who::ui::StyleListView(GetDlgItem(hwnd, IDC_LIST), g_fonts, g_dpi);
        who::ui::StyleListView(GetDlgItem(hwnd, IDC_HANDLES), g_fonts, g_dpi, 26);
        auto* r = reinterpret_cast<RECT*>(l);
        SetWindowPos(hwnd, nullptr, r->left, r->top, r->right - r->left, r->bottom - r->top,
                     SWP_NOZORDER | SWP_NOACTIVATE);
        InvalidateRect(hwnd, nullptr, TRUE);
        return 0;
    }
    case WM_DROPFILES: {
        HDROP hdrop = reinterpret_cast<HDROP>(w);
        const UINT n = DragQueryFileW(hdrop, 0xFFFFFFFF, nullptr, 0); // v1.0 F5：全部文件逐行入框
        std::wstring joined;
        wchar_t path[32768];
        for (UINT i = 0; i < n; ++i) {
            path[0] = 0;
            if (DragQueryFileW(hdrop, i, path, 32767) && path[0]) {
                if (!joined.empty()) joined += L"\r\n";
                joined += path;
            }
        }
        DragFinish(hdrop);
        if (!joined.empty()) {
            SetDlgItemTextW(hwnd, IDC_PATH, joined.c_str());
            OnScan(hwnd);
        }
        return 0;
    }
    case WM_DESTROY: {
        RECT wr{}; GetWindowRect(hwnd, &wr); // F3：退出存几何（危险模式勾选不在 UiSettings 结构中，接口层杜绝持久化）
        auto s = who::win32::loadUiSettings();
        s.x = wr.left; s.y = wr.top; s.w = wr.right - wr.left; s.h = wr.bottom - wr.top;
        s.hasGeometry = true;
        who::win32::saveUiSettings(s);
        g_cancel.store(true);
        if (g_ht) g_ht->cancel();
        if (g_deep) g_deep->cancel();
        if (g_worker && g_worker->joinable()) g_worker->join();
        g_worker.reset();
        if (g_hWorker && g_hWorker->joinable()) g_hWorker->join(); // F7：明细线程同走超时杀，不会挂死 join
        g_hWorker.reset();
        PostQuitMessage(0);
        return 0;
    }
    }
    return DefWindowProcW(hwnd, msg, w, l);
}

// ==================== 入口 ====================
int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int nShow) {
    { // v1.0 取证：子进程到达性文件日志（%TEMP%\wholock_internal.log；GUI 进程 stdout 不可依赖）
        wchar_t tmp[MAX_PATH]; GetTempPathW(MAX_PATH, tmp);
        HANDLE lf = CreateFileW((std::wstring(tmp) + L"wholock_internal.log").c_str(),
                                FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (lf != INVALID_HANDLE_VALUE) {
            wchar_t exe[4096]; GetModuleFileNameW(nullptr, exe, 4095);
            wchar_t line[4224];
            swprintf_s(line, L"[tick=%llu] wWinMain enter exe=%s\r\n", GetTickCount64(), exe);
            DWORD w = 0; WriteFile(lf, line, DWORD(wcslen(line) * sizeof(wchar_t)), &w, nullptr);
            CloseHandle(lf);
        }
    }
    int argc = 0;
    LPWSTR* argvW = CommandLineToArgvW(GetCommandLineW(), &argc);
    std::vector<std::wstring> argv;
    for (int i = 0; i < argc; ++i) argv.emplace_back(argvW[i]);
    LocalFree(argvW);

    who::win32::WindowsPathMap pathmap;
    if (who::isCliMode(argv)) { // --cli：控制台宿主（输出 UTF-8 后立即退出）
        // v1.0 关键坑：internal 模式（DeepScanRunner 子进程）的标准句柄已由
        // STARTF_USESTDHANDLES 继承为管道，绝不能再 Attach/AllocConsole——
        // AllocConsole 会替换标准句柄，子进程将永远读不到 stdin EOF（实测挂 20s 被超时杀）。
        const bool internal = std::any_of(argv.begin(), argv.end(), [](const std::wstring& a) {
            return a.rfind(L"--internal-htfull", 0) == 0 || a.rfind(L"--internal-procscan", 0) == 0;
        });
        if (!internal) { if (!AttachConsole(ATTACH_PARENT_PROCESS)) AllocConsole(); }
        const int rc = who::cli::run(argv, pathmap);
        if (!internal) FreeConsole();
        return rc;
    }

    // v1.0：引擎集合每次扫描按勾选重建（OnScan），不再常驻 coord
    const auto uiSet = who::win32::loadUiSettings();
    g_htFullPref = uiSet.htFull;
    g_pathmap = &pathmap;

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_LISTVIEW_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    WNDCLASSEXW wc{ sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW; // 缩放时整窗失效重绘：NULL_BRUSH 背景下无此标志会产生拖动残影
    wc.lpfnWndProc = WndProc; wc.hInstance = hInst; wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(GetStockObject(NULL_BRUSH)); // 全客户区自绘
    wc.hIcon = LoadIconW(hInst, MAKEINTRESOURCEW(1)); // F9：扁平 ICO（rc 资源 1）
    wc.hIconSm = wc.hIcon;
    wc.lpszClassName = kClass;
    RegisterClassExW(&wc);

    // 双击 exe / 拖文件到 exe 图标：位置参数即目标 → 打开自动扫描
    for (int i = 1; i < argc; ++i) {
        if (argv[i] == L"-h" || argv[i] == L"-?" || argv[i] == L"--help") {
            MessageBoxW(nullptr, L"用法：双击运行；拖文件到本图标自动扫描。\nCLI: WhoLock.exe --cli [--json] [--engine=rm|ht|both] <文件路径>",
                        L"WhoLock", MB_OK | MB_ICONINFORMATION);
            return 0;
        }
    }

    // WS_EX_COMPOSITED 与 WS_CLIPCHILDREN 互斥（合成层不含子窗→黑块，截图取证）；
    // 防闪烁由 ListView LVS_EX_DOUBLEBUFFER + 按钮 ownerdraw 直绘承担，无需整窗合成。
    HWND hwnd = CreateWindowExW(0, kClass, L"WhoLock · 文件占用定位器 v" WHOLOCK_WSTR(WHOLOCK_VERSION_STR),
                                WS_OVERLAPPEDWINDOW | WS_CLIPCHILDREN,
                                uiSet.hasGeometry ? uiSet.x : CW_USEDEFAULT,
                                uiSet.hasGeometry ? uiSet.y : CW_USEDEFAULT,
                                uiSet.hasGeometry ? uiSet.w : S(790),
                                uiSet.hasGeometry ? uiSet.h : S(660), nullptr, nullptr, hInst, nullptr);
    ShowWindow(hwnd, nShow);

    // 自动扫描（GUI 启动带路径参数）
    for (int i = 1; i < argc; ++i) {
        if (!argv[i].empty() && argv[i][0] != L'-') {
            SetDlgItemTextW(hwnd, IDC_PATH, argv[i].c_str());
            PostMessageW(hwnd, WM_APP_AUTOSCAN, 0, 0);
            break;
        }
    }

    MSG m;
    while (GetMessageW(&m, nullptr, 0, 0)) { TranslateMessage(&m); DispatchMessageW(&m); }
    g_fonts.Destroy();
    g_coord = nullptr; g_pathmap = nullptr; g_ht = nullptr;
    return static_cast<int>(m.wParam);
}
