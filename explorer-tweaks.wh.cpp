// ==WindhawkMod==
// @id              explorer-tweaks
// @name            Explorer Tweaks
// @description     Windows 11 File Explorer'ı sadeleştirir: tab satırı, toolbar, pin ikonları ve sol menü düzeni
// @version         1.0.0
// @author          omer
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lcomctl32 -lole32 -loleaut32 -lshlwapi -lruntimeobject
// ==/WindhawkMod==

// ==WindhawkModReadme==
/*
# Explorer Tweaks

Windows 11 File Explorer'ı sadeleştiren, tek başına çalışan bir mod.

- Tab satırını gizler.
- Toolbar'ı (command bar) gizler.
- Quick Access'teki pinli öğelerin pin ikonlarını kaldırır.
- Sol menüyü (navigation pane) düzenler: sıralama, yeniden adlandırma,
  gizleme ve pinli klasörlerin yeri.

Tab, toolbar ve pin ikonu değişiklikleri yeni açılan pencerelerde etkili olur.
Sol menü sırası ve adları açık pencerelere hemen uygulanır. Gizleme için
Explorer'ı yeniden açmak gerekir.
*/
// ==/WindhawkModReadme==

// ==WindhawkModSettings==
/*
- hideTabs: true
  $name: Tabları gizle
- hideCommandBar: true
  $name: Toolbar'ı gizle
- hidePins: true
  $name: Quick Access pin ikonlarını kaldır
- pinnedPosition: bottom
  $name: Pinli klasörlerin yeri
  $options:
  - default: Windows varsayılanı
  - top: En üstte
  - bottom: En altta
- navItems:
  - - match: "This PC"
    - newName: ""
    - hidden: false
  - - match: "{3611C374-393E-41F8-98EB-1AD7BF051346}"
    - newName: "Phone"
    - hidden: false
  - - match: "Linux"
    - newName: ""
    - hidden: false
  $name: Sol menü öğeleri
  $description: Listedeki sıra menüdeki sırayı belirler. match görünen isim ya da {CLSID} olabilir.
*/
// ==/WindhawkModSettings==

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <algorithm>
#include <climits>
#include <cwchar>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

// ===== Logic =====
// Windhawk'tan bağımsız saf mantık. ET_LOGIC_ONLY tanımlıyken tek başına
// derlenir (tests/logic_test.cpp), bu yüzden Windhawk başlığı gerektirmez.

struct NavRule {
    std::wstring match;
    std::wstring newName;
    bool hidden = false;
};

enum class PinnedPosition { Default, Top, Bottom };

struct NavEntry {
    std::wstring name;
    bool pinned;
};

// Büyük/küçük harf duyarsız karşılaştırma.
inline bool NamesEqual(std::wstring_view a, std::wstring_view b) {
    return CompareStringOrdinal(a.data(), static_cast<int>(a.size()),
                                b.data(), static_cast<int>(b.size()),
                                TRUE) == CSTR_EQUAL;
}

// "{" ile başlayıp "}" ile biten 38 karakterlik metin.
inline bool IsClsidString(std::wstring_view s) {
    return s.size() == 38 && s.front() == L'{' && s.back() == L'}';
}

// İsimle eşleşen ilk kuralın indeksi, yoksa -1. effectiveMatch[i] kural i'nin
// eşleşme metnidir; boş olanlar (çözülemeyen CLSID, boş match) hiçbir isimle
// eşleşmez.
inline int FindRule(const std::vector<NavRule>& rules,
                    const std::vector<std::wstring>& effectiveMatch,
                    std::wstring_view name) {
    size_t count = std::min(rules.size(), effectiveMatch.size());
    for (size_t i = 0; i < count; i++) {
        if (!effectiveMatch[i].empty() && NamesEqual(effectiveMatch[i], name)) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

// entries için yeni sırayı orijinal indeksler cinsinden döner.
inline std::vector<int> ComputeOrder(
    const std::vector<NavEntry>& entries,
    const std::vector<NavRule>& rules,
    const std::vector<std::wstring>& effectiveMatch,
    PinnedPosition pos) {
    std::vector<int> pinned;
    std::vector<std::pair<int, int>> keyed;  // (kural indeksi, orijinal indeks)
    for (size_t i = 0; i < entries.size(); i++) {
        if (entries[i].pinned) {
            pinned.push_back(static_cast<int>(i));
        } else {
            int rule = FindRule(rules, effectiveMatch, entries[i].name);
            keyed.emplace_back(rule == -1 ? INT_MAX : rule,
                               static_cast<int>(i));
        }
    }
    std::stable_sort(keyed.begin(), keyed.end());

    std::vector<int> sorted;
    sorted.reserve(keyed.size());
    for (const auto& k : keyed) {
        sorted.push_back(k.second);
    }

    std::vector<int> result;
    result.reserve(entries.size());
    switch (pos) {
        case PinnedPosition::Bottom:
            result = sorted;
            result.insert(result.end(), pinned.begin(), pinned.end());
            break;
        case PinnedPosition::Top:
            result = pinned;
            result.insert(result.end(), sorted.begin(), sorted.end());
            break;
        case PinnedPosition::Default: {
            size_t next = 0;
            for (size_t i = 0; i < entries.size(); i++) {
                result.push_back(entries[i].pinned ? static_cast<int>(i)
                                                   : sorted[next++]);
            }
            break;
        }
    }
    return result;
}

// ===== End Logic =====

#ifndef ET_LOGIC_ONLY

#include <commctrl.h>
#include <windhawk_utils.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <unordered_set>

// ===== Settings =====

struct Settings {
    bool hideTabs = true;
    bool hideCommandBar = true;
    bool hidePins = true;
    PinnedPosition pinnedPosition = PinnedPosition::Bottom;
    std::vector<NavRule> navItems;
};

// Ayarların değişmez anlık görüntüsü. Wh_ModSettingsChanged Explorer'ın UI
// thread'lerinden farklı bir thread'de çalışır; okuyucular GetConfig() ile
// kendi kopyalarını alır, yazıcı yeni bir Config kurup işaretçiyi değiştirir.
struct Config {
    Settings settings;
    // effectiveMatch[i]: navItems[i]'nin eşleşme metni. CLSID kuralları için
    // boş (Task 4 çözülen görünen adı yazar).
    std::vector<std::wstring> effectiveMatch;
};

std::mutex g_configMutex;
std::shared_ptr<const Config> g_config = std::make_shared<const Config>();

std::shared_ptr<const Config> GetConfig() {
    std::lock_guard<std::mutex> lock(g_configMutex);
    return g_config;
}

static std::wstring ReadStringSetting(const wchar_t* fmt, int index) {
    PCWSTR value = Wh_GetStringSetting(fmt, index);
    std::wstring result = value ? value : L"";
    Wh_FreeStringSetting(value);
    return result;
}

Settings LoadSettings() {
    Settings s;
    s.hideTabs = Wh_GetIntSetting(L"hideTabs") != 0;
    s.hideCommandBar = Wh_GetIntSetting(L"hideCommandBar") != 0;
    s.hidePins = Wh_GetIntSetting(L"hidePins") != 0;

    PCWSTR pos = Wh_GetStringSetting(L"pinnedPosition");
    if (pos && wcscmp(pos, L"top") == 0) {
        s.pinnedPosition = PinnedPosition::Top;
    } else if (pos && wcscmp(pos, L"default") == 0) {
        s.pinnedPosition = PinnedPosition::Default;
    } else {
        s.pinnedPosition = PinnedPosition::Bottom;
    }
    Wh_FreeStringSetting(pos);

    // Liste, match ve newName ikisi birden boş olan ilk indekste biter.
    for (int i = 0;; i++) {
        NavRule rule;
        rule.match = ReadStringSetting(L"navItems[%d].match", i);
        rule.newName = ReadStringSetting(L"navItems[%d].newName", i);
        if (rule.match.empty() && rule.newName.empty()) {
            break;
        }
        rule.hidden = Wh_GetIntSetting(L"navItems[%d].hidden", i) != 0;
        s.navItems.push_back(std::move(rule));
    }

    return s;
}

// Her kuralın eşleşme metnini çözer. İsim kuralları olduğu gibi kullanılır;
// CLSID kuralları şimdilik boş kalır (hiçbir öğeyle eşleşmez).
std::vector<std::wstring> ResolveRules(const Settings& s) {
    std::vector<std::wstring> effective;
    effective.reserve(s.navItems.size());
    for (const NavRule& rule : s.navItems) {
        effective.push_back(IsClsidString(rule.match) ? std::wstring()
                                                      : rule.match);
    }
    return effective;
}

// Ayarları okur, kuralları çözer ve yeni Config'i yayınlar.
void ReloadConfig() {
    auto config = std::make_shared<Config>();
    config->settings = LoadSettings();
    config->effectiveMatch = ResolveRules(config->settings);

    std::shared_ptr<const Config> old;
    {
        std::lock_guard<std::mutex> lock(g_configMutex);
        old = std::exchange(g_config, std::move(config));
    }
    // Eski Config (son okuyucu bırakmadıysa) kilidin dışında serbest kalır.
}

// ===== Run on window thread =====

UINT g_runMsg = RegisterWindowMessageW(L"ExplorerTweaks_Run");

constexpr UINT kRunTimeoutMs = 2000;

enum RunState : int { kRunPending, kRunRunning, kRunDone, kRunAbandoned };

struct RunRequest {
    std::function<void()> fn;
    std::atomic<int> state{kRunPending};
};

// Hedef thread'de, g_runMsg pencere prosedürüne ulaşmadan önce çalışır.
LRESULT CALLBACK RunCallWndProc(int nCode, WPARAM wParam, LPARAM lParam) {
    if (nCode == HC_ACTION && g_runMsg) {
        const auto* cwp = reinterpret_cast<const CWPSTRUCT*>(lParam);
        if (cwp->message == g_runMsg && cwp->lParam) {
            auto* req = reinterpret_cast<RunRequest*>(cwp->lParam);
            // Aynı thread'de birden fazla hook kurulu olabilir (eşzamanlı iki
            // çağıran); her istek yalnızca bir kez çalışır.
            int expected = kRunPending;
            if (req->state.compare_exchange_strong(expected, kRunRunning)) {
                req->fn();
                req->state.store(kRunDone);
            }
        }
    }
    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

// fn'i hwnd'nin thread'inde senkron olarak çalıştırır. Çağıran hiçbir kilit
// tutmamalıdır: fn hedef thread'de aynı kilitleri alabilir.
void RunOnWindowThread(HWND hwnd, std::function<void()> fn) {
    DWORD threadId = GetWindowThreadProcessId(hwnd, nullptr);
    if (!threadId) {
        Wh_Log(L"RunOnWindowThread: invalid window %p", hwnd);
        return;
    }
    if (threadId == GetCurrentThreadId()) {
        fn();
        return;
    }
    if (!g_runMsg) {
        Wh_Log(L"RunOnWindowThread: message not registered");
        return;
    }

    HHOOK hook = SetWindowsHookExW(WH_CALLWNDPROC, RunCallWndProc, nullptr,
                                   threadId);
    if (!hook) {
        Wh_Log(L"RunOnWindowThread: SetWindowsHookExW failed (%u)",
               GetLastError());
        return;
    }

    auto* req = new RunRequest;
    req->fn = std::move(fn);

    DWORD_PTR result = 0;
    LRESULT sent = SendMessageTimeoutW(
        hwnd, g_runMsg, 0, reinterpret_cast<LPARAM>(req),
        SMTO_NORMAL | SMTO_ABORTIFHUNG, kRunTimeoutMs, &result);
    DWORD sendError = sent ? 0 : GetLastError();
    UnhookWindowsHookEx(hook);

    int expected = kRunPending;
    if (req->state.compare_exchange_strong(expected, kRunAbandoned)) {
        if (sent) {
            Wh_Log(L"RunOnWindowThread: hook did not run for %p", hwnd);
            delete req;
        } else {
            // Mesaj hedef kuyrukta kalmış olabilir ve ileride işlenirse req'e
            // dokunur. Abandoned durumu fn'in çalışmasını engeller; req
            // bilerek serbest bırakılmaz (nadir, küçük sızıntı).
            Wh_Log(L"RunOnWindowThread: SendMessageTimeout failed for %p (%u)",
                   hwnd, sendError);
        }
        return;
    }

    // fn çalıştı ya da (zaman aşımına denk geldiyse) hâlâ çalışıyor. fn
    // çağıranın yığınına referans tutabileceği için bitmesini bekle.
    while (req->state.load() != kRunDone) {
        Sleep(1);
    }
    delete req;
}

// ===== Nav tree =====

constexpr UINT_PTR kTreeId = 0xE7;  // subclass ve zamanlayıcı kimliği
constexpr UINT kResortDelayMs = 50;

std::mutex g_treesMutex;
std::unordered_set<HWND> g_trees;

// Kendi TVM_SORTCHILDRENCB çağrımız sürerken true. Her Explorer penceresinin
// kendi UI thread'i olduğu için thread başınadır.
thread_local bool g_sorting = false;

bool HasClassName(HWND hwnd, const wchar_t* name) {
    if (!hwnd) {
        return false;
    }
    wchar_t buf[64];
    int len = GetClassNameW(hwnd, buf, ARRAYSIZE(buf));
    return len > 0 && _wcsicmp(buf, name) == 0;
}

bool IsNavTree(HWND hwnd, HWND parent) {
    return HasClassName(hwnd, WC_TREEVIEWW) &&
           HasClassName(parent, L"NamespaceTreeControl");
}

HTREEITEM GetRootItem(HWND tree) {
    return reinterpret_cast<HTREEITEM>(
        SendMessageW(tree, TVM_GETNEXTITEM, TVGN_ROOT, 0));
}

// item, gizli kökün (ilk kök öğe) doğrudan çocuğu mu?
bool IsRootChild(HWND tree, HTREEITEM item) {
    if (!item || item == TVI_ROOT) {
        return false;
    }
    HTREEITEM root = GetRootItem(tree);
    if (!root || item == root) {
        return false;
    }
    HTREEITEM parent = reinterpret_cast<HTREEITEM>(SendMessageW(
        tree, TVM_GETNEXTITEM, TVGN_PARENT, reinterpret_cast<LPARAM>(item)));
    return parent == root;
}

void ArmResort(HWND tree) {
    // Aynı kimlikle SetTimer zamanlayıcıyı yeniden başlatır (debounce).
    SetTimer(tree, kTreeId, kResortDelayMs, nullptr);
}

using TargetMap = std::unordered_map<LPARAM, int>;

int CALLBACK CompareByTarget(LPARAM a, LPARAM b, LPARAM context) {
    const auto& map = *reinterpret_cast<const TargetMap*>(context);
    auto ia = map.find(a);
    auto ib = map.find(b);
    int ka = ia == map.end() ? INT_MAX : ia->second;
    int kb = ib == map.end() ? INT_MAX : ib->second;
    return ka < kb ? -1 : (ka > kb ? 1 : 0);
}

// ResortTree süresince g_sorting'i true tutar, çıkışta eski değere döner.
struct SortingGuard {
    bool previous = g_sorting;
    SortingGuard() { g_sorting = true; }
    ~SortingGuard() { g_sorting = previous; }
    SortingGuard(const SortingGuard&) = delete;
    SortingGuard& operator=(const SortingGuard&) = delete;
};

// Kökün çocuklarını ayarlara göre yeniden sıralar. Ağacın thread'inde
// çağrılmalıdır. g_sorting yalnızca TVM_SORTCHILDRENCB değil tüm işlem boyunca
// tutulur: callback metinli öğelerde TVM_GETITEMW ebeveyne TVN_GETDISPINFOW
// gönderir ve ebeveynin buna tepkisi (ör. TVM_SETITEMW) zamanlayıcıyı yeniden
// kurup sonsuz bir döngü başlatmamalıdır.
void ResortTree(HWND tree) {
    SortingGuard guard;

    HTREEITEM root = GetRootItem(tree);
    if (!root) {
        return;
    }

    std::vector<NavEntry> entries;
    std::vector<LPARAM> params;
    for (HTREEITEM item = TreeView_GetChild(tree, root); item;
         item = TreeView_GetNextSibling(tree, item)) {
        wchar_t text[MAX_PATH] = L"";
        TVITEMW tvi{};
        tvi.mask = TVIF_HANDLE | TVIF_TEXT | TVIF_STATE | TVIF_PARAM;
        tvi.hItem = item;
        tvi.stateMask = TVIS_STATEIMAGEMASK;
        tvi.pszText = text;
        tvi.cchTextMax = ARRAYSIZE(text);
        if (!SendMessageW(tree, TVM_GETITEMW, 0,
                          reinterpret_cast<LPARAM>(&tvi))) {
            Wh_Log(L"ResortTree: TVM_GETITEMW failed, skipping");
            return;
        }
        bool pinned = ((tvi.state & TVIS_STATEIMAGEMASK) >> 12) == 1;
        entries.push_back({tvi.pszText ? tvi.pszText : L"", pinned});
        params.push_back(tvi.lParam);
    }

    auto config = GetConfig();
    std::vector<int> order =
        ComputeOrder(entries, config->settings.navItems,
                     config->effectiveMatch, config->settings.pinnedPosition);

    bool identity = true;
    for (size_t i = 0; i < order.size(); i++) {
        if (order[i] != static_cast<int>(i)) {
            identity = false;
            break;
        }
    }
    if (identity) {
        return;
    }

    TargetMap target;
    for (size_t i = 0; i < order.size(); i++) {
        target[params[order[i]]] = static_cast<int>(i);
    }
    if (target.size() != params.size()) {
        Wh_Log(L"ResortTree: duplicate item lParam, skipping");
        return;
    }

    TVSORTCB sort{};
    sort.hParent = root;
    sort.lpfnCompare = CompareByTarget;
    sort.lParam = reinterpret_cast<LPARAM>(&target);
    SendMessageW(tree, TVM_SORTCHILDRENCB, 0, reinterpret_cast<LPARAM>(&sort));
}

LRESULT CALLBACK TreeSubclassProc(HWND hwnd,
                                  UINT msg,
                                  WPARAM wParam,
                                  LPARAM lParam,
                                  UINT_PTR idSubclass,
                                  DWORD_PTR) {
    switch (msg) {
        case TVM_INSERTITEMW: {
            if (g_sorting || !lParam) {
                break;
            }
            // Kök henüz yoksa bu ekleme kökün kendisidir (hParent = TVI_ROOT);
            // sıralanacak çocuk yoktur.
            HTREEITEM root = GetRootItem(hwnd);
            HTREEITEM parent =
                reinterpret_cast<const TVINSERTSTRUCTW*>(lParam)->hParent;
            bool arm = root && parent == root;
            LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
            if (arm && result) {
                ArmResort(hwnd);
            }
            return result;
        }

        case TVM_DELETEITEM: {
            if (g_sorting) {
                break;
            }
            // Kontrol silmeden önce yapılmalı; sonra öğe geçersizdir.
            bool arm = IsRootChild(hwnd, reinterpret_cast<HTREEITEM>(lParam));
            LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
            if (arm && result) {
                ArmResort(hwnd);
            }
            return result;
        }

        case TVM_SETITEMW: {
            if (g_sorting || !lParam) {
                break;
            }
            const auto* tvi = reinterpret_cast<const TVITEMW*>(lParam);
            // Yalnızca pin (state image) ya da metin değişiklikleri sırayı
            // etkiler; hover/seçim durum değişiklikleri yok sayılır.
            bool pinChange = (tvi->mask & TVIF_STATE) &&
                             (tvi->stateMask & TVIS_STATEIMAGEMASK);
            bool textChange = (tvi->mask & TVIF_TEXT) != 0;
            bool arm =
                (pinChange || textChange) && IsRootChild(hwnd, tvi->hItem);
            LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
            if (arm && result) {
                ArmResort(hwnd);
            }
            return result;
        }

        case WM_TIMER:
            if (wParam == kTreeId) {
                KillTimer(hwnd, kTreeId);
                ResortTree(hwnd);
                return 0;
            }
            break;

        case WM_NCDESTROY: {
            KillTimer(hwnd, kTreeId);
            RemoveWindowSubclass(hwnd, TreeSubclassProc, idSubclass);
            std::lock_guard<std::mutex> lock(g_treesMutex);
            g_trees.erase(hwnd);
            break;
        }
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// Ağacın thread'inde çağrılmalıdır. Tekrar çağrılması zararsızdır.
void AttachTree(HWND tree) {
    if (!SetWindowSubclass(tree, TreeSubclassProc, kTreeId, 0)) {
        Wh_Log(L"AttachTree: SetWindowSubclass failed for %p", tree);
        return;
    }
    std::lock_guard<std::mutex> lock(g_treesMutex);
    g_trees.insert(tree);
}

BOOL CALLBACK CollectNavTreesProc(HWND hwnd, LPARAM lParam) {
    if (IsNavTree(hwnd, GetAncestor(hwnd, GA_PARENT))) {
        reinterpret_cast<std::vector<HWND>*>(lParam)->push_back(hwnd);
    }
    return TRUE;
}

BOOL CALLBACK CollectCabinetsProc(HWND hwnd, LPARAM lParam) {
    DWORD pid = 0;
    GetWindowThreadProcessId(hwnd, &pid);
    if (pid == GetCurrentProcessId() && HasClassName(hwnd, L"CabinetWClass")) {
        EnumChildWindows(hwnd, CollectNavTreesProc, lParam);
    }
    return TRUE;
}

// Bu süreçteki açık Explorer pencerelerinin nav ağaçlarında fn'i çağırır.
// Ağaçlar önce toplanır; fn hiçbir kilit tutulmadan çağrılır.
void ForEachNavTree(void (*fn)(HWND)) {
    std::vector<HWND> trees;
    EnumWindows(CollectCabinetsProc, reinterpret_cast<LPARAM>(&trees));
    for (HWND tree : trees) {
        fn(tree);
    }
}

// ===== Hooks =====

using CreateWindowExW_t = decltype(&CreateWindowExW);
CreateWindowExW_t CreateWindowExW_Original;

HWND WINAPI CreateWindowExW_Hook(DWORD dwExStyle,
                                 LPCWSTR lpClassName,
                                 LPCWSTR lpWindowName,
                                 DWORD dwStyle,
                                 int X,
                                 int Y,
                                 int nWidth,
                                 int nHeight,
                                 HWND hWndParent,
                                 HMENU hMenu,
                                 HINSTANCE hInstance,
                                 LPVOID lpParam) {
    HWND hwnd = CreateWindowExW_Original(dwExStyle, lpClassName, lpWindowName,
                                         dwStyle, X, Y, nWidth, nHeight,
                                         hWndParent, hMenu, hInstance, lpParam);
    // Pencereyi oluşturan thread'deyiz, doğrudan subclass edilebilir.
    if (hwnd && hWndParent && (dwStyle & WS_CHILD) &&
        IsNavTree(hwnd, hWndParent)) {
        AttachTree(hwnd);
    }
    return hwnd;
}

// ===== Mod lifecycle =====

BOOL Wh_ModInit() {
    ReloadConfig();
    if (!WindhawkUtils::SetFunctionHook(CreateWindowExW, CreateWindowExW_Hook,
                                        &CreateWindowExW_Original)) {
        Wh_Log(L"CreateWindowExW hook failed; only existing windows sorted");
    }
    return TRUE;
}

// Hook'lar Wh_ModInit'ten sonra uygulanır; mevcut pencereler burada taranırsa
// arada açılan bir pencere kaçmaz (iki kez bağlanması zararsızdır).
void Wh_ModAfterInit() {
    ForEachNavTree([](HWND tree) {
        RunOnWindowThread(tree, [tree] {
            AttachTree(tree);
            ResortTree(tree);
        });
    });
}

void Wh_ModUninit() {
}

void Wh_ModSettingsChanged() {
    ReloadConfig();
    ForEachNavTree([](HWND tree) {
        RunOnWindowThread(tree, [tree] { ResortTree(tree); });
    });
}

#endif  // ET_LOGIC_ONLY
