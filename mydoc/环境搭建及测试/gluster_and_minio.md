## gluster +　minio

sudo apt-get install make automake autoconf libtool flex bison  \
    pkg-config libssl-dev libxml2-dev python-dev libaio-dev       \
    libibverbs-dev librdmacm-dev libreadline-dev liblvm2-dev      \
    libglib2.0-dev liburcu-dev libcmocka-dev libsqlite3-dev       \
    libacl1-dev uuid-dev attr

cd glusterfs && ./autogen.sh && ./configure  && make && make install

ldconfig

/etc/init.d/glusterd start


gluster v create ec-vol disperse 3 redundancy 1 wxb{1..3}:/mnt/sdb/ec-brick
mount -t glusterfs wxb1:ec-vol /mnt/ecvol/



./minio server /mnt/ecvol
minio使用glusterfs挂载目录作为driver，使用本地服务，需要修改ec卷的参数, 具体需要调整的参数的是哪个，需要后续慢慢尝试
gusterfs 7.5版本，以下参数可以启动
~~~
root@wxb_1:/home/wxb/glusterfs# gluster v get ec-vol all | grep disperse
disperse.eager-lock                     off                                     
disperse.other-eager-lock               off                                     
disperse.eager-lock-timeout             60                                      
disperse.other-eager-lock-timeout       60                                      
cluster.disperse-self-heal-daemon       disable                                 
disperse.background-heals               8                                       
disperse.heal-wait-qlength              128                                     
disperse.read-policy                    gfid-hash                               
disperse.shd-max-threads                1                                       
disperse.shd-wait-qlength               1024                                    
disperse.cpu-extensions                 auto                                    
disperse.self-heal-window-size          1                                       
disperse.optimistic-change-log          on                                      
disperse.stripe-cache                   0                                       
disperse.parallel-writes                off 
~~~
minio web端上传之后，刷新文件显示文件不全，可能还是有些问题...


