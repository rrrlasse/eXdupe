eXdupe is an ultra fast file archiver that supports incremental backups and sliding-window deduplication.

It's easy to use. Example of backup:

```
./exdupe ~/Desktop backup.exd
```
We can then each day add a new backup to the archive that will only store the changes:
```
./exdupe ~/Desktop backup.exd
./exdupe ~/Desktop backup.exd
...
```
## Benchmark 
Backup of a Linux virtual machine of 22.1 GB on an average desktop computer with two SSDs using 12 threads (if customizable):

|             | eXdupe | tar + zstd | kopia | restic | Duplicacy | zpaq64 | 7-Zip lzma2 | Duplicati |
|-----------------|----------|----------|-------|---------|-----------|--------|-------------|------------|
| **Time**        | 9.76 s   | 14.2 s   | 14.8 s| 24.8 s  | 77.0 s    | 112 s  | 209 s       | 360 s      |
| **Size** | 7.34 GB  | 10.6 GB  | 9.93 GB | 9.21 GB | 11.4 GB | 8.18 GB | 9.42 GB   | 10.2 GB    |

Incremental backup after some random work inside the virtual machine:

|      | eXdupe | kopia   | restic  | Duplicacy | Duplicati |
| -------- | -------- | ------- | ------- | --------- | --------- |
| **Size** | 0.77 GB  | 2.42 GB | 1.78 GB | 3.10 GB   | 1.62 GB   |

## Download
Please try the upcoming [version 4.x](https://github.com/rrrlasse/eXdupe/releases/tag/nightly) that supports **incremental** backups like described above.

Last stable version that only supports **differential** backups with a different syntax:

[exdupe.exe](https://github.com/rrrlasse/eXdupe/releases/download/v3.0.1/exdupe.exe)<br>
[exdupe_3.0.1_linux_amd64.tar.gz](https://github.com/rrrlasse/eXdupe/releases/download/v3.0.1/exdupe_3.0.1_linux_amd64.tar.gz)

## Build
It has been tested on Windows, Linux and FreeBSD.
```
wget https://github.com/rrrlasse/eXdupe/archive/refs/heads/4.x.tar.gz
tar -zxf 4.x.tar.gz
cmake -DCMAKE_BUILD_TYPE=Release eXdupe-4.x/src/
make
```
## About
It uses a new sliding-window deduplication algorithm to find identical data blocks as small as 4 KB at byte grannularity positions across terabytes. Traditional compression is optionally applied afterwards.

It reaches **4.7 gigabyte/second** (command line flags -g1t3x0) with just 3 threads if not disk bound.
<img src="https://exdupe.net/exdupe3.png" width="90%">
