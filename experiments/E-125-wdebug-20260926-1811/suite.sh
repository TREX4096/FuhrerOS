# Diagnostic: where does FFS0 write time go? (storage benchmarks with the
# cache and block-device counters before and after)
echo BENCH-BEGIN wdebug
cat /proc/bcache
cat /proc/blk
fubench storage
cat /proc/bcache
cat /proc/blk
cat /proc/sched
cat /proc/blk
echo BENCH-END wdebug
