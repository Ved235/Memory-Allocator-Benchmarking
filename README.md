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
```
