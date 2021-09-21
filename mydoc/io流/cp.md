cp /mnt/vol1/dir1/dir2/dir3/file /mnt/vol2/dir1/dir2/dir3/file

## vol1

1. fuse_getattr, 由/目录开始，层层lookup，直到/mnt/vol1/dir1/dir2/dir3/file。

2.  OPEN /dir1/dir2/dir3/file. OPEN() /dir1/dir2/dir3/file => 0x7f05e800b338

3. READ (0x7f05e800b338, size=4096, offset=0)

4. EC(READ) 0x7f05e0004d98, EC(INODELK) 0x7f05e0013438,   EC(FXATTROP) 0x7f05e0013438(key 'trusted.ec.config'),   READ => 0/4096,0/0

5. FLUSH 0x7f05e800b338, FLUSH() ERR => 0


## vol2


---
fuse-bridge.c:1372:fuse_getattr

default_lookup

 [ec-generic.c:739:ec_wind_lookup] 0-stack-trace: stack-address: 0x7f05e000b938, winding from vol1-disperse-0 to vol1-client-0

 [2021-09-15 14:01:50.172511] T [socket.c:3000:socket_event_handler] 0-vol1-client-0: client (sock:13) in:1, out:0, err:0
[2021-09-15 14:01:50.172515] T [socket.c:3026:socket_event_handler] 0-vol1-client-0: Client socket (13) is already connected
[2021-09-15 14:01:50.172518] T [socket.c:574:__socket_ssl_readv] 0-vol1-client-0: ***** reading over non-SSL
[2021-09-15 14:01:50.172522] T [socket.c:574:__socket_ssl_readv] 0-vol1-client-0: ***** reading over non-SSL
[2021-09-15 14:01:50.172529] T [rpc-clnt.c:662:rpc_clnt_reply_init] 0-vol1-client-0: received rpc message (RPC XID: 0x79e Program: GF-DUMP, ProgVers: 1, Proc: 2) from rpc-transport (vol1-client-0)
[2021-09-15 14:01:50.172532] D [rpc-clnt-ping.c:195:rpc_clnt_ping_cbk] 0-vol1-client-0: Ping latency is 0ms
[2021-09-15 14:01:50.172537] T [socket.c:3044:socket_event_handler] 0-vol1-client-0: (sock:13) socket_event_poll_in returned 0
[2021-09-15 14:01:50.172540] T [socket.c:3000:socket_event_handler] 0-vol1-client-1: client (sock:14) in:1, out:0, err:0
[2021-09-15 14:01:50.172543] T [socket.c:3026:socket_event_handler] 0-vol1-client-1: Client socket (14) is already connected




[2021-09-15 14:01:50.172815] T [MSGID: 0] [ec-helpers.c:85:ec_trace] 0-ec: CBK(LOOKUP) 0x7f05e0003ef8((nil)) [refs=5, winds=3, jobs=1] frame=0x7f05e0020c98/0x7f05e0009838, min/exp=2/3, err=0 state=4 {111:000:000} idx=0, frame=0x7f05e0009838, op_ret=0, op_errno=0
[2021-09-15 14:01:50.172824] T [MSGID: 0] [ec-helpers.c:85:ec_trace] 0-ec: ANSWER(LOOKUP) 0x7f05e0003ef8((nil)) [refs=5, winds=3, jobs=1] frame=0x7f05e0020c98/0x7f05e0009838, min/exp=2/3, err=0 state=4 {111:000:000} combine=1[1]
[2021-09-15 14:01:50.172829] T [MSGID: 0] [ec-helpers.c:85:ec_trace] 0-ec: COMPLETE(LOOKUP) 0x7f05e0003ef8((nil)) [refs=5, winds=3, jobs=1] frame=0x7f05e0020c98/0x7f05e0009838, min/exp=2/3, err=0 state=4 {111:000:000}
[2021-09-15 14:01:50.172833] T [MSGID: 0] [ec-helpers.c:85:ec_trace] 0-ec: RELEASE(LOOKUP) 0x7f05e0003ef8((nil)) [refs=5, winds=2, jobs=1] frame=0x7f05e0020c98/0x7f05e0009838, min/exp=2/3, err=0 state=4 {111:000:000}




[2021-09-15 14:01:50.173237] T [MSGID: 0] [write-behind.c:2439:wb_lookup_cbk] 0-stack-trace: stack-address: 0x7f05e800f0f8, vol1-write-behind returned 0
[2021-09-15 14:01:50.173241] T [MSGID: 0] [io-cache.c:258:ioc_inode_update] 0-vol1-io-cache: locked inode(0x7f05ec015a40)
[2021-09-15 14:01:50.173244] T [MSGID: 0] [io-cache.c:267:ioc_inode_update] 0-vol1-io-cache: unlocked inode(0x7f05ec015a40)
[2021-09-15 14:01:50.173247] T [MSGID: 0] [io-cache.c:185:ioc_inode_flush] 0-vol1-io-cache: locked inode(0x7f05ec015a40)
[2021-09-15 14:01:50.173250] T [MSGID: 0] [io-cache.c:189:ioc_inode_flush] 0-vol1-io-cache: unlocked inode(0x7f05ec015a40)
[2021-09-15 14:01:50.173253] T [MSGID: 0] [io-cache.c:275:ioc_inode_update] 0-vol1-io-cache: locked table(0x7f05f4041080)
[2021-09-15 14:01:50.173256] T [MSGID: 0] [io-cache.c:280:ioc_inode_update] 0-vol1-io-cache: unlocked table(0x7f05f4041080)
[2021-09-15 14:01:50.173259] T [MSGID: 0] [io-cache.c:318:ioc_lookup_cbk] 0-stack-trace: stack-address: 0x7f05e800f0f8, vol1-io-cache returned 0





 T [fuse-bridge.c:1223:fuse_attr_cbk] 0-glusterfs-fuse: 3881: LOOKUP() / => 1
[2021-09-15 14:01:50.173318] T [fuse-bridge.c:290:send_fuse_iov] 0-glusterfs-fuse: writev() result 120/120




[2021-09-15 14:01:50.173356] T [fuse-bridge.c:1054:fuse_lookup_resume] 0-glusterfs-fuse: 3925: LOOKUP /dir1(efe262df-2953-4c7f-a521-2660256b7712)




[2021-09-15 14:01:50.174663] T [fuse-bridge.c:907:fuse_entry_cbk] 0-glusterfs-fuse: 3885: LOOKUP() /dir1 => 11898833884876076818
[2021-09-15 14:01:50.174680] T [fuse-bridge.c:290:send_fuse_iov] 0-glusterfs-fuse: writev() result 144/144
[2021-09-15 14:01:50.174689] T [MSGID: 0] [ec-helpers.c:85:ec_trace] 0-ec: MANAGER(LOOKUP) 0x7f05e00367b8((nil)) [refs=3, winds=0, jobs=0] frame=0x7f05e00093d8/0x7f05e001a688, min/exp=2/3, err=0 state=0 {111:000:111} error=0
[2021-09-15 14:01:50.174694] T [MSGID: 0] [ec-helpers.c:85:ec_trace] 0-ec: RELEASE(LOOKUP) 0x7f05e00367b8((nil)) [refs=3, winds=0, jobs=0] frame=0x7f05e00093d8/0x7f05e001a688, min/exp=2/3, err=0 state=0 {111:000:111}
[2021-09-15 14:01:50.174698] T [MSGID: 0] [ec-helpers.c:85:ec_trace] 0-ec: RELEASE(LOOKUP) 0x7f05e00367b8((nil)) [refs=2, winds=0, jobs=0] frame=0x7f05e00093d8/0x7f05e001a688, min/exp=2/3, err=0 state=0 {111:000:111}
[2021-09-15 14:01:50.174701] T [MSGID: 0] [ec-helpers.c:85:ec_trace] 0-ec: RELEASE(LOOKUP) 0x7f05e00367b8((nil)) [refs=1, winds=0, jobs=0] frame=0x7f05e00093d8/0x7f05e001a688, min/exp=2/3, err=0 state=0 {111:000:111}




[2021-09-15 14:01:50.178197] T [fuse-bridge.c:907:fuse_entry_cbk] 0-glusterfs-fuse: 3888: LOOKUP() /dir1/dir2/dir3/file => 10847316928109268235
[2021-09-15 14:01:50.178215] T [fuse-bridge.c:290:send_fuse_iov] 0-glusterfs-fuse: writev() result 144/144







[2021-09-15 14:01:50.179314] T [fuse-bridge.c:2773:fuse_open_resume] 0-glusterfs-fuse: 3929: OPEN /dir1/dir2/dir3/file
[2021-09-15 14:01:50.179323] T [MSGID: 0] [fuse-bridge.c:2776:fuse_open_resume] 0-stack-trace: stack-address: 0x7f05e800f0f8, winding from fuse to meta-autoload








[2021-09-15 14:01:50.180307] T [fuse-bridge.c:1483:fuse_fd_cbk] 0-glusterfs-fuse: 3889: OPEN() /dir1/dir2/dir3/file => 0x7f05e800b338
[2021-09-15 14:01:50.180324] T [fuse-bridge.c:290:send_fuse_iov] 0-glusterfs-fuse: writev() result 32/32




[2021-09-15 14:01:50.183607] T [fuse-bridge.c:2853:fuse_readv_resume] 0-glusterfs-fuse: 3930: READ (0x7f05e800b338, size=4096, offset=0)
[2021-09-15 14:01:50.183616] T [MSGID: 0] [fuse-bridge.c:2856:fuse_readv_resume] 0-stack-trace: stack-address: 0x7f05e800f0f8, winding from fuse to meta-autoload

