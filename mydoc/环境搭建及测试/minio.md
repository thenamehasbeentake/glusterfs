## minio
### 下载及搭建
wget http://dl.minio.org.cn/server/minio/release/linux-amd64/minio
chmod +x minio


ec比例参考
https://docs.min.io/minio/baremetal/concepts/erasure-coding.html#minio-ec-parity

export MINIO_ROOT_USER=<ACCESS_KEY>
export MINIO_ROOT_PASSWORD=<SECRET_KEY>
export MINIO_STORAGE_CLASS_RRS=EC:1        #设置最小的冗余数,不支持4个及以下的drives
minio server http://host{1...n}/export{1...m}

## 如下命令启动minio实现4:2纠删卷
export MINIO_ROOT_USER=wxb
export MINIO_ROOT_PASSWORD=wenxiaobao
export MINIO_STORAGE_CLASS_STANDARD=EC:2        #(Starting with RELEASE.2021-01-30T00-20-58Z)
export MINIO_STORAGE_CLASS_RRS=EC:2
./minio server http://wxb{1...6}/mnt/sdb

## 扩容    (kill掉单个节点并执行以下命令，删除文件失败，且登录失败)
./minio server http://wxb{1...6}/mnt/sdb http://wxb{1...6}/mnt/sdc
扩容操作时出现以下错误
~~~
Unable to read 'format.json' from http://wxb1:9000/mnt/sdb: Authentication failed, check your access credentials

Unable to read 'format.json' from http://wxb2:9000/mnt/sdb: Authentication failed, check your access credentials

Unable to read 'format.json' from http://wxb3:9000/mnt/sdb: Authentication failed, check your access credentials

Unable to read 'format.json' from http://wxb5:9000/mnt/sdb: Authentication failed, check your access credentials

Unable to read 'format.json' from http://wxb6:9000/mnt/sdb: Authentication failed, check your access credentials


API: SYSTEM()
Time: 11:42:23 UTC 08/29/2021
Error: Read failed. Insufficient number of disks online (*errors.errorString)
       5: cmd/prepare-storage.go:268:cmd.connectLoadInitFormats()
       4: cmd/prepare-storage.go:317:cmd.waitForFormatErasure()
       3: cmd/erasure-server-pool.go:90:cmd.newErasureServerPools()
       2: cmd/server-main.go:609:cmd.newObjectLayer()
       1: cmd/server-main.go:523:cmd.serverMain()
Waiting for a minimum of 3 disks to come online (elapsed 49s)
~~~
### 再次尝试， 关闭minio服务再启动minio服务扩容，扩容成功
./minio server http://wxb{1...3}/mnt/sd{b...c}
./minio server http://wxb{1...3}/mnt/sd{b...c} http://wxb{4...6}/mnt/sd{b...c}

## 创建分布式纠删桶, 可以创建没有问题
export MINIO_ROOT_USER=wxb
export MINIO_ROOT_PASSWORD=wenxiaobao
export MINIO_STORAGE_CLASS_STANDARD=EC:2
export MINIO_STORAGE_CLASS_RRS=EC:2
./minio server http://wxb{1...6}/mnt/sdb http://wxb{1...6}/mnt/sdc

## 尝试创建2：1桶， 报错。REDUCED_REDUNDANCY设置最小的冗余数,不支持4个及以下的drives
export MINIO_ROOT_USER=wxb
export MINIO_ROOT_PASSWORD=wenxiaobao
export MINIO_STORAGE_CLASS_STANDARD=EC:2
export MINIO_STORAGE_CLASS_RRS=EC:1
./minio server http://wxb{1...3}/mnt/sdb

## 三个节点创建4：2的桶,可行
export MINIO_ROOT_USER=wxb
export MINIO_ROOT_PASSWORD=wenxiaobao
export MINIO_STORAGE_CLASS_STANDARD=EC:2
export MINIO_STORAGE_CLASS_RRS=EC:2
./minio server http://wxb{1...3}/mnt/sd{b...c}

## 测试单盘故障，操作为取消挂载 minio存储的目录(umount -l /mnt/sdb), minion进程直接挂掉了...
WARNING: Console endpoint is listening on a dynamic port (38681), please use --console-address ":PORT" to choose a static port.
Found unformatted drive http://wxb6:9000/mnt/sdb, attempting to heal...
Disk `/mnt/sdb` the same as the system root disk.
Disk will not be used. Please supply a separate disk and restart the server.
panic: runtime error: invalid memory address or nil pointer dereference
[signal SIGSEGV: segmentation violation code=0x1 addr=0x100 pc=0x28c6166]

goroutine 254 [running]:
github.com/minio/minio/cmd.(*erasureSets).HealFormat(0xc001c12300, 0x4577ab8, 0xc00fe03740, 0xc00fe17e00, 0x0, 0x33cd2c0, 0x8, 0x0, 0x0, 0x0, ...)
	github.com/minio/minio/cmd/erasure-sets.go:1295 +0xa86
github.com/minio/minio/cmd.(*erasureServerPools).HealFormat(0xc001a28340, 0x4577ab8, 0xc000256c80, 0x4aa500, 0x0, 0x0, 0x0, 0x0, 0x0, 0x0, ...)
	github.com/minio/minio/cmd/erasure-server-pool.go:1509 +0x332
github.com/minio/minio/cmd.healDiskFormat(0x4577ab8, 0xc000256c80, 0x45c1ab8, 0xc001a28340, 0x10000, 0x1, 0x0, 0x0, 0x0, 0x0, ...)
	github.com/minio/minio/cmd/background-heal-ops.go:139 +0xb6
github.com/minio/minio/cmd.(*healRoutine).run(0xc001c0f380, 0x4577ab8, 0xc000256c80, 0x45c1ab8, 0xc001a28340)
	github.com/minio/minio/cmd/background-heal-ops.go:110 +0x4ec
created by github.com/minio/minio/cmd.initBackgroundHealing
	github.com/minio/minio/cmd/background-newdisks-heal-ops.go:314 +0x105


## umount掉一个driver所挂载的目录，模拟该磁盘损坏，执行./minio server会失败。格式化该磁盘并重新挂载，在执行./minio server。 该节点重新启动并开始修复磁盘
~~~
root@wxb_1:~# df
Filesystem                        1K-blocks    Used Available Use% Mounted on
udev                                 976056       0    976056   0% /dev
tmpfs                                201748     924    200824   1% /run
/dev/mapper/ubuntu--vg-ubuntu--lv   7155192 3548056   3223956  53% /
tmpfs                               1008724       0   1008724   0% /dev/shm
tmpfs                                  5120       0      5120   0% /run/lock
tmpfs                               1008724       0   1008724   0% /sys/fs/cgroup
/dev/sda2                            999320   81268    849240   9% /boot
tmpfs                                201744       0    201744   0% /run/user/1000
/dev/sdc                            8378368   66944   8311424   1% /mnt/sdc
/dev/sdb                            8378368   41500   8336868   1% /mnt/sdb
root@wxb_1:~# df -h
Filesystem                         Size  Used Avail Use% Mounted on
udev                               954M     0  954M   0% /dev
tmpfs                              198M  924K  197M   1% /run
/dev/mapper/ubuntu--vg-ubuntu--lv  6.9G  3.4G  3.1G  53% /
tmpfs                              986M     0  986M   0% /dev/shm
tmpfs                              5.0M     0  5.0M   0% /run/lock
tmpfs                              986M     0  986M   0% /sys/fs/cgroup
/dev/sda2                          976M   80M  830M   9% /boot
tmpfs                              198M     0  198M   0% /run/user/1000
/dev/sdc                           8.0G   66M  8.0G   1% /mnt/sdc
/dev/sdb                           8.0G   66M  8.0G   1% /mnt/sdb
root@wxb_1:~# df
Filesystem                        1K-blocks    Used Available Use% Mounted on
udev                                 976056       0    976056   0% /dev
tmpfs                                201748     924    200824   1% /run
/dev/mapper/ubuntu--vg-ubuntu--lv   7155192 3548056   3223956  53% /
tmpfs                               1008724       0   1008724   0% /dev/shm
tmpfs                                  5120       0      5120   0% /run/lock
tmpfs                               1008724       0   1008724   0% /sys/fs/cgroup
/dev/sda2                            999320   81268    849240   9% /boot
tmpfs                                201744       0    201744   0% /run/user/1000
/dev/sdc                            8378368   66944   8311424   1% /mnt/sdc
/dev/sdb                            8378368   66704   8311664   1% /mnt/sdb
~~~





umount /mnt/sdb && mkfs.xfs -f /dev/sdb && mount /dev/sdb /mnt/sdb
umount /mnt/sdc && mkfs.xfs -f /dev/sdc && mount /dev/sdc /mnt/sdc