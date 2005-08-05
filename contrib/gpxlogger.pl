#!/usr/bin/perl -T

# Copyright (c) 2005 Chris Kuethe <chris.kuethe@gmail.com>
#
# Permission to use, copy, modify, and distribute this software for any
# purpose with or without fee is hereby granted, provided that the above
# copyright notice and this permission notice appear in all copies.
#
# THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
# WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
# MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
# ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
# WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
# ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
# OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.

# This program was heavily inspired by Amaury Jacquot's "gpxlogger.c"

$| = 1;
use strict;
use warnings;
use Time::HiRes qw(sleep);
use Getopt::Std;
use IO::Socket;

my $author = 'Chris Kuethe (chris.kuethe@gmail.com)';
my $copyright = 'ISC (BSD) License';

# state variables
my ($sock, $out, %opt, $line, %GPS);
# accumulators, buffers, registers, ...
my ($i, $j, $lt, $tk, $tm);

# do all that getopt stuff. option validation moved into a subroutine
getopts ("hi:p:s:vw:", \%opt);
usage() if (defined($opt{'h'}));
check_options();

#connect
print "Connecting to $opt{'s'}:$opt{'p'}" if ($opt{'v'});
$sock = IO::Socket::INET->new(PeerAddr => $opt{'s'},
			PeerPort => $opt{'p'},
			Proto    => "tcp",
			Type     => SOCK_STREAM)
	or die "\nCouldn't connect to $opt{'s'}:$opt{'p'} - $@\n";
print " OK!\n" if ($opt{'v'});

open($out, ">" . $opt{'w'}) or die "Can't open $opt{'w'}: $!\n";
select((select($out), $| = 1)[0]);
write_header();

$SIG{'TERM'} = $SIG{'QUIT'} = $SIG{'HUP'} = $SIG{'INT'} = \&cleanup;

print $sock "MSQO\n";
while (defined( $line = <$sock> )){
	chomp $line;
	if( parse_line()){
		write_gpx();

		# foofy eyecandy. print a dot for each line in verbose mode
		if ($opt{'v'}){
			$i++; $j++;
			print '.';
			if ($i == 50){
				printf (" %8d\n", $j);
				$i = 0;
			} else {
				print ' ' if ($i % 5 == 0);
			}
		}
	}
	sleep($opt{'i'});
	print $sock "MSQO\n";
}

cleanup("PIPE");

###########################################################################
sub usage{
	print <<HELP;
Usage: $0 [-h] [-i I] [-s S] [-p P] [-w W] [-v]
	-h	display this help screen and exit
	-i I	poll every "I" seconds - defaults to "5"
	-s S	connect to server "S" - defaults to "127.0.0.1"
	-p P	connect to port "P" - defaults to "2947"
	-w W	write to file "W" - defaults to stdout
	-v	enable verbose mode
HELP
	exit(1);
}
sub cleanup{
	my $s = $_[0];
	$s = '?' unless (defined($s));
	$SIG{'TERM'} = $SIG{'QUIT'} = $SIG{'HUP'} = $SIG{'INT'} = 'IGNORE';
	printf STDERR ("exiting, signal %s received\n", $s);
	eval { close($sock); };
	write_footer ();
	exit (0);
}

sub write_header{
	return unless (defined($out));
	$tk = $lt = 0;

	print $out <<EOF;
<?xml version="1.0" encoding="utf-8"?>
<gpx version="1.1" creator="Perl GPX GPSD client"
        xmlns:xsi="http://www.w3.org/2001/XMLSchema-instance"
        xmlns="http://www.topografix.com/GPX/1.1"
        xsi:schemaLocation="http://www.topografix.com/GPS/1/1
        http://www.topografix.com/GPX/1/1/gpx.xsd">
 <metadata>
  <name>Perl GPX GPSD client</name>
  <author>$author</author>
  <copyright>$copyright</copyright>
 </metadata>

EOF
}

sub write_footer{
	return unless (defined($out));
	track_end() if ($tk);
	print $out "</gpx>\n";
	close $out;
	$out = undef;
}

sub track_start{
	return if ($tk);
	print $out " <trk>\n";
	print $out "  <trkseg>\n";
 	$tk = 1;
}

sub track_end{
	return unless ($tk);
	print $out "  </trkseg>\n";
	print $out " </trk>\n\n";
 	$tk = 0;
}

sub parse_line{
	%GPS = ();
	my ($junk, $m, $s, $q, $o) = split(/,/, $line);

	# extract fix quality and status
	if ($m =~/M=(\d)/){
		$m = $1; $m = 0 if (($m < 0) || ($m > 3));
		return (0) if ($m == 1);
		$GPS{'mode'} = ('none', 'none', '2d', '3d')[$m];
		$GPS{'fq'} = $m;
	} else {
		$GPS{'mode'} = 'none';
	}

	if ($s =~/S=(\d)/){
		if ($1 == 2){
			$GPS{'mode'} = 'dgps';
			$GPS{'fq'} = 4;
		}
	}

	if ($q =~/Q=(\d+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)/){
		$GPS{'sat'}  = $1; $GPS{'sdop'} = $2;
		$GPS{'hdop'} = $3; $GPS{'vdop'} = $4;
		$GPS{'tdop'} = $5; $GPS{'gdop'} = $6;
		delete($GPS{'sdop'}) if ($GPS{'sdop'} =~ /[^0-9\.]/);
		delete($GPS{'hdop'}) if ($GPS{'hdop'} =~ /[^0-9\.]/);
		delete($GPS{'vdop'}) if ($GPS{'vdop'} =~ /[^0-9\.]/);
		delete($GPS{'tdop'}) if ($GPS{'tdop'} =~ /[^0-9\.]/);
		delete($GPS{'gdop'}) if ($GPS{'gdop'} =~ /[^0-9\.]/);
	}


	return 0 if ($o =~ /O=\?/i);
	my @w = split(/\s+/, $o); shift @w;
	$GPS{'time'} = int ($w[0]);
	$GPS{'lat'} = $w[2];
	$GPS{'lon'} = $w[3];
	$GPS{'alt'} = $w[4]; delete($GPS{'alt'}) if ($GPS{'alt'} =~ /\D/);
	return(1);
}

sub write_gpx{
	my @t;
	return unless (defined($out));

 	if (($GPS{'time'} != $lt ) && ($GPS{'mode'} ne 'none')) {
 		track_end() if (abs($GPS{'time'} - $lt) > $tm);
 		track_start() unless ($tk);

 		$lt = $GPS{'time'};
 		printf $out ("   <!-- %s -->\n", $line);
 		printf $out ("   <trkpt lat=\"%f\" ", $GPS{'lat'});
 		printf $out ("lon=\"%f\">\n", $GPS{'lon'});

		# if we know our fix quality, then print it.
 		printf $out ("    <fix>%s</fix>\n", $GPS{'mode'}) if ($GPS{'fq'});

		# if we have a 3d or dgps solution, then print altitude
 		printf $out ("    <ele>%.2f</ele>\n", $GPS{'alt'}) if (($GPS{'fq'} > 2) && defined($GPS{'alt'}));

		# GPX allows us to give some other indicators of fix quality
 		printf $out ("    <hdop>%s</hdop>\n", $GPS{'hdop'}) if (defined($GPS{'hdop'}));
 		printf $out ("    <vdop>%s</vdop>\n", $GPS{'vdop'}) if (defined($GPS{'vdop'}));
 		printf $out ("    <sat>%s</sat>\n",   $GPS{'sat'} ) if (defined($GPS{'sat'} ));

		# and finally, note what time this fix was made
		@t = gmtime($GPS{'time'});
		$t[5]+=1900; $t[4]++;
 		printf $out ("    <time>%04d-%02d-%02dT", $t[5], $t[4], $t[3]);
 		printf $out ("%02d:%02d:%02dZ</time>\n", $t[2], $t[1], $t[0]);
 		print  $out "   </trkpt>\n";

# print '=' x 75 . "\n"; foreach (sort keys %GPS){ printf("%s -> %s\n", $_, $GPS{$_}); }

	}
}

sub check_options{
# sanitize the server
	$opt{'s'} = '127.0.0.1' unless (defined($opt{'s'}));
	$opt{'s'} =~ /([0-9a-zA-Z\.-]+)/; $opt{'s'} = $1;
	$opt{'s'} = '127.0.0.1' unless ($opt{'s'});

# sanitize the port
	$opt{'p'} = 2947 unless (defined($opt{'p'}));
	$opt{'p'} =~ /([0-9]+)/; $opt{'p'} = $1;
	$opt{'p'} = 2947 unless ($opt{'p'});

#sanitize polling interval in seconds
	$opt{'i'} = 5 unless (defined($opt{'i'}));
	$opt{'i'} =~ /([0-9]+)/; $opt{'i'} = $1;
	$opt{'i'} = 5 unless ($opt{'i'} > 0);

# maximum clock jump
	$tm = 2 * $opt{'i'};
	$tm = 5 if ($tm < 5);

#verbosity
	if (defined($opt{'v'})){
		$opt{'v'} = 1;
	} else {
		$opt{'v'} = 0;
	}

#write to file - try to prevent shell metacharacters and other bad stuff
	$opt{'w'} = '/dev/stdout' unless (defined($opt{'w'}));
	$opt{'w'} =~ /([\w\s_,\+\-\.\/]+)/; $opt{'w'} = $1;
	$opt{'w'} = '/dev/stdout' unless (length($opt{'w'}));
}
