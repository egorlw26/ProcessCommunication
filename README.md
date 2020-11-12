# Benchmarks

**Latency** - a time delay between sending and receiving.

**Throughput** - amount of data can be transferred from one location to another per second.

**Capacity** - maximal throughput over some period of time.


| Type                  | Latency    | Throughput   | Capacity   |
| --------------------- | -------:   | ---------:   | -------:   |
| SOCKET                | 0.19 ms    | 175.0 MB/s   | 190.1 MB/s |
| FILE                  | 0.11 ms    | 180.5 MB/s   | 220.1 MB/s |
| MMAP                  | < 0.01 ms  | 210.5 MB/s   | 252.0 MB/s |
| SHARED                | 0.82 ms    | 145.8 MB/s   | 155.3 MB/s |

