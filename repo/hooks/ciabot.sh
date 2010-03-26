#!/bin/sh
# Distributed under the terms of the GNU General Public License v2
# Copyright (c) 2006 Fernando J. Pereda <ferdy@gentoo.org>
# Copyright (c) 2008 Natanael Copa <natanael.copa@gmail.com>
# Copyright (c) 2010 Eric S. Raymond <esr@thyrsus.com>
#
# Git CIA bot in bash. (no, not the POSIX shell, bash).
# It is *heavily* based on Git ciabot.pl by Petr Baudis.
# This script contains porcelain and porcelain byproducts.
#
# It is meant to be run either on a post-commit hook or in an update
# hook:
#
# post-commit: It parses latest commit and current HEAD to get the
# information it needs.
#
# update: You have to call it once per merged commit:
#
#       refname=$1
#       oldhead=$2
#       newhead=$3
#       for merged in $(git rev-list ${oldhead}..${newhead} | tac) ; do
#               /path/to/ciabot.bash ${refname} ${merged}
#       done
#

# The project as known to CIA:
project="GPSD"
repo="${REPO:-gpsd}"

# Changeset URL prefix for your repo: when the commit ID is appended
# to this, it should point at a CGI that will display the commit
# through gitweb or something similar.
urlprefix="http://git.berlios.de/cgi-bin/gitweb.cgi?p=$repo;a=commit;h="

# You probably will not need to change the following:

# Addresses for the e-mail
from="$LOGNAME@`hostname --fqdn`"
to="cia@cia.vc"

# SMTP client to use - may need to edit the absolute pathname for your system
sendmail="/usr/sbin/sendmail -t -f ${from}"

#
# No user-serviceable parts below this line
#

PATH="$PATH:`git --exec-path`"

if [ $# -eq 0 ] ; then
	refname=$(git symbolic-ref HEAD 2>/dev/null)
	merged=$(git rev-parse HEAD)
else
	refname=$1
	merged=$2
fi

url=$(wget -O - -q http://tinyurl.com/api-create.php?url=${urlprefix}${merged} 2>/dev/null)
if [ -z "$url" ]; then
	url="${urlprefix}${merged}"
fi

refname=${refname##refs/heads/}

gitver=$(git --version)
gitver=${gitver##* }

rev=$(git describe ${merged} 2>/dev/null)
[ -z ${rev} ] && rev=${merged:0:12}

rawcommit=$(git cat-file commit ${merged})
author=$(echo "$rawcommit" | sed -n -e '/^author .*<\([^@]*\).*$/s--\1-p')
logmessage=$(echo "$rawcommit" | sed -e '1,/^$/d' | head -n 1)
logmessage=$(echo "$logmessage" | sed 's/\&/&amp\;/g; s/</&lt\;/g; s/>/&gt\;/g')
ts=$(echo "$rawcommit" | sed -n -e '/^author .*> \([0-9]\+\).*$/s--\1-p')

# 
#
#      <files>
#        $(git diff-tree -r --name-only ${merged} |
#          sed -e '1d' -e 's-.*-<file>&</file>-')
#      </files>

out="
<message>
  <generator>
    <name>CIA Shell client for Git</name>
    <version>${gitver}</version>
    <url>http://dev.alpinelinux.org/~ncopa/alpine/ciabot.sh</url>
  </generator>
  <source>
    <project>${project}</project>
    <branch>$repo:${refname}</branch>
  </source>
  <timestamp>${ts}</timestamp>
  <body>
    <commit>
      <author>${author}</author>
      <revision>${rev}</revision>
      <log>${logmessage} ${url}</log>
      <url>${url}</url>
    </commit>
  </body>
</message>"

${sendmail} << EOM
Message-ID: <${merged:0:12}.${author}@${project}>
From: ${from}
To: ${to}
Content-type: text/xml
Subject: DeliverXML
${out}
EOM

# vim: set tw=70 :
