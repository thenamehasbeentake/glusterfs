## glusterd状态不一致
### 操作方法
1. 创建gluster集群，两个brick，创建卷vol如下
root@wxb_1:~# gluster v info
Volume Name: vol
Type: Distribute
Volume ID: 0bdf52be-1abd-4e39-8863-372fa2c2ad9e
Status: Created
Snapshot Count: 0
Number of Bricks: 2
Transport-type: tcp
Bricks:
Brick1: wxb3:/mnt/sdb/brick
Brick2: wxb4:/mnt/sdb/brick
Options Reconfigured:
transport.address-family: inet
storage.fips-mode-rchecksum: on
nfs.disable: on

2. 在其中一个卷中删除配置文件/var/lib/glusterd/vols/卷名， 重启glusterd
此时两个节点执行gluster v info命令都可以显示卷的信息

3. 在有配置文件的节点执行gluster v delete vol
    ~~~
    root@wxb_1:~# gluster v delete vol
    Deleting volume will erase all information about the volume. Do you want to continue? (y/n) y
    volume delete: vol: failed: Commit failed on wxb3. Please check log file for details.

    he message "I [MSGID: 106488] [glusterd-handler.c:1400:__glusterd_handle_cli_get_volume] 0-management: Received get vol req" repeated 5 times between [2021-08-29 05:22:27.139705] and [2021-08-29 05:23:06.511888]
    [2021-08-29 05:23:49.835827] I [MSGID: 106488] [glusterd-handler.c:1400:__glusterd_handle_cli_get_volume] 0-management: Received get vol req
    [2021-08-29 05:24:18.095274] I [glusterd-volume-ops.c:1714:glusterd_op_stage_delete_volume] 0-management: Setting stage deleted flag to true for volume vol
    [2021-08-29 05:24:18.102458] I [run.c:242:runner_log] (-->/usr/local/lib/glusterfs/7.5/xlator/mgmt/glusterd.so(+0x37bcd) [0x7fcce967cbcd] -->/usr/local/lib/glusterfs/7.5/xlator/mgmt/glusterd.so(+0xcb0ac) [0x7fcce97100ac] -->/usr/local/lib/libglusterfs.so.0(runner_log+0x105) [0x7fcceef877b5] ) 0-management: Ran script: /var/lib/glusterd/hooks/1/delete/pre/S10selinux-del-fcontext.sh --volname=vol
    [2021-08-29 05:24:18.108817] E [MSGID: 106152] [glusterd-syncop.c:104:gd_collate_errors] 0-glusterd: Commit failed on wxb3. Please check log file for details.
    [2021-08-29 05:24:18.199893] E [run.c:242:runner_log] (-->/usr/local/lib/glusterfs/7.5/xlator/mgmt/glusterd.so(+0xcb58a) [0x7fcce971058a] -->/usr/local/lib/glusterfs/7.5/xlator/mgmt/glusterd.so(+0xcb052) [0x7fcce9710052] -->/usr/local/lib/libglusterfs.so.0(runner_log+0x105) [0x7fcceef877b5] ) 0-management: Failed to execute script: /var/lib/glusterd/hooks/1/delete/post/S57glusterfind-delete-post --volname=vol
    ~~~

执行删除命令并报错的卷，删除成功
root@wxb_1:/var/lib/glusterd/vols# gluster v status
No volumes present
root@wxb_1:/var/lib/glusterd/vols# ls /var/lib/glusterd/vols/
root@wxb_1:/var/lib/glusterd/vols# 

手动删除配置文件并重启的卷，删除失败
root@wxb_1:/var/lib/glusterd/vols# gluster v status
Volume vol is not started
 
root@wxb_1:/var/lib/glusterd/vols# ls /var/lib/glusterd/vols/

### 原因
glusterd进程没有对gluster系列命令执行出错做回滚操作，当gluster命令执行失败，可以会出现不一致的问题。

### 重启glusterd可以恢复卷配置文件
root@wxb_1:/var/lib/glusterd/vols# gluster v status
No volumes present
root@wxb_1:/var/lib/glusterd/vols# ls /var/lib/glusterd/vols/
root@wxb_1:/var/lib/glusterd/vols# systemctl restart glusterd
root@wxb_1:/var/lib/glusterd/vols# gluster v status
Volume vol is not started
 
root@wxb_1:/var/lib/glusterd/vols# ls /var/lib/glusterd/


### glusterd进程开启与关闭不影响fuse读写，但是会影响gluster命令(比如一些监控脚本)
- 其中一个节点
root@wxb_1:/var/lib/glusterd/vols# systemctl stop glusterd
root@wxb_1:/var/lib/glusterd/vols# systemctl status glusterd
● glusterd.service - LSB: Gluster File System service for volume management
   Loaded: loaded (/etc/init.d/glusterd; generated)
   Active: inactive (dead) since Sun 2021-08-29 06:24:26 UTC; 4s ago
     Docs: man:systemd-sysv-generator(8)
  Process: 81926 ExecStop=/etc/init.d/glusterd stop (code=exited, status=0/SUCCESS)
  Process: 81613 ExecStart=/etc/init.d/glusterd start (code=exited, status=0/SUCCESS)
    Tasks: 21 (limit: 2287)
   CGroup: /system.slice/glusterd.service
           └─81721 /usr/local/sbin/glusterfsd -s wxb4 --volfile-id vol.wxb4.mnt-sdb-brick -p /var/run/gluster/vols/vol/wxb4-mnt-sdb-brick.pid -S /var/run/gluster/98ed0ec0ae560585.socket --brick-name /mnt/sdb/brick -l /var/log/glusterfs/bricks/mnt-sdb-brick.log --xlator-

Aug 29 06:20:13 wxb_1 systemd[1]: Stopped LSB: Gluster File System service for volume management.
Aug 29 06:20:13 wxb_1 systemd[1]: Starting LSB: Gluster File System service for volume management...
Aug 29 06:20:13 wxb_1 glusterd[81613]:  * Starting glusterd service glusterd
Aug 29 06:20:14 wxb_1 glusterd[81613]:    ...done.
Aug 29 06:20:14 wxb_1 systemd[1]: Started LSB: Gluster File System service for volume management.
Aug 29 06:24:26 wxb_1 systemd[1]: Stopping LSB: Gluster File System service for volume management...
Aug 29 06:24:26 wxb_1 glusterd[81926]:  * Stopping glusterd service glusterd
Aug 29 06:24:26 wxb_1 glusterd[81926]:    ...done.
Aug 29 06:24:26 wxb_1 systemd[1]: Stopped LSB: Gluster File System service for volume management.
root@wxb_1:/var/lib/glusterd/vols# touch /mnt/vol/file{1..100}
root@wxb_1:/var/lib/glusterd/vols# df /mnt/vol/
Filesystem     1K-blocks   Used Available Use% Mounted on
wxb3:vol        16756736 250624  16506112   2% /mnt/vol
root@wxb_1:/var/lib/glusterd/vols# ls /mnt/vol/
file1    file12  file16  file2   file23  file27  file30  file34  file38  file41  file45  file49  file52  file56  file6   file63  file67  file70  file74  file78  file81  file85  file89  file92  file96
file10   file13  file17  file20  file24  file28  file31  file35  file39  file42  file46  file5   file53  file57  file60  file64  file68  file71  file75  file79  file82  file86  file9   file93  file97
file100  file14  file18  file21  file25  file29  file32  file36  file4   file43  file47  file50  file54  file58  file61  file65  file69  file72  file76  file8   file83  file87  file90  file94  file98
file11   file15  file19  file22  file26  file3   file33  file37  file40  file44  file48  file51  file55  file59  file62  file66  file7   file73  file77  file80  file84  file88  file91  file95  file99
root@wxb_1:/var/lib/glusterd/vols# ls /mnt/sdb/brick/
file1   file15  file20  file22  file26  file29  file33  file35  file39  file49  file51  file56  file59  file60  file64  file67  file70  file74  file77  file79  file81  file83  file85  file89  file92  file95  file98
file14  file2   file21  file23  file27  file30  file34  file36  file45  file5   file52  file57  file6   file61  file65  file68  file72  file76  file78  file8   file82  file84  file88  file90  file94  file96  file99
root@wxb_1:/var/lib/glusterd/vols# gluster v status
Connection failed. Please check if gluster daemon is operational.
root@wxb_1:/var/lib/glusterd/vols# 


- 另一个节点
root@wxb_1:/var/lib/glusterd/vols# systemctl stop glusterd
root@wxb_1:/var/lib/glusterd/vols# ls
vol
root@wxb_1:/var/lib/glusterd/vols# systemctl status glusterd
● glusterd.service - LSB: Gluster File System service for volume management
   Loaded: loaded (/etc/init.d/glusterd; generated)
   Active: inactive (dead) since Sun 2021-08-29 06:24:38 UTC; 7s ago
     Docs: man:systemd-sysv-generator(8)
  Process: 93485 ExecStop=/etc/init.d/glusterd stop (code=exited, status=0/SUCCESS)
  Process: 93231 ExecStart=/etc/init.d/glusterd start (code=exited, status=0/SUCCESS)
    Tasks: 20 (limit: 2287)
   CGroup: /system.slice/glusterd.service
           └─93436 /usr/local/sbin/glusterfsd -s wxb3 --volfile-id vol.wxb3.mnt-sdb-brick -p /var/run/gluster/vols/vol/wxb3-mnt-sdb-brick.pid -S /var/run/gluster/1310c4cebf4020a3.socket --brick-name /mnt/sdb/brick -l /var/log/glusterfs/bricks/mnt-sdb-brick.log --xlator-

Aug 29 06:22:18 wxb_1 systemd[1]: Starting LSB: Gluster File System service for volume management...
Aug 29 06:22:18 wxb_1 glusterd[93231]:  * Starting glusterd service glusterd
Aug 29 06:22:19 wxb_1 glusterd[93231]:    ...done.
Aug 29 06:22:19 wxb_1 systemd[1]: Started LSB: Gluster File System service for volume management.
Aug 29 06:24:38 wxb_1 systemd[1]: Stopping LSB: Gluster File System service for volume management...
Aug 29 06:24:38 wxb_1 glusterd[93485]:  * Stopping glusterd service glusterd
Aug 29 06:24:38 wxb_1 glusterd[93485]:    ...done.
Aug 29 06:24:38 wxb_1 systemd[1]: Stopped LSB: Gluster File System service for volume management.
root@wxb_1:/var/lib/glusterd/vols# ls /mnt/sdb/brick/
file10   file11  file13  file17  file19  file25  file3   file32  file38  file40  file42  file44  file47  file50  file54  file58  file63  file69  file71  file75  file86  file9   file93
file100  file12  file16  file18  file24  file28  file31  file37  file4   file41  file43  file46  file48  file53  file55  file62  file66  file7   file73  file80  file87  file91  file97


