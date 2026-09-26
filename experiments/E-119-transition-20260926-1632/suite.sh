# Workload transition experiment (NEW_EXPLANATION section 36)
echo BENCH-BEGIN transition
cachectl policy adaptive
transition -s 8 -p adaptive
cachectl policy lru
transition -s 8 -p round_robin
cachectl policy adaptive
echo BENCH-END transition
