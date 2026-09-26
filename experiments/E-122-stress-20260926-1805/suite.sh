# M17 desktop stress / §35 mixed workload: browser + build proxy + file copy +
# network + interactive probe, per scheduling policy, 3 repetitions, order rotated
echo BENCH-BEGIN stress
deskstress -p round_robin -t 10 -n stress-r1
deskstress -p priority -t 10 -n stress-r1
deskstress -p low_latency -t 10 -n stress-r1
deskstress -p adaptive -t 10 -n stress-r1
deskstress -p priority -t 10 -n stress-r2
deskstress -p low_latency -t 10 -n stress-r2
deskstress -p adaptive -t 10 -n stress-r2
deskstress -p round_robin -t 10 -n stress-r2
deskstress -p low_latency -t 10 -n stress-r3
deskstress -p adaptive -t 10 -n stress-r3
deskstress -p round_robin -t 10 -n stress-r3
deskstress -p priority -t 10 -n stress-r3
sched policy adaptive
echo BENCH-END stress
