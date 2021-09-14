# 
~~~

Volume Name: three-brick-vol
Type: Distribute
Volume ID: 4c12faf2-6772-4cda-bb0b-52f733da25e2
Status: Started
Snapshot Count: 0
Number of Bricks: 3
Transport-type: tcp
Bricks:
Brick1: node30:/mnt/sdl/three-brick-vol-brick
Brick2: node32:/mnt/sdl/three-brick-vol-brick
Brick3: node33:/mnt/sdl/three-brick-vol-brick
Options Reconfigured:
performance.rda-high-wmark: 1MB
performance.parallel-readdir: on
diagnostics.count-fop-hits: on
diagnostics.latency-measurement: on
storage.fips-mode-rchecksum: on
nfs.disable: on
diagnostics.client-log-level: INFO

~~~

performance.rda-high-wmark: 1MB并没有达到理想的效果
performance.prallel-readdir: on   将readdir 置于dht的下一等， dht调用次数变多
修改了






## 第一次

~~~

deeproute@storage_cluster2:/mnt/three-brick-vol$ time ls vdb.1_1.dir > /dev/null

real	1m29.415s
user	0m4.760s
sys	0m5.922s
deeproute@storage_cluster2:/mnt/three-brick-vol$ sudo gluster v profile three-brick-vol info
Brick: node33:/mnt/sdl/three-brick-vol-brick
--------------------------------------------
Cumulative Stats:
 %-latency   Avg-latency   Min-Latency   Max-Latency   No. of calls         Fop
 ---------   -----------   -----------   -----------   ------------        ----
      0.00       0.00 us       0.00 us       0.00 us              1  RELEASEDIR
      0.00      57.85 us      57.85 us      57.85 us              1     OPENDIR
      0.00     126.24 us      96.37 us     156.10 us              2      LOOKUP
    100.00   12338.17 us     164.21 us   18169.17 us           1217    READDIRP
 
    Duration: 175 seconds
   Data Read: 0 bytes
Data Written: 0 bytes
 
Interval 0 Stats:
 %-latency   Avg-latency   Min-Latency   Max-Latency   No. of calls         Fop
 ---------   -----------   -----------   -----------   ------------        ----
      0.00       0.00 us       0.00 us       0.00 us              1  RELEASEDIR
      0.00      57.85 us      57.85 us      57.85 us              1     OPENDIR
      0.00     126.24 us      96.37 us     156.10 us              2      LOOKUP
    100.00   12338.17 us     164.21 us   18169.17 us           1217    READDIRP
 
    Duration: 175 seconds
   Data Read: 0 bytes
Data Written: 0 bytes
 
Brick: node32:/mnt/sdl/three-brick-vol-brick
--------------------------------------------
Cumulative Stats:
 %-latency   Avg-latency   Min-Latency   Max-Latency   No. of calls         Fop
 ---------   -----------   -----------   -----------   ------------        ----
      0.00       0.00 us       0.00 us       0.00 us              1  RELEASEDIR
      0.00      68.59 us      68.59 us      68.59 us              1     OPENDIR
      0.00     138.70 us     123.63 us     153.76 us              2      LOOKUP
    100.00   11672.27 us     255.03 us   17374.74 us           1222    READDIRP
 
    Duration: 175 seconds
   Data Read: 0 bytes
Data Written: 0 bytes
 
Interval 0 Stats:
 %-latency   Avg-latency   Min-Latency   Max-Latency   No. of calls         Fop
 ---------   -----------   -----------   -----------   ------------        ----
      0.00       0.00 us       0.00 us       0.00 us              1  RELEASEDIR
      0.00      68.59 us      68.59 us      68.59 us              1     OPENDIR
      0.00     138.70 us     123.63 us     153.76 us              2      LOOKUP
    100.00   11672.27 us     255.03 us   17374.74 us           1222    READDIRP
 
    Duration: 175 seconds
   Data Read: 0 bytes
Data Written: 0 bytes
 
Brick: node30:/mnt/sdl/three-brick-vol-brick
--------------------------------------------
Cumulative Stats:
 %-latency   Avg-latency   Min-Latency   Max-Latency   No. of calls         Fop
 ---------   -----------   -----------   -----------   ------------        ----
      0.00       0.00 us       0.00 us       0.00 us              1  RELEASEDIR
      0.00      80.03 us      80.03 us      80.03 us              1     OPENDIR
      0.00     147.83 us     114.80 us     180.87 us              2      LOOKUP
    100.00    9504.85 us     162.42 us   17600.78 us           1221    READDIRP
 
    Duration: 175 seconds
   Data Read: 0 bytes
Data Written: 0 bytes
 
Interval 0 Stats:
 %-latency   Avg-latency   Min-Latency   Max-Latency   No. of calls         Fop
 ---------   -----------   -----------   -----------   ------------        ----
      0.00       0.00 us       0.00 us       0.00 us              1  RELEASEDIR
      0.00      80.03 us      80.03 us      80.03 us              1     OPENDIR
      0.00     147.83 us     114.80 us     180.87 us              2      LOOKUP
    100.00    9504.85 us     162.42 us   17600.78 us           1221    READDIRP
 
    Duration: 175 seconds
   Data Read: 0 bytes
Data Written: 0 bytes
 

~~~


~~~
deeproute@storage_cluster2:/var/log/glusterfs$ cat io-stats-result.20210909 
fuse
Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      410.11 us      410.11 us      410.11 us
LOOKUP                 2      662.53 us      470.56 us      854.51 us
READDIRP          117838      248.34 us       23.96 us    27985.33 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-io-threads

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      395.89 us      395.89 us      395.89 us
LOOKUP                 2      641.51 us      459.20 us      823.83 us
READDIRP          117838      173.76 us       11.59 us    27970.91 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-md-cache

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      382.53 us      382.53 us      382.53 us
LOOKUP                 2      607.33 us      436.36 us      778.31 us
READDIRP          117838      135.26 us        8.28 us    27903.29 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-quick-read

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      379.52 us      379.52 us      379.52 us
LOOKUP                 2      598.69 us      431.35 us      766.03 us
READDIRP          117838      132.95 us        6.86 us    27896.78 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-open-behind

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      376.36 us      376.36 us      376.36 us
LOOKUP                 2      596.31 us      430.31 us      762.32 us
READDIRP          117838      132.33 us        6.16 us    27894.44 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-io-cache

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      373.40 us      373.40 us      373.40 us
LOOKUP                 2      589.03 us      425.48 us      752.59 us
READDIRP          117838      105.82 us        5.39 us    27861.08 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-read-ahead

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      370.32 us      370.32 us      370.32 us
LOOKUP                 2      586.79 us      424.50 us      749.07 us
READDIRP          117838      105.25 us        4.69 us    27858.69 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-write-behind

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      367.35 us      367.35 us      367.35 us
LOOKUP                 2      573.18 us      422.07 us      724.29 us
READDIRP          117838      103.60 us        3.85 us    27852.52 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-utime

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      360.14 us      360.14 us      360.14 us
LOOKUP                 2      566.85 us      418.13 us      715.58 us
READDIRP          117838      103.01 us        3.38 us    27850.40 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-dht

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      242.52 us      242.52 us      242.52 us
LOOKUP                 2      411.63 us      273.00 us      550.26 us
READDIRP           39190       23.40 us        0.89 us    27783.46 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-readdir-ahead-2

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      322.92 us      322.92 us      322.92 us
LOOKUP                 2      407.63 us      348.41 us      466.85 us
READDIRP           39350       17.69 us        1.99 us    24410.88 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-readdir-ahead-1

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      287.93 us      287.93 us      287.93 us
LOOKUP                 2      444.68 us      345.01 us      544.35 us
READDIRP           39300      100.98 us        1.64 us    20552.13 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-readdir-ahead-0

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      193.29 us      193.29 us      193.29 us
LOOKUP                 2      409.63 us      271.65 us      547.61 us
READDIRP            1217    21844.62 us     1116.34 us    27489.15 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-client-2

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      287.65 us      287.65 us      287.65 us
LOOKUP                 2      405.13 us      347.50 us      462.76 us
READDIRP            1222    18789.15 us     1241.01 us    24105.89 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-client-1

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      223.21 us      223.21 us      223.21 us
LOOKUP                 2      441.64 us      343.69 us      539.59 us
READDIRP            1221    12788.92 us      362.41 us    21777.15 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-client-0

~~~