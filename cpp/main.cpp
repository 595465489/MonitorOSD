// ============================================================================
// MonitorOSD C++ 版 —— 网吧显示器控制（Win32 + Direct2D + DDC/CI）
// 以 C# 参考实现的界面为基线，按 UI/ 设计规范补全其未完成部分：
//   - 深色主题 + 品牌自适应强调色 + 45° 氛围光（左上→右下）
//   - 三页签：参数设置 / 系统管理 / 关于（v1.6.8）
//   - 左栏显示器信息卡（Logo/品牌/型号/刷新率/分辨率/详细信息，选中三圈发光）
//   - 右栏参数分组卡（自绘滑条/分段，caps Allowed 联动过滤，滑块实时生效+合并写队列）
//   - 置灰行 + 原因文案 / "…" 未知值 / 色温=用户 才显示用户 RGB 三行
//   - 顶部按钮：保存基准 / 一键还原基准（hover/press 三态）
//   - Toast 浮层、状态栏（状态点+热键提示）
//   - 托盘图标 + 右键菜单、5 个全局热键、Esc 隐藏、Win11 DWM 圆角 / Win10 Region 兜底
//   - 字体：加载 Fonts\NotoSC / JBMonoOSD 子集（与 C# 嵌入资源同族名），
//     缺失时回落 Microsoft YaHei UI / Consolas（与 C# 回落链一致）
//   - Win7 SP1 ~ Win11 全兼容，/MT 静态链接单文件
//
// 设计规范 §6.2 补全（C# 未完成，此处按规范实现）：
//   - 系统管理页三卡：显示器维护（唤醒/OSD锁/恢复出厂）/
//     校准向导（状态点+打开向导）/ 软件设置（开机自启/最小化到托盘/强调色色块）
//     （v1.2 按需求移除「基准与还原」卡；自动还原策略仍留在 config.json/后台）
//   - 网管工具窗口：VCP 扫描器（1~255 全扫+报告保存）+ 差分校准向导
//     （基准→对比→指认→写 vcpmap.json，托盘菜单/系统页/命令行 /tools 均可打开）
//   - 命令行：/set 0xNN val [/verbose]、/restore、/scan、/detect、/tools
//   - vcpmap.json 码表覆盖（code/uimin/uimax/uiadd/values/labels；动作码黑名单拒绝绑定）
//   - config.json 扩展：AutoStart / MinimizeToTray / AccentMode / ManualAccent（兼容 C# 字段）
//
// v1.3 在线更新 / 意见反馈（服务器侧见 server/ 与 docs/在线更新与反馈-部署手册.md）：
//   - WinHTTP 拉清单 → 版本比较 → 下载 → SHA-256 强校验 → .old/.new 换名 → 重启（失败自动回滚）
//   - 反馈：JSON POST + HMAC-SHA256 签名，附带显示器快照与 log.txt 尾部；机器码只上报哈希
//   - 系统管理页新增「在线更新」卡；独立 GDI 窗口（双页签：软件更新 / 意见反馈，支持 Ctrl+V 粘贴）
//   - 命令行：/checkupdate、/feedback "内容" [0-2] [--log]、/selftest（10 项协议自检）
//
// 与 C# 的已知行为差异（有意调整，非缺陷）：
//   - ASUS 系统管理分类在固件里叫 "SYSTEM SETUP"（≠"SYSTEM"），C# 因此把功能项挤进参数页、
//     且其系统页为空；v1.2 起归一化为 "SYSTEM"，华硕参数页不再出现功能项卡。
//   - 分段/滑条点击仅在「参数设置」页生效（与 C# 一致）。
//   - OnAccent 用白字（v1.1 项目约定；设计规范 v1.0 为 #0A0A0B）。
// ============================================================================
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <winhttp.h>
#include <rpc.h>
#include <lmshare.h>
#include <lmcons.h>
#include <lowlevelmonitorconfigurationapi.h>
#include <highlevelmonitorconfigurationapi.h>
#include <physicalmonitorenumerationapi.h>
#include <d2d1.h>
#include <dwrite.h>
#include <dwmapi.h>
#include <wincodec.h>
#include <shellapi.h>
#include <windowsx.h>
#include <dbt.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <wbemidl.h>
#include <oleauto.h>
#include <shlobj.h>      // v1.5.3：SHGetKnownFolderPath（字体落盘回退到 LOCALAPPDATA）
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>
#include <vector>
#include <map>
#include <set>
#include <algorithm>
#include <mutex>
#include <thread>

#pragma comment(lib, "d2d1.lib")
#pragma comment(lib, "dwrite.lib")
#pragma comment(lib, "dxva2.lib")
#pragma comment(lib, "user32.lib")
#pragma comment(lib, "gdi32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "shlwapi.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "ole32.lib")
#pragma comment(lib, "windowscodecs.lib")
#pragma comment(lib, "dwmapi.lib")
// v1.4 起补全（换机器编译时缺这些会只有链接错误、看不出原因）：
#pragma comment(lib, "winhttp.lib")     // 在线更新 / 反馈
#pragma comment(lib, "setupapi.lib")    // EDID（SetupAPI）
#pragma comment(lib, "cfgmgr32.lib")    // EDID（设备实例 ID / 状态）
#pragma comment(lib, "oleaut32.lib")    // SafeArray（WMI 序列号）
#pragma comment(lib, "wbemuuid.lib")    // WMI
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "version.lib")

// ================= 基础工具 =================
static float ClampF(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }
static int   ClampI(int v, int lo, int hi)       { return v < lo ? lo : (v > hi ? hi : v); }

struct Col {
    float r, g, b, a;
    Col() : r(0), g(0), b(0), a(1) {}
    Col(float R, float G, float B, float A = 1.f) : r(R), g(G), b(B), a(A) {}
    static Col Hex(const char* hex, float a = 1.f) {
        auto h = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            if (c >= 'A' && c <= 'F') return c - 'A' + 10;
            return 0; };
        if (hex[0] == '#') hex++;
        return Col(h(hex[0]) * 16 + h(hex[1]), h(hex[2]) * 16 + h(hex[3]),
                   h(hex[4]) * 16 + h(hex[5]), a) / 255.f;
    }
    Col operator/(float d) const { return Col(r / d, g / d, b / d, a); }
    Col Dark(float perc)   const { return Col(r * (1 - perc), g * (1 - perc), b * (1 - perc), a); }   // ControlPaint.Dark
    Col Lighten(float amt) const { return Col((std::min)(1.f, r + amt), (std::min)(1.f, g + amt), (std::min)(1.f, b + amt), a); }
    bool operator==(const Col& o) const { return r == o.r && g == o.g && b == o.b && a == o.a; }
    D2D1_COLOR_F F() const { return D2D1::ColorF(r, g, b, a); }
};

static std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return "";
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), &s[0], n, nullptr, nullptr);
    return s;
}
static std::wstring Utf8ToWide(const std::string& s) {
    if (s.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, s.c_str(), (int)s.size(), &w[0], n);
    return w;
}
// UTF-8 路径的 fopen：fopen_s 按 ANSI 代码页解释路径，中文目录下全部静默失败，必须走 wide 版
static FILE* FopenUtf8(const std::string& path, const char* mode) {
    FILE* f = nullptr;
    _wfopen_s(&f, Utf8ToWide(path).c_str(), Utf8ToWide(mode).c_str());
    return f;
}
static std::string ToUpperAscii(std::string s) { for (auto& c : s) if (c >= 'a' && c <= 'z') c -= 32; return s; }
static std::string ToLowerAscii(std::string s) { for (auto& c : s) if (c >= 'A' && c <= 'Z') c += 32; return s; }
static bool Contains(const std::string& s, const char* sub) { return s.find(sub) != std::string::npos; }
static void LogLine(const std::string& msg);        // 前向声明（码表覆盖/资源定位先期使用）
// 两位大写十六进制（日志里标 VCP 码用）
static std::string Hex2(int v) {
    char b[8]; sprintf_s(b, "%02X", v & 0xFF); return b;
}
static void ShowToast(const std::string& msg);     // 前向声明（显示模式切换等早期代码使用）
static std::string ResolvePath(const char* rel);
static std::string TrimStr(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n"); if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n"); return s.substr(a, b - a + 1);
}

// ================= 主题（设计规范 §3/§9，逐值平移 Theme.cs）=================
namespace Theme {
    static const Col BgWindow      = Col::Hex("#0A0A0B");
    static const Col BgStatusBar   = Col::Hex("#0E0E10");
    static const Col SurfaceGroup  = Col::Hex("#121214");
    static const Col SurfaceCard   = Col::Hex("#141417");
    static const Col SurfaceSub    = Col::Hex("#1B1B1F");
    static const Col SurfaceHover  = Col::Hex("#232329");
    static const Col LineStrong    = Col::Hex("#27272A");
    static const Col LineSubtle    = Col::Hex("#1F1F23");
    static const Col LineOutline   = Col::Hex("#3F3F46");   // 未选复选框内描边（design-tokens line.outline）
    static const Col TextPrimary   = Col::Hex("#FAFAFA");
    static const Col TextSecondary = Col::Hex("#E4E4E7");
    static const Col TextTertiary  = Col::Hex("#A1A1AA");
    static const Col TextMuted     = Col::Hex("#71717A");
    static const Col OnAccent      = Col::Hex("#FFFFFF");   // v1.1：红底配白字
    static const Col OnLogo        = Col::Hex("#FFFFFF");
    static const Col Success       = Col::Hex("#10B981");
    static const Col Danger        = Col::Hex("#F43F5E");
    static Col Accent      = Col::Hex("#F04438");
    static Col AccentHover = Col::Hex("#F15B50");

    struct BrandColor { const char* vid; const char* hex; };
    static const BrandColor BrandAccents[] = {
        {"BNQ", "#F04438"}, {"ACI", "#F04438"}, {"ASU", "#F04438"}, {"AUS", "#F04438"}, {"HKC", "#F04438"},
        {"AOC", "#FF5A47"}, {"PHL", "#FF5A47"}, {"ANT", "#FF7A3D"},
        {"DEL", "#3B82F6"}, {"HWP", "#38BDF8"}, {"HEC", "#E8483C"},
        {"MSI", "#DA291C"}, {"GSM", "#A50034"}, {"SEC", "#1428A0"}, {"ACR", "#83B81A"},
        {"AWR", "#F04438"}, {"TIT", "#E8483C"}, {"HUA", "#E8483C"}, {"HAX", "#E8483C"}, {"XIA", "#FF6900"},
    };
    // manual 为 null 时按 EDID 厂商 ID 自动，未知回落默认红（与 C# ResolveAccent 一致）
    static void ResolveAccent(const std::string& vid, const std::string* manualHex) {
        const char* hex = nullptr;
        if (manualHex && !manualHex->empty()) hex = manualHex->c_str();
        else if (!vid.empty())
            for (auto& bc : BrandAccents)
                if (vid == bc.vid) { hex = bc.hex; break; }
        if (!hex) hex = "#F04438";
        Accent = Col::Hex(hex);
        AccentHover = Accent.Lighten(0.08f);
    }
}

// ================= DDC 总线（全局串行化，与 C# lock(_bus) 对齐）=================
static std::mutex g_bus;
// v1.6.9：ddccache.json 文件级互斥——AfterCapsReady 后台线程与 StartProbe 探测线程
// 可并发调用 SaveDdcCacheAll，读-合并-写重叠时后写覆盖先写（丢一侧更新）。
// SaveDdcCacheAll 内部无自身重入/间接重入（仅调 ReadTextFile/MonitorKey/NormalizeAnyKey/
// JsonSectionEnd，均不回写缓存），普通 mutex 即可。
static std::mutex g_cacheSaveMx;

// ================= 显示器设备 =================
struct MonitorDev {
    HANDLE handle = nullptr;
    std::wstring desc;
    std::string capModel, edidModel, serial, vendor;
    std::string manufacturerName;   // v1.5.3：WMI ManufacturerName（制造商全名，如 ASUSTeK Computer Inc.）
    std::string productSubkey;      // v1.5.3：注册表产品子键（如 BNQ7FB7）——卓威报通用名时的机型级身份
    std::string seriesKey;          // v1.5.3：枚举时刻冻结的系列键（EDID vendor+前缀），缓存/基准的唯一键——
                                    // 不随 caps 探测后 capModel 到位而漂移，否则存取键不一致导致缓存永远未命中
    int brand = 0;                  // 0=generic 1=zowie 2=asus 3=aoc
    int edidWeek = -1, edidYear = -1, edidVerMaj = -1, edidVerMin = -1, edidCmW = -1, edidCmH = -1;
    std::vector<unsigned char> edidRaw;   // 完整 EDID（含扩展块），详细信息弹窗解析用
    bool ddcOk = false, capsOk = false;
    bool capsFromCache = false;   // v1.6.0：caps 来自缓存，未经本次实测验证
    std::atomic<bool> capsDone{false};   // v1.5.3：LoadCaps 看门狗用（caps 挂起机型不拖垮缓存保存）
    std::set<int> supported;
    std::map<int, std::vector<unsigned>> allowed;
    std::map<int, unsigned> cache, cacheMax;
    // v1.6.6：写后轨迹。卓威等固件对 DDC 写入生效有延迟（渐变应用），
    // 生效前回读是过渡值。窗口内（3 秒）回读值 != 写入目标时不污染缓存，
    // 否则 3 秒轮询会把过渡值刷进 UI（滑块回弹 / 还原后界面瞬间变回旧样子）。
    struct WriteTrack { unsigned target; ULONGLONG tick; };
    std::map<int, WriteTrack> lastWrite;    // infoMx 保护
    std::string screenDevice;
    // 探测线程写 / UI 线程读（首屏先于探测完成），容器访问一律过这把锁
    // （std::map 插入会重平衡，无锁并发 find 是崩溃级竞争；ValSpec 的 int 改写撕裂读取无害，不锁）
    std::recursive_mutex infoMx;

    std::mutex pendMx;
    std::map<int, unsigned> pend;
    // v1.6.6 修复 4：hasPend 改原子——原实现是普通 bool，写线程在 pendMx 锁内置位，
    // 但读侧（timer3 L7585、RefreshAllValues L5865）有的不持 pendMx 直接读，与 pend 的
    // 修改之间无同步关系 → 可能读到「hasPend=true 但 pend 已空」或反之，让轮询在写队列
    // 未清时提前进入，把过渡值刷进 UI。原子 + 锁内写/原子读保证内存序。
    std::atomic<bool> hasPend{false};

    // ---- v1.5.8 P1-4：引用计数 + 延迟回收 ----
    // 背景：Pump / LoadCaps / WakeUp 都是 detach 线程，捕获裸 this。重探时
    //   for (auto* om : g_mons) { om->Close(); delete om; }
    // 当场销毁，在途线程醒来访问 pendMx/handle 就是访问已释放内存（偶发崩溃，难复现）。
    // 做法：delete 改成 retire()——先标记失效再减主人引用；在途线程用 Guard 持一份引用，
    //       最后一个引用释放时才真正 delete。这样不用把 g_mons 全改成 shared_ptr
    //       （那样要动几十处调用点，风险更大）。
    std::atomic<int>  refs{1};          // 1 = 主人（g_mons / 局部列表）那一份
    std::atomic<bool> closed{false};    // DestroyPhysicalMonitor 只调一次
    std::atomic<bool> retired{false};   // 已退役：不再接受新对外的写入/读取语义

    // 在途任务持有它，保证对象活到自己用完
    struct Guard {
        MonitorDev* p;
        explicit Guard(MonitorDev* m) : p(m) { if (p) p->refs.fetch_add(1); }
        ~Guard() { if (p) p->Release(); }
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
    };
    void AddRef() { refs.fetch_add(1); }
    void Release() {
        if (refs.fetch_sub(1) == 1) {      // 最后一个引用
            Close();
            delete this;
        }
    }
    // 退役：调用方不再使用该对象，但可能在途线程还在用。
    // 这里立刻 Close()——先释放物理监视器句柄，在途线程即使还在跑，
    // 后续 DDC 调用会因 handle==nullptr 直接失败（安全），而不是操作一个可能已被
    // 别的枚举重新分配到的野句柄。
    // 内存本身延迟释放：把"主人"那一份引用交给延迟回收线程，宽限期 3 秒。
    // 宽限期覆盖所有在途 detach 线程（Pump 30ms 一轮、LoadCaps 最长 6s 看门狗之外的
    // 分离线程、WakeUp 最长约 10s —— 这些线程自己也都持引用，所以宽限期只是兜底）。
    void Retire() {
        if (retired.exchange(true)) return;   // 幂等：重复 retire 不重复计数
        Close();
        // v1.6.2 修复引用计数配对（这里很容易算错，写清楚）：
        //   对象出生时 refs=1，代表「待退役的主人」那一份。
        //   旧实现：延迟线程 3 秒后 Release 摊掉的就是这份主人引用 —— 但线程自身
        //           没有持引用，若对象在这 3 秒内已被别人 Release 到 0，就会先 delete，
        //           延迟线程再 Release 就是野指针 → 偶发闪退。
        //   现在：先 AddRef 给延迟线程自己（refs+1），再立刻 Release 掉主人那份（refs-1）；
        //         3 秒后延迟线程 Release 自己那份，这次 Release 必然让 refs 归零并 delete。
        //   关键点：主人那份必须在「交棒时」就释放，否则 3 秒后只 Release 一次会永远差 1 → 泄漏。
        AddRef();                              // 延迟线程自己的一份
        std::thread([this]() {
            Sleep(3000);
            Release();                         // 释放延迟线程自己那份（最后一份 → delete）
        }).detach();
        Release();                             // 释放「主人」那份（交棒完成，对象由延迟线程接管）
    }
    bool IsRetired() const { return retired.load(); }

    bool Read(int code, unsigned& cur, unsigned& maxv, bool updateCache = true) {
        std::lock_guard<std::mutex> lk(g_bus);
        // v1.6.1：不能用 !handle 判空 —— dxva2 在本机合法返回 0 句柄且 DDC 可用。
        // 只挡「已退役」这一种确定失效的情况。
        if (retired.load()) return false;
        MC_VCP_CODE_TYPE t; DWORD c = 0, m = 0;
        bool ok = !!GetVCPFeatureAndVCPFeatureReply(handle, (BYTE)code, &t, &c, &m);
        if (!ok) return false;
        // v1.6.0：DDC 回包可能「API 返回 TRUE 但内容无效」——实测 DP 链路不稳时同一码两次读到的
        // max 会不一致（0x10 一次 100、一次 1），cur 恒为 1。必须校验合理性，否则脏值会被当成真值
        // 写进缓存、显示到界面，还会把错误的上限当成滑条范围。
        if (m == 0) {
            LogLine(std::string("DDC: 码 0x") + Hex2(code) + " 回包异常（max=0），判为无效");
            return false;
        }
        // 注意：cur/maxv 是【输出参数】，此处尚未赋值（调用方传的是未初始化变量）。
        // 必须用本次实际读到的 c 来校验 —— 写成 cur 会把调用方的栈垃圾当成当前值，
        // 几乎恒满足 cur>max → 每次读取都被判无效，整页参数全废（v1.6.0 首版的真实回归 bug）。
        if (c > m) {
            LogLine(std::string("DDC: 码 0x") + Hex2(code) + " 回包异常（cur>max），判为无效");
            return false;
        }
        // 不做「同码二次读取比对」：那会让每次读取耗时翻倍（50 码 → 100 次 DDC 调用），
        // 而且部分显示器同一码的 max 会随模式动态变化，二次比对会误杀正常机型。
        // 上面两条硬校验（max==0 / cur>max）已足够挡住脏回包。
        cur = c; maxv = m;
        if (updateCache) {
            { std::lock_guard<std::recursive_mutex> il(infoMx);
              // v1.6.6：写后保护窗口——过渡值不写缓存，但真值照常返回给调用方
              auto wt = lastWrite.find(code);
              if (wt != lastWrite.end() && GetTickCount64() - wt->second.tick < 3000 &&
                  c != wt->second.target)
                  return true;
              cache[code] = c; cacheMax[code] = m; }
        }
        return true;
    }
    bool WriteNow(int code, unsigned val) {
        bool ok;
        {
            std::lock_guard<std::mutex> lk(g_bus);
            if (retired.load()) return false;   // v1.6.1：同上，句柄 0 可能是合法的
            ok = !!SetVCPFeature(handle, (BYTE)code, val);
        }
        if (ok) { std::lock_guard<std::recursive_mutex> il(infoMx);
                  cache[code] = val;
                  lastWrite[code] = { val, GetTickCount64() }; }   // v1.6.6：记录写后轨迹（保护窗口用）
        return ok;
    }
    // 异步顺序写入：同码连续写入合并为最后一次（拖动滑块不积压）
    void BeginWrite(int code, unsigned val) {
        if (retired.load()) return;                       // 已退役：不再接新写入
        { std::lock_guard<std::recursive_mutex> il(infoMx);
          cache[code] = val;
          lastWrite[code] = { val, GetTickCount64() }; }  // v1.6.6：同步记录写后轨迹
        std::lock_guard<std::mutex> lk(pendMx);
        if (retired.load()) return;                       // 取锁期间可能被退役
        pend[code] = val;
        if (!hasPend) {
            hasPend = true;
            // v1.5.8：线程自持一份引用，对象被 retire 后也能安全跑完再自行释放
            AddRef();
            std::thread([this]() { Pump(); }).detach();
        }
    }
    void Pump() {
        for (;;) {
            std::map<int, unsigned> batch;
            {
                std::lock_guard<std::mutex> lk(pendMx);
                if (retired.load() || pend.empty()) { hasPend = false; break; }
                batch.swap(pend);
            }
            for (auto& kv : batch) {
                if (retired.load()) break;                // 退役后不再往旧句柄写字
                WriteNow(kv.first, kv.second);
            }
            // v1.6.6：收尾验证——队列已空（拖动结束/单击设置完成）才验证本批写入。
            // 卓威等固件写入 ACK 后实际应用有延迟（渐变），只写不验会出现「拖到 100
            // 实际停在 90」。验证读用 updateCache=false，不污染界面缓存。
            {
                bool more = false;
                { std::lock_guard<std::mutex> lk(pendMx); more = !retired.load() && !pend.empty(); }
                if (!more) {
                    for (auto& kv : batch) {
                        if (retired.load()) break;
                        Sleep(300);
                        unsigned c = 0, mx = 0;
                        if (!Read(kv.first, c, mx, false)) break;   // 读失败（休眠/退役）不再折腾
                        if (c != kv.second) {
                            if (kv.second > mx) break;              // 目标超出当前 max（模式变了），不硬补
                            WriteNow(kv.first, kv.second);
                            Sleep(150);
                            unsigned c2 = 0, mx2 = 0;
                            if (Read(kv.first, c2, mx2, false) && c2 != kv.second)
                                LogLine("DDC: 码 0x" + Hex2(kv.first) + " 补写后仍为 " +
                                        std::to_string(c2) + "（目标 " + std::to_string(kv.second) +
                                        "，固件未应用）");
                        }
                    }
                }
            }
            Sleep(30);
        }
        Release();                                        // 与 BeginWrite 里的 AddRef 配对
    }
    void Close() {
        // v1.6.2：不能用 `if (handle)` —— 句柄 0 在部分机型是合法值，
        // 跳过 DestroyPhysicalMonitor 会漏掉释放（与 Read 里的判空是同一类错误）。
        // 用 closed 标志保证只销毁一次。
        if (!closed.exchange(true)) DestroyPhysicalMonitor(handle);
        handle = nullptr;
    }
};

static std::string DisplayNameOf(MonitorDev* m);   // 前向声明（MonitorKey 先用）
static std::vector<MonitorDev*> g_mons;            // 已枚举的显示器（基准还原复用，免重新枚举）

// ================= 码表（1:1 平移自 VcpMap.cs）=================
struct ValSpec {
    int code;                       // VCP 码，0 = 不可控
    int uiMin = 0, uiMax = 100, uiAdd = 0;
    bool isEnum = false;
    std::vector<unsigned> optVals;  // 空 = C# 的 null（运行时按 caps/CacheMax 补）
    std::vector<std::string> optLabels;

    bool IsEnum() const { return isEnum && !optVals.empty(); }
    int  UiFromVcp(unsigned v) const { return (int)v + uiAdd; }
    unsigned VcpFromUi(int ui) const { return (unsigned)ClampI(ui - uiAdd, 0, 65535); }
    const char* LabelOf(unsigned v) const {
        for (size_t i = 0; i < optVals.size(); i++)
            if (optVals[i] == v && i < optLabels.size() && !optLabels[i].empty())
                return optLabels[i].c_str();
        return nullptr;
    }
};
typedef std::map<std::string, ValSpec> SpecMap;
static SpecMap g_dynSpec;   // caps 动态码表（brand==0 未知品牌时由 BuildCapsMenu 填充）

static void Sl(SpecMap& d, const char* id, int code, int mn, int mx, int add = 0) {
    d[id] = ValSpec{ code, mn, mx, add, false, {}, {} };
}
static void En(SpecMap& d, const char* id, int code, std::vector<unsigned> vals,
               std::vector<std::string> labs) {
    d[id] = ValSpec{ code, 0, 100, 0, true, vals, labs };
}

static SpecMap MakeZowie() {
    SpecMap d;
    Sl(d, "bright", 0x10, 0, 100);
    Sl(d, "contrast", 0x12, 0, 100);
    Sl(d, "beqexp", 0x7A, 0, 20);
    Sl(d, "zsatexp", 0x89, 0, 20);
    En(d, "dyacexp", 0x4D, {0,1,2}, {"关","高","高级"});
    En(d, "amaexp", 0xF0, {0,1,2,3}, {"关","低","中","高"});
    Sl(d, "sharp", 0x87, 0, 5);
    En(d, "gray", 0x72, {80,100,120,140,160}, {"1","2","3","4","5"});
    Sl(d, "bluelight", 0x19, 0, 100);   // 低蓝光
    // v1.5.6 P2-5：原此处还有第二条 "gamma"(0x72)，与上面的 gray 同码，会把 gray 的
    // 取值范围（1~5）覆盖成 gamma 的档位名（1.8~2.6），菜单里调"灰度"显示成伽马值。
    // ZOWIE 菜单树只用 gray（见 BuildMenu），gamma 是死条目，已删除。
    En(d, "lbl", 0xEA, {0,1,2,3,4,5}, {"0","1","2","3","4","5"});
    En(d, "colortemp", 0x14, {0x04,0x05,0x08,0x0B}, {"标准","偏蓝","偏红","用户"});
    En(d, "input", 0x60, {0x0F,0x11,0x12,0x13}, {"DP","HDMI1","HDMI2","HDMI3"});
    En(d, "mode", 0xDC, {0x00,0x03,0x15,0x16,0x18,0x19,0x1A,0x1B},
       {"标准","电影","FPS1","FPS2","FPS3","玩家1","玩家2","玩家3"});
    En(d, "dmode", 0x86, {0x01,0x02,0x05,0x0B,0x0C,0x0D,0x0E,0x0F,0x10,0x11,0x12,0x13,0x14},
       {"全面屏","全屏","","17寸(4:3)","19寸(4:3)","19寸宽","21.6寸(16:10)","22寸宽(16:9)","23寸宽(16:9)","24寸(16:9)","24寸宽(16:10)","",""});
    En(d, "smartfocus", 0xF4, {0,1}, {"关","开"});
    d["cformat"] = ValSpec{ 0xB5, 0, 100, 0, false, {}, {} };   // 值序未校准（C# OptionValues=null）
    Sl(d, "volume", 0x62, 0, 100);
    Sl(d, "zR", 0x6C, 0, 255);
    Sl(d, "zG", 0x6E, 0, 255);
    Sl(d, "zB", 0x70, 0, 255);
    // v1.5.6 P2-5：原此处有 "lowlbl"(0x19, 范围 0~5)，与上方 bluelight(0x19, 0~100) 同码且
    // 范围更窄，会把低蓝光的滑条上限从 100 压到 5（拖到一半就满）。菜单树无引用，已删除。
    d["picture"] = ValSpec{ 0xDC, 0, 100, 0, false, {}, {} };
    d["gamut"]   = ValSpec{ 0x5F, 0, 100, 0, false, {}, {} };
    Sl(d, "autooff", 0xF8, 0, 60);
    En(d, "freesync", 0x71, {0,1}, {"关","开"});
    En(d, "uniformity", 0xFA, {0,1}, {"关","开"});
    En(d, "osdlock", 0xCA, {0x02,0x01,0x03}, {"正常","锁菜单(按键可用)","全锁(菜单+按键)"});
    return d;
}
static SpecMap MakeAsus() {
    SpecMap d;
    Sl(d, "bright", 0x10, 0, 100);
    Sl(d, "contrast", 0x12, 0, 100);
    Sl(d, "sharp", 0x87, 0, 100);
    Sl(d, "trace", 0xE0, 0, 10);
    Sl(d, "bl", 0xE6, 0, 10);
    En(d, "gamevisual", 0xDC, {1,2,3,4,5,6,7,8,9},
       {"影院","风景","sRGB","用户","竞赛","RTS/RPG","FPS","MOBA","夜视"});
    Sl(d, "shadow", 0xE5, 0, 10);
    En(d, "elmb", 0xEE, {0,1}, {"关","开"});
    En(d, "gamma", 0x72, {80,100,120,140,160}, {"1.8","2.0","2.2","2.4","2.6"});
    En(d, "colortemp", 0x14, {0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0B},
       {"4000K","5000K","6500K","7500K","8200K","9300K","10000K","USER"});
    Sl(d, "sat", 0x8A, 0, 100);
    Sl(d, "uR", 0x16, 0, 255);
    Sl(d, "uG", 0x18, 0, 255);
    Sl(d, "uB", 0x1A, 0, 255);
    En(d, "input", 0x60, {0x0F,0x11,0x12,0x1B}, {"DP","HDMI1","HDMI2","USB-C"});
    Sl(d, "volume", 0x62, 0, 100);
    En(d, "mute", 0x8D, {1,2}, {"静音","取消静音"});
    En(d, "cross", 0xE3, {0x00,0x07,0x08,0x09,0x0A,0x0B,0x0C,0x0D,0x0E,0x0F},
       {"关","蓝点","绿点","小蓝十字","小绿十字","大蓝十字","大绿十字","动态点","动态小十字","动态大十字"});
    En(d, "timer", 0xE4, {0,1,2,3,4,5}, {"关","30分钟","40分钟","50分钟","60分钟","90分钟"});
    En(d, "fps", 0xEA, {0,1,2}, {"关","数字","柱状图"});
    return d;
}
static SpecMap MakeAoc() {
    // v1.6.7：按 AGON AG276QKD 实机扫描（39 码可读，MCCS 2.2）重校准。
    // 标注【已验证】= 实机写入+恢复双向成功；【待验证】= 写入成功、语义按 MCCS 标准+同族机型先验，日常使用反馈。
    // v1.6.8：删 ratio(0x86)/shadowR(0x6C)/shadowG(0x6E)/shadowB(0x70)——用户反馈画面比例与
    //          黑阶 RGB 调整均无视觉效果（AOC 固件未真正开放这些码的功能），菜单瘦身避免死项。
    SpecMap d;
    Sl(d, "bright", 0x10, 0, 100);          // 【已验证】实测 cur=60 max=100
    Sl(d, "contrast", 0x12, 0, 100);        // 【已验证】实测 50/100
    // v1.6.7 删除：sharp(0x87)/gamma(0x72)——本机实测无此码（回包异常），菜单死行
    // v1.6.7 色温校准：原「未校准滑条」改标准档位枚举（MCCS 2.2 Select color preset；
    // 实测 cur=5=6500K 档，与 ASUS 系同族枚举吻合，max=11=0x0B 用户档）
    En(d, "colortemp", 0x14, {0x04,0x05,0x06,0x08,0x0B},
       {"5000K","6500K","7500K","9300K","用户"});
    Sl(d, "uR", 0x16, 0, 100);              // 【已验证】用户红增益（实测 max=100，非 255）
    Sl(d, "uG", 0x18, 0, 100);              // 【已验证】用户绿增益
    Sl(d, "uB", 0x1A, 0, 100);              // 【已验证】用户蓝增益
    En(d, "dmode", 0xDC, {0x00,0x0B,0x0C,0x0D,0x0E,0x0F,0x10},
       {"标准","FPS","RTS","赛车","电竞1","电竞2","游戏3"});   // 【已验证】实测 cur=0x10 max=0x10 与枚举吻合
    En(d, "input", 0x60, {0x0F,0x11,0x12}, {"DP","HDMI1","HDMI2"});   // 实测 cur=0x0F max=0x12；VGA/DVI 本机不支持已剔除
    // v1.6.11 删除 volume(0x62)——用户要求去掉音量控制（写入本身有效，需要时可加回）。
    En(d, "mute", 0x8D, {0x01,0x02}, {"静音","取消静音"});            // 【待验证】MCCS 音频静音（ASUS 同码先验），写入/恢复双向成功
    // v1.6.10 实验 sat(0x20) 用户实测拖动无效果 → v1.6.11 删除。0x20 写入被 ACK 但语义无效
    //（MCCS 标准该位为水平频率只读）。显示器 OSD 里的饱和度（1-20 档）为 G-Menu 私有通道，
    // caps 声明的枚举码均已识别/排除，无对应 DDC 码，不再尝试。
    // v1.6.8 删除：ratio(0x86)/shadowR(0x6C)/shadowG(0x6E)/shadowB(0x70)——调整无视觉效果
    // v1.6.7 删除：od(0xA8)/shadow(0xE1)——本机实测无此码
    En(d, "osdlock", 0xCA, {0x02,0x01}, {"正常","锁菜单(按键可用)"}); // 实测 max=2：旧表 0x03「全锁」本机不支持，已剔除
    return d;
}
static SpecMap MakeGeneric() {
    SpecMap d;
    Sl(d, "bright", 0x10, 0, 100);
    Sl(d, "contrast", 0x12, 0, 100);
    Sl(d, "sharp", 0x87, 0, 100);
    d["dmode"] = ValSpec{ 0xDC, 0, 100, 0, false, {}, {} };   // 显示模式/场景预设（值序未校准）
    return d;
}
// ---- v1.4 扩展品牌：标准核心（亮度/对比度/锐利度/色温/伽马/饱和度/RGB）+ 品牌扩展 ----
// 原则：码值不确定的项先按「未校准」进表，真机不支持时由「DDC 不支持→整行隐藏」机制兜底
static SpecMap MakeStd() {
    SpecMap d;
    Sl(d, "bright", 0x10, 0, 100);
    Sl(d, "contrast", 0x12, 0, 100);
    Sl(d, "sharp", 0x87, 0, 100);
    En(d, "gamma", 0x72, {80,100,120,140,160}, {"1.8","2.0","2.2","2.4","2.6"});   // 0x72=Gamma（0x73 是 LPCM 音频，修正）
    En(d, "colortemp", 0x14, {0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0B},
       {"4000K","5000K","6500K","7500K","8200K","9300K","10000K","USER"});
    Sl(d, "sat", 0x8A, 0, 100);
    Sl(d, "uR", 0x16, 0, 255);
    Sl(d, "uG", 0x18, 0, 255);
    Sl(d, "uB", 0x1A, 0, 255);
    return d;
}
static SpecMap MakeHkc() {   // HKC / 泰坦军团 / 蚂蚁电竞：显示模式与响应加速（码值未校准）
    auto d = MakeStd();
    d["dmode"] = ValSpec{ 0xDC, 0, 100, 0, false, {}, {} };
    d["od"]    = ValSpec{ 0xA8, 0, 100, 0, false, {}, {} };
    return d;
}
static SpecMap MakeMsi() { return MakeHkc(); }
static SpecMap MakeLg() {    // LG：社区验证码（ddcutil 生态）
    auto d = MakeStd();
    En(d, "freesync", 0xF8, {0,1}, {"关","开"});   // Adaptive-Sync
    Sl(d, "blackeq", 0xF9, 0, 100);                // Black Stabilizer 暗部稳定
    Sl(d, "rt", 0xF7, 0, 100);                     // Response Time（值序未校准）
    return d;
}
static SpecMap g_zowie = MakeZowie(), g_asus = MakeAsus(), g_aoc = MakeAoc(), g_generic = MakeGeneric();
static SpecMap g_hkc = MakeHkc(), g_msi = MakeMsi(), g_lg = MakeLg();
static SpecMap g_samsung = MakeStd(), g_dell = MakeStd(), g_acer = MakeStd(), g_hp = MakeStd();
static SpecMap g_aw = MakeStd(), g_ant = MakeHkc(), g_titan = MakeHkc(), g_hua = MakeStd(), g_xia = MakeStd();
static const SpecMap* SpecOf(int brand) {
    switch (brand) {
    case 1: return &g_zowie;
    case 2: return &g_asus;
    case 3: return &g_aoc;
    case 5: return &g_hkc;
    case 6: return &g_msi;
    case 7: return &g_lg;
    case 8: return &g_samsung;
    case 9: return &g_dell;
    case 10: return &g_acer;
    case 11: return &g_hp;
    case 12: return &g_aw;
    case 13: return &g_ant;
    case 14: return &g_titan;
    case 15: return &g_hua;
    case 16: return &g_xia;
    default: return &g_generic;
    }
}
static const char* BrandKeyOf(int brand) {
    switch (brand) {
    case 1: return "ZOWIE";
    case 2: return "ASUS";
    case 3: return "AOC";
    case 5: return "HKC";
    case 6: return "MSI";
    case 7: return "LG";
    case 8: return "SAMSUNG";
    case 9: return "DELL";
    case 10: return "ACER";
    case 11: return "HP";
    case 12: return "ALIENWARE";
    case 13: return "ANT";
    case 14: return "TITAN";
    case 15: return "HUAWEI";
    case 16: return "XIAOMI";
    default: return "GENERIC";
    }
}

// ================= 极简 JSON（vcpmap.json 覆盖 / 配置序列化共用，保序对象）=================
struct JVal {
    enum T { NUL, BOOL, NUM, STR, ARR, OBJ } t = NUL;
    bool b = false;
    double num = 0;
    std::string s;
    std::vector<JVal> arr;
    std::vector<std::pair<std::string, JVal>> obj;   // 保序：写回 vcpmap.json 时不打乱注释顺序
    const JVal* find(const char* k) const {
        for (auto& kv : obj) if (kv.first == k) return &kv.second;
        return nullptr;
    }
    void set(const char* k, JVal v) {
        for (auto& kv : obj) if (kv.first == k) { kv.second = std::move(v); return; }
        obj.push_back({ k, std::move(v) });
    }
    static JVal S(const std::string& v) { JVal j; j.t = STR; j.s = v; return j; }
    static JVal N(double v)        { JVal j; j.t = NUM; j.num = v; return j; }
    static JVal B(bool v)          { JVal j; j.t = BOOL; j.b = v; return j; }
};
struct JParser {
    const std::string& s; size_t i = 0;
    explicit JParser(const std::string& src) : s(src) {}
    void ws() { while (i < s.size() && (s[i] == ' ' || s[i] == '\t' || s[i] == '\n' || s[i] == '\r')) i++; }
    bool val(JVal& out) {
        ws();
        if (i >= s.size()) return false;
        char c = s[i];
        if (c == '{') return obj(out);
        if (c == '[') return arr(out);
        if (c == '"') { out = JVal::S(str()); return true; }
        if (!strncmp(s.c_str() + i, "true", 4))  { i += 4; out = JVal::B(true);  return true; }
        if (!strncmp(s.c_str() + i, "false", 5)) { i += 5; out = JVal::B(false); return true; }
        if (!strncmp(s.c_str() + i, "null", 4))  { i += 4; out = JVal(); return true; }
        // 数字
        size_t st = i;
        while (i < s.size() && (isdigit((unsigned char)s[i]) || s[i] == '-' || s[i] == '+' ||
               s[i] == '.' || s[i] == 'e' || s[i] == 'E')) i++;
        if (i == st) return false;
        out = JVal::N(atof(s.substr(st, i - st).c_str()));
        return true;
    }
    std::string str() {
        std::string r;
        i++;                                  // 跳过开引号
        while (i < s.size() && s[i] != '"') {
            if (s[i] == '\\' && i + 1 < s.size()) {
                i++;
                switch (s[i]) {
                    case 'n': r += '\n'; break;
                    case 't': r += '\t'; break;
                    case 'r': r += '\r'; break;
                    case 'b': r += '\b'; break;
                    case 'f': r += '\f'; break;
                    case 'u': {   // \uXXXX → UTF-8（BMP 即可，界面文案够用）
                        if (i + 4 < s.size()) {
                            unsigned cp = (unsigned)strtoul(s.substr(i + 1, 4).c_str(), nullptr, 16);
                            i += 4;
                            if (cp < 0x80) r += (char)cp;
                            else if (cp < 0x800) { r += (char)(0xC0 | (cp >> 6)); r += (char)(0x80 | (cp & 0x3F)); }
                            else { r += (char)(0xE0 | (cp >> 12)); r += (char)(0x80 | ((cp >> 6) & 0x3F)); r += (char)(0x80 | (cp & 0x3F)); }
                        }
                        break;
                    }
                    default: r += s[i];
                }
                i++;
            } else r += s[i++];
        }
        if (i < s.size()) i++;                // 跳过闭引号
        return r;
    }
    bool obj(JVal& out) {
        out = JVal(); out.t = JVal::OBJ;
        i++; ws();
        if (i < s.size() && s[i] == '}') { i++; return true; }
        for (;;) {
            ws();
            if (i >= s.size() || s[i] != '"') return false;
            std::string k = str();
            ws(); if (i >= s.size() || s[i] != ':') return false;
            i++;
            JVal v;
            if (!val(v)) return false;
            out.obj.push_back({ k, std::move(v) });
            ws();
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == '}') { i++; return true; }
            return false;
        }
    }
    bool arr(JVal& out) {
        out = JVal(); out.t = JVal::ARR;
        i++; ws();
        if (i < s.size() && s[i] == ']') { i++; return true; }
        for (;;) {
            JVal v;
            if (!val(v)) return false;
            out.arr.push_back(std::move(v));
            ws();
            if (i < s.size() && s[i] == ',') { i++; continue; }
            if (i < s.size() && s[i] == ']') { i++; return true; }
            return false;
        }
    }
};
static bool JsonParse(const std::string& text, JVal& out) {
    JParser p(text);
    return p.val(out) && out.t == JVal::OBJ;
}
static void JsonEsc(const std::string& s, std::string& o) {
    o += '"';
    for (unsigned char c : s) {
        switch (c) {
            case '"':  o += "\\\""; break;
            case '\\': o += "\\\\"; break;
            case '\n': o += "\\n";  break;
            case '\r': o += "\\r";  break;
            case '\t': o += "\\t";  break;
            default:
                if (c < 0x20) { char b[8]; sprintf_s(b, "\\u%04x", c); o += b; }
                else o += (char)c;
        }
    }
    o += '"';
}
static void JsonDump(const JVal& v, std::string& o, int ind) {
    char buf[64];
    switch (v.t) {
        case JVal::NUL:  o += "null"; break;
        case JVal::BOOL: o += v.b ? "true" : "false"; break;
        case JVal::NUM:
            if (v.num == (long long)v.num) { sprintf_s(buf, "%lld", (long long)v.num); o += buf; }
            else { sprintf_s(buf, "%g", v.num); o += buf; }
            break;
        case JVal::STR:  JsonEsc(v.s, o); break;
        case JVal::ARR:
            if (v.arr.empty()) { o += "[]"; break; }
            o += "[\n";
            for (size_t k = 0; k < v.arr.size(); k++) {
                o.append(ind + 2, ' ');
                JsonDump(v.arr[k], o, ind + 2);
                if (k + 1 < v.arr.size()) o += ',';
                o += '\n';
            }
            o.append(ind, ' '); o += ']';
            break;
        case JVal::OBJ:
            if (v.obj.empty()) { o += "{}"; break; }
            o += "{\n";
            for (size_t k = 0; k < v.obj.size(); k++) {
                o.append(ind + 2, ' ');
                JsonEsc(v.obj[k].first, o);
                o += ": ";
                JsonDump(v.obj[k].second, o, ind + 2);
                if (k + 1 < v.obj.size()) o += ',';
                o += '\n';
            }
            o.append(ind, ' '); o += '}';
            break;
    }
}
static bool ReadTextFile(const std::string& path, std::string& out) {
    FILE* f = nullptr; f = FopenUtf8(path, "rb");
    if (!f) return false;
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) out.append(buf, n);
    fclose(f);
    return true;
}
static bool WriteTextFile(const std::string& path, const std::string& text) {
    FILE* f = nullptr; f = FopenUtf8(path, "wb");
    if (!f) return false;
    fwrite(text.data(), 1, text.size(), f);
    fclose(f);
    return true;
}

// ================= vcpmap.json 码表覆盖（README §7 / VcpMap.LoadOverrides）=================
// 动作码黑名单：恢复出厂/保存设置类，绝不能绑成滑条或分段（BenQ 社区 never touch）
static bool IsActionCode(int code) { return code == 0x04 || code == 0x08 || code == 0x0C; }
static bool ParseVcpCode(const std::string& s, int& out) {
    std::string t = TrimStr(s);
    if (t.empty()) return false;
    int base = 10;
    if (t.size() > 2 && t[0] == '0' && (t[1] == 'x' || t[1] == 'X')) { base = 16; t = t.substr(2); }
    if (t.empty()) return false;
    char* end = nullptr;
    long v = strtol(t.c_str(), &end, base);
    if (end == t.c_str() || v < 0 || v > 255) return false;
    out = (int)v;
    return true;
}
static void ApplyVcpOverride(SpecMap& d, const char* id, const JVal& o, const char* key) {
    ValSpec spec;
    auto exist = d.find(id);
    if (exist != d.end()) spec = exist->second;      // 增量覆盖：未提供的字段保留内置值
    bool changed = false;
    if (auto* v = o.find("code")) {
        int code = 0;
        bool ok = false;
        if (v->t == JVal::NUM) ok = v->num >= 0 && v->num <= 255 && (code = (int)v->num, true);
        else if (v->t == JVal::STR) ok = ParseVcpCode(v->s, code);
        if (!ok) { LogLine(std::string("vcpmap.json: ") + key + "." + id + " code 无效，已跳过"); return; }
        if (IsActionCode(code)) {
            LogLine(std::string("vcpmap.json: ") + key + "." + id + " 指向动作码（黑名单拒绝）");
            return;
        }
        spec.code = code; changed = true;
    }
    if (spec.code == 0 && !changed) return;          // 新增项必须给 code，否则无意义
    if (auto* v = o.find("uimin"))  if (v->t == JVal::NUM) { spec.uiMin  = (int)v->num; changed = true; }
    if (auto* v = o.find("uimax"))  if (v->t == JVal::NUM) { spec.uiMax  = (int)v->num; changed = true; }
    if (auto* v = o.find("uiadd"))  if (v->t == JVal::NUM) { spec.uiAdd  = (int)v->num; changed = true; }
    if (auto* v = o.find("values")) if (v->t == JVal::ARR) {
        spec.optVals.clear();
        for (auto& e : v->arr)
            if (e.t == JVal::NUM) spec.optVals.push_back((unsigned)e.num);
            else if (e.t == JVal::STR) { int c; if (ParseVcpCode(e.s, c)) spec.optVals.push_back((unsigned)c); }
        spec.isEnum = !spec.optVals.empty();
        changed = true;
    }
    if (auto* v = o.find("labels")) if (v->t == JVal::ARR) {
        spec.optLabels.clear();
        for (auto& e : v->arr)
            if (e.t == JVal::STR) spec.optLabels.push_back(e.s);
        while (spec.optLabels.size() < spec.optVals.size()) spec.optLabels.push_back("");
        changed = true;
    }
    if (changed) d[id] = spec;
}
static void LoadVcpOverrides() {
    std::string text;
    if (!ReadTextFile(ResolvePath("vcpmap.json"), text)) return;
    JVal root;
    if (!JsonParse(text, root)) { LogLine("vcpmap.json 解析失败（已忽略，用内置码表）"); return; }
    int applied = 0;
    for (auto& brandKv : root.obj) {
        if (brandKv.first.empty() || brandKv.first[0] == '_') continue;   // "_说明" 等注释键
        if (brandKv.second.t != JVal::OBJ) continue;
        const char* key = brandKv.first.c_str();
        // P1-3 修复：接受全部品牌键（原只认 4 键，12 品牌的覆盖"写入后静默丢弃"）
        SpecMap* target = nullptr;
        if (!strcmp(key, "ZOWIE")) target = &g_zowie;
        else if (!strcmp(key, "ASUS")) target = &g_asus;
        else if (!strcmp(key, "AOC")) target = &g_aoc;
        else if (!strcmp(key, "PHL")) target = &g_aoc;          // 飞利浦与 AOC 同码表
        else if (!strcmp(key, "GENERIC")) target = &g_generic;
        else if (!strcmp(key, "HKC")) target = &g_hkc;
        else if (!strcmp(key, "MSI")) target = &g_msi;
        else if (!strcmp(key, "LG")) target = &g_lg;
        else if (!strcmp(key, "SAMSUNG")) target = &g_samsung;
        else if (!strcmp(key, "DELL")) target = &g_dell;
        else if (!strcmp(key, "ACER")) target = &g_acer;
        else if (!strcmp(key, "HP")) target = &g_hp;
        else if (!strcmp(key, "ANT") || !strcmp(key, "TITAN")) target = &g_hkc;   // HKC 系
        else if (!strcmp(key, "ALIENWARE") || !strcmp(key, "HUAWEI") || !strcmp(key, "XIAOMI"))
            target = &g_dell;                                       // 无独立码表的走标准表
        if (!target) { LogLine("vcpmap.json: 未知品牌键 " + brandKv.first + "（已忽略）"); continue; }
        for (auto& itemKv : brandKv.second.obj) {
            if (itemKv.first.empty() || itemKv.first[0] == '_') continue;
            if (itemKv.second.t != JVal::OBJ) continue;
            ApplyVcpOverride(*target, itemKv.first.c_str(), itemKv.second, key);
            applied++;
        }
    }
    LogLine("vcpmap.json 覆盖已加载（" + std::to_string(applied) + " 项）");
}

// ================= 菜单树（1:1 平移自 Menus.cs）=================
struct Item {
    const char* en;                 // OSD 英文名
    const char* cn;                 // 中文显示名
    const char* mapId;              // 码表 ID；null=动作或不可控
    const char* action;             // savebase/restorebase/resetall/wakeup/scanner/calibrate
    // 运行时
    RECT rowR = {0,0,0,0};
    RECT trackR = {0,0,0,0};
    struct Seg { unsigned v; std::string label; RECT r; };
    std::vector<Seg> segs;
    bool visible = true;
};
struct Group { const char* en; const char* cn; std::vector<Item> items; RECT cardR = {0,0,0,0}; };

static std::vector<Group> BuildMenu(int brand) {
    std::vector<Group> g;
    auto grp = [&](const char* en, const char* cn) -> Group& {
        g.push_back(Group{ en, cn, {} });
        return g.back();
    };
    auto leaf = [&](Group& G, const char* en, const char* cn, const char* mapId) {
        G.items.push_back(Item{ en, cn, mapId, nullptr });
    };
    auto act = [&](Group& G, const char* en, const char* cn, const char* action) {
        G.items.push_back(Item{ en, cn, nullptr, action });
    };
    auto sysActs = [&](Group& s) {
        act(s, "RESET ALL", "恢复出厂设置", "resetall");
        act(s, "VCP SCANNER", "VCP 扫描器", "scanner");
        act(s, "CALIBRATE", "差分校准向导", "calibrate");
    };

    if (brand == 1) {   // ZOWIE
        auto& m = grp("MODE", "模式");
        leaf(m, "PICTURE MODE", "画面模式", "mode");
        auto& b = grp("BASIC", "基本设置");
        leaf(b, "BRIGHTNESS", "亮度", "bright");
        leaf(b, "CONTRAST", "对比度", "contrast");
        leaf(b, "SHARPNESS", "锐利度", "sharp");
        leaf(b, "COLOR SATURATION", "自然色彩饱和度", "zsatexp");
        leaf(b, "COLOR TEMPERATURE", "色温", "colortemp");
        leaf(b, "USER RED", "用户色温-红", "zR");
        leaf(b, "USER GREEN", "用户色温-绿", "zG");
        leaf(b, "USER BLUE", "用户色温-蓝", "zB");
        leaf(b, "GRAY", "灰度", "gray");
        leaf(b, "LOW BLUE LIGHT", "低蓝光", "bluelight");
        auto& a = grp("ADVANCED", "高级功能");
        leaf(a, "DYAC", "DyAc+", "dyacexp");
        leaf(a, "BLACK EQUALIZER", "黑位均衡器", "beqexp");
        leaf(a, "AMA", "AMA 瞬时加速", "amaexp");
        auto& s = grp("SYSTEM", "系统管理");
        act(s, "WAKEUP", "唤醒显示器", "wakeup");
        leaf(s, "OSD LOCK", "OSD 锁定", "osdlock");
        act(s, "RESET ALL", "恢复出厂设置", "resetall");
        act(s, "VCP SCANNER", "VCP 扫描器", "scanner");
        act(s, "CALIBRATE", "差分校准向导", "calibrate");
        act(s, "SAVE BASELINE", "保存基准", "savebase");
    } else if (brand == 2) { // ASUS
        auto& g1 = grp("GAMING", "游戏");
        leaf(g1, "GameVisual", "场景模式 GameVisual", "gamevisual");
        leaf(g1, "Shadow Boost", "暗部增强", "shadow");
        leaf(g1, "Blue Light Filter", "滤蓝光", "bl");
        leaf(g1, "ELMB", "低运动模糊 ELMB", "elmb");
        auto& g2 = grp("GAMEPLUS", "游戏辅助");
        leaf(g2, "Crosshair", "屏幕准星", "cross");
        leaf(g2, "Timer", "计时器", "timer");
        leaf(g2, "FPS Counter", "帧率显示", "fps");
        auto& g3 = grp("IMAGE", "图像");
        leaf(g3, "Brightness", "亮度", "bright");
        leaf(g3, "Contrast", "对比度", "contrast");
        leaf(g3, "Sharpness", "锐利度 VividPixel", "sharp");
        leaf(g3, "Trace Free", "灰阶响应 Trace Free", "trace");
        // 亮度与色温并同一张卡（用户需求：全左边）；输入/音量/静音卡已按需求移除
        leaf(g3, "Color Temp.", "色温", "colortemp");
        leaf(g3, "Gamma", "伽马", "gamma");
        leaf(g3, "Saturation", "饱和度", "sat");
        leaf(g3, "USER Red", "用户红增益", "uR");
        leaf(g3, "USER Green", "用户绿增益", "uG");
        leaf(g3, "USER Blue", "用户蓝增益", "uB");
        // v1.2：不再保留 C# 的 "SYSTEM SETUP"≠"SYSTEM" 怪癖——华硕这组归位系统管理页，
        // 参数页不再出现功能项卡（恢复出厂/扫描器/校准入口在系统管理页与托盘均可达）
        auto& s = grp("SYSTEM", "系统管理");
        sysActs(s);
    } else if (brand == 3) { // AOC/AGON（v1.6.7 按 AG276QKD 实机重组；v1.6.8 删 ratio/黑阶三行死项；
                             // v1.6.11 删饱和度实验行(0x20 无效果)与音量(用户要求)）
        auto& g1 = grp("GAME", "游戏");
        leaf(g1, "DISPLAY MODE", "显示模式", "dmode");
        auto& g2 = grp("IMAGE", "图像");
        leaf(g2, "BRIGHTNESS", "亮度", "bright");
        leaf(g2, "CONTRAST", "对比度", "contrast");
        auto& g3 = grp("COLOR", "色彩");
        leaf(g3, "COLOR TEMP", "色温", "colortemp");
        leaf(g3, "USER Red", "用户红增益", "uR");
        leaf(g3, "USER Green", "用户绿增益", "uG");
        leaf(g3, "USER Blue", "用户蓝增益", "uB");
        auto& g4 = grp("AUDIO", "音频");
        leaf(g4, "MUTE", "静音", "mute");
        auto& s = grp("SYSTEM", "系统管理");
        leaf(s, "OSD LOCK", "OSD/按键锁定", "osdlock");
        act(s, "RESET ALL", "恢复出厂设置", "resetall");
        act(s, "WAKEUP", "唤醒显示器", "wakeup");
        act(s, "VCP SCANNER", "VCP 扫描器", "scanner");
        act(s, "CALIBRATE", "差分校准向导", "calibrate");
    } else if (brand >= 5) {   // HKC/MSI/LG/三星/戴尔/宏碁/惠普/外星人/蚂蚁/泰坦/华为/小米：标准核心 + 品牌扩展
                               // （码值未校准的项按回读渲染；真机不支持时「DDC 不支持→整行隐藏」兜底）
        auto& g1 = grp("IMAGE", "图像");
        leaf(g1, "BRIGHTNESS", "亮度", "bright");
        leaf(g1, "CONTRAST", "对比度", "contrast");
        leaf(g1, "SHARPNESS", "锐利度", "sharp");
        if (brand == 5 || brand == 6 || brand == 13 || brand == 14) {   // 电竞线：游戏模式与响应加速
            leaf(g1, "DISPLAY MODE", "显示模式", "dmode");
            leaf(g1, "OVERDRIVE", "响应加速", "od");
        }
        if (brand == 7) {                                  // LG：社区验证码
            leaf(g1, "ADAPTIVE-SYNC", "自适应同步", "freesync");
            leaf(g1, "BLACK STABILIZER", "暗部稳定", "blackeq");
            leaf(g1, "RESPONSE TIME", "响应时间", "rt");
        }
        leaf(g1, "COLOR TEMP", "色温", "colortemp");
        leaf(g1, "GAMMA", "伽马", "gamma");
        leaf(g1, "Saturation", "饱和度", "sat");
        leaf(g1, "USER Red", "用户红增益", "uR");
        leaf(g1, "USER Green", "用户绿增益", "uG");
        leaf(g1, "USER Blue", "用户蓝增益", "uB");
        auto& s = grp("SYSTEM", "系统管理");
        sysActs(s);
    } else {            // generic
        auto& g1 = grp("IMAGE", "图像");
        leaf(g1, "BRIGHTNESS", "亮度", "bright");
        leaf(g1, "CONTRAST", "对比度", "contrast");
        leaf(g1, "SHARPNESS", "锐利度", "sharp");
        leaf(g1, "DISPLAY MODE", "显示模式", "dmode");
        auto& s = grp("SYSTEM", "系统管理");
        sysActs(s);
    }
    return g;
}

// ================= 基准档案 / 配置 / 日志（profiles.json 兼容 C# 版）=================
struct Baseline {
    std::map<int, unsigned> vals;
    std::string srcModel, srcSerial;   // v1.5.3 快照来源身份：跨设备还原时判定安全等级
};
static std::map<std::string, Baseline> g_profiles;
static int  g_debugBrand = 0;         // 0=跟随硬件 1=ZOWIE 2=ASUS 3=AOC 4=通用（调试预览）
struct AppConfig {
    int  autoRestoreMinutes = 0;
    bool restoreOnStart = false;
    bool restoreOnExit = false;
    bool autoStart = false;            // 开机自启（HKCU Run，设计规范 §6.2 软件设置卡）
    bool minimizeToTray = false;       // 最小化到托盘（否则正常最小化）
    std::string skin = "auto";         // 兼容 C#：auto 或 #RRGGBB
    std::string accentMode = "auto";   // auto=跟随品牌 / manual=用户手动（design-tokens.json）
    std::string manualAccent = "";     // #RRGGBB（AccentMode=manual 时生效）
    int debugBrand = 0;                // 调试预览：0=跟随硬件 1=ZOWIE 2=ASUS 3=AOC 4=通用（v1.4）
    std::string titlePrefix = "";      // 标题前置名（如「金橙电竞」，空=不显示；窗口/托盘显示「前置-显示器控制」）
    // ---- 在线更新 / 反馈（v1.3，字段名与 config.json 保持 PascalCase 兼容）----
    std::string updateBase = "";       // 更新服务根地址，空=内置默认
    std::string updateChannel = "latest";  // latest / beta
    bool checkUpdateOnStart = true;    // 启动后静默检查一次
    int  updateCheckHours = 6;         // 后台检查间隔（小时，0=关闭）
};
static AppConfig g_config;

static std::string ExeDir() {
    wchar_t buf[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, buf, MAX_PATH);
    std::wstring p = buf;
    size_t pos = p.find_last_of(L"\\/");
    return pos == std::wstring::npos ? "." : WideToUtf8(p.substr(0, pos));
}
// 资源定位：exe 目录 → 上一级 → 上两级（cpp\ 下构建时命中工程根目录）
static std::string ResolvePath(const char* rel) {
    std::string base = ExeDir();
    const char* pre[] = { "", "..\\", "..\\..\\" };
    for (auto p : pre) {
        std::string full = base + "\\" + p + rel;
        if (GetFileAttributesW(Utf8ToWide(full).c_str()) != INVALID_FILE_ATTRIBUTES) return full;
    }
    return base + "\\" + rel;
}
// v1.5.3 系列键：EDID 厂商码 + 型号数字段前字母前缀（XL2566K→BNQ|XL，XG27ACDNG→AUS|XG）。
// 同系列共用基准/缓存（同固件家族码值一致）；纯数字开头无前缀回落完整型号。
// v1.5.3 键体系（用户定版）：基准与缓存都按「厂商|完整型号」——
// 同型号共享（含跨代，保守还原兜底）、不同型号彻底分开（修复系列键粗粒度互相污染）。
// 型号标识优先级：EDID 具体型号（含数字）→ 注册表产品码映射（卓威报「ZOWIE XL LCD」通用名时）→ 产品码原样。
static const struct { const char* code; const char* model; } kProdModels[] = {
    { "BNQ7FA1", "XL2746K" },   // 卓威产品码 → 真实型号（网吧实际机型，新增机型在此扩充）
    { "BNQ7FB7", "XL2566K" },
};
static std::string ModelKeyFrom(const std::string& vendorIn, const std::string& specificModel,
                                const std::string& productSubkey) {
    std::string vendor = ToUpperAscii(vendorIn);
    if (vendor.empty() || vendor == "UNK") vendor = "GEN";
    std::string model = TrimStr(specificModel);
    std::string up = ToUpperAscii(model);
    if (!model.empty() && up.find("GENERIC") == std::string::npos &&
        model.find_first_of("0123456789") != std::string::npos)
        return vendor + "|" + up;                                     // 具体型号（XG27ACDNG 等）
    std::string code = ToUpperAscii(productSubkey);                   // 注册表产品子键（BNQ7FB7）
    if (!code.empty()) {
        for (auto& e : kProdModels) if (code == e.code) return vendor + "|" + e.model;
        return vendor + "|" + code;                                   // 未知产品码：产品码即机型身份（稳定）
    }
    return vendor + "|GEN";                                           // 全失败兜底
}
// 键归一（v1.5.3）：兼容旧格式载入——「型号|序列号」/纯型号 → 系列键；已是系列键的原样保留（ZOWIE 系列名归一）。
// 返回空串 = 无法归一（调用方丢弃）。LoadProfiles / SaveDdcCacheAll 共用。
static std::string NormalizeAnyKey(std::string key) {
    size_t bar = key.find('|');
    if (bar != std::string::npos) {
        std::string v = key.substr(0, bar), s = key.substr(bar + 1);
        if (v.size() == 3 && !s.empty() && s.size() <= 12) {          // 已是「厂商|系列」新格式
            if (s.rfind("ZOWIE ", 0) == 0) {                          // BNQ|ZOWIE XL LCD → BNQ|XL
                std::string rest = s.substr(6);
                size_t sp = rest.find(' ');
                if (sp != std::string::npos) rest = rest.substr(0, sp);
                if (!rest.empty()) return v + "|" + rest;
            }
            return v + "|" + s;
        }
        key = key.substr(0, bar);                                     // 「型号|序列号」旧格式：剥序列号
    }
    std::string up = ToUpperAscii(key);
    size_t z = up.find("ZOWIE");
    if (z != std::string::npos) {
        size_t s = z + 5;
        while (s < up.size() && up[s] == ' ') s++;
        size_t e = s;
        while (e < up.size() && up[e] != ' ') e++;
        if (e > s) return std::string("BNQ|") + up.substr(s, e - s);
    }
    size_t d = up.find_first_of("0123456789");
    if (d == std::string::npos || d == 0) return "";                  // 无法归一 → 丢弃
    std::string prefix = up.substr(0, d);
    const char* v = nullptr;
    if (prefix == "XL" || prefix == "RL" || prefix == "PD") v = "BNQ";
    else if (prefix == "PG" || prefix == "VG" || prefix == "XG" || prefix == "PA" || prefix == "PB" || prefix == "MG" || prefix == "MB") v = "AUS";
    else if (up.find("AGON") != std::string::npos) v = "AOC";
    else if (prefix == "G" || up.find("ULTRAGEAR") != std::string::npos) v = "GSM";
    else if (prefix == "C" || prefix == "ODYSSEY" || up.find("SAMSUNG") != std::string::npos) v = "SEC";
    else if (up.find("MSI") != std::string::npos || prefix == "MAG" || prefix == "MPG" || prefix == "OPTIX") v = "MSI";
    else if (prefix == "HKC" || prefix == "ANT" || up.find("TITAN") != std::string::npos) v = "HKC";
    else if (up.find("DELL") != std::string::npos || prefix == "U" || prefix == "S") v = "DEL";
    else if (up.find("ACER") != std::string::npos || prefix == "VI" || prefix == "NI" || prefix == "PR") v = "ACR";
    else if (up.find("HP") != std::string::npos || prefix == "OM") v = "HWP";
    else if (up.find("ALIENWARE") != std::string::npos || prefix == "AW") v = "AWR";
    else if (up.find("HUAWEI") != std::string::npos || up.find("MATEVIEW") != std::string::npos) v = "HUA";
    else if (up.find("XIAOMI") != std::string::npos || up.find("REDMI") != std::string::npos) v = "XIA";
    if (!v) return "";
    return std::string(v) + "|" + prefix;
}
static std::string MonitorKey(MonitorDev* m) {
    // 键在枚举时刻冻结（LoadLiveEdid 里算好存 seriesKey），此后 capModel 到位也不漂移——
    // 保证「存缓存时的键」和「下次读缓存时的键」必然一致
    if (!m->seriesKey.empty()) return m->seriesKey;
    return ModelKeyFrom(m->vendor, DisplayNameOf(m), m->productSubkey);   // 兜底：枚举时未算出（罕见）
}


// ================= DDC 探测缓存（ddccache.json）：二次启动秒出全量界面 =================
// 键 = EDID厂商|系列（v1.5.1，见 SeriesKeyOf）；值 = caps 结果 + supported/allowed + 各码上限与当前值。
// 启动时先载缓存（界面立即全量），深探测后台静默校准后回写（累积合并，离线型号保留）。
static std::string DdcCachePath() {
    char p[MAX_PATH];
    sprintf_s(p, "%s\\ddccache.json", ExeDir().c_str());
    return p;
}
static size_t JsonSectionEnd(const std::string& j, size_t open) {   // open 指向 '{'，返回配对 '}' 位置
    int depth = 0; bool inStr = false;
    for (size_t i = open; i < j.size(); i++) {
        char c = j[i];
        if (inStr) { if (c == '\\') i++; else if (c == '"') inStr = false; continue; }
        if (c == '"') inStr = true;
        else if (c == '{') depth++;
        else if (c == '}') { depth--; if (depth == 0) return i; }
    }
    return std::string::npos;
}
static void LoadDdcCache(MonitorDev* m) {
    std::string j;
    if (!ReadTextFile(DdcCachePath(), j)) return;
    std::string key = "\"" + MonitorKey(m) + "\"";           // v1.5.1 系列键
    size_t k = j.find(key);
    if (k == std::string::npos) {                            // 旧格式兼容：型号（或 型号|序列号）
        std::string model = ToUpperAscii(DisplayNameOf(m));
        size_t d = model.find_first_of("0123456789");
        std::string prefix = (d != std::string::npos && d > 0) ? model.substr(0, d) : model;
        // v1.4.x 旧键兼容：型号|序列号 / 纯型号 / 纯系列前缀（AUS 这类过渡键也认，键迁移期平滑）
        std::string alt1 = "\"" + DisplayNameOf(m) + "|";
        std::string alt2 = "\"" + model;
        std::string alt3 = "\"" + prefix;
        if (j.find(alt1) != std::string::npos) k = j.find(alt1);
        else if (j.find(alt2) != std::string::npos) k = j.find(alt2);
        else if (j.find(alt3) != std::string::npos) k = j.find(alt3);
    }
    if (k == std::string::npos) { LogLine("缓存: 未命中（键=" + MonitorKey(m) + "）"); return; }
    LogLine("缓存: 命中 键=" + MonitorKey(m));
    size_t ob = j.find('{', k);
    if (ob == std::string::npos) return;
    size_t cb = JsonSectionEnd(j, ob);
    if (cb == std::string::npos) return;
    std::string sec = j.substr(ob, cb - ob + 1);
    int filled = 0;
    // v1.6.0 关键修复：缓存只提供「首屏显示用的数值」，**绝不能证明硬件当前可用**。
    // 原来命中缓存就置 ddcOk=true，导致 DDC 明明读不到、界面却显示「正常」并给出缓存值，
    // 用户以为能调，实际写入无效（真实案例：DP 链路 DDC 不稳，缓存一直掩盖故障）。
    // 现在：缓存值进 cache 供首屏展示，但 ddcOk 由后台深探测的真实读取结果决定。
    if (sec.find("\"capsOk\":true") != std::string::npos) { m->capsOk = true; m->capsFromCache = true; }
    // v1.5.3 关键修复：不再从缓存回填 capModel——系列键按系列共享（如 BNQ|XL 覆盖 XL2566K/XL2746K），
    // 换同系列显示器时缓存里的旧型号会顶掉当前显示器的真实型号（UI/详细信息全显示上一台的）。
    // capModel 一律以本机实时数据为准：caps 探测成功 → capModel；caps 拒答 → 回落 EDID 名。
    // { size_t p = sec.find("\"capModel\":\"");
    //   if (p != std::string::npos) { size_t s = p + 12, e = sec.find('"', s);
    //     if (e != std::string::npos) m->capModel = sec.substr(s, e - s); } }
    { size_t p = sec.find("\"supported\":[");
      if (p != std::string::npos) { size_t e = sec.find(']', p);
        std::string a = sec.substr(p + 13, e - p - 13);
        size_t pos = 0;
        while (pos < a.size()) {
            while (pos < a.size() && (a[pos] == ',' || a[pos] == ' ')) pos++;
            if (pos >= a.size()) break;
            m->supported.insert((int)strtoul(a.c_str() + pos, nullptr, 10));
            while (pos < a.size() && isdigit((unsigned char)a[pos])) pos++;
            filled++;
        } } }
    auto arrMap = [&](const char* name, std::map<int, std::vector<unsigned>>& dst) {
        std::string tag = std::string("\"") + name + "\":{";
        size_t p = sec.find(tag);
        if (p == std::string::npos) return;
        size_t ob2 = p + tag.size() - 1;
        size_t cb2 = JsonSectionEnd(sec, ob2);
        if (cb2 == std::string::npos) return;
        std::string body = sec.substr(ob2 + 1, cb2 - ob2 - 1);
        size_t pos = 0;
        while (pos < body.size()) {
            size_t q = body.find('"', pos);
            if (q == std::string::npos) break;
            size_t qe = body.find('"', q + 1);
            if (qe == std::string::npos) break;
            int code = (int)strtoul(body.c_str() + q + 1, nullptr, 10);
            size_t colon = body.find(':', qe);
            if (colon == std::string::npos) break;
            if (body[colon + 1] == '[') {
                size_t ae = body.find(']', colon);
                if (ae == std::string::npos) break;
                std::string a = body.substr(colon + 2, ae - colon - 2);
                std::vector<unsigned> vals;
                size_t pp = 0;
                while (pp < a.size()) {
                    while (pp < a.size() && (a[pp] == ',' || a[pp] == ' ')) pp++;
                    if (pp >= a.size()) break;
                    vals.push_back((unsigned)strtoul(a.c_str() + pp, nullptr, 10));
                    while (pp < a.size() && isdigit((unsigned char)a[pp])) pp++;
                }
                dst[code] = vals;
                pos = ae + 1;
            } else {
                dst[code] = std::vector<unsigned>{ (unsigned)strtoul(body.c_str() + colon + 1, nullptr, 10) };
                pos = colon + 1;
            }
            filled++;
        }
    };
    auto valMap = [&](const char* name, std::map<int, unsigned>& dst) {
        std::string tag = std::string("\"") + name + "\":{";
        size_t p = sec.find(tag);
        if (p == std::string::npos) return;
        size_t ob2 = p + tag.size() - 1;
        size_t cb2 = JsonSectionEnd(sec, ob2);
        if (cb2 == std::string::npos) return;
        std::string body = sec.substr(ob2 + 1, cb2 - ob2 - 1);
        size_t pos = 0;
        while (pos < body.size()) {
            size_t q = body.find('"', pos);
            if (q == std::string::npos) break;
            size_t qe = body.find('"', q + 1);
            if (qe == std::string::npos) break;
            int code = (int)strtoul(body.c_str() + q + 1, nullptr, 10);
            size_t colon = body.find(':', qe);
            if (colon == std::string::npos) break;
            dst[code] = (unsigned)strtoul(body.c_str() + colon + 1, nullptr, 10);
            filled++;
            pos = colon + 1;
        }
    };
    std::map<int, std::vector<unsigned>> allowed;
    std::map<int, unsigned> cmax, cvals;
    arrMap("allowed", allowed);
    valMap("max", cmax);
    valMap("vals", cvals);
    { std::lock_guard<std::recursive_mutex> il(m->infoMx);
      m->allowed = allowed;
      for (auto& kv : cmax) m->cacheMax[kv.first] = kv.second;
      for (auto& kv : cvals) m->cache[kv.first] = kv.second; }
}
static void SaveDdcCacheAll(const std::vector<MonitorDev*>& mons) {
    // v1.6.9：并发调用互斥（AfterCapsReady 后台补读落盘 vs StartProbe 探测落盘），
    // 整个「读-合并-写」过程串行化，避免后写覆盖先写丢更新
    std::lock_guard<std::mutex> lk(g_cacheSaveMx);
    // 累积合并：离线型号的缓存条目原样保留（换接显示器不丢，接回免重探），在线条目覆盖更新。
    // 旧键经 NormalizeAnyKey 归一；无法归一的丢弃。
    std::string old;
    ReadTextFile(DdcCachePath(), old);
    std::string fresh;
    std::set<std::string> freshKeys;
    bool first = true;
    auto addEntry = [&](const std::string& key, const std::string& body) {
        fresh += (first ? "" : ",") + std::string("\"") + key + "\":" + body;
        first = false;
        freshKeys.insert(key);
    };
    for (auto* m : mons) {
        std::string capModel; bool capsOk;
        std::set<int> supported;
        std::map<int, std::vector<unsigned>> allowed;
        std::map<int, unsigned> cmax, cvals;
        { std::lock_guard<std::recursive_mutex> il(m->infoMx);
          capsOk = m->capsOk; capModel = m->capModel;
          supported = m->supported; allowed = m->allowed;
          cmax = m->cacheMax; cvals = m->cache; }
        if (!capsOk && cvals.empty()) continue;
        std::string body = "{";
        body += std::string("\"capsOk\":") + (capsOk ? "true" : "false") + ",";
        body += "\"capModel\":\"" + capModel + "\",";
        body += "\"supported\":[";
        { bool f = true;
          for (int c : supported) { body += (f ? "" : ",") + std::to_string(c); f = false; }
          body += "],"; }
        body += "\"allowed\":{";
        { bool f = true;
          for (auto& kv : allowed) {
              body += (f ? "" : ",") + std::string("\"") + std::to_string(kv.first) + "\":[";
              f = false;
              bool g = true;
              for (unsigned v : kv.second) { body += (g ? "" : ",") + std::to_string(v); g = false; }
              body += "]";
          }
          body += "},"; }
        body += "\"max\":{";
        { bool f = true;
          for (auto& kv : cmax) { body += (f ? "" : ",") + std::string("\"") + std::to_string(kv.first) + "\":" + std::to_string(kv.second); f = false; }
          body += "},"; }
        body += "\"vals\":{";
        { bool f = true;
          for (auto& kv : cvals) { body += (f ? "" : ",") + std::string("\"") + std::to_string(kv.first) + "\":" + std::to_string(kv.second); f = false; }
          body += "}"; }
        body += "}";
        addEntry(MonitorKey(m), body);
    }
    // 旧文件：离线条目归一保留（NormalizeAnyKey：系列键原样/归一 ZOWIE 名，旧型号键映射，不可归一丢弃）
    std::string kept;
    std::set<std::string> keptSeen;
    size_t ob = old.find('{');
    if (ob != std::string::npos) {
        size_t pos = ob + 1;
        while (pos < old.size()) {
            while (pos < old.size() && (old[pos] == ',' || old[pos] == ' ')) pos++;
            if (pos >= old.size() || old[pos] == '}') break;
            if (old[pos] != '"') break;
            size_t ke = old.find('"', pos + 1);
            if (ke == std::string::npos) break;
            std::string rawKey = old.substr(pos + 1, ke - pos - 1);
            std::string key = NormalizeAnyKey(rawKey);
            size_t bodyOb = old.find('{', ke);
            if (bodyOb == std::string::npos) break;
            size_t bodyCb = JsonSectionEnd(old, bodyOb);
            if (bodyCb == std::string::npos) break;
            if (!key.empty() && !freshKeys.count(key) && !keptSeen.count(key)) {
                kept += (kept.empty() ? "" : ",") + std::string("\"") + key + "\":" +
                        old.substr(bodyOb, bodyCb - bodyOb + 1);
                keptSeen.insert(key);
            }
            pos = bodyCb + 1;
        }
    }
    std::string j = "{";
    if (!kept.empty()) { j += kept; if (!fresh.empty()) j += ","; }
    j += fresh;
    j += "}";
    WriteTextFile(DdcCachePath(), j);
}

static void LoadProfiles() {
    std::string json;
    char path[MAX_PATH];
    sprintf_s(path, "%s\\profiles.json", ExeDir().c_str());
    FILE* f = nullptr;
    f = FopenUtf8(path, "rb");
    if (!f) return;
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) json.append(buf, n);
    fclose(f);
    size_t i = 0;
    auto skipWs = [&]() { while (i < json.size() && (json[i] == ' ' || json[i] == '\n' || json[i] == '\r' || json[i] == '\t')) i++; };
    skipWs();
    if (i >= json.size() || json[i] != '{') return;
    i++;
    for (;;) {
        skipWs();
        if (i >= json.size() || json[i] == '}') break;
        if (json[i] != '"') break;
        size_t ke = json.find('"', i + 1);
        if (ke == std::string::npos) break;
        std::string key = json.substr(i + 1, ke - i - 1);
        i = ke + 1;
        skipWs(); if (i >= json.size() || json[i] != ':') break; i++;
        skipWs(); if (i >= json.size() || json[i] != '{') break; i++;
        Baseline bl;
        for (;;) {
            skipWs();
            if (i >= json.size() || json[i] == '}') { i++; break; }
            if (json[i] != '"') break;
            size_t ce = json.find('"', i + 1);
            if (ce == std::string::npos) break;
            std::string keyText = json.substr(i + 1, ce - i - 1);
            i = ce + 1;
            skipWs(); if (i < json.size() && json[i] == ':') i++;
            skipWs();
            // v1.5.3 快照来源身份（字符串键）
            if (keyText == "srcModel" || keyText == "srcSerial") {
                if (i < json.size() && json[i] == '"') {
                    size_t ve = json.find('"', i + 1);
                    if (ve == std::string::npos) break;
                    std::string sval = json.substr(i + 1, ve - i - 1);
                    if (keyText == "srcModel") bl.srcModel = sval; else bl.srcSerial = sval;
                    i = ve + 1;
                    skipWs(); if (i < json.size() && json[i] == ',') i++;
                    continue;
                }
            }
            unsigned val = (unsigned)strtoul(json.c_str() + i, nullptr, 10);
            bl.vals[atoi(keyText.c_str())] = val;
            while (i < json.size() && isdigit((unsigned char)json[i])) i++;
            skipWs(); if (i < json.size() && json[i] == ',') i++;
        }
        // v1.5.3 键归一：共用 NormalizeAnyKey——「型号|序列号」/纯型号 → 「VENDOR|系列」；
        // 已是新格式（VENDOR|系列）的原样保留并归一 ZOWIE 系列名；无法归一的丢弃。
        key = NormalizeAnyKey(key);
        if (!key.empty()) g_profiles[key] = bl;
        skipWs(); if (i < json.size() && json[i] == ',') i++;
    }
}
static void SaveProfiles() {
    std::string json = "{";
    bool first = true;
    for (auto& kv : g_profiles) {
        if (!first) json += ",";
        first = false;
        char buf[512];
        json += "\"" + kv.first + "\":{";
        // v1.5.3 快照来源身份（跨设备还原安全判定用）
        json += "\"srcModel\":\"" + kv.second.srcModel + "\",";
        json += "\"srcSerial\":\"" + kv.second.srcSerial + "\",";
        bool f2 = true;
        for (auto& v : kv.second.vals) {
            if (!f2) json += ",";
            f2 = false;
            sprintf_s(buf, "\"%d\":%u", v.first, v.second);
            json += buf;
        }
        json += "}";
    }
    json += "}";
    char path[MAX_PATH];
    sprintf_s(path, "%s\\profiles.json", ExeDir().c_str());
    FILE* f = nullptr;
    f = FopenUtf8(path, "wb");
    if (f) { fwrite(json.data(), 1, json.size(), f); fclose(f); }
}
// 极简 config.json 读取（键值对，兼容 C# JavaScriptSerializer 输出）
static void LoadConfig() {
    char path[MAX_PATH];
    sprintf_s(path, "%s\\config.json", ExeDir().c_str());
    FILE* f = nullptr;
    f = FopenUtf8(path, "rb");
    if (!f) return;
    std::string json;
    char buf[4096]; size_t n;
    while ((n = fread(buf, 1, sizeof(buf), f)) > 0) json.append(buf, n);
    fclose(f);
    auto findNum = [&](const char* key, int def) -> int {
        std::string k = std::string("\"") + key + "\"";
        size_t p = json.find(k);
        if (p == std::string::npos) return def;
        p = json.find(':', p + k.size());
        if (p == std::string::npos) return def;
        return atoi(json.c_str() + p + 1);
    };
    auto findStr = [&](const char* key, const char* def) -> std::string {
        std::string k = std::string("\"") + key + "\"";
        size_t p = json.find(k);
        if (p == std::string::npos) return def;
        p = json.find(':', p + k.size());
        if (p == std::string::npos) return def;
        p = json.find('"', p + 1);
        if (p == std::string::npos) return def;
        size_t e = json.find('"', p + 1);
        if (e == std::string::npos) return def;
        return json.substr(p + 1, e - p - 1);
    };
    auto findBool = [&](const char* key, bool def) -> bool {
        std::string k = std::string("\"") + key + "\"";
        size_t p = json.find(k);
        if (p == std::string::npos) return def;
        p = json.find(':', p + k.size());
        if (p == std::string::npos) return def;
        p = json.find_first_not_of(" \t", p + 1);
        if (p == std::string::npos) return def;
        if (!strncmp(json.c_str() + p, "true", 4)) return true;
        if (!strncmp(json.c_str() + p, "false", 5)) return false;
        return atoi(json.c_str() + p) != 0;
    };
    g_config.autoRestoreMinutes = findNum("AutoRestoreMinutes", 0);
    g_config.restoreOnStart     = findBool("RestoreOnStart", false);
    g_config.restoreOnExit      = findBool("RestoreOnExit", false);
    g_config.autoStart          = findBool("AutoStart", false);
    // v1.6.6：默认 true——网吧场景顾客点「关闭」时若真退出，正在跑的还原则会被中断。
    // 默认缩到托盘让还原继续完成；网管要真退出走托盘菜单「退出」或设 MinimizeToTray=false。
    g_config.minimizeToTray     = findBool("MinimizeToTray", true);
    g_config.skin               = findStr("Skin", "auto");
    g_config.accentMode         = findStr("AccentMode", "auto");
    g_config.manualAccent       = findStr("ManualAccent", "");
    g_config.updateBase         = findStr("UpdateBase", "");
    g_config.updateChannel      = findStr("UpdateChannel", "latest");
    g_config.checkUpdateOnStart = findBool("CheckUpdateOnStart", true);
    g_config.updateCheckHours   = findNum("UpdateCheckHours", 6);
    g_config.debugBrand         = (int)findNum("DebugBrand", 0);
    g_debugBrand                = g_config.debugBrand;
    g_config.titlePrefix        = findStr("TitlePrefix", "");
}

// 显示名：TitlePrefix 非空时「前置-显示器控制」，否则「显示器控制」
static std::string AppDisplayName() {
    return g_config.titlePrefix.empty() ? "显示器控制" : g_config.titlePrefix + "-显示器控制";
}
// 写回 config.json（C# JavaScriptSerializer 字段名兼容：AutoRestoreMinutes/RestoreOnStart/…）
static void SaveConfig() {
    std::string j = "{";
    char buf[128];
    sprintf_s(buf, "\"AutoRestoreMinutes\":%d,", g_config.autoRestoreMinutes); j += buf;
    j += std::string("\"RestoreOnStart\":") + (g_config.restoreOnStart ? "true" : "false") + ",";
    j += std::string("\"RestoreOnExit\":")  + (g_config.restoreOnExit  ? "true" : "false") + ",";
    j += std::string("\"AutoStart\":")      + (g_config.autoStart      ? "true" : "false") + ",";
    j += std::string("\"MinimizeToTray\":") + (g_config.minimizeToTray ? "true" : "false") + ",";
    j += "\"Skin\":\"" + g_config.skin + "\",";
    j += "\"AccentMode\":\"" + g_config.accentMode + "\",";
    j += "\"ManualAccent\":\"" + g_config.manualAccent + "\",";
    j += "\"UpdateBase\":\"" + g_config.updateBase + "\",";
    j += "\"UpdateChannel\":\"" + g_config.updateChannel + "\",";
    j += std::string("\"CheckUpdateOnStart\":") + (g_config.checkUpdateOnStart ? "true" : "false") + ",";
    sprintf_s(buf, "\"UpdateCheckHours\":%d,", g_config.updateCheckHours); j += buf;
    j += "\"TitlePrefix\":\"" + g_config.titlePrefix + "\",";
    sprintf_s(buf, "\"DebugBrand\":%d}", g_config.debugBrand); j += buf;
    char path[MAX_PATH];
    sprintf_s(path, "%s\\config.json", ExeDir().c_str());
    WriteTextFile(path, j);
}
// 开机自启：HKCU\...\Run 增删 MonitorOSD 项（不需要管理员权限）
static void ApplyAutoStart() {
    HKEY k;
    const wchar_t* run = L"Software\\Microsoft\\Windows\\CurrentVersion\\Run";
    if (RegOpenKeyExW(HKEY_CURRENT_USER, run, 0, KEY_SET_VALUE, &k) != ERROR_SUCCESS) return;
    if (g_config.autoStart) {
        wchar_t exe[MAX_PATH] = { 0 };
        GetModuleFileNameW(nullptr, exe, MAX_PATH);
        RegSetValueExW(k, L"MonitorOSD", 0, REG_SZ, (BYTE*)exe, (DWORD)(wcslen(exe) + 1) * 2);
    } else {
        RegDeleteValueW(k, L"MonitorOSD");
    }
    RegCloseKey(k);
}
static void LogLine(const std::string& msg) {
    char path[MAX_PATH];
    sprintf_s(path, "%s\\log.txt", ExeDir().c_str());
    // v1.5.3 P2-8：日志轮转。原实现只追加不限制，网吧 7×24 常开 + 每次调参都写，
    // 单机一个月能到几十 MB，还会拖慢无盘/还原卡环境的写入。超过 2 MB 就滚成 log.1.txt。
    {
        static long long s_nextCheck = 0;
        LARGE_INTEGER sz = {};
        if (GetFileAttributesA(path) != INVALID_FILE_ATTRIBUTES) {
            WIN32_FILE_ATTRIBUTE_DATA fad = {};
            if (GetFileAttributesExA(path, GetFileExInfoStandard, &fad)) {
                sz.HighPart = (LONG)fad.nFileSizeHigh;
                sz.LowPart  = fad.nFileSizeLow;
            }
        }
        long long total = sz.QuadPart;
        if (total > s_nextCheck) {
            if (total > (long long)2 * 1024 * 1024) {
                std::string old = std::string(path) + ".1";
                DeleteFileW(Utf8ToWide(old).c_str());
                MoveFileW(Utf8ToWide(path).c_str(), Utf8ToWide(old).c_str());
                s_nextCheck = 0;
            } else {
                s_nextCheck = total + ((long long)256 * 1024);   // 每涨 256 KB 才重新取一次大小，避免每次 stat
            }
        }
    }
    FILE* f = nullptr;
    f = FopenUtf8(path, "ab");
    if (f) {
        SYSTEMTIME st; GetLocalTime(&st);
        fprintf(f, "%04d-%02d-%02d %02d:%02d:%02d  %s\r\n",
                st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond, msg.c_str());
        fclose(f);
    }
}

// ================= EDID / 枚举 / 品牌 =================
static std::string GetEdidString(const BYTE* e, int len, BYTE type) {
    for (int off = 54; off + 18 <= len; off += 18) {
        if (e[off] != 0 || e[off + 1] != 0) continue;
        if (e[off + 3] == type) {
            std::string s;
            for (int i = 5; i < 18; i++) {
                char c = (char)e[off + i];
                if (c == 0 || c == '\n') break;
                if (c >= 32 && c < 127) s += c;
            }
            return s;
        }
    }
    return "";
}
struct EdidRec {
    std::string model, serial, vendor;
    int week = -1, year = -1, verMaj = -1, verMin = -1, cmW = -1, cmH = -1;
};
static std::vector<EdidRec> LoadAllEdids() {
    std::vector<EdidRec> out;
    HKEY root;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "SYSTEM\\CurrentControlSet\\Enum\\DISPLAY", 0, KEY_READ, &root) != ERROR_SUCCESS) return out;
    DWORD i = 0;
    char dev[256]; DWORD devLen = sizeof(dev);
    while (RegEnumKeyExA(root, i++, dev, &devLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
        devLen = sizeof(dev);
        HKEY dk;
        if (RegOpenKeyExA(root, dev, 0, KEY_READ, &dk) == ERROR_SUCCESS) {
            DWORD j = 0;
            char inst[256]; DWORD instLen = sizeof(inst);
            while (RegEnumKeyExA(dk, j++, inst, &instLen, nullptr, nullptr, nullptr, nullptr) == ERROR_SUCCESS) {
                instLen = sizeof(inst);
                char sub[512];
                sprintf_s(sub, "%s\\Device Parameters", inst);
                HKEY dp;
                if (RegOpenKeyExA(dk, sub, 0, KEY_READ, &dp) == ERROR_SUCCESS) {
                    BYTE edid[512]; DWORD sz = sizeof(edid), tp = REG_BINARY;
                    if (RegQueryValueExA(dp, "EDID", nullptr, &tp, edid, &sz) == ERROR_SUCCESS && sz >= 128) {
                        EdidRec r;
                        r.model  = GetEdidString(edid, (int)sz, 0xFC);
                        r.serial = GetEdidString(edid, (int)sz, 0xFF);
                        // 与 C# Edid.Parse 一致：厂商码取注册表目名前 3 位；
                        // 无序列号描述符时回退数字序列号；无型号时回退 EDID+ProductCode
                        r.vendor = dev;
                        if (r.vendor.size() > 3) r.vendor = r.vendor.substr(0, 3);
                        if (r.serial.empty()) {
                            unsigned sn = (unsigned)edid[12] | ((unsigned)edid[13] << 8) |
                                          ((unsigned)edid[14] << 16) | ((unsigned)edid[15] << 24);
                            r.serial = std::to_string(sn);
                        }
                        if (r.model.empty()) {
                            char fb[16];
                            sprintf_s(fb, "EDID%02X%02X", edid[11], edid[10]);
                            r.model = fb;
                        }
                        r.week  = edid[16];  // EDID 基块：16=周，17=年-1990，18/19=版本，21/22=宽高cm
                        r.year  = 1990 + edid[17];
                        r.verMaj = edid[18]; r.verMin = edid[19];
                        r.cmW = edid[21]; r.cmH = edid[22];
                        out.push_back(r);
                    }
                    RegCloseKey(dp);
                }
            }
            RegCloseKey(dk);
        }
    }
    RegCloseKey(root);
    return out;
}
// 用显示设备接口名精确定位当前显示器的注册表 EDID（DisplayID: DISPLAY#BNQxxxx#实例#{guid}）
// 模糊按名字匹配会在多台同型号机器上拿错序列号，这里走实例路径，一一对应。
// v1.5.3：WMI WmiMonitorID 采集（对齐 PassMark 第三信息源）——
// 作用：①注册表 EDID 读不到时的身份兜底（活动实例连接时 WMI 由驱动实时填充）；
//      ②ManufacturerName 给出制造商全名（替代 PNP 厂商 CSV 库）。
// 每次 StartProbe 调一次，结果缓存 g_wmiMon（inst 小写 → {友好名, 序列号, 制造商全名}）。
static std::map<std::string, std::tuple<std::string, std::string, std::string>> g_wmiMon;
static std::string U16ArrToStdString(const VARIANT& vIn) {
    std::string out;
    // 经典坑①：WmiMonitorID 的 MOF 声明 Uint16Array，WMI 实际交付 VT_I2（有符号）
    // 经典坑②：Get 返回的是 VT_BYREF|VT_ARRAY|VT_I2（数组按引用交付）——必须 VariantCopyInd 解引用
    VARIANT ind; VariantInit(&ind);
    const VARIANT* v = &vIn;
    if (vIn.vt & VT_BYREF) {
        if (FAILED(VariantCopyInd(&ind, (VARIANT*)&vIn))) return out;
        v = &ind;
    }
    bool ui2 = (v->vt == (VT_ARRAY | VT_UI2)), i2 = (v->vt == (VT_ARRAY | VT_I2)),
         i4 = (v->vt == (VT_ARRAY | VT_I4));   // 实测 WMI 把 Uint16Array 交付为 VT_I4 数组（元素 4 字节）
    if (ui2 || i2 || i4) {
        SAFEARRAY* sa = V_ARRAY(v);
        long lb = 0, ub = 0;
        if (SafeArrayGetLBound(sa, 1, &lb) == S_OK && SafeArrayGetUBound(sa, 1, &ub) == S_OK) {
            for (long i = lb; i <= ub; i++) {
                long el = 0;
                if (SafeArrayGetElement(sa, &i, &el) != S_OK) break;
                if (el == 0) break;   // 0 = 字符串结束
                wchar_t w[2] = { (wchar_t)el, 0 };
                out += WideToUtf8(w);
            }
        }
    }
    VariantClear(&ind);
    return out;
}
static void LoadWmiMonitorIDs() {
    g_wmiMon.clear();
    bool comInit = false;
    if (SUCCEEDED(CoInitializeEx(nullptr, COINIT_MULTITHREADED))) comInit = true;
    IWbemLocator* loc = nullptr;
    if (FAILED(CoCreateInstance(CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, IID_IWbemLocator, (void**)&loc))) {
        if (comInit) CoUninitialize();
        return;
    }
    IWbemServices* svc = nullptr;
    BSTR ns = SysAllocString(L"ROOT\\WMI");
    HRESULT hrc = loc->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc);
    if (hrc == WBEM_S_NO_ERROR) {
        HRESULT hr = CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHN_NONE, nullptr,
                                       RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
        if (FAILED(hr)) LogLine("WMI: CoSetProxyBlanket 失败 hr=" + std::to_string((long)hr));
        if (SUCCEEDED(hr)) {
            IEnumWbemClassObject* en = nullptr;
            BSTR q = SysAllocString(L"SELECT * FROM WmiMonitorID");
            BSTR ql = SysAllocString(L"WQL");
            // 同步枚举（后台线程无消息泵，半同步 RETURN_IMMEDIATELY 可能取不到对象）
            if (svc->ExecQuery(ql, q, 0 /* WBEM_FLAG_RETURN_WHEN_COMPLETE */, nullptr, &en) == WBEM_S_NO_ERROR) {
                for (;;) {
                    IWbemClassObject* obj = nullptr; ULONG got = 0;
                    HRESULT hn = en->Next(WBEM_INFINITE, 1, &obj, &got);
                    if (hn != WBEM_S_NO_ERROR || got == 0) {
                        if (hn != WBEM_S_FALSE && hn != S_OK) LogLine("WMI: Next 失败 hr=" + std::to_string((long)hn));
                        break;
                    }
                    VARIANT vInst, vName, vSerial, vMfr;
                    VariantInit(&vInst); VariantInit(&vName); VariantInit(&vSerial); VariantInit(&vMfr);
                    if (SUCCEEDED(obj->Get(L"InstanceName", 0, &vInst, nullptr, nullptr)) && vInst.vt == VT_BSTR) {
                        std::string inst = WideToUtf8(vInst.bstrVal);
                        for (auto& ch : inst) ch = (char)tolower((unsigned char)ch);
                        std::string friendly, serial, mfr;
                        HRESULT h1 = obj->Get(L"UserFriendlyName", 0, &vName, nullptr, nullptr);
                        HRESULT h2 = obj->Get(L"SerialNumberID", 0, &vSerial, nullptr, nullptr);
                        HRESULT h3 = obj->Get(L"ManufacturerName", 0, &vMfr, nullptr, nullptr);
                        friendly = U16ArrToStdString(vName);
                        serial = U16ArrToStdString(vSerial);
                        mfr = U16ArrToStdString(vMfr);
                        if (!friendly.empty() || !serial.empty() || !mfr.empty())
                            g_wmiMon[inst] = { friendly, serial, mfr };
                    }
                    VariantClear(&vInst); VariantClear(&vName); VariantClear(&vSerial); VariantClear(&vMfr);
                    obj->Release();
                }
                en->Release();
            }
            SysFreeString(q); SysFreeString(ql);
        } else {
            LogLine("WMI: ExecQuery 失败 hr=" + std::to_string((long)hr));
        }
        svc->Release();
    } else {
        LogLine("WMI: ConnectServer(ROOT\\WMI) 失败 hr=" + std::to_string((long)hrc));
    }
    SysFreeString(ns);
    loc->Release();
    if (comInit) CoUninitialize();
    LogLine("WMI: WmiMonitorID 采集 " + std::to_string(g_wmiMon.size()) + " 台");
}
// v1.5.3 制造商全名静态表（PNP ID → 公司名）：EDID 完整的机器不再依赖 WMI 查询（省 2-8 秒启动），
// WMI ManufacturerName 仍可用时优先（更新更准），此表兜底
static std::string MfrNameFromVendor(const std::string& v) {
    static const struct { const char* code; const char* name; } tbl[] = {
        { "AUS", "ASUSTeK Computer Inc." }, { "ACI", "ASUSTeK Computer Inc." },
        { "BNQ", "BenQ Corporation" },      { "DEL", "Dell Inc." },
        { "ACR", "Acer Incorporated" },     { "GSM", "LG Electronics" },
        { "SEC", "Samsung Electronics" },   { "MSI", "Micro-Star International" },
        { "HKC", "HKC Corporation" },       { "PHL", "Koninklijke Philips N.V." },
        { "HPN", "HP Inc." },               { "HWP", "HP Inc." },
        { "AOC", "AOC International" },     { "TPV", "TPV Technology" },
    };
    for (auto& e : tbl) if (v == e.code) return e.name;
    return "";
}
// 实例匹配：WMI InstanceName 形如 DISPLAY\AUS27F4\5&xxx&UID4357_0，包含注册表实例段即可
static bool WmiLookup(const std::string& instKey, std::string& friendly, std::string& serial, std::string& mfr) {
    std::string low = instKey;
    for (auto& ch : low) ch = (char)tolower((unsigned char)ch);
    for (auto& kv : g_wmiMon) {
        if (kv.first.find(low) != std::string::npos) {
            friendly = std::get<0>(kv.second); serial = std::get<1>(kv.second); mfr = std::get<2>(kv.second);
            return true;
        }
    }
    return false;
}
// v1.5.3：EDID 数据套用（SetupAPI/注册表双路径共用；instKey = 注册表实例路径 "厂商\5&xxx&UIDn"）
static void ApplyEdidToMonitor(MonitorDev* m, const BYTE* edid, DWORD sz, const std::string& instKey,
                               const std::string& productSubkey) {
    std::lock_guard<std::recursive_mutex> il(m->infoMx);
    std::string name   = GetEdidString(edid, (int)sz, 0xFC);
    std::string serial = GetEdidString(edid, (int)sz, 0xFF);
    size_t bs = instKey.find('\\');
    if (bs == std::string::npos) bs = instKey.size();
    m->vendor = instKey.substr(0, (std::min)(bs, (size_t)3));
    if (!name.empty()) m->edidModel = TrimStr(name);
    if (!serial.empty()) m->serial = serial;
    else {
        unsigned sn = (unsigned)edid[12] | ((unsigned)edid[13] << 8) |
                      ((unsigned)edid[14] << 16) | ((unsigned)edid[15] << 24);
        if (sn) m->serial = std::to_string(sn);
    }
    if (m->edidModel.empty()) {
        char fb[16];
        sprintf_s(fb, "EDID%02X%02X", edid[11], edid[10]);
        m->edidModel = fb;
    }
    m->edidWeek = edid[16];  // EDID 基块：16=周，17=年-1990，18/19=版本，21/22=宽高cm
    m->edidYear = 1990 + edid[17];
    m->edidVerMaj = edid[18]; m->edidVerMin = edid[19];
    m->edidCmW = edid[21]; m->edidCmH = edid[22];
    int realN = 128 * (1 + edid[126]);             // 基块 + 扩展块实际长度
    if (realN > (int)sz) realN = (int)sz;          // v1.5.3 闪退修复：按实际读到的字节数钳制
    if (realN > 512) realN = 512;                  // （原实现只钳 512，edid[126] 偏大时会把
    if (realN < 128) realN = (int)sz;              //  注册表缓冲区里的未初始化栈字节当成 EDID 解析）
    m->edidRaw.assign(edid, edid + realN);         // 保留完整 EDID（含扩展块）
    // v1.5.3：WMI 交叉补充（序列号缺失时用 WMI 补；制造商全名入档）
    // 制造商名先取静态表（EDID 完整时无需 WMI——启动提速的关键），WMI 有数据则覆盖
    m->manufacturerName = MfrNameFromVendor(m->vendor);
    {
        std::string wf, ws, wm;
        if (WmiLookup(instKey, wf, ws, wm)) {
            if (m->serial.empty() && !ws.empty()) m->serial = ws;
            if (!wm.empty()) m->manufacturerName = wm;
        }
    }
    // v1.5.3：枚举时刻冻结系列键（此时 capModel 尚空，DisplayNameOf 回落 EDID 名——
    // 来源稳定，保证跨启动的键一致；caps 探测后 capModel 到位不影响键）
    m->seriesKey = ModelKeyFrom(m->vendor, m->edidModel, productSubkey);
}
// v1.5.3：SetupAPI 设备树 EDID（行业标准方案，对齐 PassMark）——
// 遍历「活动的监视器接口」（GUID_DEVINTERFACE_MONITOR），按实例精确匹配，
// SetupDiOpenDevRegKey 读 EDID，CM_Get_DevNode_Status 检查设备节点异常。
// 相比直接拼注册表路径：设备节点由系统保证存在且当前有效，多适配器/重复实例边界更稳。
static bool LoadEdidViaSetupAPI(MonitorDev* m, const std::string& instA) {
    static const GUID kMonIf = { 0xe6f07b5f, 0xee97, 0x4a90, {0xb0,0x76,0x33,0xf5,0x7b,0xf4,0xea,0xa7} }; // GUID_DEVINTERFACE_MONITOR
    HDEVINFO h = SetupDiGetClassDevsW(&kMonIf, nullptr, nullptr, DIGCF_PRESENT | DIGCF_DEVICEINTERFACE);
    if (h == INVALID_HANDLE_VALUE) return false;
    bool ok = false;
    SP_DEVINFO_DATA did; did.cbSize = sizeof(did);
    for (DWORD i = 0; SetupDiEnumDeviceInfo(h, i, &did); i++) {
        wchar_t idw[512] = {};
        if (CM_Get_Device_IDW(did.DevInst, idw, 512, 0) != CR_SUCCESS) continue;
        std::string ida = WideToUtf8(idw);                       // 形如 DISPLAY\AUS27F4\5&xxx&UIDnnn
        const char* pfx = "DISPLAY\\";
        std::string idCore = (_strnicmp(ida.c_str(), pfx, 7) == 0) ? ida.substr(7) : ida;
        if (_stricmp(idCore.c_str(), instA.c_str()) != 0) continue;   // 实例精确匹配当前屏
        DWORD status = 0, prob = 0;
        if (CM_Get_DevNode_Status(&status, &prob, did.DevInst, 0) == CR_SUCCESS && prob != 0)
            LogLine("EDID: 设备节点异常 problem=" + std::to_string(prob));
        HKEY dk = SetupDiOpenDevRegKey(h, &did, DICS_FLAG_GLOBAL, 0, DIREG_DEV, KEY_READ);
        if (dk == INVALID_HANDLE_VALUE) continue;
        BYTE edid[512]; DWORD sz = sizeof(edid), tp = REG_BINARY;
        if (RegQueryValueExA(dk, "EDID", nullptr, &tp, edid, &sz) == ERROR_SUCCESS && sz >= 128) {
            ApplyEdidToMonitor(m, edid, sz, instA, instA.substr(0, instA.find('\\')));
            ok = true;
        }
        RegCloseKey(dk);
        break;
    }
    SetupDiDestroyDeviceInfoList(h);
    return ok;
}
static void LoadLiveEdid(MonitorDev* m) {
    if (m->screenDevice.empty()) return;
    DISPLAY_DEVICEW adapter = {}; adapter.cb = sizeof(adapter);
    for (int i = 0; EnumDisplayDevicesW(nullptr, i, &adapter, EDD_GET_DEVICE_INTERFACE_NAME); i++) {
        DISPLAY_DEVICEW mon = {}; mon.cb = sizeof(mon);
        for (int j = 0; EnumDisplayDevicesW(adapter.DeviceName, j, &mon, EDD_GET_DEVICE_INTERFACE_NAME); j++) {
            if (!(mon.StateFlags & DISPLAY_DEVICE_ACTIVE)) continue;
            char dnA[80];
            WideCharToMultiByte(CP_UTF8, 0, mon.DeviceName, -1, dnA, 80, nullptr, nullptr);
            if (_strnicmp(dnA, m->screenDevice.c_str(), m->screenDevice.size()) != 0) continue;
            std::wstring id = mon.DeviceID;
            size_t p1 = id.find(L"DISPLAY#");
            if (p1 == std::wstring::npos) continue;
            p1 += 8;
            size_t p2 = id.find(L"#{", p1);
            if (p2 == std::wstring::npos) continue;
            std::wstring instW = id.substr(p1, p2 - p1);
            std::replace(instW.begin(), instW.end(), L'#', L'\\');
            char inst[256];
            WideCharToMultiByte(CP_UTF8, 0, instW.c_str(), -1, inst, 256, nullptr, nullptr);
            // v1.5.3：SetupAPI 设备树优先（节点由系统保证当前有效）；失败回落注册表直读
            HKEY dp = nullptr;
            bool got = LoadEdidViaSetupAPI(m, inst);
            if (!got) {
                char sub[600];
                sprintf_s(sub, "SYSTEM\\CurrentControlSet\\Enum\\DISPLAY\\%s\\Device Parameters", inst);
                if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, sub, 0, KEY_READ, &dp) == ERROR_SUCCESS) {
                    BYTE edid[512]; DWORD sz = sizeof(edid), tp = REG_BINARY;
                    if (RegQueryValueExA(dp, "EDID", nullptr, &tp, edid, &sz) == ERROR_SUCCESS && sz >= 128) {
                        ApplyEdidToMonitor(m, edid, sz, inst, std::string(inst).substr(0, std::string(inst).find('\\')));
                        got = true;
                    }
                }
            }
            if (!got) {
                // v1.5.3 第三信息源：WMI 身份兜底（注册表 EDID 缺失的机器上也能拿身份）
                std::string wf, ws, wm;
                if (WmiLookup(inst, wf, ws, wm)) {
                    std::lock_guard<std::recursive_mutex> il(m->infoMx);
                    size_t bs = std::string(inst).find('\\');
                    if (bs == std::string::npos) bs = std::string(inst).size();
                    m->vendor = std::string(inst).substr(0, (std::min)(bs, (size_t)3));
                    if (!wf.empty()) m->edidModel = TrimStr(wf);
                    if (!ws.empty()) m->serial = ws;
                    m->manufacturerName = wm;
                    m->seriesKey = ModelKeyFrom(m->vendor, m->edidModel, "");
                    LogLine("EDID: 注册表缺失，身份来自 WMI（" + m->edidModel + "）");
                }
            }
            if (dp) RegCloseKey(dp);
            return;
        }
    }
}
static std::string DisplayNameOf(MonitorDev* m) {
    std::lock_guard<std::recursive_mutex> il(m->infoMx);   // capModel 可能正被后台 caps 探测写入
    std::string s = m->capModel;
    auto bad = [](const std::string& x) { return x.empty() || x == "UNK"; };
    if (bad(s)) s = m->edidModel;
    if (bad(s)) s = WideToUtf8(m->desc);
    if (!s.empty() && ToUpperAscii(s).find("GENERIC") != std::string::npos) s = m->edidModel;
    if (s.empty()) s = "显示器";
    return TrimStr(s);
}
// 词边界匹配：\bXL\s?\d{4}
static bool MatchXlModel(const std::string& n) {
    for (size_t i = 0; i + 1 < n.size(); i++) {
        if (n[i] != 'X' || n[i + 1] != 'L') continue;
        if (i > 0 && (isalnum((unsigned char)n[i - 1]) || n[i - 1] == '_')) continue;
        size_t j = i + 2;
        if (j < n.size() && n[j] == ' ') j++;
        int digits = 0;
        while (j < n.size() && isdigit((unsigned char)n[j])) { j++; digits++; }
        if (digits == 4) return true;
    }
    return false;
}
// 词边界匹配：\b(PG|VG|XG|PA|PB|MG|MB)\d{2}[A-Z]
static bool MatchAsusModel(const std::string& n) {
    static const char* pfx[] = { "PG","VG","XG","PA","PB","MG","MB" };
    for (auto p : pfx) {
        size_t len = strlen(p);
        for (size_t i = 0; i + len + 2 < n.size(); i++) {
            if (n.compare(i, len, p) != 0) continue;
            if (i > 0 && (isalnum((unsigned char)n[i - 1]) || n[i - 1] == '_')) continue;
            size_t j = i + len;
            if (!isdigit((unsigned char)n[j]) || !isdigit((unsigned char)n[j + 1])) continue;
            j += 2;
            if (j < n.size() && isupper((unsigned char)n[j])) return true;
        }
    }
    return false;
}
static int DetectBrand(MonitorDev* m) {
    std::string n = ToUpperAscii(DisplayNameOf(m) + " " + WideToUtf8(m->desc) + " " + m->capModel);
    if (m->vendor == "BNQ" || m->vendor == "BEN" || Contains(n, "ZOWIE") || Contains(n, "BENQ") || MatchXlModel(n)) return 1;
    if (m->vendor == "ACI" || m->vendor == "ASU" || m->vendor == "AUS" ||
        Contains(n, "ASUS") || Contains(n, "ROG ") || Contains(n, "TUF") || MatchAsusModel(n)) return 2;
    if (m->vendor == "AOC" || m->vendor == "PHL" || Contains(n, "AOC") || Contains(n, "PHILIPS") ||
        Contains(n, "AGON") || Contains(n, "HERO ")) return 3;
    if (m->vendor == "HKC" || Contains(n, "HKC") || Contains(n, "TITAN") || Contains(n, "ANTGAMER")) return 5;
    if (m->vendor == "MSI" || Contains(n, "MSI")) return 6;
    if (m->vendor == "GSM" || Contains(n, "LG ") || Contains(n, "ULTRAGEAR") || Contains(n, "LG电子")) return 7;
    if (m->vendor == "SEC" || Contains(n, "SAMSUNG")) return 8;
    if (m->vendor == "DEL" || Contains(n, "DELL")) return 9;
    if (m->vendor == "ACR" || Contains(n, "ACER") || Contains(n, "PREDATOR") || Contains(n, "NITRO")) return 10;
    if (m->vendor == "HWP" || m->vendor == "HPN" || Contains(n, "HEWLETT") || Contains(n, "HP ")) return 11;
    // v1.5.2 新品牌：外星人（Dell 子品牌，EDID 可能报 DEL* 或 AWR）/ 蚂蚁/泰坦（HKC 系）/ 华为 / 小米
    if (Contains(n, "ALIENWARE") || Contains(n, "ALIEN ") || Contains(n, "AW27") || Contains(n, "AW34") ||
        m->vendor == "AWR" || m->vendor == "AWL") return 12;
    if (m->vendor == "ANT" || Contains(n, "ANTGAMER") || Contains(n, "蚂蚁") ||
        Contains(n, "ANT27") || Contains(n, "ANT25") || Contains(n, "ANTG")) return 13;
    if (m->vendor == "TIT" || Contains(n, "TITAN ARMY") || Contains(n, "泰坦军团") ||
        Contains(n, "T27Q") || Contains(n, "P27A") || Contains(n, "T27")) return 14;
    if (m->vendor == "HUA" || m->vendor == "HAX" || Contains(n, "HUAWEI") || Contains(n, "华为") ||
        Contains(n, "MATEVIEW") || Contains(n, "ADANDSEN")) return 15;
    if (m->vendor == "XIA" || m->vendor == "MI " || Contains(n, "XIAOMI") || Contains(n, "小米") ||
        Contains(n, "REDMI") || Contains(n, "XMR")) return 16;
    return 0;
}

// ================= caps 动态菜单（v1.5：未知品牌/型号按探测结果自动成行）=================
// 规则：标准码有中文名按重要性排前，未知码进「高级」尾部；解析失败的行不进菜单（无置灰）。
static std::vector<Group> BuildCapsMenu(MonitorDev* m) {
    g_dynSpec.clear();
    static std::vector<std::string> s_idPool;   // mapId 生命周期与 g_menu 同步（静态池，不做清理）
    struct NameRec { int code; const char* cn; const char* group; int rank; bool isEnumPreset; };
    static const NameRec kNames[] = {
        { 0x10, "亮度",       "IMAGE", 1,  false },
        { 0x12, "对比度",     "IMAGE", 2,  false },
        { 0x87, "锐利度",     "IMAGE", 3,  false },
        { 0xDC, "显示模式",   "IMAGE", 4,  false },
        { 0xA8, "响应加速",   "IMAGE", 5,  false },
        { 0xF7, "响应时间",   "IMAGE", 6,  false },
        { 0xF9, "暗部稳定",   "IMAGE", 7,  false },
        { 0x14, "色温",       "COLOR", 1,  true  },
        { 0x72, "伽马",       "COLOR", 2,  true  },
        { 0x8A, "饱和度",     "COLOR", 3,  false },
        { 0x16, "用户红增益", "COLOR", 4,  false },
        { 0x18, "用户绿增益", "COLOR", 5,  false },
        { 0x1A, "用户蓝增益", "COLOR", 6,  false },
        { 0x6C, "黑电平红",   "COLOR", 7,  false },
        { 0x6E, "黑电平绿",   "COLOR", 8,  false },
        { 0x70, "黑电平蓝",   "COLOR", 9,  false },
        { 0xCA, "OSD 按键锁", "SYS",   1,  true  },
        { 0xD6, "电源模式",   "SYS",   2,  true  },
    };
    // 只读/内部/已按需求移除的码：不进动态菜单
    static const int kSkip[] = { 0x02,0x04,0x05,0x08,0x0C,0x52,0x54,0x60,0x62,0x8D,
                                 0xAC,0xAE,0xB2,0xB6,0xC0,0xC6,0xC8,0xC9,0xDF,0x73 };
    auto skipped = [&](int code) {
        for (int k : kSkip) if (k == code) return true;
        return false;
    };
    auto nameOf = [&](int code) -> const NameRec* {
        for (auto& n : kNames) if (n.code == code) return &n;
        return nullptr;
    };

    std::map<int, std::vector<unsigned>> allowed;
    std::map<int, unsigned> cmax;
    { std::lock_guard<std::recursive_mutex> il(m->infoMx);
      allowed = m->allowed;
      cmax = m->cacheMax; }

    // 三个固定组 + 高级尾部
    std::vector<Group> g;
    auto grp = [&](const char* en, const char* cn) -> Group& {
        g.push_back(Group{ en, cn, {} });
        return g.back();
    };
    auto leaf = [&](Group& G, const std::string& en, const std::string& cn, const std::string& mapId) {
        s_idPool.push_back(en); s_idPool.push_back(cn); s_idPool.push_back(mapId);
        G.items.push_back(Item{ s_idPool[s_idPool.size()-3].c_str(),
                                s_idPool[s_idPool.size()-2].c_str(),
                                s_idPool[s_idPool.size()-1].c_str(), nullptr });
    };
    auto act = [&](Group& G, const char* en, const char* cn, const char* action) {
        G.items.push_back(Item{ en, cn, nullptr, action });
    };

    // 收集可控行：{rank, code, groupId}
    struct Row { int rank; int code; const char* grp; const NameRec* rec; };
    std::vector<Row> rows;
    for (int code : m->supported) {
        if (skipped(code)) continue;
        const NameRec* rec = nameOf(code);
        char en[32];
        sprintf_s(en, "VCP 0x%02X", code);
        if (rec) rows.push_back({ rec->rank, code, rec->group, rec });
        else {
            rows.push_back({ 100 + code, code, "ADV", nullptr });
            (void)en;
        }
    }
    std::sort(rows.begin(), rows.end(), [](const Row& a, const Row& b) {
        if (strcmp(a.grp, b.grp) != 0) return strcmp(a.grp, b.grp) < 0;
        return a.rank < b.rank;
    });

    std::map<std::string, Group*> gmap;
    auto ensureGrp = [&](const char* en, const char* cn) -> Group& {
        auto it = gmap.find(en);
        if (it != gmap.end()) return *it->second;
        Group& G = grp(en, cn);
        gmap[en] = &g.back();
        return G;
    };
    auto spec = [&](const std::string& id, const ValSpec& s) { g_dynSpec[id] = s; };

    for (auto& r : rows) {
        char idbuf[16];
        sprintf_s(idbuf, "c%02X", r.code);
        std::string mapId = idbuf;
        std::string en = r.rec ? r.rec->cn : (std::string("VCP 0x") + [&] { char b[8]; sprintf_s(b, "%02X", r.code); return std::string(b); }());
        std::string cn = en;
        auto ai = allowed.find(r.code);
        const std::vector<unsigned>* av = ai != allowed.end() ? &ai->second : nullptr;
        bool range = av && av->size() == 2 && (*av)[1] > (*av)[0] && (int)((*av)[1] - (*av)[0]) > 2;
        if (r.rec && strcmp(r.rec->group, "SYS") == 0 && (r.code == 0xCA || r.code == 0xD6)) {
            // 系统动作类枚举：值未知时也先给安全默认档
            auto& G = ensureGrp("SYSTEM", "系统管理");
            if (r.code == 0xCA) {
                leaf(G, "OSD LOCK", "OSD 按键锁", mapId);
                spec(mapId, ValSpec{ r.code, 0, 100, 0, true, {0x01,0x02}, {"锁定","正常"} });
            } else {
                leaf(G, "POWER MODE", "电源模式", mapId);
                spec(mapId, ValSpec{ r.code, 0, 100, 0, true, {0x01,0x04}, {"开","关屏"} });
            }
            continue;
        }
        auto& G = ensureGrp(r.rec ? r.rec->group : "ADV",
                            r.rec ? (strcmp(r.rec->group, "IMAGE") == 0 ? "图像" :
                                     strcmp(r.rec->group, "COLOR") == 0 ? "色彩" :
                                     strcmp(r.rec->group, "SYS") == 0 ? "系统管理" : "高级")
                                  : "高级");
        if (av && !range && av->size() >= 2 && av->size() <= 8) {
            // 枚举：值序未校准 → 十进制标签（校准后由 vcpmap/码表接管）
            std::vector<unsigned> vals = *av;
            std::vector<std::string> labs;
            for (unsigned v : vals) labs.push_back(std::to_string(v));
            // 已知预设的码带语义标签
            if (r.code == 0x14) {
                static const std::pair<unsigned, const char*> ct[] = {
                    {0x03,"4000K"},{0x04,"5000K"},{0x05,"6500K"},{0x06,"7500K"},
                    {0x07,"8200K"},{0x08,"9300K"},{0x09,"10000K"},{0x0B,"用户"},{0x01,"sRGB"} };
                for (size_t i = 0; i < vals.size(); i++)
                    for (auto& pr : ct)
                        if (vals[i] == pr.first) labs[i] = pr.second;
            }
            if (r.code == 0x72)
                for (size_t i = 0; i < vals.size(); i++) {
                    char b[8]; sprintf_s(b, "%.1f", vals[i] / 100.0);
                    labs[i] = b;
                }
            leaf(G, en, cn, mapId);
            spec(mapId, ValSpec{ r.code, 0, 100, 0, true, vals, labs });
        } else {
            int hi = 100;
            auto mi = cmax.find(r.code);
            if (mi != cmax.end() && mi->second > 0 && mi->second <= 255) hi = (int)mi->second;
            else if (av && !av->empty()) hi = (int)*std::max_element(av->begin(), av->end());
            leaf(G, en, cn, mapId);
            spec(mapId, ValSpec{ r.code, 0, hi, 0, false, {}, {} });
        }
    }
    // 系统管理固定动作（恢复出厂/扫描器/校准）挂在最后
    {
        Group& s = ensureGrp("SYSTEM", "系统管理");
        act(s, "RESET ALL", "恢复出厂设置", "resetall");
        act(s, "VCP SCANNER", "VCP 扫描器", "scanner");
        act(s, "CALIBRATE", "差分校准向导", "calibrate");
    }
    // 过滤空组
    g.erase(std::remove_if(g.begin(), g.end(), [](const Group& G) { return G.items.empty(); }), g.end());
    return g;
}
// LoadCaps 前置声明（LoadCapsAsync 后台线程引用）
static void LoadCaps(MonitorDev* m);
// v1.6.9 前置声明：caps 就绪后的收尾（通知界面重建菜单 + 补读 caps 声明码）。
// 定义在 g_hwnd / g_probeGen / g_uiAlive 声明之后（此处尚不可见）。
static void AfterCapsReady(MonitorDev* m);
// v1.6.9：caps 查询改纯后台（原 LoadCapsBounded 带看门狗同步等待最多 6s，caps 慢的
// 机型会拖住探测线程，深探测被推迟 6s 才开始）。分离线程照常跑 LoadCaps，结果经
// infoMx 锁自行写入；就绪后由 AfterCapsReady 收尾（通知界面 + 补读声明码）。
// AddRef/Release 引用计数保留（v1.5.8 P1-4）：即使调用方随后 retire 该对象，
// LoadCaps 也不会踩到已释放内存。
static void LoadCapsAsync(MonitorDev* m) {
    m->AddRef();
    std::thread([m]() {
        LoadCaps(m);
        m->Release();
    }).detach();
}
static void LoadCaps(MonitorDev* m) {
    // DDC 读取只持 g_bus；解析在锁外做，结果短锁提交——infoMx 不跨 DDC I/O（否则 UI 布局取锁排队秒级卡顿）
    DWORD len = 0;
    { std::lock_guard<std::mutex> lk(g_bus);
      // v1.6.1：句柄 0 在本机是合法值，不能据此跳过（否则 caps 永远读不到 → caps=FAIL）
      if (m->retired.load() ||
          !GetCapabilitiesStringLength(m->handle, &len) || len == 0 || len > 65536) goto probe; }
    {
        std::vector<char> buf(len + 1, 0);
        bool ok;
        { std::lock_guard<std::mutex> lk(g_bus);
          ok = !m->retired.load() &&
               !!CapabilitiesRequestAndCapabilitiesReply(m->handle, (LPSTR)buf.data(), len); }
        if (!ok) goto probe;
        std::string caps = buf.data();
        std::string capModel;
        std::set<int> supported;
        std::map<int, std::vector<unsigned>> allowed;
        size_t p = caps.find("model(");
        if (p != std::string::npos) {
            size_t e = caps.find(')', p);
            if (e != std::string::npos) capModel = TrimStr(caps.substr(p + 6, e - p - 6));
        }
        size_t vp = caps.find("vcp(");
        if (vp != std::string::npos) {
            vp += 4;
            int depth = 1; size_t i = vp;
            for (; i < caps.size(); i++) {
                if (caps[i] == '(') depth++;
                else if (caps[i] == ')') { depth--; if (depth == 0) break; }
            }
            std::string vcp = caps.substr(vp, i - vp);
            size_t pos = 0;
            while (pos < vcp.size()) {
                while (pos < vcp.size() && vcp[pos] == ' ') pos++;
                if (pos >= vcp.size()) break;
                int code = (int)strtoul(vcp.c_str() + pos, nullptr, 16);
                while (pos < vcp.size() && isxdigit((unsigned char)vcp[pos])) pos++;
                supported.insert(code);
                if (pos < vcp.size() && vcp[pos] == '(') {
                    size_t e = vcp.find(')', pos);
                    if (e != std::string::npos) {
                        std::string inner = vcp.substr(pos + 1, e - pos - 1);
                        std::vector<unsigned> vals;
                        size_t p2 = 0;
                        while (p2 < inner.size()) {
                            while (p2 < inner.size() && inner[p2] == ' ') p2++;
                            if (p2 >= inner.size()) break;
                            vals.push_back((unsigned)strtoul(inner.c_str() + p2, nullptr, 16));
                            while (p2 < inner.size() && isxdigit((unsigned char)inner[p2])) p2++;
                        }
                        if (!vals.empty()) allowed[code] = vals;
                        pos = e + 1;
                    } else break;
                }
            }
        }
        {   // 短锁提交（微秒级），UI 任何时刻取锁不再被 DDC I/O 阻塞
            std::lock_guard<std::recursive_mutex> il(m->infoMx);
            m->capsOk = true;
            m->capModel = capModel;
            m->supported = std::move(supported);
            m->allowed = std::move(allowed);
        }
        // v1.6.9：caps 就绪 → 收尾（通知界面重建菜单 + 后台补读 caps 声明码）。
        // 原深探测流程靠 StartProbe 同步等待 caps，caps 纯后台化后由这里补齐。
        {
            std::string cm;
            size_t n = 0;
            { std::lock_guard<std::recursive_mutex> il(m->infoMx); cm = m->capModel; n = m->supported.size(); }
            LogLine(std::string("探测: caps 就绪（型号 ") + (cm.empty() ? "未知" : cm) +
                    "，声明 " + std::to_string(n) + " 码），后台补读中");
        }
        AfterCapsReady(m);
    }
probe:
    unsigned c, x;
    m->ddcOk = m->capsOk;
    for (int t = 0; t < 3 && !m->ddcOk; t++)   // 部分机型（如华硕）首读拒答，重试 3 次
        m->ddcOk = m->Read(0x10, c, x) || m->Read(0x12, c, x);
}
static bool g_enumWithCaps = true;
static std::vector<MonitorDev*> Enumerate(bool withCaps = true) {
    g_enumWithCaps = withCaps;
    std::vector<MonitorDev*> list;
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hMon, HDC, LPRECT, LPARAM lp) -> BOOL {
        auto* list = (std::vector<MonitorDev*>*)lp;
        DWORD n = 0;
        if (!GetNumberOfPhysicalMonitorsFromHMONITOR(hMon, &n) || n == 0) return TRUE;
        std::vector<PHYSICAL_MONITOR> pms(n);
        if (GetPhysicalMonitorsFromHMONITOR(hMon, n, pms.data())) {
            MONITORINFOEXW mi = {};
            mi.cbSize = sizeof(mi);
            bool hasInfo = !!GetMonitorInfoW(hMon, &mi);
            for (DWORD i = 0; i < n; i++) {
                // v1.6.1：**不要**用 hPhysicalMonitor == 0 判断「无效」——
                // 实测本机（RTX 5070 + DP）dxva2 就是返回 0，但 DDC 读写完全正常
                // （亮度 cur=100 max=100）。曾据此跳过空句柄，把唯一能用的显示器丢掉，
                // 导致整页「DDC/CI 不可用」。句柄为 0 是否可用只能由实际读取来判定。
                auto* m = new MonitorDev();
                m->handle = pms[i].hPhysicalMonitor;
                m->desc = pms[i].szPhysicalMonitorDescription;
                if (hasInfo) {
                    wchar_t dn[64];
                    wcscpy_s(dn, mi.szDevice);
                    char buf[64];
                    WideCharToMultiByte(CP_UTF8, 0, dn, -1, buf, 64, nullptr, nullptr);
                    m->screenDevice = buf;
                }
                if (g_enumWithCaps) LoadCaps(m);
                else {
                    // v1.5.3：快路径不做 DDC 存活读——网吧客户机 DDC 慢/被禁用/KVM 场景下单次读
                    // 可阻塞数秒，3×2 次重试会把枚举拖到十几秒，且缓存救不了（缓存在枚举后才载）。
                    // 存活判定交给后台深探测；有缓存时 LoadDdcCache 直接置 ddcOk=true，界面即时全量。
                    m->ddcOk = false;
                }
                LoadLiveEdid(m);   // 精确取本机 EDID（序列号/日期/尺寸/版本），品牌也依赖 vendor
                list->push_back(m);
            }
        }
        return TRUE;
    }, (LPARAM)&list);

    auto edids = LoadAllEdids();
    for (auto* m : list) {
        if (m->vendor.empty()) {   // 活 EDID 没拿到时才按名字模糊兜底（可能拿错同型号机器的序列号）
            std::string dn = ToUpperAscii(DisplayNameOf(m));
            for (auto& e : edids) {
                std::string mn = ToUpperAscii(TrimStr(e.model));
                if (mn.size() < 4) continue;
                if (Contains(dn, mn.c_str()) || (dn.size() >= 4 && Contains(mn, dn.c_str()))) {
                    m->edidModel = e.model;
                    m->serial = e.serial;
                    m->vendor = e.vendor;
                    m->edidWeek = e.week; m->edidYear = e.year;
                    m->edidVerMaj = e.verMaj; m->edidVerMin = e.verMin;
                    m->edidCmW = e.cmW; m->edidCmH = e.cmH;
                    break;
                }
            }
        }
        m->brand = DetectBrand(m);
        if (g_debugBrand) m->brand = g_debugBrand;   // 调试预览：强制品牌界面（v1.4）
    }
    // 排序：品牌优先（zowie>asus>aoc>generic），与 C# 一致
    std::stable_sort(list.begin(), list.end(), [](MonitorDev* a, MonitorDev* b) {
        int ra = a->brand == 1 ? 3 : a->brand == 2 ? 2 : a->brand == 3 ? 1 : 0;
        int rb = b->brand == 1 ? 3 : b->brand == 2 ? 2 : b->brand == 3 ? 1 : 0;
        return ra > rb;
    });
    return list;
}

// ================= 基准操作 =================
static void SendWakeInput() {
    // DDC 唤不动（整机节能/无信号）时，模拟一次鼠标微动让系统点亮显示输出，再走 DDC
    INPUT in[2] = {};
    in[0].type = INPUT_MOUSE; in[0].mi.dwFlags = MOUSEEVENTF_MOVE; in[0].mi.dx = 1;
    in[1].type = INPUT_MOUSE; in[1].mi.dwFlags = MOUSEEVENTF_MOVE; in[1].mi.dx = (DWORD)-1;
    SendInput(2, in, sizeof(INPUT));
}
static volatile LONG g_waking = 0;   // 唤醒序列互斥（DDC 写入+系统输入+多次重试，勿重叠）
static ULONGLONG g_lastWakeTick = 0;
static void WakeUpInner(MonitorDev* m) {
    unsigned c, x;
    if (m->Read(0x10, c, x)) return;
    for (int i = 0; i < 3; i++) {
        m->WriteNow(0xD6, 1);        // MCCS Power mode: 0x01=On
        Sleep(1200);
        m->WriteNow(0x10, 50);
        Sleep(800);
        if (m->Read(0x10, c, x)) return;
    }
    SendWakeInput();
    Sleep(2500);
    for (int i = 0; i < 2; i++) {
        m->WriteNow(0xD6, 1);
        Sleep(1000);
        if (m->Read(0x10, c, x)) { LogLine("唤醒成功（系统输入后）: " + MonitorKey(m)); return; }
        SendWakeInput();
        Sleep(1500);
    }
    LogLine("唤醒显示器失败: " + MonitorKey(m));
}
static void WakeUp(MonitorDev* m) {
    if (InterlockedCompareExchange(&g_waking, 1, 0) != 0) return;   // 上一轮未结束，跳过
    g_lastWakeTick = GetTickCount64();
    WakeUpInner(m);
    InterlockedExchange(&g_waking, 0);
}
static bool WriteWithRetry(MonitorDev* m, int code, unsigned val) {
    for (int i = 0; i < 3; i++) {
        if (m->WriteNow(code, val)) {
            Sleep(150);
            unsigned c, x;
            if (m->Read(code, c, x)) {
                if (c == val) return true;
                WakeUp(m);
                continue;
            }
            return true;
        }
        WakeUp(m);
    }
    return false;
}
// v1.5.3 型号键 → 系列键（型号段取首个数字前的前缀：AUS|XG27ACDNG → AUS|XG）
static std::string SeriesOfModelKey(const std::string& modelKey) {
    size_t bar = modelKey.find('|');
    if (bar == std::string::npos) return modelKey;
    std::string vendor = modelKey.substr(0, bar), model = modelKey.substr(bar + 1);
    size_t d = model.find_first_of("0123456789");
    if (d == std::string::npos || d == 0) return modelKey;
    std::string prefix = model.substr(0, d);
    // P1-6 修复：型号名以厂商码开头时（如 AUS27B3），数字前缀会算出 AUS|AUS——
    // 改用完整型号作系列，保证系列兜底能命中
    if (_strnicmp(prefix.c_str(), vendor.c_str(), vendor.size()) == 0) return modelKey;
    return vendor + "|" + prefix;
}
static int g_restoreSame = 0;   // v1.6.3：本次还原因「值已一致」跳过的项数（供上层区分「无需还原」与「写入全失败」）
static int RestoreOne(MonitorDev* m) {
    // v1.5.3 定版两级还原（用户规则）：
    // ① 型号键命中 = 同型号 → 全部恢复（厂商码语义一致，安全）
    // ② 未命中查系列键 = 不同型号 → 只写该型号「码表已审计」的标准码
    //    （实测教训：跨代厂商私有码盲写会卡死 scaler，跨型号必须收敛到已审计码集）
    std::string mk = MonitorKey(m);
    auto it = g_profiles.find(mk);
    bool modelLevel = it != g_profiles.end() && !it->second.vals.empty();
    if (!modelLevel) {
        auto it2 = g_profiles.find(SeriesOfModelKey(mk));
        if (it2 != g_profiles.end() && !it2->second.vals.empty()) { it = it2; }
        else return -1;
    }
    std::set<int> specCodes;
    if (!modelLevel) {
        auto sp = SpecOf(m->brand);
        if (sp) for (auto& sv : *sp) if (sv.second.code != 0) specCodes.insert(sv.second.code);
    }
    unsigned c, x;
    bool alive = m->Read(0x10, c, x);
    if (!alive) WakeUp(m);
    // v1.6.5 关键修复：**废弃「缓存对比跳过」**。缓存不代表显示器实际状态（用户按物理键调乱/
    // 固件静默丢写/恢复出厂后缓存全错），缓存==基准就跳过会让该写的码永远写不上——
    // 实测症状「显示全部恢复了但观感明显不对、点几次也恢复不彻底」即源于此。
    // 改为「读-写-验证」：逐码读实际值 → 不一致才写 → 写完回读确认 → 顺带把缓存同步成真实值。
    //   时间代价：50 码全漂移 ≈ 读50+写50+验证50×61ms ≈ 10~12s（有「正在还原」提示兜底）；
    //   已一致为主时 ≈ 3s 纯读，提示如实（本已一致 N 项）。
    ULONGLONG t0 = GetTickCount64();
    int n = 0, skipped = 0, same = 0, writeSeq = 0, unsupported = 0;
    // v1.6.3：caps 已读到时，先剔除「本机明确不支持」的码 —— 写它们必然失败，
    // 但每次仍要付 61ms 的 DDC 往返代价。实测基准里常有大量此类码（换机型/固件升级后遗留），
    // 在 40 项的还原里能省掉可观的无效往返。
    std::set<int> supportedCopy;
    // 只在 caps 经本次实测确认时启用过滤：capsFromCache 的 supported 可能来自另一台机器/旧固件，
    // 拿它剔除码有误杀风险（宁可多写 61ms，也不要把该还原的项悄悄丢掉）。
    if (m->capsOk && !m->capsFromCache) {
        std::lock_guard<std::recursive_mutex> il(m->infoMx);
        supportedCopy = m->supported;
    }
    const bool useSupportedFilter = m->capsOk && !m->capsFromCache;
    // 两遍写入：第一遍失败的码收集后重试一遍（实测还原 50+ 项串行写入偶发丢写，
    // 用户被迫"点几次才彻底"）。0xAE=刷新率模式与 0x60/0xD6 同列跳过——
    // 还原写它触发显示模式切换 → 显示器重枚举 → 剩余写入全断 + 重探转圈（实测三症状一条链）。
    std::vector<std::pair<int, unsigned>> todo, failed;
    for (auto& kv : it->second.vals) {
        // 0xFA=华硕厂商动态码（固件自变，写完即被改走，实测每次还原都"回弹"）
        if (kv.first == 0x60 || kv.first == 0xD6 || kv.first == 0xAE || kv.first == 0xFA) { skipped++; continue; }
        if (!modelLevel && !specCodes.count(kv.first)) { skipped++; continue; }
        if (useSupportedFilter && !supportedCopy.count(kv.first)) { unsupported++; continue; }   // 本机不支持
        todo.push_back(kv);
    }
    int unverified = 0;
    std::vector<int> adjusted;
    for (auto& t : todo) {
        unsigned curv = 0, mxv = 0;
        // ① 读实际值（顺带校准缓存）
        bool okr = m->Read(t.first, curv, mxv);
        { std::lock_guard<std::recursive_mutex> il(m->infoMx);
          if (okr) m->cache[t.first] = curv; else m->cache.erase(t.first); }
        if (okr && curv == t.second) { same++; continue; }   // 实际已一致（读缓存对比永远做不到这一步）
        // ② 写
        bool ok = alive ? m->WriteNow(t.first, t.second) : WriteWithRetry(m, t.first, t.second);
        if (ok && (++writeSeq % 10) == 0) Sleep(5);
        // ③ 回读验证：写成功但值没生效（固件静默丢写）→ 再补一次
        unsigned v2 = 0;
        if (ok && m->Read(t.first, v2, mxv) && v2 != t.second) {
            ok = alive ? m->WriteNow(t.first, t.second) : WriteWithRetry(m, t.first, t.second);
            if (ok && (++writeSeq % 10) == 0) Sleep(5);
        }
        if (ok) {
            { std::lock_guard<std::recursive_mutex> il(m->infoMx); m->cache[t.first] = t.second; }
            adjusted.push_back(t.first);
            if (okr) n++;              // 读通且写通且回读一致 → 确认「已调整」
            else unverified++;         // 读不到的码写入成功但结果未经验证
        } else unverified++;
    }
    if (unverified > 0)
        LogLine("还原 " + mk + "：" + std::to_string(unverified) + " 项写入后未能确认生效（DDC 繁忙/固件拒答）");
    {
        std::string lst;
        for (int c : adjusted) { char b[8]; sprintf_s(b, "0x%02X ", c); lst += b; }
        LogLine("还原 " + mk + " 调整码: " + (lst.empty() ? "(无)" : lst));
    }
    LogLine("还原 " + mk + (modelLevel ? "（同型号全量）" : "（跨型号，系列基准→标准码）") +
            " 调整 " + std::to_string(n) + " 项" +
            (same > 0 ? "，跳过无变化 " + std::to_string(same) + " 项" : "") +
            (skipped > 0 ? "，跳过厂商私有码 " + std::to_string(skipped) + " 项" : "") +
            (unsupported > 0 ? "，本机不支持 " + std::to_string(unsupported) + " 项" : "") +
            "，耗时 " + std::to_string(GetTickCount64() - t0) + "ms");
    // 记录「本来要写但已一致」的数量，供上层区分「无需还原」与「写入全失败」
    g_restoreSame += same;
    return n;
}
static std::atomic<bool> g_restoring{false};   // v1.6.6：还原进行中——暂停 timer3 轮询
static std::atomic<bool> g_restoreBusy{false}; // v1.6.6 修复 1：还原重入守卫（按钮/热键/自动还原共用）
// v1.6.6 修复 2/3：g_restoring 改 RAII——原实现「store(true); 调用; store(false)」，
// 若中间抛异常（vector bad_alloc 等）标志永远停在 true，3 秒轮询永久停摆，界面再不刷新。
struct RestoringGuard {
    RestoringGuard()  { g_restoring.store(true); }
    ~RestoringGuard() { g_restoring.store(false); }
    RestoringGuard(const RestoringGuard&) = delete;
    RestoringGuard& operator=(const RestoringGuard&) = delete;
};
static int RestoreAllInternalImpl() {
    std::vector<MonitorDev*> mons;
    bool owned = false;
    if (!g_mons.empty()) mons = g_mons;
    else { mons = Enumerate(); owned = true; }
    if (mons.empty()) return -2;                       // 未检测到显示器（与「无基准」明确区分）
    g_restoreSame = 0;
    // v1.6.2 关键修复：还原是耗时操作（50+ 码 × 数十毫秒，期间界面仍在跑 3 秒轮询与重探）。
    // 期间一旦发生重探，g_mons 被替换、旧对象被 Retire（3 秒后真正 delete），
    // 而这里拿的还是裸指针 → 写已释放对象 → 偶发闪退（用户反馈「点一键还原基准崩溃概率高」）。
    // 全部先 AddRef，结束统一 Release（手动配对，Guard 不可拷贝故不能放进 vector）。
    for (auto* m : mons) m->AddRef();
    int total = 0;
    bool miss = false;
    for (auto* m : mons) {
        if (m->IsRetired()) continue;                  // 已被新探测取代，跳过（对象本身仍存活）
        int n = RestoreOne(m);
        if (n > 0) { total += n; LogLine("还原 " + MonitorKey(m) + " " + std::to_string(n) + " 项"); }
        if (n < 0) miss = true;                        // 该屏没有基准档案
        if (owned) { m->Retire(); }   // v1.5.8 P1-4：延迟回收，等在途线程用完
    }
    for (auto* m : mons) m->Release();                 // 与上面的 AddRef 配对
    if (total == 0) return miss ? -1 : 0;              // -1 = 基准缺失；0 = 有基准但全部写入失败
    return total;
}
// v1.6.6：包装层——还原期间置 g_restoring，暂停 timer3 的 3 秒轮询。
// 还原走 RestoreOne 直写（不走写队列，hasPend 挡不住轮询），轮询与还原并发
// 抢 g_bus 会把固件渐变期的旧值/过渡值刷进缓存，UI 表现为「还原完成瞬间变回旧样子」。
static int RestoreAllInternal() {
    RestoringGuard rg;                      // RAII：任何提前 return / 异常都保证复位
    return RestoreAllInternalImpl();
}

// ================= UI 状态 =================
static HWND g_hwnd = nullptr;
static ID2D1Factory* g_d2f = nullptr;
static IDWriteFactory* g_dwf = nullptr;
static ID2D1HwndRenderTarget* g_rt = nullptr;
static IWICImagingFactory* g_wic = nullptr;
static float g_scale = 1.f;
static int g_W = 0, g_H = 0;
static MonitorDev* g_cur = nullptr;
static int g_page = 0;             // 0=参数设置 1=系统管理 2=关于（v1.6.8）
static std::atomic<bool> g_probing{true};   // v1.6.4：缓存富足时由探测线程直接放行主界面（跨线程写）
static bool g_deepDone = false;   // v1.5.9：深探测（caps+全码）是否真正跑完
// v1.5.8 P1-4：探测代数。每次 StartProbe 递增，完成信号带上启动时的代数；
// 界面收到时若代数已过期（期间又触发过一次重探）就丢弃，避免旧探测的
// WM_APP+6 去操作新一轮的对象（这是重探场景崩溃的直接原因之一）。
static std::atomic<int> g_probeGen{0};
static bool g_shown = false;    // 主窗口是否已显示（缓存命中→放行即显示；真慢→0.4s 后带动画显示）
static bool g_reallyExit = false;
// v1.6.2：后台线程向 UI 投递状态文本的安全入口。
// 原来各处直接 PostMessageW(g_hwnd, ..., new std::string(...))：
// 窗口已销毁时消息永远不会被处理，那块 new 出来的内存就泄漏了；g_hwnd 还可能已被复用。
// 统一在这里判存活，投递失败立即 delete，避免泄漏。
static std::atomic<bool> g_uiAlive{false};
static void PostStatus(const std::string& msg) {
    if (!g_uiAlive.load()) return;                       // 窗口已销毁：直接丢弃，别 new
    HWND h = g_hwnd;
    if (!h || !IsWindow(h)) return;
    auto* p = new std::string(msg);
    if (!PostMessageW(h, WM_APP + 1, 0, (LPARAM)p)) delete p;   // 投递失败立刻回收
}
#define BOOTMARK(tag) do { FILE* d2 = fopen("boot_dbg.txt", "a"); if (d2) { fprintf(d2, tag " @%llu ms\n", GetTickCount64() - s_t0); fclose(d2); } } while(0)
static ULONGLONG s_t0 = 0;   // 启动耗时诊断基点（临时）
#define BOOTMARK(tag) do { FILE* d2 = fopen("boot_dbg.txt", "a"); if (d2) { fprintf(d2, tag " @%llu ms\n", GetTickCount64() - s_t0); fclose(d2); } } while(0)
static std::string g_statusText = "正在检测显示器…";
static Col g_statusDot = Theme::Success;
static POINT g_mouse = { -1, -1 };
static bool g_mouseDown = false;
static RECT g_pressRect = { 0,0,0,0 };
static ULONGLONG g_lastChangeTick = 0;
static bool g_draggingSlider = false;
static Item* g_dragItem = nullptr;
static std::vector<Group> g_menu;
static std::vector<RECT> g_monRects;
static RECT g_tabP, g_tabS, g_tabA, g_btnSave, g_btnRestore, g_btnClose, g_btnMin;
static RECT g_aboutBtnUpd, g_aboutBtnLog, g_aboutBtnFb;   // v1.6.8 关于页按钮；v1.6.11 内联更新中心（主按钮/查看日志/反馈）
static RECT g_aboutLnkDir, g_aboutLnkLog, g_aboutLnkCopy; // v1.6.11 关于页三链接（打开目录/查看日志/复制版本信息）
static RECT g_btnDbg;                 // 标题栏「调试」按钮（品牌界面预览）
static int g_contentW = 800;
static ULONGLONG g_toastUntil = 0;
static HRGN g_roundRgn = nullptr;
static NOTIFYICONDATAW g_nid = {};
static HICON g_trayIcon = nullptr;

static float S(float v) { return v * g_scale; }
static int SI(int v) { return (int)(v * g_scale + 0.5f); }

static void Invalidate() { if (g_hwnd) InvalidateRect(g_hwnd, nullptr, FALSE); }

// v1.6.9：caps 后台就绪后的收尾（LoadCaps 成功解析 caps 后调用，与调用方同线程执行）。
//  1) 通知界面用新 caps 重建菜单（allowed 过滤生效）：带当前探测代数，期间若发生重探
//     gen 已变，WM_APP+6 处理端按代数校验丢弃，无副作用。
//  2) caps 声明码补读入缓存：caps 晚到时 ProbeMonitor（菜单码 ∪ 当时的 supported）已跑完，
//     supported 中非菜单码部分没读过——逐个补读写入 cache/cacheMax（约 35 码 × 61ms ≈ 2s，
//     纯后台无感知）。复用 MonitorDev::Read 现有封装（内部自持 g_bus 并按写后保护窗口写缓存），
//     不引入新锁序。补读完成后单机落盘（SaveDdcCacheAll 收集合，临时 vector{m} 传入）。
// LoadCaps 也被 CLI /detect 等路径调用：CLI 无窗口（g_hwnd 空）→ 只补读不通知，不影响 CLI。
static void AfterCapsReady(MonitorDev* m) {
    if (m->retired.load()) return;
    // 先通知界面用新 caps 重建菜单：菜单码值此时已在缓存（ProbeMonitor 先行），allowed
    // 过滤立即生效；声明码补读再慢也不拖界面。带当前探测代数，期间若发生重探 gen 已变，
    // WM_APP+6 处理端按代数校验丢弃，无副作用。
    if (g_hwnd && g_uiAlive.load())
        PostMessageW(g_hwnd, WM_APP + 6, (WPARAM)g_probeGen.load(), 0);
    // caps 晚到时 ProbeMonitor 已跑完，supported 中不在 cache 的码逐个补读
    std::vector<int> missing;
    { std::lock_guard<std::recursive_mutex> il(m->infoMx);
      for (int code : m->supported)
          if (m->cache.find(code) == m->cache.end()) missing.push_back(code); }
    if (!missing.empty()) {
        unsigned c = 0, mx = 0;
        int got = 0;
        for (int code : missing) {
            if (m->retired.load()) return;      // 补读中途退役：立即放弃（对象由 Release 配对负责）
            if (m->Read(code, c, mx)) got++;    // updateCache=true：命中即写入 cache/cacheMax
        }
        SaveDdcCacheAll(std::vector<MonitorDev*>{ m });
        LogLine("探测: caps 声明码补读完成（" + std::to_string(got) + "/" +
                std::to_string(missing.size()) + " 码）");
    }
}

// ================= 分辨率 / 刷新率（系统级显示模式，非 DDC）=================
struct ResOpt { int w, h; };
static std::vector<ResOpt> g_resOpts;      // 可用分辨率（按像素数降序）
static std::vector<int>    g_hzOpts;       // 当前分辨率下的可用刷新率（降序）
static int g_curResW = 0, g_curResH = 0, g_curHz = 0;
static RECT g_resRow = {}, g_hzRow = {};   // 显示器卡下方的两个下拉行
static RECT g_btnDetail = {};              // 选中卡内「详细信息」按钮
static int g_ddMode = 0;                   // 自绘下拉面板：0 关 / 1 分辨率 / 2 刷新率
static RECT g_ddCard = {};
static std::vector<RECT> g_ddItems;        // 可见项矩形
static std::vector<int>  g_ddIdx;          // 可见项对应选项下标
static int g_ddScroll = 0;                 // 首个可见项

static void RefreshAllValues(bool allowWake = true);
static void RestoreWorker();
static void LayoutAll();
static void LayoutSystem();

static void BuildDisplayOptions() {
    g_resOpts.clear(); g_hzOpts.clear();
    MonitorDev* m = g_cur;
    if (!m || m->screenDevice.empty()) return;
    std::wstring sd = Utf8ToWide(m->screenDevice);
    DEVMODEW cur = {}; cur.dmSize = sizeof(cur);
    if (!EnumDisplaySettingsW(sd.c_str(), ENUM_CURRENT_SETTINGS, &cur)) return;
    g_curResW = cur.dmPelsWidth; g_curResH = cur.dmPelsHeight; g_curHz = cur.dmDisplayFrequency;
    std::set<std::pair<int, int>> resSet;
    std::set<int> hzSet;
    DEVMODEW dm = {}; dm.dmSize = sizeof(dm);
    for (int i = 0; EnumDisplaySettingsW(sd.c_str(), i, &dm); i++) {
        if (dm.dmBitsPerPel != cur.dmBitsPerPel) continue;   // 只列当前色深的模式
        if (dm.dmPelsWidth < 800 || dm.dmPelsHeight < 600) continue;   // 下限 800×600
        resSet.insert({ (int)dm.dmPelsWidth, (int)dm.dmPelsHeight });
        if (dm.dmPelsWidth == cur.dmPelsWidth && dm.dmPelsHeight == cur.dmPelsHeight)
            hzSet.insert((int)dm.dmDisplayFrequency);
    }
    g_resOpts.clear();
    for (auto& pr : resSet) g_resOpts.push_back(ResOpt{ pr.first, pr.second });
    std::sort(g_resOpts.begin(), g_resOpts.end(),
              [](const ResOpt& a, const ResOpt& b) { return (long long)a.w * a.h > (long long)b.w * b.h; });
    g_hzOpts.assign(hzSet.begin(), hzSet.end());
    std::sort(g_hzOpts.begin(), g_hzOpts.end(), std::greater<int>());
}
static bool ApplyDisplayMode(bool changeRes, int w, int h, int hz) {
    MonitorDev* m = g_cur;
    if (!m || m->screenDevice.empty()) return false;
    std::wstring sd = Utf8ToWide(m->screenDevice);
    DEVMODEW dm = {}; dm.dmSize = sizeof(dm);
    if (!EnumDisplaySettingsW(sd.c_str(), ENUM_CURRENT_SETTINGS, &dm)) return false;
    if (changeRes) {
        dm.dmPelsWidth = (DWORD)w; dm.dmPelsHeight = (DWORD)h;
        dm.dmFields |= DM_PELSWIDTH | DM_PELSHEIGHT;
        // 换分辨率默认配该模式可用的最高刷新率
        DEVMODEW it = {}; it.dmSize = sizeof(it);
        int bestHz = 0;
        for (int i = 0; EnumDisplaySettingsW(sd.c_str(), i, &it); i++)
            if (it.dmBitsPerPel == dm.dmBitsPerPel && it.dmPelsWidth == (DWORD)w && it.dmPelsHeight == (DWORD)h)
                bestHz = (std::max)(bestHz, (int)it.dmDisplayFrequency);
        if (bestHz > 0) { dm.dmDisplayFrequency = (DWORD)bestHz; dm.dmFields |= DM_DISPLAYFREQUENCY; }
    } else {
        dm.dmDisplayFrequency = (DWORD)hz;
        dm.dmFields |= DM_DISPLAYFREQUENCY;
    }
    LONG r = ChangeDisplaySettingsExW(sd.c_str(), &dm, nullptr, 0, nullptr);
    if (r == DISP_CHANGE_SUCCESSFUL) {
        char b[128];
        sprintf_s(b, "已切换到 %ux%u @ %u Hz", dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency);
        LogLine(std::string("显示模式: ") + b);
        ShowToast(b);
        BuildDisplayOptions();
        Invalidate();
        return true;
    }
    ShowToast("设置失败：该显示模式不被支持");
    return false;
}
static void RefreshAllValues(bool allowWake);   // 前向声明
static void RestoreWorker();
static void LayoutAll();
static void LayoutSystem();
// 在线更新 / 反馈（v1.3）——定义见 SysClick 之前
HWND g_upd_hwnd = nullptr;
CRITICAL_SECTION g_netCs;
extern const char* const kAppVersion;       // 定义见模块处
extern const char* const kDefaultUpdateBase;
enum { UPD_IDLE = 0, UPD_CHECKING, UPD_AVAILABLE, UPD_DOWNLOADING, UPD_READY, UPD_ERROR };
struct UpdateInfo {
    std::string version, url, sha256;
    long long   size = 0;
    bool        force = false;
    std::vector<std::string> notes;
};
struct UpdateUi {
    int         state = UPD_IDLE;
    std::string msg = "点击检查更新，或等待后台自动检查";
    int         progress = 0;
    UpdateInfo  info;
    std::string newPath;
    std::string feedbackState;
};
extern UpdateUi g_upd;
static void OpenUpdateWindow();   // v1.6.12：弹窗精简为纯「意见反馈」窗口（更新中心/关于已内联主窗口「关于」页签）
static void CheckUpdateAsync(bool userTriggered);
static void SubmitFeedbackAsync(const std::string& text, int type, bool withLog);
static void OpenToolsWindow(int tab);

// ================= 字体（自定义 DWrite 字体集合：Fonts\*.ttf，与 C# 嵌入子集同族名）=================
static IDWriteFontCollection* g_fontColl = nullptr;
static bool g_fontUiOk = false, g_fontMonoOk = false;
static std::vector<IDWriteFontFile*> g_fontFiles;

class FontFileEnum : public IDWriteFontFileEnumerator {
public:
    FontFileEnum(std::vector<IDWriteFontFile*>* files) : m_files(files), m_idx(0) {}
    STDMETHODIMP QueryInterface(REFIID riid, void** pp) override {
        if (riid == __uuidof(IDWriteFontFileEnumerator) || riid == __uuidof(IUnknown))
        { *pp = this; AddRef(); return S_OK; }
        *pp = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_ref; }
    STDMETHODIMP_(ULONG) Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }
    STDMETHODIMP MoveNext(BOOL* hasCurrentFile) override { m_idx++; *hasCurrentFile = (m_idx < m_files->size()); return S_OK; }
    STDMETHODIMP GetCurrentFontFile(IDWriteFontFile** fontFile) override {
        if (m_idx >= m_files->size()) return E_FAIL;
        *fontFile = (*m_files)[m_idx]; (*fontFile)->AddRef(); return S_OK;
    }
private:
    std::vector<IDWriteFontFile*>* m_files; size_t m_idx; ULONG m_ref = 1;
};
class CollLoader : public IDWriteFontCollectionLoader {
public:
    STDMETHODIMP QueryInterface(REFIID riid, void** pp) override {
        if (riid == __uuidof(IDWriteFontCollectionLoader) || riid == __uuidof(IUnknown))
        { *pp = this; AddRef(); return S_OK; }
        *pp = nullptr; return E_NOINTERFACE;
    }
    STDMETHODIMP_(ULONG) AddRef() override { return ++m_ref; }
    STDMETHODIMP_(ULONG) Release() override { ULONG r = --m_ref; if (!r) delete this; return r; }
    STDMETHODIMP CreateEnumeratorFromKey(IDWriteFactory*, const void*, UINT32, IDWriteFontFileEnumerator** e) override {
        *e = new FontFileEnum(&g_fontFiles); return S_OK;
    }
private:
    ULONG m_ref = 1;
};
static CollLoader* g_collLoader = nullptr;
static void LoadFonts() {
    const char* files[] = { "Fonts\\NotoSC-Regular.ttf", "Fonts\\NotoSC-Bold.ttf",
                            "Fonts\\JetBrainsMono-Regular.ttf", "Fonts\\JetBrainsMono-Bold.ttf" };
    for (auto f : files) {
        std::wstring p = Utf8ToWide(ResolvePath(f));
        if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        IDWriteFontFile* ff = nullptr;
        if (SUCCEEDED(g_dwf->CreateFontFileReference(p.c_str(), nullptr, &ff)) && ff) g_fontFiles.push_back(ff);
    }
    if (g_fontFiles.empty()) {   // 回落系统字体（与 C# 一致）
        LogLine("字体: 未加载到任何自定义字体，界面使用系统雅黑（字号/字宽会与设计规范有偏差）");
        return;
    }
    g_collLoader = new CollLoader();
    if (FAILED(g_dwf->RegisterFontCollectionLoader(g_collLoader))) return;
    if (FAILED(g_dwf->CreateCustomFontCollection(g_collLoader, nullptr, 0, &g_fontColl))) return;
    UINT32 idx = 0; BOOL exists = FALSE;
    if (SUCCEEDED(g_fontColl->FindFamilyName(L"NotoSC", &idx, &exists))) g_fontUiOk = !!exists;
    idx = 0; exists = FALSE;
    if (SUCCEEDED(g_fontColl->FindFamilyName(L"JBMonoOSD", &idx, &exists))) g_fontMonoOk = !!exists;
    FILE* dbg = fopen("boot_dbg.txt", "a");
    if (dbg) {
        fprintf(dbg, "fonts: files=%zu ui=%d mono=%d\n", g_fontFiles.size(), (int)g_fontUiOk, (int)g_fontMonoOk);
        fclose(dbg);
    }
}

static IDWriteTextFormat* MakeFmt(const wchar_t* fam, float px, bool bold,
                                  DWRITE_TEXT_ALIGNMENT ha, DWRITE_PARAGRAPH_ALIGNMENT va) {
    IDWriteTextFormat* f = nullptr;
    if (FAILED(g_dwf->CreateTextFormat(fam, g_fontColl, bold ? DWRITE_FONT_WEIGHT_BOLD : DWRITE_FONT_WEIGHT_NORMAL,
                                       DWRITE_FONT_STYLE_NORMAL, DWRITE_FONT_STRETCH_NORMAL, px, L"zh-CN", &f)))
        return nullptr;
    f->SetTextAlignment(ha);
    f->SetParagraphAlignment(va);
    f->SetWordWrapping(DWRITE_WORD_WRAPPING_NO_WRAP);
    return f;
}
// px 为已缩放字号；对齐缺省 = 左/上（对应 C# TextRenderer 无 flags 的默认行为）
static IDWriteTextFormat* FUi(float px, bool bold = false,
                              DWRITE_TEXT_ALIGNMENT ha = DWRITE_TEXT_ALIGNMENT_LEADING,
                              DWRITE_PARAGRAPH_ALIGNMENT va = DWRITE_PARAGRAPH_ALIGNMENT_NEAR) {
    static std::map<std::string, IDWriteTextFormat*> cache;
    char key[80]; sprintf_s(key, "ui|%.2f|%d|%d|%d", px, (int)bold, (int)ha, (int)va);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    IDWriteTextFormat* f = MakeFmt(g_fontUiOk ? L"NotoSC" : L"Microsoft YaHei UI", px, bold, ha, va);
    if (!f) f = MakeFmt(L"Microsoft YaHei UI", px, bold, ha, va);
    cache[key] = f;
    return f;
}
static IDWriteTextFormat* FMono(float px, bool bold = false,
                                DWRITE_TEXT_ALIGNMENT ha = DWRITE_TEXT_ALIGNMENT_LEADING,
                                DWRITE_PARAGRAPH_ALIGNMENT va = DWRITE_PARAGRAPH_ALIGNMENT_NEAR) {
    static std::map<std::string, IDWriteTextFormat*> cache;
    char key[80]; sprintf_s(key, "mo|%.2f|%d|%d|%d", px, (int)bold, (int)ha, (int)va);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    IDWriteTextFormat* f = MakeFmt(g_fontMonoOk ? L"JBMonoOSD" : L"Consolas", px, bold, ha, va);
    if (!f) f = MakeFmt(L"Consolas", px, bold, ha, va);
    cache[key] = f;
    return f;
}
static float MeasureW(const std::string& s, IDWriteTextFormat* f) {
    if (s.empty() || !f) return 0;
    std::wstring w = Utf8ToWide(s);
    IDWriteTextLayout* lay = nullptr;
    if (FAILED(g_dwf->CreateTextLayout(w.c_str(), (UINT32)w.size(), f, 10000.f, 10000.f, &lay))) return 0;
    DWRITE_TEXT_METRICS tm;
    lay->GetMetrics(&tm);
    lay->Release();
    return tm.width;
}

// ================= D2D 基础 =================
static void CreateDeviceResources() {
    if (g_rt) return;
    RECT rc; GetClientRect(g_hwnd, &rc);
    D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
    // 96 DPI：1 DIP = 1 px，布局坐标即物理像素（与 C# 像素绘制一致）
    // v1.6.6 修复 M7：原实现不查返回值——GPU 紧张/驱动重置时 g_rt 永久为 null，
    // 窗口全灰但进程活着，用户以为程序卡死。失败时记日志并 2 秒后重试（SetTimer id=8）。
    HRESULT hr = g_d2f->CreateHwndRenderTarget(D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                  D2D1::PixelFormat(), 96.f, 96.f),
                                  D2D1::HwndRenderTargetProperties(g_hwnd, size), &g_rt);
    if (FAILED(hr)) {
        g_rt = nullptr;
        LogLine("D2D: CreateHwndRenderTarget 失败 hr=0x" + [&] { char b[16]; sprintf_s(b, "%08X", (unsigned)hr); return std::string(b); }() + "，2 秒后重试");
        SetTimer(g_hwnd, 8, 2000, nullptr);
    }
}
static ID2D1SolidColorBrush* Brush(Col c) {
    ID2D1SolidColorBrush* b = nullptr;
    g_rt->CreateSolidColorBrush(c.F(), &b);
    return b;
}
static D2D1_ROUNDED_RECT RRect(RECT r, float radius) {
    D2D1_RECT_F rf = D2D1::RectF((float)r.left, (float)r.top, (float)r.right, (float)r.bottom);
    return D2D1::RoundedRect(rf, radius, radius);
}
static D2D1_RECT_F RectF(RECT r) { return D2D1::RectF((float)r.left, (float)r.top, (float)r.right, (float)r.bottom); }
static bool PtIn(const RECT& r, POINT p) { return p.x >= r.left && p.x < r.right && p.y >= r.top && p.y < r.bottom; }
static bool Hover(const RECT& r) { return PtIn(r, g_mouse); }
static bool Press(const RECT& r) {
    return g_mouseDown && g_pressRect.left == r.left && g_pressRect.right == r.right &&
           g_pressRect.top == r.top && g_pressRect.bottom == r.bottom;
}
static RECT InflateR(RECT r, int dx, int dy) { r.left -= dx; r.top -= dy; r.right += dx; r.bottom += dy; return r; }
static void FillRRect(RECT r, float rad, Col c) {
    ID2D1SolidColorBrush* b = Brush(c);
    g_rt->FillRoundedRectangle(RRect(r, rad), b);
    b->Release();
}
static void DrawT(const std::string& s, IDWriteTextFormat* f, const D2D1_RECT_F& rc, Col c) {
    if (s.empty() || !f) return;
    std::wstring w = Utf8ToWide(s);
    ID2D1SolidColorBrush* b = Brush(c);
    g_rt->DrawText(w.c_str(), (UINT32)w.size(), f, rc, b);
    b->Release();
}

// ================= Logo 图片（内嵌资源优先，img\*.png 磁盘兜底；缺失回落色块字母）=================
static ID2D1Bitmap* LoadLogoBitmap(const std::wstring& path) {
    if (!g_wic || !g_rt) return nullptr;
    IWICBitmapDecoder* dec = nullptr;
    if (FAILED(g_wic->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ,
                                                WICDecodeMetadataCacheOnDemand, &dec))) return nullptr;
    ID2D1Bitmap* bmp = nullptr;
    IWICBitmapFrameDecode* fr = nullptr;
    if (SUCCEEDED(dec->GetFrame(0, &fr))) {
        IWICFormatConverter* conv = nullptr;
        if (SUCCEEDED(g_wic->CreateFormatConverter(&conv))) {
            if (SUCCEEDED(conv->Initialize(fr, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                           nullptr, 0.f, WICBitmapPaletteTypeCustom)))
                g_rt->CreateBitmapFromWicBitmap(conv, nullptr, &bmp);
            conv->Release();
        }
        fr->Release();
    }
    dec->Release();
    return bmp;
}
static ID2D1Bitmap* LoadLogoBitmapMem(const unsigned char* data, unsigned int len) {
    // 从内存解码（内嵌资源；单文件部署无需携带 img 目录）。
    // 本 SDK 无 CreateDecoderFromMemory：HGlobal 流 → CreateDecoderFromStream
    if (!g_wic || !g_rt || !data || !len) return nullptr;
    HGLOBAL hg = GlobalAlloc(GMEM_MOVEABLE, len);
    if (!hg) return nullptr;
    void* pv = GlobalLock(hg);
    if (!pv) { GlobalFree(hg); return nullptr; }
    memcpy(pv, data, len);
    GlobalUnlock(hg);
    IStream* st = nullptr;
    if (FAILED(CreateStreamOnHGlobal(hg, TRUE, &st))) { GlobalFree(hg); return nullptr; }   // TRUE=释放流时释放内存
    IWICBitmapDecoder* dec = nullptr;
    ID2D1Bitmap* bmp = nullptr;
    if (SUCCEEDED(g_wic->CreateDecoderFromStream(st, nullptr, WICDecodeMetadataCacheOnDemand, &dec))) {
        IWICBitmapFrameDecode* fr = nullptr;
        if (SUCCEEDED(dec->GetFrame(0, &fr))) {
            IWICFormatConverter* conv = nullptr;
            if (SUCCEEDED(g_wic->CreateFormatConverter(&conv))) {
                if (SUCCEEDED(conv->Initialize(fr, GUID_WICPixelFormat32bppPBGRA, WICBitmapDitherTypeNone,
                                               nullptr, 0.f, WICBitmapPaletteTypeCustom)))
                    g_rt->CreateBitmapFromWicBitmap(conv, nullptr, &bmp);
                conv->Release();
            }
            fr->Release();
        }
        dec->Release();
    }
    st->Release();   // 自动 GlobalFree
    return bmp;
}
// 品牌 Logo 表：按 vendor / 型号特征选图，命中即缓存（含未命中，避免每帧重试磁盘）
#include "logos_embed.h"
struct LogoEntry { const char* key; const char* file; const char* alt; };
static const LogoEntry kLogos[] = {
    { "ZOWIE",  "img\\zhuowei.png",  "img\\ZOWIE.png"  },
    { "ASUS",   "img\\ROG.png",      "img\\ASUS.png"   },
    { "AGON",   "img\\爱攻.png",      nullptr           },
    { "AOC",    "img\\冠捷.png",      nullptr           },
    { "HKC",    "img\\HKC.png",      nullptr           },
    { "HUAWEI", "img\\华为.png",      nullptr           },
    { "MSI",    "img\\微星.png",      nullptr           },
    { "TITAN",  "img\\泰坦军团.png",   nullptr           },
    { "XIAOMI", "img\\小米.png",      nullptr           },
    { "ANT",    "img\\蚂蚁电竞.png",   nullptr           },
    { "ALIEN",  "img\\外星人.png",    nullptr           },
};
static std::map<std::string, ID2D1Bitmap*> g_logos;
static std::string LogoKeyOf(MonitorDev* m) {
    if (g_debugBrand) {   // 调试预览：logo 跟随预览品牌
        return g_debugBrand == 1 ? "ZOWIE" : g_debugBrand == 2 ? "ASUS" : g_debugBrand == 3 ? "AOC" : "";
    }
    std::string vendor = ToUpperAscii(m->vendor);
    std::lock_guard<std::recursive_mutex> il(m->infoMx);
    std::string hay = ToUpperAscii(WideToUtf8(m->desc) + " " + m->capModel + " " + m->edidModel);
    if (vendor == "BNQ" || Contains(hay, "ZOWIE") || Contains(hay, "BENQ")) return "ZOWIE";
    if (vendor == "ACI" || vendor == "ASU" || vendor == "AUS" || Contains(hay, "ASUS") || Contains(hay, "ROG ")) return "ASUS";
    if (vendor == "AWR" || Contains(hay, "ALIENWARE") || Contains(hay, "ALIEN ")) return "ALIEN";
    if (vendor == "AOC" || vendor == "PHL") {
        // v1.6.9：爱攻（AGON）系列识别——描述含 AGON，或 EDID/caps 型号为 AG+数字
        // （如 AG276QKD：这类机型 EDID/caps 里往往没有 AGON 字样）。逐字段判断而非对
        // 拼接串 hay 做 "AG"+数字 匹配，避免品牌词误命中。字段在 infoMx 锁内读取（函数开头已持锁）。
        bool agon = Contains(hay, "AGON");
        if (!agon) {
            for (const std::string* s : { &m->capModel, &m->edidModel }) {
                const std::string& t = *s;
                if (t.size() >= 3 && t[0] == 'A' && t[1] == 'G' && isdigit((unsigned char)t[2])) { agon = true; break; }
            }
        }
        return agon ? "AGON" : "AOC";
    }
    if (vendor == "HKC" || Contains(hay, "HKC")) return "HKC";
    if (vendor == "HUA" || Contains(hay, "HUAWEI") || Contains(hay, "HONOR")) return "HUAWEI";
    if (vendor == "MSI" || Contains(hay, "MSI")) return "MSI";
    if (vendor == "TIT" || Contains(hay, "TITAN")) return "TITAN";
    if (vendor == "XIA" || Contains(hay, "XIAOMI") || Contains(hay, "REDMI")) return "XIAOMI";
    if (vendor == "ANT" || Contains(hay, "ANTGAMER") || Contains(hay, "ANT ")) return "ANT";
    return "";
}
static ID2D1Bitmap* BrandLogo(MonitorDev* m) {
    std::string key = LogoKeyOf(m);
    if (key.empty()) return nullptr;
    auto it = g_logos.find(key);
    if (it != g_logos.end()) return it->second;
    ID2D1Bitmap* b = nullptr;
    for (auto& e : kEmbedLogos)      // 内嵌资源优先（另一台机器没带 img 目录也能显示）
        if (key == e.key) {
            b = LoadLogoBitmapMem(e.data, e.len);
            break;
        }
    if (!b)
        for (auto& e : kLogos)
            if (key == e.key) {
                b = LoadLogoBitmap(Utf8ToWide(ResolvePath(e.file)));
                if (!b && e.alt) b = LoadLogoBitmap(Utf8ToWide(ResolvePath(e.alt)));
                break;
            }
    g_logos[key] = b;
    return b;
}

// ================= 控件解析（1:1 平移 MainForm.Resolve）=================
struct Eff {
    bool Ok = false;
    std::string Reason;
    bool EnumMode = false;
    std::vector<unsigned> Options;
    int Lo = 0, Hi = 100;
};
static const ValSpec* SpecOfItem(Item* it) {
    if (!it->mapId || !g_cur) return nullptr;
    if (g_cur->brand == 0) {   // caps 动态码表优先（未知品牌）
        auto d = g_dynSpec.find(it->mapId);
        if (d != g_dynSpec.end()) return &d->second;
    }
    auto sp = SpecOf(g_cur->brand);
    auto s = sp->find(it->mapId);
    if (s != sp->end()) return &s->second;
    auto g = g_generic.find(it->mapId);          // C# 的非 Unknown 品牌回落 GENERIC
    return g != g_generic.end() ? &g->second : nullptr;
}
static Eff Resolve(Item* it) {
    Eff e;
    const ValSpec* s = SpecOfItem(it);
    if (!s || s->code == 0) { e.Ok = false; e.Reason = "厂商未开放软件控制，请用显示器按键调整"; return e; }
    if (!g_cur) { e.Ok = false; e.Reason = "DDC/CI 不可用"; return e; }
    std::lock_guard<std::recursive_mutex> il(g_cur->infoMx);   // supported/allowed/cache 与探测线程并发
    // v1.6.0：缓存值可以给首屏看，但硬件读不到时必须明说，不能让人以为能调。
    // 深探测还没跑完（ddcOk=false）时允许先显示缓存值，但提示「检测中」；
    // 深探测明确失败后一律拦下，避免用户改了没反应还以为程序有问题。
    if (!g_cur->ddcOk) {
        if (g_cur->cache.empty()) { e.Ok = false; e.Reason = "DDC/CI 不可用"; return e; }
        if (g_deepDone) {
            e.Ok = false;
            e.Reason = g_cur->capsOk ? "DDC/CI 读取失败（能识别显示器，但读不到参数）"
                                     : "DDC/CI 不可用（显示器未响应）";
            return e;
        }
    }
    if (g_cur->capsOk && !g_cur->supported.count(s->code) && !g_cur->cache.count(s->code))
    { e.Ok = false; e.Reason = "此型号 DDC 不支持该项"; return e; }

    const std::vector<unsigned>* allowed = nullptr;
    auto ai = g_cur->allowed.find(s->code);
    if (ai != g_cur->allowed.end()) allowed = &ai->second;
    bool range = allowed && allowed->size() == 2 && (*allowed)[1] > (*allowed)[0] &&
                 (int)((*allowed)[1] - (*allowed)[0]) > 2;

    if (s->IsEnum()) {
        std::vector<unsigned> opts = s->optVals;
        if (allowed) {
            std::vector<unsigned> filtered;
            for (auto v : opts)
                if (range ? (v >= (*allowed)[0] && v <= (*allowed)[1])
                          : (std::find(allowed->begin(), allowed->end(), v) != allowed->end()))
                    filtered.push_back(v);
            if (!filtered.empty()) opts = filtered;
        }
        e.Ok = true; e.EnumMode = true; e.Options = opts;
        return e;
    }
    if (allowed && !range && allowed->size() >= 2)
    { e.Ok = true; e.EnumMode = true; e.Options = *allowed; return e; }

    if (s->optVals.empty()) {                     // C# OptionValues == null
        auto mi = g_cur->cacheMax.find(s->code);
        if (mi != g_cur->cacheMax.end() && mi->second > 0 && mi->second <= 255)
        { e.Ok = true; e.Lo = 0; e.Hi = (int)mi->second; return e; }
        e.Ok = false; e.Reason = "该码 DDC 值未知";
        return e;
    }
    int lo = s->uiMin, hi = s->uiMax;
    if (range) { lo = (std::max)(lo, (int)(*allowed)[0]); hi = (std::min)(hi, (int)(*allowed)[1]); }
    if (hi < lo) hi = lo;
    e.Ok = true; e.Lo = lo; e.Hi = hi;
    return e;
}

// ================= 布局（1:1 平移 LayoutAll / LayoutRows）=================
static RECT ContentArea() { return { 0, SI(36), g_W, g_H - SI(36) - SI(36) }; }
static int  StatusBarH()   { return SI(36); }

static int  g_scroll0 = 0, g_scrollMax0 = 0;   // 参数页滚动（内容超两列可用高度时启用）
static int  g_colBottom[2] = { 0, 0 };

// v1.5.3：RGB 增益行仅色温为「用户/USER」档时显示——按各品牌色温枚举的语义标签识别，
// 不再硬编码 0x0B（卓威=用户、华硕=USER、HKC 同理）。未读到值时默认显示（探测后自动修正）。
static bool ColortempIsUser() {
    if (!g_cur) return true;
    unsigned v = 0; bool has = false;
    { std::lock_guard<std::recursive_mutex> il(g_cur->infoMx);
      auto ct = g_cur->cache.find(0x14);
      if (ct != g_cur->cache.end()) { v = ct->second; has = true; } }
    if (!has) return true;
    auto sp = SpecOf(g_cur->brand);
    if (sp) {
        auto spec = sp->find("colortemp");
        if (spec != sp->end()) {
            const char* lb = spec->second.LabelOf(v);
            if (lb) return strstr(lb, "用户") != nullptr || strstr(lb, "USER") != nullptr;
        }
    }
    return v == 0x0B;   // 无标签兜底：MCCS 通用的用户色温值
}

// 一遍完整布局（offY = 垂直滚动偏移，像素）。内部含测高/列数决策，两遍调用结果一致。
static void LayoutRowsPass(int offY) {
    if (g_menu.empty()) return;
    RECT ca = ContentArea();
    const int top0 = ca.top + SI(58);       // 让开顶部按钮区
    const int limit = g_H - StatusBarH();   // 状态栏顶 = 可用下界

    // ---- 第一遍：测每分组高度（可见性规则与第二遍完全一致）----
    struct GInfo { Group* g; int h; bool showUserRgb; };
    std::vector<GInfo> gi;
    int total = 0;
    for (auto& G : g_menu) {
        if ((g_page == 0 && strcmp(G.en, "SYSTEM") == 0) ||
            (g_page == 1 && strcmp(G.en, "SYSTEM") != 0) ||
            g_page >= 2) continue;   // v1.6.8：关于页不显示菜单卡片
        bool showUserRgb = ColortempIsUser();
        int rows = 0, extra = 0;
        for (auto& it : G.items) {
            if (!showUserRgb && it.mapId && (!strcmp(it.mapId, "uR") || !strcmp(it.mapId, "uG") ||
                                 !strcmp(it.mapId, "uB") || !strcmp(it.mapId, "zR") ||
                                 !strcmp(it.mapId, "zG") || !strcmp(it.mapId, "zB"))) continue;
            Eff e = Resolve(&it);
            // 该机型 DDC 不支持的项目：整行隐藏（不置灰占位），caps 到位后自动生效。
            // 调试预览态跳过过滤——预览的是目标品牌的完整排版，不被真机能力裁剪
            if (!g_debugBrand && !e.Ok && e.Reason == "此型号 DDC 不支持该项") {
                it.visible = false;
                continue;
            }
            rows++;
            if (!e.Ok) extra += SI(16);     // 置灰说明行
        }
        int h = SI(12) + SI(18) + rows * SI(46) + extra + SI(18);
        gi.push_back({ &G, h, showUserRgb });
        total += h;
    }

    // ---- 单列放得下就单列（设计规范原样）；放不下则分组流式均衡到两列 ----
    int ncols = (total > limit - top0) ? 2 : 1;
    if (ncols == 2) {
        // 常用组固定排左列：ZOWIE=基本设置(亮度等)，ASUS=图像(亮度+色温卡)
        for (size_t a = 0; a < gi.size(); a++)
            if (strcmp(gi[a].g->en, "BASIC") == 0 || strcmp(gi[a].g->en, "IMAGE") == 0) {
                GInfo tmp = gi[a];
                gi.erase(gi.begin() + a);
                gi.insert(gi.begin(), tmp);
                break;
            }
    }
    const int gapX = SI(24);
    int colW = ncols == 1 ? g_contentW : (g_contentW - gapX) / 2;
    int colX[2] = { SI(206), SI(206) + colW + gapX };
    int colY[2] = { top0 - offY, top0 - offY };

    size_t ci = 0;
    for (auto& in : gi) {
        if (ncols == 2) ci = (colY[0] <= colY[1]) ? 0 : 1;   // 贪心：放进较矮的列
        Group& G = *in.g;
        int x = colX[ci], w = colW;
        int y = colY[ci] + SI(12);              // 卡片间距
        int inner = SI(18);
        int cardTop = y;
        y += inner;

        for (auto& it : G.items) {
            it.segs.clear();
            it.trackR = { 0,0,0,0 };
            if (!in.showUserRgb && it.mapId && (!strcmp(it.mapId, "uR") || !strcmp(it.mapId, "uG") ||
                                    !strcmp(it.mapId, "uB") || !strcmp(it.mapId, "zR") ||
                                    !strcmp(it.mapId, "zG") || !strcmp(it.mapId, "zB"))) {
                it.visible = false;
                continue;
            }
            it.visible = true;
            int rowH = SI(46);
            it.rowR = { x, y, x + w, y + rowH };
            Eff e = Resolve(&it);
            if (e.Ok) {
                if (e.EnumMode) {
                    const ValSpec* s = SpecOfItem(&it);
                    int sx0 = x + SI(140);
                    // 先量总宽：超出行宽则换两行（行高加大，避免最后一个按钮冲出卡片）
                    int totalW = 0;
                    for (auto v : e.Options) {
                        const char* lab = s ? s->LabelOf(v) : nullptr;
                        char fb[32];
                        if (!lab) { sprintf_s(fb, "%u", v); lab = fb; }   // 无标签枚举显示十进制值（如暗部增强 0~4）
                        int segW = (std::max)(SI(50), (int)MeasureW(lab, FUi(SI(11))) + SI(20));
                        totalW += segW + SI(5);
                    }
                    bool two = totalW - SI(5) > w - SI(140) - SI(8);
                    if (two) rowH = SI(78);
                    int sx = sx0, sy = y + SI(8);
                    for (auto v : e.Options) {
                        const char* lab = s ? s->LabelOf(v) : nullptr;
                        char fb[32];
                        if (!lab) { sprintf_s(fb, "%u", v); lab = fb; }   // 无标签枚举显示十进制值（如暗部增强 0~4）
                        int segW = (std::max)(SI(50), (int)MeasureW(lab, FUi(SI(11))) + SI(20));
                        if (two && sx + segW > x + w - SI(6)) { sx = sx0; sy = y + SI(45); }
                        it.segs.push_back({ v, lab, { sx, sy, sx + segW, sy + SI(24) } });
                        sx += segW + SI(5);
                    }
                } else {
                    int sw = (std::min)(SI(560), w - SI(140) - SI(120));   // 右侧留出数值框
                    it.trackR = { x + SI(140), y + SI(20), x + SI(140) + sw, y + SI(20) + SI(6) };
                }
            }
            y += rowH;
            if (!e.Ok) y += SI(16);  // 置灰说明行
        }
        y += inner;                 // 卡底部内边距
        G.cardR = { x - SI(10), cardTop, x + w - SI(2), y };
        colY[ci] = y;
    }
    g_colBottom[0] = colY[0]; g_colBottom[1] = colY[1];
}
static void LayoutRows() {
    if (g_menu.empty()) return;
    // 第一遍：零偏移布局，得到实际列底 → 滚动上限；夹紧后第二遍带偏移重排
    LayoutRowsPass(0);
    RECT ca = ContentArea();
    const int top0 = ca.top + SI(58);
    const int limit = g_H - StatusBarH();
    int bottom = (std::max)(g_colBottom[0], g_colBottom[1]);
    g_scrollMax0 = (std::max)(0, bottom - limit);
    if (g_scroll0 > g_scrollMax0) g_scroll0 = g_scrollMax0;
    if (g_scroll0 < 0) g_scroll0 = 0;
    LayoutRowsPass(g_scroll0);
}
static void LayoutAll() {
    int W = g_W;
    g_contentW = (std::max)(520, W - SI(196) - SI(24));

    // 页签（首项 x=206，后续项 = 前项右缘+文字宽+48；文字宽实测）
    int tw1 = (int)MeasureW("参数设置", FUi(SI(12)));
    int tw2 = (int)MeasureW("系统管理", FUi(SI(12)));
    int tw3 = (int)MeasureW("关于", FUi(SI(12)));
    int tx = SI(206);
    g_tabP = { tx, SI(38), tx + tw1 + 16, SI(38) + SI(34) };
    g_tabS = { g_tabP.right + SI(48), SI(38), g_tabP.right + SI(48) + tw2 + 16, SI(38) + SI(34) };
    g_tabA = { g_tabS.right + SI(48), SI(38), g_tabS.right + SI(48) + tw3 + 16, SI(38) + SI(34) };

    // 标题栏按钮（调试入口已移除；品牌预览改由 config.json DebugBrand 字段控制）
    g_btnClose = { W - SI(46), 0, W, SI(36) };
    g_btnMin   = { W - SI(92), 0, W - SI(46), SI(36) };
    g_btnDbg   = {};

    // 顶部按钮（与页签同行，右对齐；v1.2 去掉「保存基准」按钮，保留热键 Ctrl+Alt+S）
    int bh = SI(30);
    int rw = (std::min)(SI(196), SI(132) + g_contentW / 9);
    g_btnRestore = { W - SI(20) - rw, SI(40), W - SI(20), SI(40) + bh };
    g_btnSave    = { 0, 0, 0, 0 };

    // 显示器卡（284 高；栅格 4px 系；选中卡内含 分辨率/刷新率 下拉 + 详细信息按钮）
    g_monRects.clear();
    int cardH = SI(284);
    int my = SI(58);
    for (size_t i = 0; i < g_mons.size() && i < 2; i++) {
        g_monRects.push_back({ SI(14), my, SI(14) + SI(172), my + cardH });
        my += cardH + SI(12);
    }
    g_resRow = {}; g_hzRow = {}; g_btnDetail = {};
    for (size_t i = 0; i < g_monRects.size() && i < g_mons.size(); i++) {
        if (g_mons[i] != g_cur) continue;
        RECT r = g_monRects[i];
        g_resRow    = { r.left + SI(12), r.top + SI(170), r.right - SI(12), r.top + SI(204) };
        g_hzRow     = { r.left + SI(12), r.top + SI(208), r.right - SI(12), r.top + SI(242) };
        int btnW = (int)MeasureW("详细信息", FUi(SI(11), true)) + SI(24);
        g_btnDetail = { r.left + SI(12), r.top + SI(246), r.left + SI(12) + btnW, r.top + SI(272) };
        break;
    }
    if (g_page == 0) LayoutRows();
    LayoutSystem();
    // v1.6.6 修复 H3：布局已重排，下拉面板的屏幕坐标快照（g_ddCard/g_ddItems）随之失效——
    // 原实现不关闭，下拉打开期间来 WM_SIZE/重探/WM_APP+2 触发 LayoutAll 后，
    // 用户点击会命中到完全错误的分辨率/刷新率选项（如把 1080p 点到 4K）。统一关掉。
    g_ddMode = 0;
}

// ================= 绘制 =================
static std::string PortName(MonitorDev* m) {
    std::lock_guard<std::recursive_mutex> il(m->infoMx);
    auto iv = m->cache.find(0x60);
    if (iv != m->cache.end()) {
        switch (iv->second) {
            case 0x0F: return "DP";
            case 0x11: return "HDMI1";
            case 0x12: return "HDMI2";
            case 0x13: return "HDMI3";
            case 0x01: return "VGA";
            case 0x03: return "DVI";
        }
    }
    return "--";
}
static const char* BrandLetter(MonitorDev* m) {
    int b = m->brand;
    if (b == 1) return "Z";
    if (b == 2) return "R";
    if (b == 3) return "A";
    if (b == 5) return "H";
    if (b == 6) return "M";
    if (b == 7) return "L";
    if (b == 8) return "S";
    if (b == 9) return "D";
    if (b == 10) return "A";
    if (b == 11) return "H";
    if (b == 12) return "A";
    if (b == 13) return "A";
    if (b == 14) return "T";
    if (b == 15) return "H";
    if (b == 16) return "M";
    return "M";
}
static const char* BrandName(MonitorDev* m) {
    int b = m->brand;
    if (b == 1) return "BENQ ZOWIE";
    if (b == 2) return "ASUS ROG";
    if (b == 3) return "AOC AGON";
    if (b == 5) return "HKC 惠科";
    if (b == 6) return "MSI 微星";
    if (b == 7) return "LG";
    if (b == 8) return "SAMSUNG 三星";
    if (b == 9) return "DELL 戴尔";
    if (b == 10) return "ACER 宏碁";
    if (b == 11) return "HP 惠普";
    if (b == 12) return "ALIENWARE 外星人";
    if (b == 13) return "ANTGAMER 蚂蚁电竞";
    if (b == 14) return "TITAN ARMY 泰坦军团";
    if (b == 15) return "HUAWEI 华为";
    if (b == 16) return "XIAOMI 小米";
    return "MONITOR";
}

static void DrawSlider(Item* it, const RECT& r) {
    const ValSpec* s = SpecOfItem(it);
    if (!s) return;
    unsigned v = 0;
    bool hasV = false;
    if (g_cur) {
        std::lock_guard<std::recursive_mutex> il(g_cur->infoMx);
        hasV = g_cur->cache.count(s->code) > 0;
        if (hasV) v = g_cur->cache[s->code];
    }
    Eff e = Resolve(it);
    if (!e.Ok || e.EnumMode) return;
    RECT tr = it->trackR;
    // 轨道（C# 同款双绘，避免窄填充圆角残影）
    FillRRect(tr, 3.f, Theme::LineStrong);
    int lo = e.Lo, hi = (std::max)(e.Lo + 1, e.Hi);
    int ui = ClampI(s->UiFromVcp(v), lo, hi);
    float frac = (float)(ui - lo) / (float)(hi - lo);
    int fillW = (int)((tr.right - tr.left) * frac);
    FillRRect(tr, 3.f, Theme::LineStrong);
    if (fillW >= 4) {
        RECT fill = { tr.left, tr.top, tr.left + fillW, tr.bottom };
        FillRRect(fill, 3.f, Theme::Accent);
    } else if (fillW > 0) {
        D2D1_ELLIPSE el = D2D1::Ellipse(D2D1::Point2F((float)tr.left + 3, (tr.top + tr.bottom) / 2.f), 4, 5);
        auto b = Brush(Theme::Accent);
        g_rt->FillEllipse(el, b); b->Release();
    }
    // 滑块（14px 圆，中心对准填充末端）
    float kx = (float)(tr.left + fillW);
    float ky = (tr.top + tr.bottom) / 2.f;
    D2D1_ELLIPSE knob = D2D1::Ellipse(D2D1::Point2F(kx, ky), 7.f, 7.f);
    auto b = Brush(Theme::TextPrimary);
    g_rt->FillEllipse(knob, b); b->Release();
    // 数值
    char txt[32];
    sprintf_s(txt, "%d / %d", ui, hi);
    RECT vr = { r.right - SI(112), r.top + SI(14), r.right - SI(16), r.top + SI(14) + SI(20) };
    DrawT(txt, FMono(SI(12), false, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
          RectF(vr), Theme::TextTertiary);
}

static void PaintRow(Item* it) {
    RECT r = it->rowR;
    if (it->action) {
        // 功能项（恢复出厂/扫描器/校准等）：可点击，不做 VCP 解析、不置灰
        DrawT(it->cn, FUi(SI(13)), RectF({ r.left + SI(24), r.top + SI(15), r.right, r.bottom }),
              Theme::TextSecondary);
        int bw = SI(76);
        RECT br = { r.right - SI(16) - bw, r.top + SI(11), r.right - SI(16), r.top + SI(11) + SI(24) };
        bool hov = Hover(br);
        FillRRect(br, 6.f, hov ? Theme::SurfaceHover : Theme::SurfaceSub);
        DrawT("打开", FUi(SI(11), false, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
              RectF({ br.left, br.top, br.right - SI(10), br.bottom }),
              hov ? Theme::TextPrimary : Theme::TextTertiary);
        int cx = br.right - SI(14), cy = (br.top + br.bottom) / 2;
        auto cb = Brush(hov ? Theme::TextPrimary : Theme::TextTertiary);
        g_rt->DrawLine(D2D1::Point2F((float)cx - 2, (float)cy - 3), D2D1::Point2F((float)cx + 1, (float)cy), cb, 1.2f);
        g_rt->DrawLine(D2D1::Point2F((float)cx + 1, (float)cy), D2D1::Point2F((float)cx - 2, (float)cy + 3), cb, 1.2f);
        cb->Release();
        return;
    }
    Eff e = Resolve(it);
    // 标签
    Col lblColor = e.Ok ? Theme::TextSecondary : Theme::TextMuted;
    DrawT(it->cn, FUi(SI(13)), RectF({ r.left + SI(24), r.top + SI(15), r.right, r.bottom }), lblColor);
    if (!e.Ok) {
        // v1.6.0：原因文字统一对齐到固定列，且必须避开标签实际宽度。
        // 原先把 left+140 写死：标签一长（「灰阶响应 Trace Free」）就被原因文字压住；
        // 改成「跟随标签」又会让每行参差不齐。折中：列位置固定，但被标签挡住时右移。
        int lblW = (int)MeasureW(it->cn, FUi(SI(13)));
        int colX = r.left + SI(24) + (int)MeasureW("用户红增益", FUi(SI(13))) + SI(24);   // 最长标签列的右侧
        if (colX < r.left + SI(24) + lblW + SI(14))
            colX = r.left + SI(24) + lblW + SI(14);
        DrawT(e.Reason, FUi(SI(11)),
              RectF({ colX, r.top + SI(15), r.right - SI(12), r.bottom }), Theme::TextMuted);
        return;
    }
    const ValSpec* s = SpecOfItem(it);
    unsigned v = 0;
    bool hasV = false;
    if (s && g_cur) {
        std::lock_guard<std::recursive_mutex> il(g_cur->infoMx);
        hasV = g_cur->cache.count(s->code) > 0;
        if (hasV) v = g_cur->cache[s->code];
    }
    if (!hasV) {
        DrawT("…", FMono(SI(12)), RectF({ r.left + SI(140), r.top + SI(15), r.right, r.bottom }), Theme::TextTertiary);
        return;
    }
    if (e.EnumMode) {
        for (auto& seg : it->segs) {
            bool on = seg.v == v;
            bool hov = Hover(seg.r);
            bool prs = Press(seg.r);
            Col bg = on ? (prs ? Theme::AccentHover.Dark(0.06f) : Theme::Accent)
                        : hov ? Theme::SurfaceHover : Theme::SurfaceSub;
            FillRRect(seg.r, 6.f, bg);
            DrawT(seg.label, FUi(SI(11), on, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
                  RectF(seg.r), on ? Theme::OnAccent : (hov ? Theme::TextSecondary : Theme::TextTertiary));
        }
    } else {
        DrawSlider(it, r);
    }
}

static void PaintTitleBar() {
    auto b = Brush(Theme::LineSubtle);
    g_rt->DrawLine(D2D1::Point2F(0, (float)SI(36)), D2D1::Point2F((float)g_W, (float)SI(36)), b, 1.f);
    b->Release();
    DrawT(AppDisplayName(), FUi(SI(12), true),
          D2D1::RectF(20.f, 10.f, 20.f + 8000.f, 10.f + 200.f), Theme::TextPrimary);
    // 窗口按钮（hover 提亮）
    Col minC = Hover(g_btnMin) ? Theme::TextPrimary : Theme::TextMuted;
    Col closeC = Hover(g_btnClose) ? Theme::Danger : Theme::TextMuted;
    DrawT("—", FUi(SI(12), false, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER), RectF(g_btnMin), minC);
    DrawT("✕", FUi(SI(12), false, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER), RectF(g_btnClose), closeC);
}
static void PaintTabs() {
    bool a1 = g_page == 0, a2 = g_page == 1, a3 = g_page == 2;
    DrawT("参数设置", FUi(SI(12), a1, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
          RectF(g_tabP), a1 ? Theme::TextPrimary : Theme::TextTertiary);
    DrawT("系统管理", FUi(SI(12), a2, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
          RectF(g_tabS), a2 ? Theme::TextPrimary : Theme::TextTertiary);
    DrawT("关于", FUi(SI(12), a3, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
          RectF(g_tabA), a3 ? Theme::TextPrimary : Theme::TextTertiary);
    const RECT& bar = a1 ? g_tabP : a2 ? g_tabS : g_tabA;
    RECT br = { bar.left - SI(8), SI(74), bar.right, SI(74) + SI(3) };
    FillRRect(br, 2.f, Theme::Accent);
}
static void PaintTopButtons() {
    // 一键还原基准（主按钮；「保存基准」按钮已按需求移除，仅保留 Ctrl+Alt+S 热键）
    // v1.6.6 修复 1：还原期间置灰 + 改文案——原实现只有 hover/press 三态，2.4s 还原过程中
    // 按钮看起来和原来一样，用户等不及再点一次就触发重入（两个还原线程并发写 DDC）。
    bool busy = g_restoreBusy.load();
    bool h2 = !busy && Hover(g_btnRestore);
    Col c2 = busy ? Theme::SurfaceSub
                  : Press(g_btnRestore) ? Theme::AccentHover.Dark(0.06f) : h2 ? Theme::AccentHover : Theme::Accent;
    FillRRect(g_btnRestore, 8.f, c2);
    DrawT(busy ? "还原中…" : "一键还原基准",
          FUi(SI(12), true, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
          RectF(g_btnRestore), busy ? Theme::TextMuted : Theme::OnAccent);
}
static void PaintMonitorCards() {
    for (size_t i = 0; i < g_monRects.size() && i < g_mons.size(); i++) {
        auto* m = g_mons[i];
        RECT r = g_monRects[i];
        bool sel = m == g_cur;

        FillRRect(r, 12.f, Theme::SurfaceCard);
        if (sel) {
            // 选中发光：内发光 r3 + r8，外发光 r20（三圈描边）
            for (int pass = 0; pass < 3; pass++) {
                int grow = pass == 0 ? 0 : pass == 1 ? 1 : 2;
                int alpha = pass == 0 ? 160 : pass == 1 ? 70 : 60;
                RECT gr = { r.left - grow, r.top - grow, r.right + grow, r.bottom + grow };
                ID2D1SolidColorBrush* pb = Brush(Col(Theme::Accent.r, Theme::Accent.g, Theme::Accent.b, alpha / 255.f));
                g_rt->DrawRoundedRectangle(RRect(gr, 12 + grow), pb, pass == 0 ? 1.6f : pass == 1 ? 4.f : 8.f);
                pb->Release();
            }
        }

        int pad = SI(12);
        int cx = r.left + pad;
        int innerW = (r.right - r.left) - pad * 2;
        int y = r.top + pad;

        // 主/副徽标（右上角）
        RECT badge = { r.right - pad - SI(24), r.top + pad, r.right - pad, r.top + pad + SI(16) };
        FillRRect(badge, 4.f, i == 0 ? Theme::Accent : Theme::SurfaceSub);
        DrawT(i == 0 ? "主" : "副",
              FUi(SI(10), true, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
              RectF(badge), i == 0 ? Theme::OnAccent : Theme::TextTertiary);

        // v1.6.8 Logo 显示修复：
        //  1) 先铺一块浅色圆角底板——深色 logo 直接画在深色卡片上看不清；
        //  2) 底板宽度给右上角主/副徽标留 34px，宽 logo 不再伸进徽标区（卡内宽约 146 < 旧上限 150）；
        //  3) logo 在底板内四周内缩 8px 居中绘制。
        ID2D1Bitmap* logo = BrandLogo(m);
        int bgW = (std::min)(SI(150), innerW - SI(34));
        RECT logoBg = { cx, y, cx + bgW, y + SI(64) };
        // v1.6.9：底板 = 浅白 与 品牌强调色按 9:1 混合（品牌色调浅底，避免纯白单调；深色 logo 仍清晰）
        const Col bgBase = Col::Hex("#ECEDEF");
        const Col logoBgCol{
            bgBase.r * 0.9f + Theme::Accent.r * 0.1f,
            bgBase.g * 0.9f + Theme::Accent.g * 0.1f,
            bgBase.b * 0.9f + Theme::Accent.b * 0.1f };
        FillRRect(logoBg, 10.f, logoBgCol);
        if (logo) {
            D2D1_SIZE_U px = logo->GetPixelSize();
            int pw = (int)px.width, ph = (int)px.height;
            int boxW = bgW - SI(16), boxH = SI(48);
            float k = (pw && ph) ? (std::min)((float)boxW / pw, (float)boxH / ph) : 1.f;
            int iw = (int)(pw * k), ih = (int)(ph * k);
            int lx0 = logoBg.left + (bgW - iw) / 2;       // 底板内水平居中
            int ly0 = logoBg.top + (SI(64) - ih) / 2;     // 底板内垂直居中
            g_rt->DrawBitmap(logo, D2D1::RectF((float)lx0, (float)ly0,
                                               (float)(lx0 + iw), (float)(ly0 + ih)));
        } else {
            RECT logoR = { cx, y, cx + SI(60), y + SI(60) };
            FillRRect(logoR, 10.f, Theme::Accent);
            DrawT(BrandLetter(m), FUi(SI(26), true, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
                  RectF(logoR), Theme::OnLogo);
        }
        y += SI(64);
        DrawT(BrandName(m), FUi(SI(11), true, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
              RectF({ cx, y, cx + innerW, y + SI(16) }), Theme::TextTertiary);
        y += SI(22);

        // 型号
        DrawT(DisplayNameOf(m), FUi(SI(15), true),
              RectF({ cx, y, cx + innerW, y + SI(20) }), Theme::TextPrimary);
        y += SI(26);

        // 统计双列：标签（10 号灰）在上，值（等宽加粗加大）在下——刷新率 | 分辨率
        int colW = (innerW - SI(8)) / 2;
        char hz[32] = "-- Hz";
        char res[80] = "----×----";
        if (!m->screenDevice.empty()) {
            DEVMODEW dm = {}; dm.dmSize = sizeof(dm);
            std::wstring sd = Utf8ToWide(m->screenDevice);
            if (EnumDisplaySettingsW(sd.c_str(), ENUM_CURRENT_SETTINGS, &dm)) {
                sprintf_s(hz, "%u Hz", dm.dmDisplayFrequency);
                sprintf_s(res, "%u×%u", dm.dmPelsWidth, dm.dmPelsHeight);
            }
        }
        DrawT("刷新率", FUi(SI(10)), RectF({ cx, y, cx + colW, y + SI(13) }), Theme::TextMuted);
        DrawT(hz, FMono(SI(16), true), RectF({ cx, y + SI(13), cx + colW, y + SI(33) }),
              sel ? Theme::Accent : Theme::TextPrimary);
        DrawT("分辨率", FUi(SI(10)), RectF({ cx + colW + SI(8), y, cx + innerW, y + SI(13) }), Theme::TextMuted);
        DrawT(res, FMono(SI(13), true), RectF({ cx + colW + SI(8), y + SI(13), cx + innerW, y + SI(33) }), Theme::TextSecondary);
        y += SI(41);

        // 分隔线（信息区 / 设置区分界）
        {
            auto b = Brush(Theme::LineStrong);
            g_rt->DrawLine(D2D1::Point2F((float)cx, (float)y), D2D1::Point2F((float)(cx + innerW), (float)y), b, 1.f);
            b->Release();
        }

        // 卡尾「详细信息」按钮（贴文字宽，红色实心，无箭头；矩形由 LayoutAll 依选中卡给出）
        if (sel && g_btnDetail.right > g_btnDetail.left) {
            bool hov = Hover(g_btnDetail);
            bool prs = Press(g_btnDetail);
            FillRRect(g_btnDetail, 6.f, prs ? Theme::Accent.Dark(0.14f) : hov ? Theme::Accent.Dark(0.07f) : Theme::Accent);
            DrawT("详细信息", FUi(SI(11), true, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
                  RectF(g_btnDetail), Theme::OnAccent);
        }
    }
}
static void PaintDisplayOptions() {
    // 两个下拉行与显示器卡同风格：SurfaceCard 圆角卡、左标签（灰）右数值（等宽主色）+ V 形箭头
    if (g_resRow.right <= g_resRow.left) return;   // 空间不足已隐藏
    char resv[40], hzv[24];
    if (g_curResW > 0) sprintf_s(resv, "%u×%u", g_curResW, g_curResH);
    else sprintf_s(resv, "--×--");
    if (g_curHz > 0) sprintf_s(hzv, "%u Hz", g_curHz);
    else sprintf_s(hzv, "-- Hz");
    struct OptRow { RECT r; const char* label; const char* value; };
    OptRow rows[2] = { { g_resRow, "分辨率", resv }, { g_hzRow, "刷新率", hzv } };
    for (auto& row : rows) {
        if (row.r.right <= row.r.left) continue;
        bool hov = Hover(row.r);
        FillRRect(row.r, 8.f, hov ? Theme::SurfaceHover : Theme::SurfaceSub);
        { auto b = Brush(hov ? Theme::Accent : Theme::LineStrong);
          g_rt->DrawRoundedRectangle(RRect(row.r, 8.f), b, 1.f); b->Release(); }
        DrawT(row.label, FUi(SI(12), true, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
              RectF({ row.r.left + SI(12), row.r.top, row.r.left + SI(56), row.r.bottom }),
              Theme::TextSecondary);
        DrawT(row.value, FMono(SI(12), true, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
              RectF({ row.r.left + SI(58), row.r.top, row.r.right - SI(28), row.r.bottom }),
              Theme::TextPrimary);
        int ar = SI(6);
        int ax = row.r.right - SI(16), ay = (row.r.top + row.r.bottom) / 2;
        auto ab = Brush(hov ? Theme::TextPrimary : Theme::TextTertiary);
        g_rt->DrawLine(D2D1::Point2F((float)(ax - ar), (float)(ay - ar / 2)),
                       D2D1::Point2F((float)ax, (float)(ay + ar / 2)), ab, 1.5f);
        g_rt->DrawLine(D2D1::Point2F((float)ax, (float)(ay + ar / 2)),
                       D2D1::Point2F((float)(ax + ar), (float)(ay - ar / 2)), ab, 1.5f);
        ab->Release();
    }
}
// ---- 显示器详细信息弹窗 ----
static bool g_infoOpen = false;
static int  g_infoScroll = 0, g_infoMaxScroll = 0;
static RECT g_infoCard = {}, g_infoClose = {};
static std::vector<std::pair<std::string, std::string>> g_infoRows;
static std::vector<std::string> g_infoCodeLines;
static int g_infoCodeCount = 0;
static void OpenInfoPanel() {
    MonitorDev* m = g_cur;
    if (!m) return;
    g_infoOpen = true; g_infoScroll = 0;
    g_infoRows.clear(); g_infoCodeLines.clear();
    std::vector<std::pair<std::string, std::string>> R;
    R.push_back({ "品牌", BrandName(m) });
    R.push_back({ "型号", DisplayNameOf(m) });
    R.push_back({ "caps 型号", m->capModel.empty() ? "-" : m->capModel });
    R.push_back({ "制造商", m->manufacturerName.empty() ? "-" : m->manufacturerName });
    R.push_back({ "序列号", m->serial.empty() ? "-" : m->serial });
    char date[128] = "-";   // v1.5.3 闪退修复：UTF-8 中文备注约 68 字节，64 缓冲区溢出触发 CRT 异常
    if (m->edidYear >= 1990) {
        if (m->edidWeek > 0) {
            // EDID 只有「年+周」，无日月——按「该周第一天」折算近似日期（1月1日 + (周-1)*7 天）
            SYSTEMTIME st = { (WORD)m->edidYear, 1, 0, 1, 0, 0, 0, 0 };
            FILETIME ft;
            if (SystemTimeToFileTime(&st, &ft)) {
                ULARGE_INTEGER u; u.LowPart = ft.dwLowDateTime; u.HighPart = ft.dwHighDateTime;
                int wk = m->edidWeek > 53 ? 53 : m->edidWeek;
                u.QuadPart += (ULONGLONG)(wk - 1) * 7 * 86400000ULL * 10000ULL;   // 7 天（100ns 单位）
                ft.dwLowDateTime = u.LowPart; ft.dwHighDateTime = u.HighPart;
                SYSTEMTIME out;
                if (FileTimeToSystemTime(&ft, &out)) {
                    char day[24];
                    sprintf_s(day, "%04d-%02d-%02d", out.wYear, out.wMonth, out.wDay);
                    sprintf_s(date, "%d 第%d周（约 %s 那周，EDID 自报）", m->edidYear, m->edidWeek, day);
                }
            }
            if (date[0] == '-')   // 折算失败回落周表示
                sprintf_s(date, "%d 第%d周（EDID 自报）", m->edidYear, m->edidWeek);
        } else                 sprintf_s(date, "%d", m->edidYear);
    }
    R.push_back({ "生产日期", date });
    char diag[32] = "-";
    if (m->edidCmW > 0 && m->edidCmH > 0)
        sprintf_s(diag, "%.1f 英寸 (%d×%d cm)", sqrt((float)m->edidCmW * m->edidCmW + (float)m->edidCmH * m->edidCmH) / 2.54f, m->edidCmW, m->edidCmH);
    R.push_back({ "面板尺寸", diag });
    char ever[16] = "-";
    if (m->edidVerMaj >= 0) sprintf_s(ever, "%d.%d", m->edidVerMaj, m->edidVerMin);
    R.push_back({ "EDID 版本", ever });
    // ---- EDID 首选时序 + 扩展块解析（CTA-861 / DisplayID）----
    if (m->edidRaw.size() >= 128) {
        auto& E = m->edidRaw;
        int pclk = E[54] | (E[55] << 8);                       // 单位 10kHz
        int hact = E[56] | ((E[58] & 0xF0) << 4);
        int hbl  = E[57] | ((E[58] & 0x0F) << 8);
        int vact = E[59] | ((E[61] & 0xF0) << 4);
        int vbl  = E[60] | ((E[61] & 0x0F) << 8);
        int tot  = (hact + hbl) * (vact + vbl);
        if (pclk > 0 && tot > 0) {
            char pt[48];
            sprintf_s(pt, "%d×%d @ %.0f Hz（首选时序）", hact, vact, (double)pclk * 10000.0 / tot);
            R.push_back({ "首选时序", pt });
        }
        int extN = E[126];
        bool cta = false, dispId = false, hdr = false, audio = false, y444 = false;
        int ctaRev = 0;
        int maxExt = (std::min)(extN, (int)((E.size() - 128) / 128));
        for (int e = 0; e < maxExt; e++) {
            int b0 = 128 * (e + 1);
            // v1.5.3 闪退修复：整个扩展块解析必须按块内边界钳制。
            // 原实现 end = 4 + dtdOff（dtdOff 是 EDID 里的字节，厂商可写任意值）
            // 且 off += len 无上限检查 → 畸形 EDID 可越界读取 130+ 字节，
            // 在别的机器上表现为「打开详细信息随机闪退」，本机 EDID 规范所以从不复现。
            const int blkEnd = b0 + 128;                        // 本扩展块末尾（不含）
            if (blkEnd > (int)E.size()) break;                  // 块不完整：放弃后续解析
            unsigned char tag = E[b0];
            if (tag == 0x02) {                                  // CTA-861
                cta = true; ctaRev = E[b0 + 3];
                if (ctaRev >= 2) { y444 = (E[b0 + 3] & 0x20) != 0; }
                int dtdOff = E[b0 + 2];
                int off = 4, end = dtdOff >= 4 ? 4 + dtdOff : 128;
                if (end > 128) end = 128;                       // 数据块区不超过块内 128 字节
                while (off + 1 <= end) {                        // 至少要有 1 字节头部
                    unsigned char h = E[b0 + off];
                    int t = h >> 5, len = h & 0x1F;
                    off++;
                    if (off + len > end) break;                 // 块声明长度越过数据块区：停止
                    if (t == 1) audio = true;                   // Audio Data Block
                    if (t == 7 && len >= 1) {                   // Extended tag
                        if (E[b0 + off] == 6) hdr = true;       // HDR Static Metadata DB
                    }
                    off += len;
                }
            } else if (tag == 0x70) dispId = true;              // DisplayID
        }
        std::string exts = std::to_string(extN) + " 个";
        if (cta)  exts += " · CTA-861 rev" + std::to_string(ctaRev);
        if (dispId) exts += " · DisplayID";
        R.push_back({ "EDID 扩展块", exts });
        if (cta) {
            std::string au = audio ? "有" : "无";
            if (y444) au += " · YCbCr 4:4:4";
            R.push_back({ "基础音频", au });
        }
        R.push_back({ "HDR（EDID）", hdr ? "支持" : "不支持" });
    }
    // ---- DisplayConfig：当前输出实际状态（HDR 开关 / 每通道色深）----
    {
        UINT32 nP = 0, nM = 0;
        std::wstring sd = Utf8ToWide(m->screenDevice);
        if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &nP, &nM) == ERROR_SUCCESS && nP) {
            std::vector<DISPLAYCONFIG_PATH_INFO> paths(nP);
            std::vector<DISPLAYCONFIG_MODE_INFO> modes(nM);
            if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &nP, paths.data(), &nM, modes.data(), nullptr) == ERROR_SUCCESS) {
                for (auto& p : paths) {
                    DISPLAYCONFIG_SOURCE_DEVICE_NAME src = {};
                    src.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
                    src.header.size = sizeof(src);
                    src.header.adapterId = p.sourceInfo.adapterId;
                    src.header.id = p.sourceInfo.id;
                    if (DisplayConfigGetDeviceInfo(&src.header) != ERROR_SUCCESS) continue;
                    if (_wcsicmp(src.viewGdiDeviceName, sd.c_str()) != 0) continue;   // 匹配本显示器
                    DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO aci = {};
                    aci.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
                    aci.header.size = sizeof(aci);
                    aci.header.adapterId = p.targetInfo.adapterId;
                    aci.header.id = p.targetInfo.id;
                    if (DisplayConfigGetDeviceInfo(&aci.header) == ERROR_SUCCESS) {
                        std::string h;
                        if (aci.advancedColorSupported)
                            h = aci.advancedColorEnabled ? "支持 · 已开启" : "支持 · 未开启";
                        else h = "不支持";
                        R.push_back({ "HDR（系统）", h });
                        char bits[32];
                        sprintf_s(bits, "%u bit / 通道", (unsigned)aci.bitsPerColorChannel);
                        R.push_back({ "每通道色深", bits });
                    }
                    break;
                }
            }
        }
    }
    R.push_back({ "PnP 厂商", m->vendor.empty() ? "-" : m->vendor });
    R.push_back({ "接口", PortName(m) });
    char cur[64] = "-";
    if (!m->screenDevice.empty()) {
        DEVMODEW dm = {}; dm.dmSize = sizeof(dm);
        if (EnumDisplaySettingsW(Utf8ToWide(m->screenDevice).c_str(), ENUM_CURRENT_SETTINGS, &dm))
            sprintf_s(cur, "%u×%u @ %u Hz", dm.dmPelsWidth, dm.dmPelsHeight, dm.dmDisplayFrequency);
    }
    R.push_back({ "当前显示", cur });
    R.push_back({ "屏幕设备", m->screenDevice.empty() ? "-" : m->screenDevice });
    R.push_back({ "DDC/CI", m->ddcOk ? (m->capsOk ? "正常（caps 已读取）" : "正常（caps 拒答）") : "不可用" });
    // ---- 只读硬件信息（实时 DDC 读取）----
    unsigned c, x;
    char rb[64];
    if (m->Read(0xC0, c, x)) {   // Display Usage Time（32 位小时数）
        R.push_back({ "使用时长", std::to_string(c) + " 小时" });
    }
    if (m->Read(0xC9, c, x)) {   // Display Firmware Level
        sprintf_s(rb, "0x%04X", (unsigned)(c & 0xFFFF));
        R.push_back({ "固件版本", rb });
    }
    if (m->Read(0xC8, c, x)) {   // Display Controller Type
        sprintf_s(rb, "0x%02X / %u", (unsigned)((c >> 8) & 0xFF), (unsigned)(c & 0xFF));
        R.push_back({ "主控芯片", rb });
    }
    if (m->Read(0xDF, c, x)) {   // VCP Version
        sprintf_s(rb, "%u.%u", (unsigned)((c >> 8) & 0xFF), (unsigned)(c & 0xFF));
        R.push_back({ "MCCS 版本", rb });
    }
    g_infoRows = std::move(R);
    std::vector<int> codes(m->supported.begin(), m->supported.end());
    if (codes.empty()) {
        g_infoCodeLines.push_back("（未获取到可控码列表）");
        g_infoCodeCount = 0;
    } else {
        g_infoCodeCount = (int)codes.size();
        std::string line;
        int n = 0;
        for (int c : codes) {
            char b[12];
            sprintf_s(b, "0x%02X", c);
            line += (n % 8 == 0 && n) ? "  " : (n ? ", " : "");
            line += b;
            if (++n % 8 == 0) { g_infoCodeLines.push_back(line); line.clear(); }
        }
        if (!line.empty()) g_infoCodeLines.push_back(line);
    }
    // 可滚动内容高度 = 信息行 + 码块 + 内边距
    int innerW = SI(520) - SI(40);
    g_infoMaxScroll = (std::max)(0,
        (int)g_infoRows.size() * SI(24) + SI(16) + SI(22) + (int)g_infoCodeLines.size() * SI(16) + SI(12)
        - (SI(560) - SI(48) - SI(36)));
}
static void PaintInfoPanel() {
    auto dim = Brush(Col(0.f, 0.f, 0.f, 0.62f));
    g_rt->FillRectangle(D2D1::RectF(0, 0, (float)g_W, (float)g_H), dim);
    dim->Release();
    RECT card = { (g_W - SI(520)) / 2, (g_H - SI(560)) / 2 - SI(16),
                  (g_W + SI(520)) / 2, (g_H + SI(560)) / 2 - SI(16) };
    g_infoCard = card;
    FillRRect(card, 12.f, Theme::SurfaceCard);
    { auto b = Brush(Theme::LineStrong);
      g_rt->DrawRoundedRectangle(RRect(card, 12.f), b, 1.f); b->Release(); }
    int lx = card.left + SI(24), rx = card.right - SI(24);
    DrawT("显示器详细信息", FUi(SI(14), true),
          RectF({ lx, card.top + SI(14), rx - SI(30), card.top + SI(34) }), Theme::TextPrimary);
    g_infoClose = { card.right - SI(40), card.top + SI(12), card.right - SI(12), card.top + SI(36) };
    bool hc = Hover(g_infoClose);
    DrawT("✕", FUi(SI(12), false, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
          RectF(g_infoClose), hc ? Theme::TextPrimary : Theme::TextMuted);
    // 内容（可滚动裁剪）
    D2D1_RECT_F clip = D2D1::RectF((float)card.left + 1, (float)card.top + SI(44),
                                   (float)card.right - 1, (float)card.bottom - SI(12));
    g_rt->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    int y = card.top + SI(52) - g_infoScroll;
    for (auto& kv : g_infoRows) {
        DrawT(kv.first, FUi(SI(11)), RectF({ lx, y, lx + SI(96), y + SI(22) }), Theme::TextMuted);
        DrawT(kv.second.c_str(), FMono(SI(11)), RectF({ lx + SI(100), y, rx, y + SI(22) }), Theme::TextSecondary);
        y += SI(24);
    }
    y += SI(8);
    char cnt[48];
    sprintf_s(cnt, "DDC 可控 VCP 码（%d）", g_infoCodeCount);
    DrawT(cnt, FUi(SI(11), true), RectF({ lx, y, rx, y + SI(20) }), Theme::TextPrimary);
    y += SI(24);
    for (auto& l : g_infoCodeLines) {
        DrawT(l.c_str(), FMono(SI(10)), RectF({ lx, y, rx, y + SI(15) }), Theme::TextTertiary);
        y += SI(16);
    }
    g_rt->PopAxisAlignedClip();
}
// ---- v1.6.7 基准档案弹窗：查看已保存基准的型号清单 ----
// 背景：profiles.json 里同时存「型号键」(AUS|XG27ACDNG) 和「系列键」(AUS|XG)，
// 两者内容通常相同（SaveBaseline 双写）。直接罗列会让用户看到重复项、误以为存了两份。
// 这里只列型号键（含 srcModel 的），并标注它同时也是哪台当前显示器的基准。
struct ProfileRow {
    std::string key;        // 完整键，如 AUS|XG27ACDNG
    std::string model;      // 展示用型号名（优先 srcModel）
    int  codeCount = 0;     // 基准项数
    bool isCurrent = false; // 是否当前选中显示器的基准
    bool isSeries = false;  // 是否为系列键（跨型号兜底）
};
static bool g_pfOpen = false;
static bool g_pfShowSeries = false;   // v1.6.7：默认只看型号键（系列兜底是内部机制，默认不打扰）
static int  g_pfScroll = 0, g_pfMaxScroll = 0;
static RECT g_pfCard = {}, g_pfClose = {}, g_pfChkSeries = {};
static std::vector<ProfileRow> g_pfRows;
static int g_pfModelCount = 0, g_pfSeriesCount = 0;   // 分类计数，用于标题与复选框文案
static std::string g_pfCurrentKey;   // 当前显示器型号键，用于高亮

static void BuildProfileRows() {
    g_pfRows.clear();
    g_pfCurrentKey = g_cur ? MonitorKey(g_cur) : std::string();
    // 先收集所有「系列键」，用于区分型号键与系列兜底
    std::set<std::string> seriesKeys;
    for (auto& kv : g_profiles)
        if (kv.first == SeriesOfModelKey(kv.first)) seriesKeys.insert(kv.first);
    g_pfModelCount = g_pfSeriesCount = 0;
    for (auto& kv : g_profiles) {
        if (kv.second.vals.empty()) continue;
        bool isSeries = seriesKeys.count(kv.first) > 0;
        if (isSeries) g_pfSeriesCount++; else g_pfModelCount++;
        // 默认只显示型号键；开启「显示系列兜底」后才列出系列键
        if (isSeries && !g_pfShowSeries) continue;
        ProfileRow r;
        r.key = kv.first;
        r.model = kv.second.srcModel.empty() ? kv.first : kv.second.srcModel;
        r.codeCount = (int)kv.second.vals.size();
        r.isSeries = isSeries;
        // v1.6.7 修复：只有型号键精确命中才算「当前显示器」。
        // 原实现把系列键也算进去 → 当前显示器 XG27ACDNG 的系列键 AUS|XG 也被标成
        // 「当前显示器」，与它自身的「系列兜底」标记自相矛盾（截图实证）。
        r.isCurrent = !g_pfCurrentKey.empty() && kv.first == g_pfCurrentKey;
        g_pfRows.push_back(std::move(r));
    }
    // 排序：当前显示器优先，其次型号键优先于系列键，再按型号名
    std::sort(g_pfRows.begin(), g_pfRows.end(), [](const ProfileRow& a, const ProfileRow& b) {
        if (a.isCurrent != b.isCurrent) return a.isCurrent;
        if (a.isSeries != b.isSeries) return !a.isSeries;
        return a.model < b.model;
    });
    // 内容高度：每行 34px + 顶部说明 + 底部内边距
    g_pfMaxScroll = (std::max)(0, (int)g_pfRows.size() * SI(34) + SI(52) - (SI(520) - SI(48) - SI(36)));
}
static void OpenProfilePanel() {
    BuildProfileRows();
    g_pfOpen = true; g_pfScroll = 0;
}
static void PaintProfilePanel() {
    auto dim = Brush(Col(0.f, 0.f, 0.f, 0.62f));
    g_rt->FillRectangle(D2D1::RectF(0, 0, (float)g_W, (float)g_H), dim);
    dim->Release();
    RECT card = { (g_W - SI(520)) / 2, (g_H - SI(520)) / 2,
                  (g_W + SI(520)) / 2, (g_H + SI(520)) / 2 };
    g_pfCard = card;
    FillRRect(card, 12.f, Theme::SurfaceCard);
    { auto b = Brush(Theme::LineStrong);
      g_rt->DrawRoundedRectangle(RRect(card, 12.f), b, 1.f); b->Release(); }
    int lx = card.left + SI(24), rx = card.right - SI(24);
    char title[96];
    sprintf_s(title, "已保存的基准型号（%d）", g_pfModelCount);
    DrawT(title, FUi(SI(14), true),
          RectF({ lx, card.top + SI(14), rx - SI(30), card.top + SI(34) }), Theme::TextPrimary);
    g_pfClose = { card.right - SI(40), card.top + SI(12), card.right - SI(12), card.top + SI(36) };
    bool hc = Hover(g_pfClose);
    DrawT("✕", FUi(SI(12), false, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
          RectF(g_pfClose), hc ? Theme::TextPrimary : Theme::TextMuted);
    // v1.6.7：系列兜底开关（默认关）——系列键是内部兜底机制，与型号键成对出现会让列表翻倍，
    // 默认只列真正"保存过的型号"，需要排查跨型号还原时再勾开。
    {
        char lbl[96];
        sprintf_s(lbl, "显示系列兜底（%d 项）", g_pfSeriesCount);
        g_pfChkSeries = { lx, card.top + SI(38), lx + SI(16) + SI(6) + (int)MeasureW(lbl, FUi(SI(11))), card.top + SI(54) };
        RECT box = { g_pfChkSeries.left, g_pfChkSeries.top + SI(3), g_pfChkSeries.left + SI(14), g_pfChkSeries.top + SI(17) };
        bool hov = Hover(g_pfChkSeries);
        FillRRect(box, 4.f, g_pfShowSeries ? Theme::Accent : hov ? Theme::SurfaceHover : Theme::SurfaceSub);
        if (g_pfShowSeries) {
            auto b = Brush(Theme::OnAccent);
            g_rt->DrawLine(D2D1::Point2F((float)box.left + 3.f, (float)box.top + 8.f),
                           D2D1::Point2F((float)box.left + 6.f, (float)box.top + 11.f), b, 1.7f);
            g_rt->DrawLine(D2D1::Point2F((float)box.left + 6.f, (float)box.top + 11.f),
                           D2D1::Point2F((float)box.left + 11.f, (float)box.top + 4.f), b, 1.7f);
            b->Release();
        } else {
            auto b = Brush(Theme::LineOutline);
            g_rt->DrawRoundedRectangle(RRect(box, 4.f), b, 1.f); b->Release();
        }
        DrawT(lbl, FUi(SI(11)), RectF({ box.right + SI(6), g_pfChkSeries.top, g_pfChkSeries.right, g_pfChkSeries.bottom }),
              Theme::TextMuted);
    }

    D2D1_RECT_F clip = D2D1::RectF((float)card.left + 1, (float)card.top + SI(58),
                                   (float)card.right - 1, (float)card.bottom - SI(12));
    g_rt->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    int y = card.top + SI(68) - g_pfScroll;
    if (g_pfRows.empty()) {
        DrawT(g_pfModelCount == 0
                  ? "还没有保存过基准。在参数设置页调好显示器，按 Ctrl+Alt+S 保存。"
                  : "没有可显示的型号。勾选上方「显示系列兜底」查看系列键。",
              FUi(SI(11)), RectF({ lx, y, rx, y + SI(40) }), Theme::TextMuted);
    }
    for (auto& r : g_pfRows) {
        RECT rowR = { lx, y, rx, y + SI(30) };
        if (r.isCurrent) {
            FillRRect(rowR, 6.f, Col(Theme::Accent.r, Theme::Accent.g, Theme::Accent.b, 0.10f));
        }
        // 型号名
        std::string name = r.model;
        if (name.empty()) name = r.key;
        Col nameC = r.isCurrent ? Theme::Accent : Theme::TextPrimary;
        DrawT(name.c_str(), FUi(SI(12), r.isCurrent, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
              RectF({ rowR.left + (r.isCurrent ? SI(8) : 0), y, rowR.left + SI(220), y + SI(18) }), nameC);
        // 右侧：项数 + 标记
        std::string meta = std::to_string(r.codeCount) + " 项";
        if (r.isSeries) meta += "  · 系列兜底";
        if (r.isCurrent) meta += "  · 当前显示器";
        DrawT(meta.c_str(), FUi(SI(10), false, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
              RectF({ rowR.right - SI(220), y, rowR.right, y + SI(18) }), Theme::TextTertiary);
        // 第二行：完整键（序列号已移除——它不参与基准匹配，展示出来会误导成"按序列号区分"）
        DrawT(r.key.c_str(), FMono(SI(10)),
              RectF({ rowR.left + (r.isCurrent ? SI(8) : 0), y + SI(16), rowR.right, y + SI(30) }),
              Theme::TextMuted);
        y += SI(34);
    }
    g_rt->PopAxisAlignedClip();
    // 底部提示（内容超屏时）
    if (g_pfMaxScroll > 0) {
        DrawT("滚轮可滚动查看全部", FUi(SI(10), false, DWRITE_TEXT_ALIGNMENT_CENTER),
              RectF({ card.left, card.bottom - SI(26), card.right, card.bottom - SI(10) }),
              Theme::TextMuted);
    }
}
// ---- 自绘下拉面板（替代系统白菜单，与整体暗色 UI 统一）----
static const int kDdPad = 6, kDdItemH = 24, kDdMaxVis = 12;
static void RebuildDdItems() {
    g_ddItems.clear(); g_ddIdx.clear();
    int count = g_ddMode == 1 ? (int)g_resOpts.size() : (int)g_hzOpts.size();
    int pad = SI(kDdPad), itemH = SI(kDdItemH);
    int vis = (g_ddCard.bottom - g_ddCard.top - pad * 2) / itemH;
    for (int k = 0; k < vis; k++) {
        int idx = k + g_ddScroll;
        if (idx >= count) break;
        g_ddIdx.push_back(idx);
        g_ddItems.push_back({ g_ddCard.left + SI(4), g_ddCard.top + pad + k * itemH,
                              g_ddCard.right - SI(4), g_ddCard.top + pad + (k + 1) * itemH });
    }
}
static void OpenDisplayDropdown(HWND h, int mode, const RECT& anchor) {
    g_ddMode = mode; g_ddScroll = 0;
    int count = mode == 1 ? (int)g_resOpts.size() : (int)g_hzOpts.size();
    if (count <= 0) { g_ddMode = 0; return; }
    int pad = SI(kDdPad), itemH = SI(kDdItemH);
    int availBelow = (g_H - StatusBarH()) - (anchor.bottom + SI(4));
    int availAbove = anchor.top - SI(40);
    RECT card;
    int vis;
    if (availBelow >= itemH + pad * 2) {
        vis = (std::min)(count, (std::min)(kDdMaxVis, (availBelow - pad * 2) / itemH));
        card = { anchor.left, anchor.bottom + SI(4), anchor.right,
                 anchor.bottom + SI(4) + pad * 2 + vis * itemH };
    } else {
        vis = (std::min)(count, (std::min)(kDdMaxVis, (availAbove - pad * 2) / itemH));
        if (vis <= 0) { g_ddMode = 0; return; }
        card = { anchor.left, anchor.top - SI(4) - pad * 2 - vis * itemH, anchor.right, anchor.top - SI(4) };
    }
    g_ddCard = card;
    RebuildDdItems();
    Invalidate();
}
static void PaintDisplayDropdown() {
    FillRRect(g_ddCard, 8.f, Theme::SurfaceCard);
    { auto b = Brush(Theme::LineStrong);
      g_rt->DrawRoundedRectangle(RRect(g_ddCard, 8.f), b, 1.f); b->Release(); }
    for (size_t i = 0; i < g_ddItems.size(); i++) {
        const RECT& ir = g_ddItems[i];
        int idx = g_ddIdx[i];
        bool isCur = false;
        wchar_t txt[48];
        if (g_ddMode == 1) {
            auto& o = g_resOpts[idx];
            isCur = o.w == g_curResW && o.h == g_curResH;
            swprintf_s(txt, L"%d×%d", o.w, o.h);
        } else {
            isCur = g_hzOpts[idx] == g_curHz;
            swprintf_s(txt, L"%d Hz", g_hzOpts[idx]);
        }
        if (PtIn(ir, g_mouse)) FillRRect(ir, 5.f, Theme::SurfaceHover);
        if (isCur) {
            RECT dot = {};
            dot.left = ir.left + SI(4); dot.right = dot.left + SI(5);
            dot.top = (ir.top + ir.bottom) / 2 - SI(2); dot.bottom = dot.top + SI(5);
            FillRRect(dot, 2.5f, Theme::Accent);
        }
        DrawT(WideToUtf8(txt).c_str(), FUi(SI(11), false, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
              RectF({ ir.left + SI(14), ir.top, ir.right - SI(8), ir.bottom }),
              isCur ? Theme::Accent : Theme::TextPrimary);
    }
}
static void PaintContent() {
    for (auto& G : g_menu) {
        if ((g_page == 0 && strcmp(G.en, "SYSTEM") == 0) ||
            (g_page == 1 && strcmp(G.en, "SYSTEM") != 0) ||
            g_page >= 2) continue;   // v1.6.8：关于页不画菜单卡片
        if (G.cardR.right <= 0) continue;   // 未布局（防御）
        FillRRect(G.cardR, 12.f, Theme::SurfaceGroup);
        for (auto& it : G.items)
            if (it.visible) PaintRow(&it);
    }
}
// ================= 系统管理页（显示器维护 / 校准向导 / 软件设置）=================
// v1.2：按需求移除「基准与还原」卡（自动还原策略仍留在 config.json/后台，UI 不再露出）。
struct SysUi {
    RECT cardMaint, cardCal, cardSoft;             // （v1.6.12：cardUpd 在线更新卡已删——更新/反馈入口移至「关于」页签）
    RECT btnWake, btnReset, btnTest;               // 显示器维护
    std::vector<RECT> segLock;                     // OSD 锁定分段
    RECT btnCal;                                   // 校准向导
    RECT chkBoot, chkTray, capFollow;              // 软件设置
    RECT btnProfiles;                              // v1.6.7 已保存基准型号
    std::vector<RECT> swatches;                    // 强调色块
};
static SysUi g_sys;
static bool g_confirmReset = false;        // 恢复出厂确认卡（程序内样式，不用系统 MessageBox）
static RECT g_confirmYes = {}, g_confirmNo = {};
static const struct { const char* label; unsigned v; } kLockOpts[] = {
    { "正常", 0x02 }, { "锁菜单", 0x01 }, { "全锁", 0x03 },
};
static const char*   kAccentOpts[] = { "#F04438", "#F5B913", "#22D3EE", "#34D399", "#A78BFA" };

static void LayoutSystem() {
    int x = SI(196), cw = g_contentW;
    int lx = SI(230);
    int y = SI(76);
    const int gap = SI(20);

    // ---- 卡2：显示器维护（恢复出厂设置 + 显示器测试 + 基准档案）----
    int h2 = SI(96);
    g_sys.cardMaint = { x, y, x + cw, y + h2 };
    {
        int by = y + SI(46);
        int bx = lx;
        int rw = (std::max)(SI(112), (int)MeasureW("恢复出厂设置", FUi(SI(12), true)) + SI(28));
        g_sys.btnReset = { bx, by, bx + rw, by + SI(30) };
        int tw = (std::max)(SI(112), (int)MeasureW("显示器测试", FUi(SI(12), true)) + SI(28));
        g_sys.btnTest = { g_sys.btnReset.right + SI(16), by, g_sys.btnReset.right + SI(16) + tw, by + SI(30) };
        // v1.6.7：查看已保存基准的型号清单
        int pw = (std::max)(SI(128), (int)MeasureW("基准档案", FUi(SI(12), true)) + SI(28));
        g_sys.btnProfiles = { g_sys.btnTest.right + SI(16), by, g_sys.btnTest.right + SI(16) + pw, by + SI(30) };
    }
    y += h2 + gap;

    // ---- 卡3：校准向导 ----
    int h3 = SI(72);
    g_sys.cardCal = { x, y, x + cw, y + h3 };
    g_sys.btnCal = { x + cw - SI(14) - SI(112), y + SI(38), x + cw - SI(14), y + SI(38) + SI(30) };
    y += h3 + gap;

    // ---- 卡4：软件设置 ----
    int h4 = SI(128);
    g_sys.cardSoft = { x, y, x + cw, y + h4 };
    {
        int by = y + SI(38);
        int bx = lx;
        auto place = [&](RECT& hit, const char* label, int minW) {
            int tw = (std::max)(minW, (int)MeasureW(label, FUi(SI(12))));
            hit = { bx, by - SI(5), bx + SI(16) + SI(6) + tw, by + SI(16) + SI(5) };
            bx = hit.right + SI(28);
        };
        place(g_sys.chkBoot,  "开机自启", SI(64));
        place(g_sys.chkTray, "最小化到托盘", SI(88));
        int sy = y + SI(92);
        bx = lx;
        int capW = (int)MeasureW("跟随品牌", FUi(SI(11))) + SI(20);
        g_sys.capFollow = { bx, sy, bx + capW, sy + SI(22) };
        bx += capW + SI(12);
        g_sys.swatches.clear();
        for (auto hex : kAccentOpts) {
            g_sys.swatches.push_back({ bx, sy, bx + SI(20), sy + SI(20) });
            bx += SI(20) + SI(12);
        }
    }
    y += h4 + gap;
    // v1.6.12：原「卡5：在线更新 / 意见反馈」已删除——更新与反馈入口全部收拢到
    // 主窗口「关于」页签（更新中心内联 + 反馈表单弹窗），此处不再保留重复入口。
}
static void DrawChk(const RECT& hit, bool on, const char* label) {
    RECT box = { hit.left, hit.top + SI(5), hit.left + SI(16), hit.top + SI(21) };
    bool hov = Hover(hit);
    Col bg = on ? Theme::Accent : hov ? Theme::SurfaceHover : Theme::SurfaceSub;
    FillRRect(box, 4.f, bg);
    if (on) {
        // 对勾用折线画（子集字体未必含 ✓ 字形）
        auto b = Brush(Theme::OnAccent);
        g_rt->DrawLine(D2D1::Point2F((float)box.left + 3.5f, (float)box.top + 9.f),
                       D2D1::Point2F((float)box.left + 7.f, (float)box.top + 12.5f), b, 1.8f);
        g_rt->DrawLine(D2D1::Point2F((float)box.left + 7.f, (float)box.top + 12.5f),
                       D2D1::Point2F((float)box.left + 12.5f, (float)box.top + 4.f), b, 1.8f);
        b->Release();
    } else {
        auto b = Brush(Theme::LineOutline);
        g_rt->DrawRoundedRectangle(RRect(box, 4.f), b, 1.f);
        b->Release();
    }
    DrawT(label, FUi(SI(12)), RectF({ box.right + SI(6), hit.top, hit.right + SI(260), hit.bottom }),
          Theme::TextSecondary);
}
static void DrawBtn(const RECT& r, const char* label, bool danger) {
    bool hov = Hover(r);
    bool prs = Press(r);
    Col bg, fg;
    if (danger) {
        bg = prs ? Col(Theme::Danger.r, Theme::Danger.g, Theme::Danger.b, 0.22f)
                 : hov ? Col(Theme::Danger.r, Theme::Danger.g, Theme::Danger.b, 0.12f)
                       : Theme::SurfaceSub;
        fg = Theme::Danger;
    } else {
        bg = prs ? Theme::SurfaceHover.Dark(0.06f) : hov ? Theme::SurfaceHover : Theme::SurfaceSub;
        fg = Theme::TextPrimary;
    }
    FillRRect(r, 8.f, bg);
    if (danger) {
        auto b = Brush(Theme::Danger);
        g_rt->DrawRoundedRectangle(RRect(r, 8.f), b, 1.f);
        b->Release();
    }
    DrawT(label, FUi(SI(12), true, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
          RectF(r), fg);
}
static void PaintSystemPage() {
    // ---- 卡1：显示器维护（只保留恢复出厂设置）----
    {
        const RECT& c = g_sys.cardMaint;
        FillRRect(c, 12.f, Theme::SurfaceGroup);
        DrawT("显示器维护", FUi(SI(12), true),
              RectF({ c.left + SI(14), c.top + SI(8), c.right - SI(14), c.top + SI(28) }), Theme::TextPrimary);
        DrawBtn(g_sys.btnReset, "恢复出厂设置", true);    // 危险红：破坏性操作，与普通按钮明确区分
        DrawBtn(g_sys.btnTest, "显示器测试", false);      // 中性样式：只读检测操作
        DrawBtn(g_sys.btnProfiles, "基准档案", false);    // v1.6.7：查看已保存基准的型号清单
    }
    // ---- 卡3：校准向导 ----
    {
        const RECT& c = g_sys.cardCal;
        FillRRect(c, 12.f, Theme::SurfaceGroup);
        DrawT("校准向导", FUi(SI(12), true),
              RectF({ c.left + SI(14), c.top + SI(8), c.right - SI(14), c.top + SI(28) }), Theme::TextPrimary);
        D2D1_ELLIPSE dot = D2D1::Ellipse(D2D1::Point2F((float)(c.left + SI(38)), (float)(c.top + SI(52))), 3.5f, 3.5f);
        auto db = Brush(Theme::Success);
        g_rt->FillEllipse(dot, db); db->Release();
        DrawT("差分校准已就绪 — 遇到置灰项？去 OSD 改一项设置，程序自动识别对应 DDC 码并写入 vcpmap.json",
              FUi(SI(11)), RectF({ c.left + SI(50), c.top + SI(40), g_sys.btnCal.left - SI(14), c.top + SI(64) }),
              Theme::TextTertiary);
        DrawBtn(g_sys.btnCal, "打开向导", false);
    }
    // ---- 卡4：软件设置 ----
    {        const RECT& c = g_sys.cardSoft;
        FillRRect(c, 12.f, Theme::SurfaceGroup);
        DrawT("软件设置", FUi(SI(12), true),
              RectF({ c.left + SI(14), c.top + SI(8), c.right - SI(14), c.top + SI(28) }), Theme::TextPrimary);
        DrawChk(g_sys.chkBoot,  g_config.autoStart,      "开机自启");
        DrawChk(g_sys.chkTray, g_config.minimizeToTray,  "最小化到托盘");
        DrawT("强调色", FUi(SI(11)),
              RectF({ g_sys.capFollow.left, g_sys.capFollow.top - SI(20), g_sys.capFollow.left + SI(60), g_sys.capFollow.top - SI(4) }),
              Theme::TextMuted);
        bool follow = g_config.accentMode != "manual";
        Col cbg = follow ? Theme::Accent : Hover(g_sys.capFollow) ? Theme::SurfaceHover : Theme::SurfaceSub;
        FillRRect(g_sys.capFollow, 6.f, cbg);
        DrawT("跟随品牌", FUi(SI(11), follow, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
              RectF(g_sys.capFollow), follow ? Theme::OnAccent : Theme::TextTertiary);
        for (size_t i = 0; i < g_sys.swatches.size(); i++) {
            const RECT& sw = g_sys.swatches[i];
            bool on = !follow && !_stricmp(g_config.manualAccent.c_str(), kAccentOpts[i]);
            FillRRect(sw, 6.f, Col::Hex(kAccentOpts[i]));
            if (on || Hover(sw)) {
                RECT inner = { sw.left + 2, sw.top + 2, sw.right - 2, sw.bottom - 2 };
                auto b = Brush(on ? Theme::TextPrimary : Theme::TextMuted);
                g_rt->DrawRoundedRectangle(RRect(inner, 5.f), b, 1.4f);
                b->Release();
            }
        }
    }
    // v1.6.12：原「卡5：在线更新 / 意见反馈」绘制已删除（布局同步移除），
    // 更新与反馈入口收拢到主窗口「关于」页签。
}
// ================= 在线更新 / 反馈（v1.3） =================
// 协议契约（服务器：宝塔 + Nginx + PHP/静态，详见 docs/update-feedback-server.md）
//   GET  {base}/{channel}.json                        更新清单：{"version":"1.3.0","url":"...","sha256":"...","size":n,"force":false,"notes":["..."]}
//   POST {base}/feedback.php  (JSON)                  反馈：{...,"hash":"HMAC-SHA256(body, salt)","markdown"...}
//   下载 exe → 校验 SHA-256 → %TEMP% 写出 → 提权换名 (.old/.new) → 重启
// 约束：全程 WinHTTP（读取 IE 代理设置），无第三方依赖；/MT 单文件不变。
static const char* const kDefaultUpdateBase = "https://osd.test.com/api";   // 上线前改成你的面板域名+路径
static const char* const kFeedbackSalt      = "change-me-feedback-salt";    // 必须与 feedback.php 一致
#ifndef APP_VERSION
#define APP_VERSION "1.6.12"
#endif
static const char* const kAppVersion        = APP_VERSION;   // P0-2：build.bat 可注入版本号

// ---------- SHA-256 / HMAC / Base64（自实现，避免依赖 libcrypto） ----------
struct Sha256 {
    unsigned int  h[8];
    unsigned char buf[64];
    unsigned long long len = 0;
    int  n = 0;
    static unsigned int ror(unsigned int x, int r) { return (x >> r) | (x << (32 - r)); }
    void init() {
        static const unsigned int iv[8] = { 0x6a09e667u,0xbb67ae85u,0x3c6ef372u,0xa54ff53au,
                                            0x510e527fu,0x9b05688cu,0x1f83d9abu,0x5be0cd19u };
        memcpy(h, iv, sizeof iv); len = 0; n = 0;
    }
    void block(const unsigned char* p) {
        static const unsigned int K[64] = {
            0x428a2f98u,0x71374491u,0xb5c0fbcfu,0xe9b5dba5u,0x3956c25bu,0x59f111f1u,0x923f82a4u,0xab1c5ed5u,
            0xd807aa98u,0x12835b01u,0x243185beu,0x550c7dc3u,0x72be5d74u,0x80deb1feu,0x9bdc06a7u,0xc19bf174u,
            0xe49b69c1u,0xefbe4786u,0x0fc19dc6u,0x240ca1ccu,0x2de92c6fu,0x4a7484aau,0x5cb0a9dcu,0x76f988dau,
            0x983e5152u,0xa831c66du,0xb00327c8u,0xbf597fc7u,0xc6e00bf3u,0xd5a79147u,0x06ca6351u,0x14292967u,
            0x27b70a85u,0x2e1b2138u,0x4d2c6dfcu,0x53380d13u,0x650a7354u,0x766a0abbu,0x81c2c92eu,0x92722c85u,
            0xa2bfe8a1u,0xa81a664bu,0xc24b8b70u,0xc76c51a3u,0xd192e819u,0xd6990624u,0xf40e3585u,0x106aa070u,
            0x19a4c116u,0x1e376c08u,0x2748774cu,0x34b0bcb5u,0x391c0cb3u,0x4ed8aa4au,0x5b9cca4fu,0x682e6ff3u,
            0x748f82eeu,0x78a5636fu,0x84c87814u,0x8cc70208u,0x90befffau,0xa4506cebu,0xbef9a3f7u,0xc67178f2u };
        unsigned int w[64];
        for (int i = 0; i < 16; i++)
            w[i] = ((unsigned int)p[i * 4] << 24) | ((unsigned int)p[i * 4 + 1] << 16) |
                   ((unsigned int)p[i * 4 + 2] << 8) | (unsigned int)p[i * 4 + 3];
        for (int i = 16; i < 64; i++) {
            unsigned int s0 = ror(w[i - 15], 7) ^ ror(w[i - 15], 18) ^ (w[i - 15] >> 3);
            unsigned int s1 = ror(w[i - 2], 17) ^ ror(w[i - 2], 19) ^ (w[i - 2] >> 10);
            w[i] = w[i - 16] + s0 + w[i - 7] + s1;
        }
        unsigned int a = h[0], b = h[1], c = h[2], d = h[3], e = h[4], f = h[5], g = h[6], hh2 = h[7];
        for (int i = 0; i < 64; i++) {
            unsigned int S1 = ror(e, 6) ^ ror(e, 11) ^ ror(e, 25);
            unsigned int ch = (e & f) ^ (~e & g);
            unsigned int t1 = hh2 + S1 + ch + K[i] + w[i];
            unsigned int S0 = ror(a, 2) ^ ror(a, 13) ^ ror(a, 22);
            unsigned int mj = (a & b) ^ (a & c) ^ (b & c);
            unsigned int t2 = S0 + mj;
            hh2 = g; g = f; f = e; e = d + t1; d = c; c = b; b = a; a = t1 + t2;
        }
        h[0] += a; h[1] += b; h[2] += c; h[3] += d; h[4] += e; h[5] += f; h[6] += g; h[7] += hh2;
    }
    void update(const unsigned char* p, size_t n2) {
        len += n2;
        while (n2) {
            size_t k = 64 - (size_t)n; if (k > n2) k = n2;
            memcpy(buf + n, p, k); n += (int)k; p += k; n2 -= k;
            if (n == 64) { block(buf); n = 0; }
        }
    }
    void final(unsigned char out[32]) {
        unsigned long long bits = len * 8;
        unsigned char pad = 0x80; update(&pad, 1);
        unsigned char z = 0;
        while (n != 56) update(&z, 1);
        unsigned char L[8];
        for (int i = 0; i < 8; i++) L[i] = (unsigned char)(bits >> (56 - i * 8));
        update(L, 8);
        for (int i = 0; i < 8; i++) {
            out[i * 4]     = (unsigned char)(h[i] >> 24); out[i * 4 + 1] = (unsigned char)(h[i] >> 16);
            out[i * 4 + 2] = (unsigned char)(h[i] >> 8);  out[i * 4 + 3] = (unsigned char)h[i];
        }
    }
};
static std::string Hex32(const unsigned char* d, size_t n) {
    static const char* hx = "0123456789abcdef";
    std::string s; s.reserve(n * 2);
    for (size_t i = 0; i < n; i++) { s += hx[d[i] >> 4]; s += hx[d[i] & 15]; }
    return s;
}
static std::string Sha256Hex(const std::string& data) {
    Sha256 s; s.init(); s.update((const unsigned char*)data.data(), data.size());
    unsigned char d[32]; s.final(d); return Hex32(d, 32);
}
static std::string HmacSha256B64(const std::string& key, const std::string& msg) {
    unsigned char k[64] = { 0 };
    if (key.size() > 64) { Sha256 t; t.init(); t.update((const unsigned char*)key.data(), key.size());
                           unsigned char d[32]; t.final(d); memcpy(k, d, 32); }
    else memcpy(k, key.data(), key.size());
    std::string ip(64, 0), op(64, 0);
    for (int i = 0; i < 64; i++) { ip[i] = (char)(k[i] ^ 0x36); op[i] = (char)(k[i] ^ 0x5c); }
    Sha256 a; a.init(); a.update((const unsigned char*)ip.data(), 64);
    a.update((const unsigned char*)msg.data(), msg.size());
    unsigned char d1[32]; a.final(d1);
    Sha256 b; b.init(); b.update((const unsigned char*)op.data(), 64); b.update(d1, 32);
    unsigned char d2[32]; b.final(d2);
    static const char* b64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string out;
    for (int i = 0; i < 32; i += 3) {
        unsigned int v = (unsigned int)d2[i] << 16;
        if (i + 1 < 32) v |= (unsigned int)d2[i + 1] << 8;
        if (i + 2 < 32) v |= d2[i + 2];
        out += b64[(v >> 18) & 63]; out += b64[(v >> 12) & 63];
        out += (i + 1 < 32) ? b64[(v >> 6) & 63] : '=';
        out += (i + 2 < 32) ? b64[v & 63] : '=';
    }
    return out;
}

// ---------- WinHTTP ----------
struct HttpResp { int status = 0; std::string body; std::string err; };
static std::string UrlEncode(const std::string& s) {
    std::string o;
    for (unsigned char c : s) {
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') o += (char)c;
        else { char b[8]; sprintf_s(b, "%%%02X", c); o += b; }
    }
    return o;
}
// 拆 URL → 组件。WinHttpCrackUrl 直接可用，避免手写解析坑
static bool CrackUrl(const std::string& url, std::wstring& host, std::wstring& path,
                     INTERNET_PORT& port, bool& https, std::wstring& extra) {
    std::wstring w = Utf8ToWide(url);
    URL_COMPONENTS uc = { sizeof(uc) };
    wchar_t hbuf[256] = { 0 }, pbuf[2048] = { 0 }, ebuf[1024] = { 0 };
    uc.lpszHostName = hbuf; uc.dwHostNameLength = 255;
    uc.lpszUrlPath = pbuf;  uc.dwUrlPathLength = 2047;
    uc.lpszExtraInfo = ebuf; uc.dwExtraInfoLength = 1023;
    if (!WinHttpCrackUrl(w.c_str(), (DWORD)w.size(), 0, &uc)) return false;
    host = hbuf; path = pbuf; port = uc.nPort; https = (uc.nScheme == INTERNET_SCHEME_HTTPS);
    extra = ebuf;
    std::wstring q = ebuf;
    if (!q.empty()) path += q;
    return true;
}
static bool g_netReady = false;
// 解析环境变量 http_proxy / https_proxy / no_proxy（网吧与内网部署常用）
static void NetReadEnvProxy(WINHTTP_PROXY_INFO& pi, std::wstring& holderA, std::wstring& holderB) {
    auto getenvU8 = [](const char* n) -> std::string {
        char buf[1024] = { 0 }; DWORD n2 = GetEnvironmentVariableA(n, buf, sizeof(buf));
        return n2 > 0 && n2 < sizeof(buf) ? std::string(buf) : std::string();
    };
    std::string p = getenvU8("https_proxy");
    if (p.empty()) p = getenvU8("HTTPS_PROXY");
    if (p.empty()) p = getenvU8("http_proxy");
    if (p.empty()) p = getenvU8("HTTP_PROXY");
    std::string no = getenvU8("no_proxy");
    if (no.empty()) no = getenvU8("NO_PROXY");
    if (p.empty()) return;
    // http://host:port → host:port（WinHTTP 只认后者）
    size_t sp = p.find("://");
    if (sp != std::string::npos) p = p.substr(sp + 3);
    while (!p.empty() && p.back() == '/') p.pop_back();
    holderA = Utf8ToWide(p);
    holderB = Utf8ToWide(no);
    pi.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
    pi.lpszProxy = (LPWSTR)holderA.c_str();
    pi.lpszProxyBypass = holderB.empty() ? nullptr : (LPWSTR)holderB.c_str();
}
static void NetInit() {
    if (g_netReady) return;
    g_netReady = true;
    WINHTTP_PROXY_INFO pi = {};
    std::wstring ha, hb;
    NetReadEnvProxy(pi, ha, hb);
    if (!pi.lpszProxy) {
        // 环境变量优先，其次系统/IE 代理
        WINHTTP_CURRENT_USER_IE_PROXY_CONFIG pc = {};
        if (WinHttpGetIEProxyConfigForCurrentUser(&pc) && pc.lpszProxy) {
            pi.dwAccessType = WINHTTP_ACCESS_TYPE_NAMED_PROXY;
            pi.lpszProxy = pc.lpszProxy;
            pi.lpszProxyBypass = pc.lpszProxyBypass;
            WinHttpSetDefaultProxyConfiguration(&pi);
        }
        if (pc.lpszProxy) GlobalFree(pc.lpszProxy);
        if (pc.lpszProxyBypass) GlobalFree(pc.lpszProxyBypass);
        if (pc.lpszAutoConfigUrl) GlobalFree(pc.lpszAutoConfigUrl);
        return;
    }
    WinHttpSetDefaultProxyConfiguration(&pi);
}
static HttpResp HttpCall(const std::string& url, const std::string& postBody, bool isPost) {
    HttpResp r;
    NetInit();
    std::wstring host, path; INTERNET_PORT port = 0; bool https = false; std::wstring extra;
    if (!CrackUrl(url, host, path, port, https, extra)) { r.err = "URL 格式错误"; return r; }
    HINTERNET ses = WinHttpOpen(L"MonitorOSD/1.5.3", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    if (!ses) { r.err = "网络初始化失败"; return r; }
    WinHttpSetTimeouts(ses, 4000, 4000, 8000, 15000);
    HINTERNET con = WinHttpConnect(ses, host.c_str(), port, 0);
    if (!con) { r.err = "DNS/域名解析失败"; WinHttpCloseHandle(ses); return r; }
    DWORD fl = https ? WINHTTP_FLAG_SECURE : 0;
    HINTERNET req = WinHttpOpenRequest(con, isPost ? L"POST" : L"GET", path.c_str(),
                                       nullptr, WINHTTP_NO_REFERER,
                                       WINHTTP_DEFAULT_ACCEPT_TYPES, fl);
    if (!req) { r.err = "HTTP 请求创建失败"; WinHttpCloseHandle(con); WinHttpCloseHandle(ses); return r; }
    // POST 必须显式声明 Content-Length 才能在 SendRequest 之后 WriteData；
    // 依赖 lpOptional 自动算长度会返回 87（实测该环境下必现），故一律显式给头。
    std::wstring hdr = L"Content-Type: application/json; charset=utf-8\r\nX-Osd-Version: " + Utf8ToWide(kAppVersion);
    if (isPost) hdr += L"\r\nContent-Length: " + std::to_wstring(postBody.size());
    hdr += L"\r\n";
    BOOL ok = WinHttpSendRequest(req, hdr.c_str(), (DWORD)-1L, WINHTTP_NO_REQUEST_DATA, 0, 0, 0);
    if (ok && isPost && !postBody.empty()) {
        DWORD written = 0;
        ok = WinHttpWriteData(req, postBody.data(), (DWORD)postBody.size(), &written);
    }
    if (ok) ok = WinHttpReceiveResponse(req, nullptr);
    if (ok) {
        DWORD st = 0, sz = sizeof(st);
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &st, &sz, WINHTTP_NO_HEADER_INDEX);
        r.status = (int)st;
        char buf[4096];
        for (;;) {
            DWORD got = 0;
            if (!WinHttpReadData(req, buf, sizeof(buf), &got) || got == 0) break;
            r.body.append(buf, got);
            if (r.body.size() > 1024u * 1024u) break;   // 防御：清单/回执不该超过 1MB
        }
    } else {
        DWORD e = GetLastError();
        if (GetEnvironmentVariableA("OSD_NET_DEBUG", nullptr, 0))
            LogLine("http err step=" + std::to_string(isPost ? 2 : 1) + " code=" + std::to_string(e) + " url=" + url);
        r.err = "网络连接失败（" + std::to_string(e) + "）";
    }
    WinHttpCloseHandle(req); WinHttpCloseHandle(con); WinHttpCloseHandle(ses);
    return r;
}
// 下载到文件（幂等：调用方给目标路径），返回 false 时 err 有值
static bool HttpDownload(const std::string& url, const std::string& outPath, std::string& err,
                         void (*progress)(int)) {
    NetInit();
    std::wstring host, path; INTERNET_PORT port = 0; bool https = false; std::wstring extra;
    if (!CrackUrl(url, host, path, port, https, extra)) { err = "下载地址格式错误"; return false; }
    HINTERNET ses = WinHttpOpen(L"MonitorOSD/1.5.3", WINHTTP_ACCESS_TYPE_DEFAULT_PROXY,
                                WINHTTP_NO_PROXY_NAME, WINHTTP_NO_PROXY_BYPASS, 0);
    // v1.5.3 P2-1 修复：下载路径原本一个超时都没设（默认 INFINITE），DNS 卡住时线程永久挂起，
    // 界面卡在"下载中"只能杀进程。解析/连接各 10s，收发 15s，整体给 60s 上限。
    if (ses) WinHttpSetTimeouts(ses, 10000, 10000, 15000, 60000);
    HINTERNET con = ses ? WinHttpConnect(ses, host.c_str(), port, 0) : nullptr;
    HINTERNET req = con ? WinHttpOpenRequest(con, L"GET", path.c_str(), nullptr, WINHTTP_NO_REFERER,
                                             WINHTTP_DEFAULT_ACCEPT_TYPES, https ? WINHTTP_FLAG_SECURE : 0) : nullptr;
    bool ok = req && WinHttpSendRequest(req, WINHTTP_NO_ADDITIONAL_HEADERS, 0, WINHTTP_NO_REQUEST_DATA, 0, 0, 0)
              && WinHttpReceiveResponse(req, nullptr);
    DWORD st = 0, sz = sizeof(st);
    if (ok) {
        WinHttpQueryHeaders(req, WINHTTP_QUERY_STATUS_CODE | WINHTTP_QUERY_FLAG_NUMBER,
                            WINHTTP_HEADER_NAME_BY_INDEX, &st, &sz, WINHTTP_NO_HEADER_INDEX);
        if (st != 200) { ok = false; err = "服务器返回 " + std::to_string(st); }
    } else if (err.empty()) err = "下载连接失败（" + std::to_string(GetLastError()) + "）";
    FILE* f = ok ? FopenUtf8((char*)outPath.c_str(), "wb") : nullptr;
    if (ok && !f) { ok = false; err = "无法写入临时文件"; }
    if (ok) {
        char buf[65536]; DWORD got = 0; long long tot = 0;
        for (;;) {
            if (!WinHttpReadData(req, buf, sizeof(buf), &got) || got == 0) break;
            fwrite(buf, 1, got, f); tot += got;
            if (tot > 128ll * 1024 * 1024) { ok = false; err = "文件异常过大，已中止"; break; }
        }
        fclose(f);
        if (ok && tot < 1024) { ok = false; err = "下载内容为空"; }
        if (progress) progress(100);
    }
    if (req) WinHttpCloseHandle(req);
    if (con) WinHttpCloseHandle(con);
    if (ses) WinHttpCloseHandle(ses);
    return ok;
}

// ---------- 版本比较 / 更新清单 ----------
static std::vector<int> VerNums(const std::string& v) {
    std::vector<int> r; int cur = 0; bool has = false;
    for (char c : v) {
        if (isdigit((unsigned char)c)) { cur = cur * 10 + (c - '0'); has = true; }
        else if (c == '.') { if (has) r.push_back(cur); cur = 0; has = false; }
        else break;
    }
    if (has) r.push_back(cur);
    return r;
}
static int VerCmp(const std::string& a, const std::string& b) {
    auto x = VerNums(a), y = VerNums(b);
    size_t n = (std::max)(x.size(), y.size());
    for (size_t i = 0; i < n; i++) {
        int p = i < x.size() ? x[i] : 0, q = i < y.size() ? y[i] : 0;
        if (p != q) return p < q ? -1 : 1;
    }
    return 0;
}
static std::string JsonStr(const std::string& j, const char* key) {
    std::string k = std::string("\"") + key + "\"";
    size_t p = j.find(k);
    if (p == std::string::npos) return "";
    p = j.find(':', p + k.size());
    if (p == std::string::npos) return "";
    p = j.find('"', p + 1);
    if (p == std::string::npos) return "";
    std::string o;
    for (size_t i = p + 1; i < j.size(); i++) {
        if (j[i] == '\\' && i + 1 < j.size()) {
            char n = j[++i];
            o += (n == 'n') ? '\n' : (n == 't') ? '\t' : n;
        } else if (j[i] == '"') break;
        else o += j[i];
    }
    return o;
}
static bool ParseManifest(const std::string& j, UpdateInfo& ui) {
    ui.version = JsonStr(j, "version");
    if (ui.version.empty()) return false;
    ui.url    = JsonStr(j, "url");
    ui.sha256 = ToLowerAscii(JsonStr(j, "sha256"));
    size_t p = j.find("\"size\"");
    if (p != std::string::npos) { p = j.find(':', p); if (p != std::string::npos) ui.size = _atoi64(j.c_str() + p + 1); }
    p = j.find("\"force\"");
    if (p != std::string::npos) {
        p = j.find(':', p);
        if (p != std::string::npos) {
            size_t q = j.find_first_not_of(" \t\r\n", p + 1);
            ui.force = (q != std::string::npos && j.compare(q, 4, "true") == 0);
        }
    }
    // notes 数组
    size_t a = j.find("\"notes\"");
    if (a != std::string::npos) {
        a = j.find('[', a);
        size_t e = a == std::string::npos ? std::string::npos : j.find(']', a);
        if (a != std::string::npos && e != std::string::npos) {
            size_t i = a;
            while ((i = j.find('"', i + 1)) != std::string::npos && i < e) {
                std::string s; size_t k = i + 1;
                for (; k < e && j[k] != '"'; k++) {
                    if (j[k] == '\\' && k + 1 < e) { char n = j[++k]; s += (n == 'n') ? '\n' : n; }
                    else s += j[k];
                }
                if (!s.empty()) ui.notes.push_back(s);
                i = k;
            }
        }
    }
    return true;
}
static std::string UpdateBase() {
    std::string b = g_config.updateBase;
    while (!b.empty() && b.back() == '/') b.pop_back();
    return b.empty() ? std::string(kDefaultUpdateBase) : b;
}
// v1.5.7：config.json 未配置 UpdateBase（用的是内置占位域名）时返回 true。
// 交付态下 UpdateBase 通常是空的，原来会去连 osd.test.com 然后报"网络初始化失败"，
// 网管根本不知道是"没配服务器"还是"网络坏了"。调用方据此给明确提示。
static bool UpdateBaseUnconfigured() {
    return g_config.updateBase.empty();
}

// 更新窗口状态机（UpdateUi 结构见前向声明区）
UpdateUi g_upd;
static std::string g_fbText;
static int         g_fbType = 0;            // 0=问题 1=建议 2=其他
static const char* kFbTypes[] = { "意见反馈", "功能建议", "其他" };   // v1.5.8 起不再让用户选，统一记「意见反馈」

static void UpdPost(int state, const std::string& msg) {
    EnterCriticalSection(&g_netCs);
    g_upd.state = state;
    if (!msg.empty()) g_upd.msg = msg;
    LeaveCriticalSection(&g_netCs);
    if (g_upd_hwnd) PostMessageW(g_upd_hwnd, WM_APP + 20, 0, 0);
}
static void CheckUpdateAsync(bool userTriggered) {
    EnterCriticalSection(&g_netCs);
    if (g_upd.state == UPD_CHECKING || g_upd.state == UPD_DOWNLOADING) { LeaveCriticalSection(&g_netCs); return; }
    g_upd.state = UPD_CHECKING;
    g_upd.msg = "正在连接更新服务器…";
    LeaveCriticalSection(&g_netCs);
    if (g_upd_hwnd) PostMessageW(g_upd_hwnd, WM_APP + 20, 0, 0);
    // v1.5.7：没配更新地址时直接给明确结论，不去连占位域名再报"网络失败"
    if (UpdateBaseUnconfigured()) {
        UpdPost(UPD_IDLE, "未配置更新服务器（config.json 的 UpdateBase 为空），本机不走在线更新。\n"
                          "如已有更新服务器，把地址填到 UpdateBase 后重启程序即可。");
        return;
    }
    std::thread([userTriggered]() {
        std::string base = UpdateBase();
        std::string ch = g_config.updateChannel.empty() ? "latest" : g_config.updateChannel;
        HttpResp r = HttpCall(base + "/" + ch + ".json", "", false);
        UpdateInfo ui;
        if (!r.err.empty())          { UpdPost(UPD_ERROR, r.err + "\n检查更新地址：" + base); return; }
        if (r.status == 404)         { UpdPost(UPD_IDLE,  "服务器没有发布该通道的版本清单（" + ch + ".json）"); return; }
        if (r.status != 200)         { UpdPost(UPD_ERROR, "服务器返回 " + std::to_string(r.status)); return; }
        if (!ParseManifest(r.body, ui)) { UpdPost(UPD_ERROR, "版本清单格式不正确（缺 version 字段）"); return; }
        EnterCriticalSection(&g_netCs);
        g_upd.info = ui;
        LeaveCriticalSection(&g_netCs);
        int c = VerCmp(ui.version, kAppVersion);
        if (c <= 0) {
            UpdPost(UPD_IDLE, "当前已是最新版本 v" + std::string(kAppVersion));
            return;
        }
        // 小版本静默：网吧场景不希望后台弹窗，只允许手动触发时提示
        if (!userTriggered && VerCmp(ui.version, std::string(kAppVersion).substr(0, 4)) > 0) {
            // 主版本跃迁（1.3 → 1.4）才考虑静默提醒；此处先记录，不打扰
        }
        UpdPost(UPD_AVAILABLE, "发现新版本 v" + ui.version);
    }).detach();
}
static void StartDownloadAsync() {
    EnterCriticalSection(&g_netCs);
    if (g_upd.state != UPD_AVAILABLE) { LeaveCriticalSection(&g_netCs); return; }
    g_upd.state = UPD_DOWNLOADING;
    g_upd.progress = 0;
    g_upd.msg = "正在下载安装包…";
    UpdateInfo ui = g_upd.info;
    LeaveCriticalSection(&g_netCs);
    if (g_upd_hwnd) PostMessageW(g_upd_hwnd, WM_APP + 20, 0, 0);
    std::thread([ui]() {
        char tmp[MAX_PATH] = { 0 };
        GetTempPathA(MAX_PATH, tmp);
        std::string dir = tmp;
        std::string verSan = ui.version;
        for (auto& ch : verSan) if (ch == '\\' || ch == '/' || ch == ':' || ch == '.' && false) ch = '_';
        // 简化：仅允许字母数字与点
        for (auto& ch : verSan) if (!isalnum((unsigned char)ch) && ch != '.') ch = '_';
        std::string out = dir + "MonitorOSD_update_" + verSan + ".exe";
        std::string err;
        if (!HttpDownload(ui.url, out, err, nullptr)) { UpdPost(UPD_ERROR, "下载失败：" + err); return; }
        // 完整性校验：清单给 sha256 就强校验，没给则拒绝（防中间人替换）
        if (ui.sha256.size() != 64) { UpdPost(UPD_ERROR, "清单缺少 sha256，拒绝安装（安全策略）"); return; }
        std::string hash;
        {
            FILE* f = FopenUtf8((char*)out.c_str(), "rb");
            if (!f) { UpdPost(UPD_ERROR, "无法读取下载文件"); return; }
            Sha256 s; s.init();
            std::vector<unsigned char> buf(1 << 16); size_t n;
            while ((n = fread(buf.data(), 1, buf.size(), f)) > 0) s.update(buf.data(), n);
            fclose(f);
            unsigned char d[32]; s.final(d); hash = Hex32(d, 32);
        }
        if (_stricmp(hash.c_str(), ui.sha256.c_str()) != 0) {
            DeleteFileW(Utf8ToWide(out).c_str());
            UpdPost(UPD_ERROR, "文件校验失败，已删除下载内容（请检查服务器文件或 CDN 缓存）");
            return;
        }
        EnterCriticalSection(&g_netCs);
        g_upd.newPath = out;
        LeaveCriticalSection(&g_netCs);
        UpdPost(UPD_READY, "下载完成并通过 SHA-256 校验");
    }).detach();
}
// 替换自身：当前 exe 改名 .old → 新文件拷到原位 → 异步删除 .old → 重启
static std::string g_relaunchArgs;
static void ApplyUpdateAndRestart() {
    wchar_t selfW[MAX_PATH] = { 0 };
    GetModuleFileNameW(nullptr, selfW, MAX_PATH);
    if (!selfW[0] || !g_upd.newPath.size()) return;
    std::string selfPath = WideToUtf8(selfW);
    std::wstring wold = Utf8ToWide(selfPath + ".old");
    std::wstring wnew = Utf8ToWide(selfPath + ".new");
    // 1) 若旧版残留 .old 先清掉（上次升级遗留，删除失败说明正被占用 → 走重启链路）
    DeleteFileW(wold.c_str());
    // 2) 自身 → .old（运行中的 exe 允许改名）
    if (!MoveFileExW(selfW, wold.c_str(), MOVEFILE_REPLACE_EXISTING)) {
        MessageBoxW(nullptr, L"升级准备失败：无法重命名当前程序。\n请确认程序所在目录可写，或以管理员身份运行后重试。",
                    L"MonitorOSD 更新", MB_OK | MB_ICONERROR);
        return;
    }
    // 3) 下载文件 → 原位；失败则回滚（保证不会变成"没有程序"状态）
    //    P0-3 修复：MOVEFILE_COPY_ALLOWED——下载在 %TEMP%(C:) 而程序在 D:/E:（还原卡常见），
    //    缺此标志跨卷移动 100% 失败（ERROR_NOT_SAME_DEVICE）
    std::wstring wdl = Utf8ToWide(g_upd.newPath);
    bool ok2 = MoveFileExW(wdl.c_str(), wnew.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
    if (ok2) ok2 = MoveFileExW(wnew.c_str(), selfW, MOVEFILE_REPLACE_EXISTING);
    if (!ok2) {
        DWORD gle = GetLastError();
        bool rolled = MoveFileExW(wold.c_str(), selfW, MOVEFILE_REPLACE_EXISTING) != 0;   // 回滚（检查返回值）
        LogLine("更新: 落地失败 gle=" + std::to_string((long)gle) + " 回滚" + (rolled ? "成功" : "失败！磁盘上留有 .old 可手动恢复"));
        MessageBoxW(nullptr, rolled ? L"升级失败：无法写入程序目录，已回滚到原版本。\n建议以管理员身份运行后再更新。"
                                    : L"升级失败且回滚失败！\n程序目录中留有「原程序名.old」，请手动改回原名后重试。",
                    L"MonitorOSD 更新", MB_OK | MB_ICONERROR);
        return;
    }
    // 4) 交接：先起新进程，再退出（.old 由下次启动时清理）
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = {};
    // P0-3 修复：新进程带 --relaunch 标记，绕开单实例互斥（旧进程还握着锁，新进程否则会自杀）
    std::wstring wcmd = L"\"" + std::wstring(selfW) + L"\" --relaunch";
    if (CreateProcessW(nullptr, &wcmd[0], nullptr, nullptr, FALSE, 0, nullptr,
                       Utf8ToWide(ExeDir()).c_str(), &si, &pi)) {
        CloseHandle(pi.hThread); CloseHandle(pi.hProcess);
    }
    LogLine("更新: 新进程已启动，旧进程退出");
    g_reallyExit = true;
    Shell_NotifyIconW(NIM_DELETE, &g_nid);
    DestroyWindow(g_hwnd);
}

// ================= 更新 / 反馈窗口（GDI 双缓冲，无需 D2D） =================
// 与主窗口分离：本窗口要能独立粘贴文本（Ctrl+V），且深/浅色主题下都固定深色卡片 + 高对比文字。
// v1.6.12：原三页签（软件更新/意见反馈/关于）精简为单一「意见反馈」页——
// 更新中心与关于信息已内联主窗口「关于」页签，弹窗只剩反馈表单（自绘文本框需独立焦点处理）。
static int  g_fbWithLog = 1;
static bool g_fbFocus = false, g_fbLogFocus = false;
static std::string g_fbLogInput;                // 补充信息（可粘贴）
static std::string g_aboutCopied;               // 复制回执（主窗口关于页「复制版本信息」用）
static float g_uScale = 1.f;
static int SUI(int v) { return (int)(v * g_uScale + 0.5f); }
static bool  g_titleDrag = false;               // 正在拖拽标题栏
static POINT g_titleDragOrg = { 0, 0 };         // 拖拽起点（屏幕坐标）
static RECT  r_titleBar{}, r_btnMin{}, r_btnClose{};
static const int UW_TITLE_H = 34;               // 设计像素（未缩放）
// 布局结果：坐标全部在 Paint 的 Layout() 里按内容流算出，交互与绘制共用同一份矩形
static RECT r_btnPrimary{}, r_btnSecondary{}, r_fbType[3] = {},
            r_chkLog{}, r_editFb{}, r_editLog{}, r_msg{}, r_pageTitle{};
static LRESULT CALLBACK UpdWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp);
static void PaintUpdateWindow(HWND h);
static std::string MonitorSnapshot();   // 定义在反馈模块（本文件后段）

static const COLORREF CW_BG      = RGB(0x0A, 0x0A, 0x0B);
static const COLORREF CW_CARD    = RGB(0x15, 0x16, 0x18);
static const COLORREF CW_SUB     = RGB(0x1E, 0x20, 0x23);
static const COLORREF CW_LINE    = RGB(0x2C, 0x2F, 0x34);
static const COLORREF CW_TEXT    = RGB(0xF2, 0xF3, 0xF5);
static const COLORREF CW_TEXT2   = RGB(0xB9, 0xBD, 0xC4);
static const COLORREF CW_MUTED   = RGB(0x7C, 0x82, 0x8C);
static const COLORREF CW_ACCENT  = RGB(0xF0, 0x44, 0x38);
static const COLORREF CW_OK      = RGB(0x34, 0xD3, 0x99);
static const COLORREF CW_WARN    = RGB(0xF5, 0xB9, 0x13);

// v1.5.9：把内嵌字体注册进 GDI 字体表，让本窗口用与主界面一致的 NotoSC / JBMonoOSD。
// 原来这里写死 "Microsoft YaHei UI"，导致更新/反馈/关于窗口的字形和主界面不是一套
// （用户反馈「字体又换成集成的了」就是这个原因 —— 不是主界面回落，是这个窗口没用内嵌字体）。
static bool g_gdiFontsReady = false;
static const wchar_t* kGdiUiFace   = L"Microsoft YaHei UI";
static const wchar_t* kGdiMonoFace = L"Consolas";
static void RegisterGdiFonts() {
    if (g_gdiFontsReady) return;
    g_gdiFontsReady = true;
    // v1.6.5 改本地加载：GDI 字体从磁盘文件注册（AddFontResourceEx + FR_PRIVATE 进程私有），
    // 与 DWrite 侧的 CreateFontFileReference 同源（Fonts\ 目录），内嵌字节数组方案废弃
    struct { const wchar_t* rel; bool mono; } list[] = {
        { L"Fonts\\NotoSC-Regular.ttf",        false },
        { L"Fonts\\NotoSC-Bold.ttf",           false },
        { L"Fonts\\JetBrainsMono-Regular.ttf", true  },
        { L"Fonts\\JetBrainsMono-Bold.ttf",    true  },
    };
    int okUi = 0, okMono = 0;
    const char* files[] = { "Fonts\\NotoSC-Regular.ttf", "Fonts\\NotoSC-Bold.ttf",
                            "Fonts\\JetBrainsMono-Regular.ttf", "Fonts\\JetBrainsMono-Bold.ttf" };
    for (int i = 0; i < 4; i++) {
        std::wstring p = Utf8ToWide(ResolvePath(files[i]));
        if (GetFileAttributesW(p.c_str()) == INVALID_FILE_ATTRIBUTES) continue;
        // AddFontResourceEx + FR_PRIVATE：进程私有注册，进程退出自动卸载（等价旧 AddFontMemResourceEx 语义）
        if (AddFontResourceExW(p.c_str(), FR_PRIVATE, nullptr) > 0)
            (list[i].mono ? okMono : okUi)++;
    }
    if (okUi > 0)   kGdiUiFace   = L"NotoSC";
    if (okMono > 0) kGdiMonoFace = L"JBMonoOSD";
    LogLine(std::string("字体(GDI): ui=") + (okUi ? "NotoSC" : "雅黑") +
            " mono=" + (okMono ? "JBMonoOSD" : "Consolas"));
}
static HFONT MkFont(int h, bool bold, bool mono = false) {
    RegisterGdiFonts();
    LOGFONTW lf = {};
    lf.lfHeight = -h;
    lf.lfWeight = bold ? FW_SEMIBOLD : FW_NORMAL;
    lf.lfCharSet = DEFAULT_CHARSET;
    lf.lfQuality = CLEARTYPE_QUALITY;
    wcscpy_s(lf.lfFaceName, mono ? kGdiMonoFace : kGdiUiFace);
    return CreateFontIndirectW(&lf);
}
static void GTxt(HDC dc, const std::string& s, RECT r, COLORREF c, HFONT f, UINT fmt = DT_LEFT | DT_TOP | DT_SINGLELINE) {
    if (r.right <= r.left || r.bottom <= r.top) return;
    HGDIOBJ old = SelectObject(dc, f);
    SetTextColor(dc, c);
    SetBkMode(dc, TRANSPARENT);
    RECT rr = r;
    DrawTextW(dc, Utf8ToWide(s).c_str(), -1, &rr, fmt | DT_NOPREFIX);
    SelectObject(dc, old);
}
static int GTxtH(HDC dc, HFONT f, const std::string& s, int width) {
    HGDIOBJ old = SelectObject(dc, f);
    RECT r = { 0, 0, (std::max)(1, width), 4000 };
    DrawTextW(dc, Utf8ToWide(s).c_str(), -1, &r,
              DT_LEFT | DT_TOP | DT_WORDBREAK | DT_NOPREFIX | DT_CALCRECT);
    SelectObject(dc, old);
    return (std::max)(1, (int)(r.bottom - r.top));
}
static int GLineH(HDC dc, HFONT f) {
    HGDIOBJ old = SelectObject(dc, f);
    TEXTMETRICW tm;
    GetTextMetricsW(dc, &tm);
    SelectObject(dc, old);
    return tm.tmHeight;
}
static void GFill(HDC dc, RECT r, COLORREF c) {
    if (r.right <= r.left || r.bottom <= r.top) return;
    HBRUSH b = CreateSolidBrush(c);
    FillRect(dc, &r, b);
    DeleteObject(b);
}
static void GCard(HDC dc, RECT r, COLORREF fill) {
    if (r.right <= r.left || r.bottom <= r.top) return;
    GFill(dc, r, fill);
    HBRUSH b = CreateSolidBrush(CW_LINE);
    FrameRect(dc, &r, b);
    DeleteObject(b);
}
// v1.6.12：GBar 进度条删除（仅「软件更新」页签使用，该页签已移除）

// 内容流排版：一页一次算完，绘制/命中测试共用（旧版是 Paint 里临时算坐标，
// 页签与标题各自硬编码 → 文字被压住、进度条压状态行，都是这么来的）
struct UpdLayout {
    HDC    dc;
    HFONT  fTitle, fBody, fSmall, fBtn, fMono, fH2;
    int    W, H, pad, x, y, contentW;
    void Row(int hgt) { y += hgt; }
};
static void LayoutCommon(UpdLayout& L) {
    // 无边框窗口：顶部先让出自绘标题栏，之后才是标题 → 副行（当前版本）
    // 标题栏：左侧程序名，右侧最小化/关闭按钮
    r_titleBar = { 0, 0, L.W, SUI(UW_TITLE_H) };
    {
        int bs = SUI(30);                            // 按钮方块边长
        int by = (r_titleBar.bottom - bs) / 2;
        r_btnClose = { L.W - SUI(10) - bs, by, L.W - SUI(10), by + bs };
        r_btnMin   = { r_btnClose.left - SUI(6) - bs, by, r_btnClose.left - SUI(6), by + bs };
    }
    L.y = r_titleBar.bottom + SUI(12);
    int titleH = GLineH(L.dc, L.fTitle);
    r_pageTitle = { L.pad, L.y, L.W - L.pad, L.y + titleH };
    L.y += titleH + SUI(4);

    // 副行：当前版本（原右侧「状态点 + 更新消息」随更新中心页签一并移除）
    RECT sub = { L.pad, L.y, L.W - L.pad, L.y + GLineH(L.dc, L.fSmall) };
    L.y += (sub.bottom - sub.top) + SUI(10);
    r_msg = sub;
}
static void LayoutFooter(UpdLayout& L, bool hasSecondary) {
    // 底部按钮：主按钮贴右下，次按钮在其左侧；只在需要时占位
    int bh = SUI(34);
    int by = L.H - L.pad - bh;
    r_btnPrimary = { L.W - L.pad - SUI(124), by, L.W - L.pad, by + bh };
    if (hasSecondary) {
        r_btnSecondary = { r_btnPrimary.left - SUI(10) - SUI(108), by, r_btnPrimary.left - SUI(10), by + bh };
    } else {
        r_btnSecondary = {};
    }
}
static void LayoutFeedbackPage(UpdLayout& L) {
    LayoutCommon(L);
    // 不再有「问题反馈/功能建议/其他」分类行：用户说一个类别就够，留着只是多一次点击
    for (int i = 0; i < 3; i++) r_fbType[i] = {};
    LayoutFooter(L, true);
    // 从下往上排：按钮 → 状态行 → 补充框 → 勾选行 → 正文框。
    // 旧写法先按比例给正文框定高、再让补充框吃掉剩余，窗口变矮时正文框挤掉了补充框和按钮空间。
    int statusH = SUI(26);                                  // 提交状态行（留白也是它）
    int bottom = r_btnPrimary.top - statusH;
    int chkH = SUI(20);
    int logH = (std::max)(SUI(54), (bottom - L.y) / 3);      // 补充框：可写两三行
    int et = bottom - logH;
    r_editLog = { L.x, et, L.W - L.pad, bottom };
    int ly = et - SUI(10) - chkH;
    r_chkLog = { L.x, ly, L.x + chkH, ly + chkH };
    int fbBottom = ly - SUI(10);
    int fbh = fbBottom - L.y;
    if (fbh < SUI(110)) fbh = SUI(110);
    r_editFb = { L.x, L.y, L.W - L.pad, L.y + fbh };
}
// v1.6.12：LayoutAboutPage / AboutText / AboutContact 删除（关于页内联进主窗口「关于」页签）
// 固定窗口尺寸（设计像素）：不再随内容变高变矮。
// 窗口尺寸（设计像素，相对主窗口 1152×712 而言的「对话框」尺寸，约为主窗口的 3/4）
static const int UW_CLIENT_W = 640;   // 布局基准宽（设计像素）：与目标窗口宽一致，g_uScale = clientW/640
static void LayoutUpdateWindow(HWND h) {
    RECT rc; GetClientRect(h, &rc);
    // 缩放只看宽度：窗口是固定大小，高度不再参与（否则改高度会让字号跳变）
    g_uScale = (float)rc.right / (float)UW_CLIENT_W;
    if (g_uScale < 0.85f) g_uScale = 0.85f;
    if (g_uScale > 1.60f) g_uScale = 1.60f;
}
// 作者与联系方式（发版前改成你自己的；留空则该行不显示）
static const char* const kAuthorName  = "咸鱼工作室";
static const char* const kAuthorQQ    = "";
static const char* const kAuthorWeChat = "";
static const char* const kAuthorEmail = "";
static const char* const kAuthorSite  = "";
// v1.6.12：AboutContact 删除（弹窗关于页移除；作者信息在主窗口「关于」页签由 kAuthor* 常量直接渲染）
struct AboutMeta { std::string name, version, build, env, path; };
static AboutMeta CollectAboutMeta() {
    AboutMeta m;
    m.name = "MonitorOSD";
    m.version = std::string("v") + kAppVersion;
    // 构建时间取 exe 自身时间戳（__DATE__ 是编译机时间，且与版本号重复无意义）
    {
        wchar_t self[MAX_PATH] = { 0 };
        GetModuleFileNameW(nullptr, self, MAX_PATH);
        WIN32_FILE_ATTRIBUTE_DATA fa = {};
        if (GetFileAttributesExW(self, GetFileExInfoStandard, &fa)) {
            SYSTEMTIME st; FileTimeToSystemTime(&fa.ftLastWriteTime, &st);
            char b[64];
            sprintf_s(b, "%04d-%02d-%02d", st.wYear, st.wMonth, st.wDay);
            m.build = b;
        }
    }
    // 运行环境：OS 名称（GetVersionEx 有兼容性伪装，改用 RtlGetVersion 取真实版本）
    {
        typedef LONG(WINAPI* RtlGetVersionFn)(PRTL_OSVERSIONINFOW);
        RTL_OSVERSIONINFOW vi = {};
        vi.dwOSVersionInfoSize = sizeof(vi);
        HMODULE nt = GetModuleHandleW(L"ntdll.dll");
        RtlGetVersionFn rgv = nt ? (RtlGetVersionFn)GetProcAddress(nt, "RtlGetVersion") : nullptr;
        std::string osName = "Windows";
        if (rgv && rgv(&vi) == 0) {
            if (vi.dwMajorVersion >= 10) osName = (vi.dwBuildNumber >= 22000) ? "Windows 11" : "Windows 10";
            else if (vi.dwMajorVersion == 6 && vi.dwMinorVersion == 3) osName = "Windows 8.1";
            else if (vi.dwMajorVersion == 6 && vi.dwMinorVersion == 2) osName = "Windows 8";
            else if (vi.dwMajorVersion == 6 && vi.dwMinorVersion == 1) osName = "Windows 7";
            else osName = "Windows " + std::to_string(vi.dwMajorVersion) + "." + std::to_string(vi.dwMinorVersion);
            if (vi.dwBuildNumber) osName += " (" + std::to_string(vi.dwBuildNumber) + ")";
        }
        m.env = osName;
        if (!g_mons.empty()) m.env += "  ·  " + std::to_string(g_mons.size()) + " 台显示器";
        else m.env += "  ·  未检测到显示器";
    }
    m.path = ExeDir();
    return m;
}
static std::string CopyableInfo() {
    AboutMeta m = CollectAboutMeta();
    std::string t;
    t += m.name + " " + m.version + "\r\n";
    t += "构建: " + m.build + "\r\n";
    t += "系统: " + m.env + "\r\n";
    t += "目录: " + m.path + "\r\n";
    t += "更新源: " + UpdateBase() + " (" + (g_config.updateChannel.empty() ? "latest" : g_config.updateChannel) + ")\r\n";
    t += "---------- 显示器 ----------\r\n";
    std::string mon = MonitorSnapshot();
    t += mon.empty() ? "(未检测到显示器)\r\n" : mon;
    return t;
}
static void PaintUpdateWindow(HWND h) {
    PAINTSTRUCT ps;
    HDC dc0 = BeginPaint(h, &ps);
    RECT rc; GetClientRect(h, &rc);
    if (rc.right <= 0 || rc.bottom <= 0) { EndPaint(h, &ps); return; }
    HDC dc = CreateCompatibleDC(dc0);
    HBITMAP bmp = CreateCompatibleBitmap(dc0, rc.right, rc.bottom);
    HGDIOBJ oldBmp = SelectObject(dc, bmp);

    UpdLayout L;
    L.dc = dc; L.W = rc.right; L.H = rc.bottom;
    L.pad = SUI(18);
    L.x = L.pad; L.contentW = rc.right - L.pad * 2;
    // 与主界面同一套字号：正文/按钮 12，小字 11，标题 15（主界面卡片标题也是 12~15）
    L.fTitle = MkFont(SUI(15), true);
    L.fH2    = MkFont(SUI(13), true);
    L.fBody  = MkFont(SUI(12), false);
    L.fSmall = MkFont(SUI(11), false);
    L.fBtn   = MkFont(SUI(12), true);
    L.fMono  = MkFont(SUI(11), false, true);

    EnterCriticalSection(&g_netCs);
    std::string fbState = g_upd.feedbackState;
    LeaveCriticalSection(&g_netCs);

    // ---- 排版（先算坐标，再画）；v1.6.12 起固定为反馈页 ----
    LayoutFeedbackPage(L);

    GFill(dc, rc, CW_BG);

    // ---- 自绘标题栏（无边框窗口）----
    {
        GFill(dc, r_titleBar, RGB(0x14, 0x15, 0x17));
        GFill(dc, { 0, r_titleBar.bottom - 1, rc.right, r_titleBar.bottom }, CW_LINE);
        // 左侧：品牌点 + 程序名
        int ds = SUI(8);
        int dcx = L.pad + SUI(2), dcy = (r_titleBar.bottom - ds) / 2;
        HBRUSH ab = CreateSolidBrush(CW_ACCENT);
        HGDIOBJ ob = SelectObject(dc, ab);
        HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, dcx, dcy, dcx + ds, dcy + ds);
        SelectObject(dc, op); SelectObject(dc, ob);
        DeleteObject(ab);
        GTxt(dc, "MonitorOSD", { dcx + ds + SUI(8), 0, rc.right - SUI(120), r_titleBar.bottom },
             CW_TEXT, L.fH2, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        // 右侧两个按钮：悬停高亮，关闭悬停变红
        POINT mt; GetCursorPos(&mt); ScreenToClient(h, &mt);
        auto winBtn = [&](RECT r, bool isClose) {
            bool hv = PtIn(r, mt);
            if (hv) GFill(dc, r, isClose ? RGB(0xC4, 0x2B, 0x1C) : CW_SUB);
            COLORREF fg = hv ? RGB(255, 255, 255) : CW_TEXT2;
            int cx = (r.left + r.right) / 2, cy = (r.top + r.bottom) / 2, k = SUI(5);
            HPEN pen = CreatePen(PS_SOLID, (std::max)(1, SUI(1)), fg);
            HGDIOBJ open = SelectObject(dc, pen);
            if (isClose) {
                MoveToEx(dc, cx - k, cy - k, nullptr); LineTo(dc, cx + k + 1, cy + k + 1);
                MoveToEx(dc, cx + k, cy - k, nullptr); LineTo(dc, cx - k - 1, cy + k + 1);
            } else {
                MoveToEx(dc, cx - k, cy, nullptr); LineTo(dc, cx + k + 1, cy);
            }
            SelectObject(dc, open);
            DeleteObject(pen);
        };
        winBtn(r_btnMin, false);
        winBtn(r_btnClose, true);
    }

    // 标题 + 副行（v1.6.12：单页固定「意见反馈」，窗口标题在创建时已设置）
    GTxt(dc, "意见反馈", r_pageTitle, CW_TEXT, L.fTitle);
    // 副行：当前版本
    GTxt(dc, std::string("当前版本 v") + kAppVersion, r_msg, CW_MUTED, L.fSmall,
         DT_LEFT | DT_VCENTER | DT_SINGLELINE);

    if (true) {
        // 正文
        GCard(dc, r_editFb, CW_CARD);
        RECT er = { r_editFb.left + SUI(12), r_editFb.top + SUI(10),
                    r_editFb.right - SUI(12), r_editFb.bottom - SUI(10) };
        if (g_fbText.empty()) {
            GTxt(dc, "请描述遇到的问题或想要的功能（Ctrl+V 可粘贴报错文本）", er, CW_MUTED, L.fBody,
                 DT_LEFT | DT_TOP | DT_WORDBREAK);
        } else {
            // 光标只在末尾闪烁位显示（简单模型：追加输入），文本过多时从顶部裁切
            std::string shown = g_fbText;
            int need = GTxtH(dc, L.fBody, shown + "|", er.right - er.left);
            int room = er.bottom - er.top;
            if (need > room) {
                // 保留末尾能放下的行数
                size_t cut = 0;
                while (cut < shown.size() &&
                       GTxtH(dc, L.fBody, shown.substr(cut), er.right - er.left) > room) {
                    size_t nl = shown.find('\n', cut);
                    cut = (nl == std::string::npos) ? shown.size() : nl + 1;
                }
                shown = shown.substr(cut);
            }
            GTxt(dc, shown + (g_fbFocus ? "|" : ""), er, CW_TEXT, L.fBody,
                 DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL);
        }
        // 附带日志勾选
        GCard(dc, r_chkLog, g_fbWithLog ? CW_ACCENT : CW_SUB);
        if (g_fbWithLog) {
            HGDIOBJ old = SelectObject(dc, L.fSmall);
            SetTextColor(dc, RGB(255, 255, 255));
            SetBkMode(dc, TRANSPARENT);
            RECT cb = r_chkLog;
            DrawTextW(dc, L"\u2713", -1, &cb, DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_NOPREFIX);
            SelectObject(dc, old);
        }
        GTxt(dc, "附带诊断日志（显示器型号 / DDC 状态 / 最近 120 行日志，不含个人信息）",
             { r_chkLog.right + SUI(10), r_chkLog.top - SUI(2), rc.right - L.pad, r_chkLog.bottom + SUI(2) },
             CW_TEXT2, L.fSmall, DT_LEFT | DT_VCENTER | DT_SINGLELINE | DT_END_ELLIPSIS);
        // 补充信息
        if (r_editLog.bottom > r_editLog.top) {
            GCard(dc, r_editLog, CW_CARD);
            RECT lr = { r_editLog.left + SUI(10), r_editLog.top + SUI(8),
                        r_editLog.right - SUI(10), r_editLog.bottom - SUI(8) };
            if (g_fbLogInput.empty())
                GTxt(dc, "补充信息（可选）：可粘贴报错文本 / 显示器型号 / 操作步骤", lr, CW_MUTED, L.fMono,
                     DT_LEFT | DT_TOP | DT_WORDBREAK);
            else
                GTxt(dc, g_fbLogInput + (g_fbLogFocus ? "|" : ""), lr, CW_TEXT2, L.fMono,
                     DT_LEFT | DT_TOP | DT_WORDBREAK | DT_EDITCONTROL);
        }
        // 提交状态：放在底部按钮上方单独一行（旧版画在反馈类型下方，和进度条挤在一起）
        if (!fbState.empty()) {
            GTxt(dc, fbState, { L.pad, r_btnPrimary.top - SUI(26), rc.right - L.pad, r_btnPrimary.top - SUI(6) },
                 fbState.find("已提交") != std::string::npos ? CW_OK : CW_ACCENT, L.fSmall,
                 DT_LEFT | DT_VCENTER | DT_SINGLELINE);
        }
        GCard(dc, r_btnPrimary, CW_ACCENT);
        GTxt(dc, "提交反馈", r_btnPrimary, RGB(255, 255, 255), L.fBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
        GCard(dc, r_btnSecondary, CW_SUB);
        GTxt(dc, "清空", r_btnSecondary, CW_TEXT2, L.fBtn, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
    }
    // v1.6.12：原「关于页」绘制段删除（内联进主窗口「关于」页签）

    // 圆角输出：用路径裁剪到圆角矩形，四角露出桌面（与 Win11 圆角窗口一致）
    {
        int rad = SUI(10) * 2;
        HRGN rgn = CreateRoundRectRgn(0, 0, rc.right + 1, rc.bottom + 1, rad, rad);
        SelectClipRgn(dc0, rgn);
        DeleteObject(rgn);
    }
    BitBlt(dc0, 0, 0, rc.right, rc.bottom, dc, 0, 0, SRCCOPY);
    SelectClipRgn(dc0, nullptr);
    // v1.5.3 P2-4：先把原图选回 dc，否则 bmp 仍处于"被选中"状态，DeleteObject 会失败（位图泄漏）
    SelectObject(dc, oldBmp);
    DeleteObject(bmp);
    // v1.5.3 P2-4 修复：内存 DC 必须用 DeleteDC 释放。原写 DeleteObject(dc) 对 DC 无效
    // （返回 FALSE 且不报错），每重绘一次泄漏一个 DC —— 反馈页每敲一个字符就重绘一次，
    // 值守机常开数天会耗尽 GDI 句柄。全文件 CreateCompatibleDC 只有这一处。
    DeleteDC(dc);
    DeleteObject(L.fTitle); DeleteObject(L.fH2); DeleteObject(L.fBody);
    DeleteObject(L.fSmall); DeleteObject(L.fBtn); DeleteObject(L.fMono);
    EndPaint(h, &ps);
}
static void OpenUpdateWindow() {
    if (g_upd_hwnd) {
        ShowWindow(g_upd_hwnd, SW_SHOW);
        SetForegroundWindow(g_upd_hwnd);
        InvalidateRect(g_upd_hwnd, nullptr, FALSE);
        return;
    }
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    // 固定客户区尺寸，按主窗口缩放系数适配高分屏（v1.6.12：单一反馈页，略收高度更贴表单比例）
    int cw = (int)(640 * g_scale);
    int ch = (int)(430 * g_scale);
    int maxW = (int)((wa.right - wa.left) * 0.72f), maxH = (int)((wa.bottom - wa.top) * 0.80f);
    if (cw > maxW) cw = maxW;
    if (ch > maxH) ch = maxH;
    int x = wa.left + (wa.right - wa.left - cw) / 2;
    int y = wa.top + (wa.bottom - wa.top - ch) / 2;
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.lpfnWndProc = UpdWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW);
    wc.lpszClassName = L"MonitorOSDUpdate";
    wc.hbrBackground = nullptr;
    RegisterClassExW(&wc);
    // 无边框（WS_POPUP + 自绘标题栏）。不去任务栏按钮，Alt+Tab 可切；阴影交给 DWM。
    g_upd_hwnd = CreateWindowExW(WS_EX_APPWINDOW, L"MonitorOSDUpdate", L"MonitorOSD · 意见反馈",
                                 WS_POPUP | WS_VISIBLE | WS_CLIPCHILDREN,
                                 x, y, cw, ch, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_upd_hwnd) return;
    // Win11 圆角 + 阴影（无边框窗口默认没有系统阴影）
    DWM_WINDOW_CORNER_PREFERENCE pr = DWMWCP_ROUND;
    DwmSetWindowAttribute(g_upd_hwnd, DWMWA_WINDOW_CORNER_PREFERENCE, &pr, sizeof(pr));
    MARGINS mg = { 1, 1, 1, 1 };
    DwmExtendFrameIntoClientArea(g_upd_hwnd, &mg);
    // 只允许用 DWM 边框投影，保留自绘圆角
    SetWindowPos(g_upd_hwnd, nullptr, 0, 0, 0, 0,
                 SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);
    ShowWindow(g_upd_hwnd, SW_SHOW);
    UpdateWindow(g_upd_hwnd);
}
static void RefreshUpdWindow() { if (g_upd_hwnd) InvalidateRect(g_upd_hwnd, nullptr, FALSE); }

// 取剪贴板文本（CF_UNICODETEXT → UTF-8）
static std::string ClipGetText() {
    std::string out;
    if (!OpenClipboard(g_upd_hwnd)) return out;
    HANDLE hd = GetClipboardData(CF_UNICODETEXT);
    if (hd) {
        const wchar_t* p = (const wchar_t*)GlobalLock(hd);
        if (p) { out = WideToUtf8(p); GlobalUnlock(hd); }
    }
    CloseClipboard();
    return out;
}
static void ClipPutText(HWND owner, const std::string& s) {
    std::wstring w = Utf8ToWide(s);
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    HGLOBAL g = GlobalAlloc(GMEM_MOVEABLE, (w.size() + 1) * sizeof(wchar_t));
    if (g) {
        void* p = GlobalLock(g);
        memcpy(p, w.c_str(), (w.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(g);
        SetClipboardData(CF_UNICODETEXT, g);
    }
    CloseClipboard();
}
static void InsertAt(std::string& dst, const std::string& add, size_t maxLen) {
    for (char c : add) {
        if (dst.size() >= maxLen) break;
        if (c == '\r') continue;
        dst += c;
    }
}
static LRESULT CALLBACK UpdWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE:
        g_upd_hwnd = h;
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT:
        PaintUpdateWindow(h);
        return 0;
    case WM_SIZE:
        LayoutUpdateWindow(h);
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_NCHITTEST: {
        // 无边框窗口：自己判断命中——边缘 6px 可拉伸，标题栏可拖拽
        POINT sp = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        RECT wr; GetWindowRect(h, &wr);
        int bw = (std::max)(4, SUI(5));
        bool L = sp.x < wr.left + bw, R = sp.x >= wr.right - bw;
        bool T = sp.y < wr.top + bw, B = sp.y >= wr.bottom - bw;
        if (T && L) return HTTOPLEFT;
        if (T && R) return HTTOPRIGHT;
        if (B && L) return HTBOTTOMLEFT;
        if (B && R) return HTBOTTOMRIGHT;
        if (T) return HTTOP;
        if (B) return HTBOTTOM;
        if (L) return HTLEFT;
        if (R) return HTRIGHT;
        POINT cp = sp; ScreenToClient(h, &cp);
        // 按钮区必须先排除，否则 HTCAPTION 会把点击吞掉（系统当标题栏处理）
        if (PtIn(r_btnClose, cp) || PtIn(r_btnMin, cp)) return HTCLIENT;
        if (PtIn(r_titleBar, cp)) return HTCAPTION;   // 其余标题栏交给系统拖拽
        return HTCLIENT;
    }
    case WM_APP + 20:          // 更新状态变化（工作线程 → UI）
    case WM_APP + 21:          // 反馈提交结果
        RefreshUpdWindow();
        return 0;
    case WM_SETCURSOR: {
        POINT pt; GetCursorPos(&pt); ScreenToClient(h, &pt);
        bool edit = PtIn(r_editFb, pt) || PtIn(r_editLog, pt);
        SetCursor(LoadCursorW(nullptr, edit ? (LPCWSTR)IDC_IBEAM : (LPCWSTR)IDC_ARROW));
        return TRUE;
    }
    case WM_MOUSEMOVE: {
        // 只关心「进入/离开可点区域」的切换，避免每帧重绘
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        static POINT s_last = { -9999, -9999 };
        auto zone = [&](POINT p) -> int {
            if (PtIn(r_chkLog, p)) return 33;
            if (PtIn(r_btnPrimary, p) || PtIn(r_btnSecondary, p)) return 34;
            return 0;
        };
        int a = zone(pt), b = zone(s_last);
        if ((a == 0) != (b == 0) || a != b) { s_last = pt; RefreshUpdWindow(); }
        if (a != 0 || b != 0) {
            TRACKMOUSEEVENT t = { sizeof(t), TME_LEAVE, h, 0 };
            TrackMouseEvent(&t);
        }
        return 0;
    }
    case WM_MOUSELEAVE:
        RefreshUpdWindow();
        return 0;
    case WM_CHAR: {            // 文本输入（中文输入法走 WM_IME_CHAR → WM_CHAR）
        wchar_t ch = (wchar_t)wp;
        if (ch == 8) {
            std::string& t = g_fbLogFocus ? g_fbLogInput : g_fbText;
            size_t cut = 1;
            while (cut < t.size() && ((unsigned char)t[t.size() - cut] & 0xC0) == 0x80) cut++;   // 回退整字符
            if (t.size() >= cut) t.erase(t.size() - cut);
        } else if (ch == 22) {  // Ctrl+V
            std::string clip = ClipGetText();
            if (g_fbLogFocus) InsertAt(g_fbLogInput, clip, 4000);
            else              InsertAt(g_fbText, clip, 4000);
        } else if (ch == 1) {   // Ctrl+A → 仅全选语义，此处不实现选区（单行追加模型），清空便于重输
            if (g_fbLogFocus) g_fbLogInput.clear(); else g_fbText.clear();
        } else if (ch >= 32 || ch == '\n') {
            char u8[8] = { 0 };
            int n = WideCharToMultiByte(CP_UTF8, 0, &ch, 1, u8, sizeof(u8), nullptr, nullptr);
            if (n > 0) {
                if (g_fbLogFocus) InsertAt(g_fbLogInput, std::string(u8, n), 4000);
                else              InsertAt(g_fbText, std::string(u8, n), 4000);
            }
        }
        RefreshUpdWindow();
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(h); return 0; }
        if (wp == VK_RETURN) {
            if (g_fbLogFocus) InsertAt(g_fbLogInput, "\n", 4000);
            else if (g_fbFocus) InsertAt(g_fbText, "\n", 4000);
            RefreshUpdWindow();
            return 0;
        }
        return 0;
    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        if (PtIn(r_btnClose, pt)) { DestroyWindow(h); return 0; }
        if (PtIn(r_btnMin, pt))   { ShowWindow(h, SW_MINIMIZE); return 0; }
        // v1.6.12：页签点击/关于页链接/更新中心按钮分支已随页签移除，只处理反馈交互
        if (PtIn(r_chkLog, pt)) { g_fbWithLog = !g_fbWithLog; RefreshUpdWindow(); return 0; }
        if (PtIn(r_editFb, pt)) { g_fbFocus = true; g_fbLogFocus = false; RefreshUpdWindow(); return 0; }
        if (PtIn(r_editLog, pt)) { g_fbLogFocus = true; g_fbFocus = false; RefreshUpdWindow(); return 0; }
        if (PtIn(r_btnPrimary, pt)) {
            if (g_fbText.size() < 4) {
                EnterCriticalSection(&g_netCs); g_upd.feedbackState = "请先填写问题描述（至少 4 个字）"; LeaveCriticalSection(&g_netCs);
                RefreshUpdWindow();
                return 0;
            }
            EnterCriticalSection(&g_netCs); g_upd.feedbackState = "正在提交…"; LeaveCriticalSection(&g_netCs);
            SubmitFeedbackAsync(g_fbText, g_fbType, g_fbWithLog != 0);
            RefreshUpdWindow();
            return 0;
        }
        if (PtIn(r_btnSecondary, pt)) { g_fbText.clear(); g_fbLogInput.clear(); RefreshUpdWindow(); return 0; }
        g_fbFocus = g_fbLogFocus = false;
        RefreshUpdWindow();
        return 0;
    }
    case WM_COMMAND:
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        if (g_upd_hwnd == h) g_upd_hwnd = nullptr;
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static std::string MachineGuid() {
    char v[128] = { 0 }; DWORD n = sizeof(v); HKEY k;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Microsoft\\Cryptography", 0,
                      KEY_READ | KEY_WOW64_64KEY, &k) == ERROR_SUCCESS) {
        RegQueryValueExA(k, "MachineGuid", nullptr, nullptr, (LPBYTE)v, &n);
        RegCloseKey(k);
    }
    return v[0] ? v : "unknown";
}
static std::string UserName() {
    char b[UNLEN + 1] = { 0 }; DWORD n = UNLEN + 1;
    if (!GetUserNameA(b, &n)) return "unknown";
    return b;
}
static std::string JsonEsc(const std::string& s) {
    std::string o;
    for (char c : s) {
        switch (c) {
        case '"':  o += "\\\""; break;
        case '\\': o += "\\\\"; break;
        case '\n': o += "\\n";  break;
        case '\r': o += "\\r";  break;
        case '\t': o += "\\t";  break;
        default:
            if ((unsigned char)c < 0x20) { char b[8]; sprintf_s(b, "\\u%04X", c); o += b; }
            else o += c;
        }
    }
    return o;
}
static std::string TailLog(int maxLines) {
    std::string path = ExeDir() + "\\log.txt", all;
    FILE* f = FopenUtf8((char*)path.c_str(), "rb");
    if (!f) return "";
    if (fseek(f, 0, SEEK_END) == 0) {
        long sz = ftell(f);
        long from = sz > 32768 ? sz - 32768 : 0;
        fseek(f, from, SEEK_SET);
        char buf[8192]; size_t n;
        while ((n = fread(buf, 1, sizeof(buf), f)) > 0) all.append(buf, n);
    }
    fclose(f);
    // 取最后 maxLines 行
    int cnt = 0;
    size_t p = all.size();
    while (p > 0) {
        p--;
        if (all[p] == '\n' && ++cnt > maxLines) { all = all.substr(p + 1); break; }
    }
    if (all.size() > 8000) all = all.substr(all.size() - 8000);
    return all;
}
static std::string MonitorSnapshot() {
    std::string s;
    for (auto* m : g_mons) {
        if (!m) continue;
        s += DisplayNameOf(m) + " | " + m->capModel + " | " + m->edidModel + " | serial=" + m->serial +
             " | ddc=" + (m->ddcOk ? "ok" : "fail") + "\n";
    }
    return s;
}
// 反馈 JSON 追加 HMAC 签名。注意：body 形如 {...,"log":"..."} 已带结尾 }，
// 必须先剥掉再拼 hash，否则多一个 }（解析必失败——曾踩过）。
static std::string SignFeedback(const std::string& body) {
    std::string prefix = body;
    if (!prefix.empty() && prefix.back() == '}') prefix.pop_back();
    std::string payload = prefix + "}";
    return prefix + ",\"hash\":\"" + HmacSha256B64(kFeedbackSalt, payload) + "\"}";
}
static void SubmitFeedbackAsync(const std::string& text, int type, bool withLog) {
    std::string body = "{";
    body += "\"type\":\"" + JsonEsc(kFbTypes[type]) + "\",";
    body += "\"version\":\"" + std::string(kAppVersion) + "\",";
    body += "\"text\":\"" + JsonEsc(text) + "\",";
    body += "\"extra\":\"" + JsonEsc(g_fbLogInput) + "\",";
    body += "\"with_log\":" + std::string(withLog ? "true" : "false") + ",";
    body += "\"machine\":\"" + JsonEsc(Sha256Hex(MachineGuid()).substr(0, 16)) + "\",";   // 只上报哈希，不暴露原始 GUID
    body += "\"user\":\"" + JsonEsc(UserName()) + "\",";
    body += "\"os\":\"" + JsonEsc(std::to_string(LOBYTE(LOWORD(GetVersion()))) + "." +
                                  std::to_string(HIBYTE(LOWORD(GetVersion())))) + "\",";
    body += "\"monitors\":\"" + JsonEsc(MonitorSnapshot()) + "\",";
    body += "\"log\":\"" + JsonEsc(withLog ? TailLog(120) : "") + "\"";
    body += "}";
    body = SignFeedback(body);
    if (g_upd_hwnd) PostMessageW(g_upd_hwnd, WM_APP + 21, 0, 0);
    std::thread([body]() {
        HttpResp r = HttpCall(UpdateBase() + "/feedback.php", body, true);
        std::string msg;
        if (UpdateBaseUnconfigured()) msg = "未配置反馈服务器（config.json 的 UpdateBase 为空）。\n"
                                           "可把反馈直接发给开发者：见「关于」页联系方式。";
        else if (!r.err.empty())      msg = "提交失败：" + r.err;
        else if (r.status == 200)     msg = "反馈已提交，感谢！";
        else                          msg = "提交失败（服务器返回 " + std::to_string(r.status) + "）";
        EnterCriticalSection(&g_netCs);
        g_upd.feedbackState = msg;
        LeaveCriticalSection(&g_netCs);
        if (g_upd_hwnd) PostMessageW(g_upd_hwnd, WM_APP + 21, 0, 0);
    }).detach();
}

static void OpenMonitorTest(MonitorDev* m);   // 前置声明（全屏测试窗口，定义见后）
static void SysClick(POINT pt) {
    // ---- 显示器维护（恢复出厂设置 + 显示器测试；唤醒/OSD 锁定入口已按需求移除）----
    if (PtIn(g_sys.btnReset, pt)) {
        if (!g_cur) return;
        g_confirmReset = true;   // 程序内样式确认卡（PaintConfirmReset / WndProc 处理）
        Invalidate();
        return;
    }
    if (PtIn(g_sys.btnTest, pt)) { OpenMonitorTest(g_cur); return; }
    if (PtIn(g_sys.btnProfiles, pt)) { OpenProfilePanel(); Invalidate(); return; }   // v1.6.7
    // ---- 校准向导 ----
    if (PtIn(g_sys.btnCal, pt)) { OpenToolsWindow(1); return; }
    // ---- 软件设置 ----
    if (PtIn(g_sys.chkBoot, pt))  { g_config.autoStart = !g_config.autoStart; ApplyAutoStart(); SaveConfig(); Invalidate(); return; }
    if (PtIn(g_sys.chkTray, pt)) { g_config.minimizeToTray = !g_config.minimizeToTray; SaveConfig(); Invalidate(); return; }
    if (PtIn(g_sys.capFollow, pt)) {
        g_config.accentMode = "auto";
        if (g_cur) Theme::ResolveAccent(g_cur->vendor, nullptr);
        SaveConfig(); Invalidate();
        return;
    }
    for (size_t i = 0; i < g_sys.swatches.size(); i++)
        if (PtIn(g_sys.swatches[i], pt)) {
            g_config.accentMode = "manual";
            g_config.manualAccent = kAccentOpts[i];
            Theme::ResolveAccent(g_cur ? g_cur->vendor : "", &g_config.manualAccent);
            SaveConfig(); Invalidate();
            return;
        }
    // v1.6.12：原「在线更新 / 意见反馈」两按钮点击已删除（卡片移除，入口收拢到「关于」页签）。
}
static void StartResetFactory(MonitorDev* m) {
    ShowToast("正在发送恢复出厂指令…");
    std::thread([m]() {
        // v1.6.8：写后验证——部分固件（如 AOC AGON）对 MCCS 恢复出厂(0x04)无任何响应，
        // 旧版无条件报成功会误导用户。先读亮度(0x10)留底，写 0x04 → 等 1.5s → 再读比对；
        // 前后读都成功且值未变 → 判定固件不响应，引导用户改用「一键还原基准」。
        // v1.6.8 F1 修复：线程持引用——恢复出厂写 0x04 可能触发省电/热插拔→重探→旧对象 Retire，
        // 本线程总时长（Read+WriteNow×3+WakeUp 重试+Sleep(1500)+Read）可超 3s 宽限期，
        // 裸指针捕获会访问已释放内存。Guard 构造即 AddRef，任何路径退出都保证 Release（同 6120 行用法）。
        MonitorDev::Guard guard(m);
        unsigned b1 = 0, b2 = 0, x1 = 0, x2 = 0;
        bool hasBefore = m->Read(0x10, b1, x1);
        bool ok = false;
        for (int i = 0; i < 3 && !ok; i++) {
            ok = m->WriteNow(0x04, 0);
            if (!ok) WakeUp(m);
        }
        Sleep(1500);
        bool hasAfter = m->Read(0x10, b2, x2);
        if (ok && hasBefore && hasAfter && b1 == b2)
            PostStatus("显示器未响应出厂重置指令（固件可能不支持远程恢复），建议使用『一键还原基准』恢复参数");
        else
            PostStatus(ok ? "已发送恢复出厂指令 — 显示器可能进入省电模式，请按显示器按键退出"
                          : "恢复出厂失败（DDC 无响应）");
    }).detach();
}
static void PaintConfirmReset() {
    auto dim = Brush(Col(0.f, 0.f, 0.f, 0.62f));
    g_rt->FillRectangle(D2D1::RectF(0, 0, (float)g_W, (float)g_H), dim);
    dim->Release();
    int w = SI(470), hh = SI(252);
    RECT card = { (g_W - w) / 2, (g_H - hh) / 2 - SI(16), (g_W + w) / 2, (g_H + hh) / 2 - SI(16) };
    FillRRect(card, 12.f, Theme::SurfaceCard);
    { auto b = Brush(Theme::LineStrong);
      g_rt->DrawRoundedRectangle(RRect(card, 12.f), b, 1.f); b->Release(); }
    int lx = card.left + SI(20), rx = card.right - SI(20);
    DrawT("恢复出厂设置", FUi(SI(14), true),
          RectF({ lx, card.top + SI(14), rx, card.top + SI(34) }), Theme::Danger);
    DrawT("确定要让显示器恢复出厂设置吗？将清空显示器内部存储，不可撤销。",
          FUi(SI(11)), RectF({ lx, card.top + SI(42), rx, card.top + SI(62) }), Theme::TextSecondary);
    DrawT("恢复出厂设置可能会进入省电/节能模式",
          FUi(SI(12), true), RectF({ lx, card.top + SI(74), rx, card.top + SI(95) }), Theme::Danger);
    DrawT("此模式下在本程序进行任何操作都不起作用",
          FUi(SI(12), true), RectF({ lx, card.top + SI(95), rx, card.top + SI(116) }), Theme::Danger);
    DrawT("需要手动按显示器按钮退出省电/节能模式",
          FUi(SI(12), true), RectF({ lx, card.top + SI(116), rx, card.top + SI(137) }), Theme::Danger);
    // v1.6.8：部分固件（如 AOC）对远程恢复出厂无响应，提前给出替代路径
    DrawT("部分品牌固件不支持远程恢复出厂，无效时请用『一键还原基准』",
          FUi(SI(11)), RectF({ lx, card.top + SI(160), rx, card.top + SI(180) }), Theme::TextMuted);
    int by = card.bottom - SI(44);
    g_confirmYes = { rx - SI(112), by, rx, by + SI(30) };
    g_confirmNo  = { g_confirmYes.left - SI(10) - SI(88), by, g_confirmYes.left - SI(10), by + SI(30) };
    bool hy = Hover(g_confirmYes);
    FillRRect(g_confirmYes, 8.f, hy ? Theme::Danger.Dark(0.12f) : Theme::Danger);
    DrawT("确认恢复", FUi(SI(12), true, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
          RectF(g_confirmYes), Theme::OnAccent);
    bool hn = Hover(g_confirmNo);
    FillRRect(g_confirmNo, 8.f, hn ? Theme::SurfaceHover : Theme::SurfaceSub);
    DrawT("取消", FUi(SI(12), false, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
          RectF(g_confirmNo), Theme::TextPrimary);
}
static void PaintStatusBar() {
    int y = g_H - StatusBarH();
    auto b = Brush(Theme::LineSubtle);
    g_rt->DrawLine(D2D1::Point2F(0, (float)y), D2D1::Point2F((float)g_W, (float)y), b, 1.f);
    b->Release();
    {
        auto* sb = Brush(Theme::BgStatusBar);
        g_rt->FillRectangle(D2D1::RectF(0, (float)(y + 1), (float)g_W, (float)g_H), sb);
        sb->Release();
    }
    D2D1_ELLIPSE dot = D2D1::Ellipse(D2D1::Point2F(23.5f, (float)y + 17.5f), 3.5f, 3.5f);
    auto db = Brush(g_statusDot);
    g_rt->FillEllipse(dot, db); db->Release();
    DrawT(g_statusText, FUi(SI(11)), D2D1::RectF(34.f, (float)(y + 12), 34.f + 800.f, (float)(y + 32)), Theme::TextTertiary);
    DrawT("Ctrl+Alt+O 显示/隐藏   Ctrl+Alt+R 还原   Ctrl+Alt+S 存基准",
          FMono(SI(11), false, DWRITE_TEXT_ALIGNMENT_TRAILING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
          D2D1::RectF((float)(g_W - 430), (float)(y + 12), (float)(g_W - 20), (float)(y + 32)), Theme::TextMuted);
}
static void PaintToast() {
    if (!g_toastUntil || GetTickCount64() >= g_toastUntil) return;
    int x = (g_W - 400) / 2, y = g_H - 96;
    FillRRect({ x, y, x + 400, y + 38 }, 10.f, Col(Theme::SurfaceSub.r, Theme::SurfaceSub.g, Theme::SurfaceSub.b, 230.f / 255.f));
    DrawT(g_statusText, FUi(SI(12), false, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
          D2D1::RectF((float)x, (float)y, (float)(x + 400), (float)(y + 38)), Theme::TextPrimary);
}
// ---- 规范 §3.5 氛围光：左上 → 右下 45° ----
// 用 accent 原色（用户反馈派生的光晕色偏褐，故不用 §3.6 的 glow），靠低 alpha 铺氛围。
// 浓度旋钮（嫌深就调小 kAmbA0）：起始 alpha / 中段 alpha / 中段位置。
static const float kAmbA0 = 0.16f, kAmbA1 = 0.032f, kAmbMid = 0.50f;
static void PaintAmbient(ID2D1RenderTarget* rt, float w, float h) {
    if (!rt || w <= 0 || h <= 0) return;
    ID2D1GradientStopCollection* gs = nullptr;
    D2D1_GRADIENT_STOP stops[3] = {
        { 0.0f,    Col(Theme::Accent.r, Theme::Accent.g, Theme::Accent.b, kAmbA0).F() },
        { kAmbMid, Col(Theme::Accent.r, Theme::Accent.g, Theme::Accent.b, kAmbA1).F() },
        { 1.0f,    Col(Theme::Accent.r, Theme::Accent.g, Theme::Accent.b, 0.f).F() },
    };
    if (FAILED(rt->CreateGradientStopCollection(stops, 3, D2D1_GAMMA_2_2, D2D1_EXTEND_MODE_CLAMP, &gs)) || !gs) return;
    ID2D1LinearGradientBrush* lgb = nullptr;
    D2D1_LINEAR_GRADIENT_BRUSH_PROPERTIES lp = {};
    lp.startPoint = D2D1::Point2F(0.f, 0.f);   // 浓端在左上角
    lp.endPoint   = D2D1::Point2F(w, h);       // 衰减到右下角归零
    if (SUCCEEDED(rt->CreateLinearGradientBrush(lp, gs, &lgb)) && lgb) {
        rt->FillRectangle(D2D1::RectF(0, 0, w, h), lgb);
        lgb->Release();
    }
    gs->Release();
}
// ================= 关于页（v1.6.8：主窗口第三页签） =================
// 布局沿用「先算矩形后绘制、命中矩形存全局」的模式：按钮矩形写 g_aboutBtnUpd/g_aboutBtnFb，
// WndProc 里 PtIn 命中后调 OpenUpdateWindow——与系统管理页 SysClick 同一套路。
static void PaintAboutTab() {
    // v1.6.11：内联原更新弹窗内容——软件更新（四宫格/说明/进度/主按钮）、意见反馈入口、
    // 关于信息（版本/链接/作者）已内联主窗口「关于」页签。反馈表单含自绘文本框仍走弹窗（OpenUpdateWindow()）。
    int x = SI(196), cw = g_contentW;
    int y = SI(76);
    const int gap = SI(16);

    EnterCriticalSection(&g_netCs);
    int ustate = g_upd.state, uprog = g_upd.progress;
    std::string umsg = g_upd.msg;
    UpdateInfo ui = g_upd.info;
    LeaveCriticalSection(&g_netCs);

    // ---- 卡1：软件更新（内联原「更新中心」页签）----
    const int h1 = SI(268);
    RECT card1 = { x, y, x + cw, y + h1 };
    FillRRect(card1, 12.f, Theme::SurfaceGroup);
    int ix = x + SI(14), iw = cw - SI(28);
    int ry = y + SI(8);
    DrawT("软件更新", FUi(SI(12), true),
          RectF({ ix, ry, ix + iw, ry + SI(20) }), Theme::TextPrimary);
    ry += SI(20);
    // 状态行：空闲显版本；检查/下载/出错时显状态文案（与弹窗同一 msg 源）
    {
        Col mc = (ustate == UPD_ERROR) ? Theme::Danger : Theme::TextMuted;
        std::string line = umsg.empty() ? std::string("当前版本 v") + kAppVersion : umsg;
        DrawT(line, FUi(SI(11)), RectF({ ix, ry, ix + iw, ry + SI(16) }), mc);
    }
    ry += SI(18);
    // 进度条：活动状态才显示（预留高度固定，防布局跳动）
    if (ustate == UPD_CHECKING || ustate == UPD_DOWNLOADING || ustate == UPD_READY || ustate == UPD_AVAILABLE) {
        int pct = ustate == UPD_CHECKING ? 35 : ustate == UPD_DOWNLOADING ? uprog : 100;
        RECT bar = { ix, ry + SI(2), ix + iw, ry + SI(6) };
        FillRRect(bar, 2.f, Theme::SurfaceHover);
        int fw = iw * pct / 100;
        if (fw > SI(4)) FillRRect({ ix, ry + SI(2), ix + fw, ry + SI(6) }, 2.f, Theme::Accent);
    }
    ry += SI(12);
    // 四宫格：最新版本 / 更新通道 / 安装包大小 / 完整性校验（照搬弹窗数据源）
    {
        const char* t[4] = { "最新版本", "更新通道", "安装包大小", "完整性校验" };
        std::string v[4] = {
            ui.version.empty() ? "—" : "v" + ui.version,
            g_config.updateChannel.empty() ? "latest" : g_config.updateChannel,
            ui.size > 0 ? (std::to_string((long long)((ui.size + 512 * 1024) / 1024 / 1024)) + " MB") : "—",
            ui.sha256.empty() ? "待服务器提供 SHA-256" : "SHA-256 已就绪"
        };
        int gap2 = SI(10), bw = (iw - gap2) / 2, bh = SI(40);
        for (int i = 0; i < 4; i++) {
            int bx = ix + (i % 2) * (bw + gap2), byy = ry + (i / 2) * (bh + gap2);
            FillRRect({ bx, byy, bx + bw, byy + bh }, 8.f, Theme::SurfaceSub);
            DrawT(t[i], FUi(SI(10)),
                  RectF({ bx + SI(10), byy + SI(5), bx + bw - SI(10), byy + SI(18) }), Theme::TextMuted);
            DrawT(v[i], FUi(SI(12), true),
                  RectF({ bx + SI(10), byy + SI(19), bx + bw - SI(10), byy + bh - SI(4) }), Theme::TextPrimary);
        }
        ry += bh * 2 + gap2;
    }
    ry += SI(6);
    // 更新说明
    {
        int nh = SI(72);
        FillRRect({ ix, ry, ix + iw, ry + nh }, 8.f, Theme::SurfaceSub);
        DrawT("更新说明", FUi(SI(10)),
              RectF({ ix + SI(10), ry + SI(6), ix + iw - SI(10), ry + SI(20) }), Theme::TextMuted);
        std::string notes;
        if (ui.notes.empty()) notes = "—";
        else for (auto& n : ui.notes) notes += "• " + n + "\r\n";
        DrawT(notes, FUi(SI(11)),
              RectF({ ix + SI(10), ry + SI(22), ix + iw - SI(10), ry + nh - SI(6) }), Theme::TextSecondary);
        ry += nh + SI(8);
    }
    // 按钮行：主按钮（状态机文案，有更新时 accent 高亮）+ 查看更新日志
    {
        bool hot = (ustate == UPD_AVAILABLE || ustate == UPD_READY);
        const char* pl = ustate == UPD_AVAILABLE ? "立即更新" : ustate == UPD_READY ? "重启并完成更新"
                       : ustate == UPD_CHECKING ? "检查中…" : ustate == UPD_DOWNLOADING ? "下载中…" : "检查更新";
        int bw1 = (std::max)(SI(120), (int)MeasureW(pl, FUi(SI(12), true)) + SI(28));
        g_aboutBtnUpd = { ix, ry, ix + bw1, ry + SI(30) };
        int bw2 = (std::max)(SI(110), (int)MeasureW("查看更新日志", FUi(SI(12), true)) + SI(26));
        g_aboutBtnLog = { g_aboutBtnUpd.right + SI(12), ry, g_aboutBtnUpd.right + SI(12) + bw2, ry + SI(30) };
        if (hot) {
            FillRRect(g_aboutBtnUpd, 8.f, Theme::Accent);
            DrawT(pl, FUi(SI(12), true, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
                  RectF(g_aboutBtnUpd), Col(1, 1, 1, 1));
        } else {
            DrawBtn(g_aboutBtnUpd, pl, false);
        }
        DrawBtn(g_aboutBtnLog, "查看更新日志", false);
        ry += SI(30);
    }
    y += h1 + gap;

    // ---- 卡2：意见反馈（表单含自绘文本框，仍走弹窗）----
    {
        int h2 = SI(64);
        RECT card2 = { x, y, x + cw, y + h2 };
        FillRRect(card2, 12.f, Theme::SurfaceGroup);
        DrawT("意见反馈", FUi(SI(12), true),
              RectF({ x + SI(14), y + SI(8), x + cw - SI(170), y + SI(28) }), Theme::TextPrimary);
        DrawT("遇到问题或想提建议？告诉我们（可自动附带上运行日志）", FUi(SI(11)),
              RectF({ x + SI(14), y + SI(32), x + cw - SI(170), y + SI(50) }), Theme::TextMuted);
        int bw = (std::max)(SI(110), (int)MeasureW("打开反馈窗口", FUi(SI(12), true)) + SI(26));
        g_aboutBtnFb = { x + cw - SI(14) - bw, y + SI(17), x + cw - SI(14), y + SI(47) };
        DrawBtn(g_aboutBtnFb, "打开反馈窗口", false);
        y += h2 + gap;
    }

    // ---- 卡3：关于 ----
    {
        int h3 = SI(140);
        RECT card3 = { x, y, x + cw, y + h3 };
        FillRRect(card3, 12.f, Theme::SurfaceGroup);
        int ix3 = x + SI(14), iw3 = cw - SI(28);
        int ry3 = y + SI(8);
        DrawT("关于", FUi(SI(12), true),
              RectF({ ix3, ry3, ix3 + iw3, ry3 + SI(20) }), Theme::TextPrimary);
        ry3 += SI(20);
        DrawT(std::string(AppDisplayName()) + " v" + kAppVersion, FUi(SI(18), true),
              RectF({ ix3, ry3, ix3 + iw3, ry3 + SI(28) }), Theme::TextPrimary);
        ry3 += SI(28);
        {
            std::string meta = std::string("更新通道 ") + (g_config.updateChannel.empty() ? "latest" : g_config.updateChannel.c_str())
                             + " · 作者 " + kAuthorName;
            DrawT(meta, FUi(SI(11)),
                  RectF({ ix3, ry3, ix3 + iw3, ry3 + SI(16) }), Theme::TextMuted);
        }
        ry3 += SI(22);
        int lx = ix3;
        auto linkBtn = [&](RECT& r, const char* label) {
            int w = (std::max)(SI(96), (int)MeasureW(label, FUi(SI(11), true)) + SI(24));
            r = { lx, ry3, lx + w, ry3 + SI(26) };
            DrawBtn(r, label, false);
            lx = r.right + SI(10);
        };
        linkBtn(g_aboutLnkDir, "打开程序目录");
        linkBtn(g_aboutLnkLog, "查看日志");
        linkBtn(g_aboutLnkCopy, g_aboutCopied.empty() ? "复制版本信息" : g_aboutCopied.c_str());
        y += h3 + gap;
    }

    // ---- 底部落款 ----
    DrawT("Copyright (C) 2026 咸鱼工作室 · 纪念那些在深夜里依然亮着的屏幕。",
          FUi(SI(11)), RectF({ x, y, x + cw, y + SI(20) }), Theme::TextMuted);
}
static void PaintFrame() {
    if (!g_rt) CreateDeviceResources();
    if (!g_rt) return;
    g_rt->BeginDraw();
    // 背景 + 氛围光（左上→右下 45°，accent 原色低 alpha：0.16 → 0.03 → 0）
    g_rt->Clear(Theme::BgWindow.F());
    if (g_probing) {
        // 启动加载动画：旋转弧 + 文案（探测完成后自动进入正常界面）
        DWORD t = GetTickCount64() % 1000;
        float ang = t / 1000.0f * 6.28318f;
        float cxp = (float)g_W / 2, cyp = (float)g_H / 2 - (float)SI(20);
        D2D1_POINT_2F ctr = D2D1::Point2F(cxp, cyp);
        float rr = SI(26);
        for (int i = 0; i < 8; i++) {
            float a = ang + i * 6.28318f / 8;
            float alpha = 0.15f + 0.75f * i / 8;
            auto b = Brush(Col(Theme::Accent.r, Theme::Accent.g, Theme::Accent.b, alpha));
            float x = ctr.x + cosf(a) * rr, yy = ctr.y + sinf(a) * rr;
            g_rt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, yy), SI(3.5f), SI(3.5f)), b);
            b->Release();
        }
        DrawT("正在检测显示器…", FUi(SI(12), true, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
              D2D1::RectF(ctr.x - 150.f, ctr.y + rr + 18.f, ctr.x + 150.f, ctr.y + rr + 40.f), Theme::TextSecondary);
        g_rt->EndDraw();
        return;
    }
    PaintAmbient(g_rt, (float)g_W, (float)g_H);
    PaintTitleBar();
    PaintTabs();
    PaintMonitorCards();      // 两个页签共用侧栏显示器卡
    PaintDisplayOptions();    // 选中卡内的 分辨率/刷新率 下拉
    if (g_page == 0) {
        PaintTopButtons();
        // 参数页可滚动：内容裁剪在标题线以下、状态栏以上
        D2D1_RECT_F clip = D2D1::RectF(0, (float)SI(37), (float)g_W, (float)(g_H - StatusBarH()));
        g_rt->PushAxisAlignedClip(clip, D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
        PaintContent();
        g_rt->PopAxisAlignedClip();
    }
    else if (g_page == 2) { PaintTopButtons(); PaintAboutTab(); }   // v1.6.8：关于页，一键还原按钮照常可用
    else PaintSystemPage();
    PaintStatusBar();
    PaintToast();
    if (g_confirmReset) PaintConfirmReset();
    if (g_pfOpen) PaintProfilePanel();   // v1.6.7：基准档案弹层（在详细信息之上）
    if (g_infoOpen) PaintInfoPanel();
    if (g_ddMode) PaintDisplayDropdown();
    if (FAILED(g_rt->EndDraw())) { g_rt->Release(); g_rt = nullptr; }
}

// ================= 交互 =================
static Item* HitRow(POINT pt) {
    for (auto& G : g_menu)
        for (auto& it : G.items)
            if (it.visible && PtIn(it.rowR, pt)) return &it;
    return nullptr;
}
static void HitSeg(POINT pt, Item** outItem, Item::Seg** outSeg) {
    *outItem = nullptr; *outSeg = nullptr;
    for (auto& G : g_menu)
        for (auto& it : G.items) {
            if (!it.visible) continue;
            for (auto& s : it.segs)
                if (PtIn(s.r, pt)) { *outItem = &it; *outSeg = &s; return; }
        }
}
static MonitorDev* HitMon(POINT pt) {
    for (size_t i = 0; i < g_monRects.size(); i++)
        if (PtIn(g_monRects[i], pt) && i < g_mons.size()) return g_mons[i];
    return nullptr;
}
static void ShowToast(const std::string& msg) {
    g_statusText = msg;
    g_statusDot = Theme::Accent;
    g_toastUntil = GetTickCount64() + 2500;
    Invalidate();
}
static void SetVcp(Item* it, unsigned v, const std::string& label) {
    const ValSpec* s = SpecOfItem(it);
    if (!s || !g_cur) return;
    g_cur->BeginWrite(s->code, v);
    LogLine("SET " + MonitorKey(g_cur) + " " + it->en + " -> " + std::to_string(v));
    g_lastChangeTick = GetTickCount64();
    ShowToast(std::string("已设置：") + it->cn + " = " + label);
    // 色温切换会改变用户RGB行的显示，需要重排布局
    if (it->mapId && strcmp(it->mapId, "colortemp") == 0) LayoutAll();
    Invalidate();
}
static void SliderSetFromX(Item* it, RECT tr, int x) {
    Eff e = Resolve(it);
    if (!e.Ok || e.EnumMode) return;
    g_dragItem = it;
    g_draggingSlider = true;
    if (tr.right <= tr.left) return;
    const ValSpec* s = SpecOfItem(it);
    if (!s) return;
    float frac = ClampF((float)(x - tr.left) / (float)(tr.right - tr.left), 0.f, 1.f);
    int ui = e.Lo + (int)(frac * (e.Hi - e.Lo) + 0.5f);
    unsigned v = s->VcpFromUi(ui);
    unsigned cur = 0;
    bool has = false;
    if (g_cur) {
        std::lock_guard<std::recursive_mutex> il(g_cur->infoMx);
        has = g_cur->cache.count(s->code) > 0;
        if (has) cur = g_cur->cache[s->code];
        if (!has || cur != v) g_cur->cache[s->code] = v;   // 界面立即跟手
    }
    if (!has || cur != v) {
        // 实时生效：写队列合并去重，高频拖动只写最新值
        g_cur->BeginWrite(s->code, v);
        g_lastChangeTick = GetTickCount64();
    }
    InvalidateRect(g_hwnd, &it->rowR, FALSE);   // 只重绘滑条行，拖动丝滑
}
static void SaveBaseline() {
    if (!g_cur) return;
    Baseline bl;
    {
        std::lock_guard<std::recursive_mutex> il(g_cur->infoMx);
        // v1.5.3 全量快照：保存缓存里「当前读到的所有值」——含菜单外未知功能的码。
        // 用途：显示器本体恢复出厂会进省电模式（DDC 关闭无法操作），改用本程序快照/还原代替。
        // 恢复端 RestoreOne 按码直写，未知码天然支持。
        bl.vals = g_cur->cache;
    }
    // 动作类码与只读状态码绝不入基准（回写 0x04=恢复出厂/0x08=保存设置适得其反；
    // 0x02/0x03/0x05 是 MCCS 只读状态码，回写无意义还污染快照对比）
    // P0-4 修复：0x60=输入源（DP/HDMI 切换——客户机在线 HDMI 而基准来自 DP → 黑屏）、
    // 0xD6=电源模式（可能触发节能假死）——改变链路状态的码绝不入基准
    for (auto it = bl.vals.begin(); it != bl.vals.end(); ) {
        int c = it->first;
        // 0xAE=刷新率模式也不入基准（还原写它触发显示模式切换 → 重枚举 → 转圈/写断）
        // 0xFA=动态码（还原写它每次都回弹，实测）；同列的 0x60/0xD6/0xAE 见上
        if (c == 0x60 || c == 0xD6 || c == 0xAE || c == 0xFA || IsActionCode(c) || c == 0x02 || c == 0x03 || c == 0x05)
            it = bl.vals.erase(it);
        else ++it;
    }
    if (bl.vals.empty()) { ShowToast("没有可保存的 DDC 项"); return; }
    bl.srcModel = DisplayNameOf(g_cur);
    { std::lock_guard<std::recursive_mutex> il(g_cur->infoMx); bl.srcSerial = g_cur->serial; }
    // v1.5.3 定版：全量快照写两级键——型号键（同型号全量恢复）+ 系列键（跨型号恢复时供审计过滤）
    g_profiles[MonitorKey(g_cur)] = bl;
    g_profiles[SeriesOfModelKey(MonitorKey(g_cur))] = bl;
    SaveProfiles();
    LogLine("保存基准 " + MonitorKey(g_cur) + " " + std::to_string(bl.vals.size()) + " 项（全量快照）");
    ShowToast("已保存基准（" + std::to_string(bl.vals.size()) + " 项全量快照）");
}
// v1.6.6 修复 1：还原重入守卫（声明已前移至 g_restoring 旁，供 PaintTopButtons 使用）。
// 原实现三个入口（主按钮/系统页菜单/热键）都可并发触发 RestoreWorker，用户「等不及再点一次」
// 就起第二个还原线程——两个线程交错写 DDC，WriteTrack 的 3 秒窗口被互相污染，缓存被刷成过渡值，
// UI 显示「还原完又变回旧样子」，用户更确定「没效果」→ 再点，形成恶性循环（日志里 1 分 39 秒连点 4 次即实证）。
static void RestoreWorker() {
    if (g_restoreBusy.exchange(true)) {
        ShowToast("还原正在进行中，请稍候…");
        return;
    }
    // v1.5.3：点击即时反馈（全量快照 50+ 项 × ~60ms 写入，完成提示要等数秒）
    ShowToast("正在还原基准…（全量快照写入中）");
    Invalidate();   // 立即重绘：按钮显示「还原中…」禁用态，给用户明确反馈
    std::thread([]() {
        int n = RestoreAllInternal();
        g_restoreBusy.store(false);
        std::string msg = n == -2 ? "未检测到显示器"
                      : n == -1 ? "没有已保存的基准档案（同系列键不匹配？看 log.txt 的还原行）"
                      : n == 0 && g_restoreSame > 0
                                ? "已是最优状态（" + std::to_string(g_restoreSame) + " 项与基准一致，无需改动）"
                      : n == 0  ? "基准存在但还原 0 项（DDC 写入全部失败？确认显示器唤醒）"
                                : "一键还原完成（写 " + std::to_string(n) + " 项" +
                                  (g_restoreSame > 0 ? "，" + std::to_string(g_restoreSame) + " 项本就一致" : "") + "）";
        PostStatus(msg);
        // 还原完成后重绘一次，让按钮从「还原中…」恢复常态
        if (g_hwnd) PostMessageW(g_hwnd, WM_APP + 2, 0, 0);
    }).detach();
}
static void RefreshAllValues(bool allowWake) {
    if (!g_cur) return;
    // 抓拍当前显示器与品牌，避免刷新期间切换显示器导致竞态（与 C# 捕获 m/root 同效）
    MonitorDev* m = g_cur;
    int brand = m->brand;
    // v1.5.3：开启了闲置还原才做基准比对（捕获到值里，避免工作线程读全局配置的竞态）
    bool watchDev = g_config.autoRestoreMinutes > 0;
    // v1.5.8 P1-4：线程持引用。这是之前漏掉的一处——3 秒轮询持续抓 g_cur 裸指针，
    // 重探时旧对象被 retire（handle 释放），线程醒来访问 m 就是访问已释放内存。
    std::thread([m, brand, allowWake, watchDev]() {
        MonitorDev::Guard guard(m);          // 构造即 AddRef，任何提前 return / 抛出都保证释放
        unsigned c, x;
        if (m->IsRetired()) return;
        // OSD 恢复默认可能触发节能假死，先探测；3s 轮询（allowWake=false）不打扰休眠显示器
        if (!m->Read(0x10, c, x)) {
            if (!allowWake) return;
            WakeUp(m);
        }
        // 先全部读进暂存区，再一次合并进缓存：界面全程只见稳定值，不再"先清后读"闪烁。
        // 回读失败的码保留旧值（瞬态 DDC 失败不闪「…」）；写队列里待写的码不覆盖。
        std::map<int, unsigned> fresh;
        for (auto& G : BuildMenu(brand))
            for (auto& it : G.items) {
                if (!it.mapId) continue;
                auto sp = SpecOf(brand);
                auto spec = sp->find(it.mapId);
                if (spec == sp->end() || spec->second.code == 0) continue;
                if (m->Read(spec->second.code, c, x)) fresh[spec->second.code] = c;
            }
        {
            std::lock_guard<std::recursive_mutex> il(m->infoMx);
            ULONGLONG nowTick = GetTickCount64();
            for (auto& kv : fresh) {
                bool pending = false;
                { std::lock_guard<std::mutex> pl(m->pendMx);
                  pending = m->hasPend && m->pend.count(kv.first) > 0; }
                if (!pending) {
                    // v1.6.6：写后保护窗口（与 Read 内部同规则）——过渡值不覆盖缓存
                    auto wt = m->lastWrite.find(kv.first);
                    if (wt != m->lastWrite.end() && nowTick - wt->second.tick < 3000 &&
                        kv.second != wt->second.target)
                        continue;
                    m->cache[kv.first] = kv.second;
                }
            }
        }
        // v1.5.3 P1-1 修复：闲置自动还原原来只认"本程序改参数"（g_lastChangeTick 仅在
        // SetVcp / SliderSetFromX 里置位）。网吧真实场景是顾客直接按显示器物理按键调乱，
        // 此时计时器从未启动 → 闲置还原永不触发，功能等于不存在。
        // 这里在已有的轮询读值里顺带比对基准：只要有编码与基准不符就刷新计时起点。
        if (watchDev) {
            auto itB = g_profiles.find(MonitorKey(m));
            if (itB != g_profiles.end() && !itB->second.vals.empty()) {
                bool deviated = false;
                std::lock_guard<std::recursive_mutex> il(m->infoMx);
                for (auto& bv : itB->second.vals) {
                    if (bv.first == 0x60 || bv.first == 0xD6) continue;   // 输入源/电源不参与判定
                    auto cv = m->cache.find(bv.first);
                    if (cv != m->cache.end() && cv->second != bv.second) { deviated = true; break; }
                }
                if (deviated) g_lastChangeTick = GetTickCount64();
            }
        }
        PostMessageW(g_hwnd, WM_APP + 2, 0, 0);
    }).detach();
}
static void ApplyMonitor() {
    g_dragItem = nullptr;
    g_draggingSlider = false;
    // v1.6.6 修复 H2：热键切屏（Ctrl+Alt+←/→）时若用户正拖着滑条未松鼠标，必须清捕获——
    // 原实现只清 g_dragItem，下一帧 WM_MOUSEMOVE 的「MK_LBUTTON && g_mouseDown」分支会用
    // 新 g_menu 里同一行的 item 重新调 SliderSetFromX，把当前鼠标 x 对应的值写到新显示器上。
    g_mouseDown = false;
    if (GetCapture() == g_hwnd) ReleaseCapture();
    // v1.6.6 修复 H4：切显示器时关掉仍显示旧机器信息的弹窗/下拉/确认卡——
    // 这些面板的内容是打开时的快照，不随切屏更新会让用户对着旧显示器的信息误操作。
    g_infoOpen = false;
    g_confirmReset = false;
    g_ddMode = 0;
    g_pfOpen = false;      // v1.6.7：切显示器后基准档案列表需要重算（当前显示器标记会变）
    g_scroll0 = 0;              // 切显示器后回到页首
    BuildDisplayOptions();      // 分辨率/刷新率选项跟随当前显示器
    // v1.5：未知品牌走 caps 动态菜单（caps 未就绪时先回落通用表，深探测完成后 WM_APP+6 重建）
    g_menu = (g_cur->brand == 0 && g_cur->capsOk) ? BuildCapsMenu(g_cur) : BuildMenu(g_cur->brand);
    // 手动指定优先（Skin=#RRGGBB 或 AccentMode=manual + ManualAccent），否则按 EDID 厂商 ID 自动
    std::string manual;
    auto isHex = [](const std::string& s) {
        return s.size() == 7 && s[0] == '#'; };
    if (isHex(g_config.skin)) manual = g_config.skin;
    if (g_config.accentMode == "manual" && isHex(g_config.manualAccent)) manual = g_config.manualAccent;
    Theme::ResolveAccent(g_cur->vendor, manual.empty() ? nullptr : &manual);
    LayoutAll();
    Invalidate();
}
// 调试预览：强制品牌界面（无对应硬件也能检查菜单/码表/Logo/主题色）
static void ApplyDebugBrand(int b) {
    g_debugBrand = b;
    g_config.debugBrand = b;
    SaveConfig();
    for (auto* m : g_mons) m->brand = b ? b : DetectBrand(m);
    ApplyMonitor();
    const char* vkey = b == 1 ? "BNQ" : b == 2 ? "ACI" : b == 3 ? "AOC" :
                       b == 5 ? "HKC" : b == 6 ? "MSI" : b == 7 ? "GSM" :
                       b == 8 ? "SEC" : b == 9 ? "DEL" : b == 10 ? "ACR" : b == 11 ? "HWP" : "";
    if (b && g_cur) Theme::ResolveAccent(vkey, nullptr);   // 强调色跟随预览品牌
    RefreshAllValues(false);   // 按新品牌码表回读，界面立即有值
}
static void SwitchMonitor(int dir) {
    if (g_mons.size() < 2 || !g_cur) { ShowToast("只有一台显示器"); return; }
    int i = (int)(std::find(g_mons.begin(), g_mons.end(), g_cur) - g_mons.begin());
    i = (i + dir + (int)g_mons.size()) % (int)g_mons.size();
    g_cur = g_mons[i];
    ApplyMonitor();
    ShowToast("已切换到 " + DisplayNameOf(g_cur));
}

// ================= 托盘 =================
#define WM_TRAY (WM_APP + 5)
#define TRAY_SHOWHIDE 1001
#define TRAY_RESTORE  1002
#define TRAY_SAVE     1003
#define TRAY_TOOLS    1004
#define TRAY_EXIT     1005
#define TRAY_TEST     1006
static HICON MakeColorIcon(int r, int g, int b) {
    BITMAPINFO bi = {};
    bi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
    bi.bmiHeader.biWidth = 16;
    bi.bmiHeader.biHeight = -16;
    bi.bmiHeader.biPlanes = 1;
    bi.bmiHeader.biBitCount = 32;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    HBITMAP mask = CreateBitmap(16, 16, 1, 1, nullptr);
    if (bits) {
        for (int i = 0; i < 16 * 16; i++)
            ((DWORD*)bits)[i] = ((DWORD)255 << 24) | ((DWORD)r << 16) | ((DWORD)g << 8) | (DWORD)b;
    }
    ICONINFO ii = {};
    ii.fIcon = TRUE;
    ii.hbmMask = mask;
    ii.hbmColor = color;
    HICON h = CreateIconIndirect(&ii);
    DeleteObject(color); DeleteObject(mask);
    ReleaseDC(nullptr, dc);
    return h;
}
static void InitTray() {
    // 应用图标（exe 内嵌资源）；失败回落品牌色方块
    g_trayIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    if (!g_trayIcon) {
        int r = (int)(Theme::Accent.r * 255), g = (int)(Theme::Accent.g * 255), b = (int)(Theme::Accent.b * 255);
        g_trayIcon = MakeColorIcon(r, g, b);
    }
    ZeroMemory(&g_nid, sizeof(g_nid));
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY;
    g_nid.hIcon = g_trayIcon;
    std::wstring tip = Utf8ToWide(AppDisplayName());
    wcscpy_s(g_nid.szTip, tip.c_str());
    Shell_NotifyIconW(NIM_ADD, &g_nid);
    SendMessageW(g_hwnd, WM_SETICON, ICON_SMALL, (LPARAM)g_trayIcon);
    SendMessageW(g_hwnd, WM_SETICON, ICON_BIG, (LPARAM)g_trayIcon);
}
static void ShowTrayMenu() {
    HMENU m = CreatePopupMenu();
    AppendMenuW(m, MF_STRING, TRAY_SHOWHIDE, L"显示 / 隐藏");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, TRAY_TOOLS, L"网管工具（扫描/校准）");
    AppendMenuW(m, MF_STRING, TRAY_TEST, L"显示器测试（坏点/灰阶/拖影）");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, TRAY_EXIT, L"退出");
    POINT pt; GetCursorPos(&pt);
    SetForegroundWindow(g_hwnd);
    TrackPopupMenu(m, TPM_RIGHTBUTTON, pt.x, pt.y, 0, g_hwnd, nullptr);
    DestroyMenu(m);
}
static void ToggleVisible() {
    if (IsWindowVisible(g_hwnd)) { ShowWindow(g_hwnd, SW_HIDE); return; }
    ShowWindow(g_hwnd, SW_SHOW);
    SetForegroundWindow(g_hwnd);
}

// ================= 圆角 =================
static void ApplyRounded() {
    int pref = 2 /* DWMWCP_ROUND */;
    // Win11 走 DWM 原生；失败（Win10）用 Region 圆角兜底
    if (FAILED(DwmSetWindowAttribute(g_hwnd, 33 /* DWMWA_WINDOW_CORNER_PREFERENCE */, &pref, sizeof(pref)))) {
        if (g_roundRgn) DeleteObject(g_roundRgn);
        g_roundRgn = CreateRoundRectRgn(0, 0, g_W + 1, g_H + 1, SI(16), SI(16));
        // v1.5.3 P2-4：SetWindowRgn 成功后 region 所有权归系统，不能再用 DeleteObject 释放；
        // 原来把句柄一直留在 g_roundRgn 里，下次改尺寸就会重复释放系统对象（未定义行为）。
        if (SetWindowRgn(g_hwnd, g_roundRgn, TRUE) == 0) {
            DeleteObject(g_roundRgn);      // 失败才由我们释放
            g_roundRgn = nullptr;
        } else {
            g_roundRgn = nullptr;          // 已移交系统，句柄不再由我们持有
        }
    }
}

// ================= 启动探测 =================
static void ProbeMonitor(MonitorDev* m) {
    // v1.5.3 全码探测：菜单码 + caps 报告的全部支持码都读一遍——
    // 缓存即完整快照，基准「全量快照」才能覆盖未知功能的码（约 50 码 × ~150ms ≈ 8s 后台）
    auto menu = BuildMenu(m->brand);
    std::set<int> codes;                       // 待读码集合 = 菜单码 ∪ caps 支持码
    for (auto& G : menu)
        for (auto& it : G.items) {
            if (!it.mapId) continue;
            auto sp = SpecOf(m->brand);
            auto spec = sp->find(it.mapId);
            if (spec != sp->end() && spec->second.code != 0) codes.insert(spec->second.code);
        }
    { std::lock_guard<std::recursive_mutex> il(m->infoMx);
      for (int c : m->supported) codes.insert(c); }
    for (int code : codes) {
        unsigned c, mx;
        if (m->Read(code, c, mx)) {
            { std::lock_guard<std::recursive_mutex> il(m->infoMx);
              if (!m->capsOk) m->supported.insert(code);
              m->cache[code] = c;                   // 深探测值入缓存：回写 ddccache.json 供秒开
              m->cacheMax[code] = mx; }
            m->ddcOk = true;                        // 任一实读成功 = DDC 存活（枚举已不再实测）
            // FixRange：菜单码的上限按实读自适应（未知码无 spec，只进缓存）
            for (auto& G : menu)
                for (auto& it : G.items) {
                    if (!it.mapId) continue;
                    auto sp = SpecOf(m->brand);
                    auto spec = sp->find(it.mapId);
                    if (spec == sp->end() || spec->second.code != code) continue;
                    ValSpec& s = const_cast<ValSpec&>(spec->second);
                    if (!s.IsEnum() && mx > 0 && (int)mx != s.uiMax) {
                        if (s.uiMin == 1 && (int)mx == s.uiMax - 1) s.uiAdd = 1;
                        else if ((int)mx >= s.uiMin && (int)mx <= 255) s.uiMax = (int)mx;
                    }
                }
        }
    }
}
// v1.6.9：富足判定阈值 = 菜单 leaf 码数（原固定 20：菜单只有约 12 个 leaf 码，
// 无缓存冷启动的二轮判定永远不满足，退化成每次启动都全码探测）。
// 旧缓存（约 50 码）⊇ 菜单码 → 照样命中富足，向后兼容。
static size_t MenuLeafCount(int brand) {
    size_t n = 0;
    for (auto& G : BuildMenu(brand))
        for (auto& it : G.items)
            if (it.mapId) n++;
    return n;
}
static void StartProbe() {
    if (g_config.restoreOnStart)
        std::thread([]() { RestoreAllInternal(); }).detach();
    const int gen = g_probeGen.fetch_add(1) + 1;
    std::thread([gen]() {
        // v1.6.1 关键修复：物理显示器句柄来自 dxva2（WMI/COM 底层），在没有 COM 初始化的
        // 线程里 GetPhysicalMonitorsFromHMONITOR 会「成功」但返回空句柄（实测 hPhysicalMonitor=0），
        // 之后所有 DDC 调用全失败、整页显示「不可用」。主线程（wWinMain 已 CoInitialize）不会出这问题，
        // 所以老版本界面能正常识别 —— 差异就在这里。
        HRESULT hrCo = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        ULONGLONG pt0 = GetTickCount64();
        auto* mons = new std::vector<MonitorDev*>(Enumerate(false));
        LogLine("探测计时: 枚举+EDID=" + std::to_string(GetTickCount64() - pt0) + "ms");
        // 命中本地缓存：立即恢复 caps/全码值，界面第一屏即全量（无「值未知」等待）
        for (auto* m : *mons) LoadDdcCache(m);
        // v1.5.3 提速：WMI 查询冷启动可达数秒，EDID 完整（型号+序列号+制造商齐）时直接跳过；
        // 只有身份残缺的机器才补查 WMI（兜底身份）
        bool needWmi = false;
        for (auto* m : *mons)
            if (m->edidModel.empty() || m->serial.empty() || m->manufacturerName.empty()) { needWmi = true; break; }
        if (needWmi) {
            LoadWmiMonitorIDs();
            for (auto* m : *mons) {
                std::string wf, ws, wm;
                if (!WmiLookup(m->productSubkey, wf, ws, wm)) continue;
                std::lock_guard<std::recursive_mutex> il(m->infoMx);
                if (m->edidModel.empty() && !wf.empty()) m->edidModel = TrimStr(wf);
                if (m->serial.empty() && !ws.empty()) m->serial = ws;
                if (m->manufacturerName.empty() && !wm.empty()) m->manufacturerName = wm;
            }
        }
        // 先把列表交给界面（EDID/品牌已就绪，秒出首屏），caps+全码探测留在后台
        // v1.6.4：缓存富足 → 探测线程直接放行主界面（恢复 v1.5 的"打开就到主界面"手感——
        // 0.5s 内进界面，存活验证/深探测标记在后台完成）。无缓存/缓存不完整仍等深探测（无数据可显示）。
        bool allRich = !mons->empty();
        for (auto* m : *mons) {
            std::lock_guard<std::recursive_mutex> il(m->infoMx);
            if (m->cache.size() < MenuLeafCount(m->brand)) { allRich = false; break; }   // v1.6.9：自适应阈值
        }
        if (allRich) g_probing = false;
        LogLine("探测: 列表就绪（EDID/缓存完成）");
        LogLine("探测计时: 列表就绪总耗时=" + std::to_string(GetTickCount64() - pt0) + "ms");
        PostMessageW(g_hwnd, WM_APP + 7, (WPARAM)gen, (LPARAM)new std::vector<MonitorDev*>(*mons));
        // ① 立即落盘「缓存已有」的部分——caps 挂起机型（部分 ZOWIE）不拖累保存，
        //    用户中途关程序也不丢已确认的数据
        SaveDdcCacheAll(*mons);
        for (auto* m : *mons) {
            // v1.6.9 时序：菜单码探测先行、caps 随后纯后台——本机固件 caps 单次请求可占住
            // DDC 总线数秒，若先发起 caps 查询，ProbeMonitor 的菜单码读值会被总线阻塞，
            // 界面全量可用被拖到 5s+。菜单码仅约 12 个（≈1s），读完即深探测完成、界面全量；
            // caps/声明码补读全部留在后台（AfterCapsReady 收尾），对界面无感知。
            // v1.6.4 提速：缓存命中（全码值已覆盖全部菜单 leaf 码）跳过 ProbeMonitor 全码重读——
            // 50+ 码 × 61ms 串行读会饿死 UI 线程（转圈冻结数秒才出主界面），且值本来就在缓存里。
            // 实时变化由 3 秒后台轮询继续跟随；无缓存/缓存不完整仍走全码探测。
            bool cacheRich = false;
            { std::lock_guard<std::recursive_mutex> il(m->infoMx); cacheRich = m->cache.size() >= MenuLeafCount(m->brand); }   // v1.6.9：自适应阈值
            if (cacheRich) {
                LogLine("探测: 缓存命中跳过全码重读（" + MonitorKey(m) + "）");
                // 存活验证：一次 0x10 读（61ms）代替 50+ 次全码读——缓存不代表 DDC 活着
                // （v1.6.0 教训：命中缓存置 ddcOk=true 会掩盖真实故障），读通才如实置 OK
                unsigned c = 0, x = 0;
                if (m->Read(0x10, c, x)) m->ddcOk = true;
                else { WakeUp(m); if (m->Read(0x10, c, x)) m->ddcOk = true; }
                if (!m->capsOk) LoadCapsAsync(m);   // 缓存富足仍照常后台补 caps（capsFromCache 不算实测）
                continue;
            }
            ProbeMonitor(m);               // 静默校准：刷新各码现值与上限（菜单码 ∪ 当时已知的 supported）
            if (!m->capsOk) LoadCapsAsync(m);   // v1.6.9：菜单码读完后再发起 caps 后台查询（不与深探测抢总线）
        }
        SaveDdcCacheAll(*mons);            // ② 深探测完成后最终回写
        PostMessageW(g_hwnd, WM_APP + 6, (WPARAM)gen, 0);   // 深探测完成：带代数，界面据此丢弃过期信号
        delete mons;
        if (SUCCEEDED(hrCo)) CoUninitialize();
    }).detach();
}

// ================= 命令行（1:1 对应 Program.cs：/set /tools /restore /scan /detect）=================
static std::vector<std::wstring> g_cliArgs;
// 同时接受 /flag 与 -flag（与 C# Program.Has 一致）
static bool CliHas(const wchar_t* f1) {
    std::wstring alt = std::wstring(L"-") + (f1 + 1);
    for (auto& a : g_cliArgs)
        if (!_wcsicmp(a.c_str(), f1) || !_wcsicmp(a.c_str(), alt.c_str())) return true;
    return false;
}
static int CliIndexOf(const wchar_t* f1) {
    std::wstring alt = std::wstring(L"-") + (f1 + 1);
    for (size_t i = 0; i < g_cliArgs.size(); i++)
        if (!_wcsicmp(g_cliArgs[i].c_str(), f1) || !_wcsicmp(g_cliArgs[i].c_str(), alt.c_str()))
            return (int)i;
    return -1;
}
// 挂到父进程控制台（从 cmd/计划任务调用时可见输出）；无控制台返回 false
static bool CliAttachConsole() {
    if (!AttachConsole(ATTACH_PARENT_PROCESS)) return false;
    FILE* fp = nullptr;
    return freopen_s(&fp, "CONOUT$", "w", stdout) == 0 && fp != nullptr;
}
static void CliNote(const char* msg) {
    printf("%s\n", msg);
    fflush(stdout);
}
static void CliMessageBox(const std::string& msg, const char* title) {
    MessageBoxW(nullptr, Utf8ToWide(msg).c_str(), Utf8ToWide(title).c_str(), MB_OK | MB_ICONINFORMATION);
}

// ---- v1.3 在线更新 / 反馈的命令行形态（便于现场批处理与自动化自检）----
static std::string ArgAfter(const wchar_t* key) {
    int idx = CliIndexOf(key);
    if (idx < 0 || idx + 1 >= (int)g_cliArgs.size()) return "";
    return WideToUtf8(g_cliArgs[idx + 1]);
}
static void CliCheckUpdate() {
    bool attach = CliAttachConsole();
    // v1.5.7：未配置服务器时给明确结论，别去连占位域名再报"网络失败"
    if (UpdateBaseUnconfigured()) {
        LogLine("checkupdate: 未配置 UpdateBase（config.json 为空），跳过");
        if (attach) CliNote("CHECK 未执行: config.json 的 UpdateBase 为空（未配置更新服务器）");
        return;
    }
    std::string base = UpdateBase();
    std::string ch = g_config.updateChannel.empty() ? "latest" : g_config.updateChannel;
    HttpResp r = HttpCall(base + "/" + ch + ".json", "", false);
    if (!r.err.empty()) {
        LogLine("checkupdate: 失败 " + r.err);
        if (attach) CliNote(("CHECK 失败: " + r.err).c_str());
        return;
    }
    UpdateInfo ui;
    if (r.status != 200 || !ParseManifest(r.body, ui)) {
        if (attach) CliNote(("CHECK 失败: HTTP " + std::to_string(r.status) + " / 清单无效").c_str());
        LogLine("checkupdate: 失败 HTTP " + std::to_string(r.status));
        return;
    }
    int c = VerCmp(ui.version, kAppVersion);   // > 0 表示服务器更新
    std::string msg = "当前 " + std::string(kAppVersion) + "，服务器 " + ui.version +
                      (c > 0 ? "（有更新）" : "（已最新）");
    LogLine("checkupdate: " + msg);
    if (attach) {
        CliNote(msg.c_str());
        if (c > 0) {
            std::string dl = "下载地址：" + ui.url;
            CliNote(dl.c_str());
        }
    }
}
static void CliSelfTest() {
    bool attach = CliAttachConsole();
    auto chk = [&](const char* name, bool ok) {
        LogLine(std::string("selftest ") + name + (ok ? " OK" : " FAIL"));
        if (attach) CliNote((std::string(name) + (ok ? "  OK" : "  FAIL")).c_str());
        (void)ok;
    };
    chk("sha256(abc)", Sha256Hex("abc") ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    chk("sha256(empty)", Sha256Hex("") ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    // RFC 4231 Test Case 2：key="Jefe", data="what do ya want for nothing?"
    chk("hmac-rfc4231-2", HmacSha256B64("Jefe", "what do ya want for nothing?") ==
        "W9zBRr9gdU5qBCQmCJV1x1oAPwidJzmDnexYuWTsOEM=");
    chk("vercmp-1.3>1.2.9", VerCmp("1.3.0", "1.2.9") > 0);
    chk("vercmp-1.10>1.9",  VerCmp("1.10", "1.9") > 0);
    chk("vercmp-eq",        VerCmp("1.3", "1.3.0") == 0);
    UpdateInfo ui;
    chk("manifest-parse", ParseManifest(
        "{\"version\":\"1.4.2\",\"url\":\"https://x/y.exe\",\"sha256\":\"AB12\",\"size\":123,"
        "\"force\":true,\"notes\":[\"修复 A\",\"新增 B\"]}", ui) &&
        ui.version == "1.4.2" && ui.sha256 == "ab12" && ui.size == 123 && ui.force && ui.notes.size() == 2);
    // ---- 网络链路（依赖真实服务器；失败不影响前 7 项结论）----
    // v1.5.7：未配置 UpdateBase 时跳过而不是去连占位域名，避免把"没配服务器"报成"网络故障"
    std::string base = UpdateBase();
    if (UpdateBaseUnconfigured()) {
        if (attach) {
            CliNote("SKIP http-get  （未配置 UpdateBase，跳过网络测试）");
            CliNote("SKIP http-post （未配置 UpdateBase，跳过网络测试）");
        }
        LogLine("selftest http-get  SKIP（未配置 UpdateBase）");
        LogLine("selftest http-post SKIP（未配置 UpdateBase）");
        if (attach) CliNote("selftest 完成（网络 2 项已跳过）");
        return;
    }
    HttpResp g = HttpCall(base + "/" + (g_config.updateChannel.empty() ? "latest" : g_config.updateChannel) + ".json", "", false);
    chk("http-get", g.err.empty() && g.status == 200 && !g.body.empty());
    // POST 自校验：本地按服务端规则重算 HMAC 比对，验证签名与请求格式
    std::string body = "{\"type\":\"问题反馈\",\"version\":\"" + std::string(kAppVersion) +
                       "\",\"text\":\"selftest\",\"extra\":\"\",\"with_log\":false,"
                       "\"machine\":\"t\",\"user\":\"t\",\"os\":\"t\",\"monitors\":\"\",\"log\":\"\"}";
    std::string signedBody = SignFeedback(body);
    chk("hmac-wrap", signedBody.find(",\"hash\":\"") != std::string::npos);
    HttpResp pst = HttpCall(base + "/feedback.php", signedBody, true);
    chk("http-post", pst.err.empty() && pst.status == 200);
    if (attach) {
        CliNote(("更新地址: " + base).c_str());
        CliNote(("GET  status=" + std::to_string(g.status) + " " + g.err).c_str());
        CliNote(("POST status=" + std::to_string(pst.status) + " " + pst.err + " " + pst.body).c_str());
    }
}
static void CliFeedback() {
    bool attach = CliAttachConsole();
    // /feedback "正文" [类型]
    int idx = CliIndexOf(L"/feedback");
    std::string text = ArgAfter(L"/feedback");
    if (text.empty()) {
        if (attach) CliNote("用法: MonitorOSD.exe /feedback \"问题描述\" [0=问题 1=建议 2=其他] [--log]");
        return;
    }
    int type = 0;
    if (idx + 2 < (int)g_cliArgs.size()) {
        std::string t = WideToUtf8(g_cliArgs[idx + 2]);
        if (t.size() == 1 && t[0] >= '0' && t[0] <= '2') type = t[0] - '0';
    }
    bool withLog = CliHas(L"--log");
    g_fbText = text;
    std::string body = "{";
    body += "\"type\":\"" + JsonEsc(kFbTypes[type]) + "\",";
    body += "\"version\":\"" + std::string(kAppVersion) + "\",";
    body += "\"text\":\"" + JsonEsc(text) + "\",\"extra\":\"\",";
    body += "\"with_log\":" + std::string(withLog ? "true" : "false") + ",";
    body += "\"machine\":\"" + JsonEsc(Sha256Hex(MachineGuid()).substr(0, 16)) + "\",";
    body += "\"user\":\"" + JsonEsc(UserName()) + "\",";
    body += "\"os\":\"" + JsonEsc(std::to_string(LOBYTE(LOWORD(GetVersion()))) + "." +
                                  std::to_string(HIBYTE(LOWORD(GetVersion())))) + "\",";
    body += "\"monitors\":\"" + JsonEsc(MonitorSnapshot()) + "\",";
    body += "\"log\":\"" + JsonEsc(withLog ? TailLog(120) : "") + "\"";
    body += "}";
    body = SignFeedback(body);
    HttpResp r = HttpCall(UpdateBase() + "/feedback.php", body, true);
    std::string msg = r.err.empty() ? ("提交结果 HTTP " + std::to_string(r.status) + " " + r.body.substr(0, 200))
                                    : ("提交失败: " + r.err);
    LogLine("feedback: " + msg);
    if (attach) CliNote(msg.c_str());
}
// /set 0x10 80 [/verbose]：向所有显示器写入指定 VCP 参数（配合开机脚本统一初始化）
static void CliSet() {
    int idx = CliIndexOf(L"/set");
    int code = -1; unsigned val = 0;
    bool ok = idx >= 0 && idx + 2 < (int)g_cliArgs.size();
    if (ok) {
        std::wstring ws = g_cliArgs[idx + 1];
        std::string cs = WideToUtf8(ws);
        char* endp = nullptr;
        long parsed;
        if (cs.size() > 2 && cs[0] == '0' && (cs[1] == 'x' || cs[1] == 'X')) {
            parsed = strtol(cs.c_str() + 2, &endp, 16);          // 十六进制
        } else {
            parsed = strtol(cs.c_str(), &endp, 10);              // 裸数字按十进制（与 vcpmap.json 约定一致）
        }
        // v1.5.3：校验必须严格。原实现用 atoi —— "abc" 会静默变成 0（写出 0x00），
        // "-5" 经 wcstoul 变成巨大无符号值；都被 code<0||>255 拦住了，但报错原因误导。
        if (!endp || endp == cs.c_str() || *endp != '\0' || parsed < 0 || parsed > 255) ok = false;
        else code = (int)parsed;
        val = (unsigned)wcstoul(g_cliArgs[idx + 2].c_str(), nullptr, 10);
        if (val > 0xFFFF) ok = false;
    }
    if (!ok) {
        LogLine("/set 失败: 参数无效（用法: MonitorOSD.exe /set 0x10 80）");
        if (CliHas(L"/verbose")) CliMessageBox("用法: MonitorOSD.exe /set 0x10 80", "MonitorOSD");
        return;
    }
    // P1-2 修复：CLI /set 同样接入动作码黑名单（/set 0x04 1 一条命令可整店恢复出厂）
    if (code == 0x04 || code == 0x08 || code == 0x0C) {
        LogLine("/set: 拒绝动作码 0x" + std::to_string(code) + "（动作码黑名单）");
        // v1.5.3：弹框必须受 /verbose 控制。原实现无条件弹→计划任务里参数写错会弹一个
        // 没人点的模态框，后台任务永久挂住（其余 CLI 提示都在 /verbose 里，这里保持一致）
        if (CliHas(L"/verbose")) {
            char hx[8]; sprintf_s(hx, "%02X", code);
            CliMessageBox(std::string("拒绝执行：0x") + hx +
                          " 是动作类码（恢复出厂/保存设置），已被黑名单拦截", "MonitorOSD");
        }
        return;
    }
    auto mons = Enumerate();
    int okc = 0;
    for (auto* m : mons) if (m->WriteNow(code, val)) okc++;
    for (auto* m : mons) { m->Retire(); }   // v1.5.8 P1-4：延迟回收
    char buf[128];
    sprintf_s(buf, "/set 0x%02X=%u 成功 %d/%zu 台", code, val, okc, mons.size());
    LogLine(buf);
    if (CliHas(L"/verbose")) {
        sprintf_s(buf, "写入成功 %d/%zu 台", okc, mons.size());
        CliMessageBox(buf, "MonitorOSD");
    }
}

// /restore [/verbose]：还原所有显示器的基准后退出（可挂开机计划任务）
static void CliRestore() {
    int n = RestoreAllInternal();
    char buf[96];
    sprintf_s(buf, "/restore 完成，还原 %d 项", n);
    LogLine(buf);
    if (CliHas(L"/verbose"))
        CliMessageBox(n >= 0 ? ("已还原基准，共 " + std::to_string(n) + " 项")
                             : std::string("没有找到显示器或基准档案"), "MonitorOSD");
}

// /scan 全量扫描 VCP 码 → scan_report.txt；/detect 列菜单支持情况 → detect.txt（对应 C# RunCli）
static void CliScanReport(bool fullScan) {
    bool attached = CliAttachConsole();
    std::string file = std::string(ExeDir()) + (fullScan ? "\\scan_report.txt" : "\\detect.txt");
    std::string sb;
    auto flush = [&]() { WriteTextFile(file, sb); };
    sb += std::string("==== MonitorOSD ") + (fullScan ? "SCAN" : "DETECT") + " ====\n";
    {
        char tb[64];
        SYSTEMTIME st; GetLocalTime(&st);
        sprintf_s(tb, "%04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay, st.wHour, st.wMinute, st.wSecond);
        sb += std::string("时间: ") + tb + "\n";
    }
    try {
        sb += "枚举显示器中…\n";
        flush();
        auto mons = Enumerate();
        sb += "显示器数量: " + std::to_string(mons.size()) + "\n\n";
        sb += "EDID 记录（注册表）:\n";
        for (auto& e : LoadAllEdids())
            sb += "  " + e.vendor + " " + e.model + " S/N " + e.serial + "\n";
        flush();
        int idx = 0;
        for (auto* m : mons) {
            char line[512];
            sprintf_s(line, "\n==== [%d] %s  品牌=%s  S/N=%s  DDC=%s  caps=%s\n",
                      idx, DisplayNameOf(m).c_str(), BrandKeyOf(m->brand),
                      m->serial.empty() ? "-" : m->serial.c_str(),
                      m->ddcOk ? "OK" : "不可用", m->capsOk ? "OK" : "拒答");
            sb += line;
            sb += "caps: " + (m->capModel.empty() ? std::string("(无)") : m->capModel) + "\n";
            flush();
            if (!fullScan) {
                auto menu = BuildMenu(m->brand);
                for (auto& G : menu) {
                    sb += std::string("[") + G.en + "]\n";
                    for (auto& it : G.items) {
                        std::string pad = "  ";
                        if (it.action) {
                            sb += pad + it.en + " (" + it.cn + ")  (功能项)\n";
                            continue;
                        }
                        const ValSpec* s = nullptr;
                        auto sp = SpecOf(m->brand);
                        auto f = sp->find(it.mapId ? it.mapId : "");
                        if (f != sp->end()) s = &f->second;
                        if (!s || s->code == 0) {
                            sb += pad + it.en + " (" + it.cn + ")  -- DDC不支持\n";
                            continue;
                        }
                        bool supported;
                        unsigned v = 0, mx = 0;
                        { std::lock_guard<std::recursive_mutex> il(m->infoMx);
                          supported = !m->capsOk || m->supported.count(s->code) || m->cache.count(s->code);
                          auto cv = m->cache.find(s->code);
                          if (cv != m->cache.end()) v = cv->second; }
                        if (supported) {
                            if (v == 0 && !m->Read(s->code, v, mx)) v = 0;
                            char b2[128];
                            sprintf_s(b2, "%s%s (%s)  VCP=0x%02X  当前=%u\n", pad.c_str(), it.en, it.cn, s->code, v);
                            sb += b2;
                        } else {
                            char b2[160];
                            sprintf_s(b2, "%s%s (%s)  VCP=0x%02X  -- DDC不支持\n", pad.c_str(), it.en, it.cn, s->code);
                            sb += b2;
                        }
                    }
                }
                flush();
            } else {
                sb += "可读的 VCP 码:\n";
                for (int c = 1; c <= 255; c++) {
                    unsigned cur = 0, mx = 0;
                    if (m->Read(c, cur, mx)) {
                        char b2[64];
                        sprintf_s(b2, "0x%02X   cur=%u   max=%u\n", c, cur, mx);
                        sb += b2;
                    }
                    if (c % 32 == 0) flush();
                }
            }
            m->Retire();   // v1.5.8 P1-4：延迟回收
            idx++;
        }
    } catch (...) {
        sb += "异常: 扫描过程中断\n";
    }
    flush();
    std::string msg = "报告已保存:\r\n" + file;
    if (attached) { CliNote(msg.c_str()); }
    else CliMessageBox(msg, "MonitorOSD");
}

// ================= 网管工具窗口（ToolsForm.cs：VCP 扫描器 + 差分校准向导）=================
// 复用主窗口的 D2D/DWrite 工厂、字体格式缓存与 Theme；独立 HWND + 独立 RenderTarget。
#define WM_TSCAN_BATCH (WM_APP + 13)   // lParam = new std::vector<std::string>（扫描行批次）
#define WM_TSCAN_PROG  (WM_APP + 11)   // wParam=已扫码数 lParam=已读到码数
#define WM_TSCAN_DONE  (WM_APP + 12)   // lParam = new std::map<int,unsigned>（全码值）

static HWND g_toolsHwnd = nullptr;
static ID2D1HwndRenderTarget* g_toolsRt = nullptr;
static int  g_toolsTab = 0;                 // 0=VCP 扫描器 1=差分校准向导
static bool g_toolsOwnMons = false;         // true = /tools 模式自持显示器（退出时清理+退消息循环）
static MonitorDev* g_toolsMon = nullptr;
static int  g_toolsW = 0, g_toolsH = 0;

// 扫描器
static std::vector<std::string> g_scanLines;
static int  g_scanScroll = 0;
static bool g_scanning = false;

// 校准向导
static std::map<int, unsigned> g_calBase, g_calSecond, g_calMax;
static std::vector<int>         g_calChanged;
static std::vector<std::string> g_calLines;
static std::vector<int>         g_calLineIdx;   // 每行对应的变化码索引（-1 = 非结果行）
static int  g_calScroll = 0;
static int  g_calSel = -1;
static int  g_calPhase = 1;                     // 1=建基准 2=对比
static std::string g_calStep = "点【建立基准】→ 去 OSD 改一项 → 回来点【扫描对比】→ 指认变化的码。";
struct Bindable { std::string cn, en, id; };
static std::vector<Bindable> g_tBindable;
static int  g_tComboSel = 0;

// 布局热区
static RECT g_tTabScan, g_tTabCal, g_tClose;
static RECT g_tText, g_tBtnScan, g_tBtnSave;          // 扫描器
static RECT g_tStep, g_tList, g_tCombo, g_tBtnBind, g_tBtnCmp, g_tBtnWake, g_tBtnBase;   // 校准

static IDWriteTextFormat* FWrap(float px, bool bold = false) {
    static std::map<std::string, IDWriteTextFormat*> cache;
    char key[80]; sprintf_s(key, "wrap|%.2f|%d", px, (int)bold);
    auto it = cache.find(key);
    if (it != cache.end()) return it->second;
    IDWriteTextFormat* f = MakeFmt(g_fontUiOk ? L"NotoSC" : L"Microsoft YaHei UI", px, bold,
                                   DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_NEAR);
    if (f) f->SetWordWrapping(DWRITE_WORD_WRAPPING_WRAP);
    cache[key] = f;
    return f;
}
static void LayoutTools() {
    g_toolsW = 0; g_toolsH = 0;
    RECT rc; GetClientRect(g_toolsHwnd, &rc);
    g_toolsW = rc.right; g_toolsH = rc.bottom;
    int W = g_toolsW, H = g_toolsH;
    g_tClose = { W - SI(46), 0, W, SI(36) };
    // 页签
    int tw1 = (int)MeasureW("VCP 扫描器", FUi(SI(12)));
    int tw2 = (int)MeasureW("差分校准向导", FUi(SI(12)));
    int tx = SI(16);
    g_tTabScan = { tx, SI(40), tx + tw1 + SI(16), SI(74) };
    g_tTabCal  = { g_tTabScan.right + SI(40), SI(40), g_tTabScan.right + SI(40) + tw2 + SI(16), SI(74) };
    // 扫描器：文本区 + 底部两按钮
    g_tText = { SI(16), SI(84), W - SI(16) - SI(14), H - SI(46) - SI(46) };
    g_tBtnScan = { SI(16), H - SI(46) - SI(38), SI(16) + SI(104), H - SI(46) - SI(8) };
    g_tBtnSave = { g_tBtnScan.right + SI(8), H - SI(46) - SI(38), g_tBtnScan.right + SI(8) + SI(104), H - SI(46) - SI(8) };
    // 校准：步骤 / 列表 / 指认行 / 按钮行
    g_tStep  = { SI(16), SI(84), W - SI(32), SI(84) + SI(52) };
    g_tList  = { SI(16), SI(142), W - SI(16) - SI(14), SI(142) + SI(176) };
    int cy = SI(330);
    g_tCombo   = { SI(100), cy, SI(100) + SI(280), cy + SI(28) };
    g_tBtnBind = { SI(396), cy - SI(2), SI(396) + SI(170), cy + SI(30) };
    int by = cy + SI(44);
    g_tBtnCmp  = { SI(16), by, SI(16) + SI(200), by + SI(36) };
    g_tBtnWake = { g_tBtnCmp.right + SI(20), by, g_tBtnCmp.right + SI(20) + SI(200), by + SI(36) };
    g_tBtnBase = { g_tBtnWake.right + SI(20), by, g_tBtnWake.right + SI(20) + SI(190), by + SI(36) };
}
static void DrawToolsTextLines(const std::vector<std::string>& lines, const RECT& area, int& scroll,
                               const std::vector<int>& lineIdx, int sel) {
    float lineH = (float)SI(16);
    int visible = (std::max)(1, (int)((area.bottom - area.top) / lineH));
    int maxScroll = (std::max)(0, (int)lines.size() - visible);
    scroll = ClampI(scroll, 0, maxScroll);
    g_rt->PushAxisAlignedClip(RectF(area), D2D1_ANTIALIAS_MODE_PER_PRIMITIVE);
    for (int i = 0; i < visible && i + scroll < (int)lines.size(); i++) {
        int li = i + scroll;
        float y = (float)(area.top + (int)(i * lineH));
        if (!lineIdx.empty() && li < (int)lineIdx.size() && lineIdx[li] == sel && sel >= 0) {
            FillRRect({ area.left, (int)y, area.right, (int)(y + lineH) }, 0.f,
                      Col(Theme::Accent.r, Theme::Accent.g, Theme::Accent.b, 0.14f));
        }
        DrawT(lines[li], FMono(SI(11)),
              D2D1::RectF((float)(area.left + SI(8)), y, (float)(area.right - SI(20)), y + lineH),
              Theme::TextSecondary);
    }
    g_rt->PopAxisAlignedClip();
    if (maxScroll > 0) {   // 自绘滚动条
        RECT track = { area.right - SI(10), area.top, area.right - SI(4), area.bottom };
        FillRRect(track, 3.f, Theme::LineStrong);
        int span = track.bottom - track.top;
        int thumbH = (std::max)(SI(24), (int)((float)visible / (float)lines.size() * span));
        int ty = track.top + (int)((float)scroll / (float)maxScroll * (span - thumbH));
        FillRRect({ track.left, ty, track.right, ty + thumbH }, 3.f, Theme::TextMuted);
    }
}
static void DrawToolsBtn(const RECT& r, const char* label, bool enabled, bool primary = false) {
    bool hov = enabled && Hover(r);
    bool prs = enabled && Press(r);
    Col bg = !enabled ? Theme::SurfaceSub
           : primary ? (prs ? Theme::AccentHover.Dark(0.06f) : hov ? Theme::AccentHover : Theme::Accent)
           : prs ? Theme::SurfaceHover.Dark(0.06f) : hov ? Theme::SurfaceHover : Theme::SurfaceSub;
    FillRRect(r, 8.f, bg);
    DrawT(label, FUi(SI(12), true, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
          RectF(r), !enabled ? Theme::TextMuted : primary ? Theme::OnAccent : Theme::TextPrimary);
}
static void PaintToolsFrame() {
    if (!g_toolsRt) return;
    // 绘制辅助函数都作用于 g_rt：临时切换，画完还原（单线程 UI，安全）
    ID2D1HwndRenderTarget* saved = g_rt;
    g_rt = g_toolsRt;
    g_toolsRt->BeginDraw();
    g_toolsRt->Clear(Theme::BgWindow.F());
    PaintAmbient(g_toolsRt, (float)g_toolsW, (float)g_toolsH);
    // 标题栏
    {
        auto b = Brush(Theme::LineSubtle);
        g_toolsRt->DrawLine(D2D1::Point2F(0, (float)SI(36)), D2D1::Point2F((float)g_toolsW, (float)SI(36)), b, 1.f);
        b->Release();
        std::string title = std::string("网管工具 - ") + (g_toolsMon ? DisplayNameOf(g_toolsMon) : "");
        DrawT(title, FUi(SI(12), true), D2D1::RectF(20.f, 10.f, 20.f + 800.f, 10.f + 200.f), Theme::TextPrimary);
        DrawT("✕", FUi(SI(12), false, DWRITE_TEXT_ALIGNMENT_CENTER, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
              RectF(g_tClose), Hover(g_tClose) ? Theme::Danger : Theme::TextMuted);
    }
    // 页签
    {
        auto drawTab = [&](const RECT& r, const char* label, bool on) {
            DrawT(label, FUi(SI(12), on, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
                  RectF(r), on ? Theme::TextPrimary : Theme::TextTertiary);
        };
        drawTab(g_tTabScan, "VCP 扫描器", g_toolsTab == 0);
        drawTab(g_tTabCal,  "差分校准向导", g_toolsTab == 1);
        const RECT& bar = g_toolsTab == 0 ? g_tTabScan : g_tTabCal;
        RECT br = { bar.left - SI(8), SI(74), bar.right, SI(74) + SI(3) };
        FillRRect(br, 2.f, Theme::Accent);
    }
    if (g_toolsTab == 0) {
        // 扫描器
        FillRRect({ g_tText.left - SI(8), g_tText.top - SI(8), g_tText.right + SI(8), g_tText.bottom + SI(8) },
                  12.f, Theme::SurfaceGroup);
        std::vector<int> empty;
        DrawToolsTextLines(g_scanLines, g_tText, g_scanScroll, empty, -1);
        DrawToolsBtn(g_tBtnScan, g_scanning ? "扫描中…" : "开始扫描", !g_scanning);
        DrawToolsBtn(g_tBtnSave, "保存报告", !g_scanLines.empty() && !g_scanning);
    } else {
        // 差分校准向导
        DrawT(g_calStep, FWrap(SI(11), true), RectF(g_tStep), Theme::TextSecondary);
        FillRRect({ g_tList.left - SI(8), g_tList.top - SI(8), g_tList.right + SI(8), g_tList.bottom + SI(8) },
                  12.f, Theme::SurfaceGroup);
        DrawToolsTextLines(g_calLines, g_tList, g_calScroll, g_calLineIdx, g_calSel);
        // 指认行
        DrawT("这个码是：", FUi(SI(11)), RectF({ SI(16), g_tCombo.top, SI(96), g_tCombo.bottom }), Theme::TextSecondary);
        {
            bool hov = Hover(g_tCombo);
            FillRRect(g_tCombo, 6.f, hov ? Theme::SurfaceHover : Theme::SurfaceSub);
            std::string sel = "（选择功能名）";
            if (g_tComboSel >= 0 && g_tComboSel < (int)g_tBindable.size())
                sel = g_tBindable[g_tComboSel].cn + " [" + g_tBindable[g_tComboSel].en + "]";
            else if (g_tComboSel == (int)g_tBindable.size())
                sel = "其他/未知功能（先记下来）";
            int tw = (int)MeasureW(sel, FUi(SI(11)));
            if (tw > (g_tCombo.right - g_tCombo.left) - SI(30)) sel = sel.substr(0, 12) + "…";
            DrawT(sel, FUi(SI(11), false, DWRITE_TEXT_ALIGNMENT_LEADING, DWRITE_PARAGRAPH_ALIGNMENT_CENTER),
                  RectF({ g_tCombo.left + SI(8), g_tCombo.top, g_tCombo.right - SI(26), g_tCombo.bottom }),
                  g_tComboSel >= 0 ? Theme::TextSecondary : Theme::TextMuted);
            int cx = g_tCombo.right - SI(14), cy = (g_tCombo.top + g_tCombo.bottom) / 2;
            auto b = Brush(Theme::TextMuted);
            g_toolsRt->DrawLine(D2D1::Point2F((float)cx - 3, (float)cy - 1), D2D1::Point2F((float)cx, (float)cy + 2), b, 1.2f);
            g_toolsRt->DrawLine(D2D1::Point2F((float)cx, (float)cy + 2), D2D1::Point2F((float)cx + 3, (float)cy - 1), b, 1.2f);
            b->Release();
        }
        DrawToolsBtn(g_tBtnBind, "保存绑定到 vcpmap.json", !g_calChanged.empty());
        DrawToolsBtn(g_tBtnCmp, g_calPhase == 1 ? "① 建立基准" : "② 扫描对比（与上次）", !g_scanning);
        DrawToolsBtn(g_tBtnWake, "唤醒显示器（节能假死）", !g_scanning);
        DrawToolsBtn(g_tBtnBase, "存为基准快照", !g_calBase.empty() && !g_scanning);
        DrawT("流程：改一项 → 点一次。每次自动与上一次扫描结果对比，无限循环，无需重新快照。",
              FUi(SI(10)), RectF({ SI(16), g_tBtnWake.bottom + SI(8), g_toolsW - SI(32), g_tBtnWake.bottom + SI(26) }),
              Theme::TextMuted);
    }
    // 底部信息栏
    {
        int y = g_toolsH - SI(46);
        auto b = Brush(Theme::LineSubtle);
        g_toolsRt->DrawLine(D2D1::Point2F(0, (float)y), D2D1::Point2F((float)g_toolsW, (float)y), b, 1.f);
        b->Release();
        auto* sb = Brush(Theme::BgStatusBar);
        g_toolsRt->FillRectangle(D2D1::RectF(0, (float)(y + 1), (float)g_toolsW, (float)g_toolsH), sb);
        sb->Release();
        std::string info = "型号: " + (g_toolsMon ? DisplayNameOf(g_toolsMon) : "-") +
                           "  品牌: " + (g_toolsMon ? BrandKeyOf(g_toolsMon->brand) : "-") +
                           "  S/N: " + (g_toolsMon && !g_toolsMon->serial.empty() ? g_toolsMon->serial : "-");
        DrawT(info, FUi(SI(10)), D2D1::RectF(16.f, (float)(y + 14), 16.f + 900.f, (float)(y + 34)), Theme::TextTertiary);
    }
    if (FAILED(g_toolsRt->EndDraw())) { g_toolsRt->Release(); g_toolsRt = nullptr; }
    g_rt = saved;
}

// ---- 扫描worker（1~255 全码读一次；批次回显 + 进度 + 完成）----
static void ToolsScanWorker(bool showCodes) {
    MonitorDev* m = g_toolsMon;
    if (!m) return;
    auto* vals = new std::map<int, unsigned>();
    auto* batch = new std::vector<std::string>();
    int found = 0;
    for (int c = 1; c <= 255; c++) {
        unsigned cur = 0, mx = 0;
        if (m->Read(c, cur, mx)) {
            (*vals)[c] = cur;
            g_calMax[c] = mx;
            found++;
            if (showCodes) {
                char b[64];
                sprintf_s(b, "0x%02X   当前=%u   上限=%u", c, cur, mx);
                batch->push_back(b);
            }
        }
        Sleep(6);   // 节流：让进度肉眼可见，也避免高频 DDC 查询触发固件限流
        if (c % 16 == 0) {
            PostMessageW(g_toolsHwnd, WM_TSCAN_BATCH, 0, (LPARAM)batch);
            batch = new std::vector<std::string>();
            PostMessageW(g_toolsHwnd, WM_TSCAN_PROG, (WPARAM)c, (LPARAM)found);
        }
    }
    PostMessageW(g_toolsHwnd, WM_TSCAN_BATCH, 0, (LPARAM)batch);
    PostMessageW(g_toolsHwnd, WM_TSCAN_DONE, 0, (LPARAM)vals);
}
static void ToolsStartScan(bool showCodes) {
    if (g_scanning || !g_toolsMon) return;
    g_scanning = true;
    std::thread(ToolsScanWorker, showCodes).detach();
}
static void ToolsShowDiff() {
    g_calChanged.clear();
    g_calLines.push_back("---- 对比结果 ----");
    g_calLineIdx.push_back(-1);
    char b[160];
    for (auto& kv : g_calSecond) {
        auto old = g_calBase.find(kv.first);
        if (old == g_calBase.end()) {
            g_calChanged.push_back(kv.first);
            g_calLineIdx.push_back((int)g_calChanged.size() - 1);
            sprintf_s(b, "0x%02X   （基准未读到）→ %u", kv.first, kv.second);
            g_calLines.push_back(b);
        } else if (old->second != kv.second) {
            g_calChanged.push_back(kv.first);
            g_calLineIdx.push_back((int)g_calChanged.size() - 1);
            unsigned mx = 0;
            auto mi = g_calMax.find(kv.first);
            if (mi != g_calMax.end()) mx = mi->second;
            sprintf_s(b, "0x%02X   %u → %u   (上限 %u)", kv.first, old->second, kv.second, mx);
            g_calLines.push_back(b);
        }
    }
    for (auto& kv : g_calBase)
        if (!g_calSecond.count(kv.first)) {
            g_calChanged.push_back(kv.first);
            g_calLineIdx.push_back((int)g_calChanged.size() - 1);
            sprintf_s(b, "0x%02X   %u → （本次未读到）", kv.first, kv.second);
            g_calLines.push_back(b);
        }
    LogLine("校准向导: 对比扫描 " + std::to_string(g_calSecond.size()) +
            " 码，变化 " + std::to_string(g_calChanged.size()) + " 个");
    g_calBase = g_calSecond;   // 滚动基准：本次结果即下一轮基准
    if (g_calChanged.empty()) {
        g_calLines.push_back("（无变化）");
        g_calLineIdx.push_back(-1);
        g_calStep = "没有码变化（基准已更新）。可能该项不走 DDC，或刚才没改成功。继续改下一项再对比。";
        return;
    }
    g_calSel = 0;
    g_tBindable.clear();
    if (g_toolsMon)
        for (auto& G : BuildMenu(g_toolsMon->brand))
            for (auto& it : G.items) {
                if (it.action || !it.mapId) continue;
                g_tBindable.push_back({ it.cn, it.en, it.mapId });
            }
    g_tComboSel = 0;
    g_calStep = "检测到 " + std::to_string(g_calChanged.size()) +
                " 个码变化。选中正确的码 → 指认功能名 → 点保存。然后继续改下一项，再点【扫描对比】。";
}
// 写 vcpmap.json（读-改-写，保留既有条目与顺序；对齐 C# ToolsForm.WriteOverride）
static void WriteVcpMapOverride(const std::string& brandKey, const std::string& id, int code, unsigned max) {
    std::string path = ExeDir() + "\\vcpmap.json";
    std::string text;
    JVal root;
    if (!(ReadTextFile(path, text) && JsonParse(text, root))) root = JVal();
    JVal* brand = nullptr;
    for (auto& kv : root.obj)
        if (kv.first == brandKey) brand = &kv.second;
    if (!brand) {
        root.obj.push_back({ brandKey, JVal() });
        brand = &root.obj.back().second;
        brand->t = JVal::OBJ;
    }
    JVal entry;
    char cb[16];
    sprintf_s(cb, "0x%02X", code);
    entry.set("code", JVal::S(cb));
    if (max > 0 && max <= 255) {
        entry.set("uimin", JVal::N(0));
        entry.set("uimax", JVal::N((double)max));
    }
    brand->set(id.c_str(), std::move(entry));
    std::string out;
    JsonDump(root, out, 0);
    WriteTextFile(path, out);
    LogLine("校准: 已写入 " + path + " [" + brandKey + "." + id + "]");
}
static void ToolsSaveBinding() {
    if (g_calChanged.empty() || !g_toolsMon) return;
    int code = (g_calSel >= 0 && g_calSel < (int)g_calChanged.size()) ? g_calChanged[g_calSel] : g_calChanged[0];
    std::string id;
    if (g_tComboSel == (int)g_tBindable.size()) {
        char b[32];
        sprintf_s(b, "custom0x%02X", code);
        id = b;
        LogLine(std::string("校准：未知功能 0x") + b + " 记为 " + id);
    } else if (g_tComboSel >= 0 && g_tComboSel < (int)g_tBindable.size()) {
        id = g_tBindable[g_tComboSel].id;
    } else return;
    unsigned mx = 0;
    auto mi = g_calMax.find(code);
    if (mi != g_calMax.end()) mx = mi->second;
    WriteVcpMapOverride(BrandKeyOf(g_toolsMon->brand), id, code, mx);
    char hx[8];
    sprintf_s(hx, "%02X", code);
    LogLine("校准绑定: " + std::string(BrandKeyOf(g_toolsMon->brand)) + " " + id +
            " -> 0x" + hx + " max=" + std::to_string(mx));
    char msg[256];
    sprintf_s(msg, "已绑定: %s -> 0x%02X\r\n已写入 vcpmap.json，重启软件后生效。\r\n\r\n基准已更新：现在可直接去 OSD 改下一项，\r\n回来点【扫描对比】继续校准。",
              id.c_str(), code);
    MessageBoxW(g_toolsHwnd, Utf8ToWide(msg).c_str(), L"校准向导", MB_OK | MB_ICONINFORMATION);
}
static void ToolsSaveReport() {
    if (g_scanLines.empty() || !g_toolsMon) return;
    // 文件名安全化（替换 Windows 非法字符）
    std::string safe = DisplayNameOf(g_toolsMon);
    for (auto& c : safe)
        if (strchr("\\/:*?\"<>|", c)) c = '_';
    std::string file = ExeDir() + "\\scan_" + safe + ".txt";
    std::string body;
    for (auto& l : g_scanLines) body += l + "\r\n";
    if (WriteTextFile(file, body)) {
        std::string msg = "已保存:\r\n" + file;
        MessageBoxW(g_toolsHwnd, Utf8ToWide(msg).c_str(), L"扫描报告", MB_OK | MB_ICONINFORMATION);
    } else {
        MessageBoxW(g_toolsHwnd, L"保存失败（详见 log.txt）", L"扫描报告", MB_OK | MB_ICONWARNING);
    }
}
static void ToolsClick(POINT pt) {
    if (PtIn(g_tClose, pt)) { DestroyWindow(g_toolsHwnd); return; }
    if (PtIn(g_tTabScan, pt)) { g_toolsTab = 0; InvalidateRect(g_toolsHwnd, nullptr, FALSE); return; }
    if (PtIn(g_tTabCal, pt))  { g_toolsTab = 1; InvalidateRect(g_toolsHwnd, nullptr, FALSE); return; }
    if (g_toolsTab == 0) {
        if (PtIn(g_tBtnScan, pt)) {
            if (g_scanning || !g_toolsMon) return;
            g_scanLines.clear();
            g_scanScroll = 0;
            g_scanLines.push_back("==== MonitorOSD VCP 扫描报告 ====");
            {
                char hdr[64];
                SYSTEMTIME st; GetLocalTime(&st);
                sprintf_s(hdr, "时间: %04d-%02d-%02d %02d:%02d:%02d", st.wYear, st.wMonth, st.wDay,
                          st.wHour, st.wMinute, st.wSecond);
                g_scanLines.push_back(hdr);
            }
            {
                char hdr[512];
                sprintf_s(hdr, "型号: %s   品牌: %s   S/N: %s", DisplayNameOf(g_toolsMon).c_str(),
                          BrandKeyOf(g_toolsMon->brand),
                          g_toolsMon->serial.empty() ? "-" : g_toolsMon->serial.c_str());
                g_scanLines.push_back(hdr);
            }
            g_scanLines.push_back("caps: " + (g_toolsMon->capModel.empty() ? std::string("(无)") : g_toolsMon->capModel));
            g_scanLines.push_back("可读的 VCP 码（code / 当前值 / 上限）:");
            ToolsStartScan(true);
            return;
        }
        if (PtIn(g_tBtnSave, pt)) { ToolsSaveReport(); return; }
        return;
    }
    // 差分校准向导
    if (PtIn(g_tBtnCmp, pt)) {
        if (g_scanning || !g_toolsMon) return;
        if (g_calPhase == 1) {
            g_calLines.clear();
            g_calLineIdx.clear();
            g_calScroll = 0;
            g_calSel = -1;
            g_calStep = "基准扫描中… 0/255（请勿动显示器）";
            ToolsStartScan(true);
        } else {
            g_calLines.push_back("---- 对比扫描中… ----");
            g_calLineIdx.push_back(-1);
            g_calStep = "扫描中… 0/255（请勿动显示器）";
            ToolsStartScan(false);
        }
        return;
    }
    if (PtIn(g_tBtnWake, pt)) {
        if (!g_toolsMon) return;
        MonitorDev* m = g_toolsMon;
        g_calStep = "唤醒流程已执行。若仍无响应，手动按一下显示器电源键。";
        // v1.5.8 P1-4：线程持引用，唤醒最长要跑十几秒，期间窗口关闭/重探都不能把对象销毁
        m->AddRef();
        std::thread([m]() { WakeUp(m); m->Release(); }).detach();
        InvalidateRect(g_toolsHwnd, nullptr, FALSE);
        return;
    }
    if (PtIn(g_tBtnBase, pt)) {
        // v1.5.3：把校准向导的全码扫描快照（66 码级别）存为系列基准——最完整的「恢复默认」快照源。
        // 剔除动作/状态码：回写它们适得其反（0x04 恢复出厂 / 0x08 保存设置 / 0x02·0x03·0x05 只读状态）。
        if (g_calBase.empty() || !g_toolsMon) return;
        Baseline bl;
        for (auto& kv : g_calBase) {
            int c = kv.first;
            if (c == 0x02 || c == 0x03 || c == 0x04 || c == 0x05 || c == 0x08 || c == 0x0C) continue;
            bl.vals[c] = kv.second;
        }
        if (bl.vals.empty()) { g_calStep = "快照里无可保存的值。"; InvalidateRect(g_toolsHwnd, nullptr, FALSE); return; }
        g_profiles[MonitorKey(g_toolsMon)] = bl;
        SaveProfiles();
        g_calStep = "已存为基准快照（" + std::to_string(bl.vals.size()) + " 项）——可用「一键还原」恢复到该状态。";
        InvalidateRect(g_toolsHwnd, nullptr, FALSE);
        return;
    }
    if (PtIn(g_tCombo, pt)) {
        HMENU m = CreatePopupMenu();
        for (size_t i = 0; i < g_tBindable.size(); i++) {
            std::wstring w = Utf8ToWide(g_tBindable[i].cn + " [" + g_tBindable[i].en + "]");
            AppendMenuW(m, MF_STRING, 3001 + (UINT)i, w.c_str());
        }
        AppendMenuW(m, MF_STRING, 3001 + (UINT)g_tBindable.size(), L"其他/未知功能（先记下来）");
        if (g_tComboSel >= 0)
            CheckMenuRadioItem(m, 3001, 3001 + (UINT)g_tBindable.size(), 3001 + g_tComboSel, MF_BYCOMMAND);
        POINT sp = { g_tCombo.left, g_tCombo.bottom };
        ClientToScreen(g_toolsHwnd, &sp);
        TrackPopupMenu(m, TPM_LEFTALIGN | TPM_LEFTBUTTON, sp.x, sp.y, 0, g_toolsHwnd, nullptr);
        DestroyMenu(m);
        return;
    }
    if (PtIn(g_tBtnBind, pt)) { ToolsSaveBinding(); return; }
    if (PtIn(g_tList, pt)) {
        int row = (int)((pt.y - g_tList.top) / SI(16)) + g_calScroll;
        if (row >= 0 && row < (int)g_calLineIdx.size() && g_calLineIdx[row] >= 0) {
            g_calSel = g_calLineIdx[row];
            InvalidateRect(g_toolsHwnd, nullptr, FALSE);
        }
        return;
    }
}
static LRESULT CALLBACK ToolsWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_toolsHwnd = h;
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2f))) return -1;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_dwf))) return -1;
        LoadFonts();
    BOOTMARK("wWinMain: LoadFonts 完成");
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        RECT rc; GetClientRect(h, &rc);
        g_toolsW = rc.right; g_toolsH = rc.bottom;
        if (FAILED(g_d2f->CreateHwndRenderTarget(
                D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT, D2D1::PixelFormat(), 96.f, 96.f),
                D2D1::HwndRenderTargetProperties(h, D2D1::SizeU(g_toolsW, g_toolsH)), &g_toolsRt)))
            return -1;
        // Win11 圆角；Win10 用 Region 兜底
        int pref = 2 /* DWMWCP_ROUND */;
        if (FAILED(DwmSetWindowAttribute(h, 33, &pref, sizeof(pref)))) {
            HRGN rgn = CreateRoundRectRgn(0, 0, g_toolsW + 1, g_toolsH + 1, SI(16), SI(16));
            SetWindowRgn(h, rgn, TRUE);
        }
        LayoutTools();
        return 0;
    }
    case WM_SIZE: {
        RECT rc; GetClientRect(h, &rc);
        g_toolsW = rc.right; g_toolsH = rc.bottom;
        if (g_toolsRt) {
            if (FAILED(g_toolsRt->Resize(D2D1::SizeU(g_toolsW, g_toolsH))))
                { g_toolsRt->Release(); g_toolsRt = nullptr; }
        }
        LayoutTools();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        PaintToolsFrame();
        EndPaint(h, &ps);
        return 0;
    }
    case WM_NCHITTEST: {
        // WS_POPUP 无标题栏：标题带（0~36px，排除关闭按钮）返回 HTCAPTION 以支持拖动
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        ScreenToClient(h, &pt);
        if (pt.y >= 0 && pt.y < SI(36) && !PtIn(g_tClose, pt)) return HTCAPTION;
        break;
    }
    case WM_MOUSELEAVE:
        g_mouse = { -1, -1 };
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        g_mouse = pt;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        g_mouse = pt;
        g_mouseDown = true;
        g_pressRect = { 0,0,0,0 };
        SetCapture(h);
        ToolsClick(pt);
        return 0;
    }
    case WM_LBUTTONUP:
        if (GetCapture() == h) ReleaseCapture();
        g_mouseDown = false;
        g_pressRect = { 0,0,0,0 };
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_MOUSEWHEEL: {
        int d = GET_WHEEL_DELTA_WPARAM(wp);
        int& sc = g_toolsTab == 0 ? g_scanScroll : g_calScroll;
        sc += d > 0 ? -3 : 3;
        if (sc < 0) sc = 0;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(h); return 0; }
        break;
    case WM_TSCAN_BATCH: {
        auto* lines = (std::vector<std::string>*)lp;
        if (lines) {
            if (g_toolsTab == 0)
                for (auto& l : *lines) g_scanLines.push_back(l);
            else if (g_calPhase == 1)
                for (auto& l : *lines) { g_calLines.push_back(l); g_calLineIdx.push_back(-1); }
            delete lines;
        }
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_TSCAN_PROG:
        if (g_toolsTab == 1 && g_scanning) {
            char b[96];
            sprintf_s(b, "扫描中… %d/255（已读到 %d 个码）", (int)wp, (int)lp);
            g_calStep = b;
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    case WM_TSCAN_DONE: {
        auto* vals = (std::map<int, unsigned>*)lp;
        g_scanning = false;
        if (g_toolsTab == 0) {
            g_scanLines.push_back("---- 扫描完成 ----");
        } else if (vals) {
            if (g_calPhase == 1) {
                g_calBase = *vals;
                g_calPhase = 2;
                g_calLines.push_back("---- 基准已建立：共 " + std::to_string(g_calBase.size()) + " 个码可读 ----");
                g_calLineIdx.push_back(-1);
                g_calStep = "基准已建立（" + std::to_string(g_calBase.size()) +
                            " 个码）。去 OSD 改【一项】设置，改完回来再点【扫描对比】。";
                LogLine("校准向导: 基准快照 " + std::to_string(g_calBase.size()) + " 码");
            } else {
                g_calSecond = *vals;
                ToolsShowDiff();
            }
        }
        delete vals;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_COMMAND:
        if (LOWORD(wp) >= 3001 && LOWORD(wp) <= 3001 + (int)g_tBindable.size()) {
            g_tComboSel = LOWORD(wp) - 3001;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        break;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        g_toolsHwnd = nullptr;
        if (g_toolsRt) { g_toolsRt->Release(); g_toolsRt = nullptr; }
        if (g_toolsOwnMons) {
            // /tools 模式：清理自持显示器并退出消息循环
            for (auto* m : g_mons) { m->Retire(); }   // v1.5.8 P1-4：延迟回收
            g_mons.clear();
            PostQuitMessage(0);
        }
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
static bool s_toolsClassReady = false;
static void EnsureToolsClass() {
    if (s_toolsClassReady) return;
    HINSTANCE inst = GetModuleHandleW(nullptr);
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = ToolsWndProc;
    wc.hInstance = inst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.lpszClassName = L"MonitorOSDTools";
    RegisterClassExW(&wc);
    s_toolsClassReady = true;
}
// 打开网管工具窗口（tab：0=扫描器 1=校准向导）。已开则切页签前置。
static void OpenToolsWindow(int tab) {
    if (g_toolsHwnd) {
        g_toolsTab = tab;
        ShowWindow(g_toolsHwnd, SW_SHOW);
        SetForegroundWindow(g_toolsHwnd);
        InvalidateRect(g_toolsHwnd, nullptr, FALSE);
        return;
    }
    if (g_mons.empty()) { ShowToast("未检测到可控制的显示器"); return; }
    g_toolsMon = g_cur ? g_cur : g_mons[0];
    g_toolsTab = tab;
    g_toolsOwnMons = false;   // 显示器归主窗口所有
    // 每次新开窗口重置校准向导（阶段/基准/列表），避免上次会话的旧基准串场
    g_calPhase = 1;
    g_calBase.clear();
    g_calSecond.clear();
    g_calChanged.clear();
    g_calLines.clear();
    g_calLineIdx.clear();
    g_calSel = -1;
    g_calScroll = 0;
    g_scanning = false;
    EnsureToolsClass();
    int W = (int)(700 * g_scale), H = (int)(620 * g_scale);
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = (wa.right - wa.left - W) / 2 + wa.left;
    int y = (wa.bottom - wa.top - H) / 2 + wa.top;
    CreateWindowExW(WS_EX_APPWINDOW, L"MonitorOSDTools", L"网管工具",
                    WS_POPUP | WS_VISIBLE, x, y, W, H, g_hwnd, nullptr, GetModuleHandleW(nullptr), nullptr);
}
// /tools 命令行模式：只出工具窗口，不出主界面（对应 C# Program.cs /tools 分支）
// mutexTaken：单实例锁已由 wWinMain 在 CLI 分派前取得（v1.5.3 P1-5），这里不再重复创建
static int RunToolsMode(HINSTANCE, bool mutexTaken) {
    if (!mutexTaken) return 0;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2f))) return 0;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_dwf))) return 0;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LoadFonts();
    EnsureToolsClass();
    auto mons = Enumerate();
    for (auto* m : mons) ProbeMonitor(m);
    if (mons.empty()) {
        MessageBoxW(nullptr, L"未检测到可控制的显示器", L"MonitorOSD 工具", MB_OK | MB_ICONWARNING);
        return 0;
    }
    g_toolsOwnMons = true;
    g_mons = mons;              // 排序后第一台为目标（与 C# 同序）
    g_toolsMon = mons[0];
    int W = (int)(700 * g_scale), H = (int)(620 * g_scale);
    RECT wa; SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    int x = (wa.right - wa.left - W) / 2 + wa.left;
    int y = (wa.bottom - wa.top - H) / 2 + wa.top;
    CreateWindowExW(WS_EX_APPWINDOW, L"MonitorOSDTools", L"网管工具",
                    WS_POPUP | WS_VISIBLE, x, y, W, H, nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    // v1.6.2：窗口已销毁，后台线程（还原/轮询）仍在跑，禁止它们再往已销毁的 hwnd 投递
    g_uiAlive.store(false);
    return 0;
}

// ================= 全屏显示器测试（v1.5.4：坏点/灰阶/拖影/文字/网格）=================
// 独立全屏置顶窗口，投到指定显示器；空格/点击=下一图案，←=上一图案，ESC/右键=退出。
enum TestPat { TP_WHITE, TP_BLACK, TP_RED, TP_GREEN, TP_BLUE, TP_CYAN, TP_MAGENTA, TP_YELLOW,
               TP_GRAY,
               TP_CS_R, TP_CS_G, TP_CS_B, TP_CS_C, TP_CS_M, TP_CS_Y,
               TP_MASTER, TP_CONVERGE_DOTS, TP_CONVERGE_LINES,
               TP_GRID, TP_ZOOM, TP_MOTION, TP_TEXT,
               TP_COUNT };
static const char* TestPatName(int i) {
    switch (i) {
    case TP_WHITE: return "纯白（坏点-暗点）";
    case TP_BLACK: return "纯黑（亮点/漏光）";
    case TP_RED:   return "纯红";
    case TP_GREEN: return "纯绿";
    case TP_BLUE:  return "纯蓝";
    case TP_CYAN:  return "纯青";
    case TP_MAGENTA: return "纯品红";
    case TP_YELLOW:  return "纯黄";
    case TP_GRAY:  return "灰阶渐变（断层/暗部）";
    case TP_CS_R: return "红色阶（偏色/断层）";
    case TP_CS_G: return "绿色阶（偏色/断层）";
    case TP_CS_B: return "蓝色阶（偏色/断层）";
    case TP_CS_C: return "青色阶";
    case TP_CS_M: return "品红色阶";
    case TP_CS_Y: return "黄色阶";
    case TP_MASTER: return "主屏综合（一屏验收）";
    case TP_CONVERGE_DOTS: return "汇聚-点阵（彩色镶边）";
    case TP_CONVERGE_LINES: return "汇聚-线阵（彩色镶边）";
    case TP_GRID:  return "网格点距/几何";
    case TP_ZOOM: return "闪烁缩放（电源品质）";
    case TP_MOTION: return "拖影测试（响应时间）";
    case TP_TEXT:  return "文字清晰度";
    default: return "";
    }
}
static HWND g_testHwnd = nullptr;
static ID2D1HwndRenderTarget* g_testRt = nullptr;
static int  g_testPat = 0;
static float g_testX = 0;            // 拖影方块横向位置
static int   g_testDir = 1;
static bool  g_testLoop = false;     // 循环自动播放（L 键开关，~2 秒/图案）
static int   g_loopTicks = 0;
static RECT  g_testRc = {};          // 目标屏全屏矩形
static bool  s_testClassReady = false;
static bool  g_testOwnMode = false;   // /test 命令行模式：自持消息循环（窗口销毁即退出）
static ID2D1SolidColorBrush* TestBrush(D2D1_COLOR_F c) {
    ID2D1SolidColorBrush* b = nullptr;
    g_testRt->CreateSolidColorBrush(c, &b);
    return b;
}
static void TestText(const char* s, IDWriteTextFormat* f, const D2D1_RECT_F& r, D2D1_COLOR_F col) {
    std::wstring w = Utf8ToWide(s);
    auto b = TestBrush(col);
    if (b) { g_testRt->DrawText(w.c_str(), (UINT32)w.size(), f, r, b); b->Release(); }
}
static D2D1_COLOR_F TestPatColor(int i) {
    switch (i) {
    case TP_BLACK: return { 0.f, 0.f, 0.f, 1.f };
    case TP_RED: return { 1.f, 0.f, 0.f, 1.f };
    case TP_GREEN: return { 0.f, 1.f, 0.f, 1.f };
    case TP_BLUE: return { 0.f, 0.f, 1.f, 1.f };
    case TP_CYAN: return { 0.f, 1.f, 1.f, 1.f };
    case TP_MAGENTA: return { 1.f, 0.f, 1.f, 1.f };
    case TP_YELLOW: return { 1.f, 1.f, 0.f, 1.f };
    default: return { 1.f, 1.f, 1.f, 1.f };   // TP_WHITE 纯白
    }
}
// 阶梯渐变条：base 色 × 亮度 v（0..1），n 阶，用于灰阶/彩色色阶
static void TestBands(D2D1_COLOR_F base, float x0, float y0, float x1, float y1, int n) {
    float bw = (x1 - x0) / n;
    for (int i = 0; i < n; i++) {
        float v = (n == 1) ? 1.f : i / (n - 1.f);
        auto b = TestBrush(D2D1::ColorF(base.r * v, base.g * v, base.b * v, 1.f));
        if (b) { g_testRt->FillRectangle(D2D1::RectF(x0 + i * bw, y0, x0 + (i + 1) * bw, y1), b); b->Release(); }
    }
}
static HRESULT TestPaint() {
    RECT rc; GetClientRect(g_testHwnd, &rc);
    float W = (float)rc.right, H = (float)rc.bottom;
    g_testRt->BeginDraw();
    g_testRt->Clear(D2D1::ColorF(0, 0, 0));
    int p = g_testPat;
    if (p <= TP_YELLOW) {                                   // 纯色 8 连
        auto b = TestBrush(TestPatColor(p));
        if (b) { g_testRt->FillRectangle(D2D1::RectF(0, 0, W, H), b); b->Release(); }
    } else if (p == TP_GRAY) {                              // 灰阶 32 阶
        TestBands({ 1.f, 1.f, 1.f, 1.f }, 0, 0, W, H, 32);
    } else if (p >= TP_CS_R && p <= TP_CS_Y) {              // 彩色色阶：黑 → 该色（64 阶）
        static const D2D1_COLOR_F kCS[6] = { {1,0,0,1},{0,1,0,1},{0,0,1,1},{0,1,1,1},{1,0,1,1},{1,1,0,1} };
        TestBands(kCS[p - TP_CS_R], 0, 0, W, H, 64);
    } else if (p == TP_MASTER) {                            // 主屏综合：一屏验收
        auto bw = TestBrush({ 1.f, 1.f, 1.f, 1.f });
        if (bw) { g_testRt->FillRectangle(D2D1::RectF(0, 0, W, H), bw); bw->Release(); }
        auto bl = TestBrush({ 0.f, 0.f, 0.f, 1.f });
        if (bl) {
            float R = (std::min)(W, H) * 0.17f;             // 中央大圆：几何/线性度
            g_testRt->DrawEllipse(D2D1::Ellipse(D2D1::Point2F(W / 2, H * 0.34f), R, R), bl, 2.f);
            float sr = R * 0.2f;                            // 四角小圆
            D2D1_POINT_2F qs[4] = { {W*0.12f,H*0.10f},{W*0.88f,H*0.10f},{W*0.12f,H*0.60f},{W*0.88f,H*0.60f} };
            for (auto& q : qs) g_testRt->DrawEllipse(D2D1::Ellipse(q, sr, sr), bl, 1.5f);
            TestText("几何：圆要正  灰阶：过渡平滑", FUi(14.f),
                     D2D1::RectF(W * 0.34f, H * 0.50f, W * 0.66f, H * 0.56f), { 0,0,0,1 });
            bl->Release();
        }
        TestBands({ 1.f, 1.f, 1.f, 1.f }, W * 0.06f, H * 0.70f, W * 0.94f, H * 0.78f, 32);
        const D2D1_COLOR_F kCols[8] = { {1,1,1,1},{0,0,0,1},{1,0,0,1},{0,1,0,1},{0,0,1,1},{0,1,1,1},{1,0,1,1},{1,1,0,1} };
        float x0 = W * 0.06f, bwid = (W * 0.94f - x0) / 8;
        for (int i = 0; i < 8; i++) {
            auto b = TestBrush(kCols[i]);
            if (b) { g_testRt->FillRectangle(D2D1::RectF(x0 + i * bwid, H * 0.80f, x0 + (i + 1) * bwid, H * 0.88f), b); b->Release(); }
        }
        TestText("文字清晰度检查 The quick brown fox 0123456789", FUi(15.f),
                 D2D1::RectF(W * 0.06f, H * 0.90f, W * 0.94f, H * 0.96f), { 0,0,0,1 });
    } else if (p == TP_CONVERGE_DOTS || p == TP_CONVERGE_LINES) {   // 汇聚：点阵/线阵看彩色镶边
        auto b = TestBrush({ 1.f, 1.f, 1.f, 1.f });
        float step = 24.f;
        if (b) {
            if (p == TP_CONVERGE_DOTS) {
                for (float y = step; y < H; y += step)
                    for (float x = step; x < W; x += step)
                        g_testRt->FillEllipse(D2D1::Ellipse(D2D1::Point2F(x, y), 1.5f, 1.5f), b);
            } else {
                for (float x = step; x < W; x += step) g_testRt->DrawLine(D2D1::Point2F(x, 0), D2D1::Point2F(x, H), b, 1.f);
                for (float y = step; y < H; y += step) g_testRt->DrawLine(D2D1::Point2F(0, y), D2D1::Point2F(W, y), b, 1.f);
            }
            b->Release();
        }
    } else if (p == TP_ZOOM) {                              // 闪烁缩放：内框黑白闪烁，盯外框尺寸是否变化
        static int s_zf = 0; s_zf++;
        bool on = ((s_zf / 30) % 2) == 0;
        float m = (std::min)(W, H) * 0.06f;
        auto b1 = TestBrush({ 1.f, 1.f, 1.f, 1.f });
        if (b1) { g_testRt->DrawRectangle(D2D1::RectF(m, m, W - m, H - m), b1, 2.f); b1->Release(); }
        auto b2 = TestBrush(on ? D2D1::ColorF(1, 1, 1, 1) : D2D1::ColorF(0, 0, 0, 1));
        float iw = (W - 2 * m) / 3.f, ih = (H - 2 * m) / 3.f;
        if (b2) { g_testRt->FillRectangle(D2D1::RectF(W / 2 - iw / 2, H / 2 - ih / 2, W / 2 + iw / 2, H / 2 + ih / 2), b2); b2->Release(); }
    } else if (p == TP_MOTION) {                            // 拖影：白块横移 + 参照块
        auto b = TestBrush({ 1.f, 1.f, 1.f, 1.f });
        float bw = 140.f, bh = 220.f, y = H / 2 - bh / 2;
        if (b) {
            g_testRt->FillRectangle(D2D1::RectF(60, y, 60 + bw, y + bh), b);          // 参照（静止）
            g_testRt->FillRectangle(D2D1::RectF(g_testX, y, g_testX + bw, y + bh), b);// 移动
            b->Release();
        }
    } else if (p == TP_TEXT) {                              // 文字清晰度：多字号多色
        const char* lines[] = {
            "显示器文字清晰度测试 0123456789",
            "网吧电竞专用 ABCDEFGHIJKLMN",
            "The quick brown fox jumps over the lazy dog 0123456789",
            "纪念那些在深夜里依然亮着的屏幕。",
        };
        float sizes[] = { 28.f, 22.f, 17.f, 14.f, 11.f, 9.f };
        float y = H * 0.08f;
        for (float sz : sizes) {
            auto f = FUi(sz);
            if (f) {
                TestText(lines[0], f, D2D1::RectF(W * 0.06f, y, W * 0.94f, y + sz * 2.2f), { 1,1,1,1 });
                y += sz * 2.2f;
            }
        }
        for (int i = 0; i < 4; i++) {
            auto f = FUi(13.f);
            if (f) TestText(lines[i], f, D2D1::RectF(W * 0.06f, y, W * 0.94f, y + 40.f),
                            i % 2 ? D2D1::ColorF(0.7f, 0.7f, 0.7f, 1.f) : D2D1::ColorF(1.f, 0.3f, 0.3f, 1.f));
            y += 44.f;
        }
    } else if (p == TP_GRID) {                              // 网格 32px + 中心十字
        auto b = TestBrush({ 0.5f, 0.5f, 0.5f, 1.f });
        if (b) {
            for (float x = 0; x < W; x += 32) g_testRt->DrawLine(D2D1::Point2F(x, 0), D2D1::Point2F(x, H), b, 1.f);
            for (float y = 0; y < H; y += 32) g_testRt->DrawLine(D2D1::Point2F(0, y), D2D1::Point2F(W, y), b, 1.f);
            b->Release();
        }
        auto bc = TestBrush({ 1.f, 0.f, 0.f, 1.f });
        if (bc) {
            g_testRt->DrawLine(D2D1::Point2F(W / 2 - 60, H / 2), D2D1::Point2F(W / 2 + 60, H / 2), bc, 2.f);
            g_testRt->DrawLine(D2D1::Point2F(W / 2, H / 2 - 60), D2D1::Point2F(W / 2, H / 2 + 60), bc, 2.f);
            bc->Release();
        }
    }
    // v1.5.4：画面纯净模式——角标与退出按钮已移除（退出方式：ESC / 右键）
    return g_testRt->EndDraw();
}
static LRESULT CALLBACK TestWndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_PAINT: {
        // 定时器已持续绘制；这里确保渲染目标存在并校验（洪水期 WM_PAINT 只是兜底路径）
        RECT rc; GetClientRect(h, &rc);
        if (!g_testRt) {
            D2D1_SIZE_U size = D2D1::SizeU(rc.right - rc.left, rc.bottom - rc.top);
            g_d2f->CreateHwndRenderTarget(D2D1::RenderTargetProperties(D2D1_RENDER_TARGET_TYPE_DEFAULT,
                                          D2D1::PixelFormat(), 96.f, 96.f),
                                          D2D1::HwndRenderTargetProperties(h, size), &g_testRt);
        }
        if (g_testRt) TestPaint();
        ValidateRect(h, nullptr);
        return 0;
    }
    case WM_ERASEBKGND: return 1;
    case WM_SIZE:
        if (g_testRt) {
            RECT rc; GetClientRect(h, &rc);
            if (FAILED(g_testRt->Resize(D2D1::SizeU(rc.right, rc.bottom)))) { g_testRt->Release(); g_testRt = nullptr; }
        }
        return 0;
    case WM_TIMER:
        {
        // v1.5.3 关键修复：全屏窗口覆盖鼠标 → WM_MOUSEMOVE 洪水把低优先级的 WM_PAINT 无限饿死
        // （画面永远黑 + 输入“无反应”）。绘制改由 16ms 定时器直接驱动，不依赖 WM_PAINT。
        RECT rc; GetClientRect(h, &rc);
        if (g_testPat == TP_MOTION) {
            g_testX += 9.f * g_testDir;
            if (g_testX < 0) { g_testX = 0; g_testDir = 1; }
            if (g_testX > rc.right - 140) { g_testX = (float)(rc.right - 140); g_testDir = -1; }
        }
        if (g_testLoop) {
            g_loopTicks++;
            if (g_loopTicks >= 125) { g_loopTicks = 0; g_testPat = (g_testPat + 1) % TP_COUNT; }
        }
        if (g_testRt) {
            // v1.6.6 修复 M6：TestPaint 返回 EndDraw 的 HRESULT，设备丢失（锁屏/显卡重置）时
            // 原实现丢弃返回值，g_testRt 留着失效指针 → 画面定格黑屏只能 ESC。
            // 与主窗 L5736 同模式：FAILED 即 Release + 置 nullptr，下次 WM_PAINT/WM_TIMER 重建。
            if (FAILED(TestPaint())) { g_testRt->Release(); g_testRt = nullptr; }
        }
        return 0;
        }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) { DestroyWindow(h); return 0; }
        if (wp == VK_SPACE || wp == VK_RIGHT) { g_testPat = (g_testPat + 1) % TP_COUNT; InvalidateRect(h, nullptr, FALSE); return 0; }
        if (wp == VK_LEFT) { g_testPat = (g_testPat + TP_COUNT - 1) % TP_COUNT; InvalidateRect(h, nullptr, FALSE); return 0; }
        if (wp == 'L') { g_testLoop = !g_testLoop; g_loopTicks = 0; return 0; }
        return 0;
    case WM_LBUTTONDOWN:
        g_testPat = (g_testPat + 1) % TP_COUNT;
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_RBUTTONDOWN:
        DestroyWindow(h);
        return 0;
    case WM_ACTIVATE:
        if (wp) SetFocus(h);   // 激活即持焦：防止 ESC 失效导致“困在全屏里”
        return 0;
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        KillTimer(h, 1);
        if (g_testRt) { g_testRt->Release(); g_testRt = nullptr; }
        g_testHwnd = nullptr;
        if (g_testOwnMode) PostQuitMessage(0);   // /test 模式：窗口销毁即结束消息循环
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}
// 打开全屏测试：投到 m 所在的物理屏（screenDevice 精确匹配），失败回落主屏
static void OpenMonitorTest(MonitorDev* m) {
    if (g_testHwnd) { SetForegroundWindow(g_testHwnd); return; }   // 已在测试中
    struct Ctx { std::wstring dev; HMONITOR h; RECT rc; } ctx = { Utf8ToWide(m ? m->screenDevice : std::string()) };
    EnumDisplayMonitors(nullptr, nullptr, [](HMONITOR hm, HDC, LPRECT, LPARAM lp) -> BOOL {
        auto* c = (Ctx*)lp; MONITORINFOEXW mi = {}; mi.cbSize = sizeof(mi);
        if (GetMonitorInfoW(hm, &mi) && c->dev == mi.szDevice) { c->h = hm; c->rc = mi.rcMonitor; }
        return TRUE;
    }, (LPARAM)&ctx);
    if (ctx.h) g_testRc = ctx.rc;
    else { g_testRc = { 0, 0, GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN) }; }
    if (!s_testClassReady) {
        WNDCLASSEXW wc = { sizeof(wc) };
        wc.style = CS_DBLCLKS;                 // 双击退出需要
        wc.lpfnWndProc = TestWndProc;
        wc.hInstance = GetModuleHandleW(nullptr);
        wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
        wc.lpszClassName = L"MonitorOSDTest";
        RegisterClassExW(&wc);
        s_testClassReady = true;
    }
    g_testPat = TP_WHITE;   // 从纯白开始（最常用）
    g_testX = 0; g_testDir = 1;
    LogLine("测试: 目标矩形 " + std::to_string(g_testRc.left) + "," + std::to_string(g_testRc.top) +
            " - " + std::to_string(g_testRc.right) + "," + std::to_string(g_testRc.bottom));
    g_testHwnd = CreateWindowExW(WS_EX_TOPMOST | WS_EX_APPWINDOW, L"MonitorOSDTest", L"显示器测试",
                    WS_POPUP | WS_VISIBLE, g_testRc.left, g_testRc.top,
                    g_testRc.right - g_testRc.left, g_testRc.bottom - g_testRc.top,
                    nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    if (!g_testHwnd) {
        LogLine("测试: 窗口创建失败 GLE=" + std::to_string((long)GetLastError()));
        ShowToast("显示器测试窗口创建失败（GLE=" + std::to_string((long)GetLastError()) + "）");
        return;
    }
    SetTimer(g_testHwnd, 1, 16, nullptr);
    SetForegroundWindow(g_testHwnd);
    SetFocus(g_testHwnd);   // 键盘焦点：防 ESC 失效（托盘路径下前台权限可能被系统拦截）
    {
        RECT wr; GetWindowRect(g_testHwnd, &wr);
        LogLine("测试: 窗口矩形 " + std::to_string(wr.left) + "," + std::to_string(wr.top) + " - " +
                std::to_string(wr.right) + "," + std::to_string(wr.bottom));
    }
    LogLine("测试: 全屏测试已打开（投屏=" + (m ? m->screenDevice : std::string("主屏")) + "，图案=" + TestPatName(g_testPat) + "）");
}

// /test 命令行模式：只出全屏显示器测试，不出主界面
static int RunTestMode(HINSTANCE, bool mutexTaken) {
    if (!mutexTaken) return 0;
    if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2f))) return 0;
    if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_dwf))) return 0;
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    LoadFonts();
    BOOTMARK("wWinMain: LoadFonts 完成");
    auto mons = Enumerate();
    MonitorDev* m = mons.empty() ? nullptr : mons[0];
    g_testOwnMode = true;
    OpenMonitorTest(m);
    LogLine("测试: 进入消息循环");
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    for (auto* mm : mons) { mm->Retire(); }   // v1.5.8 P1-4：延迟回收
    return 0;
}

// ================= 窗口过程 =================
static LRESULT CALLBACK WndProc(HWND h, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        g_hwnd = h;
        g_uiAlive.store(true);            // v1.6.2：允许后台线程向 UI 投递状态
        RECT rc; GetClientRect(h, &rc);
        g_W = rc.right; g_H = rc.bottom;
        // D2D/DWrite/WIC/字体已在窗口创建前初始化（wWinMain）——慢机器上窗口一出现即是加载动画
        CreateDeviceResources();
        if (!g_rt) return -1;
        SetTimer(h, 1, 500, nullptr);     // toast 状态回落
        SetTimer(h, 2, 30000, nullptr);   // 闲置自动还原
        SetTimer(h, 3, 3000, nullptr);    // 值轮询：手动在 OSD 上调整后界面实时跟随
        SetTimer(h, 6, 33, nullptr);      // 启动加载动画帧（探测完成自动停）
        RegisterHotKey(h, 1, MOD_CONTROL | MOD_ALT, 'O');
        RegisterHotKey(h, 2, MOD_CONTROL | MOD_ALT, 'R');
        RegisterHotKey(h, 3, MOD_CONTROL | MOD_ALT, 'S');
        RegisterHotKey(h, 4, MOD_CONTROL | MOD_ALT, VK_LEFT);
        RegisterHotKey(h, 5, MOD_CONTROL | MOD_ALT, VK_RIGHT);
        if (g_config.checkUpdateOnStart) SetTimer(h, 4, 8000, nullptr);              // 启动 8s 后静默检查一次
        if (g_config.updateCheckHours > 0)
            SetTimer(h, 5, (UINT)(g_config.updateCheckHours * 3600u * 1000u), nullptr);
        ApplyRounded();
        InitTray();
        StartProbe();
        return 0;
    }
    case WM_SIZE: {
        RECT rc; GetClientRect(h, &rc);
        g_W = rc.right; g_H = rc.bottom;
        if (g_rt) {
            if (FAILED(g_rt->Resize(D2D1::SizeU(g_W, g_H)))) { g_rt->Release(); g_rt = nullptr; }
        }
        if (g_roundRgn) {
            DeleteObject(g_roundRgn);
            g_roundRgn = nullptr;
        }
        HRGN rgn = CreateRoundRectRgn(0, 0, g_W + 1, g_H + 1, SI(16), SI(16));
        // v1.5.3 P2-4：同上——成功后所有权归系统，不保留句柄
        if (SetWindowRgn(h, rgn, TRUE) == 0) DeleteObject(rgn);
        if (!g_probing) LayoutAll();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_ERASEBKGND:
        return 1;
    case WM_SETCURSOR:
        // 探测期/任何时刻客户端区一律正常箭头（避免启动时系统忙等光标误导「卡死」）
        if (LOWORD(lp) == HTCLIENT) { SetCursor(LoadCursorW(nullptr, (LPCWSTR)IDC_ARROW)); return TRUE; }
        break;
    case WM_DISPLAYCHANGE:
        // v1.5.3：改分辨率/拓扑变化 → 延迟 3 秒重新枚举探测（合并连续事件）
        if (!g_probing) { SetTimer(h, 7, 3000, nullptr); }
        return 0;
    case WM_DEVICECHANGE:
        // v1.5.3 关键（对齐 PassMark 无感换屏）：显示器插拔即使分辨率不变也会发
        // DBT_DEVNODES_CHANGED（设备树变化）——只挂 WM_DISPLAYCHANGE 会漏掉纯换屏场景。
        // DBT_DEVNODES_CHANGED 覆盖面最广（插拔/驱动更新都发），靠 timer 7 的 3 秒防抖合并。
        if (wp == DBT_DEVNODES_CHANGED && !g_probing) { SetTimer(h, 7, 3000, nullptr); }
        return 0;
    case WM_TIMER: {
        if (wp == 90) {   // v1.6.11 关于页「复制版本信息」回执文案恢复
            KillTimer(h, 90);
            g_aboutCopied.clear();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        if (wp == 7) {   // 显示环境变化重探（延迟合并）
            KillTimer(h, 7);
            if (g_probing && !g_deepDone) {
                // 深探测进行中被插拔事件打断 → 重新排一个 3 秒定时器，等它跑完再说，
                // 不要立刻重探（重探会作废旧 gen，前一轮成果全丢）
                LogLine("探测: 深探测进行中，延后处理显示环境变化");
                SetTimer(h, 7, 3000, nullptr);
                return 0;
            }
            if (!g_probing) {   // g_mons 为空也允许重探（否则首次探测失败的机器永远无法恢复）
                LogLine("显示环境变化，重新探测显示器");
                g_cur = nullptr;   // 旧句柄已失效，防悬垂
                g_probing = true;
                StartProbe();
            }
            return 0;
        }
        if (wp == 6) {   // 窗口显示调度：缓存快→放行即显示；探测慢→0.4s 后显示加载动画；15s 兜底强制放行
            static ULONGLONG s_animStart = 0;
            if (!s_animStart) s_animStart = GetTickCount64();
            ULONGLONG el = GetTickCount64() - s_animStart;
            if (g_probing && !g_shown && el > 400) { ShowWindow(h, SW_SHOW); g_shown = true; }
            if (g_probing && !g_deepDone && el > 15000) {
                // 超时只解除「界面等待」，不算探测结束：深探测可能还卡在某个 DDC 读上。
                // 状态先给出可诊断的文案，等 WM_APP+6 到达后再覆盖为最终结论。
                if (g_statusText.find("后台探测") != std::string::npos ||
                    g_statusText.find("正在检测") != std::string::npos) {
                    g_statusDot = Theme::Accent;   // 超时用强调色（非最终失败，不用危险色）
                    g_statusText = "DDC/CI 检测超时（显示器响应过慢或存在中间设备）— 仍在后台重试";
                    LogLine("启动: 深探测超时 15s，界面先放行，后台继续");
                }
            }
            if (g_probing) {
                if (g_shown) InvalidateRect(h, nullptr, FALSE);
            } else {
                KillTimer(h, 6);
                if (!g_shown) { ShowWindow(h, SW_SHOW); g_shown = true; }
                InvalidateRect(h, nullptr, FALSE);
            }
            return 0;
        }
        if (wp == 1) {
            if (g_toastUntil && GetTickCount64() > g_toastUntil && g_statusDot == Theme::Accent) {
                g_statusDot = (g_cur && g_cur->ddcOk) ? Theme::Success : Theme::Danger;
                g_statusText = (g_cur && g_cur->ddcOk) ? "DDC/CI 正常" : "DDC/CI 不可用";
                InvalidateRect(h, nullptr, FALSE);
            }
        } else if (wp == 2) {
            // v1.6.6 修复 3：自动还原也要过 g_restoring——原实现只拦了 timer3 轮询，
            // 手动还原跑到一半时闲置计时恰好超时会再起一个 RestoreAllInternal 线程并发写 DDC。
            if (g_config.autoRestoreMinutes > 0 && g_lastChangeTick != 0 && !g_restoring.load() &&
                !g_restoreBusy.load() &&
                (GetTickCount64() - g_lastChangeTick) >= (ULONGLONG)g_config.autoRestoreMinutes * 60000ull) {
                g_lastChangeTick = 0;
                g_restoreBusy.store(true);   // 与手动还原共用重入守卫，按钮同步显示「还原中…」
                std::thread([]() {
                    int n = RestoreAllInternal();
                    g_restoreBusy.store(false);
                    PostStatus("闲置超时，已自动还原（" + std::to_string((std::max)(0, n)) + " 项）");
                    if (g_hwnd) PostMessageW(g_hwnd, WM_APP + 2, 0, 0);
                }).detach();
            }
        } else if (wp == 3) {
            // 实时跟随：手动在显示器 OSD 上调整后，界面 3 秒内同步。
            // 仅前台可见时轮询（隐藏/最小化即停，恢复显示立即刷一次）——保护华硕等敏感 DDC 通道；
            // 拖动滑条中 / 写队列未清空时跳过；显示器无响应不唤醒、静默跳过。
            static bool s_wasHidden = true;
            // v1.6.6 修复 M8：原判定加了 `GetForegroundWindow() == h`，但 WS_POPUP 自绘窗用热键唤出时
            // 常被系统焦点窃取保护拦下（可见但非前台）→ 走 15 秒慢速分支，用户按显示器物理键界面不跟手。
            // 改为「可见即轮询」，前台与否只影响频率不影响是否轮询。
            bool foreground = GetForegroundWindow() == h;
            bool visible = IsWindowVisible(h) && !IsIconic(h);
            // v1.5.3 P1-1：开启闲置还原后，即使窗口不可见也要低频检测"基准是否被偏离"——
            // 网吧的顾客直接按显示器按键调参，程序通常缩在托盘里，原来的"仅前台可见才轮询"
            // 让偏离检测永远跑不到。这里降频到 15 秒一次，兼顾华硕等敏感 DDC 通道。
            if (!visible) {
                s_wasHidden = true;
                if (g_config.autoRestoreMinutes > 0 && !g_probing && g_cur && !g_draggingSlider && !g_cur->hasPend && !g_restoring.load()) {
                    static ULONGLONG s_lastBg = 0;
                    ULONGLONG now = GetTickCount64();
                    if (now - s_lastBg >= 15000) { s_lastBg = now; RefreshAllValues(false); }
                }
                return 0;
            }
            if (!g_cur || g_probing || g_draggingSlider || g_cur->hasPend || g_restoring.load()) return 0;   // v1.6.6：还原中暂停轮询
            if (s_wasHidden) { s_wasHidden = false; RefreshAllValues(false); return 0; }
            // 可见但非前台（热键唤出被焦点窃取保护拦）：降频到 6s 轮询，兼顾跟手与 DDC 通道压力
            if (!foreground) {
                static ULONGLONG s_lastFg = 0;
                ULONGLONG now = GetTickCount64();
                if (now - s_lastFg < 6000) return 0;
                s_lastFg = now;
            }
            RefreshAllValues(false);
        } else if (wp == 4) {
            KillTimer(h, 4);                       // 只跑一次
            if (g_config.checkUpdateOnStart) CheckUpdateAsync(false);
        } else if (wp == 5) {
            CheckUpdateAsync(false);               // 周期性后台检查
        } else if (wp == 8) {
            // v1.6.6 修复 M7：D2D render target 重建失败后的重试
            KillTimer(h, 8);
            if (!g_rt) { CreateDeviceResources(); if (g_rt) InvalidateRect(h, nullptr, FALSE); }
        }
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps;
        BeginPaint(h, &ps);
        PaintFrame();
        EndPaint(h, &ps);
        return 0;
    }
    case WM_MOUSELEAVE:
        g_mouse = { -1, -1 };
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_MOUSEWHEEL: {
        // v1.6.7：基准档案弹层滚动（优先级最高，模态）
        if (g_pfOpen) {
            g_pfScroll -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * SI(34);
            if (g_pfScroll < 0) g_pfScroll = 0;
            if (g_pfScroll > g_pfMaxScroll) g_pfScroll = g_pfMaxScroll;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        // 详细信息弹窗滚动
        if (g_infoOpen) {
            g_infoScroll -= GET_WHEEL_DELTA_WPARAM(wp) / WHEEL_DELTA * SI(24);
            if (g_infoScroll < 0) g_infoScroll = 0;
            if (g_infoScroll > g_infoMaxScroll) g_infoScroll = g_infoMaxScroll;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        // 自绘下拉面板滚动
        if (g_ddMode) {
            int count = g_ddMode == 1 ? (int)g_resOpts.size() : (int)g_hzOpts.size();
            int vis = (int)g_ddItems.size();
            int d = GET_WHEEL_DELTA_WPARAM(wp);
            g_ddScroll -= d / WHEEL_DELTA;
            if (g_ddScroll < 0) g_ddScroll = 0;
            if (g_ddScroll > count - vis) g_ddScroll = count - vis;
            if (g_ddScroll < 0) g_ddScroll = 0;
            RebuildDdItems();
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        // 参数页内容超出两列可用高度时滚轮滚动（华硕等多分组机型）
        if (g_page == 0 && g_scrollMax0 > 0) {
            int d = GET_WHEEL_DELTA_WPARAM(wp);
            g_scroll0 -= (d / WHEEL_DELTA) * SI(56);
            if (g_scroll0 < 0) g_scroll0 = 0;
            if (g_scroll0 > g_scrollMax0) g_scroll0 = g_scrollMax0;
            LayoutAll();
            InvalidateRect(h, nullptr, FALSE);
        }
        return 0;
    }
    case WM_MOUSEMOVE: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        g_mouse = pt;
        // 拖拽只在参数页有效；切页后旧滑条矩形未重排，跨页拖拽会误调显示器
        if (g_page != 0) { g_draggingSlider = false; g_dragItem = nullptr; }
        if (g_draggingSlider && g_dragItem) {
            SliderSetFromX(g_dragItem, g_dragItem->trackR, pt.x);
            return 0;
        }
        if (g_page == 0 && (wp & MK_LBUTTON) && g_mouseDown) {
            for (auto& G : g_menu)
                for (auto& it : G.items) {
                    if (!it.visible || it.trackR.right <= it.trackR.left) continue;
                    RECT wide = InflateR(it.trackR, 0, 20);
                    if (PtIn(wide, pt)) { SliderSetFromX(&it, it.trackR, pt.x); return 0; }
                }
        }
        InvalidateRect(h, nullptr, FALSE);   // hover 状态变化才整窗重绘
        return 0;
    }
    case WM_CAPTURECHANGED:
        // 捕获被夺（弹窗/切窗）：立即终止拖拽，防止松键丢失后滑条跟随鼠标
        g_draggingSlider = false;
        g_dragItem = nullptr;
        g_mouseDown = false;
        return 0;
    case WM_LBUTTONDOWN: {
        POINT pt = { GET_X_LPARAM(lp), GET_Y_LPARAM(lp) };
        g_mouse = pt;
        g_mouseDown = true;
        g_pressRect = { 0,0,0,0 };
        SetCapture(h);
        if (g_confirmReset) {   // 确认卡打开时吞掉所有点击：是/否/点外部关闭
            g_confirmReset = false;
            if (PtIn(g_confirmYes, pt) && g_cur) StartResetFactory(g_cur);
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        if (g_pfOpen) {         // v1.6.7 基准档案弹层：点 ✕ 或面板外关闭（模态吞点击）
            bool inCard = PtIn(g_pfCard, pt);
            if (PtIn(g_pfChkSeries, pt)) {
                g_pfShowSeries = !g_pfShowSeries;
                BuildProfileRows();
                g_pfScroll = 0;
            } else if (PtIn(g_pfClose, pt) || !inCard) {
                g_pfOpen = false;
            }
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        if (g_infoOpen) {       // 详细信息弹窗：点 ✕ 或面板外关闭
            bool inCard = PtIn(g_infoCard, pt);
            if (PtIn(g_infoClose, pt) || !inCard) g_infoOpen = false;
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        if (g_ddMode) {         // 自绘下拉面板：点选项应用，点外部关闭
            int mode = g_ddMode;
            int picked = -1;
            for (size_t i = 0; i < g_ddItems.size(); i++)
                if (PtIn(g_ddItems[i], pt)) { picked = g_ddIdx[i]; break; }
            g_ddMode = 0;
            if (picked >= 0 && g_cur) {
                if (mode == 1) { auto& o = g_resOpts[picked]; ApplyDisplayMode(true, o.w, o.h, 0); }
                else           { ApplyDisplayMode(false, 0, 0, g_hzOpts[picked]); }
            }
            InvalidateRect(h, nullptr, FALSE);
            return 0;
        }
        if (PtIn(g_btnClose, pt)) {
            // 关闭按钮遵循软件设置：勾选「最小化到托盘」→ 隐藏；未勾选 → 真退出
            // v1.6.6：默认 minimizeToTray=true——网吧顾客点「关闭」不会中断正在跑的还原。
            if (g_config.minimizeToTray) {
                // 还原进行中给提示，避免用户以为程序卡死
                ShowWindow(h, SW_HIDE);
                if (g_restoreBusy.load()) {
                    g_nid.uFlags = NIF_INFO;
                    wcscpy_s(g_nid.szInfo, L"正在后台还原基准，完成后程序仍在托盘运行。右键托盘图标可退出。");
                    wcscpy_s(g_nid.szInfoTitle, L"显示器控制");
                    g_nid.dwInfoFlags = NIIF_INFO;
                    g_nid.uTimeout = 5000;
                    Shell_NotifyIconW(NIM_MODIFY, &g_nid);
                    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;   // 恢复，避免下次 NIM_MODIFY 带残留
                }
                return 0;
            }
            g_reallyExit = true;
            if (g_config.restoreOnExit) RestoreAllInternal();
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            DestroyWindow(h);
            return 0;
        }
        if (PtIn(g_btnMin, pt)) {
            // 软件设置卡「最小化到托盘」开启时隐藏，否则正常最小化
            if (g_config.minimizeToTray) ShowWindow(h, SW_HIDE);
            else ShowWindow(h, SW_MINIMIZE);
            return 0;
        }
        if (pt.y <= SI(36)) {   // 标题栏拖动
            ReleaseCapture();
            SendMessageW(h, WM_NCLBUTTONDOWN, HTCAPTION, 0);
            return 0;
        }
        if (PtIn(g_tabP, pt)) { g_page = 0; g_draggingSlider = false; g_dragItem = nullptr; LayoutAll(); Invalidate(); return 0; }
        if (PtIn(g_tabS, pt)) { g_page = 1; g_draggingSlider = false; g_dragItem = nullptr; LayoutAll(); Invalidate(); return 0; }
        if (PtIn(g_tabA, pt)) { g_page = 2; g_draggingSlider = false; g_dragItem = nullptr; LayoutAll(); Invalidate(); return 0; }   // v1.6.8
        // 显示器卡（两个页签通用）：切换选中 / 分辨率刷新率下拉 / 详细信息
        auto* m = HitMon(pt);
        if (m && m != g_cur) { g_cur = m; ApplyMonitor(); return 0; }
        if (PtIn(g_resRow, pt) && !g_resOpts.empty()) { OpenDisplayDropdown(h, 1, g_resRow); return 0; }
        if (PtIn(g_hzRow, pt) && !g_hzOpts.empty()) { OpenDisplayDropdown(h, 2, g_hzRow); return 0; }
        if (PtIn(g_btnDetail, pt) && g_cur) { OpenInfoPanel(); InvalidateRect(h, nullptr, FALSE); return 0; }
        if (g_page == 0) {
            // 按压态保留到松手（与 C# 一致：MouseUp 才清除）
            if (PtIn(g_btnRestore, pt)) { g_pressRect = g_btnRestore; RestoreWorker(); Invalidate(); return 0; }
            // 分段点击
            Item* segItem = nullptr; Item::Seg* seg = nullptr;
            HitSeg(pt, &segItem, &seg);
            if (seg) {
                g_pressRect = seg->r;
                SetVcp(segItem, seg->v, seg->label);
                Invalidate();
                return 0;
            }
            // 功能项点击（恢复出厂/扫描器/校准/唤醒等）
            Item* ait = HitRow(pt);
            if (ait && ait->action) {
                const char* a = ait->action;
                if (!strcmp(a, "resetall")) {
                    if (g_cur) { g_confirmReset = true; Invalidate(); }
                } else if (!strcmp(a, "wakeup")) {
                    if (g_cur) {
                        MonitorDev* m = g_cur;
                        ShowToast("正在唤醒显示器…");
                        m->AddRef();
                        std::thread([m]() {
                            WakeUp(m);
                            unsigned c, x;
                            bool ok = m->Read(0x10, c, x);
                            m->Release();
                            PostStatus(ok ? "显示器已响应（DDC 通信正常）"
                                          : "唤醒未成功 — 请按显示器按键或检查电源");
                        }).detach();
                    }
                } else if (!strcmp(a, "scanner"))   OpenToolsWindow(0);
                else if (!strcmp(a, "calibrate"))   OpenToolsWindow(1);
                else if (!strcmp(a, "savebase"))    SaveBaseline();
                else if (!strcmp(a, "restorebase")) RestoreWorker();
                Invalidate();
                return 0;
            }
            // 滑条点击/拖动
            Item* it = HitRow(pt);
            if (it && !it->action && it->mapId && g_cur) {
                const ValSpec* s = SpecOfItem(it);
                if (s && !s->IsEnum()) {
                    RECT wide = InflateR(it->trackR, 0, 12);
                    if (PtIn(wide, pt)) { SliderSetFromX(it, it->trackR, pt.x); return 0; }
                }
            }
        } else if (g_page == 2) {
            // v1.6.8 关于页：一键还原按钮照常可用。
            // v1.6.11：更新中心内联进关于页——主按钮走状态机（检查/下载/重启安装），不再弹窗。
            if (PtIn(g_btnRestore, pt)) { g_pressRect = g_btnRestore; RestoreWorker(); Invalidate(); return 0; }
            if (PtIn(g_aboutBtnUpd, pt)) {
                EnterCriticalSection(&g_netCs);
                int st = g_upd.state;
                LeaveCriticalSection(&g_netCs);
                if (st == UPD_AVAILABLE)      StartDownloadAsync();
                else if (st == UPD_READY)     ApplyUpdateAndRestart();
                else if (st != UPD_CHECKING && st != UPD_DOWNLOADING) CheckUpdateAsync(true);
                Invalidate();
                return 0;
            }
            if (PtIn(g_aboutBtnLog, pt)) {
                std::string logp = ExeDir() + "\\update.log";
                FILE* f = FopenUtf8((char*)logp.c_str(), "rb");
                if (f) { fclose(f); ShellExecuteW(nullptr, L"open", Utf8ToWide(logp).c_str(), nullptr, nullptr, SW_SHOWNORMAL); }
                else   ShowToast("暂无更新日志");
                return 0;
            }
            if (PtIn(g_aboutBtnFb, pt)) { OpenUpdateWindow(); return 0; }
            if (PtIn(g_aboutLnkDir, pt)) {
                ShellExecuteW(nullptr, L"open", Utf8ToWide(ExeDir()).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                return 0;
            }
            if (PtIn(g_aboutLnkLog, pt)) {
                std::string lp = ExeDir() + "\\log.txt";
                if (GetFileAttributesW(Utf8ToWide(lp).c_str()) != INVALID_FILE_ATTRIBUTES)
                    ShellExecuteW(nullptr, L"open", Utf8ToWide(lp).c_str(), nullptr, nullptr, SW_SHOWNORMAL);
                else ShowToast("还没有日志文件（程序刚启动或本次未产生记录）");
                return 0;
            }
            if (PtIn(g_aboutLnkCopy, pt)) {
                ClipPutText(h, CopyableInfo());
                g_aboutCopied = "已复制";
                SetTimer(h, 90, 1600, nullptr);   // 1.6s 后恢复文案（WM_TIMER 90）
                Invalidate();
                return 0;
            }
        } else {
            SysClick(pt);   // 系统管理页（三卡）
        }
        return 0;
    }
    case WM_LBUTTONUP: {
        if (GetCapture() == h) ReleaseCapture();
        g_mouseDown = false;
        g_pressRect = { 0,0,0,0 };
        g_draggingSlider = false;
        g_dragItem = nullptr;
        Invalidate();
        return 0;
    }
    case WM_KEYDOWN:
        if (wp == VK_ESCAPE) {
            // v1.6.7：有弹层时 ESC 先关弹层（含之前未处理的详细信息/下拉/确认卡），再隐藏窗口
            if (g_pfOpen)        { g_pfOpen = false; InvalidateRect(h, nullptr, FALSE); return 0; }
            if (g_infoOpen)      { g_infoOpen = false; InvalidateRect(h, nullptr, FALSE); return 0; }
            if (g_ddMode)        { g_ddMode = 0; InvalidateRect(h, nullptr, FALSE); return 0; }
            if (g_confirmReset)  { g_confirmReset = false; InvalidateRect(h, nullptr, FALSE); return 0; }
            ShowWindow(h, SW_HIDE);
            return 0;
        }
        break;
    case WM_APP + 20:   // v1.6.11：更新状态变化（工作线程）——关于页内联更新中心需实时刷新
    case WM_APP + 21:   // 反馈提交结果
        if (g_page == 2) InvalidateRect(h, nullptr, FALSE);
        return 0;
    case WM_ACTIVATE:
        if (LOWORD(wp) != WA_INACTIVE && !g_probing && g_cur && g_cur->ddcOk)
            RefreshAllValues();   // 切回窗口时同步显示器真实状态
        return 0;
    case WM_HOTKEY: {
        switch (wp) {
        case 1: ToggleVisible(); return 0;
        case 2: RestoreWorker(); return 0;
        case 3: SaveBaseline(); return 0;
        case 4: SwitchMonitor(-1); return 0;
        case 5: SwitchMonitor(1); return 0;
        }
        return 0;
    }
    case WM_APP + 1: {
        auto* s2 = (std::string*)lp;
        if (s2) { ShowToast(*s2); delete s2; }
        // v1.6.6 修复 M9：后台状态回执（还原/唤醒失败等）不应把省电休眠的显示器唤醒——
        // 原实现 RefreshAllValues() 用默认 allowWake=true，与 timer3 明确 allowWake=false 的设计矛盾。
        RefreshAllValues(false);
        return 0;
    }
    case WM_APP + 2: {
        LayoutAll();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_APP + 6: {
        // 深探测完成：caps/型号已就绪。快速路径首屏时 capModel 为空，
        // DetectBrand 可能落到 generic → 此处重判品牌并重建菜单（否则菜单/信息卡缺失）
        // v1.5.8 P1-4：校验代数——期间若已发起过新一轮探测，本条属于过期信号，直接丢弃。
        // 否则旧探测的完成信号会去遍历新一轮的 g_mons，与并发重探打架。
        int gen = (int)wp;
        if (gen != g_probeGen.load()) {
            LogLine("探测: 丢弃过期完成信号（gen=" + std::to_string(gen) +
                    " 当前=" + std::to_string(g_probeGen.load()) + "）");
            return 0;
        }
        for (auto* m : g_mons) m->brand = DetectBrand(m);
        // V1.5.9：深探测到此才真正结束 —— 此时必须给出最终结论，不能继续挂着
        // 「正在检测 DDC/CI…」。旧版从不在这里更新状态，探测失败的机器会永远停在那句提示上。
        g_probing = false;
        g_deepDone = true;
        if (g_cur) {
            if (g_cur->ddcOk) {
                g_statusDot = Theme::Success;
                g_statusText = "DDC/CI 正常";
            } else if (g_cur->capsOk) {
                // caps 能读、VCP 读值全失败：典型是固件拒答 DDC 读指令或 KVM/切换器拦截
                g_statusDot = Theme::Danger;
                g_statusText = "DDC/CI 读取失败（能识别显示器但读不到参数，请检查 DDC/CI 开关或中间设备）";
            } else {
                g_statusDot = Theme::Danger;
                g_statusText = "DDC/CI 不可用（显示器未响应，请确认已在 OSD 中开启 DDC/CI）";
            }
            // v1.6.9：caps 已改纯后台查询，深探测完成时 caps 可能仍在后台——措辞如实区分
            LogLine(std::string("探测: 深探测完成（菜单码）ddc=") + (g_cur->ddcOk ? "OK" : "FAIL") +
                    (g_cur->capsOk ? " caps=OK" : " caps=后台中"));
        }
        if (g_cur) ApplyMonitor(); else LayoutAll();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    // v1.5.8 P1-4：探测就绪交付（原来用 WM_APP+4，现在带代数）
    case WM_APP + 7: {
        int gen = (int)wp;
        auto* mons = (std::vector<MonitorDev*>*)lp;
        if (gen != g_probeGen.load()) {
            // 过期：这批对象界面不再接管，直接退役，等在途线程用完自行释放
            LogLine("探测: 丢弃过期列表（gen=" + std::to_string(gen) +
                    " 当前=" + std::to_string(g_probeGen.load()) + "）");
            for (auto* m : *mons) m->Retire();
            delete mons;
            return 0;
        }
        if (g_toolsHwnd) DestroyWindow(g_toolsHwnd);   // 旧显示器句柄即将失效，先关工具窗防悬垂
        // 先断开 g_cur，避免退役过程中 UI 绘制/定时器读到半失效对象
        g_cur = nullptr;
        for (auto* om : g_mons) om->Retire();   // 延迟回收：在途线程（Pump/LoadCaps/WakeUp）用完才真正 delete
        g_mons = *mons;
        delete mons;
        // v1.5.9 修复：这里不能把 g_probing 置 false —— 列表只是「首屏就绪」，
        // 深探测（caps/全码，约 5~10s）还在同一个线程里跑。原来一置 false，
        // 任意 WM_DEVICECHANGE 都会立刻重探并打断它，导致 ddcOk 永远为 false、
        // 界面卡在「正在检测 DDC/CI…」。改为等 WM_APP+6（深探测完成）再复位。
        g_deepDone = false;
        KillTimer(h, 6);   // 加载动画结束（缓存快路径：窗口此刻首次显示）
        if (!g_shown) { ShowWindow(h, SW_SHOW); g_shown = true; }
        InvalidateRect(h, nullptr, FALSE);
        if (g_mons.empty()) {
            g_statusDot = Theme::Danger;
            g_statusText = "DDC/CI 未检测到显示器（严禁静默失败 → 检查连接与 DDC/CI 开关）";
        } else {
            g_cur = g_mons[0];
            ApplyMonitor();
            if (g_cur->ddcOk) {
                g_statusDot = Theme::Success;
                g_statusText = "DDC/CI 正常";
            } else {
                // 缓存命中但存活未验证 / 无缓存首启：先不吓人，深探测（后台）给出最终判定
                g_statusDot = Theme::Accent;
                g_statusText = "正在检测 DDC/CI…（后台探测完成后更新）";
            }
        }
        LayoutAll();
        InvalidateRect(h, nullptr, FALSE);
        return 0;
    }
    case WM_TRAY:
        if (lp == WM_RBUTTONUP) ShowTrayMenu();
        else if (lp == WM_LBUTTONDBLCLK) ToggleVisible();
        return 0;
    case WM_COMMAND:
        if (LOWORD(wp) >= 6001 && LOWORD(wp) <= 6012) {   // 调试预览品牌
            ApplyDebugBrand(LOWORD(wp) - 6001);
            return 0;
        }
        if (LOWORD(wp) >= 4001 && LOWORD(wp) < 4001 + (int)g_resOpts.size()) {
            auto& o = g_resOpts[LOWORD(wp) - 4001];
            ApplyDisplayMode(true, o.w, o.h, 0);
            return 0;
        }
        if (LOWORD(wp) >= 5001 && LOWORD(wp) < 5001 + (int)g_hzOpts.size()) {
            ApplyDisplayMode(false, 0, 0, g_hzOpts[LOWORD(wp) - 5001]);
            return 0;
        }
        switch (LOWORD(wp)) {
        case TRAY_SHOWHIDE: ToggleVisible(); return 0;
        case TRAY_TOOLS:    OpenToolsWindow(0); return 0;
        case TRAY_TEST:     OpenMonitorTest(g_cur); return 0;
        case TRAY_EXIT:
            g_reallyExit = true;
            Shell_NotifyIconW(NIM_DELETE, &g_nid);
            DestroyWindow(h);
            return 0;
        }
        return 0;
    case WM_ENDSESSION:
        // Windows 注销/关机时还原（与 C# SessionEnding 一致）
        if (g_config.restoreOnExit) RestoreAllInternal();
        return 0;
    case WM_CLOSE:
        // Alt+F4 与关闭按钮同规则：勾选托盘 → 隐藏；未勾选 → 真退出
        if (!g_reallyExit && g_config.minimizeToTray) { ShowWindow(h, SW_HIDE); return 0; }
        break;
    case WM_DESTROY:
        for (int i = 1; i <= 5; i++) UnregisterHotKey(h, i);
        KillTimer(h, 1);
        KillTimer(h, 2);
        KillTimer(h, 3);
        KillTimer(h, 4);
        KillTimer(h, 5);
        KillTimer(h, 6);
        KillTimer(h, 7);
        SaveDdcCacheAll(g_mons);   // v1.5.3：退出前保存缓存（深探测中途退出也不丢已确认的数据）
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        if (g_trayIcon) DestroyIcon(g_trayIcon);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, msg, wp, lp);
}

static int RunToolsMode(HINSTANCE hInst, bool mutexTaken);   // 前向声明（网管工具窗口，见文件末尾）
static int RunTestMode(HINSTANCE hInst, bool mutexTaken);

int WINAPI wWinMain(HINSTANCE hInst, HINSTANCE, LPWSTR, int) {
    s_t0 = GetTickCount64();   // 启动耗时诊断基点（临时）
    FILE* dbg = fopen("boot_dbg.txt", "w");
    if (dbg) { fprintf(dbg, "entered wWinMain @%llu ms\n", GetTickCount64() - s_t0); fclose(dbg); }
    SetProcessDPIAware();
    RECT wa;
    SystemParametersInfoW(SPI_GETWORKAREA, 0, &wa, 0);
    // v1.5.3 分辨率自适应：以 1K(1920 宽工作区) 为基准 1.0——1K 窗口 1152×712 不变，
    // 2K≈1.33（1536×949）、4K≈2.0（2304×1424），视觉占比恒定；高度不超工作区 80%；下限 0.45、上限 2.0
    g_scale = (wa.right - wa.left) / 1920.f;
    float hMax = (wa.bottom - wa.top) * 0.80f / 712.f;   // 高度钳制（超宽屏防溢出）
    g_scale = (std::min)(g_scale, hMax);
    g_scale = (std::min)(g_scale, 2.0f);
    g_scale = (std::max)(g_scale, 0.45f);
    int w = (int)(1152 * g_scale), hh = (int)(712 * g_scale);
    // 命令行参数（/xxx 与 -xxx 均可）
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(GetCommandLineW(), &argc);
    if (argv) {
        for (int i = 0; i < argc; i++) g_cliArgs.push_back(argv[i]);
        LocalFree(argv);
    }
    LoadConfig();
    LoadProfiles();
    LoadVcpOverrides();
    ApplyAutoStart();   // 以配置为准同步开机自启项（手动删了注册表也能自恢复）
    InitializeCriticalSection(&g_netCs);
    // 上次升级残留：.old 已不再被占用，删掉
    {
        wchar_t selfW[MAX_PATH] = { 0 };
        GetModuleFileNameW(nullptr, selfW, MAX_PATH);
        if (selfW[0]) DeleteFileW(Utf8ToWide(WideToUtf8(selfW) + ".old").c_str());
    }

    // v1.5.3 P1-5 修复（第二次）：单实例互斥必须覆盖**所有**入口，包括 CLI。
    // 原实现把 mutex 放在 CLI 分派之后，/set /restore /scan /checkupdate /selftest /feedback
    // 六个分支都在这之前 return → 计划任务跑 /restore 时用户手开 GUI，两个进程各持一份
    // 进程内 g_bus，同时对同一显示器发 DDC 命令（轻则写失败，重则固件限流/假死）。
    static HANDLE s_mutex = CreateMutexW(nullptr, TRUE, L"MonitorOSD_Netbar");
    bool mutexTaken = (GetLastError() != ERROR_ALREADY_EXISTS);
    if (!mutexTaken && CliHas(L"--relaunch")) {
        // 更新交接：新进程被旧进程的锁挡住时等待退出（最多 15 秒），而不是直接自杀
        for (int i = 0; i < 150; i++) {
            ReleaseMutex(s_mutex);
            CloseHandle(s_mutex);
            Sleep(100);
            s_mutex = CreateMutexW(nullptr, TRUE, L"MonitorOSD_Netbar");
            if (GetLastError() != ERROR_ALREADY_EXISTS) { mutexTaken = true; break; }
        }
        if (!mutexTaken) {
            LogLine("更新: 交接超时（旧进程 15 秒未退出），放弃启动");
            return 0;
        }
        LogLine("更新: 交接完成，新版本接管");
    }
    // v1.5.3 P1-5：**会碰 DDC 总线**的 CLI 模式拿不到锁时必须等（不能像原来那样直接往下走）。
    // 计划任务 /restore 撞上用户手开的 GUI 时，两边同时发 DDC 命令会让显示器固件限流/假死。
    // /selftest 是纯离线自检、/uiupdate 是界面调试，都不碰 DDC，不参与锁竞争。
    bool needsBus = CliHas(L"/set") || CliHas(L"/restore") || CliHas(L"/scan") || CliHas(L"/detect") ||
                    CliHas(L"/tools") || CliHas(L"/test");
    if (!mutexTaken && needsBus) {
        LogLine("CLI: 已有实例在运行，等待其退出（最多 30 秒）");
        for (int i = 0; i < 300 && !mutexTaken; i++) {
            ReleaseMutex(s_mutex);
            CloseHandle(s_mutex);
            Sleep(100);
            s_mutex = CreateMutexW(nullptr, TRUE, L"MonitorOSD_Netbar");
            if (GetLastError() != ERROR_ALREADY_EXISTS) mutexTaken = true;
        }
        if (!mutexTaken) {
            LogLine("CLI: 等待超时（另一实例仍在运行），本次命令放弃执行");
            return 0;
        }
        LogLine("CLI: 已获得单实例锁，继续执行");
    }

    // 命令行模式（与 C# Program.Main 同序）：/set → /tools → /restore → /scan|/detect
    if (CliHas(L"/set"))    { CliSet(); return 0; }
    if (CliHas(L"/tools"))  { return RunToolsMode(hInst, mutexTaken); }
    if (CliHas(L"/test"))   { return RunTestMode(hInst, mutexTaken); }
    if (CliHas(L"/restore")){ CliRestore(); return 0; }
    if (CliHas(L"/checkupdate")) { CliCheckUpdate(); return 0; }
    if (CliHas(L"/selftest"))   { CliSelfTest(); return 0; }
    // 调试/截图用：/uiupdate 直接打开反馈窗口（不进主界面），便于自动化核对排版
    // （v1.6.12：窗口精简为单一反馈页，参数已废弃，保留开关本身兼容旧脚本）
    if (CliHas(L"/uiupdate")) {
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2f))) return 1;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_dwf))) return 1;
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        LoadFonts();
        OpenUpdateWindow();
        MSG m2;
        while (GetMessageW(&m2, nullptr, 0, 0)) { TranslateMessage(&m2); DispatchMessageW(&m2); }
        return 0;
    }
    if (CliHas(L"/feedback"))   { CliFeedback(); return 0; }
    if (CliHas(L"/scan") || CliHas(L"/detect")) {
        CliScanReport(CliHas(L"/scan"));
        return 0;
    }

    // 单实例：mutex 已在上面 CLI 分派之前创建（P1-5 修复），这里只处理"已有实例"的情况
    if (!mutexTaken) {
        // 二次启动 = 唤醒已运行实例：还原并前置窗口，秒出界面
        HWND prev = FindWindowW(L"MonitorOSDCpp", nullptr);
        if (prev) {
            ShowWindow(prev, IsIconic(prev) ? SW_RESTORE : SW_SHOW);
            SetForegroundWindow(prev);
        }
        return 0;
    }

    // 重初始化前置：窗口创建前完成（慢机器/杀软扫描下，窗口一出现即是加载动画，不白屏不假死）
    {
        if (FAILED(D2D1CreateFactory(D2D1_FACTORY_TYPE_SINGLE_THREADED, &g_d2f))) return 0;
        if (FAILED(DWriteCreateFactory(DWRITE_FACTORY_TYPE_SHARED, __uuidof(IDWriteFactory), (IUnknown**)&g_dwf))) return 0;
        CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                         IID_PPV_ARGS(&g_wic));
        LoadFonts();
    }
    WNDCLASSEXW wc = { sizeof(wc) };
    wc.style = CS_HREDRAW | CS_VREDRAW;
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hInst;
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, 0, 0, LR_DEFAULTSIZE);
    wc.hIconSm = (HICON)LoadImageW(hInst, MAKEINTRESOURCEW(1), IMAGE_ICON, SI(16), SI(16), 0);
    wc.hbrBackground = CreateSolidBrush(RGB(10, 10, 11));   // 首帧前显暗色，杜绝屏幕残留像素「乱码」
    wc.lpszClassName = L"MonitorOSDCpp";
    RegisterClassExW(&wc);
    int x = (wa.right - wa.left - w) / 2 + wa.left;
    int y = (wa.bottom - wa.top - hh) / 2 + wa.top;
    std::wstring wtitle = Utf8ToWide(AppDisplayName());
    HWND hw = CreateWindowExW(WS_EX_APPWINDOW, L"MonitorOSDCpp", wtitle.c_str(),
                    WS_POPUP, x, y, w, hh, nullptr, nullptr, hInst, nullptr);   // 不带 WS_VISIBLE：数据就绪后整窗一次出现
    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    // v1.6.2：主窗口已销毁，禁止后台线程再往失效 hwnd 投递（还原/轮询线程仍在跑）
    g_uiAlive.store(false);
    return 0;
}
