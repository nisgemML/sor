# smart-order-router

**Multi-venue smart order router (SOR) for US equity trading.**

Routes parent orders across multiple venues to minimise total cost and maximise
fill rate. Three routing strategies with a realistic 4-venue US equity fee model.

## Routing strategies

| Strategy | Description | Use case |
|----------|-------------|----------|
| `BestPrice` | Greedy sweep — best quoted price first | Time-sensitive fills |
| `LowestFee` | Rank by effective price (quote + taker fee) | Cost-sensitive orders |
| `ProRata` | Proportional split by available qty | VWAP execution |

## Fee model

```
Effective buy price  = ask + taker_fee
Effective sell price = bid - taker_fee

NYSE:   $0.003/share taker,  $0.002 maker rebate
NASDAQ: $0.003/share taker,  $0.002 maker rebate
BATS:   $0.0015/share taker, $0.001 maker rebate
IEX:    $0.0009/share taker  (D-Peg)
```

## Build

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
make -j$(nproc)
ctest --output-on-failure    # 18 assertions, 0 failures
```

## Jump Trading relevance

In a multi-venue market, the naive approach — route the full order to the best-priced venue — ignores two realities: available liquidity is fragmented across venues, and effective price includes fees.

A buy order at NYSE ask $150.21 with $0.003 taker fee costs $150.213 effective. The same order at IEX ask $150.22 with $0.0009 fee costs $150.2209 effective — cheaper despite the worse quote. LowestFee captures this; BestPrice misses it.

ProRata handles a third case: when market impact matters more than price — splitting proportionally to available qty minimises the footprint of a large order across venues.

VWAP tracks the blended fill price across all child orders. Fill rate tracks residual. These are the primitives any production SOR needs regardless of strategy.
