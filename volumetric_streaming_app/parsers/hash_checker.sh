#!/bin/bash
#
# Code in this file is licensed under the GNU General Public License v3.0
#
# Copyright (C) 2025 mmlab-AUEB
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program. If not, see <https://www.gnu.org/licenses/>.


##
# gets all producer files, finds consumer file for same experiment (with same timestamp), finds hashes in producer file and looks for them in the consumer file
# prints the number of found hashes in the consumer file.
##

RESULTSFILE="/tmp/results";
rm $RESULTSFILE;

for producerfile in ./producer*; do
	rm /tmp/foundhashes; 
	rm /tmp/hashes;
	
	consumerfile=`echo "${producerfile/producer/consumer}"`;
	echo $producerfile" -> "$consumerfile;
	
	cat $producerfile | grep T1 | awk '{print $14}' > /tmp/hashes ; 
	cat /tmp/hashes | while read p; do cat $consumerfile | grep $p>> /tmp/foundhashes;  done;  
	
	config=`awk -F: 'NR==2 {print $0}' $producerfile | sed -e "s/ /,/g" `;
	number=`cat /tmp/foundhashes | wc -l`
	echo $config" "$number >> $RESULTSFILE;
done

cat $RESULTSFILE;	
