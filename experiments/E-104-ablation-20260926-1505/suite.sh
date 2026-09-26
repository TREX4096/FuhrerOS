# Ablations A1-A7 on the mixed scenario (3 repetitions each)
echo BENCH-BEGIN ablation
sched profiler on
sched window 100
sched hysteresis 2
schedbench -p priority -t 10 -c 3 -i 1 -n A1-controller-off-r1
sched profiler off
schedbench -p adaptive -t 10 -c 3 -i 1 -n A2-profiling-off-r1
sched profiler on
schedbench -p round_robin -t 10 -c 3 -i 1 -n A3-profile-no-switch-r1
schedbench -p adaptive -t 10 -c 3 -i 1 -n A4-adaptive-r1
sched window 25
schedbench -p adaptive -t 10 -c 3 -i 1 -n A6-window25-r1
sched window 500
schedbench -p adaptive -t 10 -c 3 -i 1 -n A6-window500-r1
sched window 100
sched hysteresis 1
schedbench -p adaptive -t 10 -c 3 -i 1 -n A7-hyst1-r1
sched hysteresis 5
schedbench -p adaptive -t 10 -c 3 -i 1 -n A7-hyst5-r1
sched hysteresis 2
sched profiler on
sched window 100
sched hysteresis 2
schedbench -p priority -t 10 -c 3 -i 1 -n A1-controller-off-r2
sched profiler off
schedbench -p adaptive -t 10 -c 3 -i 1 -n A2-profiling-off-r2
sched profiler on
schedbench -p round_robin -t 10 -c 3 -i 1 -n A3-profile-no-switch-r2
schedbench -p adaptive -t 10 -c 3 -i 1 -n A4-adaptive-r2
sched window 25
schedbench -p adaptive -t 10 -c 3 -i 1 -n A6-window25-r2
sched window 500
schedbench -p adaptive -t 10 -c 3 -i 1 -n A6-window500-r2
sched window 100
sched hysteresis 1
schedbench -p adaptive -t 10 -c 3 -i 1 -n A7-hyst1-r2
sched hysteresis 5
schedbench -p adaptive -t 10 -c 3 -i 1 -n A7-hyst5-r2
sched hysteresis 2
sched profiler on
sched window 100
sched hysteresis 2
schedbench -p priority -t 10 -c 3 -i 1 -n A1-controller-off-r3
sched profiler off
schedbench -p adaptive -t 10 -c 3 -i 1 -n A2-profiling-off-r3
sched profiler on
schedbench -p round_robin -t 10 -c 3 -i 1 -n A3-profile-no-switch-r3
schedbench -p adaptive -t 10 -c 3 -i 1 -n A4-adaptive-r3
sched window 25
schedbench -p adaptive -t 10 -c 3 -i 1 -n A6-window25-r3
sched window 500
schedbench -p adaptive -t 10 -c 3 -i 1 -n A6-window500-r3
sched window 100
sched hysteresis 1
schedbench -p adaptive -t 10 -c 3 -i 1 -n A7-hyst1-r3
sched hysteresis 5
schedbench -p adaptive -t 10 -c 3 -i 1 -n A7-hyst5-r3
sched hysteresis 2
sched
echo BENCH-END ablation
