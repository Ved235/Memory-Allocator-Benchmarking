Custom recorder to intercept and record all allocations and deallocations of [
SuperTuxKart ](https://github.com/supertuxkart/stk-code/tree/master). This recording will then be used to benchmark different memory allocators and determine the best one.

`gcc -shared -fPIC -o recorder.so recorder.c -ldl`

Then using LD_PRELOAD this will be ran with `supertuxkart`.
