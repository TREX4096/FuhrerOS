# Diagnostic: where does FFS0 write time go? (storage benchmarks with the
# cache and block-device counters before and after)
echo BENCH-BEGIN wdebug
cat /proc/bcache
fubench storage
cat /proc/bcache
cat /proc/sched
echo BENCH-END wdebug
