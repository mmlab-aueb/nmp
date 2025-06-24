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

RESULTSFILE="/tmp/endtoendlatency";
rm $RESULTSFILE;

for producerfile in ./producer*; do
	
	consumerfile=`echo "${producerfile/producer/consumer}"`;
	echo $producerfile" -> "$consumerfile;
	
	for frameid in {0..100};do
		start=`cat $producerfile | grep T1 | grep "Frame $frameid " | awk '{print $1-int($16)}' `
		end=`cat $consumerfile | grep T1 | grep "Frame $frameid " | awk '{print $1}' `
		if [ $start != "" ] && [ $end != "" ]; then
			diff=$(($end - $start))
			echo "Difference for $frameid is $diff" >> $RESULTSFILE;
		fi
	done;
	
	cat $RESULTSFILE | awk '{avg+=$5; sqr+=$5^2;}END{print "Average latency: "avg/NR" stdev: "sqrt(sqr/NR-(avg/NR)^2)}';
	echo "" > $RESULTSFILE
done

