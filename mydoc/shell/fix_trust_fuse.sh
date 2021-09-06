#!/bin/bash

filename=$1
newtext=""
iostats=""

sudo cp $filename ${filename}_back 

cat $filename | while read line
do
	echo $line
done
