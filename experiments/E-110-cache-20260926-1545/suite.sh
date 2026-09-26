# M16 cache comparison: 5 policies x 4 workloads, 2 repetitions, 64 MiB file, 4 MiB cache
echo BENCH-BEGIN cache
iobench -t 1 -w seq
iobench -p lru -w seq -m 64 -c 1024 -t 6
iobench -p fifo -w seq -m 64 -c 1024 -t 6
iobench -p clock -w seq -m 64 -c 1024 -t 6
iobench -p readahead -w seq -m 64 -c 1024 -t 6
iobench -p adaptive -w seq -m 64 -c 1024 -t 6
iobench -p lru -w random -m 64 -c 1024 -t 6
iobench -p fifo -w random -m 64 -c 1024 -t 6
iobench -p clock -w random -m 64 -c 1024 -t 6
iobench -p readahead -w random -m 64 -c 1024 -t 6
iobench -p adaptive -w random -m 64 -c 1024 -t 6
iobench -p lru -w hotset -m 64 -c 1024 -t 6
iobench -p fifo -w hotset -m 64 -c 1024 -t 6
iobench -p clock -w hotset -m 64 -c 1024 -t 6
iobench -p readahead -w hotset -m 64 -c 1024 -t 6
iobench -p adaptive -w hotset -m 64 -c 1024 -t 6
iobench -p lru -w scan -m 64 -c 1024 -t 6
iobench -p fifo -w scan -m 64 -c 1024 -t 6
iobench -p clock -w scan -m 64 -c 1024 -t 6
iobench -p readahead -w scan -m 64 -c 1024 -t 6
iobench -p adaptive -w scan -m 64 -c 1024 -t 6
iobench -p adaptive -w seq -m 64 -c 1024 -t 6
iobench -p readahead -w seq -m 64 -c 1024 -t 6
iobench -p clock -w seq -m 64 -c 1024 -t 6
iobench -p fifo -w seq -m 64 -c 1024 -t 6
iobench -p lru -w seq -m 64 -c 1024 -t 6
iobench -p adaptive -w random -m 64 -c 1024 -t 6
iobench -p readahead -w random -m 64 -c 1024 -t 6
iobench -p clock -w random -m 64 -c 1024 -t 6
iobench -p fifo -w random -m 64 -c 1024 -t 6
iobench -p lru -w random -m 64 -c 1024 -t 6
iobench -p adaptive -w hotset -m 64 -c 1024 -t 6
iobench -p readahead -w hotset -m 64 -c 1024 -t 6
iobench -p clock -w hotset -m 64 -c 1024 -t 6
iobench -p fifo -w hotset -m 64 -c 1024 -t 6
iobench -p lru -w hotset -m 64 -c 1024 -t 6
iobench -p adaptive -w scan -m 64 -c 1024 -t 6
iobench -p readahead -w scan -m 64 -c 1024 -t 6
iobench -p clock -w scan -m 64 -c 1024 -t 6
iobench -p fifo -w scan -m 64 -c 1024 -t 6
iobench -p lru -w scan -m 64 -c 1024 -t 6
cachectl policy adaptive
echo BENCH-END cache
