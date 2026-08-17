// tests/test_router.cpp — Correctness tests for the SOR router.

#include "sor/router.hpp"
#include <cmath>
#include <cstdio>

static int passed = 0, failed = 0;

#define CHECK(cond, msg) \
    do { if (!(cond)) { \
        fprintf(stderr, "  FAIL [%s:%d] %s\n", __FILE__, __LINE__, msg); \
        ++failed; \
    } else { ++passed; } } while(0)

static std::vector<sor::Venue> make_venues() {
    return {
        {"NYSE",   sor::build_price(150.20), sor::build_price(150.21),
                   5000, 5000, sor::build_fee(0.003),  sor::build_fee(0.002)},
        {"NASDAQ", sor::build_price(150.21), sor::build_price(150.22),
                   3000, 3000, sor::build_fee(0.003),  sor::build_fee(0.002)},
        {"BATS",   sor::build_price(150.22), sor::build_price(150.23),
                   8000, 8000, sor::build_fee(0.0015), sor::build_fee(0.001)},
        {"IEX",    sor::build_price(150.22), sor::build_price(150.22),
                   2000, 2000, sor::build_fee(0.0009), sor::build_fee(0.0)},
    };
}

void test_best_price_buy() {
    printf("BestPrice buy routing:\n");
    auto venues = make_venues();
    sor::ParentOrder order{sor::Side::Buy, 5000,
        sor::build_price(150.25), sor::RoutingStrategy::BestPrice};
    auto result = sor::Router::route(order, venues);
    CHECK(result.total_filled == 5000,  "fully filled");
    CHECK(result.total_residual == 0,   "no residual");
    CHECK(result.fill_rate == 1.0,      "100% fill rate");
    CHECK(!result.children.empty(),     "has child orders");
    CHECK(result.children[0].venue_name == "NYSE", "routes to NYSE first (lowest ask)");
    printf("  VWAP=%.4f fill_rate=%.0f%%\n",
           double(result.vwap)/sor::kPriceUnit, result.fill_rate*100);
}

void test_best_price_sell() {
    printf("BestPrice sell routing:\n");
    auto venues = make_venues();
    sor::ParentOrder order{sor::Side::Sell, 4000,
        sor::build_price(150.15), sor::RoutingStrategy::BestPrice};
    auto result = sor::Router::route(order, venues);
    CHECK(result.total_filled == 4000, "sell fully filled");
    bool first_best = (result.children[0].venue_name == "BATS" ||
                       result.children[0].venue_name == "IEX");
    CHECK(first_best, "routes to highest bid first");
}

void test_lowest_fee() {
    printf("LowestFee routing:\n");
    auto venues = make_venues();
    sor::ParentOrder order{sor::Side::Buy, 3000,
        sor::build_price(150.25), sor::RoutingStrategy::LowestFee};
    auto result = sor::Router::route(order, venues);
    CHECK(result.total_filled == 3000, "lowest fee fully filled");
    CHECK(result.total_fees <= sor::build_fee(0.003) * 3000,
          "fees <= max taker fee");
    printf("  total_fees=%lld fixed-point\n", (long long)result.total_fees);
}

void test_pro_rata() {
    printf("ProRata routing:\n");
    auto venues = make_venues();
    sor::ParentOrder order{sor::Side::Buy, 10000,
        sor::build_price(150.25), sor::RoutingStrategy::ProRata};
    auto result = sor::Router::route(order, venues);
    CHECK(result.total_filled + result.total_residual == 10000,
          "filled + residual = total");
    CHECK(result.children.size() > 1, "multiple venues in pro-rata");
    printf("  venues=%zu filled=%u residual=%u\n",
           result.children.size(), result.total_filled, result.total_residual);
}

void test_limit_price_enforced() {
    printf("Limit price enforcement:\n");
    auto venues = make_venues();
    sor::ParentOrder order{sor::Side::Buy, 5000,
        sor::build_price(150.21), sor::RoutingStrategy::BestPrice};
    auto result = sor::Router::route(order, venues);
    for (auto& c : result.children)
        CHECK(c.expected_fill_price <= sor::build_price(150.21),
              "all fills within limit price");
    printf("  filled=%u (only NYSE qualifies at 150.21)\n", result.total_filled);
}

void test_no_eligible_venues() {
    printf("No eligible venues:\n");
    auto venues = make_venues();
    sor::ParentOrder order{sor::Side::Buy, 1000,
        sor::build_price(150.10), sor::RoutingStrategy::BestPrice};
    auto result = sor::Router::route(order, venues);
    CHECK(result.total_filled == 0,    "no fill when limit below market");
    CHECK(result.total_residual == 1000, "all qty is residual");
    CHECK(result.fill_rate == 0.0,     "fill rate 0%");
}

void test_empty_venues() {
    printf("Empty venue list:\n");
    sor::ParentOrder order{sor::Side::Buy, 100, sor::build_price(150.25)};
    auto result = sor::Router::route(order, {});
    CHECK(result.total_filled == 0, "no fill with empty venues");
}

void test_vwap() {
    printf("VWAP calculation:\n");
    std::vector<sor::Venue> venues = {
        {"V1", 0, sor::build_price(150.21), 0, 2000, sor::build_fee(0), sor::build_fee(0)},
        {"V2", 0, sor::build_price(150.22), 0, 3000, sor::build_fee(0), sor::build_fee(0)},
    };
    sor::ParentOrder order{sor::Side::Buy, 5000,
        sor::build_price(150.25), sor::RoutingStrategy::BestPrice};
    auto result = sor::Router::route(order, venues);
    double expected = (2000.0*150.21 + 3000.0*150.22) / 5000.0;
    double got = double(result.vwap) / sor::kPriceUnit;
    CHECK(std::abs(expected - got) < 0.001, "VWAP correct to 3dp");
    printf("  expected=%.4f got=%.4f\n", expected, got);
}

void test_fee_calculation() {
    printf("Fee calculation:\n");
    std::vector<sor::Venue> venues = {
        {"V1", 0, sor::build_price(150.00), 0, 1000,
         sor::build_fee(0.003), sor::build_fee(0.002)},
    };
    sor::ParentOrder order{sor::Side::Buy, 1000,
        sor::build_price(150.25), sor::RoutingStrategy::BestPrice};
    auto result = sor::Router::route(order, venues);
    sor::Price expected_fee = sor::build_fee(0.003) * 1000;
    CHECK(result.total_fees == expected_fee, "fee = taker_fee * qty");
    printf("  fee=%lld fixed-point (%.4f/share)\n",
           (long long)result.total_fees,
           double(result.total_fees)/double(sor::kPriceUnit)/1000.0);
}

int main() {
    printf("=== Smart Order Router Tests ===\n\n");
    test_best_price_buy();
    test_best_price_sell();
    test_lowest_fee();
    test_pro_rata();
    test_limit_price_enforced();
    test_no_eligible_venues();
    test_empty_venues();
    test_vwap();
    test_fee_calculation();
    printf("\n================================\n");
    printf("Results: %d passed, %d failed\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
