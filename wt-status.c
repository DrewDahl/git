#include "cache.h"
#include "wt-status.h"
#include "color.h"
#include "object.h"
#include "dir.h"
#include "commit.h"
#include "diff.h"
#include "revision.h"
#include "diffcore.h"

int wt_status_relative_paths = 1;
int wt_status_use_color = 0;
static char wt_status_colors[][COLOR_MAXLEN] = {
	"",         /* WT_STATUS_HEADER: normal */
	"\033[32m", /* WT_STATUS_UPDATED: green */
	"\033[31m", /* WT_STATUS_CHANGED: red */
	"\033[31m", /* WT_STATUS_UNTRACKED: red */
};

static const char use_add_msg[] =
"use \"git add <file>...\" to update what will be committed";
static const char use_add_rm_msg[] =
"use \"git add/rm <file>...\" to update what will be committed";
static const char use_add_to_include_msg[] =
"use \"git add <file>...\" to include in what will be committed";

static int parse_status_slot(const char *var, int offset)
{
	if (!strcasecmp(var+offset, "header"))
		return WT_STATUS_HEADER;
	if (!strcasecmp(var+offset, "updated")
		|| !strcasecmp(var+offset, "added"))
		return WT_STATUS_UPDATED;
	if (!strcasecmp(var+offset, "changed"))
		return WT_STATUS_CHANGED;
	if (!strcasecmp(var+offset, "untracked"))
		return WT_STATUS_UNTRACKED;
	die("bad config variable '%s'", var);
}

static const char* color(int slot)
{
	return wt_status_use_color ? wt_status_colors[slot] : "";
}

void wt_status_prepare(struct wt_status *s)
{
	unsigned char sha1[20];
	const char *head;

	memset(s, 0, sizeof(*s));
	head = resolve_ref("HEAD", sha1, 0, NULL);
	s->branch = head ? xstrdup(head) : NULL;
	s->reference = "HEAD";
	s->fp = stdout;
	s->index_file = get_index_file();
}

static void wt_status_print_cached_header(struct wt_status *s)
{
	const char *c = color(WT_STATUS_HEADER);
	color_fprintf_ln(s->fp, c, "# Changes to be committed:");
	if (s->reference) {
		color_fprintf_ln(s->fp, c, "#   (use \"git reset %s <file>...\" to unstage)", s->reference);
	} else {
		color_fprintf_ln(s->fp, c, "#   (use \"git rm --cached <file>...\" to unstage)");
	}
	color_fprintf_ln(s->fp, c, "#");
}

static void wt_status_print_header(struct wt_status *s,
				   const char *main, const char *sub)
{
	const char *c = color(WT_STATUS_HEADER);
	color_fprintf_ln(s->fp, c, "# %s:", main);
	color_fprintf_ln(s->fp, c, "#   (%s)", sub);
	color_fprintf_ln(s->fp, c, "#");
}

static void wt_status_print_trailer(struct wt_status *s)
{
	color_fprintf_ln(s->fp, color(WT_STATUS_HEADER), "#");
}

static char *quote_path(const char *in, int len,
			struct strbuf *out, const char *prefix)
{
	if (len < 0)
		len = strlen(in);

	strbuf_grow(out, len);
	strbuf_setlen(out, 0);
	if (prefix) {
		int off = 0;
		while (prefix[off] && off < len && prefix[off] == in[off])
			if (prefix[off] == '/') {
				prefix += off + 1;
				in += off + 1;
				len -= off + 1;
				off = 0;
			} else
				off++;

		for (; *prefix; prefix++)
			if (*prefix == '/')
				strbuf_addstr(out, "../");
	}

	for ( ; len > 0; in++, len--) {
		int ch = *in;

		switch (ch) {
		case '\n':
			strbuf_addstr(out, "\\n");
			break;
		case '\r':
			strbuf_addstr(out, "\\r");
			break;
		default:
			strbuf_addch(out, ch);
			continue;
		}
	}

	if (!out->len)
		strbuf_addstr(out, "./");

	return out->buf;
}

static void wt_status_print_filepair(struct wt_status *s,
				     int t, struct diff_filepair *p)
{
	const char *c = color(t);
	const char *one, *two;
	struct strbuf onebuf, twobuf;

	strbuf_init(&onebuf, 0);
	strbuf_init(&twobuf, 0);
	one = quote_path(p->one->path, -1, &onebuf, s->prefix);
	two = quote_path(p->two->path, -1, &twobuf, s->prefix);

	color_fprintf(s->fp, color(WT_STATUS_HEADER), "#\t");
	switch (p->status) {
	case DIFF_STATUS_ADDED:
		color_fprintf(s->fp, c, "new file:   %s", one);
		break;
	case DIFF_STATUS_COPIED:
		color_fprintf(s->fp, c, "copied:     %s -> %s", one, two);
		break;
	case DIFF_STATUS_DELETED:
		color_fprintf(s->fp, c, "deleted:    %s", one);
		break;
	case DIFF_STATUS_MODIFIED:
		color_fprintf(s->fp, c, "modified:   %s", one);
		break;
	case DIFF_STATUS_RENAMED:
		color_fprintf(s->fp, c, "renamed:    %s -> %s", one, two);
		break;
	case DIFF_STATUS_TYPE_CHANGED:
		color_fprintf(s->fp, c, "typechange: %s", one);
		break;
	case DIFF_STATUS_UNKNOWN:
		color_fprintf(s->fp, c, "unknown:    %s", one);
		break;
	case DIFF_STATUS_UNMERGED:
		color_fprintf(s->fp, c, "unmerged:   %s", one);
		break;
	default:
		die("bug: unhandled diff status %c", p->status);
	}
	fprintf(s->fp, "\n");
	strbuf_release(&onebuf);
	strbuf_release(&twobuf);
}

static void wt_status_print_updated_cb(struct diff_queue_struct *q,
		struct diff_options *options,
		void *data)
{
	struct wt_status *s = data;
	int shown_header = 0;
	int i;
	for (i = 0; i < q->nr; i++) {
		if (q->queue[i]->status == 'U')
			continue;
		if (!shown_header) {
			wt_status_print_cached_header(s);
			s->commitable = 1;
			shown_header = 1;
		}
		wt_status_print_filepair(s, WT_STATUS_UPDATED, q->queue[i]);
	}
	if (shown_header)
		wt_status_print_trailer(s);
}

static void wt_status_print_changed_cb(struct diff_queue_struct *q,
                        struct diff_options *options,
                        void *data)
{
	struct wt_status *s = data;
	int i;
	if (q->nr) {
		const char *msg = use_add_msg;
		s->workdir_dirty = 1;
		for (i = 0; i < q->nr; i++)
			if (q->queue[i]->status == DIFF_STATUS_DELETED) {
				msg = use_add_rm_msg;
				break;
			}
		wt_status_print_header(s, "Changed but not updated", msg);
	}
	for (i = 0; i < q->nr; i++)
		wt_status_print_filepair(s, WT_STATUS_CHANGED, q->queue[i]);
	if (q->nr)
		wt_status_print_trailer(s);
}

static void wt_read_cache(struct wt_status *s)
{
	discard_cache();
	read_cache_from(s->index_file);
}

static void wt_status_print_initial(struct wt_status *s)
{
	int i;
	struct strbuf buf;

	strbuf_init(&buf, 0);
	wt_read_cache(s);
	if (active_nr) {
		s->commitable = 1;
		wt_status_print_cached_header(s);
	}
	for (i = 0; i < active_nr; i++) {
		color_fprintf(s->fp, color(WT_STATUS_HEADER), "#\t");
		color_fprintf_ln(s->fp, color(WT_STATUS_UPDATED), "new file: %s",
				quote_path(active_cache[i]->name, -1,
					   &buf, s->prefix));
	}
	if (active_nr)
		wt_status_print_trailer(s);
	strbuf_release(&buf);
}

static void wt_status_print_updated(struct wt_status *s)
{
	struct rev_info rev;
	init_revisions(&rev, NULL);
	setup_revisions(0, NULL, &rev, s->reference);
	rev.diffopt.output_format |= DIFF_FORMAT_CALLBACK;
	rev.diffopt.format_callback = wt_status_print_updated_cb;
	rev.diffopt.format_callback_data = s;
	rev.diffopt.detect_rename = 1;
	rev.diffopt.rename_limit = 100;
	rev.diffopt.break_opt = 0;
	wt_read_cache(s);
	run_diff_index(&rev, 1);
}

static void wt_status_print_changed(struct wt_status *s)
{
	struct rev_info rev;
	init_revisions(&rev, "");
	setup_revisions(0, NULL, &rev, NULL);
	rev.diffopt.output_format |= DIFF_FORMAT_CALLBACK;
	rev.diffopt.format_callback = wt_status_print_changed_cb;
	rev.diffopt.format_callback_data = s;
	wt_read_cache(s);
	run_diff_files(&rev, 0);
}

static void wt_status_print_untracked(struct wt_status *s)
{
	struct dir_struct dir;
	int i;
	int shown_header = 0;
	struct strbuf buf;

	strbuf_init(&buf, 0);
	memset(&dir, 0, sizeof(dir));

	if (!s->untracked) {
		dir.show_other_directories = 1;
		dir.hide_empty_directories = 1;
	}
	setup_standard_excludes(&dir);

	read_directory(&dir, ".", "", 0, NULL);
	for(i = 0; i < dir.nr; i++) {
		/* check for matching entry, which is unmerged; lifted from
		 * builtin-ls-files:show_other_files */
		struct dir_entry *ent = dir.entries[i];
		int pos = cache_name_pos(ent->name, ent->len);
		struct cache_entry *ce;
		if (0 <= pos)
			die("bug in wt_status_print_untracked");
		pos = -pos - 1;
		if (pos < active_nr) {
			ce = active_cache[pos];
			if (ce_namelen(ce) == ent->len &&
			    !memcmp(ce->name, ent->name, ent->len))
				continue;
		}
		if (!shown_header) {
			s->workdir_untracked = 1;
			wt_status_print_header(s, "Untracked files",
					       use_add_to_include_msg);
			shown_header = 1;
		}
		color_fprintf(s->fp, color(WT_STATUS_HEADER), "#\t");
		color_fprintf_ln(s->fp, color(WT_STATUS_UNTRACKED), "%s",
				quote_path(ent->name, ent->len,
					&buf, s->prefix));
	}
	strbuf_release(&buf);
}

static void wt_status_print_verbose(struct wt_status *s)
{
	struct rev_info rev;
	int saved_stdout;

	fflush(s->fp);

	/* Sigh, the entire diff machinery is hardcoded to output to
	 * stdout.  Do the dup-dance...*/
	saved_stdout = dup(STDOUT_FILENO);
	if (saved_stdout < 0 ||dup2(fileno(s->fp), STDOUT_FILENO) < 0)
		die("couldn't redirect stdout\n");

	init_revisions(&rev, NULL);
	setup_revisions(0, NULL, &rev, s->reference);
	rev.diffopt.output_format |= DIFF_FORMAT_PATCH;
	rev.diffopt.detect_rename = 1;
	wt_read_cache(s);
	run_diff_index(&rev, 1);

	fflush(stdout);

	if (dup2(saved_stdout, STDOUT_FILENO) < 0)
		die("couldn't restore stdout\n");
	close(saved_stdout);
}

void wt_status_print(struct wt_status *s)
{
	unsigned char sha1[20];
	s->is_initial = get_sha1(s->reference, sha1) ? 1 : 0;

	if (s->branch) {
		const char *on_what = "On branch ";
		const char *branch_name = s->branch;
		if (!prefixcmp(branch_name, "refs/heads/"))
			branch_name += 11;
		else if (!strcmp(branch_name, "HEAD")) {
			branch_name = "";
			on_what = "Not currently on any branch.";
		}
		color_fprintf_ln(s->fp, color(WT_STATUS_HEADER),
			"# %s%s", on_what, branch_name);
	}

	if (s->is_initial) {
		color_fprintf_ln(s->fp, color(WT_STATUS_HEADER), "#");
		color_fprintf_ln(s->fp, color(WT_STATUS_HEADER), "# Initial commit");
		color_fprintf_ln(s->fp, color(WT_STATUS_HEADER), "#");
		wt_status_print_initial(s);
	}
	else {
		wt_status_print_updated(s);
	}

	wt_status_print_changed(s);
	wt_status_print_untracked(s);

	if (s->verbose && !s->is_initial)
		wt_status_print_verbose(s);
	if (!s->commitable) {
		if (s->amend)
			fprintf(s->fp, "# No changes\n");
		else if (s->nowarn)
			; /* nothing */
		else if (s->workdir_dirty)
			printf("no changes added to commit (use \"git add\" and/or \"git commit -a\")\n");
		else if (s->workdir_untracked)
			printf("nothing added to commit but untracked files present (use \"git add\" to track)\n");
		else if (s->is_initial)
			printf("nothing to commit (create/copy files and use \"git add\" to track)\n");
		else
			printf("nothing to commit (working directory clean)\n");
	}
}

int git_status_config(const char *k, const char *v)
{
	if (!strcmp(k, "status.color") || !strcmp(k, "color.status")) {
		wt_status_use_color = git_config_colorbool(k, v, -1);
		return 0;
	}
	if (!prefixcmp(k, "status.color.") || !prefixcmp(k, "color.status.")) {
		int slot = parse_status_slot(k, 13);
		color_parse(v, k, wt_status_colors[slot]);
		return 0;
	}
	if (!strcmp(k, "status.relativepaths")) {
		wt_status_relative_paths = git_config_bool(k, v);
		return 0;
	}
	return git_default_config(k, v);
}