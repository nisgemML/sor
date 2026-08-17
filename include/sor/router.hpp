#pragma once
// include/sor/router.hpp — Smart Order Router for US equity trading.
//
// Routes a parent order across multiple venues to minimise total cost
// (price impact + fees) and maximise fill rate.
//
// ── What a SOR does ──────────────────────────────────────────────────────────
//
// Given: buy 10,000 shares AAPL at limit $150.25
// Available venues: NYSE ask $150.21, NASDAQ ask $150.22, BATS ask $150.23
//
// The SOR must:
//   1. Filter venues within limit price
//   2. Rank by strategy (best price, lowest fee, or pro-rata)
//   3. Split the order to sweep available liquidity
//   4. Track fill rate and residual
//
// ── Routing strategies ───────────────────────────────────────────────────────
//
// BestPrice:  greedy sweep — best quoted price first
// LowestFee:  rank by effective price (quote + taker_fee)
// ProRata:    split proportionally to available qty at each venue
//
// ── Fee model ────────────────────────────────────────────────────────────────
//
// US equities use maker/taker pricing:
//   Effective buy price  = ask + taker_fee
//   Effective sell price = bid - taker_fee
//
// NYSE:   fee $0.003/share, rebate $0.002
// NASDAQ: fee $0.003/share, rebate $0.002
// BATS:   fee $0.0015/share, rebate $0.001
// IEX:    fee $0.0009/share (D-Peg model)
//
// ── Jump Trading relevance ────────────────────────────────────────────────────
//
// Jump builds SOR systems for routing outbound hedging orders across venues
// to minimise adverse selection and fees. This implementation demonstrates:
// venue ranking, fee calculation, proportional splitting, fill tracking,
// and VWAP computation — all core SOR primitives.

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <string>
#include <string_view>
#include <vector>

namespace sor {

using Price = int64_t;  // fixed-point: 1 unit = $0.0001
using Qty   = uint32_t;

static constexpr Price kPriceUnit = 10000;

struct Venue {
    std::string_view name;
    Price bid;
    Price ask;
    Qty   bid_qty;
    Qty   ask_qty;
    Price taker_fee;
    Price maker_rebate;

    [[nodiscard]] Price effective_ask() const noexcept { return ask + taker_fee; }
    [[nodiscard]] Price effective_bid() const noexcept { return bid - taker_fee; }
};

enum class Side { Buy, Sell };

enum class RoutingStrategy { BestPrice, ProRata, LowestFee };

struct ParentOrder {
    Side            side;
    Qty             qty;
    Price           limit_price;
    RoutingStrategy strategy = RoutingStrategy::BestPrice;
};

struct ChildOrder {
    std::string_view venue_name;
    Qty              qty;
    Price            limit_price;
    Price            expected_fill_price;
    Price            expected_fee;
    bool             sent = false;
};

struct FillReport {
    std::string_view venue_name;
    Qty              filled_qty   = 0;
    Qty              unfilled_qty = 0;
    Price            avg_fill_px  = 0;
    Price            total_fees   = 0;
    double           fill_rate    = 0.0;
};

struct RoutingResult {
    std::vector<ChildOrder> children;
    std::vector<FillReport> fills;
    Qty    total_filled   = 0;
    Qty    total_residual = 0;
    Price  vwap           = 0;
    Price  total_fees     = 0;
    double fill_rate      = 0.0;
};

class Router {
public:
    [[nodiscard]] static RoutingResult
    route(const ParentOrder& parent,
          std::vector<Venue> venues) noexcept
    {
        RoutingResult result;
        if (venues.empty() || parent.qty == 0) return result;

        // Filter venues within limit price
        std::vector<Venue*> eligible;
        for (auto& v : venues) {
            Price px    = (parent.side == Side::Buy) ? v.ask : v.bid;
            Qty   avail = (parent.side == Side::Buy) ? v.ask_qty : v.bid_qty;
            bool ok = (parent.side == Side::Buy)
                ? (px <= parent.limit_price)
                : (px >= parent.limit_price);
            if (ok && avail > 0) eligible.push_back(&v);
        }

        if (eligible.empty()) {
            result.total_residual = parent.qty;
            return result;
        }

        // Sort by strategy
        if (parent.strategy == RoutingStrategy::BestPrice) {
            if (parent.side == Side::Buy)
                std::stable_sort(eligible.begin(), eligible.end(),
                    [](const Venue* a, const Venue* b){ return a->ask < b->ask; });
            else
                std::stable_sort(eligible.begin(), eligible.end(),
                    [](const Venue* a, const Venue* b){ return a->bid > b->bid; });
        } else if (parent.strategy == RoutingStrategy::LowestFee) {
            if (parent.side == Side::Buy)
                std::stable_sort(eligible.begin(), eligible.end(),
                    [](const Venue* a, const Venue* b){ return a->effective_ask() < b->effective_ask(); });
            else
                std::stable_sort(eligible.begin(), eligible.end(),
                    [](const Venue* a, const Venue* b){ return a->effective_bid() > b->effective_bid(); });
        }

        // Allocate qty
        Qty remaining = parent.qty;

        if (parent.strategy == RoutingStrategy::ProRata) {
            Qty total_avail = 0;
            for (const Venue* v : eligible)
                total_avail += (parent.side == Side::Buy) ? v->ask_qty : v->bid_qty;

            for (const Venue* v : eligible) {
                Qty avail = (parent.side == Side::Buy) ? v->ask_qty : v->bid_qty;
                Qty slice = Qty(uint64_t(parent.qty) * avail / total_avail);
                slice = std::min(slice, remaining);
                if (slice == 0) continue;
                Price px  = (parent.side == Side::Buy) ? v->ask : v->bid;
                Price fee = v->taker_fee * Price(slice);
                result.children.push_back({v->name, slice, parent.limit_price, px, fee, true});
                remaining -= slice;
            }
        } else {
            // Greedy sweep
            for (const Venue* v : eligible) {
                if (remaining == 0) break;
                Qty avail = (parent.side == Side::Buy) ? v->ask_qty : v->bid_qty;
                Qty slice = std::min(remaining, avail);
                Price px  = (parent.side == Side::Buy) ? v->ask : v->bid;
                Price fee = v->taker_fee * Price(slice);
                result.children.push_back({v->name, slice, parent.limit_price, px, fee, true});
                remaining -= slice;
            }
        }

        result.total_residual = remaining;

        // Simulate fills
        Price vwap_num = 0;
        for (const auto& child : result.children) {
            FillReport fr;
            fr.venue_name  = child.venue_name;
            fr.filled_qty  = child.qty;
            fr.avg_fill_px = child.expected_fill_price;
            fr.total_fees  = child.expected_fee;
            fr.fill_rate   = 1.0;
            result.total_filled += child.qty;
            result.total_fees   += child.expected_fee;
            vwap_num            += child.expected_fill_price * Price(child.qty);
            result.fills.push_back(fr);
        }

        if (result.total_filled > 0)
            result.vwap = vwap_num / Price(result.total_filled);

        result.fill_rate = double(result.total_filled) /
                           double(result.total_filled + result.total_residual);
        return result;
    }
};

inline Price build_price(double dollars) noexcept {
    return Price(dollars * kPriceUnit);
}

inline Price build_fee(double dollars_per_share) noexcept {
    return Price(dollars_per_share * kPriceUnit);
}

} // namespace sor
