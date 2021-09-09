


~~~

141:
142: volume three-brick-vol-readdir-ahead
143:     type performance/readdir-ahead
144:     option parallel-readdir off
145:     option rda-request-size 1048576
146:     option rda-cache-limit 10MB
147:     subvolumes three-brick-vol-5
148: end-volume

~~~




1. 修改fuse配置文件，rda-request-size 1048576， 未生效
~~~
deeproute@storage_cluster2:~$ time ls  /mnt/three-brick-vol/vdb.1_1.dir/ > /dev/null

real	1m30.638s
user	0m4.720s
sys	0m6.024s
deeproute@storage_cluster2:~$ sudo gluster v profile three-brick-vol info 
Brick: node33:/mnt/sdl/three-brick-vol-brick
--------------------------------------------
Cumulative Stats:
 %-latency   Avg-latency   Min-Latency   Max-Latency   No. of calls         Fop
 ---------   -----------   -----------   -----------   ------------        ----
      0.00       0.00 us       0.00 us       0.00 us              1  RELEASEDIR
      0.00      65.86 us      65.86 us      65.86 us              1     OPENDIR
      0.00     135.83 us      82.56 us     189.10 us              2      LOOKUP
    100.00   12713.85 us     315.76 us   18014.10 us           1217    READDIRP
 
    Duration: 126 seconds
   Data Read: 0 bytes
Data Written: 0 bytes
 
Interval 0 Stats:
 %-latency   Avg-latency   Min-Latency   Max-Latency   No. of calls         Fop
 ---------   -----------   -----------   -----------   ------------        ----
      0.00       0.00 us       0.00 us       0.00 us              1  RELEASEDIR
      0.00      65.86 us      65.86 us      65.86 us              1     OPENDIR
      0.00     135.83 us      82.56 us     189.10 us              2      LOOKUP
    100.00   12713.85 us     315.76 us   18014.10 us           1217    READDIRP
 
    Duration: 126 seconds
   Data Read: 0 bytes
Data Written: 0 bytes
 
Brick: node32:/mnt/sdl/three-brick-vol-brick
--------------------------------------------
Cumulative Stats:
 %-latency   Avg-latency   Min-Latency   Max-Latency   No. of calls         Fop
 ---------   -----------   -----------   -----------   ------------        ----
      0.00       0.00 us       0.00 us       0.00 us              1  RELEASEDIR
      0.00      64.95 us      64.95 us      64.95 us              1     OPENDIR
      0.00     133.82 us      89.86 us     177.78 us              2      LOOKUP
    100.00   12202.99 us     215.00 us   16742.32 us           1222    READDIRP
 
    Duration: 126 seconds
   Data Read: 0 bytes
Data Written: 0 bytes
 
Interval 0 Stats:
 %-latency   Avg-latency   Min-Latency   Max-Latency   No. of calls         Fop
 ---------   -----------   -----------   -----------   ------------        ----
      0.00       0.00 us       0.00 us       0.00 us              1  RELEASEDIR
      0.00      64.95 us      64.95 us      64.95 us              1     OPENDIR
      0.00     133.82 us      89.86 us     177.78 us              2      LOOKUP
    100.00   12202.99 us     215.00 us   16742.32 us           1222    READDIRP
 
    Duration: 126 seconds
   Data Read: 0 bytes
Data Written: 0 bytes
 
Brick: node30:/mnt/sdl/three-brick-vol-brick
--------------------------------------------
Cumulative Stats:
 %-latency   Avg-latency   Min-Latency   Max-Latency   No. of calls         Fop
 ---------   -----------   -----------   -----------   ------------        ----
      0.00       0.00 us       0.00 us       0.00 us              1  RELEASEDIR
      0.00      72.22 us      72.22 us      72.22 us              1     OPENDIR
      0.00     135.51 us      77.59 us     193.44 us              2      LOOKUP
    100.00   10321.98 us     128.19 us   20353.94 us           1221    READDIRP
 
    Duration: 126 seconds
   Data Read: 0 bytes
Data Written: 0 bytes
 
Interval 0 Stats:
 %-latency   Avg-latency   Min-Latency   Max-Latency   No. of calls         Fop
 ---------   -----------   -----------   -----------   ------------        ----
      0.00       0.00 us       0.00 us       0.00 us              1  RELEASEDIR
      0.00      72.22 us      72.22 us      72.22 us              1     OPENDIR
      0.00     135.51 us      77.59 us     193.44 us              2      LOOKUP
    100.00   10321.98 us     128.19 us   20353.94 us           1221    READDIRP
 
    Duration: 126 seconds
   Data Read: 0 bytes
Data Written: 0 bytes













// 调用次数没有降下来
fuse
Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      395.47 us      395.47 us      395.47 us
LOOKUP                 2      661.08 us      418.69 us      903.47 us
READDIRP          117837      252.60 us       16.55 us    28383.07 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-io-threads

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      380.89 us      380.89 us      380.89 us
LOOKUP                 2      637.57 us      406.32 us      868.83 us
READDIRP          117837      159.51 us        6.67 us    28369.36 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-md-cache

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      364.69 us      364.69 us      364.69 us
LOOKUP                 2      598.94 us      380.28 us      817.60 us
READDIRP          117837      115.21 us        3.38 us    28331.22 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-quick-read

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      361.44 us      361.44 us      361.44 us
LOOKUP                 2      589.84 us      375.59 us      804.10 us
READDIRP          117837      112.92 us        1.88 us    28325.82 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-open-behind

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      357.98 us      357.98 us      357.98 us
LOOKUP                 2      587.49 us      374.57 us      800.41 us
READDIRP          117837      112.25 us        1.41 us    28323.74 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-io-cache

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      353.84 us      353.84 us      353.84 us
LOOKUP                 2      578.02 us      370.54 us      785.49 us
READDIRP          117837       81.70 us        0.64 us    28303.62 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-readdir-ahead

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      259.25 us      259.25 us      259.25 us
LOOKUP                 2      575.66 us      369.38 us      781.93 us
READDIRP            3658    21523.07 us     2655.46 us    35555.22 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-read-ahead

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      255.92 us      255.92 us      255.92 us
LOOKUP                 2      573.64 us      368.48 us      778.80 us
READDIRP            3658    21521.92 us     2653.42 us    35553.93 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-write-behind

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      252.12 us      252.12 us      252.12 us
LOOKUP                 2      561.72 us      366.26 us      757.18 us
READDIRP            3658    21502.32 us     2647.52 us    35529.51 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-utime

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      245.22 us      245.22 us      245.22 us
LOOKUP                 2      555.71 us      363.35 us      748.08 us
READDIRP            3658    21501.10 us     2645.45 us    35527.98 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-dht

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      196.21 us      196.21 us      196.21 us
LOOKUP                 2      399.06 us      249.48 us      548.63 us
READDIRP            1217    21365.75 us     2407.30 us    27260.79 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-client-2

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      191.95 us      191.95 us      191.95 us
LOOKUP                 2      402.57 us      288.83 us      516.31 us
READDIRP            1222    19238.83 us      428.57 us    23732.04 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-client-1

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      210.91 us      210.91 us      210.91 us
LOOKUP                 2      418.97 us      263.00 us      574.95 us
READDIRP            1221    13555.60 us      430.69 us    24803.34 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-client-0

~~~


2. 修改代码并重新变异，在readdir-ahead xlator中所有初始话之后修改为1M
~~~
deeproute@storage_cluster2:~$ time ls  /mnt/three-brick-vol/vdb.1_1.dir/ > /dev/null

real	1m30.396s
user	0m4.862s
sys	0m5.944s
deeproute@storage_cluster2:~$ sudo gluster v profile three-brick-vol info 
Brick: node30:/mnt/sdl/three-brick-vol-brick
--------------------------------------------
Cumulative Stats:
 %-latency   Avg-latency   Min-Latency   Max-Latency   No. of calls         Fop
 ---------   -----------   -----------   -----------   ------------        ----
      0.00       0.00 us       0.00 us       0.00 us              1  RELEASEDIR
      0.00     188.19 us     173.03 us     203.35 us              2      LOOKUP
      0.01     698.75 us     698.75 us     698.75 us              1     OPENDIR
     99.99   10351.40 us     116.95 us   16315.39 us           1221    READDIRP
 
    Duration: 157 seconds
   Data Read: 0 bytes
Data Written: 0 bytes
 
Interval 0 Stats:
 %-latency   Avg-latency   Min-Latency   Max-Latency   No. of calls         Fop
 ---------   -----------   -----------   -----------   ------------        ----
      0.00       0.00 us       0.00 us       0.00 us              1  RELEASEDIR
      0.00     188.19 us     173.03 us     203.35 us              2      LOOKUP
      0.01     698.75 us     698.75 us     698.75 us              1     OPENDIR
     99.99   10351.40 us     116.95 us   16315.39 us           1221    READDIRP
 
    Duration: 157 seconds
   Data Read: 0 bytes
Data Written: 0 bytes
 
Brick: node33:/mnt/sdl/three-brick-vol-brick
--------------------------------------------
Cumulative Stats:
 %-latency   Avg-latency   Min-Latency   Max-Latency   No. of calls         Fop
 ---------   -----------   -----------   -----------   ------------        ----
      0.00       0.00 us       0.00 us       0.00 us              1  RELEASEDIR
      0.00     130.22 us      79.15 us     181.29 us              2      LOOKUP
      0.00     693.84 us     693.84 us     693.84 us              1     OPENDIR
     99.99   12947.64 us     243.32 us   16931.62 us           1217    READDIRP
 
    Duration: 157 seconds
   Data Read: 0 bytes
Data Written: 0 bytes
 
Interval 0 Stats:
 %-latency   Avg-latency   Min-Latency   Max-Latency   No. of calls         Fop
 ---------   -----------   -----------   -----------   ------------        ----
      0.00       0.00 us       0.00 us       0.00 us              1  RELEASEDIR
      0.00     130.22 us      79.15 us     181.29 us              2      LOOKUP
      0.00     693.84 us     693.84 us     693.84 us              1     OPENDIR
     99.99   12947.64 us     243.32 us   16931.62 us           1217    READDIRP
 
    Duration: 157 seconds
   Data Read: 0 bytes
Data Written: 0 bytes
 
Brick: node32:/mnt/sdl/three-brick-vol-brick
--------------------------------------------
Cumulative Stats:
 %-latency   Avg-latency   Min-Latency   Max-Latency   No. of calls         Fop
 ---------   -----------   -----------   -----------   ------------        ----
      0.00       0.00 us       0.00 us       0.00 us              1  RELEASEDIR
      0.00     116.89 us      79.62 us     177.27 us              3      LOOKUP
      0.00     509.40 us     509.40 us     509.40 us              1     OPENDIR
     99.99   12072.09 us     153.94 us   16010.97 us           1222    READDIRP
 
    Duration: 157 seconds
   Data Read: 0 bytes
Data Written: 0 bytes
 
Interval 0 Stats:
 %-latency   Avg-latency   Min-Latency   Max-Latency   No. of calls         Fop
 ---------   -----------   -----------   -----------   ------------        ----
      0.00       0.00 us       0.00 us       0.00 us              1  RELEASEDIR
      0.00     116.89 us      79.62 us     177.27 us              3      LOOKUP
      0.00     509.40 us     509.40 us     509.40 us              1     OPENDIR
     99.99   12072.09 us     153.94 us   16010.97 us           1222    READDIRP
 
    Duration: 157 seconds
   Data Read: 0 bytes
Data Written: 0 bytes
 


deeproute@storage_cluster2:~$ cat /var/log/glusterfs/io-stats-result.20210909 
fuse
Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1     1039.64 us     1039.64 us     1039.64 us
LOOKUP                 3      650.96 us       42.44 us      969.14 us
READDIRP          117837      248.89 us       18.62 us    28786.82 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-io-threads

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1     1023.57 us     1023.57 us     1023.57 us
LOOKUP                 3      618.82 us        8.96 us      954.42 us
READDIRP          117837      155.62 us        7.41 us    28772.55 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-md-cache

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1     1008.05 us     1008.05 us     1008.05 us
LOOKUP                 2      881.25 us      843.64 us      918.86 us
READDIRP          117837      112.60 us        3.50 us    28734.40 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-quick-read

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1     1004.92 us     1004.92 us     1004.92 us
LOOKUP                 2      868.54 us      827.04 us      910.03 us
READDIRP          117837      110.31 us        2.18 us    28728.31 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-open-behind

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1     1002.57 us     1002.57 us     1002.57 us
LOOKUP                 2      865.46 us      822.70 us      908.21 us
READDIRP          117837      109.62 us        1.69 us    28726.24 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-io-cache

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      999.58 us      999.58 us      999.58 us
LOOKUP                 2      854.61 us      809.14 us      900.07 us
READDIRP          117837       78.33 us        0.91 us    28703.45 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-readdir-ahead

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      901.97 us      901.97 us      901.97 us
LOOKUP                 2      851.50 us      804.53 us      898.47 us
READDIRP            3658    21647.18 us     3552.03 us    36213.99 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-read-ahead

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      899.83 us      899.83 us      899.83 us
LOOKUP                 2      848.95 us      800.90 us      897.00 us
READDIRP            3658    21645.92 us     3551.02 us    36213.05 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-write-behind

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      897.40 us      897.40 us      897.40 us
LOOKUP                 2      841.30 us      789.38 us      893.23 us
READDIRP            3658    21626.91 us     3548.07 us    36190.14 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-utime

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      891.97 us      891.97 us      891.97 us
LOOKUP                 2      832.04 us      777.02 us      887.06 us
READDIRP            3658    21625.69 us     3547.01 us    36188.88 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-dht

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      831.24 us      831.24 us      831.24 us
LOOKUP                 2      434.96 us      290.77 us      579.16 us
READDIRP            1217    21778.55 us     1950.40 us    29476.53 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-client-2

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      637.84 us      637.84 us      637.84 us
LOOKUP                 3      429.36 us      281.86 us      516.48 us
READDIRP            1222    19467.33 us      521.15 us    24417.49 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-client-1

Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      829.15 us      829.15 us      829.15 us
LOOKUP                 2      520.99 us      503.70 us      538.27 us
READDIRP            1221    13683.99 us      856.95 us    22768.72 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----

~~~