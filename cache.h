#ifndef CACHE_H
#define CACHE_H

#include "git-compat-util.h"
#include "strbuf.h"

#include SHA1_HEADER
#include <zlib.h>

#if defined(NO_DEFLATE_BOUND) || ZLIB_VERNUM < 0x1200
#define deflateBound(c,s)  ((s) + (((s) + 7) >> 3) + (((s) + 63) >> 6) + 11)
#endif

#if defined(DT_UNKNOWN) && !defined(NO_D_TYPE_IN_DIRENT)
#define DTYPE(de)	((de)->d_type)
#else
#undef DT_UNKNOWN
#undef DT_DIR
#undef DT_REG
#undef DT_LNK
#define DT_UNKNOWN	0
#define DT_DIR		1
#define DT_REG		2
#define DT_LNK		3
#define DTYPE(de)	DT_UNKNOWN
#endif

/* unknown mode (impossible combination S_IFIFO|S_IFCHR) */
#define S_IFINVALID     0030000

/*
 * A "directory link" is a link to another git directory.
 *
 * The value 0160000 is not normally a valid mode, and
 * also just happens to be S_IFDIR + S_IFLNK
 *
 * NOTE! We *really* shouldn't depend on the S_IFxxx macros
 * always having the same values everywhere. We should use
 * our internal git values for these things, and then we can
 * translate that to the OS-specific value. It just so
 * happens that everybody shares the same bit representation
 * in the UNIX world (and apparently wider too..)
 */
#define S_IFGITLINK	0160000
#define S_ISGITLINK(m)	(((m) & S_IFMT) == S_IFGITLINK)

/*
 * Intensive research over the course of many years has shown that
 * port 9418 is totally unused by anything else. Or
 *
 *	Your search - "port 9418" - did not match any documents.
 *
 * as www.google.com puts it.
 *
 * This port has been properly assigned for git use by IANA:
 * git (Assigned-9418) [I06-050728-0001].
 *
 *	git  9418/tcp   git pack transfer service
 *	git  9418/udp   git pack transfer service
 *
 * with Linus Torvalds <torvalds@osdl.org> as the point of
 * contact. September 2005.
 *
 * See http://www.iana.org/assignments/port-numbers
 */
#define DEFAULT_GIT_PORT 9418

/*
 * Basic data structures for the directory cache
 */

#define CACHE_SIGNATURE 0x44495243	/* "DIRC" */
struct cache_header {
	unsigned int hdr_signature;
	unsigned int hdr_version;
	unsigned int hdr_entries;
};

/*
 * The "cache_time" is just the low 32 bits of the
 * time. It doesn't matter if it overflows - we only
 * check it for equality in the 32 bits we save.
 */
struct cache_time {
	unsigned int sec;
	unsigned int nsec;
};

/*
 * dev/ino/uid/gid/size are also just tracked to the low 32 bits
 * Again - this is just a (very strong in practice) heuristic that
 * the inode hasn't changed.
 *
 * We save the fields in big-endian order to allow using the
 * index file over NFS transparently.
 */
struct cache_entry {
	struct cache_time ce_ctime;
	struct cache_time ce_mtime;
	unsigned int ce_dev;
	unsigned int ce_ino;
	unsigned int ce_mode;
	unsigned int ce_uid;
	unsigned int ce_gid;
	unsigned int ce_size;
	unsigned char sha1[20];
	unsigned short ce_flags;
	char name[FLEX_ARRAY]; /* more */
};

#define CE_NAMEMASK  (0x0fff)
#define CE_STAGEMASK (0x3000)
#define CE_UPDATE    (0x4000)
#define CE_VALID     (0x8000)
#define CE_STAGESHIFT 12

#define create_ce_flags(len, stage) htons((len) | ((stage) << CE_STAGESHIFT))
#define ce_namelen(ce) (CE_NAMEMASK & ntohs((ce)->ce_flags))
#define ce_size(ce) cache_entry_size(ce_namelen(ce))
#define ce_stage(ce) ((CE_STAGEMASK & ntohs((ce)->ce_flags)) >> CE_STAGESHIFT)

#define ce_permissions(mode) (((mode) & 0100) ? 0755 : 0644)
static inline unsigned int create_ce_mode(unsigned int mode)
{
	if (S_ISLNK(mode))
		return htonl(S_IFLNK);
	if (S_ISDIR(mode) || S_ISGITLINK(mode))
		return htonl(S_IFGITLINK);
	return htonl(S_IFREG | ce_permissions(mode));
}
static inline unsigned int ce_mode_from_stat(struct cache_entry *ce, unsigned int mode)
{
	extern int trust_executable_bit, has_symlinks;
	if (!has_symlinks && S_ISREG(mode) &&
	    ce && S_ISLNK(ntohl(ce->ce_mode)))
		return ce->ce_mode;
	if (!trust_executable_bit && S_ISREG(mode)) {
		if (ce && S_ISREG(ntohl(ce->ce_mode)))
			return ce->ce_mode;
		return create_ce_mode(0666);
	}
	return create_ce_mode(mode);
}
#define canon_mode(mode) \
	(S_ISREG(mode) ? (S_IFREG | ce_permissions(mode)) : \
	S_ISLNK(mode) ? S_IFLNK : S_ISDIR(mode) ? S_IFDIR : S_IFGITLINK)

#define cache_entry_size(len) ((offsetof(struct cache_entry,name) + (len) + 8) & ~7)

struct index_state {
	struct cache_entry **cache;
	unsigned int cache_nr, cache_alloc, cache_changed;
	struct cache_tree *cache_tree;
	time_t timestamp;
	void *mmap;
	size_t mmap_size;
};

extern struct index_state the_index;

#ifndef NO_THE_INDEX_COMPATIBILITY_MACROS
#define active_cache (the_index.cache)
#define active_nr (the_index.cache_nr)
#define active_alloc (the_index.cache_alloc)
#define active_cache_changed (the_index.cache_changed)
#define active_cache_tree (the_index.cache_tree)

#define read_cache() read_index(&the_index)
#define read_cache_from(path) read_index_from(&the_index, (path))
#define write_cache(newfd, cache, entries) write_index(&the_index, (newfd))
#define discard_cache() discard_index(&the_index)
#define cache_name_pos(name, namelen) index_name_pos(&the_index,(name),(namelen))
#define add_cache_entry(ce, option) add_index_entry(&the_index, (ce), (option))
#define remove_cache_entry_at(pos) remove_index_entry_at(&the_index, (pos))
#define remove_file_from_cache(path) remove_file_from_index(&the_index, (path))
#define add_file_to_cache(path, verbose) add_file_to_index(&the_index, (path), (verbose))
#define refresh_cache(flags) refresh_index(&the_index, (flags), NULL, NULL)
#define ce_match_stat(ce, st, options) ie_match_stat(&the_index, (ce), (st), (options))
#define ce_modified(ce, st, options) ie_modified(&the_index, (ce), (st), (options))
#endif

enum object_type {
	OBJ_BAD = -1,
	OBJ_NONE = 0,
	OBJ_COMMIT = 1,
	OBJ_TREE = 2,
	OBJ_BLOB = 3,
	OBJ_TAG = 4,
	/* 5 for future expansion */
	OBJ_OFS_DELTA = 6,
	OBJ_REF_DELTA = 7,
	OBJ_MAX,
};

static inline enum object_type object_type(unsigned int mode)
{
	return S_ISDIR(mode) ? OBJ_TREE :
		S_ISGITLINK(mode) ? OBJ_COMMIT :
		OBJ_BLOB;
}

#define GIT_DIR_ENVIRONMENT "GIT_DIR"
#define GIT_WORK_TREE_ENVIRONMENT "GIT_WORK_TREE"
#define DEFAULT_GIT_DIR_ENVIRONMENT ".git"
#define DB_ENVIRONMENT "GIT_OBJECT_DIRECTORY"
#define INDEX_ENVIRONMENT "GIT_INDEX_FILE"
#define GRAFT_ENVIRONMENT "GIT_GRAFT_FILE"
#define TEMPLATE_DIR_ENVIRONMENT "GIT_TEMPLATE_DIR"
#define CONFIG_ENVIRONMENT "GIT_CONFIG"
#define CONFIG_LOCAL_ENVIRONMENT "GIT_CONFIG_LOCAL"
#define EXEC_PATH_ENVIRONMENT "GIT_EXEC_PATH"
#define GITATTRIBUTES_FILE ".gitattributes"
#define INFOATTRIBUTES_FILE "info/attributes"
#define ATTRIBUTE_MACRO_PREFIX "[attr]"

extern int is_bare_repository_cfg;
extern int is_bare_repository(void);
extern int is_inside_git_dir(void);
extern char *git_work_tree_cfg;
extern int is_inside_work_tree(void);
extern const char *get_git_dir(void);
extern char *get_object_directory(void);
extern char *get_refs_directory(void);
extern char *get_index_file(void);
extern char *get_graft_file(void);
extern int set_git_dir(const char *path);
extern const char *get_git_work_tree(void);

#define ALTERNATE_DB_ENVIRONMENT "GIT_ALTERNATE_OBJECT_DIRECTORIES"

extern const char **get_pathspec(const char *prefix, const char **pathspec);
extern void setup_work_tree(void);
extern const char *setup_git_directory_gently(int *);
extern const char *setup_git_directory(void);
extern const char *prefix_path(const char *prefix, int len, const char *path);
extern const char *prefix_filename(const char *prefix, int len, const char *path);
extern void verify_filename(const char *prefix, const char *name);
extern void verify_non_filename(const char *prefix, const char *name);

#define alloc_nr(x) (((x)+16)*3/2)

/*
 * Realloc the buffer pointed at by variable 'x' so that it can hold
 * at least 'nr' entries; the number of entries currently allocated
 * is 'alloc', using the standard growing factor alloc_nr() macro.
 *
 * DO NOT USE any expression with side-effect for 'x' or 'alloc'.
 */
#define ALLOC_GROW(x, nr, alloc) \
	do { \
		if ((nr) > alloc) { \
			if (alloc_nr(alloc) < (nr)) \
				alloc = (nr); \
			else \
				alloc = alloc_nr(alloc); \
			x = xrealloc((x), alloc * sizeof(*(x))); \
		} \
	} while(0)

/* Initialize and use the cache information */
extern int read_index(struct index_state *);
extern int read_index_from(struct index_state *, const char *path);
extern int write_index(struct index_state *, int newfd);
extern int discard_index(struct index_state *);
extern int verify_path(const char *path);
extern int index_name_pos(struct index_state *, const char *name, int namelen);
#define ADD_CACHE_OK_TO_ADD 1		/* Ok to add */
#define ADD_CACHE_OK_TO_REPLACE 2	/* Ok to replace file/directory */
#define ADD_CACHE_SKIP_DFCHECK 4	/* Ok to skip DF conflict checks */
#define ADD_CACHE_JUST_APPEND 8		/* Append only; tree.c::read_tree() */
extern int add_index_entry(struct index_state *, struct cache_entry *ce, int option);
extern struct cache_entry *refresh_cache_entry(struct cache_entry *ce, int really);
extern int remove_index_entry_at(struct index_state *, int pos);
extern int remove_file_from_index(struct index_state *, const char *path);
extern int add_file_to_index(struct index_state *, const char *path, int verbose);
extern struct cache_entry *make_cache_entry(unsigned int mode, const unsigned char *sha1, const char *path, int stage, int refresh);
extern int ce_same_name(struct cache_entry *a, struct cache_entry *b);

/* do stat comparison even if CE_VALID is true */
#define CE_MATCH_IGNORE_VALID		01
/* do not check the contents but report dirty on racily-clean entries */
#define CE_MATCH_RACY_IS_DIRTY	02
extern int ie_match_stat(struct index_state *, struct cache_entry *, struct stat *, unsigned int);
extern int ie_modified(struct index_state *, struct cache_entry *, struct stat *, unsigned int);

extern int ce_path_match(const struct cache_entry *ce, const char **pathspec);
extern int index_fd(unsigned char *sha1, int fd, struct stat *st, int write_object, enum object_type type, const char *path);
extern int index_pipe(unsigned char *sha1, int fd, const char *type, int write_object);
extern int index_path(unsigned char *sha1, const char *path, struct stat *st, int write_object);
extern void fill_stat_cache_info(struct cache_entry *ce, struct stat *st);

#define REFRESH_REALLY		0x0001	/* ignore_valid */
#define REFRESH_UNMERGED	0x0002	/* allow unmerged */
#define REFRESH_QUIET		0x0004	/* be quiet about it */
#define REFRESH_IGNORE_MISSING	0x0008	/* ignore non-existent */
extern int refresh_index(struct index_state *, unsigned int flags, const char **pathspec, char *seen);

struct lock_file {
	struct lock_file *next;
	int fd;
	pid_t owner;
	char on_list;
	char filename[PATH_MAX];
};
extern int hold_lock_file_for_update(struct lock_file *, const char *path, int);
extern int commit_lock_file(struct lock_file *);

extern int hold_locked_index(struct lock_file *, int);
extern int commit_locked_index(struct lock_file *);
extern void set_alternate_index_output(const char *);

extern void rollback_lock_file(struct lock_file *);
extern int delete_ref(const char *, const unsigned char *sha1);

/* Environment bits from configuration mechanism */
extern int trust_executable_bit;
extern int quote_path_fully;
extern int has_symlinks;
extern int assume_unchanged;
extern int prefer_symlink_refs;
extern int log_all_ref_updates;
extern int warn_ambiguous_refs;
extern int shared_repository;
extern const char *apply_default_whitespace;
extern int zlib_compression_level;
extern int core_compression_level;
extern int core_compression_seen;
extern size_t packed_git_window_size;
extern size_t packed_git_limit;
extern size_t delta_base_cache_limit;
extern int auto_crlf;

#define GIT_REPO_VERSION 0
extern int repository_format_version;
extern int check_repository_format(void);

#define MTIME_CHANGED	0x0001
#define CTIME_CHANGED	0x0002
#define OWNER_CHANGED	0x0004
#define MODE_CHANGED    0x0008
#define INODE_CHANGED   0x0010
#define DATA_CHANGED    0x0020
#define TYPE_CHANGED    0x0040

/* Return a statically allocated filename matching the sha1 signature */
extern char *mkpath(const char *fmt, ...) __attribute__((format (printf, 1, 2)));
extern char *git_path(const char *fmt, ...) __attribute__((format (printf, 1, 2)));
extern char *sha1_file_name(const unsigned char *sha1);
extern char *sha1_pack_name(const unsigned char *sha1);
extern char *sha1_pack_index_name(const unsigned char *sha1);
extern const char *find_unique_abbrev(const unsigned char *sha1, int);
extern const unsigned char null_sha1[20];
static inline int is_null_sha1(const unsigned char *sha1)
{
	return !memcmp(sha1, null_sha1, 20);
}
static inline int hashcmp(const unsigned char *sha1, const unsigned char *sha2)
{
	return memcmp(sha1, sha2, 20);
}
static inline void hashcpy(unsigned char *sha_dst, const unsigned char *sha_src)
{
	memcpy(sha_dst, sha_src, 20);
}
static inline void hashclr(unsigned char *hash)
{
	memset(hash, 0, 20);
}

int git_mkstemp(char *path, size_t n, const char *template);

enum sharedrepo {
	PERM_UMASK = 0,
	PERM_GROUP,
	PERM_EVERYBODY
};
int git_config_perm(const char *var, const char *value);
int adjust_shared_perm(const char *path);
int safe_create_leading_directories(char *path);
char *enter_repo(char *path, int strict);
static inline int is_absolute_path(const char *path)
{
	return path[0] == '/';
}
const char *make_absolute_path(const char *path);

/* Read and unpack a sha1 file into memory, write memory to a sha1 file */
extern int sha1_object_info(const unsigned char *, unsigned long *);
extern void * read_sha1_file(const unsigned char *sha1, enum object_type *type, unsigned long *size);
extern int hash_sha1_file(const void *buf, unsigned long len, const char *type, unsigned char *sha1);
extern int write_sha1_file(void *buf, unsigned long len, const char *type, unsigned char *return_sha1);
extern int pretend_sha1_file(void *, unsigned long, enum object_type, unsigned char *);

extern int check_sha1_signature(const unsigned char *sha1, void *buf, unsigned long size, const char *type);

extern int write_sha1_from_fd(const unsigned char *sha1, int fd, char *buffer,
			      size_t bufsize, size_t *bufposn);
extern int write_sha1_to_fd(int fd, const unsigned char *sha1);
extern int move_temp_to_file(const char *tmpfile, const char *filename);

extern int has_sha1_pack(const unsigned char *sha1, const char **ignore);
extern int has_sha1_file(const unsigned char *sha1);

extern int has_pack_file(const unsigned char *sha1);
extern int has_pack_index(const unsigned char *sha1);

extern const signed char hexval_table[256];
static inline unsigned int hexval(unsigned char c)
{
	return hexval_table[c];
}

/* Convert to/from hex/sha1 representation */
#define MINIMUM_ABBREV 4
#define DEFAULT_ABBREV 7

extern int get_sha1(const char *str, unsigned char *sha1);
extern int get_sha1_with_mode(const char *str, unsigned char *sha1, unsigned *mode);
extern int get_sha1_hex(const char *hex, unsigned char *sha1);
extern char *sha1_to_hex(const unsigned char *sha1);	/* static buffer result! */
extern int read_ref(const char *filename, unsigned char *sha1);
extern const char *resolve_ref(const char *path, unsigned char *sha1, int, int *);
extern int dwim_ref(const char *str, int len, unsigned char *sha1, char **ref);
extern int dwim_log(const char *str, int len, unsigned char *sha1, char **ref);

extern int refname_match(const char *abbrev_name, const char *full_name, const char **rules);
extern const char *ref_rev_parse_rules[];
extern const char *ref_fetch_rules[];

extern int create_symref(const char *ref, const char *refs_heads_master, const char *logmsg);
extern int validate_headref(const char *ref);

extern int base_name_compare(const char *name1, int len1, int mode1, const char *name2, int len2, int mode2);
extern int cache_name_compare(const char *name1, int len1, const char *name2, int len2);

extern void *read_object_with_reference(const unsigned char *sha1,
					const char *required_type,
					unsigned long *size,
					unsigned char *sha1_ret);

enum date_mode {
	DATE_NORMAL = 0,
	DATE_RELATIVE,
	DATE_SHORT,
	DATE_LOCAL,
	DATE_ISO8601,
	DATE_RFC2822
};

const char *show_date(unsigned long time, int timezone, enum date_mode mode);
int parse_date(const char *date, char *buf, int bufsize);
void datestamp(char *buf, int bufsize);
unsigned long approxidate(const char *);
enum date_mode parse_date_format(const char *format);

#define IDENT_WARN_ON_NO_NAME  1
#define IDENT_ERROR_ON_NO_NAME 2
#define IDENT_NO_DATE	       4
extern const char *git_author_info(int);
extern const char *git_committer_info(int);
extern const char *fmt_ident(const char *name, const char *email, const char *date_str, int);
extern const char *fmt_name(const char *name, const char *email);

struct checkout {
	const char *base_dir;
	int base_dir_len;
	unsigned force:1,
		 quiet:1,
		 not_new:1,
		 refresh_cache:1;
};

extern int checkout_entry(struct cache_entry *ce, const struct checkout *state, char *topath);
extern int has_symlink_leading_path(const char *name, char *last_symlink);

extern struct alternate_object_database {
	struct alternate_object_database *next;
	char *name;
	char base[FLEX_ARRAY]; /* more */
} *alt_odb_list;
extern void prepare_alt_odb(void);

struct pack_window {
	struct pack_window *next;
	unsigned char *base;
	off_t offset;
	size_t len;
	unsigned int last_used;
	unsigned int inuse_cnt;
};

extern struct packed_git {
	struct packed_git *next;
	struct pack_window *windows;
	off_t pack_size;
	const void *index_data;
	size_t index_size;
	uint32_t num_objects;
	int index_version;
	time_t mtime;
	int pack_fd;
	int pack_local;
	unsigned char sha1[20];
	/* something like ".git/objects/pack/xxxxx.pack" */
	char pack_name[FLEX_ARRAY]; /* more */
} *packed_git;

struct pack_entry {
	off_t offset;
	unsigned char sha1[20];
	struct packed_git *p;
};

struct ref {
	struct ref *next;
	unsigned char old_sha1[20];
	unsigned char new_sha1[20];
	unsigned int force:1,
		merge:1,
		nonfastforward:1,
		deletion:1;
	enum {
		REF_STATUS_NONE = 0,
		REF_STATUS_OK,
		REF_STATUS_REJECT_NONFASTFORWARD,
		REF_STATUS_REJECT_NODELETE,
		REF_STATUS_UPTODATE,
		REF_STATUS_REMOTE_REJECT,
		REF_STATUS_EXPECTING_REPORT,
	} status;
	char *remote_status;
	struct ref *peer_ref; /* when renaming */
	char name[FLEX_ARRAY]; /* more */
};

#define REF_NORMAL	(1u << 0)
#define REF_HEADS	(1u << 1)
#define REF_TAGS	(1u << 2)

extern struct ref *find_ref_by_name(struct ref *list, const char *name);

#define CONNECT_VERBOSE       (1u << 0)
extern struct child_process *git_connect(int fd[2], const char *url, const char *prog, int flags);
extern int finish_connect(struct child_process *conn);
extern int path_match(const char *path, int nr, char **match);
extern int get_ack(int fd, unsigned char *result_sha1);
extern struct ref **get_remote_heads(int in, struct ref **list, int nr_match, char **match, unsigned int flags);
extern int server_supports(const char *feature);

extern struct packed_git *parse_pack_index(unsigned char *sha1);
extern struct packed_git *parse_pack_index_file(const unsigned char *sha1,
						const char *idx_path);

extern void prepare_packed_git(void);
extern void reprepare_packed_git(void);
extern void install_packed_git(struct packed_git *pack);

extern struct packed_git *find_sha1_pack(const unsigned char *sha1,
					 struct packed_git *packs);

extern void pack_report(void);
extern int open_pack_index(struct packed_git *);
extern unsigned char* use_pack(struct packed_git *, struct pack_window **, off_t, unsigned int *);
extern void unuse_pack(struct pack_window **);
extern struct packed_git *add_packed_git(const char *, int, int);
extern const unsigned char *nth_packed_object_sha1(struct packed_git *, uint32_t);
extern off_t find_pack_entry_one(const unsigned char *, struct packed_git *);
extern void *unpack_entry(struct packed_git *, off_t, enum object_type *, unsigned long *);
extern unsigned long unpack_object_header_gently(const unsigned char *buf, unsigned long len, enum object_type *type, unsigned long *sizep);
extern unsigned long get_size_from_delta(struct packed_git *, struct pack_window **, off_t);
extern const char *packed_object_info_detail(struct packed_git *, off_t, unsigned long *, unsigned long *, unsigned int *, unsigned char *);
extern int matches_pack_name(struct packed_git *p, const char *name);

/* Dumb servers support */
extern int update_server_info(int);

typedef int (*config_fn_t)(const char *, const char *);
extern int git_default_config(const char *, const char *);
extern int git_config_from_file(config_fn_t fn, const char *);
extern int git_config(config_fn_t fn);
extern int git_parse_long(const char *, long *);
extern int git_parse_ulong(const char *, unsigned long *);
extern int git_config_int(const char *, const char *);
extern unsigned long git_config_ulong(const char *, const char *);
extern int git_config_bool(const char *, const char *);
extern int git_config_set(const char *, const char *);
extern int git_config_set_multivar(const char *, const char *, const char *, int);
extern int git_config_rename_section(const char *, const char *);
extern const char *git_etc_gitconfig(void);
extern int check_repository_format_version(const char *var, const char *value);

#define MAX_GITNAME (1000)
extern char git_default_email[MAX_GITNAME];
extern char git_default_name[MAX_GITNAME];

extern const char *git_commit_encoding;
extern const char *git_log_output_encoding;

/* IO helper functions */
extern void maybe_flush_or_die(FILE *, const char *);
extern int copy_fd(int ifd, int ofd);
extern int read_in_full(int fd, void *buf, size_t count);
extern int write_in_full(int fd, const void *buf, size_t count);
extern void write_or_die(int fd, const void *buf, size_t count);
extern int write_or_whine(int fd, const void *buf, size_t count, const char *msg);
extern int write_or_whine_pipe(int fd, const void *buf, size_t count, const char *msg);

/* pager.c */
extern void setup_pager(void);
extern char *pager_program;
extern int pager_in_use(void);
extern int pager_use_color;

extern char *editor_program;
extern char *excludes_file;

/* base85 */
int decode_85(char *dst, const char *line, int linelen);
void encode_85(char *buf, const unsigned char *data, int bytes);

/* alloc.c */
extern void *alloc_blob_node(void);
extern void *alloc_tree_node(void);
extern void *alloc_commit_node(void);
extern void *alloc_tag_node(void);
extern void *alloc_object_node(void);
extern void alloc_report(void);

/* trace.c */
extern void trace_printf(const char *format, ...);
extern void trace_argv_printf(const char **argv, const char *format, ...);

/* convert.c */
/* returns 1 if *dst was used */
extern int convert_to_git(const char *path, const char *src, size_t len, struct strbuf *dst);
extern int convert_to_working_tree(const char *path, const char *src, size_t len, struct strbuf *dst);

/* add */
void add_files_to_cache(int verbose, const char *prefix, const char **pathspec);

/* diff.c */
extern int diff_auto_refresh_index;

/* match-trees.c */
void shift_tree(const unsigned char *, const unsigned char *, unsigned char *, int);

/*
 * whitespace rules.
 * used by both diff and apply
 */
#define WS_TRAILING_SPACE	01
#define WS_SPACE_BEFORE_TAB	02
#define WS_INDENT_WITH_NON_TAB	04
#define WS_DEFAULT_RULE (WS_TRAILING_SPACE|WS_SPACE_BEFORE_TAB)
extern unsigned whitespace_rule_cfg;
extern unsigned whitespace_rule(const char *);
extern unsigned parse_whitespace_rule(const char *);
extern unsigned check_and_emit_line(const char *line, int len, unsigned ws_rule,
    FILE *stream, const char *set,
    const char *reset, const char *ws);
extern char *whitespace_error_string(unsigned ws);

/* ls-files */
int pathspec_match(const char **spec, char *matched, const char *filename, int skiplen);
int report_path_error(const char *ps_matched, const char **pathspec, int prefix_offset);
void overlay_tree_on_cache(const char *tree_name, const char *prefix);

#endif /* CACHE_H */