/*
 * Host tests for the BBA2 load-time CPU budget cap (doc/bba2-esp32s3-sdd.md §6.3).
 *
 * The S3 audio path is single-core: every loaded plugin draws from one
 * 174,150-cycle budget per 32-sample block at 44.1 kHz. ChainBudget decides
 * the Elements resolution at load time (52 -> 32 -> 24 -> 16, largest step
 * whose block sum fits under 90% of budget). Only Elements LOADS are capped;
 * any other over-cap combination is allowed (SPManager logs a warning).
 * Pin the estimate table, the cap threshold, and every cap decision against
 * the bench numbers before they can regress.
 */
#include "test_framework.hpp"
#include "chain_budget.hpp"

using CTAG::SP::ChainBudget;

TEST(chain_budget_estimate_table) {
    CHECK_EQ(ChainBudget::EstimateCycles("Void"), 0u);
    CHECK_EQ(ChainBudget::EstimateCycles(""), 0u);
    CHECK_EQ(ChainBudget::EstimateCycles("TBDaits"), 45000u);
    CHECK_EQ(ChainBudget::EstimateCycles("TBDings"), 55000u);
    CHECK_EQ(ChainBudget::EstimateCycles("Claude"), 62000u);
    CHECK_EQ(ChainBudget::EstimateCycles("Mbeads"), 100000u);
    CHECK_EQ(ChainBudget::EstimateCycles("Elements"), 120000u); // 52 modes
    // Unknown plugins are not counted (cost 0), never a crash:
    CHECK_EQ(ChainBudget::EstimateCycles("Freakwaves"), 0u);
    CHECK_EQ(ChainBudget::EstimateCycles(nullptr), 0u);
}

TEST(chain_budget_cap_threshold_value) {
    // 90% of 174150, exact integer math
    CHECK_EQ(ChainBudget::kCapCycles, 156735u);
}

TEST(chain_budget_elements_solo_full_resolution) {
    CTAG::SP::gElementsCapResolution.store(52);
    CHECK_EQ(ChainBudget::ElementsResolution("Elements", "Void"), 52);
    CHECK_EQ(CTAG::SP::gElementsCapResolution.load(), 52);
}

TEST(chain_budget_elements_plus_clouds_caps_to_32) {
    CTAG::SP::gElementsCapResolution.store(52);
    // 52M(120k)+62k = 182k > 156,735 -> 32M(93k)+62k = 155k <= cap
    CHECK_EQ(ChainBudget::ElementsResolution("Elements", "Claude"), 32);
}

TEST(chain_budget_elements_plus_tbdaits_caps_to_32) {
    CTAG::SP::gElementsCapResolution.store(52);
    // 52M(120k)+45k = 165k > cap -> 32M(93k)+45k = 138k <= cap
    CHECK_EQ(ChainBudget::ElementsResolution("Elements", "TBDaits"), 32);
}

TEST(chain_budget_double_elements_caps_to_16) {
    CTAG::SP::gElementsCapResolution.store(52);
    // 93+93=186k, 82+82=164k both > cap; 72+72=144k <= cap
    CHECK_EQ(ChainBudget::ElementsResolution("Elements", "Elements"), 16);
}

TEST(chain_budget_non_elements_leaves_cap_unchanged) {
    CTAG::SP::gElementsCapResolution.store(32);
    CHECK_EQ(ChainBudget::ElementsResolution("Claude", "Void"), 32);
    CHECK_EQ(ChainBudget::ElementsResolution("TBDaits", "TBDings"), 32);
    CHECK_EQ(CTAG::SP::gElementsCapResolution.load(), 32);
}
