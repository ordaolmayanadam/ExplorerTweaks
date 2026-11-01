// Saf mantık testleri. Windhawk başlıkları olmadan derlenir.
#define ET_LOGIC_ONLY
#include "../explorer-tweaks.wh.cpp"

#include <cstdio>

static int g_failures = 0;

#define CHECK(cond)                                                      \
    do {                                                                 \
        if (!(cond)) {                                                   \
            std::printf("%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, \
                        #cond);                                          \
            g_failures++;                                                \
        }                                                                \
    } while (0)

int main() {
    if (g_failures != 0) {
        std::printf("%d check(s) failed\n", g_failures);
        return 1;
    }
    std::printf("all checks passed\n");
    return 0;
}
