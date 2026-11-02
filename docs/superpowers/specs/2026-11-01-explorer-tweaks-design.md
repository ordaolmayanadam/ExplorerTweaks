# Explorer Tweaks — Windhawk Mod Tasarımı

Tarih: 2026-11-01
Durum: Taslak (kullanıcı incelemesinde)

## 1. Amaç

Windows 11 File Explorer'ı sadeleştiren, tek başına çalışan bir Windhawk modu. Başka modlara (File Explorer Styler, Aerexplorer) bağımlı değil.

Mod şunları yapar:

1. Tab satırını gizler.
2. Toolbar'ı (command bar) gizler.
3. Quick Access'teki pinli öğelerin sağındaki pin ikonlarını kaldırır.
4. Sol menüyü (navigation pane) Windhawk ayarlarından düzenlenebilir kılar: sıralama, yeniden adlandırma, gizleme ve pinli klasörlerin yeri.

Hedef sol menü düzeni (varsayılan ayarlar):

```
This PC
Phone            (asıl adı: "Ayşe adlı kişiye ait Telefon")
Linux
<pinli Quick Access klasörleri, düz liste>
```

### Başarı ölçütleri

- Yeni açılan bir Explorer penceresinde tab satırı ve toolbar görünmez. Adres çubuğu, pencere düğmeleri ve sürükleme alanı kullanılabilir kalır.
- Pinli öğelerde pin ikonu görünmez.
- Sol menü ayarlardaki sırayla gelir, pinli klasörler en alttadır. Yeni bir klasör pinlendiğinde o da en alta gider.
- Ayarlar Windhawk ayar ekranından değiştirilebilir. Sıra ve ad değişiklikleri açık pencerelere hemen uygulanır.
- Mod kapatıldığında registry değişiklikleri geri alınır.

## 2. Bulgular (keşif)

Explorer'ın sol menüsü `SysTreeView32` üzerinde çalışan bir `CNscTree`'dir. Salt okunur bir denemeyle (`navtree2.ps1`) şu yapı görüldü:

```
(gizli kök: Desktop)
├─ Downloads, Documents, Pictures, Music, Videos, Recycle Bin, Google Drive (G:)   state image = 1 (pin)
├─ Ayşe adlı kişiye ait Telefon                                                state image = 0
├─ This PC                                                                        state image = 0
└─ Linux                                                                          state image = 0
```

- Pinli Quick Access öğeleri This PC ile **aynı ebeveynin** (gizli Desktop kökü) çocuklarıdır. `TVIS_STATEIMAGEMASK` değeri 1 olduğu için kesin olarak tanınırlar. Pin ikonu gizlense de bu değer kalır, çünkü yalnızca image list kaldırılır.
- İlgili CLSID'ler:
  - Phone (CrossDevice): `{3611C374-393E-41F8-98EB-1AD7BF051346}` (HKCU)
  - Linux: `{B2B4A4D1-2754-4140-A2EB-9A76D9D7CDC6}`
  - Home: `{f874310e-b6b7-47dc-bc84-b9e6b38f5903}`
  - Gallery: `{e88865ea-0e1c-4e20-9aa6-edcd0212c87c}`
  - This PC: `{20D04FE0-3AEA-1069-A2D8-08002B30309D}`
  - Network: `{F02C1A0D-BE21-4350-88B0-7367FC96EF3C}`
- Aerexplorer pin ikonlarını `CNscTree::SetStateImageList` fonksiyonunu hook'layıp `S_OK` döndürerek kaldırıyor.
- File Explorer Styler tab ve toolbar'ı XAML Diagnostics ile stil enjekte ederek gizliyor. Hedefleri `TabViewItem`, `FileExplorerExtensions.CommandBarControl` ve `CommandBar#FileExplorerCommandBar`.

## 3. Mimari

Proje tek bir dosyadan oluşur: `explorer-tweaks.wh.cpp`. Hedef süreç `explorer.exe`. Dört bağımsız parça var ve her biri ayrı bir ayarla açılıp kapatılır.

| Parça | Yöntem | Ne zaman etkili olur |
|---|---|---|
| Tabları gizle | XAML Diagnostics (`InitializeXamlDiagnosticsEx` + `IVisualTreeServiceCallback2`). `TabView` satırı `Visibility=Collapsed` yapılır, title bar yüksekliği ve caption alanı düzeltilir | Yeni pencerelerde |
| Toolbar'ı gizle | Aynı XAML altyapısı: `FileExplorerExtensions.CommandBarControl` → `Collapsed` | Yeni pencerelerde |
| Pin ikonlarını kaldır | Ağaç subclass'ı `TVM_SETIMAGELIST(TVSIL_STATE)` mesajını yakalar: ayar açıkken gelen liste saklanır, ağaca kurulmaz; kapanınca saklanan liste geri konur (2026-11-02 değişikliği; ilk tasarımdaki `CNscTree::SetStateImageList` sembol hook'u kaldırıldı) | Anında |
| Sol menü: sıra, ad, pinlilerin yeri | Nav pane ağacının subclass'ı | Anında |
| Sol menü: ayraç rengi (yalnızca açık tema) | `CNscTree::DrawDivider` (ExplorerFrame.dll) sembol hook'u thread-local bayrak kurar; `CreatePen` hook'u bayrak varken kalem rengini #F7F7F7 (dikey ayraçla aynı) yapar. Koyu temada Windows rengi kalır; tema `AppsUseLightTheme` değerinden okunur (2 sn önbellek) | Anında |
| Sol menü: seçim/hover vurgusu (yalnızca açık tema) | Nav ağacının WM_PAINT/WM_PRINTCLIENT işlenmesi sürerken thread-local ağaç tutamacı kurulur; uxtheme `DrawThemeBackground(Ex)` hook'u TVP_TREEITEM'in hover/seçili durumlarında (TREIS_HOT, SELECTED, SELECTEDNOTFOCUS, HOTSELECTED) kenarlıksız, yuvarlak köşeli, yatayda içeri girintili ve daha soluk bir dolgu çizer. Koyu temada ve ağaç dışı çağrılarda orijinale geçer | Anında |
| Sol menü: gizleme | `HKCU\Software\Classes\CLSID\{…}\System.IsPinnedToNameSpaceTree = 0`, yedekle ve geri yükle | Explorer yeniden açılınca |
| Ayrıntılar görünümü: sütun başlığı alt çizgisi (yalnızca açık tema) | Dosya listesi DirectUI olduğundan SysHeader32 yoktur; başlık öğeleri uxtheme "Header" sınıfıyla (HP_HEADERITEM) `DrawThemeBackground(Ex)`'e gelir. `OpenThemeData*` hook'ları "Header" tutamaçlarını kaydeder (`CloseThemeData`'da silinir); kayıtlı tutamaçla gelen HP_HEADERITEM çiziminden sonra öğenin alt kenarına 1 px #E5E5E5 çizgi çizilir | Yeni pencerelerde |
| Dosya listesi ve status bar çizgileri (yalnızca açık tema) | Dış DirectUIHWND (ebeveyni `DUIViewWndClassName`) ve dosya listesi DirectUIHWND'si (ebeveyni `SHELLDLL_DefView`) subclass edilmez: DirectUI oluşturmadan sonra kendi pencere yordamını kurup comctl32 subclass zincirini atladığı için subclass WM_PAINT'i görmez. Bunun yerine user32 `BeginPaint`/`EndPaint` süreç genelinde kancalanır (diğer pencereler yalnızca sınıf adı kontrolüyle geçer). `BeginPaint` dosya listesinin `rcPaint`'ini thread'e özgü bir boyama yığınına koyar; `EndPaint` orijinalden sonra `GetDCEx` ile 1 px #E5E5E5 çizilir: dış pencerede görünür `CtrlNotifySink` çocuklarının en alt kenarında tam genişlikte yatay çizgi (status bar'ın üstü; çocuklar alta kadar iniyorsa çizilmez); dosya listesinde son sütun başlığının sağ kenarı hizasında (x = right − 1) başlığın altından pencerenin altına dikey çizgi. Başlık geometrisi, dosya listesi boyanırken kayıtlı "Header" tutamacıyla gelen HP_HEADERITEM dikdörtgenlerinden (LPtoDP; bellek DC'sinde `rcPaint` kökeni eklenerek) toplanır; kısmi boyamalarda son bilinen değer korunur. Pencere başına durum HWND anahtarlı, kilitli bir haritadadır; ölü pencereler `IsWindow` ile ayıklanır | Status bar çizgisi anında; sağ çizgi yeni pencerelerde |

### 3.1 Nav pane subclass

- **Ağacı bulma:**
  - `CreateWindowExW` hook'lanır. Sınıfı `SysTreeView32` ve ebeveyni `NamespaceTreeControl` olan pencereler yakalanır.
  - Mod başlarken mevcut `CabinetWClass` pencereleri de taranır.
- **Subclass kurma:** `SetWindowSubclass` ağacın kendi thread'inde çağrılmalıdır. Başka bir thread'den kurmak için hedef thread'e mesaj gönderen bir yardımcı kullanılır.
- **Yeniden adlandırma:**
  - `TVM_INSERTITEMW` ve `TVM_SETITEMW` mesajlarında `pszText` bir eşleşmeye karşılık geliyorsa yeni adla değiştirilir.
  - Metin `LPSTR_TEXTCALLBACKW` ise ebeveyne gelen `TVN_GETDISPINFOW` cevabı düzeltilir. Bunun için `NamespaceTreeControl` de subclass edilir.
  - Eşleştirme her zaman orijinal ada göre yapılır. Bunun için yeni ad → orijinal ad eşlemesi `HTREEITEM` bazında tutulur.
- **Sıralama:**
  - Kökün (Desktop) altına öğe eklendiğinde ya da öğe silindiğinde 50 ms'lik bir zamanlayıcı (`SetTimer`) yeniden başlatılır. Süre dolunca tek bir `TVM_SORTCHILDRENCB` çağrılır.
  - Karşılaştırıcı her öğe için bir anahtar hesaplar:
    1. `navItems` listesindeki indeks. Listede yoksa listeden sonra gelir.
    2. Pinli öğeler (state image = 1), `pinnedPosition` ayarına göre en başa ya da en sona konur. `default` seçiliyse pinliler yerinde kalır.
    3. Eşit anahtarlarda orijinal sıra korunur (kararlı sıralama). Sıralama öncesi her öğenin indeksi bir haritaya yazılır, eşitlikte bu indeks kullanılır.
  - Kendi sıralamamızın tetiklediği mesajlar yeniden sıralamaya yol açmasın diye yeniden giriş koruması (`g_sorting`) vardır.
  - Sıralamadan hemen sonra öğe yüksekliği mevcut değerine yeniden ayarlanır (comctl32'yi öğe konumlarını yeniden hesaplamaya zorlar) ve ağacın ilk görünür öğesi (`TVGN_FIRSTVISIBLE`) yeniden kurulur. Ancak CNscTree sıralamadan sonra da ağaç işlemleri yapar (çocuk ekleme, `TVM_EXPAND`, `TVM_SELECTITEM`, `TVM_ENSUREVISIBLE`) ve comctl32'nin görünüm durumu yeniden bayatlar: ilk görünür öğe sıralama öncesi öğe (ör. "Downloads") kalır, dikey kaydırma aralığı 0 olur ve sol menü boş çizilir (2026-11-02 çalışma zamanı bulgusu).
  - Bu yüzden sıralama gerçekten yapıldığında ağaç durumunda `reordered` işaretlenir ve ertelenmiş, kendini denetleyen bir görünüm onarımı (`FixTreeView`) kurulur: `TVM_INSERTITEMW`, `TVM_DELETEITEM`, `TVM_EXPAND`, `TVM_SELECTITEM`, `TVM_ENSUREVISIBLE`, `TVM_SORTCHILDREN`, `TVM_SORTCHILDRENCB` ve `TVM_SETITEMW` iletildikten sonra 150 ms'lik zamanlayıcı (`kViewFixTimerId`, debounce) yeniden kurulur; kendi işlemlerimiz sürerken (`g_sorting`) kurulmaz. Zamanlayıcı dolunca ağaç durulmuş sayılır ve denetlenir: kullanıcı kaydırdıysa (dikey kaydırma konumu > alt sınır) ya da ilk görünür öğe gizli kök / kökün ilk çocuğu ise (ya da öğe yoksa) hiçbir şey yapılmaz (`NeedsViewFix`). Aksi halde (yeniden çizim etkinse; `WS_VISIBLE` bitine bakılır) `WM_SETREDRAW` FALSE/TRUE gönderilerek comctl32'ye öğe konumları yeniden hesaplatılır (öğe yüksekliği değil), kökün ilk çocuğu ilk görünür yapılır ve ağaç yeniden çizilir; yeniden çizim kapalıysa (CNscTree toplu işlemde) seçim yapılmaz ve zamanlayıcı yeniden kurulur. `WM_SETREDRAW` zamanlayıcıyı yeniden kurmaz; ağaç başına bir kez günlüğe yazılır.
- **Bilinen kısıt:** Mod kapatıldığında açık pencerelerin orijinal sırası ancak yenilenince (F5 veya pencereyi yeniden açma) geri gelir.

### 3.2 CLSID çözümleme

`match` değeri `{…}` biçimindeyse:

1. `SHParseDisplayName(L"shell:::{CLSID}")` çağrılır.
2. Ardından `SHGetNameFromIDList(SIGDN_NORMALDISPLAY)` ile o anki görünen ad okunur.
3. Eşleştirme bu ad üzerinden yapılır.

`match` bir isimse ve `hidden: true` ise CLSID ters yönde bulunur. Bunun için `HKLM` ve `HKCU` altındaki `…\Explorer\Desktop\NameSpace` CLSID'leri ile bilinen kök CLSID'leri (This PC, Network, Home, Gallery) taranır ve her birinin görünen adı karşılaştırılır. Bulunamazsa `Wh_Log` ile uyarı yazılır ve öğe gizlenmez.

### 3.3 Registry ile gizleme

- **Uygulama:** `hidden: true` olan her CLSID için, `HKCU\Software\Classes\CLSID\{…}` altındaki `System.IsPinnedToNameSpaceTree` değerinin önceki durumu (yok / DWORD değeri) önce `Wh_SetStringValue("backup/{CLSID}", …)` ile kaydedilir, sonra değer `0` yazılır.
- **Geri alma:** Ayar kaldırıldığında ya da mod kapatıldığında (`Wh_ModUninit`) yedek geri yazılır ve yedek kaydı silinir. Değer önceden yoksa silinir.
- **Kalıcılık:** Yedekler Windhawk'ın mod depolamasında tutulur, yani Explorer çökse bile kaybolmaz. `Wh_ModInit` sırasında bir yedek bulunur ama ayarlarda o öğe artık gizli değilse yedek geri yüklenir.
- **Bildirim:** Değişiklikten sonra `SHChangeNotify(SHCNE_ASSOCCHANGED, …)` çağrılır. Yine de değişiklik genelde yeni pencerelerde görünür.

### 3.4 XAML (tab ve toolbar)

- Windhawk XAML mod kalıbı kullanılır: `InitializeXamlDiagnosticsEx` ile kendi DLL'imizdeki bir TAP (`IObjectWithSite` + `IVisualTreeServiceCallback2`) yüklenir.
- `OnVisualTreeChange` geldiğinde:
  - Tip adı `Microsoft.UI.Xaml.Controls.TabView` olan ve Explorer title bar'ına ait eleman (ya da onu içeren satır) bulunursa `Visibility=Collapsed` yapılır.
  - `FileExplorerExtensions.CommandBarControl` bulunursa aynı şekilde `Collapsed` yapılır.
- Tab satırı gizlenince caption düğmeleri ile sürükleme alanının adres çubuğuyla çakışmaması gerekir. Gerekli satır yüksekliği ve margin ayarları, Styler'ın `NoCommandBar` ve tabsız stillerindeki değerler örnek alınarak uygulanır. Uygulama sırasında elle doğrulanacak.
- Explorer'da `TabView` yoksa (eski build) hiçbir şey yapılmaz.

## 4. Ayarlar

```yaml
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
- subtleDividers: true
  $name: Sol menü ayraçlarını sadeleştir
  $description: Yatay ayraçları sağdaki dikey ayraçla aynı renkte (açık temada) çizer.
- softSelection: true
  $name: Sol menü seçimini yumuşat
  $description: Açık temada sol menüdeki seçili/üzerine gelinen öğe arka planını daha soluk, kenarlıksız ve kenarlardan içeride çizer.
- headerUnderline: true
  $name: Sütun başlığının altına çizgi ekle
  $description: Açık temada Ayrıntılar görünümündeki sütun başlıklarının altına, aralarındaki dikey çizgilerle aynı tipte ince bir çizgi çizer.
- contentBox: true
  $name: Dosya listesi ve status bar çizgileri
  $description: Açık temada status bar'ın üstüne tam genişlikte ve dosya listesinin sağına (son sütun hizasında) başlık çizgileriyle aynı tipte ince çizgiler çizer.
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
```

Ayar kuralları:

- `match` büyük/küçük harf duyarsız tam isim ya da `{CLSID}` olabilir.
- `newName` boşsa öğenin orijinal adı kalır.
- `hidden` yalnızca CLSID'si çözülebilen öğelerde çalışır.
- Listede olmayan öğeler listedekilerden sonra, kendi sıralarıyla gelir.
- `Wh_ModSettingsChanged` tetiklendiğinde şunlar yapılır: ayarlar yeniden okunur, CLSID'ler yeniden çözülür, açık ağaçlar yeniden sıralanır ve yeniden adlandırılır, registry farkı uygulanır. Hook'ların açık/kapalı durumu ayar bayraklarıyla kontrol edilir; hook'lar yeniden kurulmaz.

## 5. Hata durumları

- Her parça bağımsızdır. Biri başlatılamazsa `Wh_Log` ile uyarı yazılır, diğerleri çalışmaya devam eder.
- Pin ikonları sembol gerektirmez (ağaç subclass'ı); subclass kurulamazsa yalnızca o pencerede ikonlar kalır.
- XAML Diagnostics başarısız olursa tab ve toolbar özellikleri devre dışı kalır.
- Ağaçta beklenmeyen bir yapı görülürse (örn. kök bulunamazsa) sıralama yapılmaz.
- Registry yazma hatası loglanır. Hatalı CLSID için yedek kaydı oluşturulmaz.
- `Wh_ModUninit` sırasında subclass'lar kaldırılır, XAML değişiklikleri geri alınır (`Visibility` eski değerine döner) ve registry geri yüklenir.

## 6. Test

- **Otomatik doğrulama:** `tests/verify-navtree.ps1`. Bugünkü okuma betiğinden türetilir ve açık bir Explorer penceresinin sol menüsünü dışarıdan, salt okunur olarak okur. Kontrol ettikleri:
  - Kök çocuklarının sırası beklenen listeyle eşleşiyor mu? Beklenen liste parametre olarak verilir; varsayılanı `This PC, Phone, Linux` + pinliler.
  - Pinliler en sonda mı?
  - Yeni adlar görünüyor mu?

  Sonuç PASS/FAIL ve çıkış kodu olarak döner.
- **Elle test listesi:**
  1. Mod Windhawk'ta derleniyor.
  2. Yeni pencerede tab satırı yok. Pencere sürüklenebiliyor, küçült/büyüt/kapat çalışıyor.
  3. Toolbar yok.
  4. Pin ikonları yok.
  5. Sol menü sırası ve yeni adlar doğru (`verify-navtree.ps1` PASS veriyor).
  6. Yeni bir klasör pinlendiğinde en alta gidiyor.
  7. Her ayar tek tek kapatılıp açılınca beklenen davranış görülüyor.
  8. Bir öğe `hidden: true` yapılıp Explorer yeniden açılınca öğe gizleniyor. Mod kapatılınca registry eski haline dönüyor (`reg query` ile kontrol edilir).
  9. Birden fazla pencere açıkken her şey doğru çalışıyor.

## 7. Kapsam dışı

- Başka uygulamaların Aç/Kaydet pencereleri (`explorer.exe` dışında çalışırlar).
- Sol menüye yeni öğe ekleme.
- Pinli öğelerin kendi aralarındaki sırası (Quick Access'in kendi sırası korunur).
- Windows 10 desteği.
