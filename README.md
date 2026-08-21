Custom recorder to intercept and record all allocations and deallocations of [
SuperTuxKart ](https://github.com/supertuxkart/stk-code/tree/master). This recording will then be used to benchmark different memory allocators and determine the best one.

`gcc -shared -fPIC -o recorder.so recorder.c -ldl`

Then using LD_PRELOAD this will be ran with `supertuxkart`.

Current log parsing gives the following output:
```
Parsing journal
Parsed 33771118 entries
Unique thread ids: 49
Counts are: 16757635 16872131 71649 69703
Pre processing journal
Realloc returned already allocated pointer: 653283c16120 at index: 1825509
Swapped with operation at index: 1825510
Duplicate allocation for pointer: 653287f79b80 at index: 6150264
Swapped with operation at index: 6150265
Duplicate allocation for pointer: 6532884ebe30 at index: 12118122
Swapped with operation at index: 12118123
Duplicate allocation for pointer: 65328bda89f0 at index: 14049286
Swapped with operation at index: 14049287
Duplicate allocation for pointer: 65328b57e430 at index: 14049291
Swapped with operation at index: 14049292
Duplicate allocation for pointer: 65328c0c6000 at index: 27164499
Swapped with operation at index: 27164500
Duplicate allocation for pointer: 653285585430 at index: 33056702
Swapped with operation at index: 33056703
Total fixes: 7
Remaining live allocations: 3988
Leaked memory: 295KB
```

GLIBC results:
```
[Alloc] Count: 16757635 | Avg: 46ns
  Worst: 8.82ms (20MB, TID:21864, t:18674713176ns)
  Times: Best=10ns, p1=20ns, p10=20ns, p25=20ns, p50=30ns, p75=30ns, p90=60ns, p95=70ns, p99=160ns, p99.9=511ns, p99.99=3.93us, p99.999=173.93us, Worst=8.82ms
  Sizes: Total=2.65GB, Avg=169B, Med=28B

[Free] Count: 16872131 | Avg: 39ns
  Times: Best=10ns, p1=20ns, p10=20ns, p25=20ns, p50=30ns, p75=40ns, p90=60ns, p95=90ns, p99=210ns, p99.9=501ns, p99.99=952ns, p99.999=3.36us, Worst=2.60ms

[Calloc] Count: 71649 | Avg: 69ns
  Worst: 76.92us (142KB, TID:21892, t:1368892668ns)
  Times: Best=20ns, p1=20ns, p10=30ns, p25=30ns, p50=40ns, p75=50ns, p90=90ns, p95=150ns, p99=311ns, p99.9=3.86us, p99.99=24.01us, p99.999=76.92us, Worst=76.92us
  Sizes: Total=20MB, Avg=298B, Med=88B

[Realloc] Count: 69703 | Avg: 56ns
  Worst: 113.27us (307KB, TID:21864, t:1095154833ns)
  Times: Best=20ns, p1=20ns, p10=20ns, p25=30ns, p50=30ns, p75=30ns, p90=60ns, p95=80ns, p99=220ns, p99.9=4.72us, p99.99=13.77us, p99.999=113.27us, Worst=113.27us
  Sizes: Total=8MB, Avg=125B, Med=4B
```

RPMALLOC results:
```
[Alloc] Count: 16757635 | Avg: 46ns
  Worst: 8.91ms (20MB, TID:21864, t:18744942601ns)
  Times: Best=20ns, p1=20ns, p10=20ns, p25=30ns, p50=30ns, p75=30ns, p90=40ns, p95=60ns, p99=130ns, p99.9=1.07us, p99.99=4.95us, p99.999=239.31us, Worst=8.91ms
  Sizes: Total=2.65GB, Avg=169B, Med=28B

[Free] Count: 16872131 | Avg: 42ns
  Times: Best=20ns, p1=30ns, p10=30ns, p25=30ns, p50=40ns, p75=40ns, p90=50ns, p95=70ns, p99=140ns, p99.9=321ns, p99.99=531ns, p99.999=1.04us, Worst=5.39ms

[Calloc] Count: 71649 | Avg: 111ns
  Worst: 287.85us (88B, TID:21864, t:3973586100ns)
  Times: Best=20ns, p1=20ns, p10=30ns, p25=30ns, p50=40ns, p75=80ns, p90=140ns, p95=160ns, p99=1.27us, p99.9=4.25us, p99.99=71.18us, p99.999=287.85us, Worst=287.85us
  Sizes: Total=20MB, Avg=298B, Med=88B

[Realloc] Count: 69703 | Avg: 62ns
  Worst: 127.62us (307KB, TID:21864, t:1041923157ns)
  Times: Best=20ns, p1=30ns, p10=30ns, p25=30ns, p50=30ns, p75=40ns, p90=60ns, p95=80ns, p99=230ns, p99.9=2.80us, p99.99=41.45us, p99.999=127.62us, Worst=127.62us
  Sizes: Total=8MB, Avg=125B, Med=4B
```

MIMALLOC results:
```
[Alloc] Count: 16757635 | Avg: 39ns
  Worst: 8.60ms (20MB, TID:21864, t:18744942601ns)
  Times: Best=10ns, p1=20ns, p10=20ns, p25=20ns, p50=20ns, p75=30ns, p90=30ns, p95=40ns, p99=120ns, p99.9=471ns, p99.99=3.82us, p99.999=229.69us, Worst=8.60ms
  Sizes: Total=2.65GB, Avg=169B, Med=28B

[Free] Count: 16872131 | Avg: 26ns
  Times: Best=10ns, p1=20ns, p10=20ns, p25=20ns, p50=20ns, p75=30ns, p90=30ns, p95=40ns, p99=130ns, p99.9=301ns, p99.99=531ns, p99.999=1.09us, Worst=121.57us

[Calloc] Count: 71649 | Avg: 79ns
  Worst: 142.57us (265KB, TID:21864, t:1283391718ns)
  Times: Best=10ns, p1=20ns, p10=20ns, p25=20ns, p50=30ns, p75=50ns, p90=130ns, p95=150ns, p99=451ns, p99.9=4.45us, p99.99=36.07us, p99.999=142.57us, Worst=142.57us
  Sizes: Total=20MB, Avg=298B, Med=88B

[Realloc] Count: 69703 | Avg: 46ns
  Worst: 121.72us (307KB, TID:21864, t:1041923157ns)
  Times: Best=10ns, p1=20ns, p10=20ns, p25=20ns, p50=20ns, p75=30ns, p90=40ns, p95=50ns, p99=180ns, p99.9=2.46us, p99.99=26.27us, p99.999=121.72us, Worst=121.72us
  Sizes: Total=8MB, Avg=125B, Med=4B
```

TCMALLOC results:
```
[Alloc] Count: 16757635 | Avg: 33ns
  Worst: 8.69ms (20MB, TID:21864, t:18744942601ns)
  Times: Best=10ns, p1=20ns, p10=20ns, p25=20ns, p50=20ns, p75=30ns, p90=30ns, p95=30ns, p99=50ns, p99.9=260ns, p99.99=3.63us, p99.999=40.99us, Worst=8.69ms
  Sizes: Total=2.65GB, Avg=169B, Med=28B

[Free] Count: 16872131 | Avg: 27ns
  Times: Best=10ns, p1=20ns, p10=20ns, p25=20ns, p50=20ns, p75=30ns, p90=30ns, p95=30ns, p99=130ns, p99.9=311ns, p99.99=661ns, p99.999=3.24us, Worst=354.14us

[Calloc] Count: 71649 | Avg: 55ns
  Worst: 143.59us (142KB, TID:21892, t:1368892668ns)
  Times: Best=10ns, p1=20ns, p10=20ns, p25=30ns, p50=30ns, p75=40ns, p90=60ns, p95=130ns, p99=331ns, p99.9=2.07us, p99.99=23.76us, p99.999=143.59us, Worst=143.59us
  Sizes: Total=20MB, Avg=298B, Med=88B

[Realloc] Count: 69703 | Avg: 37ns
  Worst: 32.54us (307KB, TID:21864, t:1041923157ns)
  Times: Best=20ns, p1=20ns, p10=20ns, p25=20ns, p50=20ns, p75=30ns, p90=40ns, p95=50ns, p99=160ns, p99.9=1.35us, p99.99=12.87us, p99.999=32.54us, Worst=32.54us
  Sizes: Total=8MB, Avg=125B, Med=4B
```
