mkdir tool
cd tool/
wget https://www.iozone.org/src/current/iozone3_492.tgz
tar -xf iozone3_492.tgz
cd iozone3_492/
cd src/current/
sudo apt install build-essential -y
make  linux-AMD64
sudo ln -s /home/deeproute/tool/iozone3_492/src/current/iozone /usr/local/bin/iozone
cd
iozone -h





sudo apt install sysstat
sudo apt install iotop
sudo apt install iftop
sudo apt install dstat







必做项
/etc/fstab 加入磁盘开机自启动
ntp时间同步
3*3副本卷，4+2 纠删卷性能对比
linux参数调优
修复性能测试


可做
12块盘同时读写性能
xfs文件系统
以下方案的验证



方案：
3*3副本           10个卷


(4+2)纠删卷     
5* (4+2)  纠删卷  4个卷

底层raid  分布式卷











Brick1: 10.3.2.11:/dn1/test-mutiafr$i-vol-brick
Brick2: 10.3.2.12:/dn1/test-mutiafr$i-vol-brick
Brick3: 10.3.2.13:/dn1/test-mutiafr$i-vol-brick
Brick4: 10.3.2.14:/dn1/test-mutiafr$i-vol-brick
Brick5: 10.3.2.15:/dn1/test-mutiafr$i-vol-brick
Brick6: 10.3.2.16:/dn1/test-mutiafr$i-vol-brick
Brick7: 10.3.2.17:/dn1/test-mutiafr$i-vol-brick
Brick8: 10.3.2.18:/dn1/test-mutiafr$i-vol-brick
Brick9: 10.3.2.11:/dn2/test-mutiafr$i-vol-brick
test-mutiafr$i-vol




gluster v create test-mutiafr1-vol replica 3 10.3.2.1{1..8}:/dn1/test-mutiafr1-vol-brick 10.3.2.11:/dn2/test-mutiafr1-vol-brick
gluster v start test-mutiafr1-vol
gluster v create test-mutiafr2-vol replica 3 10.3.2.1{2..8}:/dn2/test-mutiafr2-vol-brick 10.3.2.1{1..2}:/dn3/test-mutiafr2-vol-brick
gluster v start test-mutiafr2-vol
gluster v create test-mutiafr3-vol replica 3 10.3.2.1{3..8}:/dn3/test-mutiafr2-vol-brick 10.3.2.1{1..3}:/dn4/test-mutiafr2-vol-brick
gluster v start test-mutiafr3-vol
gluster v create test-mutiafr4-vol replica 3 10.3.2.1{4..8}:/dn4/test-mutiafr2-vol-brick 10.3.2.1{1..4}:/dn5/test-mutiafr2-vol-brick
gluster v start test-mutiafr4-vol
gluster v create test-mutiafr5-vol replica 3 10.3.2.1{5..8}:/dn5/test-mutiafr2-vol-brick 10.3.2.1{1..5}:/dn6/test-mutiafr2-vol-brick
gluster v start test-mutiafr5-vol
gluster v create test-mutiafr6-vol replica 3 10.3.2.1{6..8}:/dn5/test-mutiafr2-vol-brick 10.3.2.1{1..5}:/dn6/test-mutiafr2-vol-brick
gluster v start test-mutiafr5-vol


for i in {1..10}; do j=$i; ipnum=$i; if [ $i -gt 8 ];then ipnum=$((i-8));j=$((i+1)); fi; echo "sudo gluster v create test-mutiafr$i-vol replica 3 10.3.2.1{$ipnum..8}:/dn$j/test-mutiafr$i-vol-brick 10.3.2.1{1..$ipnum}:/dn$((j+1))/test-mutiafr$i-vol-brick; sudo gluster v start test-mutiafr$i-vol"; done

for i in {1..10}; do sudo gluster v set test-mutiafr$i-vol  features.shard on; done


for i in {1..10}; do sudo mkdir /mnt/test-mutiafr$i-vol; sudo mount.glusterfs 10.3.2.11:test-mutiafr$i-vol /mnt/test-mutiafr$i-vol; done


for i in {1..10}; do sudo gluster v set test-mutiafr$i-vol  features.shard off; done


sudo gluster volume set test-mutiafr$i-vol group metadata-cache
sudo gluster volume set test-mutiafr$i-vol network.inode-lru-limit 50000
sudo gluster volume set test-mutiafr$i-vol cluster.lookup-optimize on
sudo gluster volume set test-mutiafr$i-vol performance.readdir-ahead on
sudo gluster volume set test-mutiafr$i-vol performance.rda-cache-limit 60mb
sudo gluster volume set test-mutiafr$i-vol group nl-cache
sudo gluster volume set test-mutiafr$i-vol nl-cache-positive-entry on
sudo gluster volume set test-mutiafr$i-vol performance.cache-invalidation on
sudo gluster volume set test-mutiafr$i-vol features.cache-invalidation on
sudo gluster volume set test-mutiafr$i-vol performance.qr-cache-timeout 600
sudo gluster volume set test-mutiafr$i-vol cache-invalidation-timeout 600
sudo gluster volume set test-mutiafr$i-vol performance.parallel-readdir on
sudo gluster volume set test-mutiafr$i-vol client.event-threads 32
sudo gluster volume set test-mutiafr$i-vol server.event-threads 32
sudo gluster volume set test-mutiafr$i-vol performance.io-cache on
sudo gluster volume set test-mutiafr$i-vol performance.cache-size 16GB
sudo gluster volume set test-mutiafr$i-vol performance.cache-max-file-size 256MB
sudo gluster volume set test-mutiafr$i-vol performance.cache-min-file-size 1MB
sudo gluster volume set test-mutiafr$i-vol lookup-unhashed off
sudo gluster volume set test-mutiafr$i-vol write-behind on
sudo gluster volume set test-mutiafr$i-vol aggregate-size 8mb
sudo gluster volume set test-mutiafr$i-vol flush-behind on
# sudo gluster volume set test-mutiafr$i-vol performance.client-io-threads on
sudo gluster volume set test-mutiafr$i-vol performance.io-thread-count 16
sudo gluster volume set test-mutiafr$i-vol server.outstanding-rpc-limit 2048
sudo gluster volume set test-mutiafr$i-vol performance.write-behind-window-size 64M
sudo gluster volume set test-mutiafr$i-vol cluster.read-hash-mode 1
sudo gluster volume set test-mutiafr$i-vol storage.health-check-timeout 0
sudo gluster volume set test-mutiafr$i-vol cluster.shd-max-threads 64
sudo gluster volume set test-mutiafr$i-vol cluster.self-heal-window-size 1024
sudo gluster volume set test-mutiafr$i-vol performance.enable-least-priority no







for i in {1..10}; do \
sudo gluster volume set test-mutiafr$i-vol group metadata-cache; \
sudo gluster volume set test-mutiafr$i-vol network.inode-lru-limit 50000; \
sudo gluster volume set test-mutiafr$i-vol cluster.lookup-optimize on; \
sudo gluster volume set test-mutiafr$i-vol performance.readdir-ahead on; \
sudo gluster volume set test-mutiafr$i-vol performance.rda-cache-limit 60mb; \
sudo gluster volume set test-mutiafr$i-vol group nl-cache; \
sudo gluster volume set test-mutiafr$i-vol nl-cache-positive-entry on; \
sudo gluster volume set test-mutiafr$i-vol performance.cache-invalidation on; \
sudo gluster volume set test-mutiafr$i-vol features.cache-invalidation on; \
sudo gluster volume set test-mutiafr$i-vol performance.qr-cache-timeout 600; \
sudo gluster volume set test-mutiafr$i-vol cache-invalidation-timeout 600; \
sudo gluster volume set test-mutiafr$i-vol performance.parallel-readdir on; \
sudo gluster volume set test-mutiafr$i-vol client.event-threads 32; \
sudo gluster volume set test-mutiafr$i-vol server.event-threads 32; \
sudo gluster volume set test-mutiafr$i-vol performance.io-cache on; \
sudo gluster volume set test-mutiafr$i-vol performance.cache-size 16GB; \
sudo gluster volume set test-mutiafr$i-vol performance.cache-max-file-size 256MB; \
sudo gluster volume set test-mutiafr$i-vol performance.cache-min-file-size 1MB; \
sudo gluster volume set test-mutiafr$i-vol lookup-unhashed off; \
sudo gluster volume set test-mutiafr$i-vol write-behind on; \
sudo gluster volume set test-mutiafr$i-vol aggregate-size 8mb; \
sudo gluster volume set test-mutiafr$i-vol flush-behind on; \
sudo gluster volume set test-mutiafr$i-vol performance.io-thread-count 16; \
sudo gluster volume set test-mutiafr$i-vol server.outstanding-rpc-limit 2048; \
sudo gluster volume set test-mutiafr$i-vol performance.write-behind-window-size 64M; \
sudo gluster volume set test-mutiafr$i-vol cluster.read-hash-mode 1; \
sudo gluster volume set test-mutiafr$i-vol storage.health-check-timeout 0; \
sudo gluster volume set test-mutiafr$i-vol cluster.shd-max-threads 64; \
sudo gluster volume set test-mutiafr$i-vol cluster.self-heal-window-size 1024; \
sudo gluster volume set test-mutiafr$i-vol performance.enable-least-priority no; done






 for i in {1..10}; do sudo umount /mnt/test-mutiafr$i-vol; done

 
 for i in {1..10}; do sudo gluster v stop test-mutiafr$i-vol; done


 for i in {1..10}; do sudo echo y | sudo gluster v stop test-mutiafr$i-vol; done



 for i in {1..10}; do j=$i; ipnum=$i; if [ $i -gt 8 ];then ipnum=$((i-8));j=$((i+1)); fi; sudo gluster v create test-mutiafr$i-vol replica 3 10.3.2.1{$ipnum..8}:/dn$j/test-mutiafr$i-vol-brick 10.3.2.1{1..$ipnum}:/dn$((j+1))/test-mutiafr$i-vol-brick; sudo gluster v start test-mutiafr$i-vol; done

 gluster v create test-ec-vol disperse 6 redundancy 2 10.3.2.1{1..6}:/dn1/test-ec-vol-brick
 
gluster v create test-ec-vol disperse 6 redundancy 2 10.3.2.1{1..6}:/dn1/test-ec-vol-brick && gluster v start test-ec-vol 



sudo mkdir /mnt/test-ec-vol-brick && sudo mount.glusterfs 10.3.2.11:test-ec-vol /mnt/test-ec-vol-brick









    sudo mkdir /mnt/test-ec-vol && sudo mount.glusterfs 10.3.2.11:test-ec-vol /mnt/test-ec-vol

    gluster v create test-muti-ec-vol disperse 6 redundancy 2 10.3.2.1{1..8}:/dn1/test-muti-ec-vol-brick  10.3.2.1{1..8}:/dn2/test-muti-ec-vol-brick 10.3.2.1{1..8}:/dn3/test-muti-ec-vol-brick  10.3.2.1{1..8}:/dn4/test-muti-ec-vol-brick 10.3.2.1{1..8}:/dn5/test-muti-ec-vol-brick 10.3.2.1{1..8}:/dn6/test-muti-ec-vol-brick  10.3.2.1{1..8}:/dn7/test-muti-ec-vol-brick  10.3.2.1{1..8}:/dn8/test-muti-ec-vol-brick 10.3.2.1{1..8}:/dn9/test-muti-ec-vol-brick  10.3.2.1{1..8}:/dn10/test-muti-ec-vol-brick 10.3.2.1{1..8}:/dn11/test-muti-ec-vol-brick 10.3.2.1{1..8}:/dn12/test-muti-ec-vol-brick && sudo gluster v start test-muti-ec-vol

    sudo mkdir /mnt/test-muti-ec-vol && sudo mount.glusterfs 10.3.2.11:test-muti-ec-vol /mnt/test-muti-ec-vol