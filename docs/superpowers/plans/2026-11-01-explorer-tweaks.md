# Explorer Tweaks Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Tek başına çalışan bir Windhawk modu yazmak. Mod Explorer'da tab satırını, toolbar'ı ve Quick Access pin ikonlarını gizler; sol menüyü ayarlardan sıralanabilir, yeniden adlandırılabilir ve gizlenebilir yapar.

**Architecture:** Tek dosyalık bir Windhawk modu (`explorer-tweaks.wh.cpp`), `explorer.exe` içine yüklenir. Sol menü için `SysTreeView32` subclass edilir ve kök çocukları `TVM_SORTCHILDRENCB` ile sıralanır. Gizleme HKCU CLSID registry değerleriyle yapılır. Pin ikonları `CNscTree::SetStateImageList` sembol hook'uyla kaldırılır. Tab ve toolbar XAML Diagnostics (TAP) ile gizlenir. Saf mantık (`ComputeOrder` vb.) dosyanın `ET_LOGIC_ONLY` ile ayrılabilen bir bölümündedir; bu bölüm masaüstünde bağımsız bir test exe'siyle test edilir.

**Tech Stack:** C++23, Windhawk 1.7.3 API (`windhawk_api.h`, `windhawk_utils.h`), Windhawk'ın clang++ (x86_64-w64-mingw32) derleyicisi, Win32 comctl32 TreeView, XAML Diagnostics (`xamlom.h`), WinRT (`Windows.UI.Xaml` / `Microsoft.UI.Xaml`), PowerShell 5.1 doğrulama betikleri.

**Spec:** `docs/superpowers/specs/2026-11-01-explorer-tweaks-design.md`

## Global Constraints

- Mod kimliği `explorer-tweaks`, sürüm `1.0.0`, hedef süreç `@include explorer.exe`, mimari `x86-64`.
- Ayar anahtarları birebir şunlar olmalı: `hideTabs`, `hideCommandBar`, `hidePins`, `pinnedPosition` (`default|top|bottom`), `navItems[].match`, `navItems[].newName`, `navItems[].hidden`.
- Varsayılan `navItems` listesi: `This PC` → `{3611C374-393E-41F8-98EB-1AD7BF051346}` (newName `Phone`) → `Linux`. Varsayılan `pinnedPosition: bottom`, diğer üç bayrak `true`.
- Pinli öğe tanımı: kök (Desktop) çocuğu olmak ve `(state & TVIS_STATEIMAGEMASK) >> 12 == 1` olmak.
- `match` eşleştirmesi büyük/küçük harf duyarsızdır (`CompareStringOrdinal(..., TRUE)`) ve her zaman **orijinal** ada göre yapılır.
- Registry yedek anahtarı: `Wh_SetStringValue(L"backup/{CLSID}", L"absent" | L"<dword decimal>")`.
- Sıralama ertelemesi: 50 ms (`SetTimer`).
- Derleme: `C:\Program Files\Windhawk\Compiler\bin\clang++.exe`. Bayraklar `Compiler\compile_flags.txt` dosyasından alınır. Link: `-L "C:\Program Files\Windhawk\Engine\1.7.3\64" -lwindhawk`. Mod başlığındaki `@compilerOptions` satırı da aynı kütüphaneleri listeler: `-lcomctl32 -lole32 -loleaut32 -lshlwapi -lruntimeobject`.
- Kod, Windhawk mod kalıbını izler: ayarlar `Wh_GetIntSetting` / `Wh_GetStringSetting` ile okunur, loglama `Wh_Log`, hook'lar `Wh_SetFunctionHook` ve `WindhawkUtils::HookSymbols` ile yapılır.

## Review Focus

1. **Explorer kökü yeniden doldurursa** (yeni klasör pinleme, telefonun bağlanıp kopması, WSL'nin başlaması) sıra yeniden uygulanmalı. Test: Task 3 Step 6.
2. **Mod açılmadan önce açılmış pencereler ve birden fazla pencere** subclass edilmeli. Test: Task 3 Step 6.
3. **Hiçbir öğeyle eşleşmeyen kural** (telefon bağlı değil, CLSID çözülemiyor, `match` boş, aynı `match` iki kez yazılmış) çökmeye yol açmamalı. Boş kurallar atlanır, tekrarlarda ilki geçerli olur, diğer öğeler yine sıralanır. Test: Task 2 `ComputeOrder` testleri.
4. **Pencereler açıkken mod kapatılırsa** Explorer çökmemeli. Subclass'lar DLL boşaltılmadan önce sahibi olan thread'de kaldırılmalı, zamanlayıcılar öldürülmeli, registry geri yüklenmeli. Test: Task 7 Step 3.
5. **Yeniden adlandırılmış bir öğe Explorer tarafından tekrar `TVM_SETITEMW` ile güncellenirse** ad orijinale dönmemeli; eşleştirme de yeni adla değil orijinal adla devam etmeli. Test: Task 4 Step 5.

## Dosya yapısı

| Dosya | Sorumluluk |
|---|---|
| `explorer-tweaks.wh.cpp` | Modun tamamı. Bölüm sırası: Windhawk başlıkları/readme/ayarlar → `// ===== Logic =====` (saf, `ET_LOGIC_ONLY` altında da derlenir) → `#ifndef ET_LOGIC_ONLY` → ayarlar, nav pane, registry, pin hook'u, XAML, `Wh_Mod*` |
| `scripts/build.ps1` | Modu Windhawk'ın clang'ıyla DLL olarak derler (`-Mod`) ya da logic testini derleyip çalıştırır (`-Test`). Hata olursa çıkış kodu 1 |
| `tests/logic_test.cpp` | `#define ET_LOGIC_ONLY` + `#include "../explorer-tweaks.wh.cpp"`. Assert tabanlı küçük bir `main()` |
| `tests/verify-navtree.ps1` | Açık bir Explorer penceresinin sol menüsünü dışarıdan, salt okunur olarak doğrular |
| `tests/verify-hidden.ps1` | Bir CLSID için HKCU `System.IsPinnedToNameSpaceTree` değerini doğrular |
| `tests/verify-xaml.ps1` | UI Automation ile tab ve command bar elemanlarının görünmediğini doğrular |

**Çalışma zamanı doğrulaması (insan adımı):** Modu Explorer'a yüklemek yalnızca Windhawk arayüzünden yapılabilir. Bu adımlarda çalıştıran ajan durur ve kullanıcıdan şunu ister: *Windhawk → "Create a New Mod" (ilk sefer) ya da açık editör → içeriği `explorer-tweaks.wh.cpp` ile değiştir → "Compile Mod"*. Ardından yeni bir Explorer penceresi açılır. Ajan HKLM'ye ya da `ProgramData\Windhawk\Engine` altına kendisi yazmaz.

---

### Task 1: İskelet, derleme betiği, doğrulama betikleri

**Files:**
- Create: `explorer-tweaks.wh.cpp`, `scripts/build.ps1`, `tests/logic_test.cpp`, `tests/verify-navtree.ps1`, `.gitignore` (`build/`)

**Interfaces:**
- Produces:
  - `scripts/build.ps1 -Mod` derleme sonucu `build/explorer-tweaks.dll` üretir.
  - `scripts/build.ps1 -Test` derleme sonucu `build/logic_test.exe` üretir ve çalıştırır.
  - `tests/verify-navtree.ps1 -Expected <string[]> [-Pinned bottom|top|default] [-NoPinIcons]` çıktısı `PASS`/`FAIL: <neden>` olur, çıkış kodu 0 ya da 1.
  - Ayar yapıları: `struct NavRule { std::wstring match, newName; bool hidden; }`, `enum class PinnedPosition { Default, Top, Bottom }`, `struct Settings { bool hideTabs, hideCommandBar, hidePins; PinnedPosition pinnedPosition; std::vector<NavRule> navItems; }`, `Settings g_settings;` ve `void LoadSettings()`.

- [ ] **Step 1:** `git init` çalıştır. `.gitignore` dosyasına `build/` yaz.
- [ ] **Step 2:** `tests/verify-navtree.ps1` dosyasını yaz. Temel olarak `navtree2.ps1`'i (scratchpad) kullan, şu değişikliklerle:
  - Pencereyi `Get-Process explorer | Where MainWindowTitle -like '*File Explorer*'` ile bul. `FindWindow`'a `$null` geçirme.
  - `TVITEMW` offset'leri: `cChildren` 44, `lParam` 48.
  - Kök çocuklarını `{name, pinned}` listesi olarak topla.
  - Kontroller:
    - (a) Pinli olmayanların ilk N tanesi `-Expected` ile sırası ve adıyla birebir aynı mı?
    - (b) `-Pinned bottom` ise pinlilerin hepsi pinli olmayanlardan sonra mı geliyor? `top` ise önce mi?
    - (c) `-NoPinIcons` ise `SendMessage(tree, TVM_GETIMAGELIST=0x1108, TVSIL_STATE=2, 0) == 0` mı?
  - İlk başarısız kontrolde `FAIL: <neden>` yazıp `exit 1`. Hepsi geçerse `PASS`, `exit 0`.
- [ ] **Step 3:** Betiği şu anki (mod yüklenmemiş) Explorer'a karşı çalıştır: `powershell -File tests/verify-navtree.ps1 -Expected 'This PC','Phone','Linux' -Pinned bottom -NoPinIcons`. Beklenen: `FAIL` (sıra yanlış). Bu, betiğin gerçekten bir şey kontrol ettiğini gösterir.
- [ ] **Step 4:** `explorer-tweaks.wh.cpp` iskeletini yaz:
  - Windhawk metadata bloğu (`@id explorer-tweaks`, `@name Explorer Tweaks`, `@version 1.0.0`, `@include explorer.exe`, `@architecture x86-64`, `@compilerOptions` Global Constraints'teki gibi).
  - Kısa bir Türkçe readme.
  - Spec §4'teki ayar bloğu, birebir.
  - `// ===== Logic =====` bölümü: şimdilik yalnızca `NavRule` ve `PinnedPosition`.
  - `#ifndef ET_LOGIC_ONLY` altında: `Settings`, `LoadSettings()` (`navItems[%d].match` gibi indeksli `Wh_GetStringSetting`; `match` boş geldiğinde döngü biter), boş `Wh_ModInit` (`LoadSettings`; `TRUE` döner), `Wh_ModUninit`, `Wh_ModSettingsChanged` (`LoadSettings`).
- [ ] **Step 5:** `scripts/build.ps1` dosyasını yaz:
  - `-Mod`: clang++ çağrılır. Argümanlar sırayla: `compile_flags.txt` satırları, `-I "<Compiler>\include"`, `-shared -O2 -o build\explorer-tweaks.dll explorer-tweaks.wh.cpp`, link bayrakları.
  - `-Test`: `clang++ -x c++ -std=c++23 -target x86_64-w64-mingw32 -DUNICODE -D_UNICODE -static -o build\logic_test.exe tests\logic_test.cpp`, ardından exe çalıştırılır.
  - Derleme ya da test exe'si sıfırdan farklı çıkış kodu verirse betik `exit 1` yapar.
- [ ] **Step 6:** `tests/logic_test.cpp` dosyasını yaz. Bir `CHECK(cond)` makrosu (başarısızlıkta dosya:satır yazar ve sayaç artırır), içi boş bir test listesi ve sayaç 0 değilse `return 1` olan bir `main`.
- [ ] **Step 7:** Önce `scripts/build.ps1 -Mod`, sonra `scripts/build.ps1 -Test` çalıştır. Beklenen: ikisi de çıkış kodu 0 ve `build/explorer-tweaks.dll` oluşmuş.
- [ ] **Step 8:** Commit: `chore: scaffold explorer-tweaks mod, build and verify scripts`.

### Task 2: Sıralama mantığı (saf, test edilebilir)

**Files:**
- Modify: `explorer-tweaks.wh.cpp` (Logic bölümü)
- Test: `tests/logic_test.cpp`

**Interfaces:**
- Consumes: `NavRule`, `PinnedPosition` (Task 1).
- Produces:
  - `struct NavEntry { std::wstring name; bool pinned; };`
  - `bool NamesEqual(std::wstring_view a, std::wstring_view b);`: büyük/küçük harf duyarsız karşılaştırma.
  - `bool IsClsidString(std::wstring_view s);`: `{` ile başlayıp `}` ile biten 38 karakterlik metin.
  - `int FindRule(const std::vector<NavRule>& rules, const std::vector<std::wstring>& effectiveMatch, std::wstring_view name);`: eşleşen ilk kuralın indeksini döner, yoksa -1. `effectiveMatch[i]` değeri kural i'nin isimle eşleşmesi için kullanılır: CLSID kurallarında çözülmüş ad, isim kurallarında `match`'in kendisi, çözülemeyende boş. Boş değerler hiçbir isimle eşleşmez.
  - `std::vector<int> ComputeOrder(const std::vector<NavEntry>& entries, const std::vector<NavRule>& rules, const std::vector<std::wstring>& effectiveMatch, PinnedPosition pos);`: `entries` için yeni sırayı orijinal indeksler cinsinden döner.

- [ ] **Step 1:** `logic_test.cpp` içine şu testleri yaz. Örnek veri: entries = `[Downloads*, Documents*, MyPhone, This PC, Linux]` (`*` pinli), kurallar = `This PC`, `{3611…}`, `Linux`, `effectiveMatch` = `{L"This PC", L"MyPhone", L"Linux"}`.
  - `test_bottom`: `ComputeOrder(..., Bottom)` sonucu `{3,2,4,0,1}`.
  - `test_top`: `Top` sonucu `{0,1,3,2,4}`.
  - `test_default_keeps_pinned_slots`: `Default` ile sonuç `{0,1,3,2,4}`. Pinliler 0 ve 1 numaralı yerlerinde kalır; pinli olmayanlar kalan yerlere kural sırasıyla yerleşir.
  - `test_unlisted_after_listed`: entries'e pinli olmayan `Network` (indeks 5) eklenir. `Bottom` ile sonuç `{3,2,4,5,0,1}`.
  - `test_unresolved_rule_ignored`: `effectiveMatch[1] = L""` (telefon bağlı değil). `Bottom` ile sonuç `{3,4,2,0,1}`. MyPhone listesiz sayılır ve listelilerden sonra gelir.
  - `test_duplicate_and_empty_rules`: kurallara boş `match`'li bir kural ve ikinci bir `This PC` kuralı eklenir. `FindRule(..., L"this pc") == 0`, sonuç `test_bottom` ile aynı.
  - `test_is_clsid`: `IsClsidString(L"{3611C374-393E-41F8-98EB-1AD7BF051346}")` true, `IsClsidString(L"Linux")` false.
- [ ] **Step 2:** `scripts/build.ps1 -Test` çalıştır. Beklenen: derleme hatası ya da FAIL (fonksiyonlar henüz yok).
- [ ] **Step 3:** Fonksiyonları Logic bölümünde yaz. `ComputeOrder` algoritması:
  - Pinli olmayanlar `(ruleIndex == -1 ? INT_MAX : ruleIndex, origIndex)` anahtarına göre kararlı biçimde sıralanır.
  - `Bottom`: sonuç = sıralı pinli olmayanlar + pinliler (orijinal sırayla).
  - `Top`: sonuç = pinliler + sıralı pinli olmayanlar.
  - `Default`: orijinal dizi üzerinde yürünür. Pinli slotlarda aynı öğe kalır, pinli olmayan slotlar sıralı pinli olmayanlarla sırayla doldurulur.
  - `NamesEqual`, `CompareStringOrdinal(..., TRUE) == CSTR_EQUAL` kullanır.
- [ ] **Step 4:** `scripts/build.ps1 -Test` çalıştır. Beklenen: 7 test PASS, çıkış kodu 0.
- [ ] **Step 5:** Commit: `feat: nav pane ordering logic with tests`.

### Task 3: Nav pane subclass ve canlı sıralama

**Files:**
- Modify: `explorer-tweaks.wh.cpp` (`#ifndef ET_LOGIC_ONLY` bölümü)

**Interfaces:**
- Consumes: `ComputeOrder`, `FindRule`, `NavEntry`, `g_settings`.
- Produces:
  - `std::vector<std::wstring> g_effectiveMatch;` ve `void ResolveRules();`. Bu task'ta isim kuralları için `match` atanır, CLSID kuralları için boş bırakılır. Task 4 bunu doldurur.
  - `void ForEachNavTree(void (*fn)(HWND));`: mevcut pencerelerdeki ağaçları dolaşır.
  - `void ResortTree(HWND tree);`: ağacın kendi thread'inde çağrılmalıdır.
  - `void RunOnWindowThread(HWND hwnd, std::function<void()> fn);`: senkron çalışır. Hedef thread'e `WH_CALLWNDPROC` hook'u kurar, kayıtlı `g_runMsg` mesajını gönderir ve hook'u kaldırır.
  - `UINT g_runMsg = RegisterWindowMessageW(L"ExplorerTweaks_Run");`
  - `std::unordered_set<HWND> g_trees;`: kritik bölümle korunur.

- [ ] **Step 1:** `CreateWindowExW` hook'unu yaz. Orijinal çağrıdan sonra, sınıf adı `SysTreeView32` ve ebeveynin sınıfı `NamespaceTreeControl` ise `AttachTree(hwnd)` çağrılır. Bu kod zaten pencereyi oluşturan thread'de çalıştığı için doğrudan `SetWindowSubclass` kullanılabilir.
- [ ] **Step 2:** `AttachTree` ve `TreeSubclassProc` fonksiyonlarını yaz:
  - Subclass ID'si `0xE7`. Ağaç `g_trees`'e eklenir.
  - `TreeSubclassProc` içinde, `g_sorting` false iken:
    - `TVM_INSERTITEMW` gelirse ve yeni öğenin ebeveyni kök ise (`TVM_GETNEXTITEM, TVGN_ROOT`), `SetTimer(tree, 0xE7, 50, nullptr)` çağrılır.
    - `TVM_DELETEITEM` gelirse aynı zamanlayıcı kurulur.
    - `TVM_SETITEMW` ile durum değişirse (pin değişikliği) yine aynı zamanlayıcı kurulur.
  - `WM_TIMER 0xE7` gelince zamanlayıcı öldürülür ve `ResortTree` çağrılır.
  - `WM_NCDESTROY` gelince subclass kaldırılır ve ağaç `g_trees`'ten silinir.
- [ ] **Step 3:** `ResortTree` fonksiyonunu yaz:
  1. Kök çocukları `TVM_GETITEMW` ile gezilir. `TVIF_TEXT | TVIF_STATE | TVIF_PARAM` istenir, `stateMask = TVIS_STATEIMAGEMASK`.
  2. Her öğe için `NavEntry` oluşturulur ve `lParam` değeri bir vektörde saklanır.
  3. `ComputeOrder` çağrılır. Sonuçtan `lParam → hedef indeks` eşlemesi (`std::unordered_map`) kurulur.
  4. `g_sorting = true` yapılır. `TVM_SORTCHILDRENCB` çağrılır: `TVSORTCB{ hParent=root, lpfnCompare, lParam=&map }`. Karşılaştırıcı `map[l1] - map[l2]` döner.
  5. `g_sorting = false` yapılır.
  6. Sıra zaten doğruysa (`order[i] == i`) sıralama mesajı hiç gönderilmez.
- [ ] **Step 4:** Mevcut pencereleri işle:
  - `Wh_ModInit` içinde (ya da `Wh_ModAfterInit` varsa orada) `ForEachNavTree` ile `CabinetWClass` pencereleri `EnumWindows` + `EnumChildWindows` ile dolaşılır. Her ağaç için `RunOnWindowThread(tree, [&]{ AttachTree(tree); ResortTree(tree); })` çağrılır.
  - `Wh_ModSettingsChanged` içinde aynı şekilde ama yalnızca `ResortTree` çağrılır.
- [ ] **Step 5:** `scripts/build.ps1 -Mod` çalıştır. Beklenen: çıkış kodu 0.
- [ ] **Step 6:** İnsan adımı: kullanıcı modu Windhawk'ta derler. Ayarlarda `navItems[1].match` geçici olarak `Ayşe adlı kişiye ait Telefon` yapılır (CLSID çözümü Task 4'te gelecek). Ardından:
  1. Mod derlenmeden önce açılmış bir pencerede şunu çalıştır: `tests/verify-navtree.ps1 -Expected 'This PC','Ayşe adlı kişiye ait Telefon','Linux' -Pinned bottom`. Beklenen: `PASS`.
  2. Yeni bir pencerede aynısını çalıştır. Beklenen: `PASS`.
  3. Herhangi bir klasörü Quick Access'e pinle ve 1 saniye bekle, betiği tekrar çalıştır. Beklenen: `PASS` (yeni pin en altta).
  4. Pinlenen klasörü geri çöz.
- [ ] **Step 7:** Commit: `feat: live nav pane reordering via tree subclass`.

### Task 4: Yeniden adlandırma ve CLSID çözümleme

**Files:**
- Modify: `explorer-tweaks.wh.cpp`

**Interfaces:**
- Consumes: `g_effectiveMatch`, `FindRule`, `TreeSubclassProc`, `RunOnWindowThread`.
- Produces:
  - `std::wstring ResolveClsidDisplayName(std::wstring_view clsid);`: `SHParseDisplayName(L"shell:::" + clsid)` ve `SHGetNameFromIDList(SIGDN_NORMALDISPLAY)` kullanır. Hata olursa `L""` döner ve `Wh_Log` yazar.
  - `ResolveRules()` artık CLSID kuralları için bu adı kullanır.
  - `std::unordered_map<HTREEITEM, std::wstring> g_originalNames;`: yalnızca yeniden adlandırılan öğeler için tutulur.
  - `std::wstring_view OriginalName(HTREEITEM, std::wstring_view shown);`: `ResortTree` bunu kullanır, böylece sıralama orijinal ada göre yapılır.

- [ ] **Step 1:** `ResolveClsidDisplayName` fonksiyonunu yaz ve `ResolveRules` içine bağla. `Wh_ModInit` ve `Wh_ModSettingsChanged` içinde COM thread'i olmadığı için çağrı `CoInitializeEx(COINIT_APARTMENTTHREADED)` + `CoUninitialize` arasına alınır.
- [ ] **Step 2:** `TreeSubclassProc`'a yeniden adlandırmayı ekle:
  - `TVM_INSERTITEMW` ve `TVM_SETITEMW` mesajlarında `TVIF_TEXT` varsa ve metin `LPSTR_TEXTCALLBACKW` değilse:
    - `FindRule` ile kural bulunur. Kuralın `newName`'i doluysa yerel bir `TVINSERTSTRUCTW`/`TVITEMW` kopyası alınır, `pszText` yeni adla değiştirilir ve mesaj bu kopyayla iletilir.
    - Orijinal ad `g_originalNames`'e yazılır. `TVM_INSERTITEMW` için anahtar, dönen `HTREEITEM` olur.
  - `TVM_SETITEMW` ile gelen metin zaten bizim yeni adımızsa olduğu gibi bırakılır. Bu, Review Focus 5'i karşılar.
  - Metin `LPSTR_TEXTCALLBACKW` ise ebeveyn (`NamespaceTreeControl`) de subclass edilir (ID `0xE8`). Orada `WM_NOTIFY / TVN_GETDISPINFOW` cevabındaki `pszText` aynı kurala göre değiştirilir.
- [ ] **Step 3:** Kural değişince isimler canlı güncellensin diye `ResortTree`'den önce `ReapplyNames(tree)` çağrılır. Bu fonksiyon kök çocuklarını gezer ve gerekiyorsa `TVM_SETITEMW` ile metni yeni ada ya da orijinal ada çevirir.
- [ ] **Step 4:** `scripts/build.ps1 -Mod` çalıştır. Beklenen: çıkış kodu 0.
- [ ] **Step 5:** İnsan adımı: kullanıcı modu yeniden derler ve `navItems[1].match` varsayılan CLSID değerine döner. Ardından:
  1. `verify-navtree.ps1 -Expected 'This PC','Phone','Linux' -Pinned bottom` çalıştır. Beklenen: `PASS`.
  2. Telefon bölümüne tıkla (Explorer öğeyi yeniler), betiği tekrar çalıştır. Beklenen: `PASS`.
  3. Ayarlarda `newName`'i `Telefon` yap. `-Expected 'This PC','Telefon','Linux'` ile çalıştır, yeni pencere açmadan. Beklenen: `PASS`.
- [ ] **Step 6:** Commit: `feat: rename nav pane items, resolve CLSID rules`.

### Task 5: Registry ile gizleme

**Files:**
- Modify: `explorer-tweaks.wh.cpp`
- Create: `tests/verify-hidden.ps1`

**Interfaces:**
- Consumes: `g_settings.navItems`, `IsClsidString`, `ResolveClsidDisplayName`.
- Produces:
  - `std::wstring FindClsidByName(std::wstring_view name);`: aday CLSID'ler `HKLM` ve `HKCU` altındaki `Software\Microsoft\Windows\CurrentVersion\Explorer\Desktop\NameSpace` alt anahtarları ile şu sabitlerdir: This PC `{20D04FE0-3AEA-1069-A2D8-08002B30309D}`, Network `{F02C1A0D-BE21-4350-88B0-7367FC96EF3C}`, Home `{f874310e-b6b7-47dc-bc84-b9e6b38f5903}`, Gallery `{e88865ea-0e1c-4e20-9aa6-edcd0212c87c}`. Her adayın adı `ResolveClsidDisplayName` ile alınır ve `NamesEqual` ile karşılaştırılır.
  - `void ApplyHidden();`: ayarlara göre gizler ya da geri yükler; aşağıdaki algoritmaya bakın.
  - `void RestoreAllHidden();`: bütün `backup/*` kayıtlarını geri yükler.
- `tests/verify-hidden.ps1 -Clsid <guid> -Expect 0|absent`: `PASS`/`FAIL` yazar.

- [ ] **Step 1:** `tests/verify-hidden.ps1` dosyasını yaz. `HKCU:\Software\Classes\CLSID\<guid>` altındaki `System.IsPinnedToNameSpaceTree` değeri okunur: değer yoksa `absent`, varsa sayısal değeri.
- [ ] **Step 2:** Betiği Gallery için çalıştır: `-Clsid '{e88865ea-0e1c-4e20-9aa6-edcd0212c87c}' -Expect 0`. Beklenen sonuç mevcut duruma bağlı. Değeri not et; Step 6'daki geri yükleme kontrolünde kullanılacak.
- [ ] **Step 3:** `ApplyHidden` fonksiyonunu yaz:
  1. Hedef küme belirlenir: `hidden: true` olan kuralların CLSID'leri (doğrudan CLSID ya da `FindClsidByName` ile bulunan).
  2. Kümede olup yedeği olmayan her CLSID için: mevcut değer okunur, `backup/{CLSID}` yazılır, sonra değer `0` yazılır.
  3. Yedeği olup kümede olmayan her CLSID için: yedek geri yazılır (`absent` ise değer silinir) ve yedek kaydı `Wh_DeleteValue` ile silinir.
  4. Değişiklik olduysa `SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr)` çağrılır.

  Mevcut yedeklerin listesi `Wh_GetStringValue(L"backupList")` içinde `;` ile ayrılmış CLSID'ler olarak tutulur. Windhawk storage numaralandırma sunmadığı için bu liste gereklidir.
- [ ] **Step 4:** `ApplyHidden` fonksiyonu `Wh_ModInit` ve `Wh_ModSettingsChanged` içinde, `RestoreAllHidden` ise `Wh_ModUninit` içinde çağrılır. `scripts/build.ps1 -Mod` çalıştır. Beklenen: çıkış kodu 0.
- [ ] **Step 5:** İnsan adımı: kullanıcı modu derler ve listeye `match: "{e88865ea-0e1c-4e20-9aa6-edcd0212c87c}", hidden: true` kuralını ekler. Ardından `verify-hidden.ps1 -Clsid '{e88865ea-…}' -Expect 0` çalıştır. Beklenen: `PASS`.
- [ ] **Step 6:** Kural ayarlardan silinir. `verify-hidden.ps1`, Step 2'de not edilen değerle çalıştırılır. Beklenen: `PASS`.
- [ ] **Step 7:** Commit: `feat: hide nav pane items via HKCU registry with backup/restore`.

### Task 6: Pin ikonlarını kaldırma

**Files:**
- Modify: `explorer-tweaks.wh.cpp`

**Interfaces:**
- Consumes: `g_settings.hidePins`, `g_trees`, `RunOnWindowThread`.
- Produces: `CNscTree_SetStateImageList_hook`.

- [ ] **Step 1:** `verify-navtree.ps1 -Expected 'This PC','Phone','Linux' -Pinned bottom -NoPinIcons` çalıştır. Beklenen: `FAIL: state image list present`.
- [ ] **Step 2:** Hook'u yaz:
  - `WindhawkUtils::HookSymbols` ile `ExplorerFrame.dll` içindeki `CNscTree::SetStateImageList` hook'lanır. Beklenen sembol metni: `public: virtual long __cdecl CNscTree::SetStateImageList(struct _IMAGELIST *)`. Bu tahmin bir doğrulama gerektirir: metni `ramensoftware/windhawk-mods` deposundaki `mods/aerexplorer.wh.cpp` dosyasında geçen `CNscTree_SetStateImageList` sembol tanımıyla karşılaştır ve fark varsa oradakini kullan.
  - Hook, `g_settings.hidePins` true ise `S_OK` döner; değilse orijinal fonksiyonu çağırır.
  - Sembol bulunamazsa `Wh_Log(L"pin hook unavailable")` yazılır ve init başarısız sayılmaz.
- [ ] **Step 3:** Açık ağaçlarda ve ayar değişince: `hidePins` true ise `RunOnWindowThread` ile `TreeView_SetImageList(tree, nullptr, TVSIL_STATE)` çağrılır. False olursa eski image list geri konur; bunun için ilk kaldırırken liste ağaç başına `g_savedStateLists`'te saklanır.
- [ ] **Step 4:** `scripts/build.ps1 -Mod` çalıştır. Ardından insan adımı: kullanıcı derler ve yeni pencere açar. Step 1'deki komutu çalıştır. Beklenen: `PASS`. Gözle kontrol: pinler görünmüyor ve pinliler hâlâ en altta (stateImg korunuyor).
- [ ] **Step 5:** Commit: `feat: remove Quick Access pin icons`.

### Task 7: Tab ve toolbar gizleme (XAML), kapatma güvenliği

**Files:**
- Modify: `explorer-tweaks.wh.cpp`
- Create: `tests/verify-xaml.ps1`

**Interfaces:**
- Consumes: `g_settings.hideTabs`, `g_settings.hideCommandBar`.
- Produces:
  - `class VisualTreeWatcher : IVisualTreeServiceCallback2`: `OnVisualTreeChange` içinde eleman tipine bakar.
  - TAP: `IObjectWithSite` uygulayan bir sınıf ve onun `DllGetClassObject`'i. Bu, Windhawk XAML modlarının standart kalıbıdır. Yükleme `InitializeXamlDiagnosticsEx(L"VisualDiagConnection1", GetCurrentProcessId(), nullptr, <bu DLL'in yolu>, CLSID_ExplorerTweaksTap, nullptr)` ile yapılır. CLSID için yeni bir GUID üretilir.
  - `void SetCollapsed(FrameworkElement el, bool collapsed);`: önceki `Visibility` değeri `g_collapsed` listesinde saklanır.
  - `void RestoreXaml();`

- [ ] **Step 1:** Salt okunur keşif. Mod yüklüyken, yeni bir Explorer penceresinde UIA ile pencerenin altındaki bütün elemanlar dökülür (ClassName, AutomationId, ControlType). Amaç, tab satırı ile command bar'ın UIA kimliklerini öğrenmektir. Beklenen adaylar: `ControlType.Tab`/`TabItem` ve `FileExplorerCommandBar`. Bulunan kimlikler `tests/verify-xaml.ps1` içine sabit olarak yazılır.
- [ ] **Step 2:** `tests/verify-xaml.ps1` dosyasını yaz. Kontrol: bu kimliklerle eşleşen ve `IsOffscreen == false` olan hiçbir eleman yoksa `PASS`. Betiği şimdi çalıştır. Beklenen: `FAIL` (tab ve toolbar görünüyor).
- [ ] **Step 3:** XAML parçasını yaz ve kapatma yolunu sağlamlaştır:
  1. TAP enjeksiyonu bir `CabinetWClass` penceresi oluştuğunda yapılır. `CreateWindowExW` hook'unda sınıf `CabinetWClass` ise ve süreç başına daha önce yapılmadıysa, XAML hazır olduktan sonra enjekte edilir. Mod açıldığında zaten açık pencere varsa enjeksiyon hemen yapılır.
  2. `OnVisualTreeChange` içinde:
     - `hideCommandBar` ise `FileExplorerExtensions.CommandBarControl` tipi `Collapsed` yapılır.
     - `hideTabs` ise `Microsoft.UI.Xaml.Controls.TabView` öğesinin, title bar'daki satırı taşıyan atası `Collapsed` yapılır. Bu ata Step 1 keşfinde `VisualTreeHelper` ile belirlenir. Caption düğmeleri ve sürükleme alanı adres satırının üstünde kalacak şekilde satır yüksekliği `0` yapılır.
  3. `Wh_ModUninit` sırası: `RestoreXaml` → `RestoreAllHidden` → her ağaç için `RunOnWindowThread` ile zamanlayıcıyı öldür + subclass'ları kaldır + state image list'i geri koy. Bu, Review Focus 4'ü karşılar.
- [ ] **Step 4:** `scripts/build.ps1 -Mod` çalıştır. Ardından insan adımı: kullanıcı derler ve yeni pencere açar.
  1. `tests/verify-xaml.ps1` çalıştır. Beklenen: `PASS`.
  2. Gözle kontrol: pencere başlıktan sürüklenebiliyor; küçült/büyüt/kapat düğmeleri tıklanabiliyor ve adres çubuğuyla çakışmıyor.
- [ ] **Step 5:** Kapatma testi:
  1. İki Explorer penceresi açıkken modu Windhawk'ta devre dışı bırak.
  2. Beklenen: Explorer çökmez (`Get-Process explorer` aynı PID'leri gösterir).
  3. `verify-hidden.ps1` yedek değerini döndürür.
  4. F5 sonrası pin ikonları geri gelir.
- [ ] **Step 6:** Commit: `feat: hide tabs and command bar via XAML diagnostics; safe uninit`.

### Task 8: Son kontrol ve README

**Files:**
- Modify: `explorer-tweaks.wh.cpp` (readme bölümü)

- [ ] **Step 1:** Readme'ye şunları yaz: özellikler, ayar örneği, bilinen kısıtlar (spec §3.1 kısıtı ve §7 kapsam dışı maddeleri) ve Phone CLSID'sinin her kullanıcıda farklı olabileceğine dair bir not.
- [ ] **Step 2:** Spec §6'daki elle test listesinin 1-9 numaralı maddeleri kullanıcıyla birlikte baştan sona yürütülür. Sonuçlar commit mesajına yazılır.
- [ ] **Step 3:** `scripts/build.ps1 -Mod` ve `scripts/build.ps1 -Test` çalıştır. Beklenen: ikisi de çıkış kodu 0.
- [ ] **Step 4:** Commit: `docs: readme and final verification`.
