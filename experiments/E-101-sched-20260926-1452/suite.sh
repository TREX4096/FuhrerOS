# M16 scheduler comparison (B0 round_robin, B1 priority, B2 low_latency, B3 adaptive)
# scenarios: mixed = 3 CPU + 1 I/O worker + interactive probe; batch = 4 CPU + probe
echo BENCH-BEGIN sched
schedbench -p round_robin -t 10 -c 3 -i 1 -n mixed-r1
schedbench -p priority -t 10 -c 3 -i 1 -n mixed-r1
schedbench -p low_latency -t 10 -c 3 -i 1 -n mixed-r1
schedbench -p adaptive -t 10 -c 3 -i 1 -n mixed-r1
schedbench -p round_robin -t 10 -c 4 -i 0 -n batch-r1
schedbench -p priority -t 10 -c 4 -i 0 -n batch-r1
schedbench -p low_latency -t 10 -c 4 -i 0 -n batch-r1
schedbench -p adaptive -t 10 -c 4 -i 0 -n batch-r1
schedbench -p priority -t 10 -c 3 -i 1 -n mixed-r2
schedbench -p low_latency -t 10 -c 3 -i 1 -n mixed-r2
schedbench -p adaptive -t 10 -c 3 -i 1 -n mixed-r2
schedbench -p round_robin -t 10 -c 3 -i 1 -n mixed-r2
schedbench -p priority -t 10 -c 4 -i 0 -n batch-r2
schedbench -p low_latency -t 10 -c 4 -i 0 -n batch-r2
schedbench -p adaptive -t 10 -c 4 -i 0 -n batch-r2
schedbench -p round_robin -t 10 -c 4 -i 0 -n batch-r2
schedbench -p low_latency -t 10 -c 3 -i 1 -n mixed-r3
schedbench -p adaptive -t 10 -c 3 -i 1 -n mixed-r3
schedbench -p round_robin -t 10 -c 3 -i 1 -n mixed-r3
schedbench -p priority -t 10 -c 3 -i 1 -n mixed-r3
schedbench -p low_latency -t 10 -c 4 -i 0 -n batch-r3
schedbench -p adaptive -t 10 -c 4 -i 0 -n batch-r3
schedbench -p round_robin -t 10 -c 4 -i 0 -n batch-r3
schedbench -p priority -t 10 -c 4 -i 0 -n batch-r3
echo BENCH-END sched
