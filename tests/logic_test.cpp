// Saf mantık testleri. Windhawk başlıkları olmadan derlenir.
#define ET_LOGIC_ONLY
#include "../explorer-tweaks.wh.cpp"

#include <cstdio>
#include <vector>

static int g_failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                        #cond);                                          \
            g_failures++;                                                \
        }                                                                \
    } while (0)

static const wchar_t* kPhoneClsid = L"{3611C374-393E-41F8-98EB-1AD7BF051346}";

static std::vector<NavEntry> SampleEntries() {
    return {{L"Downloads", true},
            {L"Documents", true},
            {L"MyPhone", false},
            {L"This PC", false},
            {L"Linux", false}};
}

static std::vector<NavRule> SampleRules() {
    return {{L"This PC", L"", false},
            {kPhoneClsid, L"Phone", false},
            {L"Linux", L"", false}};
}

static std::vector<std::wstring> SampleMatch() {
    return {L"This PC", L"MyPhone", L"Linux"};
}

using Order = std::vector<int>;

static void test_bottom() {
    CHECK(ComputeOrder(SampleEntries(), SampleRules(), SampleMatch(),
                       PinnedPosition::Bottom) == (Order{3, 2, 4, 0, 1}));
}

static void test_top() {
    CHECK(ComputeOrder(SampleEntries(), SampleRules(), SampleMatch(),
                       PinnedPosition::Top) == (Order{0, 1, 3, 2, 4}));
}

static void test_default_keeps_pinned_slots() {
    CHECK(ComputeOrder(SampleEntries(), SampleRules(), SampleMatch(),
                       PinnedPosition::Default) == (Order{0, 1, 3, 2, 4}));
}

// Pinli öğe listenin ortasındayken Default onu yerinde bırakır, diğerleri
// kurala göre sıralanır (M7).
static void test_default_pinned_mid_list() {
    std::vector<NavEntry> entries = {{L"A", false}, {L"P", true}, {L"B", false}};
    std::vector<NavRule> rules = {{L"B", L"", false}, {L"A", L"", false}};
    std::vector<std::wstring> match = {L"B", L"A"};
    CHECK(ComputeOrder(entries, rules, match, PinnedPosition::Default) ==
          (Order{2, 1, 0}));
    CHECK(ComputeOrder(entries, rules, match, PinnedPosition::Bottom) ==
          (Order{2, 0, 1}));
    CHECK(ComputeOrder(entries, rules, match, PinnedPosition::Top) ==
          (Order{1, 2, 0}));
}

// Sıralamadan sonra ilk görünür öğe (F1: boş nav pane).
static void test_first_visible_after_sort() {
    const Order order{3, 2, 4, 0, 1};  // eski 3 yeni ilk çocuk
    using K = TopItemKind;
    // Görünüm en üstteydi (eski ilk çocuk): yeni ilk çocuk.
    TopItem t = FirstVisibleAfterSort(order, {K::RootChild, 0});
    CHECK(t.kind == K::RootChild && t.index == 3);
    // İlk görünür öğe yoktu: yeni ilk çocuk.
    t = FirstVisibleAfterSort(order, {K::None, -1});
    CHECK(t.kind == K::RootChild && t.index == 3);
    // Kök en üstteydi: kök kalır.
    t = FirstVisibleAfterSort(order, {K::Root, -1});
    CHECK(t.kind == K::Root);
    // Kaydırılmıştı (başka bir kök çocuğu üstte): aynı öğe yeniden kurulur.
    t = FirstVisibleAfterSort(order, {K::RootChild, 2});
    CHECK(t.kind == K::RootChild && t.index == 2);
    // Daha derin bir öğe üstteydi: aynı öğe (çağıran yeniden kurar).
    t = FirstVisibleAfterSort(order, {K::Other, -1});
    CHECK(t.kind == K::Other);
    // Sıra değişmediyse en üstteki ilk çocuk aynı kalır.
    t = FirstVisibleAfterSort(Order{0, 1, 2}, {K::RootChild, 0});
    CHECK(t.kind == K::RootChild && t.index == 0);
    // Geçersiz indeks ya da boş liste: dokunulmaz.
    t = FirstVisibleAfterSort(order, {K::RootChild, 5});
    CHECK(t.kind == K::None);
    t = FirstVisibleAfterSort(order, {K::RootChild, -3});
    CHECK(t.kind == K::None);
    t = FirstVisibleAfterSort(Order{}, {K::None, -1});
    CHECK(t.kind == K::None);
}

// Ertelenmiş görünüm onarımı kararı.
static void test_needs_view_fix() {
    CHECK(NeedsViewFix(false, true, false));   // bayat ilk görünür öğe
    CHECK(!NeedsViewFix(false, true, true));   // kök / ilk çocuk üstte
    CHECK(!NeedsViewFix(true, true, false));   // kullanıcı kaydırmış
    CHECK(!NeedsViewFix(false, false, false)); // öğe yok
}

static void test_unlisted_after_listed() {
    auto entries = SampleEntries();
    entries.push_back({L"Network", false});
    CHECK(ComputeOrder(entries, SampleRules(), SampleMatch(),
                       PinnedPosition::Bottom) == (Order{3, 2, 4, 5, 0, 1}));
}

static void test_unresolved_rule_ignored() {
    auto match = SampleMatch();
    match[1] = L"";
    CHECK(ComputeOrder(SampleEntries(), SampleRules(), match,
                       PinnedPosition::Bottom) == (Order{3, 4, 2, 0, 1}));
}

static void test_duplicate_and_empty_rules() {
    auto rules = SampleRules();
    auto match = SampleMatch();
    rules.push_back({L"", L"Renamed", false});
    match.push_back(L"");
    rules.push_back({L"This PC", L"", false});
    match.push_back(L"This PC");
    CHECK(FindRule(rules, match, L"this pc") == 0);
    CHECK(FindRule(rules, match, L"") == -1);
    CHECK(FindRule(rules, match, L"Nope") == -1);
    CHECK(ComputeOrder(SampleEntries(), rules, match,
                       PinnedPosition::Bottom) == (Order{3, 2, 4, 0, 1}));
}

static void test_is_clsid() {
    CHECK(IsClsidString(kPhoneClsid));
    CHECK(!IsClsidString(L"Linux"));
    CHECK(!IsClsidString(L"{3611C374-393E-41F8-98EB-1AD7BF05134}"));
    CHECK(!IsClsidString(L"[3611C374-393E-41F8-98EB-1AD7BF051346]"));
}

static void test_new_name_for() {
    // Kural 1 (CLSID) "MyPhone" adına çözülmüş ve "Phone" yapıyor; kural 0'ın
    // newName'i boş.
    CHECK(NewNameFor(SampleRules(), SampleMatch(), L"myphone") == L"Phone");
    CHECK(NewNameFor(SampleRules(), SampleMatch(), L"This PC").empty());
    CHECK(NewNameFor(SampleRules(), SampleMatch(), L"Nope").empty());
    // Kendi yeni adımız hiçbir kuralla eşleşmez.
    CHECK(NewNameFor(SampleRules(), SampleMatch(), L"Phone").empty());
    CHECK(DisplayNameFor(SampleRules(), SampleMatch(), L"MyPhone") == L"Phone");
    CHECK(DisplayNameFor(SampleRules(), SampleMatch(), L"Linux") == L"Linux");
}

static void test_incoming_text_new_item() {
    // Kayıt yok: gelen metin orijinaldir, kurala göre yeni ad gösterilir.
    NameUpdate u = ResolveIncomingText(SampleRules(), SampleMatch(), nullptr,
                                       L"MyPhone");
    CHECK(u.original == L"MyPhone");
    CHECK(u.display == L"Phone");

    u = ResolveIncomingText(SampleRules(), SampleMatch(), nullptr, L"Linux");
    CHECK(u.original == L"Linux");
    CHECK(u.display == L"Linux");
}

static void test_incoming_text_already_renamed() {
    // Explorer bizim yeni adımızı geri yazıyor: orijinal korunur, metin
    // olduğu gibi kalır (Review Focus 5).
    std::wstring recorded = L"MyPhone";
    NameUpdate u = ResolveIncomingText(SampleRules(), SampleMatch(), &recorded,
                                       L"Phone");
    CHECK(u.original == L"MyPhone");
    CHECK(u.display == L"Phone");

    // Explorer orijinali yeniden yazıyor: yine yeni ad gösterilir.
    u = ResolveIncomingText(SampleRules(), SampleMatch(), &recorded, L"MyPhone");
    CHECK(u.original == L"MyPhone");
    CHECK(u.display == L"Phone");
}

static void test_incoming_text_real_change() {
    // Öğenin gerçek adı değişti (ör. telefon yeniden adlandırıldı): yeni metin
    // orijinal olur ve artık kuralla eşleşmediği için olduğu gibi gösterilir.
    std::wstring recorded = L"MyPhone";
    NameUpdate u = ResolveIncomingText(SampleRules(), SampleMatch(), &recorded,
                                       L"Pixel");
    CHECK(u.original == L"Pixel");
    CHECK(u.display == L"Pixel");

    // Kural kaldırıldıktan sonra eski yeni ad gelirse o artık orijinaldir.
    auto rules = SampleRules();
    rules[1].newName = L"";
    u = ResolveIncomingText(rules, SampleMatch(), &recorded, L"Phone");
    CHECK(u.original == L"Phone");
    CHECK(u.display == L"Phone");
}

static void test_has_unresolved_clsid() {
    // Varsayılan örnekte CLSID kuralı "MyPhone" adına çözülmüş.
    CHECK(!HasUnresolvedClsid(SampleRules(), SampleMatch()));
    auto match = SampleMatch();
    match[1] = L"";
    CHECK(HasUnresolvedClsid(SampleRules(), match));
    // Boş isim kuralı CLSID değildir; yeniden denemeyi tetiklemez.
    auto rules = SampleRules();
    rules.push_back({L"", L"Renamed", false});
    auto match2 = SampleMatch();
    match2.push_back(L"");
    CHECK(!HasUnresolvedClsid(rules, match2));
    // effectiveMatch kısa kalmışsa eksik CLSID çözülmemiş sayılır.
    CHECK(HasUnresolvedClsid(SampleRules(), {L"This PC"}));
    CHECK(!HasUnresolvedClsid({}, {}));
}

static void test_retry_delay() {
    CHECK(RetryDelay(10000, 4000, 5000) == 0);  // süre doldu
    CHECK(RetryDelay(9000, 4000, 5000) == 0);   // tam sınırda
    CHECK(RetryDelay(6000, 4000, 5000) == 3000);
    CHECK(RetryDelay(4000, 4000, 5000) == 5000);
    CHECK(RetryDelay(3000, 4000, 5000) == 5000);  // saat geride: bekle
}

static const wchar_t* kGallery = L"{e88865ea-0e1c-4e20-9aa6-edcd0212c87c}";
static const wchar_t* kGalleryUpper = L"{E88865EA-0E1C-4E20-9AA6-EDCD0212C87C}";
static const wchar_t* kHome = L"{f874310e-b6b7-47dc-bc84-b9e6b38f5903}";
static const wchar_t* kHomeUpper = L"{F874310E-B6B7-47DC-BC84-B9E6B38F5903}";

static void test_backup_list() {
    // Boş metin boş liste verir.
    CHECK(ParseBackupList(L"").empty());
    CHECK(SerializeBackupList({}) == L"");

    // Büyük/küçük harf duyarsız tekilleştirme, sıra korunur, biçim büyük harfe
    // normalleştirilir; CLSID olmayan ve boş parçalar atılır.
    std::wstring text = std::wstring(kGallery) + L";;junk;" + kHomeUpper +
                        L";" + kGalleryUpper + L";" + kHome + L";";
    auto list = ParseBackupList(text);
    CHECK(list.size() == 2);
    CHECK(list.size() == 2 && list[0] == kGalleryUpper);
    CHECK(list.size() == 2 && list[1] == kHomeUpper);

    // Gidiş-dönüş.
    std::wstring ser = SerializeBackupList(list);
    CHECK(ser == std::wstring(kGalleryUpper) + L";" + kHomeUpper);
    CHECK(ParseBackupList(ser) == list);

    // Ekleme ve çıkarma da aynı kuralları izler.
    auto added = list;
    CHECK(!AddToBackupList(added, kGallery));  // zaten var
    CHECK(added == list);
    CHECK(!AddToBackupList(added, L"nope"));  // CLSID değil
    CHECK(added == list);
    auto one = ParseBackupList(kGalleryUpper);
    CHECK(AddToBackupList(one, kHome));
    CHECK(one == list);
    CHECK(RemoveFromBackupList(one, kHomeUpper));
    CHECK(one.size() == 1 && one[0] == kGalleryUpper);
    CHECK(!RemoveFromBackupList(one, kHome));
    CHECK(RemoveFromBackupList(one, kGallery));
    CHECK(one.empty());
}

static void test_backup_value() {
    BackupValue v;
    CHECK(SerializeBackupValue({true, 0}) == L"absent");
    CHECK(SerializeBackupValue({true, 7}) == L"absent");  // absent değeri yok sayar
    CHECK(SerializeBackupValue({false, 0}) == L"0");
    CHECK(SerializeBackupValue({false, 1}) == L"1");
    CHECK(SerializeBackupValue({false, 4294967295u}) == L"4294967295");

    CHECK(ParseBackupValue(L"absent", v) && v.absent);
    CHECK(ParseBackupValue(L"0", v) && !v.absent && v.value == 0);
    CHECK(ParseBackupValue(L"1", v) && !v.absent && v.value == 1);
    CHECK(ParseBackupValue(L"4294967295", v) && !v.absent &&
          v.value == 4294967295u);

    // Geçersiz metinler reddedilir.
    CHECK(!ParseBackupValue(L"", v));
    CHECK(!ParseBackupValue(L"Absent", v));
    CHECK(!ParseBackupValue(L"-1", v));
    CHECK(!ParseBackupValue(L"+1", v));
    CHECK(!ParseBackupValue(L" 1", v));
    CHECK(!ParseBackupValue(L"1 ", v));
    CHECK(!ParseBackupValue(L"0x10", v));
    CHECK(!ParseBackupValue(L"4294967296", v));  // DWORD'a sığmaz
    CHECK(!ParseBackupValue(L"99999999999999999999", v));

    // Gidiş-dönüş.
    for (BackupValue in : {BackupValue{true, 0}, BackupValue{false, 0},
                           BackupValue{false, 1}, BackupValue{false, 12345}}) {
        BackupValue out;
        CHECK(ParseBackupValue(SerializeBackupValue(in), out));
        CHECK(out.absent == in.absent);
        CHECK(in.absent || out.value == in.value);
    }
}

static void test_plan_hidden() {
    std::vector<std::wstring> a = {kGalleryUpper};
    std::vector<std::wstring> both = {kGalleryUpper, kHomeUpper};
    std::vector<std::wstring> none;

    // Hedefte olup yedeği olmayan gizlenir.
    HiddenPlan p = PlanHidden(a, none);
    CHECK(p.hide == a && p.ensure.empty() && p.restore.empty());

    // Hedefte olup yedeği olan zaten gizlenmiştir; yalnızca doğrulanır.
    p = PlanHidden(a, a);
    CHECK(p.hide.empty() && p.ensure == a && p.restore.empty());

    // Yedeği olup hedefte olmayan geri yüklenir.
    p = PlanHidden(none, a);
    CHECK(p.hide.empty() && p.ensure.empty() && p.restore == a);

    // Karışık durum; büyük/küçük harf farkı yok sayılır, sıra korunur.
    std::vector<std::wstring> target = {kHome, kGallery};
    std::vector<std::wstring> backups = {kGalleryUpper};
    p = PlanHidden(target, backups);
    CHECK(p.hide.size() == 1 && p.hide[0] == kHome);
    CHECK(p.ensure.size() == 1 && p.ensure[0] == kGallery);
    CHECK(p.restore.empty());

    p = PlanHidden(a, both);
    CHECK(p.hide.empty());
    CHECK(p.ensure == a);
    CHECK(p.restore.size() == 1 && p.restore[0] == kHomeUpper);

    // İkisi de boş: iş yok.
    p = PlanHidden(none, none);
    CHECK(p.hide.empty() && p.ensure.empty() && p.restore.empty());

    // Çözülemeyen gizli kural varsa geri yükleme yapılmaz (yedek o kurala ait
    // olabilir); gizleme ve doğrulama sürer.
    p = PlanHidden(target, backups, true);
    CHECK(p.hide.size() == 1 && p.hide[0] == kHome);
    CHECK(p.ensure.size() == 1 && p.ensure[0] == kGallery);
    CHECK(p.restore.empty());
    p = PlanHidden(none, both, true);
    CHECK(p.hide.empty() && p.ensure.empty() && p.restore.empty());
    // Bayrak yokken davranış değişmez.
    p = PlanHidden(none, both, false);
    CHECK(p.restore == both);
}

static void test_hidden_targets() {
    auto resolve = [](std::wstring_view name) -> std::wstring {
        if (NamesEqual(name, L"Gallery")) return kGallery;
        if (NamesEqual(name, L"Home")) return kHome;
        return L"";
    };
    std::vector<NavRule> rules = {
        {L"Gallery", L"", true},         // isim, gizli
        {kHomeUpper, L"", true},         // CLSID, gizli
        {L"This PC", L"", false},        // gizli değil
        {kGalleryUpper, L"", true},      // tekrar (isim kuralıyla aynı)
        {L"Nonexistent", L"", true},     // çözülemez, atlanır
        {L"", L"x", true},               // boş match, atlanır
        {kPhoneClsid, L"Phone", false},  // gizli değil
    };
    auto t = ComputeHiddenTargets(rules, resolve);
    CHECK(t.size() == 2);
    CHECK(t.size() == 2 && t[0] == kGalleryUpper);
    CHECK(t.size() == 2 && t[1] == kHomeUpper);

    CHECK(ComputeHiddenTargets({}, resolve).empty());
    // Gizli kural yoksa boş.
    CHECK(ComputeHiddenTargets({{L"Gallery", L"", false}}, resolve).empty());

    // Çözülemeyen gizli isim kuralı unresolved bayrağını kaldırır.
    bool unresolved = false;
    t = ComputeHiddenTargets(rules, resolve, &unresolved);
    CHECK(t.size() == 2);
    CHECK(unresolved);  // "Nonexistent"
    unresolved = true;  // çağıranın değeri değil, sonuç yazılır
    std::vector<NavRule> ok = {{L"Gallery", L"", true}, {kHome, L"", true}};
    t = ComputeHiddenTargets(ok, resolve, &unresolved);
    CHECK(t.size() == 2 && !unresolved);
    // Gizli olmayan çözülemez kural, boş match ve CLSID kuralları sayılmaz.
    std::vector<NavRule> noise = {{L"Nonexistent", L"", false},
                                  {L"", L"x", true},
                                  {kHome, L"", true}};
    ComputeHiddenTargets(noise, resolve, &unresolved);
    CHECK(!unresolved);
    // Çözücü CLSID olmayan bir şey dönerse de çözülemedi sayılır.
    auto junk = [](std::wstring_view) { return std::wstring(L"junk"); };
    t = ComputeHiddenTargets(std::vector<NavRule>{{L"Gallery", L"", true}},
                             junk, &unresolved);
    CHECK(t.empty() && unresolved);
}

static void test_thread_record_stale() {
    CHECK(!IsThreadRecordStale(true, true, 100, 100));   // aynı, canlı thread
    CHECK(IsThreadRecordStale(false, false, 0, 100));    // id artık yok
    CHECK(IsThreadRecordStale(true, false, 100, 100));   // çıkmış (tutamaç açık)
    CHECK(IsThreadRecordStale(true, true, 200, 100));    // id yeniden kullanılmış
}

static void test_sort_unique() {
    std::vector<unsigned long long> v{5, 1, 5, 3, 1, 1};
    SortUnique(v);
    CHECK((v == std::vector<unsigned long long>{1, 3, 5}));
    std::vector<unsigned long long> empty;
    SortUnique(empty);
    CHECK(empty.empty());
}

static void test_divider_color_override() {
    auto on = DividerColorOverride(true, true);
    CHECK(on.has_value());
    CHECK(on.value_or(0) == RGB(0xF7, 0xF7, 0xF7));
    CHECK(!DividerColorOverride(true, false).has_value());
    CHECK(!DividerColorOverride(false, true).has_value());
    CHECK(!DividerColorOverride(false, false).has_value());
}

static void test_soft_selection_color() {
    // TVP_TREEITEM = 1; TREIS_HOT = 2, SELECTED = 3, SELECTEDNOTFOCUS = 5,
    // HOTSELECTED = 6.
    CHECK(SoftSelectionColor(true, true, 1, 2).value_or(0) ==
          RGB(0xEE, 0xF5, 0xFC));
    CHECK(SoftSelectionColor(true, true, 1, 3).value_or(0) ==
          RGB(0xE1, 0xEE, 0xFB));
    CHECK(SoftSelectionColor(true, true, 1, 5).value_or(0) ==
          RGB(0xED, 0xED, 0xED));
    CHECK(SoftSelectionColor(true, true, 1, 6).value_or(0) ==
          RGB(0xD9, 0xE8, 0xF8));
    // Diğer durumlar (NORMAL=1, DISABLED=4, 7) ve diğer parçalar dokunulmaz.
    CHECK(!SoftSelectionColor(true, true, 1, 1).has_value());
    CHECK(!SoftSelectionColor(true, true, 1, 4).has_value());
    CHECK(!SoftSelectionColor(true, true, 1, 7).has_value());
    CHECK(!SoftSelectionColor(true, true, 2, 2).has_value());
    CHECK(!SoftSelectionColor(true, true, 0, 0).has_value());
    // Ayar kapalı ya da koyu tema.
    CHECK(!SoftSelectionColor(false, true, 1, 2).has_value());
    CHECK(!SoftSelectionColor(true, false, 1, 2).has_value());
    CHECK(!SoftSelectionColor(false, false, 1, 6).has_value());
}

static void test_soft_selection_metrics() {
    CHECK(SoftSelectionInset(96) == 6);
    CHECK(SoftSelectionInset(144) == 9);
    CHECK(SoftSelectionInset(192) == 12);
    CHECK(SoftSelectionRadius(96) == 4);
    CHECK(SoftSelectionRadius(144) == 6);
    CHECK(SoftSelectionRadius(192) == 8);
}

static void test_soft_selection_rect() {
    RECT r = SoftSelectionRect({10, 20, 210, 44}, 96);
    CHECK(r.left == 16 && r.right == 204 && r.top == 20 && r.bottom == 44);
    // Çok dar dikdörtgen ters dönmez.
    RECT narrow = SoftSelectionRect({0, 0, 8, 10}, 96);
    CHECK(narrow.left <= narrow.right);
    CHECK(narrow.top == 0 && narrow.bottom == 10);
}

int main() {
    test_divider_color_override();
    test_soft_selection_color();
    test_soft_selection_metrics();
    test_soft_selection_rect();
    test_thread_record_stale();
    test_sort_unique();
    test_backup_list();
    test_backup_value();
    test_plan_hidden();
    test_hidden_targets();
    test_has_unresolved_clsid();
    test_retry_delay();
    test_new_name_for();
    test_incoming_text_new_item();
    test_incoming_text_already_renamed();
    test_incoming_text_real_change();
    test_bottom();
    test_top();
    test_default_keeps_pinned_slots();
    test_default_pinned_mid_list();
    test_first_visible_after_sort();
    test_needs_view_fix();
    test_unlisted_after_listed();
    test_unresolved_rule_ignored();
    test_duplicate_and_empty_rules();
    test_is_clsid();
    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
