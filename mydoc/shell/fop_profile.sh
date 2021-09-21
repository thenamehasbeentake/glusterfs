#!/bin/bash

# 参数1 ：卷名：
# 参数2：挂载点

if [ ! "$1" ];then
    echo "parameter is null"
    exit
fi

gluster v info $1 || exit


VOLFILE="/var/lib/glusterd/vols/$1/trusted-$1.tcp-fuse.vol"
MOUNTPOINT=$2
umount $MOUNTPOINT

g++ -std=c++11 "add_iostats.cpp"
./a.out $VOLFILE

mount -t glusterfs localhost:$1 $MOUNTPOINT

PID=`ps aux | grep "glusterfs " | grep fuse | grep $1 | awk '{print $2}'`

gluster v profile $1  start


gluster v profile $1 info clear
kill -SIGUSR1 $PID

# cmd
time ls $MOUNTPOINT/vdb.1_1.dir > /dev/null
# cmdend

gluster v profile $1 info

kill -SIGUSR1 $PID

sudo ./fix_trust_fuse.sh


