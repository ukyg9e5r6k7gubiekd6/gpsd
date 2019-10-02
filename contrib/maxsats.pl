#!/usr/bin/perl

# Copyright (c) 2008 Chris Kuethe <chris.kuethe@gmail.com>
#
# This file is Copyright (c) 2008-2019 by the GPSD project
# SPDX-License-Identifier: BSD-2-clause

use strict;
use warnings;

my ($tm, $nsr, $nt, $nu, @TL, @UL, $l);
while (<>){
	next unless (/,Y=\w+ (\d+\.\d+) (\d+):(.+:)/);
	$tm = $1;
	$nsr = $2;
	$l = ":$3:";
	$nt = $nu = 0;
	@TL = @UL = ();
	while ($l =~ /(\d+) \w+ \w+ (\d+) ([01]):/g){
		if ($1 <= 32){ # $1 => prn
			if ($2){ # $2 => snr
				push(@TL, $1);
				$nt++;
			}
			if ($3){ # $3 => used
				push(@UL, $1);
				$nu++;
			}
		}
	}
	print "$tm $nsr nu/nt = $nu/$nt T=\[@TL\] U=\[@UL\]\n" if (($nu >= 10));
}
