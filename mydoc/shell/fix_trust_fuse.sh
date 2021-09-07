#!/bin/bash



prevfile="/var/log/glusterfs/io-stats-profile"
xlatorname="io-threads"
prfilename="$prevfile"*"$xlatorname"
ls $prfilename
time=$(date "+%Y%m%d-%H%M%S")
time=$(date "+%Y%m%d")
resultfilename="/var/log/glusterfs/io-stats-result.$time"
xlatorpath="/var/log/glusterfs/xlator.list"

echo "fuse" > $resultfilename;

# 循环读文件

cat $xlatorpath | while read line
do

	prfilename="$prevfile"*"$line"
	beginline=`grep -n "Fop" $prfilename | cut -d ":" -f 1`
	endline=`grep -n "\-\-\-\-\-\- \-" $prfilename | cut -d ":" -f 1`
	xlatorprofile=`sed -n "${beginline},${endline}p" $prfilename`
	echo "$xlatorprofile" >> $resultfilename;
    echo "$line" >> $resultfilename

    echo >> $resultfilename
done





# xlatorprofile=`cat $prfilename | tail -n +$beginline | head -n $[ endline-beginline ]`



