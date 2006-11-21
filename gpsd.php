<?php

#$CSK: gpsd.php,v 1.36 2006/10/31 00:04:26 ckuethe Exp $

# Copyright (c) 2006 Chris Kuethe <chris.kuethe@gmail.com>
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

global $GPS, $server, $port, $head, $blurb, $title, $autorefresh, $footer;
global $magic;
$magic = 1; # leave this set to 1

if (!file_exists("gpsd_config.inc"))
	write_config();

require_once("gpsd_config.inc");

if (isset($_GET['host']))
	if (!preg_match('/[^a-zA-Z0-9\.-]/', $_GET['host']))
		$server = $_GET['host'];

if (isset($_GET['port']))
	if (!preg_match('/\D/', $_GET['port']) && ($port>0) && ($port<65536))
		$port = $_GET['port'];

if ($magic)
	$sock = @fsockopen($server, $port, $errno, $errstr, 2);
else
	$sock = 0;

if (isset($_GET['op']) && ($_GET['op'] == 'view')){
	gen_image($sock);
} else {
	parse_pvt($sock);
	write_html();
}
if ($magic)
	@fclose($sock);

exit(0);

###########################################################################
function colorsetup($im){
	$C['white']	= imageColorAllocate($im, 255, 255, 255);
	$C['ltgray']	= imageColorAllocate($im, 191, 191, 191);
	$C['mdgray']	= imageColorAllocate($im, 127, 127, 127);
	$C['dkgray']	= imageColorAllocate($im, 63, 63, 63);
	$C['black']	= imageColorAllocate($im, 0, 0, 0);
	$C['red']	= imageColorAllocate($im, 255, 0, 0);
	$C['brightgreen'] = imageColorAllocate($im, 0, 255, 0);
	$C['darkgreen']	= imageColorAllocate($im, 0, 192, 0);
	$C['blue']	= imageColorAllocate($im, 0, 0, 255);
	$C['cyan']	= imageColorAllocate($im, 0, 255, 255);
	$C['magenta']	= imageColorAllocate($im, 255, 0, 255);
	$C['yellow']	= imageColorAllocate($im, 255, 255, 0);
	$C['orange']	= imageColorAllocate($im, 255, 128, 0);

	return $C;
}

function radial($angle, $sz){
	#turn into radians
	$angle = deg2rad($angle);

	# determine length of radius
	$r = $sz * 0.5 * 0.95;

	# and convert length/azimuth to cartesian
	$x0 = sprintf("%d", (($sz * 0.5) - ($r * cos($angle))));
	$y0 = sprintf("%d", (($sz * 0.5) - ($r * sin($angle))));
	$x1 = sprintf("%d", (($sz * 0.5) + ($r * cos($angle))));
	$y1 = sprintf("%d", (($sz * 0.5) + ($r * sin($angle))));

	return array($x0, $y0, $x1, $y1);
}

function azel2xy($az, $el, $sz){
	#rotate coords... 90deg W = 180deg trig
	$az += 270;

	#turn into radians
	$az = deg2rad($az);

	# determine length of radius
	$r = $sz * 0.5 * 0.95;
	$r -= ($r * ($el/90));

	# and convert length/azimuth to cartesian
	$x = sprintf("%d", (($sz * 0.5) + ($r * cos($az))));
	$y = sprintf("%d", (($sz * 0.5) + ($r * sin($az))));
	$x = $sz - $x;

	return array($x, $y);
}

function splot($im, $sz, $C, $e){
	list($sv, $az, $el, $snr, $u) = $e;
	$color = $C['brightgreen'];
	if ($snr < 40)
		$color = $C['darkgreen'];
	if ($snr < 35)
		$color = $C['yellow'];
	if ($snr < 30)
		$color = $C['red'];
	if ($el<10)
		$color = $C['blue'];
	if ($sv > 100)
		$color = $C['orange'];

	list($x, $y) = azel2xy($el, $az, $sz);

	$r = 12;
	if (isset($_GET['sz']) && ($_GET['sz'] == 'small'))
		$r = 8;

	imageString($im, 3, $x+4, $y+4, $sv, $C['black']);
	if ($u)
		imageFilledArc($im, $x, $y, $r, $r, 0, 360, $color, 0);
	else
		imageArc($im, $x, $y, $r, $r, 0, 360, $color);
}

function elevation($im, $sz, $C, $a){
	$b = 90 - $a;
	$a = $sz * 0.95 * ($a/180);
	imageArc($im, $sz/2, $sz/2, $a*2, $a*2, 0, 360, $C['ltgray']);
	$x = $sz/2 - 16;
	$y = $sz/2 - $a;
	imageString($im, 2, $x, $y, $b, $C['black']);
}

function skyview($im, $sz, $C){
	$a = 90; $a = $sz * 0.95 * ($a/180);
	imageFilledArc($im, $sz/2, $sz/2, $a*2, $a*2, 0, 360, $C['mdgray'], 0);
	imageArc($im, $sz/2, $sz/2, $a*2, $a*2, 0, 360, $C['black']);
	$x = $sz/2 - 16; $y = $sz/2 - $a;
	imageString($im, 2, $x, $y, "0", $C['black']);

	$a = 85; $a = $sz * 0.95 * ($a/180);
	imageFilledArc($im, $sz/2, $sz/2, $a*2, $a*2, 0, 360, $C['white'], 0);
	imageArc($im, $sz/2, $sz/2, $a*2, $a*2, 0, 360, $C['ltgray']);
	imageString($im, 1, $sz/2 - 6, $sz+$a, '5', $C['black']);
	$x = $sz/2 - 16; $y = $sz/2 - $a;
	imageString($im, 2, $x, $y, "5", $C['black']);

	for($i = 0; $i < 180; $i += 15){
		list($x0, $y0, $x1, $y1) = radial($i, $sz);
		imageLine($im, $x0, $y0, $x1, $y1, $C['ltgray']);
	}

	for($i = 15; $i < 90; $i += 15)
		elevation($im, $sz, $C, $i);

	$x = $sz/2 - 16; $y = $sz/2 - 8;
	imageString($im, 2, $x, $y, "90", $C['black']);

	imageString($im, 4, $sz/2 + 4, 2        , 'N', $C['black']);
	imageString($im, 4, $sz/2 + 4, $sz - 16 , 'S', $C['black']);
	imageString($im, 4, 4        , $sz/2 + 4, 'E', $C['black']);
	imageString($im, 4, $sz - 10 , $sz/2 + 4, 'W', $C['black']);

}

function gen_image($sock){
	global $magic;

	$sz = 640;
	if (isset($_GET['sz']) && ($_GET['sz'] == 'small'))
		$sz = 240;

	if ($magic){
		if (!$sock)
			die("socket failed");

		fwrite($sock, "Y\n");

		if (feof($sock))
			die("read error");

		$resp = fread($sock, 256);
	} else {
		$resp = 'GPSD,Y=MID9 1158081774.000000 12:25 24 70 42 1:4 13 282 36 1:23 87 196 48 1:6 9 28 29 1:16 54 102 47 1:20 34 190 45 1:2 12 319 36 1:13 52 292 46 1:24 12 265 0 0:1 8 112 41 1:27 16 247 40 1:122 23 213 31 0:';
	}
	if (!preg_match('/GPSD,Y=\S+ [0-9\.]+ (\d+):/', $resp, $m))
		die("can't parse gpsd's response");
	$n = $m[1];	

	$im = imageCreate($sz, $sz);
	$C = colorsetup($im);
	skyview($im, $sz, $C);

	$s = explode(':', $resp);
	for($i = 1; $i <= $n; $i++){
		$e = explode(' ', $s[$i]);
		splot($im, $sz, $C, $e);
	}

	header("Content-type: image/png");
	imagePNG($im);
	imageDestroy($im);
}

function clearstate(){
	global $GPS;

	$GPS['loc'] = '';
	$GPS['alt'] = 'Unavailable';
	$GPS['lat'] = 'Unavailable';
	$GPS['lon'] = 'Unavailable';
	$GPS['sat'] = 'Unavailable';
	$GPS['hdop'] = 'Unavailable';
	$GPS['dgps'] = 'Unavailable';
	$GPS['fix'] = 'Unavailable';
	$GPS['gt'] = '';
	$GPS['lt'] = '';
}

function dfix($x, $y, $z){
	if ($x < 0){
		$x = sprintf("%f %s", -1 * $x, $z);
	} else {
		$x = sprintf("%f %s", $x, $y);
	}
	return $x;
}

function parse_pvt($sock){
	global $GPS, $magic;

	clearstate();

	if ($magic && $sock){
		fwrite($sock, "J=1,SPAMQ\n");
		if (!feof($sock))
			$resp = fread($sock, 128);
	} else {
		$resp = 'GPSD,S=2,P=53.527167 -113.530168,A=704.542,M=3,Q=10 1.77 0.80 0.66 0.61 1.87';
	}
	
	if (strlen($resp)){
		$GPS['fix']  = 'No';
		if (preg_match('/M=(\d),/', $resp, $m)){
			switch ($m[1]){
			case 2:
				$GPS['fix']  = '2D';
				break;
			case 3:
				$GPS['fix']  = '3D';
				break;
			case 4:
				$GPS['fix']  = '3D (PPS)';
				break;
			default:
				$GPS['fix']  = "No";
			}
		}

		if (preg_match('/S=(\d),/', $resp, $m)){
			$GPS['fix'] .= ' (';
			if ($m[1] != 2){
				$GPS['fix'] .= 'not ';
			}
			$GPS['fix'] .= 'DGPS corrected)';
		}

		if (preg_match('/A=([0-9\.-]+),/', $resp, $m)){
			$GPS['alt'] = ($m[1] . ' m');
		}

		if (preg_match('/P=([0-9\.-]+) ([0-9\.-]+),/',
		    $resp, $m)){
			$GPS['lat'] = $m[1]; $GPS['lon'] = $m[2];
		}

		if (preg_match('/Q=(\d+) ([0-9\.]+) ([0-9\.]+) ([0-9\.]+) ([0-9\.]+)/', $resp, $m)){
			$GPS['sat']  = $m[1]; $GPS['gdop'] = $m[2];
			$GPS['hdop'] = $m[3]; $GPS['vdop'] = $m[4];
		}

		if ($GPS['lat'] != 'Unavailable' &&
		    $GPS['lon'] != 'Unavailable'){
			$GPS['lat'] = dfix($GPS['lat'], 'N', 'S');
			$GPS['lon'] = dfix($GPS['lon'], 'E', 'W');

			$GPS['loc'] = sprintf('at %s / %s',
			    $GPS['lat'], $GPS['lon']);
		}

		if (preg_match('/^No/', $GPS['fix'])){
			clearstate();
		}
	} else {
		echo "$errstr ($errno)<br>\n";
		$GPS['loc'] = '';
	}

	$GPS['gt'] = time();
	$GPS['lt'] = date("r", $GPS['gt']);
	$GPS['gt'] = gmdate("r", $GPS['gt']);
}

function write_html(){
	global $GPS, $server, $port, $head, $body;
	global $blurb, $title, $autorefresh, $footer;

	header("Content-type: text/html; charset=UTF-8");

	global $lat, $lon;
	$lat = (float)$GPS['lat'];
	$lon = -(float)$GPS['lon'];
	$x = $server; $y = $port;
	include("gpsd_config.inc"); # breaks things
	$server = $x; $port = $y;

	if ($autorefresh > 0)
		$autorefresh = "<META HTTP-EQUIV='Refresh' CONTENT='$autorefresh'>";
	else
		$autorefresh = '';

	$cvs ='$Id: gpsd.php,v 1.36 2006/10/31 00:04:26 ckuethe Exp $';
	$buf = <<<EOF
<!DOCTYPE html PUBLIC "-//W3C//DTD HTML 4.01 Transitional//EN">
<HTML>
<HEAD>
{$head}
<META http-equiv="Content-Type" content="text/html; charset=UTF-8">
<META http-equiv="Content-Language" content="en,en-us">
<TITLE>{$title} - GPSD Test Station {$GPS['loc']}</TITLE>
{$autorefresh}
</HEAD>

<BODY {$body}>
<center>
<table border="0">
<tr><td align="justify">
{$blurb}
</td>

<!-- ------------------------------------------------------------ -->

<td rowspan="4" align="center" valign="top">
<img src="?host={$server}&amp;port={$port}&amp;op=view"
width="640" height="640" alt="Skyplot"></td>
</tr>

<!-- ------------------------------------------------------------ -->

<tr><td align="justify">To get real-time information, connect to
<tt>telnet://{$server}:{$port}/</tt> and type "R".<br>
<form method=GET action="${_SERVER['SCRIPT_NAME']}">Use a different server:
<input name="host" value="{$server}">:
<input name="port" value="{$port}" size="5" maxlength="5">
<input type=submit value="Get Position"><input type=reset></form>
<br>
</td>
</tr>

<!-- ------------------------------------------------------------ -->

<tr><td align=center valign=top>
	<table border=1>
	<tr><td colspan=2 align=center><b>Current Information</b></td></tr>
	<tr><td>Time (Local)</td><td>{$GPS['lt']}</td></tr>
	<tr><td>Time (UTC)</td><td>{$GPS['gt']}</td></tr>
	<tr><td>Latitude</td><td>{$GPS['lat']}</td></tr>
	<tr><td>Longitude</td><td>{$GPS['lon']}</td></tr>
	<tr><td>Altitude</td><td>{$GPS['alt']}</td></tr>
	<tr><td>Fix Type</td><td>{$GPS['fix']}</td></tr>
	<tr><td>Satellites</td><td>{$GPS['sat']}</td></tr>
	<tr><td>HDOP</td><td>{$GPS['hdop']}</td></tr>
	</table>
</tr>
<tr><td> </td></tr>
</table>
</center>

{$footer}

<a href="http://gpsd.mainframe.cx/gpsd.phps">Script source</a><br>
<font size=-2><tt>{$cvs}</tt></font><br>
</BODY>
</HTML>

EOF;

print $buf;

}

function write_config(){
	$f = fopen("gpsd_config.inc", "a");
	if (!$f)
		die("can't generate prototype config file. try running this script as root in DOCUMENT_ROOT");

	$buf = <<<EOB
<?PHP
\$title = 'My GPS Server';
\$server = '127.0.0.1';
\$port = 2947;
\$autorefresh = 0; # number of seconds after which to refresh

## You can read the header, footer and blurb from a file...
# \$head = file_get_contents('/path/to/header.inc');
# \$body = file_get_contents('/path/to/body.inc');
# \$footer = file_get_contents('/path/to/footer.hinc');
# \$blurb = file_get_contents('/path/to/blurb.inc');

## ... or you can just define them here
\$head = '';
\$body = '';
\$footer = '';
\$blurb = <<<EOT
This is a
<a href="http://gpsd.berlios.de">gpsd</a>
server <blink><font color="red">located someplace</font></blink>.

The hardware is a
<blink><font color="red">hardware description and link</font></blink>.

This machine is maintained by
<a href="mailto:you@example.com">Your Name Goes Here</a>.<br><br>
EOT;

?>

EOB;
	fwrite($f, $buf);
	fclose($f);
}
?>
