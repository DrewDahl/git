#!/bin/sh
#
# Copyright (c) 2006 Johannes E. Schindelin

# SHORT DESCRIPTION
#
# This script makes it easy to fix up commits in the middle of a series,
# and rearrange commits.
#
# The original idea comes from Eric W. Biederman, in
# http://article.gmane.org/gmane.comp.version-control.git/22407

OPTIONS_KEEPDASHDASH=
OPTIONS_SPEC="\
git-rebase [-i] [options] [--] <upstream> [<branch>]
git-rebase [-i] (--continue | --abort | --skip)
--
 Available options are
v,verbose          display a diffstat of what changed upstream
onto=              rebase onto given branch instead of upstream
p,preserve-merges  try to recreate merges instead of ignoring them
s,strategy=        use the given merge strategy
m,merge            always used (no-op)
i,interactive      always used (no-op)
 Actions:
continue           continue rebasing process
abort              abort rebasing process and restore original branch
skip               skip current patch and continue rebasing process
no-verify          override pre-rebase hook from stopping the operation
root               rebase all reachable commmits up to the root(s)
"

. git-sh-setup
require_work_tree

DOTEST="$GIT_DIR/rebase-merge"
TODO="$DOTEST"/git-rebase-todo
DONE="$DOTEST"/done
MSG="$DOTEST"/message
SQUASH_MSG="$DOTEST"/message-squash
REWRITTEN="$DOTEST"/rewritten
DROPPED="$DOTEST"/dropped
PRESERVE_MERGES=
STRATEGY=
ONTO=
VERBOSE=
OK_TO_SKIP_PRE_REBASE=
REBASE_ROOT=

GIT_CHERRY_PICK_HELP="  After resolving the conflicts,
mark the corrected paths with 'git add <paths>', and
run 'git rebase --continue'"
export GIT_CHERRY_PICK_HELP

warn () {
	echo "$*" >&2
}

output () {
	case "$VERBOSE" in
	'')
		output=$("$@" 2>&1 )
		status=$?
		test $status != 0 && printf "%s\n" "$output"
		return $status
		;;
	*)
		"$@"
		;;
	esac
}

run_pre_rebase_hook () {
	if test -z "$OK_TO_SKIP_PRE_REBASE" &&
	   test -x "$GIT_DIR/hooks/pre-rebase"
	then
		"$GIT_DIR/hooks/pre-rebase" ${1+"$@"} || {
			echo >&2 "The pre-rebase hook refused to rebase."
			exit 1
		}
	fi
}

require_clean_work_tree () {
	# test if working tree is dirty
	git rev-parse --verify HEAD > /dev/null &&
	git update-index --ignore-submodules --refresh &&
	git diff-files --quiet --ignore-submodules &&
	git diff-index --cached --quiet HEAD --ignore-submodules -- ||
	die "Working tree is dirty"
}

ORIG_REFLOG_ACTION="$GIT_REFLOG_ACTION"

comment_for_reflog () {
	case "$ORIG_REFLOG_ACTION" in
	''|rebase*)
		GIT_REFLOG_ACTION="rebase -i ($1)"
		export GIT_REFLOG_ACTION
		;;
	esac
}

last_count=
mark_action_done () {
	sed -e 1q < "$TODO" >> "$DONE"
	sed -e 1d < "$TODO" >> "$TODO".new
	mv -f "$TODO".new "$TODO"
	count=$(grep -c '^[^#]' < "$DONE")
	total=$(($count+$(grep -c '^[^#]' < "$TODO")))
	if test "$last_count" != "$count"
	then
		last_count=$count
		printf "Rebasing (%d/%d)\r" $count $total
		test -z "$VERBOSE" || echo
	fi
}

make_patch () {
	sha1_and_parents="$(git rev-list --parents -1 "$1")"
	case "$sha1_and_parents" in
	?*' '?*' '?*)
		git diff --cc $sha1_and_parents
		;;
	?*' '?*)
		git diff-tree -p "$1^!"
		;;
	*)
		echo "Root commit"
		;;
	esac > "$DOTEST"/patch
	test -f "$DOTEST"/message ||
		git cat-file commit "$1" | sed "1,/^$/d" > "$DOTEST"/message
	test -f "$DOTEST"/author-script ||
		get_author_ident_from_commit "$1" > "$DOTEST"/author-script
}

die_with_patch () {
	make_patch "$1"
	git rerere
	die "$2"
}

die_abort () {
	rm -rf "$DOTEST"
	die "$1"
}

has_action () {
	grep '^[^#]' "$1" >/dev/null
}

pick_one () {
	no_ff=
	case "$1" in -n) sha1=$2; no_ff=t ;; *) sha1=$1 ;; esac
	output git rev-parse --verify $sha1 || die "Invalid commit name: $sha1"
	test -d "$REWRITTEN" &&
		pick_one_preserving_merges "$@" && return
	if test ! -z "$REBASE_ROOT"
	then
		output git cherry-pick "$@"
		return
	fi
	parent_sha1=$(git rev-parse --verify $sha1^) ||
		die "Could not get the parent of $sha1"
	current_sha1=$(git rev-parse --verify HEAD)
	if test "$no_ff$current_sha1" = "$parent_sha1"; then
		output git reset --hard $sha1
		test "a$1" = a-n && output git reset --soft $current_sha1
		sha1=$(git rev-parse --short $sha1)
		output warn Fast forward to $sha1
	else
		output git cherry-pick "$@"
	fi
}

pick_one_preserving_merges () {
	fast_forward=t
	case "$1" in
	-n)
		fast_forward=f
		sha1=$2
		;;
	*)
		sha1=$1
		;;
	esac
	sha1=$(git rev-parse $sha1)

	if test -f "$DOTEST"/current-commit
	then
		if test "$fast_forward" = t
		then
			cat "$DOTEST"/current-commit | while read current_commit
			do
				git rev-parse HEAD > "$REWRITTEN"/$current_commit
			done
			rm "$DOTEST"/current-commit ||
			die "Cannot write current commit's replacement sha1"
		fi
	fi

	echo $sha1 >> "$DOTEST"/current-commit

	# rewrite parents; if none were rewritten, we can fast-forward.
	new_parents=
	pend=" $(git rev-list --parents -1 $sha1 | cut -d' ' -s -f2-)"
	if test "$pend" = " "
	then
		pend=" root"
	fi
	while [ "$pend" != "" ]
	do
		p=$(expr "$pend" : ' \([^ ]*\)')
		pend="${pend# $p}"

		if test -f "$REWRITTEN"/$p
		then
			new_p=$(cat "$REWRITTEN"/$p)

			# If the todo reordered commits, and our parent is marked for
			# rewriting, but hasn't been gotten to yet, assume the user meant to
			# drop it on top of the current HEAD
			if test -z "$new_p"
			then
				new_p=$(git rev-parse HEAD)
			fi

			test $p != $new_p && fast_forward=f
			case "$new_parents" in
			*$new_p*)
				;; # do nothing; that parent is already there
			*)
				new_parents="$new_parents $new_p"
				;;
			esac
		else
			if test -f "$DROPPED"/$p
			then
				fast_forward=f
				replacement="$(cat "$DROPPED"/$p)"
				test -z "$replacement" && replacement=root
				pend=" $replacement$pend"
			else
				new_parents="$new_parents $p"
			fi
		fi
	done
	case $fast_forward in
	t)
		output warn "Fast forward to $sha1"
		output git reset --hard $sha1 ||
			die "Cannot fast forward to $sha1"
		;;
	f)
		first_parent=$(expr "$new_parents" : ' \([^ ]*\)')

		if [ "$1" != "-n" ]
		then
			# detach HEAD to current parent
			output git checkout $first_parent 2> /dev/null ||
				die "Cannot move HEAD to $first_parent"
		fi

		case "$new_parents" in
		' '*' '*)
			test "a$1" = a-n && die "Refusing to squash a merge: $sha1"

			# redo merge
			author_script=$(get_author_ident_from_commit $sha1)
			eval "$author_script"
			msg="$(git cat-file commit $sha1 | sed -e '1,/^$/d')"
			# No point in merging the first parent, that's HEAD
			new_parents=${new_parents# $first_parent}
			if ! GIT_AUTHOR_NAME="$GIT_AUTHOR_NAME" \
				GIT_AUTHOR_EMAIL="$GIT_AUTHOR_EMAIL" \
				GIT_AUTHOR_DATE="$GIT_AUTHOR_DATE" \
				output git merge $STRATEGY -m "$msg" \
					$new_parents
			then
				printf "%s\n" "$msg" > "$GIT_DIR"/MERGE_MSG
				die_with_patch $sha1 "Error redoing merge $sha1"
			fi
			;;
		*)
			output git cherry-pick "$@" ||
				die_with_patch $sha1 "Could not pick $sha1"
			;;
		esac
		;;
	esac
}

nth_string () {
	case "$1" in
	*1[0-9]|*[04-9]) echo "$1"th;;
	*1) echo "$1"st;;
	*2) echo "$1"nd;;
	*3) echo "$1"rd;;
	esac
}

make_squash_message () {
	if test -f "$SQUASH_MSG"; then
		COUNT=$(($(sed -n "s/^# This is [^0-9]*\([1-9][0-9]*\).*/\1/p" \
			< "$SQUASH_MSG" | sed -ne '$p')+1))
		echo "# This is a combination of $COUNT commits."
		sed -e 1d -e '2,/^./{
			/^$/d
		}' <"$SQUASH_MSG"
	else
		COUNT=2
		echo "# This is a combination of two commits."
		echo "# The first commit's message is:"
		echo
		git cat-file commit HEAD | sed -e '1,/^$/d'
	fi
	echo
	echo "# This is the $(nth_string $COUNT) commit message:"
	echo
	git cat-file commit $1 | sed -e '1,/^$/d'
}

peek_next_command () {
	sed -n "1s/ .*$//p" < "$TODO"
}

do_next () {
	rm -f "$DOTEST"/message "$DOTEST"/author-script \
		"$DOTEST"/amend || exit
	read command sha1 rest < "$TODO"
	case "$command" in
	'#'*|''|noop)
		mark_action_done
		;;
	pick|p)
		comment_for_reflog pick

		mark_action_done
		pick_one $sha1 ||
			die_with_patch $sha1 "Could not apply $sha1... $rest"
		;;
	edit|e)
		comment_for_reflog edit

		mark_action_done
		pick_one $sha1 ||
			die_with_patch $sha1 "Could not apply $sha1... $rest"
		make_patch $sha1
		git rev-parse --verify HEAD > "$DOTEST"/amend
		warn "Stopped at $sha1... $rest"
		warn "You can amend the commit now, with"
		warn
		warn "	git commit --amend"
		warn
		warn "Once you are satisfied with your changes, run"
		warn
		warn "	git rebase --continue"
		warn
		exit 0
		;;
	squash|s)
		comment_for_reflog squash

		test -f "$DONE" && has_action "$DONE" ||
			die "Cannot 'squash' without a previous commit"

		mark_action_done
		make_squash_message $sha1 > "$MSG"
		failed=f
		author_script=$(get_author_ident_from_commit HEAD)
		output git reset --soft HEAD^
		pick_one -n $sha1 || failed=t
		case "$(peek_next_command)" in
		squash|s)
			USE_OUTPUT=output
			MSG_OPT=-F
			EDIT_OR_FILE="$MSG"
			cp "$MSG" "$SQUASH_MSG"
			;;
		*)
			USE_OUTPUT=
			MSG_OPT=
			EDIT_OR_FILE=-e
			rm -f "$SQUASH_MSG" || exit
			cp "$MSG" "$GIT_DIR"/SQUASH_MSG
			rm -f "$GIT_DIR"/MERGE_MSG || exit
			;;
		esac
		echo "$author_script" > "$DOTEST"/author-script
		if test $failed = f
		then
			# This is like --amend, but with a different message
			eval "$author_script"
			GIT_AUTHOR_NAME="$GIT_AUTHOR_NAME" \
			GIT_AUTHOR_EMAIL="$GIT_AUTHOR_EMAIL" \
			GIT_AUTHOR_DATE="$GIT_AUTHOR_DATE" \
			$USE_OUTPUT git commit --no-verify \
				$MSG_OPT "$EDIT_OR_FILE" || failed=t
		fi
		if test $failed = t
		then
			cp "$MSG" "$GIT_DIR"/MERGE_MSG
			warn
			warn "Could not apply $sha1... $rest"
			die_with_patch $sha1 ""
		fi
		;;
	*)
		warn "Unknown command: $command $sha1 $rest"
		die_with_patch $sha1 "Please fix this in the file $TODO."
		;;
	esac
	test -s "$TODO" && return

	comment_for_reflog finish &&
	HEADNAME=$(cat "$DOTEST"/head-name) &&
	OLDHEAD=$(cat "$DOTEST"/head) &&
	SHORTONTO=$(git rev-parse --short $(cat "$DOTEST"/onto)) &&
	NEWHEAD=$(git rev-parse HEAD) &&
	case $HEADNAME in
	refs/*)
		message="$GIT_REFLOG_ACTION: $HEADNAME onto $SHORTONTO)" &&
		git update-ref -m "$message" $HEADNAME $NEWHEAD $OLDHEAD &&
		git symbolic-ref HEAD $HEADNAME
		;;
	esac && {
		test ! -f "$DOTEST"/verbose ||
			git diff-tree --stat $(cat "$DOTEST"/head)..HEAD
	} &&
	rm -rf "$DOTEST" &&
	git gc --auto &&
	warn "Successfully rebased and updated $HEADNAME."

	exit
}

do_rest () {
	while :
	do
		do_next
	done
}

# skip picking commits whose parents are unchanged
skip_unnecessary_picks () {
	fd=3
	while read command sha1 rest
	do
		# fd=3 means we skip the command
		case "$fd,$command,$(git rev-parse --verify --quiet $sha1^)" in
		3,pick,"$ONTO"*|3,p,"$ONTO"*)
			# pick a commit whose parent is current $ONTO -> skip
			ONTO=$sha1
			;;
		3,#*|3,,*)
			# copy comments
			;;
		*)
			fd=1
			;;
		esac
		echo "$command${sha1:+ }$sha1${rest:+ }$rest" >&$fd
	done <"$TODO" >"$TODO.new" 3>>"$DONE" &&
	mv -f "$TODO".new "$TODO" ||
	die "Could not skip unnecessary pick commands"
}

# check if no other options are set
is_standalone () {
	test $# -eq 2 -a "$2" = '--' &&
	test -z "$ONTO" &&
	test -z "$PRESERVE_MERGES" &&
	test -z "$STRATEGY" &&
	test -z "$VERBOSE"
}

get_saved_options () {
	test -d "$REWRITTEN" && PRESERVE_MERGES=t
	test -f "$DOTEST"/strategy && STRATEGY="$(cat "$DOTEST"/strategy)"
	test -f "$DOTEST"/verbose && VERBOSE=t
	test -f "$DOTEST"/rebase-root && REBASE_ROOT=t
}

while test $# != 0
do
	case "$1" in
	--no-verify)
		OK_TO_SKIP_PRE_REBASE=yes
		;;
	--verify)
		;;
	--continue)
		is_standalone "$@" || usage
		get_saved_options
		comment_for_reflog continue

		test -d "$DOTEST" || die "No interactive rebase running"

		# Sanity check
		git rev-parse --verify HEAD >/dev/null ||
			die "Cannot read HEAD"
		git update-index --ignore-submodules --refresh &&
			git diff-files --quiet --ignore-submodules ||
			die "Working tree is dirty"

		# do we have anything to commit?
		if git diff-index --cached --quiet --ignore-submodules HEAD --
		then
			: Nothing to commit -- skip this
		else
			. "$DOTEST"/author-script ||
				die "Cannot find the author identity"
			amend=
			if test -f "$DOTEST"/amend
			then
				amend=$(git rev-parse --verify HEAD)
				test "$amend" = $(cat "$DOTEST"/amend) ||
				die "\
You have uncommitted changes in your working tree. Please, commit them
first and then run 'git rebase --continue' again."
				git reset --soft HEAD^ ||
				die "Cannot rewind the HEAD"
			fi
			export GIT_AUTHOR_NAME GIT_AUTHOR_EMAIL GIT_AUTHOR_DATE &&
			git commit --no-verify -F "$DOTEST"/message -e || {
				test -n "$amend" && git reset --soft $amend
				die "Could not commit staged changes."
			}
		fi

		require_clean_work_tree
		do_rest
		;;
	--abort)
		is_standalone "$@" || usage
		get_saved_options
		comment_for_reflog abort

		git rerere clear
		test -d "$DOTEST" || die "No interactive rebase running"

		HEADNAME=$(cat "$DOTEST"/head-name)
		HEAD=$(cat "$DOTEST"/head)
		case $HEADNAME in
		refs/*)
			git symbolic-ref HEAD $HEADNAME
			;;
		esac &&
		output git reset --hard $HEAD &&
		rm -rf "$DOTEST"
		exit
		;;
	--skip)
		is_standalone "$@" || usage
		get_saved_options
		comment_for_reflog skip

		git rerere clear
		test -d "$DOTEST" || die "No interactive rebase running"

		output git reset --hard && do_rest
		;;
	-s)
		case "$#,$1" in
		*,*=*)
			STRATEGY="-s "$(expr "z$1" : 'z-[^=]*=\(.*\)') ;;
		1,*)
			usage ;;
		*)
			STRATEGY="-s $2"
			shift ;;
		esac
		;;
	-m)
		# we use merge anyway
		;;
	-v)
		VERBOSE=t
		;;
	-p)
		PRESERVE_MERGES=t
		;;
	-i)
		# yeah, we know
		;;
	--root)
		REBASE_ROOT=t
		;;
	--onto)
		shift
		ONTO=$(git rev-parse --verify "$1") ||
			die "Does not point to a valid commit: $1"
		;;
	--)
		shift
		test -z "$REBASE_ROOT" -a $# -ge 1 -a $# -le 2 ||
		test ! -z "$REBASE_ROOT" -a $# -le 1 || usage
		test -d "$DOTEST" &&
			die "Interactive rebase already started"

		git var GIT_COMMITTER_IDENT >/dev/null ||
			die "You need to set your committer info first"

		if test -z "$REBASE_ROOT"
		then
			UPSTREAM_ARG="$1"
			UPSTREAM=$(git rev-parse --verify "$1") || die "Invalid base"
			test -z "$ONTO" && ONTO=$UPSTREAM
			shift
		else
			UPSTREAM=
			UPSTREAM_ARG=--root
			test -z "$ONTO" &&
				die "You must specify --onto when using --root"
		fi
		run_pre_rebase_hook "$UPSTREAM_ARG" "$@"

		comment_for_reflog start

		require_clean_work_tree

		if test ! -z "$1"
		then
			output git show-ref --verify --quiet "refs/heads/$1" ||
				die "Invalid branchname: $1"
			output git checkout "$1" ||
				die "Could not checkout $1"
		fi

		HEAD=$(git rev-parse --verify HEAD) || die "No HEAD?"
		mkdir "$DOTEST" || die "Could not create temporary $DOTEST"

		: > "$DOTEST"/interactive || die "Could not mark as interactive"
		git symbolic-ref HEAD > "$DOTEST"/head-name 2> /dev/null ||
			echo "detached HEAD" > "$DOTEST"/head-name

		echo $HEAD > "$DOTEST"/head
		case "$REBASE_ROOT" in
		'')
			rm -f "$DOTEST"/rebase-root ;;
		*)
			: >"$DOTEST"/rebase-root ;;
		esac
		echo $ONTO > "$DOTEST"/onto
		test -z "$STRATEGY" || echo "$STRATEGY" > "$DOTEST"/strategy
		test t = "$VERBOSE" && : > "$DOTEST"/verbose
		if test t = "$PRESERVE_MERGES"
		then
			# $REWRITTEN contains files for each commit that is
			# reachable by at least one merge base of $HEAD and
			# $UPSTREAM. They are not necessarily rewritten, but
			# their children might be.
			# This ensures that commits on merged, but otherwise
			# unrelated side branches are left alone. (Think "X"
			# in the man page's example.)
			if test -z "$REBASE_ROOT"
			then
				mkdir "$REWRITTEN" &&
				for c in $(git merge-base --all $HEAD $UPSTREAM)
				do
					echo $ONTO > "$REWRITTEN"/$c ||
						die "Could not init rewritten commits"
				done
			else
				mkdir "$REWRITTEN" &&
				echo $ONTO > "$REWRITTEN"/root ||
					die "Could not init rewritten commits"
			fi
			# No cherry-pick because our first pass is to determine
			# parents to rewrite and skipping dropped commits would
			# prematurely end our probe
			MERGES_OPTION=
			first_after_upstream="$(git rev-list --reverse --first-parent $UPSTREAM..$HEAD | head -n 1)"
		else
			MERGES_OPTION="--no-merges --cherry-pick"
		fi

		SHORTHEAD=$(git rev-parse --short $HEAD)
		SHORTONTO=$(git rev-parse --short $ONTO)
		if test -z "$REBASE_ROOT"
			# this is now equivalent to ! -z "$UPSTREAM"
		then
			SHORTUPSTREAM=$(git rev-parse --short $UPSTREAM)
			REVISIONS=$UPSTREAM...$HEAD
			SHORTREVISIONS=$SHORTUPSTREAM..$SHORTHEAD
		else
			REVISIONS=$ONTO...$HEAD
			SHORTREVISIONS=$SHORTHEAD
		fi
		git rev-list $MERGES_OPTION --pretty=oneline --abbrev-commit \
			--abbrev=7 --reverse --left-right --topo-order \
			$REVISIONS | \
			sed -n "s/^>//p" | while read shortsha1 rest
		do
			if test t != "$PRESERVE_MERGES"
			then
				echo "pick $shortsha1 $rest" >> "$TODO"
			else
				sha1=$(git rev-parse $shortsha1)
				if test -z "$REBASE_ROOT"
				then
					preserve=t
					for p in $(git rev-list --parents -1 $sha1 | cut -d' ' -s -f2-)
					do
						if test -f "$REWRITTEN"/$p -a \( $p != $UPSTREAM -o $sha1 = $first_after_upstream \)
						then
							preserve=f
						fi
					done
				else
					preserve=f
				fi
				if test f = "$preserve"
				then
					touch "$REWRITTEN"/$sha1
					echo "pick $shortsha1 $rest" >> "$TODO"
				fi
			fi
		done

		# Watch for commits that been dropped by --cherry-pick
		if test t = "$PRESERVE_MERGES"
		then
			mkdir "$DROPPED"
			# Save all non-cherry-picked changes
			git rev-list $REVISIONS --left-right --cherry-pick | \
				sed -n "s/^>//p" > "$DOTEST"/not-cherry-picks
			# Now all commits and note which ones are missing in
			# not-cherry-picks and hence being dropped
			git rev-list $REVISIONS |
			while read rev
			do
				if test -f "$REWRITTEN"/$rev -a "$(grep "$rev" "$DOTEST"/not-cherry-picks)" = ""
				then
					# Use -f2 because if rev-list is telling us this commit is
					# not worthwhile, we don't want to track its multiple heads,
					# just the history of its first-parent for others that will
					# be rebasing on top of it
					git rev-list --parents -1 $rev | cut -d' ' -s -f2 > "$DROPPED"/$rev
					short=$(git rev-list -1 --abbrev-commit --abbrev=7 $rev)
					grep -v "^[a-z][a-z]* $short" <"$TODO" > "${TODO}2" ; mv "${TODO}2" "$TODO"
					rm "$REWRITTEN"/$rev
				fi
			done
		fi

		test -s "$TODO" || echo noop >> "$TODO"
		cat >> "$TODO" << EOF

# Rebase $SHORTREVISIONS onto $SHORTONTO
#
# Commands:
#  p, pick = use commit
#  e, edit = use commit, but stop for amending
#  s, squash = use commit, but meld into previous commit
#
# If you remove a line here THAT COMMIT WILL BE LOST.
# However, if you remove everything, the rebase will be aborted.
#
EOF

		has_action "$TODO" ||
			die_abort "Nothing to do"

		cp "$TODO" "$TODO".backup
		git_editor "$TODO" ||
			die "Could not execute editor"

		has_action "$TODO" ||
			die_abort "Nothing to do"

		test -d "$REWRITTEN" || skip_unnecessary_picks

		git update-ref ORIG_HEAD $HEAD
		output git checkout $ONTO && do_rest
		;;
	esac
	shift
done
