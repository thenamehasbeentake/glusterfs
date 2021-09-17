把一个文件从一个卷移动到另一个卷  cmd : mv /mnt/vol/ubuntu_ad.sh /mnt/vol_bk/
客户端：
1. 两边同时做lookup操作
2. 打开源文件，读取源文件。创建源文件，写入数据
3. 读取源文件的元数据，设置元数据到目标文件上
4. unlink源文件 flush刷盘