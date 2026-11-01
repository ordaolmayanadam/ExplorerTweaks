# Explorer Tweaks

Windows 11 File Explorer'ı sadeleştiren, tek başına çalışan bir [Windhawk](https://windhawk.net) modu. Başka modlara (File Explorer Styler, Aerexplorer vb.) bağlı değildir. Tüm kod tek dosyadadır: `explorer-tweaks.wh.cpp`.

## Özellikler

- **Tab satırını gizler.** Tab listesi ve "yeni sekme" düğmesi gizlenir. Pencere düğmeleri (küçült/büyüt/kapat) ve sürükleme alanı çalışmaya devam etsin diye satır boş bir başlık şeridi olarak kalır.
- **Toolbar'ı (command bar) gizler.**
- **Pin ikonlarını kaldırır.** Quick Access'teki pinli öğelerin yanındaki pin simgesi görünmez.
- **Sol menüyü (navigation pane) düzenler:**
  - **Sıralama:** `navItems` listesindeki sıra, menüdeki sırayı belirler. Listede olmayan öğeler listedekilerden sonra, kendi sıralarıyla gelir.
  - **Yeniden adlandırma:** `newName` boş değilse öğe bu adla gösterilir (ör. "Ömer Fatih adlı kişiye ait S25+" yerine "Phone").
  - **Gizleme:** `hidden: true` olan öğeler menüden kaldırılır.
  - **Pinli klasörlerin yeri:** en üstte, en altta ya da Windows varsayılanı.

Her özellik Windhawk ayar ekranından ayrı ayrı açılıp kapatılabilir. Sıra ve ad değişiklikleri açık pencerelere hemen uygulanır.

Varsayılan ayarlarla sol menü şöyle görünür:

```
This PC
Phone
Linux
<pinli Quick Access klasörleri>
```

## Kurulum

1. [Windhawk](https://windhawk.net)'ı kurun.
2. Windhawk'ta **Create a New Mod** düğmesine tıklayın.
3. Editördeki içeriği silip `explorer-tweaks.wh.cpp` dosyasının tamamını yapıştırın.
4. **Compile Mod** düğmesine tıklayın. Derleme bitince mod etkinleşir.
5. Ayarları modun **Settings** sekmesinden değiştirin.

Modu kaldırmak için Windhawk'ta kapatmanız ya da silmeniz yeterlidir. Mod kapanırken gizleme için yaptığı kayıt defteri değişikliklerini geri alır.

## Ayarlar

Varsayılan ayarlar:

| Ayar | Varsayılan | Açıklama |
|---|---|---|
| `hideTabs` | `true` | Tabları gizle |
| `hideCommandBar` | `true` | Toolbar'ı gizle |
| `hidePins` | `true` | Quick Access pin ikonlarını kaldır |
| `pinnedPosition` | `bottom` | Pinli klasörlerin yeri: `default`, `top` ya da `bottom` |
| `navItems` | aşağıya bakın | Sol menü öğeleri |

Varsayılan `navItems` listesi:

```yaml
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
```

Kurallar:

- `match`, büyük/küçük harf duyarsız tam ad ya da `{CLSID}` olabilir.
- `newName` boşsa öğenin orijinal adı kalır.
- `hidden` yalnızca CLSID'si çözülebilen öğelerde çalışır.
- Listedeki sıra, menüdeki sıradır.

### Kendi Phone CLSID'nizi bulma

Varsayılan ayardaki Phone CLSID'si (`{3611C374-393E-41F8-98EB-1AD7BF051346}`) kullanıcılar ve cihazlar arasında farklı olabilir. Kendi CLSID'nizi şu kayıt defteri anahtarının altında arayın:

```
HKCU\Software\Microsoft\Windows\CurrentVersion\Explorer\Desktop\NameSpace
```

Telefon öğesi, Phone Link'in (CrossDevice) kök girdisidir: alt anahtarlardan, varsayılan değeri `CrossDevice` ile başlayanı bulun (örneğin `CrossDevice!!S-1-5-21-...!CrossDeviceRoot...`). Telefonun görünen adı bu değerde değil, `HKCU\Software\Classes\CLSID\{o CLSID}` anahtarının varsayılan değerindedir. Alt anahtarın adı (`{...}` biçiminde) sizin CLSID'nizdir; `match` alanına onu yazın. Dilerseniz CLSID yerine öğenin görünen adını da yazabilirsiniz.

CLSID'si çözülemeyen bir kural (örneğin telefon o an bağlı değilken) mod tarafından otomatik olarak yeniden denenir.

## Geliştirme

Gereksinimler: Windows 11 ve Windhawk (derleyicisi `C:\Program Files\Windhawk\Compiler` altında olmalı).

```powershell
# Modu Windhawk'ın derleyicisiyle derler (build\explorer-tweaks.dll)
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Mod

# Saf mantık birim testlerini derleyip çalıştırır (tests/logic_test.cpp)
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/build.ps1 -Test
```

Mod Windhawk'a yüklendikten sonra, açık bir Explorer penceresini dışarıdan ve salt okunur olarak denetleyen doğrulama betikleri `tests/` altındadır. Her biri PASS/FAIL yazar ve çıkış kodu döner:

| Betik | Ne denetler |
|---|---|
| `tests/verify-navtree.ps1` | Sol menü sırası, yeni adlar, pinlilerin yeri (`-Expected`, `-Pinned`, `-NoPinIcons`) |
| `tests/verify-hidden.ps1` | Gizlenen öğenin `System.IsPinnedToNameSpaceTree` kayıt defteri değeri (`-Clsid`, `-Expect`) |
| `tests/verify-xaml.ps1` | Tab satırı ve toolbar'ın gizlendiği (`-Check All\|Tabs\|CommandBar`) |

Örnek:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tests/verify-navtree.ps1 -Expected 'This PC','Phone','Linux' -Pinned bottom
```

## Bilinen kısıtlar

- **Gizleme (`hidden`) yeni pencerelerde etkili olur.** Bu özellik `HKCU\Software\Classes\CLSID\{...}` altındaki `System.IsPinnedToNameSpaceTree` değerini değiştirir. Açık pencerelerde görünmesi için Explorer'ı yeniden açmak gerekebilir. Değerin önceki hali yedeklenir ve ayar kaldırılınca ya da mod kapatılınca geri yazılır.
- **Mod kapatıldığında sol menünün eski sırası** açık pencerelerde ancak yenileyince (F5) ya da pencereyi yeniden açınca geri gelir.
- **Tab satırı gizlenir, tab özelliği kapanmaz.** Ctrl+T, "yeni sekmede aç" ve orta tuş tıklaması hâlâ sekme oluşturur; bu sekmeler görünmezdir. Tab satırı, pencere düğmeleri ve sürükleme alanı çalışsın diye boş bir başlık şeridi olarak kalır.
- **Tab ve toolbar gizleme yalnızca WinUI 3 tabanlı yeni Explorer sürümlerinde** çalışır (XAML Diagnostics ile yapılır). Tab yoksa mod bu kısımda hiçbir şey yapmaz.
- Phone CLSID'si kullanıcıya göre değişebilir (yukarıya bakın).
- **Kapsam dışı:** başka uygulamaların Aç/Kaydet pencereleri (`explorer.exe` dışında çalışırlar), sol menüye yeni öğe eklemek, pinli öğelerin kendi aralarındaki sırası, Windows 10.

## Lisans

[GNU GPL v3.0](LICENSE)
