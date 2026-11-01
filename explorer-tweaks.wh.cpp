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

// Çözülememiş (eşleşme metni boş) bir CLSID kuralı var mı?
inline bool HasUnresolvedClsid(const std::vector<NavRule>& rules,
                               const std::vector<std::wstring>& effectiveMatch) {
    for (size_t i = 0; i < rules.size(); i++) {
        if (IsClsidString(rules[i].match) &&
            (i >= effectiveMatch.size() || effectiveMatch[i].empty())) {
            return true;
        }
    }
    return false;
}

// Son denemeden (last) bu yana interval dolduysa 0, dolmadıysa kalan süre.
// Saat geride görünürse (now < last) tam interval beklenir.
inline unsigned long long RetryDelay(unsigned long long now,
                                     unsigned long long last,
                                     unsigned long long interval) {
    if (now < last) {
        return interval;
    }
    unsigned long long elapsed = now - last;
    return elapsed >= interval ? 0 : interval - elapsed;
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

// ----- Gizleme: yedek listesi ve yedek değeri -----

// CLSID metninin ASCII büyük harfli hali. Windhawk storage anahtarları ve
// yedek listesi hep bu biçimi kullanır, böylece büyük/küçük harf farkı aynı
// CLSID'yi iki kayda bölmez.
inline std::wstring NormalizeClsid(std::wstring_view s) {
    std::wstring result(s);
    for (wchar_t& c : result) {
        if (c >= L'a' && c <= L'z') {
            c = static_cast<wchar_t>(c - L'a' + L'A');
        }
    }
    return result;
}

inline bool ContainsClsid(const std::vector<std::wstring>& list,
                          std::wstring_view clsid) {
    for (const std::wstring& item : list) {
        if (NamesEqual(item, clsid)) {
            return true;
        }
    }
    return false;
}

// clsid CLSID biçimindeyse ve listede yoksa sona (normalleştirilmiş) ekler.
inline bool AddToBackupList(std::vector<std::wstring>& list,
                            std::wstring_view clsid) {
    if (!IsClsidString(clsid) || ContainsClsid(list, clsid)) {
        return false;
    }
    list.push_back(NormalizeClsid(clsid));
    return true;
}

inline bool RemoveFromBackupList(std::vector<std::wstring>& list,
                                 std::wstring_view clsid) {
    for (auto it = list.begin(); it != list.end(); ++it) {
        if (NamesEqual(*it, clsid)) {
            list.erase(it);
            return true;
        }
    }
    return false;
}

// ";" ile ayrılmış CLSID'ler. CLSID olmayan ve boş parçalar atılır; tekrarlar
// (büyük/küçük harf duyarsız) tek kayda iner, ilk görülme sırası korunur.
inline std::vector<std::wstring> ParseBackupList(std::wstring_view text) {
    std::vector<std::wstring> list;
    size_t pos = 0;
    while (pos <= text.size()) {
        size_t end = text.find(L';', pos);
        if (end == std::wstring_view::npos) {
            end = text.size();
        }
        AddToBackupList(list, text.substr(pos, end - pos));
        pos = end + 1;
    }
    return list;
}

inline std::wstring SerializeBackupList(const std::vector<std::wstring>& list) {
    std::wstring text;
    for (const std::wstring& item : list) {
        if (!text.empty()) {
            text += L';';
        }
        text += item;
    }
    return text;
}

// Gizlemeden önceki System.IsPinnedToNameSpaceTree değeri: değer hiç yoktu
// (absent) ya da bir DWORD'du.
struct BackupValue {
    bool absent = true;
    DWORD value = 0;
};

inline std::wstring SerializeBackupValue(const BackupValue& v) {
    return v.absent ? std::wstring(L"absent") : std::to_wstring(v.value);
}

// "absent" ya da işaretsiz ondalık DWORD (boşluk, işaret, önek yok).
inline bool ParseBackupValue(std::wstring_view text, BackupValue& out) {
    if (text == L"absent") {
        out = {true, 0};
        return true;
    }
    if (text.empty() || text.size() > 10) {
        return false;
    }
    unsigned long long n = 0;
    for (wchar_t c : text) {
        if (c < L'0' || c > L'9') {
            return false;
        }
        n = n * 10 + static_cast<unsigned long long>(c - L'0');
    }
    if (n > 0xFFFFFFFFull) {
        return false;
    }
    out = {false, static_cast<DWORD>(n)};
    return true;
}

struct HiddenPlan {
    std::vector<std::wstring> hide;     // hedefte, yedeği yok: yedekle + gizle
    std::vector<std::wstring> ensure;   // hedefte, yedeği var: gizli olduğunu doğrula
    std::vector<std::wstring> restore;  // yedeği var, hedefte değil: geri yükle
};

// target: gizlenmesi istenen CLSID'ler; backups: yedek listesi. anyUnresolved:
// çözülemeyen bir gizli kural var. Böyle bir kuralın CLSID'i hedefte yoktur ve
// yedeği geri yüklenmeye aday görünür; bu yüzden o turda hiçbir şey geri
// yüklenmez (gizleme ve doğrulama sürer).
inline HiddenPlan PlanHidden(const std::vector<std::wstring>& target,
                             const std::vector<std::wstring>& backups,
                             bool anyUnresolved = false) {
    HiddenPlan plan;
    for (const std::wstring& clsid : target) {
        (ContainsClsid(backups, clsid) ? plan.ensure : plan.hide)
            .push_back(clsid);
    }
    for (const std::wstring& clsid : backups) {
        if (!anyUnresolved && !ContainsClsid(target, clsid)) {
            plan.restore.push_back(clsid);
        }
    }
    return plan;
}

// hidden: true olan kuralların CLSID'leri (normalleştirilmiş, tekilleştirilmiş,
// kural sırasıyla). CLSID kuralları doğrudan, isim kuralları resolveName ile
// (std::wstring(std::wstring_view); çözülemezse boş ya da CLSID olmayan döner)
// CLSID'e çevrilir; çözülemeyenler atlanır ve *unresolved (verilmişse) true
// olur, aksi halde false. Boş match'li ve gizli olmayan kurallar sayılmaz.
template <typename ResolveName>
std::vector<std::wstring> ComputeHiddenTargets(
    const std::vector<NavRule>& rules,
    ResolveName&& resolveName,
    bool* unresolved = nullptr) {
    if (unresolved) {
        *unresolved = false;
    }
    std::vector<std::wstring> targets;
    for (const NavRule& rule : rules) {
        if (!rule.hidden || rule.match.empty()) {
            continue;
        }
        std::wstring clsid = IsClsidString(rule.match)
                                 ? rule.match
                                 : std::wstring(resolveName(rule.match));
        if (!IsClsidString(clsid)) {
            if (unresolved) {
                *unresolved = true;
            }
            continue;
        }
        AddToBackupList(targets, clsid);
    }
    return targets;
}

// ===== End Logic =====

#ifndef ET_LOGIC_ONLY

#include <commctrl.h>
#include <shlobj.h>
#define STRSAFE_NO_DEPRECATE
#include <strsafe.h>
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

// Son CLSID çözüm denemesinin zamanı (GetTickCount64), süreç genelinde.
// Çözülemeyen CLSID kurallarının yeniden denenmesini kısar.
constexpr unsigned long long kResolveRetryMs = 5000;
std::atomic<unsigned long long> g_lastResolveAttempt{0};

// Ayarları okur, kuralları çözer ve yeni Config'i yayınlar. Yalnızca
// Wh_ModInit ve Wh_ModSettingsChanged thread'lerinden çağrılır; CLSID çözümü
// için COM burada başlatılır.
void ReloadConfig() {
    ComScope com;
    g_lastResolveAttempt.store(GetTickCount64());
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

// ===== Hiding =====
// Gizleme, HKCU\Software\Classes\CLSID\{...} altındaki
// System.IsPinnedToNameSpaceTree = 0 değeriyle yapılır (HKLM'ye dokunulmaz).
// Orijinal değer, registry'ye yazmadan ÖNCE Windhawk storage'ına "backup/{CLSID}"
// olarak yedeklenir; böylece araya giren bir çökme orijinal değeri kaybettirmez.
// Storage numaralandırma sunmadığı için yedeği olan CLSID'ler "backupList"
// değerinde ";" ile ayrılmış tutulur.

constexpr wchar_t kPinnedValueName[] = L"System.IsPinnedToNameSpaceTree";
constexpr wchar_t kBackupListName[] = L"backupList";
// ~200 CLSID. Wh_GetStringValue sığmayan değerde boş döner; bu sınır pratikte
// aşılmaz (kullanıcı birkaç öğe gizler).
constexpr size_t kBackupListChars = 8192;
constexpr size_t kBackupValueChars = 64;

// Ayarlar ve Uninit farklı thread'lerden gizleme yapabilir; durum (storage +
// registry) tek seferde bir thread'de değişir.
std::mutex g_hiddenMutex;

std::wstring ClsidKeyPath(std::wstring_view clsid) {
    std::wstring path = L"Software\\Classes\\CLSID\\";
    path.append(clsid);
    return path;
}

std::wstring BackupValueName(std::wstring_view clsid) {
    return L"backup/" + NormalizeClsid(clsid);
}

std::vector<std::wstring> LoadBackupList() {
    std::vector<wchar_t> buf(kBackupListChars);
    size_t n = Wh_GetStringValue(kBackupListName, buf.data(), buf.size());
    return ParseBackupList(std::wstring_view(buf.data(), n));
}

bool SaveBackupList(const std::vector<std::wstring>& list) {
    BOOL ok = list.empty()
                  ? Wh_DeleteValue(kBackupListName)
                  : Wh_SetStringValue(kBackupListName,
                                      SerializeBackupList(list).c_str());
    if (!ok) {
        Wh_Log(L"SaveBackupList: storage write failed");
    }
    return ok != FALSE;
}

// Değerin o anki durumu. Anahtar ya da değer yoksa absent. Okunamazsa ya da
// DWORD değilse false (neden loglanır); çağıran o CLSID'e dokunmaz.
bool ReadPinnedValue(std::wstring_view clsid, BackupValue& out) {
    std::wstring path = ClsidKeyPath(clsid);
    HKEY key = nullptr;
    LSTATUS rc = RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0,
                               KEY_QUERY_VALUE, &key);
    if (rc == ERROR_FILE_NOT_FOUND || rc == ERROR_PATH_NOT_FOUND) {
        out = {true, 0};
        return true;
    }
    if (rc != ERROR_SUCCESS) {
        Wh_Log(L"ReadPinnedValue: RegOpenKeyEx(%s) failed (%ld)", path.c_str(),
               static_cast<long>(rc));
        return false;
    }
    DWORD type = 0;
    DWORD data = 0;
    DWORD size = sizeof(data);
    rc = RegQueryValueExW(key, kPinnedValueName, nullptr, &type,
                          reinterpret_cast<BYTE*>(&data), &size);
    RegCloseKey(key);
    if (rc == ERROR_FILE_NOT_FOUND) {
        out = {true, 0};
        return true;
    }
    if (rc != ERROR_SUCCESS) {
        Wh_Log(L"ReadPinnedValue: RegQueryValueEx(%s) failed (%ld)",
               path.c_str(), static_cast<long>(rc));
        return false;
    }
    if (type != REG_DWORD || size != sizeof(data)) {
        Wh_Log(L"ReadPinnedValue: %s has unexpected type %lu; leaving it alone",
               path.c_str(), static_cast<unsigned long>(type));
        return false;
    }
    out = {false, data};
    return true;
}

// Anahtar yoksa oluşturur.
bool WritePinnedValue(std::wstring_view clsid, DWORD value) {
    std::wstring path = ClsidKeyPath(clsid);
    HKEY key = nullptr;
    LSTATUS rc = RegCreateKeyExW(HKEY_CURRENT_USER, path.c_str(), 0, nullptr,
                                 0, KEY_SET_VALUE, nullptr, &key, nullptr);
    if (rc != ERROR_SUCCESS) {
        Wh_Log(L"WritePinnedValue: RegCreateKeyEx(%s) failed (%ld)",
               path.c_str(), static_cast<long>(rc));
        return false;
    }
    rc = RegSetValueExW(key, kPinnedValueName, 0, REG_DWORD,
                        reinterpret_cast<const BYTE*>(&value), sizeof(value));
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS) {
        Wh_Log(L"WritePinnedValue: RegSetValueEx(%s) failed (%ld)",
               path.c_str(), static_cast<long>(rc));
        return false;
    }
    return true;
}

// Değeri siler; anahtara dokunmaz. Zaten yoksa başarılıdır.
bool DeletePinnedValue(std::wstring_view clsid) {
    std::wstring path = ClsidKeyPath(clsid);
    HKEY key = nullptr;
    LSTATUS rc = RegOpenKeyExW(HKEY_CURRENT_USER, path.c_str(), 0,
                               KEY_SET_VALUE, &key);
    if (rc == ERROR_FILE_NOT_FOUND || rc == ERROR_PATH_NOT_FOUND) {
        return true;
    }
    if (rc != ERROR_SUCCESS) {
        Wh_Log(L"DeletePinnedValue: RegOpenKeyEx(%s) failed (%ld)",
               path.c_str(), static_cast<long>(rc));
        return false;
    }
    rc = RegDeleteValueW(key, kPinnedValueName);
    RegCloseKey(key);
    if (rc != ERROR_SUCCESS && rc != ERROR_FILE_NOT_FOUND) {
        Wh_Log(L"DeletePinnedValue: RegDeleteValue(%s) failed (%ld)",
               path.c_str(), static_cast<long>(rc));
        return false;
    }
    return true;
}

void AppendNamespaceClsids(HKEY root, std::vector<std::wstring>& out) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(root,
                      L"Software\\Microsoft\\Windows\\CurrentVersion\\Explorer"
                      L"\\Desktop\\NameSpace",
                      0, KEY_ENUMERATE_SUB_KEYS, &key) != ERROR_SUCCESS) {
        return;
    }
    for (DWORD i = 0;; i++) {
        wchar_t name[64];
        DWORD len = ARRAYSIZE(name);
        LSTATUS rc = RegEnumKeyExW(key, i, name, &len, nullptr, nullptr,
                                   nullptr, nullptr);
        if (rc == ERROR_NO_MORE_ITEMS) {
            break;
        }
        if (rc == ERROR_MORE_DATA) {
            continue;  // CLSID olamayacak kadar uzun
        }
        if (rc != ERROR_SUCCESS) {
            Wh_Log(L"AppendNamespaceClsids: RegEnumKeyEx failed (%ld)",
                   static_cast<long>(rc));
            break;
        }
        AddToBackupList(out, name);  // CLSID olmayanları atar, tekilleştirir
    }
    RegCloseKey(key);
}

// Görünen adı name olan nav pane CLSID'i; bulunamazsa boş. Adaylar: bilinen
// sabitler ile HKLM/HKCU Desktop\NameSpace alt anahtarları. COM başlatılmış
// bir thread'de çağrılmalıdır.
std::wstring FindClsidByName(std::wstring_view name) {
    std::vector<std::wstring> candidates;
    for (const wchar_t* known : {
             L"{20D04FE0-3AEA-1069-A2D8-08002B30309D}",  // This PC
             L"{F02C1A0D-BE21-4350-88B0-7367FC96EF3C}",  // Network
             L"{f874310e-b6b7-47dc-bc84-b9e6b38f5903}",  // Home
             L"{e88865ea-0e1c-4e20-9aa6-edcd0212c87c}",  // Gallery
         }) {
        AddToBackupList(candidates, known);
    }
    AppendNamespaceClsids(HKEY_LOCAL_MACHINE, candidates);
    AppendNamespaceClsids(HKEY_CURRENT_USER, candidates);

    for (const std::wstring& clsid : candidates) {
        std::wstring resolved = ResolveClsidDisplayName(clsid);
        if (!resolved.empty() && NamesEqual(resolved, name)) {
            return clsid;
        }
    }
    Wh_Log(L"FindClsidByName: no nav item named \"%.*s\"",
           static_cast<int>(name.size()), name.data());
    return L"";
}

// clsid'i yedekleyip gizler. Sıra: (1) mevcut değeri oku, (2) yedek değerini
// yaz, (3) yedek listesine ekle, (4) registry'ye 0 yaz. Registry yazımı
// başarısız olursa 2-3 geri alınır; böylece yedeği olmayan bir CLSID'in
// "gizlenmiş" sayılması ya da başarısız yazım için yedek kalması olmaz.
bool HideOne(const std::wstring& clsid) {
    BackupValue original;
    if (!ReadPinnedValue(clsid, original)) {
        return false;
    }
    std::wstring name = BackupValueName(clsid);
    if (!Wh_SetStringValue(name.c_str(),
                           SerializeBackupValue(original).c_str())) {
        Wh_Log(L"HideOne: backup write failed for %s; not hiding",
               clsid.c_str());
        return false;
    }
    std::vector<std::wstring> list = LoadBackupList();
    AddToBackupList(list, clsid);
    if (!SaveBackupList(list)) {
        Wh_DeleteValue(name.c_str());
        return false;
    }
    if (!WritePinnedValue(clsid, 0)) {
        RemoveFromBackupList(list, clsid);
        SaveBackupList(list);
        Wh_DeleteValue(name.c_str());
        return false;
    }
    Wh_Log(L"HideOne: hid %s (original %s)", clsid.c_str(),
           SerializeBackupValue(original).c_str());
    return true;
}

// Yedeği zaten olan bir CLSID'in hâlâ gizli (0) olduğunu doğrular, değilse
// yeniden yazar (yedeğe dokunulmaz). Gizleme yazımı çöktüyse ya da değer
// dışarıdan değiştiyse onarır. Registry'ye yazıldıysa true.
bool EnsureHidden(const std::wstring& clsid) {
    BackupValue current;
    if (!ReadPinnedValue(clsid, current)) {
        return false;
    }
    if (!current.absent && current.value == 0) {
        return false;
    }
    if (!WritePinnedValue(clsid, 0)) {
        return false;
    }
    Wh_Log(L"EnsureHidden: re-hid %s", clsid.c_str());
    return true;
}

// Yedeği geri yazar ve yedek kaydını siler. Sıra: (1) registry'yi geri yaz,
// (2) listeden çıkar, (3) yedek değerini sil; her adım arasında çökme sonraki
// çalıştırmada aynı işlemi güvenle tekrarlatır. Geri yazım başarısızsa yedek
// korunur (false). Registry değiştiyse changed true olur.
bool RestoreOne(const std::wstring& clsid, bool& changed) {
    std::wstring name = BackupValueName(clsid);
    wchar_t buf[kBackupValueChars];
    size_t n = Wh_GetStringValue(name.c_str(), buf, ARRAYSIZE(buf));
    BackupValue backup;
    bool valid = n != 0 && ParseBackupValue(std::wstring_view(buf, n), backup);

    if (valid) {
        bool ok = backup.absent ? DeletePinnedValue(clsid)
                                : WritePinnedValue(clsid, backup.value);
        if (!ok) {
            return false;
        }
        changed = true;
        Wh_Log(L"RestoreOne: restored %s (%s)", clsid.c_str(),
               SerializeBackupValue(backup).c_str());
    } else {
        // Geri yüklenecek bir şey yok; kayıt takılı kalmasın diye düşülür.
        Wh_Log(L"RestoreOne: no valid backup for %s; dropping entry",
               clsid.c_str());
    }

    std::vector<std::wstring> list = LoadBackupList();
    RemoveFromBackupList(list, clsid);
    if (SaveBackupList(list)) {
        Wh_DeleteValue(name.c_str());
    }
    return true;
}

void NotifyShellChanged() {
    SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
}

// Son ApplyHidden turunda çözülemeyen bir gizli isim kuralı kaldıysa true.
// RetryClsidResolution bunu görünce (en fazla kResolveRetryMs'de bir) ApplyHidden'ı
// yeniden çalıştırır.
std::atomic<bool> g_hiddenUnresolved{false};

// Ayarlardaki hidden: true kurallarını registry'ye uygular, kaldırılanları
// geri yükler. Tekrar çağrılması zararsızdır (idempotent). Çözülemeyen bir
// gizli isim kuralı varsa o turda geri yükleme yapılmaz (kuralın yedeği geri
// yüklenip öğe görünür kalmasın) ve yeniden deneme istenir.
void ApplyHidden() {
    std::lock_guard<std::mutex> lock(g_hiddenMutex);
    ComScope com;  // isim kuralları için kabuk çağrıları
    auto config = GetConfig();

    bool unresolved = false;
    std::vector<std::wstring> target = ComputeHiddenTargets(
        config->settings.navItems,
        [](std::wstring_view name) { return FindClsidByName(name); },
        &unresolved);
    g_hiddenUnresolved.store(unresolved);
    if (unresolved) {
        Wh_Log(L"ApplyHidden: unresolved hidden name rule(s); restore "
               L"skipped, will retry");
    }
    HiddenPlan plan = PlanHidden(target, LoadBackupList(), unresolved);

    bool changed = false;
    for (const std::wstring& clsid : plan.hide) {
        changed |= HideOne(clsid);
    }
    for (const std::wstring& clsid : plan.ensure) {
        changed |= EnsureHidden(clsid);
    }
    for (const std::wstring& clsid : plan.restore) {
        RestoreOne(clsid, changed);
    }
    if (changed) {
        NotifyShellChanged();
    }
}

// Bütün yedekleri geri yükler (mod kaldırılırken).
void RestoreAllHidden() {
    std::lock_guard<std::mutex> lock(g_hiddenMutex);
    bool changed = false;
    for (const std::wstring& clsid : LoadBackupList()) {
        RestoreOne(clsid, changed);
    }
    if (changed) {
        NotifyShellChanged();
    }
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
constexpr UINT_PTR kRetryTimerId = 0xE9;  // ertelenmiş CLSID yeniden denemesi
constexpr UINT kResortDelayMs = 50;

// Ağaca gönderilir (PostMessage): adları yeniden uygula ve yeniden sırala.
UINT g_reapplyMsg = RegisterWindowMessageW(L"ExplorerTweaks_Reapply");

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
    // Pin ikonlarını kaldırırken ağaçtan alınan orijinal state image list.
    // Sahibi CNscTree'dir; biz yalnızca geri koyarız, asla yok etmeyiz.
    HIMAGELIST savedStateList = nullptr;
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

// Bağlı tüm ağaçlara adları yeniden uygulama + yeniden sıralama mesajı
// bırakır. Bloklamaz: her ağaç mesajı kendi thread'inde işler. Tutamaç bu
// arada başka bir pencereye geçmişse kayıtlı mesaj o pencerece yok sayılır.
void PostReapplyToAllTrees() {
    if (!g_reapplyMsg) {
        return;
    }
    std::vector<HWND> trees;
    {
        std::lock_guard<std::mutex> lock(g_treesMutex);
        trees.assign(g_trees.begin(), g_trees.end());
    }
    for (HWND tree : trees) {
        PostMessageW(tree, g_reapplyMsg, 0, 0);
    }
}

// Mevcut Config'te çözülemeyen CLSID kuralı varsa (ör. Phone Link isim alanı
// Explorer açılışında henüz hazır değildi) çözümü yeniden dener; çözülemeyen
// gizli isim kuralı varsa (g_hiddenUnresolved) ApplyHidden'ı yeniden çalıştırır.
// Süreç genelinde en fazla kResolveRetryMs'de bir dener; süre dolmamışsa kalan
// süre için bu ağaçta bir zamanlayıcı kurar. Ağacın (STA) UI thread'inde
// çağrılır. Kabuk çağrıları hiçbir kilit tutulmadan yapılır (ApplyHidden yalnızca
// kendi kısa süreli g_hiddenMutex'ini alır ve hiçbir pencere işlemi yapmaz).
void RetryClsidResolution(HWND tree) {
    auto base = GetConfig();
    bool hiddenPending = g_hiddenUnresolved.load();
    if (!hiddenPending &&
        !HasUnresolvedClsid(base->settings.navItems, base->effectiveMatch)) {
        return;
    }

    unsigned long long now = GetTickCount64();
    unsigned long long last = g_lastResolveAttempt.load();
    unsigned long long wait = RetryDelay(now, last, kResolveRetryMs);
    if (wait) {
        SetTimer(tree, kRetryTimerId, static_cast<UINT>(wait), nullptr);
        return;
    }
    // Aynı anda başka bir thread de denemek isteyebilir; yalnızca biri kazanır.
    if (!g_lastResolveAttempt.compare_exchange_strong(last, now)) {
        return;
    }

    // Gizli isim kuralı çözülemiyordu; şimdi çözülürse gizlenir ve (bekleyen)
    // geri yüklemeler de yapılır. Sonuç bayrağı ApplyHidden günceller; çözülene
    // kadar bir sonraki tetiklemede yine denenir.
    if (hiddenPending) {
        ApplyHidden();
    }

    std::vector<std::wstring> match = base->effectiveMatch;
    match.resize(base->settings.navItems.size());
    {
        // UI thread zaten STA'dır; bu çağrı S_FALSE döner ve CoUninitialize
        // yalnızca bu sayacı dengeler. Başka modda başlatılmışsa dokunulmaz.
        ComScope com;
        for (size_t i = 0; i < match.size(); i++) {
            const std::wstring& rule = base->settings.navItems[i].match;
            if (IsClsidString(rule) && match[i].empty()) {
                match[i] = ResolveClsidDisplayName(rule);
            }
        }
    }
    if (match == base->effectiveMatch) {
        return;  // hâlâ çözülemedi; bir sonraki tetiklemede yeniden denenir
    }

    auto config = std::make_shared<Config>();
    config->settings = base->settings;
    config->effectiveMatch = std::move(match);
    std::shared_ptr<const Config> old;
    {
        std::lock_guard<std::mutex> lock(g_configMutex);
        if (g_config != base) {
            // Bu arada ayarlar yeniden yüklendi; o yükleme kendi çözümünü yaptı.
            return;
        }
        old = std::exchange(g_config, std::move(config));
    }
    Wh_Log(L"RetryClsidResolution: resolved CLSID rule(s) late");
    PostReapplyToAllTrees();
}

LRESULT CALLBACK ParentSubclassProc(HWND, UINT, WPARAM, LPARAM, UINT_PTR,
                                    DWORD_PTR);

// Pin ikonlarını duruma göre kaldırır ya da geri koyar. Ağacın thread'inde
// çağrılmalıdır (TVM_SETIMAGELIST başka thread'den gönderilmemeli).
// hidePins açıkken: ağaçta state image list varsa çıkarılır ve TreeState'te
// saklanır (öğelerin stateImg bitlerine dokunulmaz; pinli algılama sürer).
// hidePins kapalıyken: saklı liste varsa ve ağaçta o arada yeni bir liste
// kurulmamışsa geri konur. Saklı liste yoksa (ağaç, kanca listeyi bastırırken
// açılmış) ikonlar pencere yeniden açılana kadar geri gelmez.
void ApplyPinIcons(HWND tree) {
    TreeState* state = GetTreeState(tree);
    if (!state) {
        return;
    }
    HIMAGELIST current = TreeView_GetImageList(tree, TVSIL_STATE);
    if (GetConfig()->settings.hidePins) {
        if (current) {
            HIMAGELIST old = TreeView_SetImageList(tree, nullptr, TVSIL_STATE);
            if (old) {
                // En yenisi saklanır; öncekini sahibi zaten değiştirmiş olabilir.
                state->savedStateList = old;
            }
        }
        return;
    }
    if (state->savedStateList) {
        if (!current) {
            TreeView_SetImageList(tree, state->savedStateList, TVSIL_STATE);
        }
        state->savedStateList = nullptr;
    } else if (!current) {
        Wh_Log(L"ApplyPinIcons: tree %p has no state image list to restore; "
               L"pin icons return when the window is reopened",
               tree);
    }
}

// Ağacın subclass'larını kaldırır, zamanlayıcısını öldürür ve durumunu serbest
// bırakır. Ağacın thread'inde çağrılmalıdır. windowDestroyed, pencerenin
// WM_NCDESTROY'unda true verilir: yok olan ağaca image list geri konmaz.
void DetachTree(HWND tree, bool windowDestroyed = false) {
    KillTimer(tree, kTreeId);
    KillTimer(tree, kRetryTimerId);
    TreeState* state = GetTreeState(tree);
    if (state && state->savedStateList && !windowDestroyed) {
        if (!TreeView_GetImageList(tree, TVSIL_STATE)) {
            TreeView_SetImageList(tree, state->savedStateList, TVSIL_STATE);
        }
        state->savedStateList = nullptr;
    }
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
    if (msg == g_reapplyMsg && g_reapplyMsg) {
        ReapplyNames(hwnd);
        ResortTree(hwnd);
        return 0;
    }
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
                // Yeni çözülürse Config yayınlanır ve tüm ağaçlara (bu ağaç
                // dahil) yeniden uygulama mesajı bırakılır.
                RetryClsidResolution(hwnd);
                ResortTree(hwnd);
                return 0;
            }
            if (wParam == kRetryTimerId) {
                KillTimer(hwnd, kRetryTimerId);
                RetryClsidResolution(hwnd);
                return 0;
            }
            break;

        case WM_NCDESTROY:
            DetachTree(hwnd, true);
            break;
    }
    return DefSubclassProc(hwnd, msg, wParam, lParam);
}

// Callback metinli bir kök çocuğunun TVN_GETDISPINFOW cevabını kurala göre
// yeni adla değiştirir ve orijinal adı kaydeder. buffer/cchBuffer, ağacın
// bildirimden önce verdiği tampondur. Yeni ad bu tampona kopyalanır ve
// pszText ona geri çevrilir; böylece ağacın elinde bizim belleğimize işaret
// eden bir gösterici kalmaz.
void FixDispInfo(NMTVDISPINFOW* info, LPWSTR buffer, int cchBuffer) {
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
    if (newName.empty() || !buffer || buffer == LPSTR_TEXTCALLBACKW ||
        cchBuffer <= 0) {
        // Yeniden adlandırma yok ya da yazılabilecek bir tampon yok: öğe
        // orijinal adıyla gösterilir.
        state->originalNames.erase(item.hItem);
        return;
    }
    // text ebeveynin tamponunu (buffer olabilir) gösterir; üzerine yazmadan
    // önce kopyalanır.
    state->originalNames[item.hItem] = std::wstring(text);
    std::wstring name(newName);
    // Sığmazsa kesilir; StringCchCopyW her durumda sonlandırıcı yazar.
    StringCchCopyW(buffer, static_cast<size_t>(cchBuffer), name.c_str());
    item.pszText = buffer;
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
                auto* info = reinterpret_cast<NMTVDISPINFOW*>(lParam);
                // Ebeveyn pszText'i kendi belleğine yönlendirebilir; ağacın
                // asıl tamponu önceden saklanır.
                LPWSTR buffer = info->item.pszText;
                int cchBuffer = info->item.cchTextMax;
                LRESULT result = DefSubclassProc(hwnd, msg, wParam, lParam);
                FixDispInfo(info, buffer, cchBuffer);
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

// CNscTree::SetStateImageList (ExplorerFrame.dll): Quick Access pin ikonları
// ağacın state image list'iyle çizilir. hidePins açıkken liste hiç kurulmaz.
// x64'te tüm çağrı kuralları aynıdır; this işaretçisi void* alınır.
using CNscTree_SetStateImageList_t = HRESULT (*)(void*, HIMAGELIST);
CNscTree_SetStateImageList_t CNscTree_SetStateImageList_Original;

HRESULT CNscTree_SetStateImageList_Hook(void* pThis, HIMAGELIST himl) {
    if (GetConfig()->settings.hidePins) {
        return S_OK;
    }
    return CNscTree_SetStateImageList_Original(pThis, himl);
}

// Sembol bulunamazsa ya da kancalanamazsa yalnızca açık ağaçlardaki listeler
// kaldırılır; mod yüklenir.
void HookPinIcons() {
    HMODULE frame = GetModuleHandleW(L"ExplorerFrame.dll");
    if (!frame) {
        frame = LoadLibraryExW(L"ExplorerFrame.dll", nullptr,
                               LOAD_LIBRARY_SEARCH_SYSTEM32);
    }
    if (!frame) {
        Wh_Log(L"pin hook unavailable (ExplorerFrame.dll not loaded)");
        return;
    }
    WindhawkUtils::SYMBOL_HOOK hooks[] = {
        {
            {L"public: virtual long __cdecl "
             L"CNscTree::SetStateImageList(struct _IMAGELIST *)"},
            &CNscTree_SetStateImageList_Original,
            CNscTree_SetStateImageList_Hook,
            false,
        },
    };
    if (!WindhawkUtils::HookSymbols(frame, hooks, ARRAYSIZE(hooks))) {
        Wh_Log(L"pin hook unavailable");
    }
}

// ===== Mod lifecycle =====

BOOL Wh_ModInit() {
    ReloadConfig();
    ApplyHidden();
    if (!WindhawkUtils::SetFunctionHook(CreateWindowExW, CreateWindowExW_Hook,
                                        &CreateWindowExW_Original)) {
        Wh_Log(L"CreateWindowExW hook failed; only existing windows sorted");
    }
    HookPinIcons();
    return TRUE;
}

// Hook'lar Wh_ModInit'ten sonra uygulanır; mevcut pencereler burada taranırsa
// arada açılan bir pencere kaçmaz (iki kez bağlanması zararsızdır).
void Wh_ModAfterInit() {
    ForEachNavTree([](HWND tree) {
        RunOnWindowThread(tree, [tree] {
            AttachTree(tree);
            ApplyPinIcons(tree);
            ReapplyNames(tree);
            ResortTree(tree);
        });
    });
}

void Wh_ModUninit() {
    RestoreAllHidden();
}

void Wh_ModSettingsChanged() {
    ReloadConfig();
    ApplyHidden();

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
            ApplyPinIcons(tree);
            ReapplyNames(tree);
            ResortTree(tree);
        });
    }
}

#endif  // ET_LOGIC_ONLY
