## 操作步骤
创卷修改参数
sudo ./a.out /var/lib/glusterd/vols/volname/trust*fuse*

mount -t glusterfs hosts:volname /mountpoint

gluster v profile volname info clear
kill -SIGUSR1 glusterfs_pid

time ls /mountpoint/dir > /dev/null

gluster v profile volname info
kill -SIGUSR1 glusterfs_pid

cat /var/log/glusterfs/io-stat*result*



## 结果
deeproute@storage_cluster2:~$ sudo gluster v profile three-brick-vol info
Brick: node33:/mnt/sdl/three-brick-vol-brick
--------------------------------------------
Cumulative Stats:
 %-latency   Avg-latency   Min-Latency   Max-Latency   No. of calls         Fop
 ---------   -----------   -----------   -----------   ------------        ----
      0.00       0.00 us       0.00 us       0.00 us              1  RELEASEDIR
      0.00     228.81 us     228.81 us     228.81 us              1     OPENDIR
      0.00     120.99 us      84.71 us     157.26 us              2      LOOKUP
    100.00   12626.25 us     320.22 us   17702.47 us           1217    READDIRP
  
    Duration: 206 seconds
   Data Read: 0 bytes
Data Written: 0 bytes
  
Brick: node30:/mnt/sdl/three-brick-vol-brick
--------------------------------------------
Cumulative Stats:
 %-latency   Avg-latency   Min-Latency   Max-Latency   No. of calls         Fop
 ---------   -----------   -----------   -----------   ------------        ----
      0.00       0.00 us       0.00 us       0.00 us              1  RELEASEDIR
      0.00     142.42 us      92.14 us     192.69 us              2      LOOKUP
      0.03    4236.59 us    4236.59 us    4236.59 us              1     OPENDIR
     99.97   10619.09 us     118.26 us   15872.15 us           1221    READDIRP
  
    Duration: 205 seconds
   Data Read: 0 bytes
Data Written: 0 bytes
  
  
Brick: node32:/mnt/sdl/three-brick-vol-brick
--------------------------------------------
Cumulative Stats:
 %-latency   Avg-latency   Min-Latency   Max-Latency   No. of calls         Fop
 ---------   -----------   -----------   -----------   ------------        ----
      0.00       0.00 us       0.00 us       0.00 us              1  RELEASEDIR
      0.00     160.03 us     105.37 us     214.69 us              2      LOOKUP
      0.00     560.56 us     560.56 us     560.56 us              1     OPENDIR
     99.99   12412.26 us     119.46 us   17168.83 us           1222    READDIRP
  
    Duration: 206 seconds
   Data Read: 0 bytes
Data Written: 0 bytes










在各个xlator之间插入io-stats xlator， 执行ls命令之后使用工具收集信息
fuse
Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1     9426.19 us     9426.19 us     9426.19 us
LOOKUP                 2     1339.87 us      488.04 us     2191.70 us
READDIRP          117837      241.61 us       19.51 us    33837.44 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-io-threads
 
Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1     9412.08 us     9412.08 us     9412.08 us
LOOKUP                 2     1314.82 us      473.74 us     2155.90 us
READDIRP          117837      136.51 us        7.54 us    33824.75 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-md-cache
 
Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1     9397.39 us     9397.39 us     9397.39 us
LOOKUP                 2     1276.34 us      446.11 us     2106.57 us
READDIRP          117837       95.17 us        4.26 us    33785.60 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-quick-read
 
Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1     9394.64 us     9394.64 us     9394.64 us
LOOKUP                 2     1261.88 us      439.25 us     2084.51 us
READDIRP          117837       92.91 us        2.92 us    33779.97 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-open-behind
 
Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1     9392.12 us     9392.12 us     9392.12 us
LOOKUP                 2     1258.65 us      437.99 us     2079.32 us
READDIRP          117837       92.28 us        2.23 us    33777.92 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-io-cache
 
Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1     9389.24 us     9389.24 us     9389.24 us
LOOKUP                 2     1249.31 us      432.55 us     2066.06 us
READDIRP          117837       61.74 us        1.00 us    33759.97 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-readdir-ahead
 
Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1     4413.17 us     4413.17 us     4413.17 us
LOOKUP                 2     1246.48 us      431.55 us     2061.40 us
READDIRP            3658    21950.10 us     2688.51 us    52983.17 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-read-ahead
 
Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1     4410.71 us     4410.71 us     4410.71 us
LOOKUP                 2     1243.81 us      430.44 us     2057.18 us
READDIRP            3658    21948.88 us     2687.59 us    52981.29 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-write-behind
 
Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1     4408.07 us     4408.07 us     4408.07 us
LOOKUP                 2     1221.69 us      397.59 us     2045.79 us
READDIRP            3658    21927.02 us     2685.64 us    52954.91 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-utime
 
Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1     4403.21 us     4403.21 us     4403.21 us
LOOKUP                 2     1213.44 us      393.20 us     2033.68 us
READDIRP            3658    21925.68 us     2684.66 us    52953.16 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-dht
 
Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      358.18 us      358.18 us      358.18 us
LOOKUP                 2     1062.59 us      292.39 us     1832.78 us
READDIRP            1217    20868.64 us     2658.01 us    32623.14 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-client-2
 
Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1      728.89 us      728.89 us      728.89 us
LOOKUP                 2     1086.44 us      300.69 us     1872.20 us
READDIRP            1222    18827.03 us     1833.29 us    24677.58 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-client-1
 
Fop           Call Count    Avg-Latency    Min-Latency    Max-Latency
---           ----------    -----------    -----------    -----------
OPENDIR                1     4393.85 us     4393.85 us     4393.85 us
LOOKUP                 2      455.07 us      324.50 us      585.63 us
READDIRP            1221    14549.07 us     1498.65 us    35392.20 us
RELEASEDIR             1           0 us           0 us           0 us
------ ----- ----- ----- ----- ----- ----- -----  ----- ----- ----- -----
three-brick-vol-client-0





