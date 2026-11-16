// Build & run:  g++ -std=c++17 -I../src test_tracker.cpp -o test_tracker && ./test_tracker
#include <cstdio>
#include <cstdlib>
#include "boon_tracker.h"

static int g_failed = 0;
#define CHECK(cond)                                                        \
    do {                                                                   \
        if (!(cond)) {                                                     \
            std::printf("FAIL line %d: %s\n", __LINE__, #cond);            \
            ++g_failed;                                                    \
        }                                                                  \
    } while (0)

static bt::CbtEvent Apply(uint32_t boon, uint64_t target, int durMs)
{
    bt::CbtEvent ev{};
    ev.buff = 1;
    ev.skillid = boon;
    ev.dst_agent = target;
    ev.value = durMs;
    return ev;
}

int main()
{
    bt::Settings cfg;
    bt::Tracker t;

    // A boon applied by me to two allies is counted.
    CHECK(t.OnCombat(Apply(1187, 100, 2000), true, false, 1000, cfg));
    CHECK(t.OnCombat(Apply(1187, 101, 2000), true, false, 1100, cfg));
    // Same ally again: another application, still one distinct target.
    CHECK(t.OnCombat(Apply(1187, 100, 1500), true, false, 1200, cfg));

    auto rows = t.Snapshot(1300, cfg);
    CHECK(rows.size() == 1);
    CHECK(rows[0].boon->id == 1187);
    CHECK(rows[0].targets == 2);
    CHECK(rows[0].applications == 3);
    CHECK(rows[0].lastDurationMs == 1500);
    CHECK(rows[0].msSinceLast == 100);

    // Not mine -> ignored.
    CHECK(!t.OnCombat(Apply(725, 100, 3000), false, false, 1400, cfg));
    // Not a boon id -> ignored.
    CHECK(!t.OnCombat(Apply(99999, 100, 3000), true, false, 1400, cfg));
    // Removal / damage tick / state change / activation -> ignored.
    bt::CbtEvent rem = Apply(725, 100, 3000);
    rem.is_buffremove = 1;
    CHECK(!t.OnCombat(rem, true, false, 1400, cfg));
    bt::CbtEvent tick = Apply(725, 100, 0);
    tick.buff_dmg = 120;
    CHECK(!t.OnCombat(tick, true, false, 1400, cfg));
    bt::CbtEvent sc = Apply(725, 100, 3000);
    sc.is_statechange = 5;
    CHECK(!t.OnCombat(sc, true, false, 1400, cfg));
    bt::CbtEvent act = Apply(725, 100, 3000);
    act.is_activation = 1;
    CHECK(!t.OnCombat(act, true, false, 1400, cfg));
    CHECK(t.Snapshot(1500, cfg).size() == 1);

    // includeSelf = false drops self-targeted applications.
    bt::Settings noSelf;
    noSelf.includeSelf = false;
    CHECK(!t.OnCombat(Apply(717, 1, 3000), true, true, 1600, noSelf));
    CHECK(t.OnCombat(Apply(717, 1, 3000), true, true, 1600, cfg));

    // Window: after 6s the Quickness row falls out of a 5s window.
    rows = t.Snapshot(1100 + 6000, cfg);
    bool hasQuick = false;
    for (auto& r : rows) hasQuick |= (r.boon->id == 1187);
    CHECK(!hasQuick);

    // Old history is pruned entirely.
    rows = t.Snapshot(1000000, cfg);
    CHECK(rows.empty());

    // Reset clears everything.
    t.OnCombat(Apply(740, 5, 1000), true, false, 2000000, cfg);
    t.Reset();
    CHECK(t.Snapshot(2000001, cfg).empty());

    // Every listed boon id resolves to itself.
    for (size_t i = 0; i < bt::kBoons.size(); ++i)
        CHECK(bt::BoonIndex(bt::kBoons[i].id) == static_cast<int>(i));

    if (g_failed == 0) std::printf("All tests passed.\n");
    return g_failed == 0 ? 0 : 1;
}
