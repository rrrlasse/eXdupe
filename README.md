# 2.1 gigabyte/second
eXdupe is an ultra fast file archiver that supports data deduplication and differential backups.

It's easy to use. Let's backup the entire D drive on Windows:

`exdupe d:\ d.full`

Each day we can then create a new backup that only stores the changes compared to the initial backup:

`exdupe -D d:\ d.full d1.diff`<br>
`exdupe -D d:\ d.full d2.diff`<br>

It uses a sliding window where identical data blocks as small as 4 KB are being searched at byte grannularity positions across terabytes. Full bakcup of a Windows and a Linux virtual machine of 57,776 MB in total on an Intel i7-12700F with two 6 GB/s SSD disks:
|                 |            size |  time |  options  
|-----------------|----------------:|------:|-----:|
| exdupe 1.1.0          | 28,005 MB  |   24 s  | -g1 -t10|
| zpaq64          | 29,633 MB  |  366 s  | -m1 -t12|
| 7-Zip-flzma2       | 32,331 MB  |  562 s  | fastest|
| restic          | 33,518 MB  |   72 s |  default|
| tar+zstd        | 35,982 MB  |   35 s |  -1 -T0|

Not all data is suited for deduplication and zpaq and lzma will often give much better compression ratios.

Please try the latest version which is now in feature freeze and needs testing. It doubles the speed to 2.1 gigabyte/second (if not limited by storage) and improves compression ratio: [Windows](https://github.com/rrrlasse/exdupe/raw/stuff/beta/exdupe_1.1.0.dev15.exe) and [Linux amd64](https://github.com/rrrlasse/eXdupe/raw/stuff/beta/exdupe_1.1.0.dev15_linux_amd64.tar.gz).

Or get the [latest stable version](https://github.com/rrrlasse/eXdupe/releases/tag/v1.0.0).
<img src="https://github.com/rrrlasse/exdupe/blob/stuff/cmd.webp" width="80%">
