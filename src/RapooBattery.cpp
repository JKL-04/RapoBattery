// RapooBattery — 雷柏（Rapoo）无线鼠标电量托盘监视器
// Copyright (C) 2026 JKL-04
//
// 本程序是自由软件：你可以依据 GNU 通用公共许可证（由自由软件基金会发布）
// 的条款重新发布和/或修改它，许可证版本为第 3 版或（由你选择）任何更新的版本。
//
// 本程序的发布是希望它有用，但【不提供任何担保】，甚至不提供适销性或
// 特定用途适用性的默示担保。详见 GNU 通用公共许可证。
//
// 你应该已经随本程序收到了 GNU 通用公共许可证的副本。
// 如果没有，见 <https://www.gnu.org/licenses/>。
//
// ══════════════════════════════════════════════════════════════════════
//
//
// 单一功能：在系统托盘显示雷柏无线鼠标的电量百分比。
// 原生 Win32，无 .NET 依赖，单文件 exe，常驻内存约 2 MB。
//
// ══════════════════════════════════════════════════════════════════════
// 一、读取方式（本机实测确认，非推测）
// ══════════════════════════════════════════════════════════════════════
//   打开厂商集合  usagePage = 0xFF00, usage = 0x000F   (col06)
//   读取 Feature report ID = 8，缓冲 250 字节
//   电量百分比 = 返回数据 byte[88]   (取值 0..100)
//
//   本工具是【纯只读】的，绝不向设备写入任何数据。
//
//   曾经的做法是先向 col05 的 output report 6 发送一帧
//   A5 A3 00 00 00 00 00 00 作为“握手”，但实测证明：
//     • 该帧并非必需 —— 完全不写设备也能稳定读到电量（5/5 成功）
//     • 每 30 秒重复发送该帧会反复打断鼠标与接收器之间的 2.4G 链路，
//       表现为鼠标失灵、断连重连
//   因此该帧已被彻底移除。同时句柄在启动时打开一次并保持复用，
//   避免反复 CreateFile/CloseHandle 造成链路扰动。
//
//   实测校验（官方网页驱动显示值 vs byte[88]）：
//     87% -> 0x57 = 87      93% -> 0x5D = 93
//     94%(充电中) -> 0x5C = 92      92% -> 0x5C = 92
//   同批次其他字节（byte[75] 恒定 94、byte[98]/[113] 为遥测）均已排除。
//
//   ⚠ 设备在唤醒间隙会返回整块全零的缓冲，其首字节也不是正常的 0x01。
//     必须据此区分「真实的 0%」与「空帧」，否则会把空帧当成 0% 显示。
//
// ══════════════════════════════════════════════════════════════════════
// 二、托盘图标
// ══════════════════════════════════════════════════════════════════════
//   未知    : 灰色 “--”
//   高电量  : 绿色（> 60%）
//   中等    : 白色
//   低电量  : 红色（≤ 20%）
//
//   图标由 Shell 按 DPI 请求尺寸：100% 缩放为 16x16，125% 为 20x20，
//   150% 为 24x24。因此按实际像素渲染，内部再用 8 倍超采样消除锯齿，
//   而不是渲染固定尺寸后交给系统缩小（那会白白损失分辨率）。
//
// ══════════════════════════════════════════════════════════════════════
// 三、浮窗面板
// ══════════════════════════════════════════════════════════════════════
//   左右键点击托盘图标都会切换浮窗；浮窗紧贴托盘图标上方，圆角白底。
//   面板行：电量/上次读取时间、刷新间隔（悬停或点击弹出右侧子菜单）、
//           立即刷新、开机自启、退出。
//   关闭方式：再点一次托盘图标、按 Esc、或点击面板以外的任意位置。
//   点击面板外部靠 WH_MOUSE_LL 钩子判断 —— 不能用 SetCapture，
//   那会抢走全部鼠标消息，导致浮窗上的时间戳不再刷新。
//
// ══════════════════════════════════════════════════════════════════════
// 四、构建
// ══════════════════════════════════════════════════════════════════════
//   rc /nologo RapooBattery.rc
//   cl /nologo /W3 /O2 /EHsc /std:c++17 /utf-8 RapooBattery.cpp RapooBattery.res ^
//      /Fe:RapooBattery.exe ^
//      /link setupapi.lib hid.lib user32.lib gdi32.lib shell32.lib advapi32.lib
//
//   命令行参数（用于排障）：
//     --read [次数]   只读取并打印电量，不创建窗口/托盘
//     --test-tray     逐步执行托盘注册并打印每步结果

#define WIN32_LEAN_AND_MEAN
#define UNICODE
#define _UNICODE

#include <windows.h>
#include <winioctl.h>
#include <setupapi.h>
#include <hidsdi.h>
#include <hidpi.h>
#include <shellapi.h>
#include <stdio.h>

#include "glyphs_gen.h"   // SVG 转换来的字形轮廓（由 tools/svg2cpp.py 生成）
#include <string>
#include <vector>

// GET_X_LPARAM / GET_Y_LPARAM 通常来自 windowsx.h，但 WIN32_LEAN_AND_MEAN
// 会把它屏蔽掉，这里自行定义（含符号扩展，与系统宏语义一致）。
#ifndef GET_X_LPARAM
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#endif
#ifndef GET_Y_LPARAM
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))
#endif

#pragma comment(lib, "setupapi.lib")
#pragma comment(lib, "hid.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")

// ─────────────────────────── 常量 ───────────────────────────
static const USHORT RAPOO_VID      = 0x24AE;   // 雷柏官方 VID
static const BYTE   STATUS_FEATID  = 0x08;     // 状态 Feature report ID
static const int    STATUS_BUFLEN  = 250;      // 声明长度只有 33，但有效数据到 141，必须多申请
static const int    BATTERY_OFFSET = 88;       // 电量所在字节偏移

static const UINT WM_TRAY        = WM_APP + 1;   // 托盘图标回调消息
static const UINT WM_POLL        = WM_APP + 2;   // 读取设备的定时器 id
static const UINT WM_TIP         = WM_APP + 3;   // 刷新托盘提示的定时器 id
static const UINT WM_PANELTICK   = WM_APP + 12;  // 面板唯一的定时器 id
static const DWORD PANEL_TICK_MS = 500;          // 面板定时器周期

static const wchar_t* PANEL_CLASS = L"RapooBatteryPanel";
static const int PANEL_W   = 286;
static const int PANEL_GAP = 8;      // 浮窗与托盘图标之间的间距

static const UINT TRAY_ICON_ID = 1;  // 必须与 NOTIFYICONDATAW.uID 一致

// 面板行命令 id
static const UINT IDM_REFRESH    = 1001;
static const UINT IDM_AUTORUN    = 1002;
static const UINT IDM_EXIT       = 1003;
static const UINT IDM_POLL_BASE  = 1100;  // 1100..1104 对应 POLL_CHOICES_MS
static const UINT IDM_IVL_OPEN   = 1109;  // 打开「刷新间隔」子菜单

// 可选刷新间隔（秒）。默认 30 秒（索引 2）。
// 说明：电量读取是纯只读操作，不会干扰鼠标，所以间隔可以设得比较短；
// 但也没必要过密 —— 电量变化本身很慢（92% 往往停留 20 分钟才掉 1%）。
static const DWORD POLL_CHOICES_SEC[] = { 5, 10, 30, 60, 300 };
static const int   POLL_CHOICE_COUNT  = 5;
static const int   POLL_DEFAULT_IDX   = 2;
static const DWORD POLL_RETRY_MS      = 5 * 1000;  // 读取失败后 5 秒重试
static const DWORD TIP_REFRESH_MS     = 1000;      // 托盘提示每秒刷新

static const wchar_t* CFG_KEY   = L"Software\\RapooBattery";
static const wchar_t* WND_CLASS = L"RapooBatteryTrayWnd";

// ─────────────────────────── 全局状态 ───────────────────────────
struct DevicePaths {
    std::wstring info;      // FF00/000F —— 电量数据来源
    bool valid = false;
};

struct State {
    HINSTANCE       hInst      = nullptr;
    HWND            hwnd       = nullptr;
    NOTIFYICONDATAW nid        = {};
    DevicePaths     dev;
    HANDLE          hInfo      = INVALID_HANDLE_VALUE;  // 常驻只读句柄
    bool            haveBattery = false;
    int             battery    = -1;      // -1 表示未知
    ULONGLONG       lastGoodTick = 0;     // 上次成功读取的时刻
    int             pollIdx    = POLL_DEFAULT_IDX;
    bool            pollBusy   = false;   // 防止重入
    HICON           hIcon      = nullptr;
};

static State g;

// ─────────────────────────── 小工具 ───────────────────────────
static ULONGLONG NowMs() { return GetTickCount64(); }

// 上次成功读取的绝对时刻，形如 "01:23:45"。
// 早期版本显示的是相对秒数「更新于 N 秒前」，但托盘提示与浮窗面板是两个
// 独立的定时器，采样时刻略有差异，整数除法会让两者稳定相差 1 秒。
// 改成绝对时间戳后两者必然一致，而且信息量更高。
static void FormatReadTime(wchar_t* out, size_t cch) {
    if (!g.lastGoodTick) { wcsncpy_s(out, cch, L"--:--:--", _TRUNCATE); return; }
    ULONGLONG ms = NowMs() - g.lastGoodTick;
    SYSTEMTIME st; GetLocalTime(&st);
    // 从「现在」回退到读取时刻（用 FILETIME 做减法，自动处理分/时进位）
    FILETIME ft; SystemTimeToFileTime(&st, &ft);
    ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
    ULONGLONG ticks = ms * 10000ULL;          // 1 ms = 10000 个 100ns
    if (u.QuadPart > ticks) u.QuadPart -= ticks;
    ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart;
    SYSTEMTIME rt; FileTimeToSystemTime(&ft, &rt);
    swprintf_s(out, cch, L"%02d:%02d:%02d", rt.wHour, rt.wMinute, rt.wSecond);
}

static std::wstring GetExePath() {
    wchar_t buf[MAX_PATH] = {0};
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    return buf;
}

// 诊断日志：写入 exe 同目录下的 RapooBattery.log。
// 默认开启（只记录启动、托盘注册、读取结果等关键事件），
// 设环境变量 RAPOO_DEBUG=0 可关闭。
//
// ⚠ 切勿在窗口过程的高频消息（如 WM_MOUSEMOVE）里调用本函数：
//   每次调用都要开关一次文件，鼠标在托盘图标上移动时每秒会产生大量
//   WM_MOUSEMOVE，同步文件 I/O 会把消息循环堵死，导致定时器得不到执行。
static bool DebugEnabled() {
    static int cached = -1;
    if (cached < 0) {
        wchar_t v[8] = {0};
        DWORD n = GetEnvironmentVariableW(L"RAPOO_DEBUG", v, 8);
        cached = (n > 0 && v[0] == L'0') ? 0 : 1;   // 默认开启
    }
    return cached == 1;
}

static void Dbg(const wchar_t* fmt, ...) {
    if (!DebugEnabled()) return;
    std::wstring p = GetExePath();
    size_t s = p.find_last_of(L'\\');
    if (s != std::wstring::npos) p.resize(s + 1); else p.clear();
    p += L"RapooBattery.log";

    wchar_t line[1024];
    va_list ap; va_start(ap, fmt);
    _vsnwprintf_s(line, _TRUNCATE, fmt, ap);
    va_end(ap);

    FILE* f = nullptr;
    _wfopen_s(&f, p.c_str(), L"a, ccs=UTF-8");
    if (!f) return;
    if (ftell(f) > 256 * 1024) { fclose(f); _wfopen_s(&f, p.c_str(), L"w, ccs=UTF-8"); if (!f) return; }
    SYSTEMTIME st; GetLocalTime(&st);
    fwprintf(f, L"[%02d:%02d:%02d.%03d] %s\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, line);
    fclose(f);
}

// ─────────────────────────── 配置持久化 ───────────────────────────
static void LoadConfig() {
    HKEY k = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, CFG_KEY, 0, KEY_READ, &k) != ERROR_SUCCESS) return;
    DWORD val = 0, cb = sizeof(val), type = 0;
    if (RegQueryValueExW(k, L"PollIdx", nullptr, &type,
                         reinterpret_cast<LPBYTE>(&val), &cb) == ERROR_SUCCESS) {
        if (val < (DWORD)POLL_CHOICE_COUNT) g.pollIdx = (int)val;
    }
    RegCloseKey(k);
}

static void SaveConfig() {
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, CFG_KEY, 0, nullptr, 0,
                        KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return;
    DWORD val = (DWORD)g.pollIdx;
    RegSetValueExW(k, L"PollIdx", 0, REG_DWORD,
                   reinterpret_cast<const BYTE*>(&val), sizeof(val));
    RegCloseKey(k);
}

static DWORD CurrentPollMs() { return POLL_CHOICES_SEC[g.pollIdx] * 1000; }

// ─────────────────────────── 开机自启 ───────────────────────────
// 本程序需要管理员权限（托盘注册受 UIPI 限制，见 RapooBattery.manifest），
// 因此不能用 HKCU\...\Run 注册表方式 —— 那种方式登录时不会提权，托盘图标
// 会注册失败。改用任务计划程序，并勾选「使用最高权限运行」(/RL HIGHEST)。
static const wchar_t* TASK_NAME = L"RapooBattery";

static void RunHidden(const std::wstring& cmdline) {
    STARTUPINFOW si = {}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> buf(cmdline.begin(), cmdline.end());
    buf.push_back(0);
    if (CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        WaitForSingleObject(pi.hProcess, 8000);
        CloseHandle(pi.hProcess);
        CloseHandle(pi.hThread);
    }
}

// 查询计划任务是否存在（退出码 0 表示存在）
static bool TaskExists() {
    std::wstring c = L"cmd.exe /c \"schtasks /Query /TN \"";
    c += TASK_NAME;
    c += L"\" >nul 2>&1\"";

    STARTUPINFOW si = {}; si.cb = sizeof(si);
    PROCESS_INFORMATION pi = {};
    std::vector<wchar_t> buf(c.begin(), c.end());
    buf.push_back(0);
    if (!CreateProcessW(nullptr, buf.data(), nullptr, nullptr, FALSE,
                        CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi))
        return false;
    WaitForSingleObject(pi.hProcess, 5000);
    DWORD code = 1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    return code == 0;
}

static bool IsAutoRunEnabled() { return TaskExists(); }

static bool SetAutoRun(bool enable) {
    if (!enable) {
        std::wstring c = L"cmd.exe /c \"schtasks /Delete /TN \"";
        c += TASK_NAME;
        c += L"\" /F >nul 2>&1\"";
        RunHidden(c);
        return !TaskExists();
    }
    std::wstring c = L"cmd.exe /c \"schtasks /Create /TN \"";
    c += TASK_NAME;
    c += L"\" /TR \"\\\"";
    c += GetExePath();
    c += L"\\\"\" /SC ONLOGON /RL HIGHEST /F >nul 2>&1\"";
    RunHidden(c);
    return TaskExists();
}

// ─────────────────────────── HID 设备发现 ───────────────────────────
static bool OpenHidPath(const std::wstring& path, DWORD access, HANDLE* out) {
    *out = CreateFileW(path.c_str(), access, FILE_SHARE_READ | FILE_SHARE_WRITE,
                       nullptr, OPEN_EXISTING, 0, nullptr);
    return *out != INVALID_HANDLE_VALUE;
}

// 枚举所有 Rapoo HID 集合，找到承载电量数据的那个（FF00/000F）
static bool FindRapooCollections(DevicePaths* out) {
    out->valid = false;
    out->info.clear();

    GUID guid; HidD_GetHidGuid(&guid);
    HDEVINFO devInfo = SetupDiGetClassDevsW(&guid, nullptr, nullptr,
                                            DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (devInfo == INVALID_HANDLE_VALUE) return false;

    SP_DEVICE_INTERFACE_DATA ifData; ifData.cbSize = sizeof(ifData);
    for (DWORD i = 0; SetupDiEnumDeviceInterfaces(devInfo, nullptr, &guid, i, &ifData); ++i) {
        DWORD need = 0;
        SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, nullptr, 0, &need, nullptr);
        if (!need) continue;
        std::vector<BYTE> detail(need);
        auto* did = reinterpret_cast<PSP_DEVICE_INTERFACE_DETAIL_DATA_W>(detail.data());
        did->cbSize = sizeof(SP_DEVICE_INTERFACE_DETAIL_DATA_W);
        if (!SetupDiGetDeviceInterfaceDetailW(devInfo, &ifData, did, need, &need, nullptr)) continue;

        std::wstring path = did->DevicePath;
        HANDLE h = INVALID_HANDLE_VALUE;
        if (!OpenHidPath(path, 0, &h)) continue;   // 查询属性用零权限句柄即可

        HIDD_ATTRIBUTES attr; attr.Size = sizeof(attr);
        if (!HidD_GetAttributes(h, &attr) || attr.VendorID != RAPOO_VID) {
            CloseHandle(h);
            continue;
        }

        PHIDP_PREPARSED_DATA pp = nullptr;
        USAGE page = 0, usage = 0;
        if (HidD_GetPreparsedData(h, &pp)) {
            HIDP_CAPS caps{};
            if (HidP_GetCaps(pp, &caps) == HIDP_STATUS_SUCCESS) {
                page = caps.UsagePage;
                usage = caps.Usage;
            }
            HidD_FreePreparsedData(pp);
        }
        CloseHandle(h);

        if (page == 0xFF00 && usage == 0x000F) out->info = path;
    }
    SetupDiDestroyDeviceInfoList(devInfo);

    out->valid = !out->info.empty();
    return out->valid;
}

// ─────────────────────────── 读取电量（纯只读） ───────────────────────────
// 只读 col06 的 Feature report ID 8，不向设备写入任何数据。
// 句柄保持在 g.hInfo 复用，避免反复开关句柄扰动 2.4G 链路。
static bool ReadBattery(int* battery, std::wstring* log) {
    if (g.hInfo == INVALID_HANDLE_VALUE) {
        if (log) *log += L"  信息句柄未打开\n";
        return false;
    }

    std::vector<BYTE> buf(STATUS_BUFLEN, 0);
    buf[0] = STATUS_FEATID;

    // 冗余读取：单次失败往往只是设备恰好处于休眠唤醒间隙，
    // 重试一次可显著减少误报（重试同样只读，无副作用）。
    BOOL ok = FALSE;
    for (int attempt = 0; attempt < 2 && !ok; ++attempt) {
        std::fill(buf.begin(), buf.end(), 0);
        buf[0] = STATUS_FEATID;
        ok = HidD_GetFeature(g.hInfo, buf.data(), (ULONG)buf.size());
        if (!ok) {
            DWORD err = GetLastError();
            if (log && attempt == 1) {
                wchar_t l[160]; swprintf_s(l, L"  读取失败 err=%lu\n", err); *log += l;
            }
            Sleep(120);
        }
    }
    if (!ok) return false;

    if ((int)buf.size() <= BATTERY_OFFSET) {
        if (log) *log += L"  返回数据过短\n";
        return false;
    }

    // 电量合法性校验。
    // 0% 理论上合法，但实测发现设备在唤醒间隙会返回整块全零的缓冲，
    // 且首字节也不是正常的 0x01。用「首字节 + 内容非零」区分真 0% 与空帧。
    int bat = buf[BATTERY_OFFSET];
    if (bat == 0) {
        bool payloadAllZero = true;
        for (size_t k = 1; k < buf.size(); ++k) if (buf[k]) { payloadAllZero = false; break; }
        if (payloadAllZero || buf[0] != 0x01) {
            if (log) *log += L"  空帧（全零缓冲），已丢弃\n";
            return false;
        }
    }
    if (bat > 100) {
        if (log) {
            wchar_t l[200];
            swprintf_s(l, L"  数据异常: first=0x%02X battery=%d\n", buf[0], bat);
            *log += l;
        }
        return false;
    }
    *battery = bat;
    return true;
}

// ─────────────────────────── UIPI 消息放行 ───────────────────────────
// Shell_NotifyIcon 通过向任务栏窗口发送消息来实现。若任务栏进程的完整性级别
// 高于本进程，用户界面特权隔离 (UIPI) 会拦截这些消息，导致注册返回
// ERROR_ACCESS_DENIED(5)。显式放行已知的托盘相关消息可解决此问题。
// ChangeWindowMessageFilterEx 对自身进程拥有的窗口始终允许调用。
static void AllowTrayMessages(HWND hwnd) {
    if (!hwnd) return;
    const UINT msgs[] = {
        WM_COPYDATA,                              // Shell_NotifyIcon 的主要载体
        WM_USER + 1, WM_USER + 2, WM_USER + 3,
        WM_USER + 4, WM_USER + 5,
        0x004A,                                   // WM_COPYGLOBALDATA
        RegisterWindowMessageW(L"TaskbarCreated"),
    };
    for (UINT m : msgs) {
        if (!m) continue;
        ChangeWindowMessageFilterEx(hwnd, m, MSGFLT_ALLOW, nullptr);
    }
}

// ─────────────────────────── 托盘图标绘制 ───────────────────────────
// 逐像素渲染，直接写 BGRA 值，背景完全透明。
//
// 字形来源：游戏图标包 v1.4 的 SVG（24x24 viewBox，纯填充路径），
// 由 tools/svg2cpp.py 解析并展平成多边形，写入 glyphs_gen.h。
// 相比 GDI 文本渲染的好处：
//   • 没有 ClearType 在 Alpha DIB 上产生的黑边
//   • 精确还原原始字形设计，缩放不失真
//   • 任意尺寸都清晰（当前用 8 倍超采样后再缩小，边缘平滑）
//
// 布局：电量数字居中铺满，黑色描边保证在浅色任务栏上也清晰。
static int  g_iconPx  = 16;          // 目标像素尺寸（按 DPI 计算）
static int  g_iconDim = 16 * 8;      // 超采样画布边长
static const int ICON_SS = 8;        // 超采样倍数

struct Canvas { DWORD* px; int w, h; };

static inline float Clamp01f(float v) { return v < 0 ? 0 : (v > 1 ? 1 : v); }

// source-over 混合，覆盖度为 cov
static inline void BlendPixel(Canvas& c, int x, int y, COLORREF col, float cov) {
    if (cov <= 0.0f) return;
    if (cov > 1.0f) cov = 1.0f;
    if (x < 0 || y < 0 || x >= c.w || y >= c.h) return;
    float sr = (float)GetRValue(col), sg = (float)GetGValue(col), sb = (float)GetBValue(col);
    DWORD& d = c.px[y * c.w + x];
    float da = (float)((d >> 24) & 0xFF) / 255.0f;
    float dr = (float)((d >> 16) & 0xFF), dg = (float)((d >> 8) & 0xFF), db = (float)(d & 0xFF);
    float oa = cov + da * (1.0f - cov);
    if (oa <= 0.0f) { d = 0; return; }
    float orr = (sr * cov + dr * da * (1.0f - cov)) / oa;
    float og  = (sg * cov + dg * da * (1.0f - cov)) / oa;
    float ob  = (sb * cov + db * da * (1.0f - cov)) / oa;
    d = ((DWORD)(BYTE)(oa * 255.0f + 0.5f) << 24)
      | ((DWORD)(BYTE)(orr + 0.5f) << 16)
      | ((DWORD)(BYTE)(og  + 0.5f) << 8)
      |  (DWORD)(BYTE)(ob  + 0.5f);
}

struct PolyCache {
    float* fx = nullptr;
    float* fy = nullptr;
    int*   start = nullptr;
    int*   cnt = nullptr;
    int    polyCount = 0;
    int    totalPts = 0;
    float  minX = 0, minY = 0, maxX = 0, maxY = 0;

    ~PolyCache() { free(fx); free(fy); free(start); free(cnt); }

    // 把字形轮廓映射到目标矩形（一次求值，供逐像素扫描复用）
    bool Build(const Glyph& g, float x0, float y0, float w, float h) {
        float bw = g.maxX - g.minX, bh = g.maxY - g.minY;
        if (bw <= 0 || bh <= 0 || w <= 0 || h <= 0) return false;
        totalPts = 0;
        for (int i = 0; i < g.polyCount; ++i) totalPts += g.polys[i].n;
        if (!totalPts) return false;

        fx = (float*)malloc(sizeof(float) * totalPts);
        fy = (float*)malloc(sizeof(float) * totalPts);
        start = (int*)malloc(sizeof(int) * g.polyCount);
        cnt = (int*)malloc(sizeof(int) * g.polyCount);
        if (!fx || !fy || !start || !cnt) return false;

        float sx = w / bw, sy = h / bh;
        int k = 0;
        minX = 1e9f; maxX = -1e9f; minY = 1e9f; maxY = -1e9f;
        for (int i = 0; i < g.polyCount; ++i) {
            start[i] = k; cnt[i] = g.polys[i].n;
            for (int j = 0; j < g.polys[i].n; ++j) {
                float vx = x0 + (g.polys[i].pts[j * 2]     - g.minX) * sx;
                float vy = y0 + (g.polys[i].pts[j * 2 + 1] - g.minY) * sy;
                fx[k] = vx; fy[k] = vy; ++k;
                if (vx < minX) minX = vx;  if (vx > maxX) maxX = vx;
                if (vy < minY) minY = vy;  if (vy > maxY) maxY = vy;
            }
        }
        polyCount = g.polyCount;
        return true;
    }

    // 非零环绕规则判断内外 + 到边界的最近距离
    void Sample(float xx, float yy, bool* inside, float* dist) const {
        int wind = 0;
        float minD = 1e9f;
        for (int i = 0; i < polyCount; ++i) {
            int s0 = start[i], n = cnt[i];
            for (int a = 0, b = n - 1; a < n; b = a++) {
                float ax = fx[s0 + b], ay = fy[s0 + b];
                float bx = fx[s0 + a], by = fy[s0 + a];
                if ((ay > yy) != (by > yy)) {
                    float t = (yy - ay) / (by - ay);
                    if (ax + t * (bx - ax) > xx) { if (by > ay) ++wind; else --wind; }
                }
                float dx = bx - ax, dy = by - ay;
                float L2 = dx * dx + dy * dy;
                float u = (L2 > 0) ? Clamp01f(((xx - ax) * dx + (yy - ay) * dy) / L2) : 0.0f;
                float qx = ax + u * dx - xx, qy = ay + u * dy - yy;
                float d = sqrtf(qx * qx + qy * qy);
                if (d < minD) minD = d;
            }
        }
        *inside = (wind != 0);
        *dist = minD;
    }
};

// 带描边的字形：一次轮廓求值同时得到内外与距离，
// 内部填填充色，外侧按距离淡出写入描边色。
// 早期实现是按 9 个偏移各完整重绘一遍，慢 9 倍，已优化掉。
static void FillGlyphOutline(Canvas& c, const Glyph& g, float x0, float y0,
                             float w, float h, COLORREF fill, COLORREF stroke,
                             float strokePx) {
    PolyCache pc;
    if (!pc.Build(g, x0, y0, w, h)) return;

    int px0 = (int)floorf(pc.minX - strokePx) - 1, px1 = (int)ceilf(pc.maxX + strokePx) + 1;
    int py0 = (int)floorf(pc.minY - strokePx) - 1, py1 = (int)ceilf(pc.maxY + strokePx) + 1;

    for (int py = py0; py <= py1; ++py) {
        for (int px = px0; px <= px1; ++px) {
            bool inside; float minD;
            pc.Sample(px + 0.5f, py + 0.5f, &inside, &minD);

            // 有符号距离覆盖率。
            // 早期写成 inside ? Clamp01f(0.5f - minD) + 0.5f : ...，
            // 但 Clamp01f 会先把 (0.5 - minD) 压到 0，于是任何离边界超过
            // 半个像素的内部像素覆盖率都恰好是 0.5 —— 整块字形恒为半透明
            // （alpha≈128），看起来灰蒙蒙的。这是托盘数字对比度低的真正原因。
            float cov = inside ? Clamp01f(0.5f + minD) : Clamp01f(0.5f - minD);
            if (cov <= 0.0f) continue;

            if (inside) {
                BlendPixel(c, px, py, fill, cov);
            } else {
                if (strokePx <= 0.0f) continue;
                float t = Clamp01f(minD / strokePx);   // 0 在边缘，1 在描边末端
                if (t >= 0.999f) continue;
                BlendPixel(c, px, py, stroke, cov * (1.0f - t));
            }
        }
    }
}

// 未知状态用的圆头短横线
static void FillRoundBar(Canvas& c, float ax, float ay, float bx, float by,
                         float r, COLORREF col) {
    float mnx = (ax < bx ? ax : bx) - r - 1, mxx = (ax > bx ? ax : bx) + r + 1;
    float mny = (ay < by ? ay : by) - r - 1, mxy = (ay > by ? ay : by) + r + 1;
    for (int py = (int)floorf(mny); py <= (int)ceilf(mxy); ++py) {
        for (int px = (int)floorf(mnx); px <= (int)ceilf(mxx); ++px) {
            float xx = px + 0.5f, yy = py + 0.5f;
            float dx = bx - ax, dy = by - ay;
            float L2 = dx * dx + dy * dy;
            float u = (L2 > 0) ? Clamp01f(((xx - ax) * dx + (yy - ay) * dy) / L2) : 0.0f;
            float qx = ax + u * dx - xx, qy = ay + u * dy - yy;
            BlendPixel(c, px, py, col, Clamp01f(0.5f - (sqrtf(qx * qx + qy * qy) - r)));
        }
    }
}

static HICON MakeIcon(int battery) {
    BITMAPV5HEADER bi = {};
    bi.bV5Size        = sizeof(bi);
    bi.bV5Width       = g_iconPx;
    bi.bV5Height      = -g_iconPx;          // 负高度 = 自上而下
    bi.bV5Planes      = 1;
    bi.bV5BitCount    = 32;
    bi.bV5Compression = BI_BITFIELDS;
    bi.bV5RedMask     = 0x00FF0000;
    bi.bV5GreenMask   = 0x0000FF00;
    bi.bV5BlueMask    = 0x000000FF;
    bi.bV5AlphaMask   = 0xFF000000;

    void* bits = nullptr;
    HDC hdc = GetDC(nullptr);
    HBITMAP hbm = CreateDIBSection(hdc, reinterpret_cast<BITMAPINFO*>(&bi),
                                   DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, hdc);
    if (!hbm || !bits) { if (hbm) DeleteObject(hbm); return nullptr; }

    DWORD* big = (DWORD*)malloc((size_t)g_iconDim * g_iconDim * sizeof(DWORD));
    if (!big) { DeleteObject(hbm); return nullptr; }
    memset(big, 0, (size_t)g_iconDim * g_iconDim * sizeof(DWORD));

    Canvas cv; cv.px = big; cv.w = g_iconDim; cv.h = g_iconDim;
    const float U = (float)g_iconDim / g_iconPx;

    // 只有红绿两档：低于 20% 红，其余绿（未知状态用灰色）
    COLORREF digitCol;
    if (battery < 0)        digitCol = RGB(165, 165, 165);   // 未知：灰
    else if (battery <= 20) digitCol = RGB(255, 0, 0);       // 低电量：红 #FF0000
    else                    digitCol = RGB(97, 201, 18);     // 其余：绿 #61C912

    char text[8];
    if (battery < 0) strcpy_s(text, "--"); else sprintf_s(text, "%d", battery);
    const int n = (int)strlen(text);

    // 数字尽量铺满图标，靠黑色描边在浅色任务栏上保持可读
    const float marginX = 0.2f * U;
    const float availW  = (float)g_iconDim - marginX * 2.0f;

    // 等宽排版：取最宽数字的宽高比作为统一字宽，数字等宽才整齐
    float uniAR = 0.72f;
    for (int i = 0; i < 10; ++i) {
        const Glyph* gg = DIGIT_GLYPHS[i];
        if (!gg) continue;
        float gw = gg->maxX - gg->minX, gh = gg->maxY - gg->minY;
        if (gh > 0) { float ar = gw / gh; if (ar > uniAR) uniAR = ar; }
    }

    // 字高基准固定为 2 位数，否则位数越少数字越大
    // （早期版本「7」曾是「92」的 1.6 倍，因为字高随可用宽度浮动）。
    // 1~2 位共用同一字号；3 位才按宽度等比缩小，绝不越界。
    const float gapRatio = 0.04f;
    const int   baseDigits = 2;
    float glyphH = (float)g_iconDim - 1.0f * U;
    {
        float baseH = (float)g_iconDim - 1.0f * U;
        float baseNeed = baseH * (uniAR * (float)baseDigits + gapRatio * (baseDigits - 1));
        if (baseNeed > availW)
            baseH = availW / (uniAR * (float)baseDigits + gapRatio * (baseDigits - 1));
        glyphH = baseH;
    }
    if (glyphH * (uniAR * (float)n + gapRatio * (n - 1)) > availW)
        glyphH = availW / (uniAR * (float)n + gapRatio * (n - 1));

    float glyphW = glyphH * uniAR;
    float gapW   = glyphH * gapRatio;
    float totalW = glyphW * (float)n + gapW * (n - 1);
    float ox = ((float)g_iconDim - totalW) * 0.5f;
    if (ox < marginX) ox = marginX;
    const float oy = ((float)g_iconDim - glyphH) * 0.5f;

    for (int i = 0; i < n; ++i) {
        if (text[i] == '-') {
            float bx0 = ox + glyphW * 0.08f, bx1 = ox + glyphW * 0.92f, byy = oy + glyphH * 0.5f;
            float rBar = 1.25f * U, sOff = 1.0f * U;
            for (int dx = -1; dx <= 1; dx += 2)
                for (int dy = -1; dy <= 1; dy += 2)
                    FillRoundBar(cv, bx0 + dx * sOff, byy + dy * sOff,
                                     bx1 + dx * sOff, byy + dy * sOff, rBar, RGB(0, 0, 0));
            for (int d = -1; d <= 1; d += 2) {
                FillRoundBar(cv, bx0 + d * sOff, byy, bx1 + d * sOff, byy, rBar, RGB(0, 0, 0));
                FillRoundBar(cv, bx0, byy + d * sOff, bx1, byy + d * sOff, rBar, RGB(0, 0, 0));
            }
            FillRoundBar(cv, bx0, byy, bx1, byy, rBar, digitCol);
        } else {
            const Glyph* gg = DIGIT_GLYPHS[text[i] - '0'];
            if (gg) {
                float gw = gg->maxX - gg->minX, gh = gg->maxY - gg->minY;
                float w = (gh > 0) ? glyphH * (gw / gh) : glyphW;
                FillGlyphOutline(cv, *gg, ox + (glyphW - w) * 0.5f, oy, w, glyphH,
                                 digitCol, RGB(0, 0, 0), 1.0f * U);
            }
        }
        ox += glyphW + gapW;
    }

    // 超采样画布 -> 目标尺寸（盒式平均，保留透明度）
    DWORD* out = static_cast<DWORD*>(bits);
    for (int y = 0; y < g_iconPx; ++y) {
        for (int x = 0; x < g_iconPx; ++x) {
            int accA = 0, accR = 0, accG = 0, accB = 0;
            for (int sy = 0; sy < ICON_SS; ++sy) {
                const DWORD* row = big + (size_t)(y * ICON_SS + sy) * g_iconDim + x * ICON_SS;
                for (int sx = 0; sx < ICON_SS; ++sx) {
                    DWORD v = row[sx];
                    int a = (int)((v >> 24) & 0xFF);
                    accA += a;
                    accR += (int)((v >> 16) & 0xFF) * a;
                    accG += (int)((v >> 8) & 0xFF) * a;
                    accB += (int)(v & 0xFF) * a;
                }
            }
            const int N = ICON_SS * ICON_SS;
            int A = accA / N;
            int R = accA ? accR / accA : 0;
            int G = accA ? accG / accA : 0;
            int B = accA ? accB / accA : 0;
            out[y * g_iconPx + x] = ((DWORD)A << 24) | ((DWORD)R << 16)
                                  | ((DWORD)G << 8)  |  (DWORD)B;
        }
    }
    free(big);

    HBITMAP mask = CreateBitmap(g_iconPx, g_iconPx, 1, 1, nullptr);
    ICONINFO ii = {};
    ii.fIcon    = TRUE;
    ii.hbmColor = hbm;
    ii.hbmMask  = mask;
    HICON icon = CreateIconIndirect(&ii);
    DeleteObject(hbm);
    DeleteObject(mask);
    return icon;
}

// ─────────────────────────── 界面刷新 ───────────────────────────
static void RefreshTray() {
    // 重建图标前先销毁旧句柄，否则每次刷新都会泄漏一个 HICON
    HICON old = g.hIcon;
    g.hIcon = MakeIcon(g.haveBattery ? g.battery : -1);
    if (!g.hIcon) g.hIcon = old;          // 绘制失败则沿用旧图标
    else if (old) DestroyIcon(old);

    g.nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g.nid.hIcon  = g.hIcon;

    wchar_t tip[160];
    if (!g.haveBattery) {
        wcscpy_s(tip, L"雷柏鼠标电量：未检测到设备");
    } else {
        wchar_t when[16];
        FormatReadTime(when, 16);
        swprintf_s(tip,
                   L"雷柏鼠标电量：%d%%\n上次读取 %s",
                   g.battery, when);
    }
    wcsncpy_s(g.nid.szTip, tip, _TRUNCATE);
    Shell_NotifyIconW(NIM_MODIFY, &g.nid);
}

// 只更新托盘悬停提示的文字，不读设备、不重绘图标。
static void RefreshTipOnly() {
    if (!g.haveBattery) return;
    wchar_t when[16];
    FormatReadTime(when, 16);
    wchar_t tip[160];
    swprintf_s(tip, L"雷柏鼠标电量：%d%%\n上次读取 %s", g.battery, when);
    wcsncpy_s(g.nid.szTip, tip, _TRUNCATE);
    g.nid.uFlags = NIF_TIP;
    Shell_NotifyIconW(NIM_MODIFY, &g.nid);
    g.nid.uFlags = NIF_ICON | NIF_TIP | NIF_MESSAGE;
}

static void DoPoll() {
    if (g.pollBusy) return;
    g.pollBusy = true;

    DWORD interval = CurrentPollMs();

    // 句柄未就绪（首次启动或曾读取失败）时重新枚举设备
    if (g.hInfo == INVALID_HANDLE_VALUE) {
        if (!FindRapooCollections(&g.dev) ||
            !OpenHidPath(g.dev.info, GENERIC_READ | GENERIC_WRITE, &g.hInfo)) {
            g.hInfo = INVALID_HANDLE_VALUE;
            g.haveBattery = false;
            RefreshTray();
            g.pollBusy = false;
            SetTimer(g.hwnd, WM_POLL, POLL_RETRY_MS, nullptr);
            return;
        }
    }

    int bat = -1;
    if (ReadBattery(&bat, nullptr)) {
        g.battery      = bat;
        g.haveBattery  = true;
        g.lastGoodTick = NowMs();
    } else {
        // 读取失败：接收器可能被拔掉或鼠标休眠。关闭句柄，下次重新枚举。
        if (g.hInfo != INVALID_HANDLE_VALUE) {
            CloseHandle(g.hInfo);
            g.hInfo = INVALID_HANDLE_VALUE;
        }
        g.dev.valid = false;
        interval = POLL_RETRY_MS;
    }

    RefreshTray();
    g.pollBusy = false;
    SetTimer(g.hwnd, WM_POLL, interval, nullptr);
}

// ─────────────────────────── 浮窗面板 ───────────────────────────
static bool GetTaskbarRect(RECT* rc, UINT* edge) {
    HWND tray = FindWindowW(L"Shell_TrayWnd", nullptr);
    if (!tray || !GetWindowRect(tray, rc)) return false;
    if (edge) {
        if (rc->top <= 0 && rc->left <= 0) *edge = ABE_BOTTOM;
        else if (rc->top <= 0)             *edge = ABE_TOP;
        else if (rc->left <= 0)            *edge = ABE_LEFT;
        else                               *edge = ABE_RIGHT;
    }
    return true;
}

struct PanelRow {
    const wchar_t* label;
    UINT           cmd;      // 0 表示不可点击的信息行
    int            group;    // 同一组的行之间不画分隔线
    bool           checked;
};

static HWND     g_panel    = nullptr;
static int      g_panelHot = -1;      // 当前悬停的行号
static PanelRow g_rows[16];
static int      g_rowCount = 0;

// 布局尺寸
static const int ROW_H   = 32;    // 可点行高度
static const int INFO_H  = 46;    // 顶部信息行（大号电量 + 上次读取时间）
static const int PAD_TOP = 8;
static const int PAD_BOT = 6;
static const int SEP_H   = 8;     // 分组分隔线占用的额外高度
static const int CORNER  = 12;    // 圆角半径

// 刷新间隔子菜单
static HWND      g_sub           = nullptr;
static int       g_subHot        = -1;
static int       g_subHoverRow   = -1;
static ULONGLONG g_subHoverSince = 0;

static void CloseSub();
static bool PointInOurWindows(POINT sp);

// 面板高度 = 信息行 + 各可点行（含分组分隔线）
static int PanelHeight() {
    int h = PAD_TOP + INFO_H + PAD_BOT;
    for (int i = 1; i < g_rowCount; ++i) {
        if (g_rows[i].group != g_rows[i-1].group) h += SEP_H;
        h += ROW_H;
    }
    return h;
}

// 第 i 行的顶部 y 坐标
static int RowTop(int i) {
    int y = PAD_TOP + INFO_H;
    for (int k = 1; k <= i; ++k) {
        if (g_rows[k].group != g_rows[k-1].group) y += SEP_H;
        if (k == i) break;
        y += ROW_H;
    }
    return y;
}

static void BuildRows() {
    static wchar_t bufInfo[64];
    static wchar_t bufIvl[64];
    static wchar_t bufRefresh[40], bufAuto[40], bufExit[40];
    g_rowCount = 0;

    // 第 0 行是信息行（电量 + 上次读取时间），由 PaintPanel 专门排版
    if (g.haveBattery) swprintf_s(bufInfo, L"%d%%", g.battery);
    else               wcscpy_s(bufInfo, L"--");
    g_rows[g_rowCount++] = { bufInfo, 0, 0, false };

    // 刷新间隔：点击或悬停会弹出右侧子菜单
    {
        DWORD sec = POLL_CHOICES_SEC[g.pollIdx];
        wchar_t iv[24];
        if (sec < 60) swprintf_s(iv, L"%u 秒", sec);
        else          swprintf_s(iv, L"%u 分钟", sec / 60);
        swprintf_s(bufIvl, L"刷新间隔   %s   ▶", iv);
    }
    g_rows[g_rowCount++] = { bufIvl, IDM_IVL_OPEN, 0, false };

    wcscpy_s(bufRefresh, L"立即刷新");
    g_rows[g_rowCount++] = { bufRefresh, IDM_REFRESH, 1, false };

    wcscpy_s(bufAuto, L"开机自启");
    g_rows[g_rowCount++] = { bufAuto, IDM_AUTORUN, 1, IsAutoRunEnabled() };

    wcscpy_s(bufExit, L"退出");
    g_rows[g_rowCount++] = { bufExit, IDM_EXIT, 2, false };
}

static int PanelHitTest(int y) {
    for (int i = 1; i < g_rowCount; ++i) {
        int top = RowTop(i);
        if (y >= top && y < top + ROW_H) return i;
    }
    return -1;
}

// 用窗口区域裁出圆角（SetWindowRgn 后由系统接管该区域，无需删除）
static void ApplyRoundRegion(HWND hwnd) {
    RECT rc; GetClientRect(hwnd, &rc);
    HRGN rgn = CreateRoundRectRgn(0, 0, rc.right + 1, rc.bottom + 1, CORNER, CORNER);
    SetWindowRgn(hwnd, rgn, TRUE);
}

static void PaintPanel(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HBITMAP oldBmp = (HBITMAP)SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(mem, &rc, bg);
    DeleteObject(bg);
    SetBkMode(mem, TRANSPARENT);

    HFONT fBig   = CreateFontW(-30, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
    HFONT fItem  = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
    HFONT fSmall = CreateFontW(-13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                               DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                               CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");

    // ── 信息行：左侧大号电量，右侧「上次读取 时:分:秒」 ──
    {
        SelectObject(mem, fBig);
        SetTextColor(mem, RGB(16, 16, 20));
        RECT br = { 16, PAD_TOP, 130, PAD_TOP + INFO_H };
        DrawTextW(mem, g_rows[0].label, -1, &br,
                  DT_LEFT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);

        wchar_t when[16];
        FormatReadTime(when, 16);
        wchar_t age[64];
        if (g.haveBattery) swprintf_s(age, L"上次读取 %s", when);
        else               wcscpy_s(age, L"未检测到设备");

        SelectObject(mem, fSmall);
        SetTextColor(mem, RGB(140, 140, 150));
        RECT ar = { 120, PAD_TOP, rc.right - 16, PAD_TOP + INFO_H };
        DrawTextW(mem, age, -1, &ar,
                  DT_RIGHT | DT_BOTTOM | DT_SINGLELINE | DT_NOPREFIX);
    }

    // ── 可点行（第 1 行起） ──
    for (int i = 1; i < g_rowCount; ++i) {
        PanelRow& r = g_rows[i];
        int y = RowTop(i);

        if (r.group != g_rows[i-1].group) {
            HPEN p = CreatePen(PS_SOLID, 1, RGB(220, 220, 226));
            HPEN op = (HPEN)SelectObject(mem, p);
            MoveToEx(mem, 14, y - SEP_H / 2, nullptr);
            LineTo(mem, rc.right - 14, y - SEP_H / 2);
            SelectObject(mem, op);
            DeleteObject(p);
        }

        bool hot = (i == g_panelHot) || (i == g_subHoverRow);
        if (hot && r.cmd != 0) {
            HBRUSH hb = CreateSolidBrush(RGB(234, 234, 238));
            RECT hr = { 6, y, rc.right - 6, y + ROW_H };
            FillRect(mem, &hr, hb);
            DeleteObject(hb);
        }

        SelectObject(mem, fItem);
        SetTextColor(mem, RGB(48, 48, 58));
        RECT tr = { 18, y, rc.right - 32, y + ROW_H };
        DrawTextW(mem, r.label, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        if (r.checked) {
            SetTextColor(mem, RGB(16, 16, 20));
            RECT cr = { rc.right - 30, y, rc.right - 14, y + ROW_H };
            DrawTextW(mem, L"\u2713", -1, &cr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
    }

    // 描边最后画，压在最上层
    {
        HPEN bp = CreatePen(PS_SOLID, 1, RGB(196, 196, 204));
        HPEN obp = (HPEN)SelectObject(mem, bp);
        HBRUSH ob = (HBRUSH)SelectObject(mem, GetStockObject(NULL_BRUSH));
        Rectangle(mem, 0, 0, rc.right, rc.bottom);
        SelectObject(mem, ob);
        SelectObject(mem, obp);
        DeleteObject(bp);
    }

    DeleteObject(fBig);
    DeleteObject(fItem);
    DeleteObject(fSmall);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, oldBmp);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

// ─────────────── 子菜单：刷新间隔 ───────────────
static const char* SUB_CLASS = "RapooBatterySub";

static int SubHeight() { return PAD_TOP + POLL_CHOICE_COUNT * ROW_H + PAD_BOT; }

static void SubPaint(HWND hwnd) {
    PAINTSTRUCT ps;
    HDC hdc = BeginPaint(hwnd, &ps);
    RECT rc; GetClientRect(hwnd, &rc);

    HDC mem = CreateCompatibleDC(hdc);
    HBITMAP bmp = CreateCompatibleBitmap(hdc, rc.right, rc.bottom);
    HBITMAP ob = (HBITMAP)SelectObject(mem, bmp);

    HBRUSH bg = CreateSolidBrush(RGB(255, 255, 255));
    FillRect(mem, &rc, bg);
    DeleteObject(bg);
    SetBkMode(mem, TRANSPARENT);

    HFONT f = CreateFontW(-16, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_TT_PRECIS, CLIP_DEFAULT_PRECIS,
                          CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_SWISS, L"Microsoft YaHei UI");
    SelectObject(mem, f);

    for (int i = 0; i < POLL_CHOICE_COUNT; ++i) {
        int y = PAD_TOP + i * ROW_H;
        if (i == g_subHot) {
            HBRUSH hb = CreateSolidBrush(RGB(234, 234, 238));
            RECT hr = { 5, y, rc.right - 5, y + ROW_H };
            FillRect(mem, &hr, hb);
            DeleteObject(hb);
        }
        wchar_t lbl[32];
        DWORD sec = POLL_CHOICES_SEC[i];
        if (sec < 60) swprintf_s(lbl, L"每 %u 秒", sec);
        else          swprintf_s(lbl, L"每 %u 分钟", sec / 60);

        SetTextColor(mem, RGB(48, 48, 58));
        RECT tr = { 18, y, rc.right - 30, y + ROW_H };
        DrawTextW(mem, lbl, -1, &tr, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);

        if ((int)g.pollIdx == i) {
            SetTextColor(mem, RGB(16, 16, 20));
            RECT cr = { rc.right - 30, y, rc.right - 14, y + ROW_H };
            DrawTextW(mem, L"\u2713", -1, &cr, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
        }
    }

    HPEN bp = CreatePen(PS_SOLID, 1, RGB(196, 196, 204));
    HPEN obp = (HPEN)SelectObject(mem, bp);
    HBRUSH obr = (HBRUSH)SelectObject(mem, GetStockObject(NULL_BRUSH));
    Rectangle(mem, 0, 0, rc.right, rc.bottom);
    SelectObject(mem, obr);
    SelectObject(mem, obp);
    DeleteObject(bp);

    DeleteObject(f);
    BitBlt(hdc, 0, 0, rc.right, rc.bottom, mem, 0, 0, SRCCOPY);
    SelectObject(mem, ob);
    DeleteObject(bmp);
    DeleteDC(mem);
    EndPaint(hwnd, &ps);
}

static void CloseSub() {
    if (g_sub) { DestroyWindow(g_sub); g_sub = nullptr; }
    g_subHot = -1;
}

static LRESULT CALLBACK SubProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:       SubPaint(hwnd); return 0;
    case WM_ERASEBKGND:  return 1;

    case WM_MOUSEMOVE: {
        int y = GET_Y_LPARAM(lp);
        int idx = (y - PAD_TOP) / ROW_H;
        if (y < PAD_TOP || idx >= POLL_CHOICE_COUNT) idx = -1;
        if (idx != g_subHot) { g_subHot = idx; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;
    }

    case WM_LBUTTONUP: {
        int y = GET_Y_LPARAM(lp);
        int idx = (y - PAD_TOP) / ROW_H;
        if (idx >= 0 && idx < POLL_CHOICE_COUNT) {
            g.pollIdx = idx;
            SaveConfig();
            KillTimer(g.hwnd, WM_POLL);
            DoPoll();
            BuildRows();
            if (g_panel) InvalidateRect(g_panel, nullptr, TRUE);
            CloseSub();
        }
        return 0;
    }

    case WM_DESTROY:
        g_sub = nullptr;
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

static void RegisterSubClass(HINSTANCE inst) {
    WNDCLASSEXA wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = SubProc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = SUB_CLASS;
    RegisterClassExA(&wc);
}

static void OpenSub() {
    if (g_sub || !g_panel) return;
    RegisterSubClass(g.hInst);

    RECT pr; GetWindowRect(g_panel, &pr);
    int h = SubHeight(), w = 150;
    int x = pr.right - 2;
    int y = pr.top + PAD_TOP;

    // 右侧放不下就翻到左侧；底部越界则上移
    POINT pt = { x, y };
    HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
    MONITORINFO mi = { sizeof(mi) };
    if (GetMonitorInfoW(mon, &mi)) {
        if (x + w > mi.rcWork.right)  x = pr.left - w + 2;
        if (y + h > mi.rcWork.bottom) y = mi.rcWork.bottom - h;
        if (y < mi.rcWork.top)        y = mi.rcWork.top;
    }

    g_sub = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                            L"RapooBatterySub", L"sub",
                            WS_POPUP, x, y, w, h,
                            nullptr, nullptr, g.hInst, nullptr);
    if (!g_sub) return;
    ApplyRoundRegion(g_sub);
    ShowWindow(g_sub, SW_SHOWNOACTIVATE);
    InvalidateRect(g_sub, nullptr, TRUE);
}

static bool PointInOurWindows(POINT sp) {
    RECT r;
    if (g_panel && GetWindowRect(g_panel, &r) &&
        sp.x >= r.left && sp.x < r.right && sp.y >= r.top && sp.y < r.bottom) return true;
    if (g_sub && GetWindowRect(g_sub, &r) &&
        sp.x >= r.left && sp.x < r.right && sp.y >= r.top && sp.y < r.bottom) return true;
    return false;
}

// 关闭子菜单：轮询光标位置。
// 不能用 WM_MOUSELEAVE —— 从面板移向子菜单的途中会先触发父窗口的 leave，
// 那会在用户还没到达子菜单时就把菜单收掉。
static void PollSubClose() {
    if (!g_sub) return;
    POINT sp; GetCursorPos(&sp);
    if (!PointInOurWindows(sp)) CloseSub();
}

// 点击面板外部即关闭：用 WH_MOUSE_LL 钩子。
// 不能用 SetCapture —— 那会抢走全部鼠标消息，导致浮窗上的时间戳不再刷新。
static HHOOK g_mouseHook = nullptr;

static LRESULT CALLBACK MouseHookProc(int code, WPARAM wp, LPARAM lp) {
    if (code == HC_ACTION && (wp == WM_LBUTTONDOWN || wp == WM_RBUTTONDOWN)) {
        MSLLHOOKSTRUCT* ms = (MSLLHOOKSTRUCT*)lp;
        if (!PointInOurWindows(ms->pt) && g_panel)
            PostMessageW(g_panel, WM_CLOSE, 0, 0);
    }
    return CallNextHookEx(g_mouseHook, code, wp, lp);
}

static void InstallMouseHook() {
    if (!g_mouseHook)
        g_mouseHook = SetWindowsHookExW(WH_MOUSE_LL, MouseHookProc, g.hInst, 0);
}

static void UninstallMouseHook() {
    if (g_mouseHook) { UnhookWindowsHookEx(g_mouseHook); g_mouseHook = nullptr; }
}

static LRESULT CALLBACK PanelProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT:       PaintPanel(hwnd); return 0;
    case WM_ERASEBKGND:  return 1;

    case WM_MOUSEMOVE: {
        int idx = PanelHitTest(GET_Y_LPARAM(lp));

        int ivlRow = -1;
        for (int i = 0; i < g_rowCount; ++i)
            if (g_rows[i].cmd == IDM_IVL_OPEN) { ivlRow = i; break; }

        if (ivlRow >= 0 && idx == ivlRow) {
            // 悬停约 160ms 后自动展开子菜单
            if (g_subHoverRow != ivlRow) { g_subHoverRow = ivlRow; g_subHoverSince = NowMs(); }
            if (!g_sub && (NowMs() - g_subHoverSince) > 160) OpenSub();
        } else if (g_subHoverRow == ivlRow) {
            g_subHoverRow = -1;
        }

        if (idx != g_panelHot) { g_panelHot = idx; InvalidateRect(hwnd, nullptr, FALSE); }
        TRACKMOUSEEVENT tme = { sizeof(tme), TME_LEAVE, hwnd, 0 };
        TrackMouseEvent(&tme);
        return 0;
    }

    case WM_MOUSELEAVE:
        if (g_panelHot != -1) { g_panelHot = -1; InvalidateRect(hwnd, nullptr, FALSE); }
        return 0;

    // 面板唯一的定时器：
    //   每 500ms  重绘一次，让「上次读取 时:分:秒」保持新鲜；
    //             同时检查子菜单是否该收起。
    //   注意：托盘悬停提示由主窗口的 WM_TIP 定时器每秒刷新，
    //         这里不要重复刷新，否则两个定时器会互相叠加。
    case WM_TIMER:
        if (wp == WM_PANELTICK) {
            InvalidateRect(hwnd, nullptr, FALSE);
            UpdateWindow(hwnd);
            PollSubClose();
        }
        return 0;

    case WM_LBUTTONUP: {
        int idx = PanelHitTest(GET_Y_LPARAM(lp));
        if (idx >= 0 && g_rows[idx].cmd != 0) {
            UINT cmd = g_rows[idx].cmd;
            if (cmd == IDM_EXIT) {
                DestroyWindow(hwnd);
                DestroyWindow(g.hwnd);
            } else if (cmd == IDM_IVL_OPEN) {
                if (g_sub) CloseSub(); else OpenSub();
            } else if (cmd == IDM_AUTORUN) {
                SetAutoRun(!IsAutoRunEnabled());
                BuildRows();
                InvalidateRect(hwnd, nullptr, TRUE);
            } else if (cmd == IDM_REFRESH) {
                KillTimer(g.hwnd, WM_POLL);
                DoPoll();
                BuildRows();
                InvalidateRect(hwnd, nullptr, TRUE);
            }
        }
        return 0;
    }

    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) DestroyWindow(hwnd);
        return 0;

    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, WM_PANELTICK);
        CloseSub();
        UninstallMouseHook();
        g_panel = nullptr;
        g_panelHot = -1;
        g_subHoverRow = -1;
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static void RegisterPanelClass(HINSTANCE inst) {
    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = PanelProc;
    wc.hInstance     = inst;
    wc.hCursor       = LoadCursorW(nullptr, IDC_ARROW);
    wc.lpszClassName = PANEL_CLASS;
    wc.hbrBackground = nullptr;
    wc.style         = CS_DROPSHADOW;
    RegisterClassExW(&wc);
}

// 取托盘图标自身的屏幕矩形，用于把浮窗贴到它上方。
// Shell_NotifyIconGetRect 从 shell32 动态获取，兼容旧系统。
static bool GetTrayIconRect(HWND owner, UINT id, RECT* out) {
    typedef HRESULT (WINAPI *PFN)(const NOTIFYICONIDENTIFIER*, RECT*);
    static PFN pfn = nullptr;
    static bool tried = false;
    if (!tried) {
        tried = true;
        HMODULE shell = GetModuleHandleW(L"shell32.dll");
        if (shell) pfn = (PFN)GetProcAddress(shell, "Shell_NotifyIconGetRect");
    }
    if (!pfn) return false;
    NOTIFYICONIDENTIFIER nii = {};
    nii.cbSize = sizeof(nii);
    nii.hWnd   = owner;
    nii.uID    = id;
    return pfn(&nii, out) == S_OK;
}

// 显示/隐藏浮窗（左键和右键都调用这里）
static void TogglePanel() {
    if (g_panel) { DestroyWindow(g_panel); return; }

    RegisterPanelClass(g.hInst);
    BuildRows();
    const int H = PanelHeight();

    int x = 0, y = 0;
    RECT ir;
    if (GetTrayIconRect(g.hwnd, TRAY_ICON_ID, &ir)) {
        x = ir.left + (ir.right - ir.left) / 2 - PANEL_W / 2;   // 与图标水平居中
        y = ir.top - H - PANEL_GAP;                             // 图标正上方
    } else {
        // 退回：按任务栏整体定位
        RECT tb; UINT edge = ABE_BOTTOM;
        if (GetTaskbarRect(&tb, &edge)) {
            switch (edge) {
            case ABE_BOTTOM: x = tb.right - PANEL_W - 12; y = tb.top - H - PANEL_GAP;  break;
            case ABE_TOP:    x = tb.right - PANEL_W - 12; y = tb.bottom + PANEL_GAP;   break;
            case ABE_LEFT:   x = tb.right + PANEL_GAP;    y = tb.bottom - H - 12;      break;
            default:         x = tb.left - PANEL_W - PANEL_GAP; y = tb.bottom - H - 12; break;
            }
        } else {
            int sw = GetSystemMetrics(SM_CXSCREEN), sh = GetSystemMetrics(SM_CYSCREEN);
            x = sw - PANEL_W - 12; y = sh - H - 48;
        }
    }

    // 兜底：钳制到所在显示器工作区内。
    // 若 Shell_TrayWnd 返回异常值（多屏或非标准缩放时可能发生），
    // 直接使用会算出屏幕外坐标 —— 面板「创建成功但看不见」。
    {
        POINT pt = { x, y };
        HMONITOR mon = MonitorFromPoint(pt, MONITOR_DEFAULTTONEAREST);
        MONITORINFO mi = { sizeof(mi) };
        if (GetMonitorInfoW(mon, &mi)) {
            const RECT& wa = mi.rcWork;
            if (x + PANEL_W > wa.right) x = wa.right - PANEL_W;
            if (y + H > wa.bottom)      y = wa.bottom - H;
            if (x < wa.left)            x = wa.left;
            if (y < wa.top)             y = wa.top;
        }
    }

    g_panel = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_TOPMOST,
                              PANEL_CLASS, L"RapooBatteryPanel",
                              WS_POPUP, x, y, PANEL_W, H,
                              nullptr, nullptr, g.hInst, nullptr);
    if (!g_panel) return;

    ApplyRoundRegion(g_panel);
    ShowWindow(g_panel, SW_SHOW);
    SetForegroundWindow(g_panel);
    InvalidateRect(g_panel, nullptr, TRUE);
    UpdateWindow(g_panel);

    InstallMouseHook();
    SetTimer(g_panel, WM_PANELTICK, PANEL_TICK_MS, nullptr);
    g_subHoverRow = -1;
}

// ─────────────────────────── 主窗口过程 ───────────────────────────
static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TRAY:
        // 左键和右键都切换浮窗
        if (LOWORD(lp) == WM_LBUTTONUP || LOWORD(lp) == WM_RBUTTONUP) TogglePanel();
        return 0;

    // ⚠ SetTimer(hwnd, id, ...) 到点投递的是 WM_TIMER，定时器 id 在 wParam 里，
    //   而不是投递一个以该 id 命名的自定义消息。
    //   早期版本误写成 case WM_POLL，导致轮询只在启动时执行过一次。
    case WM_TIMER:
        if (wp == WM_POLL) {
            KillTimer(hwnd, WM_POLL);
            DoPoll();
        } else if (wp == WM_TIP) {
            RefreshTipOnly();   // 只刷新悬停提示，不读设备
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDM_REFRESH:
            KillTimer(hwnd, WM_POLL);
            DoPoll();
            break;
        case IDM_AUTORUN:
            SetAutoRun(!IsAutoRunEnabled());
            break;
        case IDM_EXIT:
            DestroyWindow(hwnd);
            break;
        default:
            if (LOWORD(wp) >= IDM_POLL_BASE && LOWORD(wp) < IDM_POLL_BASE + POLL_CHOICE_COUNT) {
                g.pollIdx = LOWORD(wp) - IDM_POLL_BASE;
                SaveConfig();
                KillTimer(hwnd, WM_POLL);
                DoPoll();       // 立即按新间隔重新起表
            }
            break;
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, WM_POLL);
        KillTimer(hwnd, WM_TIP);
        Shell_NotifyIconW(NIM_DELETE, &g.nid);
        if (g.hIcon) DestroyIcon(g.hIcon);
        if (g.hInfo != INVALID_HANDLE_VALUE) CloseHandle(g.hInfo);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

// ─────────────────────────── 入口 ───────────────────────────
// --test-tray：逐步执行托盘注册并打印结果，用于定位注册失败的原因
static int RunTraySelfTest(HINSTANCE hInst) {
    printf("=== tray self test ===\n\n");

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = DefWindowProcW;
    wc.hInstance     = hInst;
    wc.lpszClassName = L"RapooBatterySelfTest";
    ATOM atom = RegisterClassExW(&wc);
    printf("1. RegisterClassExW         : atom=%u err=%lu\n", atom, GetLastError());

    HWND hwnd = CreateWindowExW(0, L"RapooBatterySelfTest", L"selftest", 0,
                                0, 0, 0, 0, HWND_MESSAGE, nullptr, hInst, nullptr);
    printf("2. CreateWindow(HWND_MESSAGE): hwnd=%p err=%lu\n", hwnd, GetLastError());
    if (!hwnd) {
        hwnd = CreateWindowExW(0, L"RapooBatterySelfTest", L"selftest", 0,
                               0, 0, 0, 0, nullptr, nullptr, hInst, nullptr);
        printf("   fallback normal window   : hwnd=%p err=%lu\n", hwnd, GetLastError());
    }
    if (!hwnd) { printf("cannot create window\n"); return 2; }

    HICON icon = MakeIcon(92);
    printf("3. MakeIcon(92)             : icon=%p\n", icon);

    NOTIFYICONDATAW nid = {};
    nid.cbSize           = sizeof(nid);
    nid.hWnd             = hwnd;
    nid.uID              = TRAY_ICON_ID;
    nid.uCallbackMessage = WM_TRAY;
    nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    nid.hIcon            = icon;
    wcscpy_s(nid.szTip, L"RapooBattery self test");
    printf("4. cbSize=%u / sizeof=%u\n", nid.cbSize, (unsigned)sizeof(NOTIFYICONDATAW));

    AllowTrayMessages(hwnd);
    SetLastError(0);
    BOOL added = Shell_NotifyIconW(NIM_ADD, &nid);
    DWORD addErr = GetLastError();
    printf("5. Shell_NotifyIcon(NIM_ADD): %d err=%lu %s\n",
           added, addErr, added ? "" : "  <== FAILED");

    if (!added) {
        HICON fb = LoadIconW(nullptr, IDI_APPLICATION);
        nid.hIcon = fb;
        BOOL again = Shell_NotifyIconW(NIM_ADD, &nid);
        printf("6. retry with system icon   : icon=%p result=%d err=%lu\n",
               fb, again, GetLastError());
        printf("   success here  -> custom icon drawing is the problem\n");
        printf("   still failing -> tray registration blocked by environment\n");
    }

    printf("\nIcon added. Check the tray (including the ^ overflow).\n");
    printf("Removing in 10 seconds...\n");
    fflush(stdout);
    Sleep(10000);

    Shell_NotifyIconW(NIM_DELETE, &nid);
    if (icon) DestroyIcon(icon);
    printf("tray icon removed, self test done.\n");
    return 0;
}

// --read [次数]：只读取并打印电量，不创建窗口/托盘，便于命令行验证协议实现
static int RunReadTest(LPWSTR lpCmdLine) {
    int times = 1;
    const wchar_t* p = wcsstr(lpCmdLine, L"--read");
    if (p) { int v = _wtoi(p + 6); if (v > 0) times = v; }

    DevicePaths dp;
    if (!FindRapooCollections(&dp)) {
        printf("未找到雷柏设备集合（请确认接收器已插好、鼠标已开机）\n");
        return 3;
    }
    g.dev = dp;
    printf("信息集合: %ls\n", dp.info.c_str());
    printf("读取方式: 纯只读 Feature report ID %d, 电量 = byte[%d]\n\n",
           STATUS_FEATID, BATTERY_OFFSET);

    if (!OpenHidPath(dp.info, GENERIC_READ | GENERIC_WRITE, &g.hInfo)) {
        printf("打开设备失败 err=%lu\n", GetLastError());
        return 4;
    }

    for (int i = 0; i < times; ++i) {
        int bat = -1;
        std::wstring log;
        bool ok = ReadBattery(&bat, &log);
        SYSTEMTIME st; GetLocalTime(&st);
        if (ok) printf("[%02d:%02d:%02d] 电量 = %d%%\n", st.wHour, st.wMinute, st.wSecond, bat);
        else    printf("[%02d:%02d:%02d] 读取失败: %ls\n",
                       st.wHour, st.wMinute, st.wSecond, log.c_str());
        fflush(stdout);
        if (i + 1 < times) Sleep(2000);
    }
    CloseHandle(g.hInfo);
    return 0;
}

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR lpCmdLine, int) {
    if (lpCmdLine && wcsstr(lpCmdLine, L"--test-tray")) return RunTraySelfTest(hInst);
    if (lpCmdLine && wcsstr(lpCmdLine, L"--read"))      return RunReadTest(lpCmdLine);

    Dbg(L"=== 启动 ===");
    g.hInst = hInst;

    // 按系统 DPI 计算托盘图标实际像素尺寸：100% -> 16px，125% -> 20px，150% -> 24px
    {
        HDC hdc = GetDC(nullptr);
        int dpi = hdc ? GetDeviceCaps(hdc, LOGPIXELSX) : 96;
        if (hdc) ReleaseDC(nullptr, hdc);
        if (dpi <= 0) dpi = 96;
        int px = MulDiv(16, dpi, 96);
        if (px < 12) px = 12;
        if (px > 64) px = 64;
        g_iconPx  = px;
        g_iconDim = px * ICON_SS;
        Dbg(L"DPI=%d -> 图标 %dx%d 像素", dpi, px, px);
    }

    LoadConfig();
    Dbg(L"刷新间隔索引=%d -> %lu ms", g.pollIdx, CurrentPollMs());

    // 单实例互斥。
    // ⚠ GetLastError() 必须在任何其他函数调用之前读取 —— Dbg() 内部会写文件、
    //   取时间、格式化字符串，这些调用会覆盖线程的 last-error 值，
    //   早期版本因此把「首次启动」误判成「已有实例在运行」。
    SetLastError(0);
    HANDLE mutex = CreateMutexW(nullptr, FALSE, L"RapooBattery_SingleInstance");
    DWORD mutexErr = GetLastError();
    if (mutex && mutexErr == ERROR_ALREADY_EXISTS) {
        Dbg(L"已有实例在运行，退出");
        MessageBoxW(nullptr,
            L"雷柏鼠标电量监视器已在运行。\n请查看系统托盘。",
            L"已在运行", MB_OK | MB_ICONINFORMATION | MB_TOPMOST);
        return 0;
    }

    WNDCLASSEXW wc = {};
    wc.cbSize        = sizeof(wc);
    wc.lpfnWndProc   = WndProc;
    wc.hInstance     = hInst;
    wc.lpszClassName = WND_CLASS;
    if (!RegisterClassExW(&wc)) return 1;

    g.hwnd = CreateWindowExW(0, WND_CLASS, L"RapooBattery", 0, 0, 0, 0, 0,
                             HWND_MESSAGE, nullptr, hInst, nullptr);
    if (!g.hwnd) {
        // 某些受限环境不支持 message-only 窗口，退回普通隐藏窗口
        g.hwnd = CreateWindowExW(0, WND_CLASS, L"RapooBattery", 0, 0, 0, 0, 0,
                                 nullptr, nullptr, hInst, nullptr);
    }
    if (!g.hwnd) { Dbg(L"无法创建窗口，退出"); return 2; }

    g.nid.cbSize           = sizeof(g.nid);
    g.nid.hWnd             = g.hwnd;
    g.nid.uID              = TRAY_ICON_ID;
    g.nid.uCallbackMessage = WM_TRAY;
    g.nid.uFlags           = NIF_ICON | NIF_TIP | NIF_MESSAGE;
    g.hIcon                = MakeIcon(-1);
    g.nid.hIcon            = g.hIcon;
    wcscpy_s(g.nid.szTip, L"RapooBattery");

    AllowTrayMessages(g.hwnd);
    SetLastError(0);
    BOOL added = Shell_NotifyIconW(NIM_ADD, &g.nid);
    DWORD addErr = GetLastError();
    Dbg(L"Shell_NotifyIcon(NIM_ADD): %d err=%lu", added, addErr);
    if (!added) {
        // 托盘注册被拒通常是权限问题（任务栏完整性级别高于本进程）。
        // RapooBattery.manifest 已声明 requireAdministrator，正常双击即可提权。
        Dbg(L"托盘注册失败 —— 请以管理员身份运行");
    }

    DoPoll();   // 首次读取，并按当前间隔起表

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }

    if (mutex) CloseHandle(mutex);
    return 0;
}
