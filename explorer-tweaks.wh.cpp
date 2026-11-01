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

// Orijinal adı original olan öğenin yeni adı; yeniden adlandırılmıyorsa boş.
// Dönen görünüm rules'un ömrüne bağlıdır.
inline std::wstring_view NewNameFor(const std::vector<NavRule>& rules,
                                    const std::vector<std::wstring>& effectiveMatch,
                                    std::wstring_view original) {
    int i = FindRule(rules, effectiveMatch, original);
    return i == -1 ? std::wstring_view() : std::wstring_view(rules[i].newName);
}

// Orijinal adı original olan öğede gösterilmesi gereken metin.
inline std::wstring_view DisplayNameFor(
    const std::vector<NavRule>& rules,
    const std::vector<std::wstring>& effectiveMatch,
    std::wstring_view original) {
    std::wstring_view newName = NewNameFor(rules, effectiveMatch, original);
    return newName.empty() ? original : newName;
}

struct NameUpdate {
    std::wstring original;  // öğenin bu mesajdan sonraki orijinal adı
    std::wstring display;   // ağaca yazılacak metin
};

// Bir öğeye (ekleme ya da güncelleme ile) gelen metni yorumlar. recorded,
// öğe için daha önce kaydedilmiş orijinal addır (yoksa nullptr). Gelen metin
// zaten bizim gösterdiğimiz yeni adsa (Explorer metni okuyup geri yazmış)
// orijinal korunur; aksi halde gelen metin yeni orijinaldir. Eşleştirme hiçbir
// zaman yeni ad üzerinden yapılmaz.
inline NameUpdate ResolveIncomingText(
    const std::vector<NavRule>& rules,
    const std::vector<std::wstring>& effectiveMatch,
    const std::wstring* recorded,
    std::wstring_view incoming) {
    if (recorded) {
        std::wstring_view current = NewNameFor(rules, effectiveMatch, *recorded);
        if (!current.empty() && current == incoming) {
            return {*recorded, std::wstring(incoming)};
        }
    }
    return {std::wstring(incoming),
            std::wstring(DisplayNameFor(rules, effectiveMatch, incoming))};
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
#include <shlobj.h>
#include <windhawk_utils.h>

#include <atomic>
#include <cstring>
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
    // çözülen görünen ad; çözülemezse boş.
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

// "shell:::{CLSID}" öğesinin o anki görünen adı. COM başlatılmış bir thread'de
// çağrılmalıdır. Hata olursa boş döner.
std::wstring ResolveClsidDisplayName(std::wstring_view clsid) {
    std::wstring path = L"shell:::";
    path.append(clsid);

    PIDLIST_ABSOLUTE pidl = nullptr;
    HRESULT hr = SHParseDisplayName(path.c_str(), nullptr, &pidl, 0, nullptr);
    if (FAILED(hr) || !pidl) {
        Wh_Log(L"ResolveClsidDisplayName: SHParseDisplayName(%s) failed "
               L"(0x%08X)",
               path.c_str(), static_cast<unsigned>(hr));
        return L"";
    }

    PWSTR name = nullptr;
    hr = SHGetNameFromIDList(pidl, SIGDN_NORMALDISPLAY, &name);
    CoTaskMemFree(pidl);
    if (FAILED(hr) || !name) {
        Wh_Log(L"ResolveClsidDisplayName: SHGetNameFromIDList(%s) failed "
               L"(0x%08X)",
               path.c_str(), static_cast<unsigned>(hr));
        CoTaskMemFree(name);
        return L"";
    }

    std::wstring result = name;
    CoTaskMemFree(name);
    return result;
}

// Her kuralın eşleşme metnini çözer. İsim kuralları olduğu gibi kullanılır;
// CLSID kuralları görünen adlarına çözülür (çözülemezse boş kalır ve hiçbir
// öğeyle eşleşmez).
std::vector<std::wstring> ResolveRules(const Settings& s) {
    std::vector<std::wstring> effective;
    effective.reserve(s.navItems.size());
    for (const NavRule& rule : s.navItems) {
        effective.push_back(IsClsidString(rule.match)
                                ? ResolveClsidDisplayName(rule.match)
                                : rule.match);
    }
    return effective;
}

// Bulunulan thread'de COM'u kapsam boyunca başlatır. Thread zaten başka bir
// modda başlatılmışsa (RPC_E_CHANGED_MODE) dokunmaz.
struct ComScope {
    HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    ComScope() = default;
    ~ComScope() {
        if (SUCCEEDED(hr)) {  // S_OK ya da S_FALSE
            CoUninitialize();
        }
    }
    ComScope(const ComScope&) = delete;
    ComScope& operator=(const ComScope&) = delete;
};

// Ayarları okur, kuralları çözer ve yeni Config'i yayınlar. Yalnızca
// Wh_ModInit ve Wh_ModSettingsChanged thread'lerinden çağrılır; CLSID çözümü
// için COM burada başlatılır.
void ReloadConfig() {
    ComScope com;
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
    DWORD pid = 0;
    DWORD threadId = GetWindowThreadProcessId(hwnd, &pid);
    if (!threadId) {
        Wh_Log(L"RunOnWindowThread: invalid window %p", hwnd);
        return;
    }
    // Tutamaç yok edilip başka bir süreçte yeniden kullanılmış olabilir; hook
    // o sürece kurulmamalı.
    if (pid != GetCurrentProcessId()) {
        Wh_Log(L"RunOnWindowThread: window %p belongs to another process",
               hwnd);
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

constexpr UINT_PTR kTreeId = 0xE7;    // ağaç subclass ve zamanlayıcı kimliği
constexpr UINT_PTR kParentId = 0xE8;  // NamespaceTreeControl subclass kimliği
constexpr UINT kResortDelayMs = 50;

std::mutex g_treesMutex;
std::unordered_set<HWND> g_trees;

// Kendi TVM_SORTCHILDRENCB / yeniden adlandırma çağrılarımız sürerken true.
// Her Explorer penceresinin kendi UI thread'i olduğu için thread başınadır.
thread_local bool g_sorting = false;

// Ağaç başına durum. Ağaç subclass'ının dwRefData'sında tutulur; yalnızca
// ağacın kendi thread'inde okunur ve yazılır, bu yüzden kilit gerekmez.
struct TreeState {
    // Yeniden adlandırılmış kök çocuklarının orijinal adları (g_originalNames).
    // Yalnızca yeniden adlandırılan öğeler için kayıt vardır.
    std::unordered_map<HTREEITEM, std::wstring> originalNames;
    // TVN_GETDISPINFOW cevabında pszText'in gösterdiği tampon. Ağaç metni
    // bildirim döner dönmez kullanır; bir sonraki bildirime kadar geçerlidir.
    std::wstring dispText;
};

LRESULT CALLBACK TreeSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR,
                                  DWORD_PTR);

// Bağlıysa ağacın durumu, değilse nullptr. Durum yalnızca ağacın kendi
// thread'inde kullanılabileceği için başka bir thread'den nullptr döner.
TreeState* GetTreeState(HWND tree) {
    DWORD_PTR ref = 0;
    if (!tree ||
        GetWindowThreadProcessId(tree, nullptr) != GetCurrentThreadId() ||
        !GetWindowSubclass(tree, TreeSubclassProc, kTreeId, &ref)) {
        return nullptr;
    }
    return reinterpret_cast<TreeState*>(ref);
}

// Öğenin eşleştirmede kullanılacak adı: yeniden adlandırılmışsa orijinali,
// değilse gösterilen metin.
std::wstring_view OriginalName(const TreeState* state,
                               HTREEITEM item,
                               std::wstring_view shown) {
    if (state) {
        auto it = state->originalNames.find(item);
        if (it != state->originalNames.end()) {
            return it->second;
        }
    }
    return shown;
}

// Metin geçerliyse onu, null ya da LPSTR_TEXTCALLBACKW ise boş görünüm döner.
std::wstring_view TextOf(LPCWSTR text) {
    if (!text || text == LPSTR_TEXTCALLBACKW) {
        return {};
    }
    return text;
}

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
    const TreeState* state = GetTreeState(tree);

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
        // Sıralama her zaman orijinal ada göre yapılır.
        entries.push_back(
            {std::wstring(OriginalName(state, item, TextOf(tvi.pszText))),
             pinned});
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

// Kökün çocuklarının metnini geçerli kurallara göre yeni ada ya da (kural
// kaldırıldıysa / newName boşaldıysa) orijinal ada çevirir. Ağacın thread'inde
// çağrılmalıdır. Kendi TVM_SETITEMW çağrılarımız zamanlayıcıyı kurmasın diye
// SortingGuard altında çalışır.
void ReapplyNames(HWND tree) {
    TreeState* state = GetTreeState(tree);
    if (!state) {
        return;
    }
    SortingGuard guard;

    HTREEITEM root = GetRootItem(tree);
    if (!root) {
        return;
    }

    auto config = GetConfig();
    const auto& rules = config->settings.navItems;
    const auto& match = config->effectiveMatch;
    for (HTREEITEM item = TreeView_GetChild(tree, root); item;
         item = TreeView_GetNextSibling(tree, item)) {
        // Callback metinli öğelerde bu çağrı TVN_GETDISPINFOW üretir; ebeveyn
        // subclass'ı metni zaten geçerli kurala göre verir ve kaydı günceller,
        // bu yüzden aşağıda shown == desired olur ve öğe callback kalır.
        wchar_t text[MAX_PATH] = L"";
        TVITEMW tvi{};
        tvi.mask = TVIF_HANDLE | TVIF_TEXT;
        tvi.hItem = item;
        tvi.pszText = text;
        tvi.cchTextMax = ARRAYSIZE(text);
        if (!SendMessageW(tree, TVM_GETITEMW, 0,
                          reinterpret_cast<LPARAM>(&tvi))) {
            continue;
        }
        std::wstring shown(TextOf(tvi.pszText));
        std::wstring original(OriginalName(state, item, shown));
        std::wstring desired(DisplayNameFor(rules, match, original));

        // Kayıt, TVM_SETITEMW'den önce güncellenir: subclass gelen metni bu
        // kayda göre yorumlar ve hiçbir şeyi değiştirmeden iletir.
        if (desired != original) {
            state->originalNames[item] = original;
        } else {
            state->originalNames.erase(item);
        }
        if (shown == desired) {
            continue;
        }

        TVITEMW set{};
        set.mask = TVIF_HANDLE | TVIF_TEXT;
        set.hItem = item;
        set.pszText = desired.data();
        if (!SendMessageW(tree, TVM_SETITEMW, 0,
                          reinterpret_cast<LPARAM>(&set))) {
            Wh_Log(L"ReapplyNames: TVM_SETITEMW failed");
        }
    }
    // Callback metinli öğeler yeniden çizildiğinde yeni adı alır.
    InvalidateRect(tree, nullptr, FALSE);
}

LRESULT OnInsertItem(HWND tree, TreeState* state, WPARAM wParam,
                     LPARAM lParam) {
    const auto* tvis = reinterpret_cast<const TVINSERTSTRUCTW*>(lParam);
    // Kök henüz yoksa bu ekleme kökün kendisidir (hParent = TVI_ROOT);
    // sıralanacak ya da yeniden adlandırılacak çocuk değildir.
    HTREEITEM root = GetRootItem(tree);
    bool rootChild = root && tvis->hParent == root;

    // Kural yeni ad veriyorsa metni yeni adla değiştirilmiş yerel bir kopya
    // iletilir. Ağaç metni ekleme sırasında kopyalar; display bu çağrı
    // boyunca yaşar.
    TVINSERTSTRUCTW copy{};
    std::wstring original;
    std::wstring display;
    LPARAM forwarded = lParam;
    const TVITEMW& item = tvis->item;
    std::wstring_view text =
        (item.mask & TVIF_TEXT) ? TextOf(item.pszText) : std::wstring_view();
    if (rootChild && state && !text.empty()) {
        auto config = GetConfig();
        std::wstring_view newName = NewNameFor(
            config->settings.navItems, config->effectiveMatch, text);
        if (!newName.empty()) {
            original = text;
            display = newName;
            copy = *tvis;
            copy.item.pszText = display.data();
            forwarded = reinterpret_cast<LPARAM>(&copy);
        }
    }

    // Kendi yeniden yazmamız da SortingGuard altında iletilir (R8).
    LRESULT result;
    if (forwarded != lParam) {
        SortingGuard guard;
        result = DefSubclassProc(tree, TVM_INSERTITEMW, wParam, forwarded);
    } else {
        result = DefSubclassProc(tree, TVM_INSERTITEMW, wParam, lParam);
    }

    auto inserted = reinterpret_cast<HTREEITEM>(result);
    if (inserted && state) {
        // Yeni öğe silinmiş bir öğenin tutamacını yeniden kullanıyor olabilir;
        // eski kayıt her durumda üzerine yazılır ya da silinir.
        if (forwarded != lParam) {
            state->originalNames[inserted] = std::move(original);
        } else {
            state->originalNames.erase(inserted);
        }
    }
    if (rootChild && inserted && !g_sorting) {
        ArmResort(tree);
    }
    return result;
}

LRESULT OnDeleteItem(HWND tree, TreeState* state, WPARAM wParam,
                     LPARAM lParam) {
    auto item = reinterpret_cast<HTREEITEM>(lParam);
    // Kontroller silmeden önce yapılmalı; sonra öğe geçersizdir. TVI_ROOT ya
    // da NULL tüm öğeleri, kök ise tüm kök çocuklarını siler.
    HTREEITEM root = GetRootItem(tree);
    bool all = !item || item == TVI_ROOT || item == root;
    bool rootChild = !all && IsRootChild(tree, item);

    LRESULT result = DefSubclassProc(tree, TVM_DELETEITEM, wParam, lParam);
    if (result && state) {
        if (all) {
            state->originalNames.clear();
        } else {
            state->originalNames.erase(item);
        }
    }
    if (rootChild && result && !g_sorting) {
        ArmResort(tree);
    }
    return result;
}

LRESULT OnSetItem(HWND tree, TreeState* state, WPARAM wParam, LPARAM lParam) {
    const auto* tvi = reinterpret_cast<const TVITEMW*>(lParam);
    // Yalnızca pin (state image) ya da metin değişiklikleri sırayı etkiler;
    // hover/seçim durum değişiklikleri yok sayılır.
    bool pinChange =
        (tvi->mask & TVIF_STATE) && (tvi->stateMask & TVIS_STATEIMAGEMASK);
    bool textChange = (tvi->mask & TVIF_TEXT) != 0;
    bool rootChild = (pinChange || textChange) && IsRootChild(tree, tvi->hItem);
    bool rename = rootChild && textChange && state;

    // Metin bizim gösterdiğimiz yeni adsa olduğu gibi iletilir; yeni bir
    // orijinalse ve kural yeni ad veriyorsa metni değiştirilmiş yerel bir kopya
    // iletilir. update.display bu çağrı boyunca yaşar.
    TVITEMEXW copy{};
    NameUpdate update;
    bool tracked = false;
    LPARAM forwarded = lParam;
    std::wstring_view text = TextOf(tvi->pszText);
    if (rename && !text.empty()) {
        auto config = GetConfig();
        auto it = state->originalNames.find(tvi->hItem);
        update = ResolveIncomingText(
            config->settings.navItems, config->effectiveMatch,
            it == state->originalNames.end() ? nullptr : &it->second, text);
        tracked = true;
        if (update.display != text) {
            // Uzantılı alanlar yalnızca maskede istenirse okunur; çağıran
            // TVITEMW geçmiş olabileceği için yalnızca o kadarı kopyalanır.
            bool ex = (tvi->mask &
                       (TVIF_INTEGRAL | TVIF_STATEEX | TVIF_EXPANDEDIMAGE)) != 0;
            std::memcpy(&copy, tvi, ex ? sizeof(TVITEMEXW) : sizeof(TVITEMW));
            copy.pszText = update.display.data();
            forwarded = reinterpret_cast<LPARAM>(&copy);
        }
    }

    // Kendi yeniden yazmamız da SortingGuard altında iletilir (R8).
    LRESULT result;
    if (forwarded != lParam) {
        SortingGuard guard;
        result = DefSubclassProc(tree, TVM_SETITEMW, wParam, forwarded);
    } else {
        result = DefSubclassProc(tree, TVM_SETITEMW, wParam, lParam);
    }

    if (result && rename) {
        // Callback ya da boş metinde kayıt silinir; callback metin ebeveyn
        // subclass'ında yeniden kaydedilir.
        if (tracked && update.display != update.original) {
            state->originalNames[tvi->hItem] = std::move(update.original);
        } else {
            state->originalNames.erase(tvi->hItem);
        }
    }
    if (rootChild && result && !g_sorting) {
        ArmResort(tree);
    }
    return result;
}

LRESULT CALLBACK ParentSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR,
                                    DWORD_PTR);

// Ağacın subclass'larını kaldırır, zamanlayıcısını öldürür ve durumunu serbest
// bırakır. Ağacın thread'inde çağrılmalıdır.
void DetachTree(HWND tree) {
    KillTimer(tree, kTreeId);
    TreeState* state = GetTreeState(tree);
    RemoveWindowSubclass(tree, TreeSubclassProc, kTreeId);
    if (HWND parent = GetAncestor(tree, GA_PARENT)) {
        RemoveWindowSubclass(parent, ParentSubclassProc, kParentId);
    }
    delete state;
    std::lock_guard<std::mutex> lock(g_treesMutex);
    g_trees.erase(tree);
}

LRESULT CALLBACK TreeSubclassProc(HWND hwnd,
                                  UINT msg,
                                  WPARAM wParam,
                                  LPARAM lParam,
                                  UINT_PTR,
                                  DWORD_PTR refData) {
    auto* state = reinterpret_cast<TreeState*>(refData);
    switch (msg) {
        case TVM_INSERTITEMW:
            if (lParam) {
                return OnInsertItem(hwnd, state, wParam, lParam);
            }
            break;

        case TVM_DELETEITEM:
            return OnDeleteItem(hwnd, state, wParam, lParam);

        case TVM_SETITEMW:
            if (lParam) {
                return OnSetItem(hwnd, state, wParam, lParam);
            }
            break;

        case WM_TIMER:
            if (wParam == kTreeId) {
                KillTimer(hwnd, kTreeId);
                ResortTree(hwnd);
                return 0;
            }
            break;

        case WM_NCDESTROY:
            DetachTree(hwnd);
            break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// Callback metinli bir kök çocuğunun TVN_GETDISPINFOW cevabını kurala göre
// yeni adla değiştirir ve orijinal adı kaydeder.
void FixDispInfo(NMTVDISPINFOW* info) {
    HWND tree = info->hdr.hwndFrom;
    TVITEMW& item = info->item;
    if (!(item.mask & TVIF_TEXT)) {
        return;
    }
    std::wstring_view text = TextOf(item.pszText);
    if (text.empty()) {
        return;
    }
    TreeState* state = GetTreeState(tree);
    if (!state || !IsRootChild(tree, item.hItem)) {
        return;
    }

    auto config = GetConfig();
    std::wstring_view newName =
        NewNameFor(config->settings.navItems, config->effectiveMatch, text);
    if (newName.empty()) {
        state->originalNames.erase(item.hItem);
        return;
    }
    state->originalNames[item.hItem] = std::wstring(text);
    // Ebeveynin tamponuna yazmak yerine pszText kendi tamponumuza yönlendirilir
    // (TVN_GETDISPINFO bunu destekler); ağaç metni hemen kopyalar/çizer.
    state->dispText.assign(newName);
    item.pszText = state->dispText.data();
}

LRESULT CALLBACK ParentSubclassProc(HWND hwnd,
                                    UINT msg,
                                    WPARAM wParam,
                                    LPARAM lParam,
                                    UINT_PTR idSubclass,
                                    DWORD_PTR) {
    switch (msg) {
        case WM_NOTIFY: {
            auto* hdr = reinterpret_cast<NMHDR*>(lParam);
            if (hdr && hdr->code == TVN_GETDISPINFOW) {
                LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
                FixDispInfo(reinterpret_cast<NMTVDISPINFOW*>(lParam));
                return result;
            }
            break;
        }

        case WM_NCDESTROY:
            RemoveWindowSubclass(hwnd, ParentSubclassProc, idSubclass);
            break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// Ağacın thread'inde çağrılmalıdır. Tekrar çağrılması zararsızdır. Ebeveyn
// (NamespaceTreeControl) de hemen subclass edilir: mod yüklenmeden önce
// eklenmiş callback metinli öğeler de TVN_GETDISPINFOW ile yeniden adlandırılır.
void AttachTree(HWND tree) {
    if (GetTreeState(tree)) {
        return;
    }
    auto* state = new TreeState;
    if (!SetWindowSubclass(tree, TreeSubclassProc, kTreeId,
                           reinterpret_cast<DWORD_PTR>(state))) {
        Wh_Log(L"AttachTree: SetWindowSubclass failed for %p", tree);
        delete state;
        return;
    }
    HWND parent = GetAncestor(tree, GA_PARENT);
    if (!parent ||
        !SetWindowSubclass(parent, ParentSubclassProc, kParentId, 0)) {
        Wh_Log(L"AttachTree: parent subclass failed for %p; callback names "
               L"will not be renamed",
               tree);
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
            ReapplyNames(tree);
            ResortTree(tree);
        });
    });
}

void Wh_ModUninit() {
}

void Wh_ModSettingsChanged() {
    ReloadConfig();

    // Bağlı tüm ağaçlar (Explorer pencereleri ve Explorer'ın barındırdığı
    // dosya diyalogları). Kopya kilit altında alınır, kilit RunOnWindowThread
    // öncesinde bırakılır.
    std::vector<HWND> trees;
    {
        std::lock_guard<std::mutex> lock(g_treesMutex);
        trees.assign(g_trees.begin(), g_trees.end());
    }
    for (HWND tree : trees) {
        RunOnWindowThread(tree, [tree] {
            // Ağaç bu arada yok edilmiş ve tutamaç yeniden kullanılmış olabilir.
            if (!GetTreeState(tree)) {
                return;
            }
            ReapplyNames(tree);
            ResortTree(tree);
        });
    }
}

#endif  // ET_LOGIC_ONLY
