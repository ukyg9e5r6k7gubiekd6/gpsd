#!/bin/bash

umask 002

# If you are having trouble with this access control hook script
# you can try setting this to true.  It will tell you exactly
# why a user is being allowed/denied access.

verbose=true

# Default shell globbing messes things up downstream
GLOBIGNORE=*

function grant {
  $verbose && echo >&2 "-Grant-		$1"
  echo grant
  exit 0
}

function deny {
  $verbose && echo >&2 "-Deny-		$1"
  echo deny
  exit 1
}

function info {
  $verbose && echo >&2 "-Info-		$1"
}

# Implement generic branch and tag policies.
# - Tags should not be updated once created.
# - Branches should only be fast-forwarded unless their pattern starts with '+'
case "$1" in
  refs/tags/*)
    git rev-parse --verify -q "$1" &&
    deny >/dev/null "You can't overwrite an existing tag"
    ;;
  refs/heads/*)
    # No rebasing or rewinding
    if expr "$2" : '0*$' >/dev/null; then
      info "The branch '$1' is new..."
    else
      # updating -- make sure it is a fast forward
      mb=$(git-merge-base "$2" "$3")
      case "$mb,$2" in
        "$2,$mb") info "Update is fast-forward" ;;
	*)	  noff=y; info "This is not a fast-forward update.";;
      esac
    fi
    ;;
  *)
    deny >/dev/null \
    "Branch is not under refs/heads or refs/tags.  What are you trying to do?"
    ;;
esac

# Implement per-branch controls based on username
allowed_users_file=$GIT_DIR/info/allowed-users
username=$(id -u -n)
info "The user is: '$username'"

if test -f "$allowed_users_file"
then
  rc=$(cat $allowed_users_file | grep -v '^#' | grep -v '^$' |
    while read heads user_patterns
    do
      # does this rule apply to us?
      head_pattern=${heads#+}
      matchlen=$(expr "$1" : "${head_pattern#+}")
      test "$matchlen" = ${#1} || continue

      # if non-ff, $heads must be with the '+' prefix
      test -n "$noff" &&
      test "$head_pattern" = "$heads" && continue

      info "Found matching head pattern: '$head_pattern'"
      for user_pattern in $user_patterns; do
	info "Checking user: '$username' against pattern: '$user_pattern'"
	matchlen=$(expr "$username" : "$user_pattern")
	if test "$matchlen" = "${#username}"
	then
	  grant "Allowing user: '$username' with pattern: '$user_pattern'"
	fi
      done
      deny "The user is not in the access list for this branch"
    done
  )
  case "$rc" in
    grant) grant >/dev/null "Granting access based on $allowed_users_file" ;;
    deny)  deny  >/dev/null "Denying  access based on $allowed_users_file" ;;
    *) ;;
  esac
fi

allowed_groups_file=$GIT_DIR/info/allowed-groups
groups=$(id -G -n)
info "The user belongs to the following groups:"
info "'$groups'"

if test -f "$allowed_groups_file"
then
  rc=$(cat $allowed_groups_file | grep -v '^#' | grep -v '^$' |
    while read heads group_patterns
    do
      # does this rule apply to us?
      head_pattern=${heads#+}
      matchlen=$(expr "$1" : "${head_pattern#+}")
      test "$matchlen" = ${#1} || continue

      # if non-ff, $heads must be with the '+' prefix
      test -n "$noff" &&
      test "$head_pattern" = "$heads" && continue

      info "Found matching head pattern: '$head_pattern'"
      for group_pattern in $group_patterns; do
	for groupname in $groups; do
	  info "Checking group: '$groupname' against pattern: '$group_pattern'"
	  matchlen=$(expr "$groupname" : "$group_pattern")
	  if test "$matchlen" = "${#groupname}"
	  then
	    grant "Allowing group: '$groupname' with pattern: '$group_pattern'"
	  fi
        done
      done
      deny "None of the user's groups are in the access list for this branch"
    done
  )
  case "$rc" in
    grant) grant >/dev/null "Granting access based on $allowed_groups_file" ;;
    deny)  deny  >/dev/null "Denying  access based on $allowed_groups_file" ;;
    *) ;;
  esac
fi

deny >/dev/null "There are no more rules to check.  Denying access"
