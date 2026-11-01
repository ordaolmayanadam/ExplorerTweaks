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
            {L"S25+", false},
            {L"This PC", false},
            {L"Linux", false}};
}

static std::vector<NavRule> SampleRules() {
    return {{L"This PC", L"", false},
            {kPhoneClsid, L"Phone", false},
            {L"Linux", L"", false}};
}

static std::vector<std::wstring> SampleMatch() {
    return {L"This PC", L"S25+", L"Linux"};
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
    // Kural 1 (CLSID) "S25+" adına çözülmüş ve "Phone" yapıyor; kural 0'ın
    // newName'i boş.
    CHECK(NewNameFor(SampleRules(), SampleMatch(), L"s25+") == L"Phone");
    CHECK(NewNameFor(SampleRules(), SampleMatch(), L"This PC").empty());
    CHECK(NewNameFor(SampleRules(), SampleMatch(), L"Nope").empty());
    // Kendi yeni adımız hiçbir kuralla eşleşmez.
    CHECK(NewNameFor(SampleRules(), SampleMatch(), L"Phone").empty());
    CHECK(DisplayNameFor(SampleRules(), SampleMatch(), L"S25+") == L"Phone");
    CHECK(DisplayNameFor(SampleRules(), SampleMatch(), L"Linux") == L"Linux");
}

static void test_incoming_text_new_item() {
    // Kayıt yok: gelen metin orijinaldir, kurala göre yeni ad gösterilir.
    NameUpdate u = ResolveIncomingText(SampleRules(), SampleMatch(), nullptr,
                                       L"S25+");
    CHECK(u.original == L"S25+");
    CHECK(u.display == L"Phone");

    u = ResolveIncomingText(SampleRules(), SampleMatch(), nullptr, L"Linux");
    CHECK(u.original == L"Linux");
    CHECK(u.display == L"Linux");
}

static void test_incoming_text_already_renamed() {
    // Explorer bizim yeni adımızı geri yazıyor: orijinal korunur, metin
    // olduğu gibi kalır (Review Focus 5).
    std::wstring recorded = L"S25+";
    NameUpdate u = ResolveIncomingText(SampleRules(), SampleMatch(), &recorded,
                                       L"Phone");
    CHECK(u.original == L"S25+");
    CHECK(u.display == L"Phone");

    // Explorer orijinali yeniden yazıyor: yine yeni ad gösterilir.
    u = ResolveIncomingText(SampleRules(), SampleMatch(), &recorded, L"S25+");
    CHECK(u.original == L"S25+");
    CHECK(u.display == L"Phone");
}

static void test_incoming_text_real_change() {
    // Öğenin gerçek adı değişti (ör. telefon yeniden adlandırıldı): yeni metin
    // orijinal olur ve artık kuralla eşleşmediği için olduğu gibi gösterilir.
    std::wstring recorded = L"S25+";
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

int main() {
    test_new_name_for();
    test_incoming_text_new_item();
    test_incoming_text_already_renamed();
    test_incoming_text_real_change();
    test_bottom();
    test_top();
    test_default_keeps_pinned_slots();
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
