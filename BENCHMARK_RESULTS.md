# Benchmark Results — Smart Order Router

## Test results

```
9 test cases, 18 assertions, 0 failures

  BestPrice buy    : routes to NYSE first (lowest ask $150.21)
  BestPrice sell   : routes to BATS/IEX first (highest bid $150.22)
  LowestFee        : total fees <= max taker rate
  ProRata          : proportional split across 4 venues
  Limit price      : only NYSE qualifies at $150.21 limit
  No eligible      : 0 fills when limit below market
  Empty venues     : handled cleanly
  VWAP             : (2000×$150.21 + 3000×$150.22)/5000 = $150.216 ✓
  Fee calculation  : $0.003/share × 1000 shares = $3.00 ✓
```

## Fee model

4-venue US equity maker/taker model:

| Venue | Taker fee | Maker rebate |
|-------|-----------|--------------|
| NYSE | $0.003/share | $0.002/share |
| NASDAQ | $0.003/share | $0.002/share |
| BATS | $0.0015/share | $0.001/share |
| IEX | $0.0009/share | $0.000/share |

## Routing strategies

**BestPrice:** greedy sweep — lowest ask (buy) or highest bid (sell) first.
Best for time-sensitive orders where price improvement matters most.

**LowestFee:** rank by effective price = quoted price + taker_fee.
Best for cost-sensitive institutional orders where total cost matters.

**ProRata:** proportional split by available qty at each venue.
Best for VWAP execution minimising market impact.
