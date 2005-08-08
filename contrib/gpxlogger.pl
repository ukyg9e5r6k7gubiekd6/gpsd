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
my ($i, $j, $k, $lt, $tk);

# do all that getopt stuff. option validation moved into a subroutine
getopts ("hi:p:s:vw:", \%opt);
usage() if (defined($opt{'h'}));
check_options();

open($out, ">" . $opt{'w'}) or die "Can't open $opt{'w'}: $!\n";
select((select($out), $| = 1)[0]);
write_header();

$SIG{'TERM'} = $SIG{'QUIT'} = $SIG{'HUP'} = $SIG{'INT'} = \&cleanup;

while (1){
	#connect
	print "Connecting to $opt{'s'}:$opt{'p'}" if ($opt{'v'});
loop:	$sock = IO::Socket::INET->new(PeerAddr => $opt{'s'},
				PeerPort => $opt{'p'},
				Proto    => "tcp",
				Type     => SOCK_STREAM);

	unless ($sock){
		print "\nCouldn't connect to $opt{'s'}:$opt{'p'}\n$@\n";
		print "retrying in 10 seconds.\n";
		sleep 10;
		goto loop;
	}

	print " Connected!\n" if ($opt{'v'});

	print $sock "SPAMQO\n";
	while (defined( $line = <$sock> )){
		$line =~ s/\s*$//gism;
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
			sleep($opt{'i'});
		} else {
			if ($opt{'v'}){
				spinner($k++);
			} else {
				sleep($opt{'i'});
			}
		}
		print $sock "SPAMQO\n";
	}
sleep(1);
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
	$tk = 0;

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
	print $out "<!-- track start -->\n";
	print $out " <trk>\n";
	print $out "  <trkseg>\n";
 	$tk = 1;
}

sub track_end{
	return unless ($tk);
	print $out "  </trkseg>\n";
	print $out " </trk>\n";
	print $out "<!-- track end -->\n\n";
 	$tk = 0;
}

sub parse_line{
	%GPS = ();

	# extract fix quality and status
	$GPS{'fix'}  = 'none';
	if ($line =~ /M=(\d),/){
		$GPS{'fix'}  = "2d" if ($1 == 2);
		$GPS{'fix'}  = "3d" if ($1 == 3);
		$GPS{'fix'}  = "pps" if ($1 == 4);
	}

	if ($line =~ /S=2,/){
		$GPS{'fix'} = 'dgps';
	}

	if ($line =~ /P=([0-9\.-]+) ([0-9\.-]+)/){
		$GPS{'lat'} = $1; $GPS{'lon'} = $2;
	}

	$GPS{'alt'}  = $1 if $line =~ /A=([0-9\.-]+)/; 

	if ($line =~ /Q=(\d+) (\S+) (\S+) (\S+)/){
		$GPS{'sat'}  = $1; $GPS{'gdop'} = $2;
		$GPS{'hdop'} = $3; $GPS{'vdop'} = $4;
		delete($GPS{'hdop'}) if ($GPS{'hdop'} =~ /[^0-9\.]/);
		delete($GPS{'vdop'}) if ($GPS{'vdop'} =~ /[^0-9\.]/);
	}

	if ($line =~ /(O=[^\?].+)/){
		my @w = split(/\s+/, $1);
		$GPS{'time'} = int ($w[1]);
		$GPS{'lat'} = $w[3] unless (defined($GPS{'lat'}));
		$GPS{'lon'} = $w[4] unless (defined($GPS{'lon'}));
		$GPS{'alt'} = $w[5] unless (defined($GPS{'alt'}));
	}

	delete($GPS{'alt'}) if (defined($GPS{'alt'}) && (($GPS{'fix'} eq '2d') || ($GPS{'alt'} =~ /[^0-9\.]/)));

	if (defined($GPS{'lat'}) && defined($GPS{'lon'}) && ($GPS{'fix'} ne 'none')){
 		track_start() unless ($tk);
		return(1);
	} else {
 		track_end() if ($tk);
		return(0);
	}
}

sub write_gpx{
 	printf $out ("   <trkpt lat=\"%f\" ", $GPS{'lat'});
 	printf $out ("lon=\"%f\">\n", $GPS{'lon'});

	# if we know our fix quality, then print it.
 	printf $out ("    <fix>%s</fix>\n", $GPS{'fix'});

	# if we have a 3d or dgps solution, then print altitude
 	printf $out ("    <ele>%.2f</ele>\n", $GPS{'alt'}) if (defined($GPS{'alt'}));

	# GPX allows us to give some other indicators of fix quality
 	printf $out ("    <hdop>%s</hdop>\n", $GPS{'hdop'}) if (defined($GPS{'hdop'}));
 	printf $out ("    <vdop>%s</vdop>\n", $GPS{'vdop'}) if (defined($GPS{'vdop'}));
	printf $out ("    <sat>%s</sat>\n",   $GPS{'sat'} ) if (defined($GPS{'sat'} ));

	# and finally, note what time this fix was made
	my @t = gmtime($GPS{'time'});
	$t[5]+=1900; $t[4]++;
 	printf $out ("    <time>%04d-%02d-%02dT", $t[5], $t[4], $t[3]);
 	printf $out ("%02d:%02d:%02dZ</time>\n", $t[2], $t[1], $t[0]);
	print  $out "   </trkpt>\n";
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

sub spinner {
	print substr('-\|/', ($_[0]) % 4, 1);
	sleep($opt{'i'});
	print '';
}
