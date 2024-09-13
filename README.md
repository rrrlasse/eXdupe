## 2.1 gigabyte/second
eXdupe is an ultra fast file archiver that supports deduplication and differential backups.

It's easy to use. Let's backup the entire D drive on Windows:

`exdupe d:\ d.full`

Each day we can then create a new backup that only stores the changes compared to the initial backup:

`exdupe -D d:\ d.full d1.diff`<br>
`exdupe -D d:\ d.full d2.diff`<br>

Identical data blocks as small as 4 KB are being searched at byte grannularity positions across terabytes. Example of a backup of a Windows and a Linux virtual machine of 57,776 MB in total:
|               | exdupe | zpaq64 | 7-Zip-flzma2 | restic | tar+zstd |
|---------------|--------------:|-------:|--------------:|-------:|---------:|
| **Size**          |     28,005 MB | 29,633 MB |     32,331 MB | 33,518 MB | 35,982 MB |
| **Time**          |          24 s |    366 s |         562 s |     72 s |     35 s |
| **Options**       |       -g1 -t10 | -m1 -t12 |       fastest | default |    -1 -T0 |

## Download

&nbsp;&nbsp;&nbsp;[exdupe.exe](https://github.com/rrrlasse/eXdupe/releases/download/v2.0.0/exdupe.exe)
<br>&nbsp;&nbsp;&nbsp;[exdupe_2.0.0_linux_amd64.tar.gz](https://github.com/rrrlasse/eXdupe/releases/download/v2.0.0/exdupe_2.0.0_linux_amd64.tar.gz)

## Building on Linux
    wget https://github.com/rrrlasse/eXdupe/archive/refs/heads/main.tar.gz
    tar -zxf main.tar.gz
    cmake eXdupe-main/src/
    make
## Screen shot
<img src="https://github.com/rrrlasse/exdupe/blob/stuff/cmd.webp" width="90%">
