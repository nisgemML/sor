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

Jump builds SOR systems for routing outbound hedging orders across venues
to minimise adverse selection and fees. This implementation covers:
venue ranking, fee calculation, proportional splitting, fill tracking,
and VWAP computation — all core SOR primitives.
