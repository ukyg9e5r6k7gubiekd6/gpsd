#!/usr/bin/perl -wT
#
# This is the CGI that processes the form return from GPS information form.
# It's in Perl rather than Python because Berlios and many other sites 
# don't support Python CGIs.
#
use CGI qw(:standard);
use CGI::Carp qw(warningsToBrowser fatalsToBrowser);
use MIME::Base64;
use MIME::Lite;
use Net::SMTP;
use strict;

# All information on the project siting, mailing
# lists, and originating host lives here.
my $website = 'http://gpsd.berlios.de';
my $to_address = 'gpsd-dev@berlios.de';
my $servaddr = 'www@mainframe.cx';

my $query = new CGI;
print $query->header;
print $query->start_html(-title=>"GPSD Receiver Reporting Form",
			 -background=>"../paper.gif");

my $output_sample_file = $query->param('output_sample');
my $output_sample_body = $query->param('output_sample_body');
$output_sample_body = '' unless ($output_sample_body);

do {
	local $/ = undef;
	my $x = <$output_sample_file>;
	$output_sample_body = encode_base64($x, "") if ($x);
};

if (hasNeededElements($query) && $query->param("action") eq "Send Report"){
	# handle successful upload...

	### mail parameters
	my $from_address = "HTTP Server <${servaddr}>";
	my $mailhost = '127.0.0.1';
	my $subject = 'new gps report';

	### Create the multipart container
	my $email = MIME::Lite->new (
	    From => $from_address,
	    To => $to_address,
	    Subject => $subject,
	    Type =>'multipart/mixed'
	) or die "Error creating multipart container: $!\n";

	### build message text
	my ($var, $val, $msg);
	$msg = "Remote: ${ENV{'REMOTE_ADDR'}}:${ENV{'REMOTE_PORT'}}\n\n";
	$msg .= sprintf("[%s]\n", $query->param('model'));
	$msg .= sprintf("type = device\n");
	foreach $var ( sort qw(submitter vendor model packaging techdoc chipset
                        firmware nmea interfaces tested rating noconfigure 
                        location date notes sample_notes)){
		$val = $query->param($var);
		$msg .= sprintf("%s = %s\n", $var, $val) if (defined($val) && $val);
	}

	### Add the text message part
	$email->attach (
	    Type => 'TEXT',
	    Data => $msg,
	) or die "Error adding the text message part: $!\n";

	### Add the sample
	$email->attach (
	    Type => 'application/octet-stream',
	    Data => decode_base64($output_sample_body),
	    Filename => 'sample.bin',
	    Disposition => 'attachment',
	    Encoding => 'base64'
	) or die "Error adding sample output: $!\n";

	#MIME::Lite->send('smtp', $mail_host, Timeout=>60, Debug=>1);
	#$email->send;
	$email->send('smtp', $mailhost, Timeout=>60, Debug=>1);

	print "new gps report accepted...\n";
	exit(0);
}

print $query->start_multipart_form;

print <<EOF;
<h1>GPSD Receiver Reporting Form</h1>

<p>Please use this form to report <code>gpsd</code> successes or
failures with GPS and AIS units, and also to upload a sample of the receiver's
output so we can add it to our regression tests and ensure continued
support of the device.</p>

<p>Fields marked <em style='color: #ff0000;'>Important!</em> have to be filled
in for the report to be useful.  These are: submitter contact address, vendor,
model, documentation URL, and output sample.  Other fields represent things we
might be able to find out ourselves, but which are easier for you to determine.
Every bit of information you can give us about your receiver will help make the
support for it more reliable.</p>

<hr/>
<h2>Contact information</h2>

<p><em style='color: #ff0000;'>Important!</em> We prefer a valid email
address for you in case we need to ask you followup questions about
the device.  While we won't use your address for anything other than
asking you questions about your receiver, and maybe asking you to test
specific changes, this gps report will be sent to the gpsd-dev list
which is publicly archived.
<br/>Example: <code>Eric Raymond &lt;esr&#x40;thyrsus.com&gt;</code></p>

EOF

print "<em>Name and email address:</em>",$query->textfield(-name=>"submitter",
							    -size=>72);
print <<EOF;

<p>(It is not actually very likely we will contact you, but we need to
be able to do it if we can find no other way of getting information
about the device.  Expect to hear from us if your receiver is obsolescent or
exotic and the information you provide in the rest of this form turns
out to be insufficient. Or if your browser is broken enough to botch
the output-sample upload.)</p>

<hr/>
<h2>Receiver type identification</h2>

<p><em style='color: #ff0000;'>Important!</em> Identify the vendor and model of
your device.
<br/>Example: <code>Haicom</code> and <code>303S</code>.</p>

EOF

print "<p><em>Vendor:</em>",$query->textfield(-name=>"vendor", -size=>72),"</p>";

print "<p><em>Model:</em>",$query->textfield(-name=>"model", -size=>72),"</p>";

print <<EOF;
<p><em style='color: #ff0000;'>Important!</em> We need a URL pointing to a
technical manual for the device.  You can usually find this on the
vendor's website by giving a search engine the product name.  If it's
not linked directly from the vendor's page for the individual product,
look under "Technical Support" or "Product support" on the vendor's
main page.
<br/>Example: <code>http://www.haicom.com.tw/gps303s.shtml</code></p>

<p><em>URL of a technical manual:</em>
EOF

print $query->textfield(-name=>"techdoc", -size=>72);

print <<EOF;
<p>It is useful to have an indication of how the receiver is packaged.

<ul>
<li>A "mouse" is a standalone sensor in a display-less case designed 
be used as an peripheral to a computer.</li>
<li>A "handset" is a standalone unit with a display and human-usable controls.</li>
<li>A "handsfree" is a hands-free unit with display designed for mounting 
on a car windshield or boat dash.</li>
<li>A "survey" unit is packaged for field-survey use.</li> 
<li>An "OEM module" is an un-cased circuit board with edge connectors.</li>
<li>A "chipset" is a bare chip or chips packaged for surface mount.</li>
</ul>

<p><em>Packaging:</em><br>
EOF

print $query->radio_group(-name=>'packaging',
			  -values=>['mouse', 'handset', 'handsfree',
				'survey', 'OEM module', 'chipset', 'other'],
			  -default=>"mouse",
			  -linebreak=>'false');


print <<EOF;
<p>Please identify the device chipset and firmware version, if possible.  You
may be able to get this from the display of <code>xgps</code>; look for
a Device Type field or at the window title bar. Alternatively, you may find
it in the technical manual.
<br/>Example: <code>SiRF-II</code> and <code>2.31ES</code>.</p>
EOF

print "<p><em>Chipset:</em>",
    $query->textfield(-name=>"chipset", -size=>72),"</p>\n";
print "<p><em>Firmware:</em>",
    $query->textfield(-name=>"firmware", -size=>72),"</p>\n";

print <<EOF;
<p>Please identify, if possible, the NMEA version the receiver emits.
You may be able to get this information from the technical manual.
Likely values are <code>2.0</code>, <code>2.2</code>, and <code>3.0</code>.
If the GPS emits only a vendor binary protocol, leave this field blank.</p>
EOF


print "<em>NMEA 0183 version emitted:</em>",$query->textfield(-name=>"nmea",
							    -size=>6);

print <<EOF;
<hr/>
<h2>Interfaces</h2>

<p>Please identify the receiver's interface type (USB, RS-232, Bluetooth,
Compact Flash, etc.). If the receiver has adapters that support other
interfaces, tell us the one you have and mention the adapters in the "Technical
Notes" box.  If it has an exotic interface not listed here, select "Other" and
tell us about it in "Technical Notes".</p>

EOF


print $query->radio_group(-name=>'interfaces',
			  -values=>['USB', 'Serial', 'Bluetooth', 'TTL',
				    'Compact Flash', 'RS-232', 'Other'],
			  -default=>"-",
			  -linebreak=>'false');

print <<EOF;
<p>If your device is USB, it probably uses a USB-to-serial adapter
chip.  Try to find out what this is by looking at the output of
<code>lsusb(1)</code>.  Likely values are <code>PL2303</code>,
<code>UC-232A</code>, <code>FTDI 8U232AM</code>, <code>Cypress
M8</code> or <code>Silicon Labs CP2102</code>.</p>

EOF

print"<em>USB-to-serial chip:</em>",$query->textfield(-name=>"usbchip",
							    -size=>72);

print <<EOF;
<hr/>
<h2>GPSD compatibility</h2>

<p>Please tell us what version you tested with.  If you used us a release,
give us the full release number, like <code>2.35</code>.  If you built
your code from our development repository please give the revision number,
like <code>r4595</code>.</p>

EOF

print "<em>Tested with:</em>",$query->textfield(-name=>"tested",
						-size=>6);

print <<EOF;
<p>Please rate how well this receiver functions with GPSD:</p>

EOF

my %labels=(
    "excellent",
    "Excellent -- gpsd recognizes the receiver rapidly and reliably, reports are complete and correct.",
    "good",
    "Good -- gpsd has minor problems or lag recognizing the device, but reports are complete and correct.",
    "fair",
    "Fair -- Reports have minor dropouts or problems, including occasional transient nonsense values.",
    "poor",
    "Poor -- Reports frequently have values that are wrong or nonsense.",
    "broken",
    "Broken -- gpsd frequently, or always, fails to recognize the device at all.",
    "other",
    "Other -- See Technical Notes.",
    );
print $query->radio_group(-name=>'rating',
			  -values=>['excellent', 'good', 'fair',
				    'poor', 'broken', 'other'],
			  -default=>"-",
			  -labels=>\%labels,
			  -linebreak=>'true');

print "Does the device break if probed or speed switched? " .
	$query->checkbox_group(-name=>'noconfigure',
			     -values=>['yes'],
			     -defaults=>[]);

print <<EOF;
<hr/>
<h2>Technical notes</h2>

<p>Now tell us the things that didn't fit in the rest of the form.
Appropriate things to put here include how to read any LEDs or other
unlabeled indicators on the device, a warning that the product has
been discontinued, a list of alternate interfaces, descriptions of
errors in the documentation, applicable PPS offsets, descriptions of
special abilities such as the ability to vary the sampling interval,
and a note if it's an OEM module rather than a retail product.
Anything else you think we need to know should go here too.</p>

EOF

print $query->textarea(-name=>"notes", -rows=>10, -cols=>72);

print <<EOF;
<hr/>
<h2>Output sample</h2>

<p><em style='color: #ff0000;'>Important!</em> We need a sample of the output
from your receiver - not the gpsd logfile, just raw output.  We'll use this for
mechanical regression testing, which is your best guarantee that support for
your device won't get broken in a future release. Please be advised that these
logs will be sent to a publicly archived mailing list, and will be available in
the public code repository.</p>

<p>Almost all receivers will simply start throwing data to your port
immediately when they're plugged in. You should normally be able to capture
this output to a file with the <code>gpscat</code> utility.</p>

<p>There will be some unusual cases in which this isn't possible,
because the device needs some kind of activation sequence written to
it before it will start reporting.  Some Garmin GPSes (the ones that
speak Garmin binary protocol rather than NMEA) are like this.  If you
think you have one of these, ask the <a
href="mailto:gpsd-dev\@lists.berlios.de">GPSD developers</a> for
help.</p>

<p>A log file is most useful when it contains (a) some sentences
generated when the receiver has no fix, (b) some sentences representing
a fix with the unit stationary, and (c) some sentences representing
a fix with the unit moving.</p>

EOF

print $query->filefield(-name=>'output_sample',
			-size=>72);
printf("\n<input type='hidden' name='output_sample_body' value='%s'>\n", $output_sample_body);

print <<EOF;

<p>There is some auxiliary data we like to have in our regression-test
files.</p>

<p>Location of the log capture. A good format would include your
nearest city or other landmark, state/province, country code, and a
rough latitude/longitude.  A GPS will give an exact location; we
want this as a sanity check.
<br/>Example: <code>Groningen, NL, 53.2N 6.6E</code></p>

EOF

print"<em>Location:</em>",$query->textfield(-name=>"location",
					    -size=>72);

print <<EOF;

<p>Day/month/year of the log capture (the receiver will give us
hour/minute/second).
<br/>Example: <code>20 May 2006</code>.</p>


EOF

print"<em>Date:</em>",$query->textfield(-name=>"date", -size=>72);

print <<EOF;

<p>Finally, add any notes you want to about how the sample was taken.  One
good thing to put here would a description of how the unit was moving while the
log was being captured.  If the sentence mix changes between "fix" and "no fix"
states, that too is a good thing to note.</p>

EOF

print $query->textarea(-name=>"sample_notes", -rows=>10, -cols=>72);

print <<EOF;

<hr/>
<p>Thanks for your help.  Here is a summary of the information you have
entered so far:</p>

EOF

print "<table border='0' width='100%'><tr><td align='center'>";

if ($query->param("submitter")) {
    print "Contact address is <code>". escapeHTML($query->param("submitter")) ."</code><br/>\n";
} else {
    print "<span style='color:#ff0000;'>No contact address.</span><br/>\n";
}
if ($query->param("vendor")) {
    print "Vendor is <code>". escapeHTML($query->param("vendor")) ."</code><br/>\n";
} else {
    print "<span style='color:#ff0000;'>No vendor.</span><br/>\n";
}
if ($query->param("packaging")) {
    print "Packaging type is <code>". escapeHTML($query->param("packaging")) ."</code><br/>\n";
} else {
    print "No packaging type specified.<br/>\n";
}
if ($query->param("model")) {
    print "Model is <code>". escapeHTML($query->param("model")) ."</code><br/>\n";
} else {
    print "<span style='color:#ff0000;'>No model specified.</span><br/>\n";
}
if ($query->param("techdoc")) {
    print "<a href='". escapeHTML($query->param("techdoc")) ."'>Document URL specified.</a><br/>\n";
} else {
    print "<span style='color:#ff0000;'>No document URL.</span><br/>\n";
}
if ($output_sample_body) {
    print "Output sample uploaded";
} else {
    print "<span style='color:#ff0000;'>No output sample.</span><br/>\n";
}

print "</td><td align='center'>";

if ($query->param("chipset")) {
    print "Chipset is <code>". escapeHTML($query->param("chipset")) ."</code><br/>\n";
} else {
    print "Chipset not specified.<br/>\n";
}
if ($query->param("firmware")) {
    print "Firmware is <code>". escapeHTML($query->param("firmware")) ."</code><br/>\n";
} else {
    print "Firmware not specified.<br/>\n";
}
if ($query->param("nmea")) {
    print "NMEA version is <code>". escapeHTML($query->param("nmea")) ."</code><br/>\n";
} else {
    print "NMEA version not specified.<br/>\n";
}
if ($query->param("interfaces")) {
    print "Interface type is <code>". escapeHTML($query->param("interfaces")) ."</code><br/>\n";
    if ($query->param("interfaces") eq "USB") {
	if ($query->param("usbchip")) {
	    print "USB chip is <code>". escapeHTML($query->param("usbchip")) ."</code><br/>\n";
	} else {
	    print "No USB chip specified.<br/>\n";
	}
    }
} else {
    print "No interface type specified.<br/>\n";
}
if ($query->param("tested")) {
    print "Tested with GPSD version <code>". escapeHTML($query->param("tested")) ."</code><br/>\n";
} else {
    print "No GPSD version specified.<br/>\n";
}
if ($query->param("rating")) {
    print "GPSD compatibility is <code>". escapeHTML($query->param("rating")) ."</code><br/>\n";
} else {
    print "No GPSD compatiblity specified.<br/>\n";
}
if ($query->param("noconfigure")) {
    print "Device can be sent catatonic by baud-rate changes<br>\n";
} else {
    print "Device handles baud-rate changes correctly<br>\n";
}

print "</td><td align='center'>";

if ($query->param("notes")) {
    print "Technical notes have been entered.<br/>\n";
} else {
    print "No technical notes.<br/>\n";
}
if ($query->param("location")) {
    print "Sample location <code>". escapeHTML($query->param("location")) ."</code><br/>\n";
} else {
    print "No sample location specified.<br/>\n";
}
if ($query->param("date")) {
    print "Sample date <code>". escapeHTML($query->param("date")) ."</code><br/>\n";
} else {
    print "No sample date specified.<br/>\n";
}

if ($query->param("sample_notes")) {
    print "Notes on the sample have been entered.<br/>\n";
} else {
    print "No notes on the sample.<br/>\n";
}

print "</td></tr></table>";

print "<p>To refresh this summary, click <code>Review</code>\n";


# Must have all critical fields to ship
if (hasNeededElements($query)){

    print <<EOF;
<p>Click the <code>Send Report</code> button to
send your report to the GPSD developers.  Eventually, your report is
likely to appear on our <a href="${website}/hardware.html">Hardware</a> page.</p>

<table width="100%" border="0">
<tr>
<td align='center'>
<a href="${ENV{'REQUEST_URI'}}">Reset Form</a>
<input type="submit" name="action" value="Review">
<input type="submit" name="action" value="Send Report">
</td>
</tr>
</table>
EOF

} else {
    print <<EOF;
<p style='color:#ff0000;'>Required fields are missing; please fill them in and click
<code>Review</code>.</p>

<table width="100%" border="0">
<tr>
<td align='center'>
	<a href="${ENV{'REQUEST_URI'}}">Reset Form</a>
	<input type="submit" name="action" value="Review">
</td>
</tr>
</table>
EOF
}

print "</form>\n<hr/>\n";


print $query->end_html;

sub hasNeededElements{
	my $query = $_[0];
	return 1 if ($query->param("submitter") &&
			$query->param("vendor") &&
			$query->param("model") &&
			$query->param("techdoc") &&
			$output_sample_body);
	return 0;
}

# The following sets edit modes for GNU EMACS
# Local Variables:
# fill-column:79
# End:

