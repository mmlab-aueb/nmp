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

for i in ./producer* ; do 
	mode=`cat $i | grep -a Config `
	echo $mode" ";
	cat $i | grep T1 | awk 'BEGIN{d=0; b=0; f=0;}
				  {d+=$12; dsqr+=$12^2; f+=$16; fsqr+=$16^2;}
				  END{print "->DracoDelay: "d/NR" (" sqrt(dsqr/NR-(d/NR)^2) ") InterFrameDelay: "f/NR" (" sqrt(fsqr/NR-(f/NR)^2) ")"}';
	cat $i | grep T2 | awk 'BEGIN{d=0; b=0; f=0;}
				  {b+=$8/1024; bsqr+=($8/1024)^2; f+=$12; fsqr+=$12^2;}
				  END{print "->FRAME_Tx(KB): "b/NR" (" sqrt(bsqr/NR-(b/NR)^2) ") Transmission_latency: "f/NR" (" sqrt(fsqr/NR-(f/NR)^2) ")"}'; 
	echo ;
done
