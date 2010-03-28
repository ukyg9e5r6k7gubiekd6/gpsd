#!/usr/bin/env python
# Distributed under the terms of the GNU General Public License v2
# Copyright (c) 2010 Eric S. Raymond <esr@thyrsus.com>
#
# This script contains porcelain and porcelain byproducts.
# It's Python because the Python standard libraries avoid portability/security
# issues raised by callouts in the ancestral Perl and sh scripts.  It should
# be compatible back to Python 2.1.5.
#
# It is meant to be run either on a post-commit hook or in an update
# hook:
#
# post-commit: It queries for current HEAD and latest commit ID to get the
# information it needs.
#
# update: You have to call it once per merged commit:
#
#       refname=$1
#       oldhead=$2
#       newhead=$3
#       for merged in $(git rev-list ${oldhead}..${newhead} | tac) ; do
#               /path/to/ciabot.py ${refname} ${merged}
#       done
#
# Note: this script uses mail, not XML-RPC, in order to avoid stalling
# until timeout when the XML-RPC server is down. 
#
# Call with -n to see the notification mail dumped to stdout rather
# than shipped to CIA.
#
import os, sys, commands, socket, urllib
#
# The project as known to CIA. You will want to change this:
#
project="GPSD"

#
# You may not need to change these:
#

# Name of the repository.
# You can hardwire this to make the script faster.
repo = os.path.basename(os.getcwd())

# Fully-qualified domain name of this host.
# You can hardwire this to make the script faster.
host = socket.getfqdn()

# Changeset URL prefix for your repo: when the commit ID is appended
# to this, it should point at a CGI that will display the commit
# through gitweb or something similar. The default will probably
# work if you have a typical gitweb/cgit setup.
#
#urlprefix="http://%(host)s/cgi-bin/gitweb.cgi?p=%(repo)s;a=commit;h="%locals()
urlprefix="http://%(host)s/cgi-bin/cgit.cgi/%(repo)s/commit/?id="%locals()

# The service used to turn your gitwebbish URL into a tinyurl so it
# will take up less space on the IRC notification line.
tinyifier = "http://tinyurl.com/api-create.php?url="

#
# No user-serviceable parts below this line:
#

def do(command):
    return commands.getstatusoutput(command)[1]

# Addresses for the e-mail. The from address is a dummy, since CIA
# will never reply to this mail.
fromaddr = "CIABOT-NOREPLY@" + host
toaddr = "cia@cia.vc"

# Identify the generator script.
# Should only change when the script itself has a new home
generator="http://www.catb.org/~esr/ciabot.sh"

# Git version number.
gitver = do("git --version").split()[0]

# Should add to the command path both places sendmail is likely to lurk, 
# and the git private command directory.
os.environ["PATH"] += ":/usr/sbin/:" + do("git --exec-path")

# Option flags
mailit = True 

def report(refname, merged):
    "Report a commit notification to CIA"

    # Try to tinyfy a reference to a web view for this commit.
    try:
        url = open(urllib.urlretrieve(tinyifier + urlprefix + merged)[0]).read()
    except:
        url = urlprefix + merged

    shortref = os.path.basename(refname)

    # Compute a shortnane for the revision
    rev = do("git describe ${merged} 2>/dev/null") or merged[:12]

    # Extract the neta-information for the commit
    rawcommit = do("git cat-file commit " + merged)
    files=do("git diff-tree -r --name-only '"+ merged +"' | sed -e '1d' -e 's-.*-<file>&</file>-'")
    inheader = True
    headers = {}
    logmessage = ""
    for line in rawcommit.split("\n"):
        if inheader:
            if line:
                fields = line.split()
                headers[fields[0]] = " ".join(fields[1:])
            else:
                inheader = False
        else:
            logmessage = line
            break
    (author, ts) = headers["author"].split(">")
    author = author.replace("<", "").split("@")[0].split()[-1]
    ts = ts.strip()

    context = locals()
    context.update(globals())

    out = '''\
<message>
  <generator>
    <name>CIA Shell client for Git</name>
    <version>%(gitver)s</version>
    <url>%(generator)s</url>
  </generator>
  <source>
    <project>%(project)s</project>
    <branch>%(repo)s:%(shortref)s</branch>
  </source>
  <timestamp>%(ts)s</timestamp>
  <body>
    <commit>
      <author>%(author)s</author>
      <revision>%(rev)s</revision>
      <files>
        %(files)s
      </files>
      <log>%(logmessage)s %(url)s</log>
      <url>%(url)s</url>
    </commit>
  </body>
</message>
''' % context

    message = '''\
Message-ID: <%(merged)s.%(author)s@%(project)s>
From: %(fromaddr)s
To: %(toaddr)s
Content-type: text/xml
Subject: DeliverXML

%(out)s''' % locals()

    if mailit:
        server.sendmail(fromaddr, [toaddr], message)
    else:
        print message

if __name__ == "__main__":
    # Call this script with -n to dump the notification mail to stdout
    if sys.argv[1] == '-n':
        mailit = False
        sys.argv.pop(1)

    # In post-commit mode, the script wants a reference to head
    # followed by the commit ID to report about.
    if len(sys.argv) == 1:
        refname = do("git symbolic-ref HEAD 2>/dev/null")
        merged = do("git rev-parse HEAD")
    else:
        refname = sys.argv[1]
        merged = sys.argv[2]

    if mailit:
        import smtplib
        server = smtplib.SMTP('localhost')

    report(refname, merged)

    if mailit:
        server.quit()

#End
