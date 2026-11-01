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

int main() {
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
