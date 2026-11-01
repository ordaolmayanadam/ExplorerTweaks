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

struct Settings {
    bool hideTabs = true;
    bool hideCommandBar = true;
    bool hidePins = true;
    PinnedPosition pinnedPosition = PinnedPosition::Bottom;
    std::vector<NavRule> navItems;
};

Settings g_settings;

static std::wstring ReadStringSetting(const wchar_t* fmt, int index) {
    PCWSTR value = Wh_GetStringSetting(fmt, index);
    std::wstring result = value ? value : L"";
    Wh_FreeStringSetting(value);
    return result;
}

void LoadSettings() {
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

    g_settings = std::move(s);
}

BOOL Wh_ModInit() {
    LoadSettings();
    return TRUE;
}

void Wh_ModUninit() {
}

void Wh_ModSettingsChanged() {
    LoadSettings();
}

#endif  // ET_LOGIC_ONLY
