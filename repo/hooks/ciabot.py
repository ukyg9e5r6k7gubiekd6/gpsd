#!/usr/bin/env python
# Copyright (c) 2010 Eric S. Raymond <esr@thyrsus.com>
# Distributed under BSD terms.
#
# This script contains porcelain and porcelain byproducts.
# It's Python because the Python standard libraries avoid portability/security
# issues raised by callouts in the ancestral Perl and sh scripts.  It should
# be compatible back to Python 2.1.5
#
# It is meant to be run either in a post-commit hook or in an update
# hook.
#
# In post-commit, run it without arguments. It will query for current HEAD and
# the latest commit ID to get the information it needs.
#
# In update, call it with a refname followed by a list of commits.
#
#       ciabot.py ${refname} $(git rev-list ${oldhead}..${newhead} | tac)
#
# Note: this script uses mail, not XML-RPC, in order to avoid stalling
# until timeout when the CIA XML-RPC server is down. 
#
# Call with -n to see the notification mail dumped to stdout rather
# than shipped to CIA. This may be useful for debugging purposes.
#

#
# The project as known to CIA. You will want to change this:
#
project="GPSD"

#
# You may not need to change these:
#
import os, sys, commands, socket, urllib

# Name of the repository.
# You can hardwire this to make the script faster.
repo = os.path.basename(os.getcwd())

# Fully-qualified domain name of this host.
# You can hardwire this to make the script faster.
host = socket.getfqdn()

# Changeset URL prefix for your repo: when the commit ID is appended
# to this, it should point at a CGI that will display the commit
# through gitweb or something similar. The defaults will probably
# work if you have a typical gitweb/cgit setup.
#
#urlprefix="http://%(host)s/cgi-bin/gitweb.cgi?p=%(repo)s;a=commit;h="
urlprefix="http://%(host)s/cgi-bin/cgit.cgi/%(repo)s/commit/?id="

# The service used to turn your gitwebbish URL into a tinyurl so it
# will take up less space on the IRC notification line.
tinyifier = "http://tinyurl.com/api-create.php?url="

# The template used to generate the XML messages to CIA.  You can make
# visible changes to the IRC-bot notification lines by hacking this.
# The default will produce a notfication line that looks like this:
#
# ${project}: ${author} ${repo}:${branch} * ${rev} ${files}: ${logmsg} ${url}
#
# By omitting $files you can collapse the files part to a single slash.
xml = '''\
<message>
  <generator>
    <name>CIA Python client for Git</name>
    <version>%(gitver)s</version>
    <url>%(generator)s</url>
  </generator>
  <source>
    <project>%(project)s</project>
    <branch>%(repo)s:%(branch)s</branch>
  </source>
  <timestamp>%(ts)s</timestamp>
  <body>
    <commit>
      <author>%(author)s</author>
      <revision>%(rev)s</revision>
      <files>
        %(files)s
      </files>
      <log>%(logmsg)s %(url)s</log>
      <url>%(url)s</url>
    </commit>
  </body>
</message>
'''

#
# No user-serviceable parts below this line:
#

# Addresses for the e-mail. The from address is a dummy, since CIA
# will never reply to this mail.
fromaddr = "CIABOT-NOREPLY@" + host
toaddr = "cia@cia.vc"

# Identify the generator script.
# Should only change when the script itself has a new home
generator="http://www.catb.org/~esr/ciabot.sh"

def do(command):
    return commands.getstatusoutput(command)[1]

def report(refname, merged):
    "Generare a commit notification to be reported to CIA"

    # Try to tinyfy a reference to a web view for this commit.
    try:
        url = open(urllib.urlretrieve(tinyifier + urlprefix + merged)[0]).read()
    except:
        url = urlprefix + merged

    branch = os.path.basename(refname)

    # Compute a shortnane for the revision
    rev = do("git describe ${merged} 2>/dev/null") or merged[:12]

    # Extract the neta-information for the commit
    rawcommit = do("git cat-file commit " + merged)
    files=do("git diff-tree -r --name-only '"+ merged +"' | sed -e '1d' -e 's-.*-<file>&</file>-'")
    inheader = True
    headers = {}
    logmsg = ""
    for line in rawcommit.split("\n"):
        if inheader:
            if line:
                fields = line.split()
                headers[fields[0]] = " ".join(fields[1:])
            else:
                inheader = False
        else:
            logmsg = line
            break
    (author, ts) = headers["author"].split(">")
    author = author.replace("<", "").split("@")[0].split()[-1]
    ts = ts.strip()

    context = locals()
    context.update(globals())

    out = xml % context

    message = '''\
Message-ID: <%(merged)s.%(author)s@%(project)s>
From: %(fromaddr)s
To: %(toaddr)s
Content-type: text/xml
Subject: DeliverXML

%(out)s''' % locals()

    return message

if __name__ == "__main__":
    # We''ll need the git version number.
    gitver = do("git --version").split()[0]

    # Add the git private command directory to the command path.
    os.environ["PATH"] += ":" + do("git --exec-path")

    urlprefix = urlprefix % globals()

    # Call this script with -n to dump the notification mail to stdout
    mailit = True
    if sys.argv[1] == '-n':
        mailit = False
        sys.argv.pop(1)

    # The script wants a reference to head followed by the list of
    # commit ID to report about.
    if len(sys.argv) == 1:
        refname = do("git symbolic-ref HEAD 2>/dev/null")
        merges = [do("git rev-parse HEAD")]
    else:
        refname = sys.argv[1]
        merges = sys.argv[2:]

    if mailit:
        import smtplib
        server = smtplib.SMTP('localhost')

    for merged in merges:
        message = report(refname, merged)
        if mailit:
            server.sendmail(fromaddr, [toaddr], message)
        else:
            print message

    if mailit:
        server.quit()

#End
