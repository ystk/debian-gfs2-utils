#include "clusterautoconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>
#include <string.h>
#include <inttypes.h>
#include <sys/types.h>
#include <linux/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <curses.h>
#include <term.h>
#include <time.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <dirent.h>

#include <linux/gfs2_ondisk.h>
#include "copyright.cf"

#include "hexedit.h"
#include "libgfs2.h"
#include "gfs2hex.h"
#include "extended.h"

const char *mtypes[] = {"none", "sb", "rg", "rb", "di", "in", "lf", "jd",
			"lh", "ld", "ea", "ed", "lb", "13", "qc"};
const char *allocdesc[2][5] = {
	{"Free ", "Data ", "Unlnk", "Meta ", "Resrv"},
	{"Free ", "Data ", "FreeM", "Meta ", "Resrv"},};

struct gfs2_rgrp *lrgrp;
struct gfs2_meta_header *lmh;
struct gfs2_dinode *ldi;
struct gfs2_leaf *lleaf;
struct gfs2_log_header *llh;
struct gfs2_log_descriptor *lld;
int pgnum;
int details = 0;
long int gziplevel = 9;
static int termcols;

int display(int identify_only);

/* for assigning numeric fields: */
#define checkassign(strfield, struct, member, value) do {		\
		if (strcmp(#member, strfield) == 0) {			\
			struct->member = (typeof(struct->member)) value; \
			return 0;					\
		}							\
	} while(0)

/* for assigning string fields: */
#define checkassigns(strfield, struct, member, val) do {		\
		if (strcmp(#member, strfield) == 0) {			\
			memset(struct->member, 0, sizeof(struct->member)); \
			strncpy((char *)struct->member, (char *)val, \
				sizeof(struct->member));		\
			return 0;					\
		}							\
	} while(0)

/* for printing numeric fields: */
#define checkprint(strfield, struct, member) do {		\
		if (strcmp(#member, strfield) == 0) {		\
			if (dmode == HEX_MODE)			\
				printf("0x%llx\n",		\
				       (unsigned long long)struct->member); \
			else					\
				printf("%llu\n",		\
				       (unsigned long long)struct->member); \
			return 0;				\
		}						\
	} while(0)

/* for printing string fields: */
#define checkprints(strfield, struct, member) do {		\
		if (strcmp(#member, strfield) == 0) {		\
			printf("%s\n", struct->member);		\
			return 0;				\
		}						\
	} while(0)

/* -------------------------------------------------------------------------
 * superblock
 * ------------------------------------------------------------------------- */
const int fieldsize_sb[] = {
	sizeof(sb.sb_header.mh_magic),
	sizeof(sb.sb_header.mh_type),
	sizeof(sb.sb_header.__pad0),
	sizeof(sb.sb_header.mh_format),
	sizeof(sb.sb_header.__pad1),
	sizeof(sb.sb_fs_format),
	sizeof(sb.sb_multihost_format),
	sizeof(sb.__pad0),
	sizeof(sb.sb_bsize),
	sizeof(sb.sb_bsize_shift),
	sizeof(sb.__pad1),
	sizeof(sb.sb_master_dir.no_formal_ino),
	sizeof(sb.sb_master_dir.no_addr),
	sizeof(sb.__pad2.no_formal_ino),
	sizeof(sb.__pad2.no_addr),
	sizeof(sb.sb_root_dir.no_formal_ino),
	sizeof(sb.sb_root_dir.no_addr),
	sizeof(sb.sb_lockproto),
	sizeof(sb.sb_locktable),
	sizeof(sb.__pad3.no_formal_ino),
	sizeof(sb.__pad3.no_addr),
	sizeof(sb.__pad4.no_formal_ino),
	sizeof(sb.__pad4.no_addr),
	sizeof(sb.sb_uuid),
	-1
};

const char *fieldnames_sb[] = {
	"mh_magic",
	"mh_type",
	"mh.__pad0",
	"mh_format",
	"mh.__pad1",
	"sb_fs_format",
	"sb_multihost_format",
	"__pad0",
	"sb_bsize",
	"sb_bsize_shift",
	"__pad1",
	"master.no_formal_ino",
	"master.no_addr",
	"__pad2.no_formal_ino",
	"__pad2.no_addr",
	"root.no_formal_ino",
	"root.no_addr",
	"sb_lockproto",
	"sb_locktable",
	"__pad3.no_formal_ino",
	"__pad3.no_addr",
	"__pad4.no_formal_ino",
	"__pad4.no_addr",
	"sb_uuid",
};

/* This determines which field the cursor is located in */
static int which_field_sb(int off)
{
	int i, tot;

	tot = 0;
	for (i = 0; fieldsize_sb[i] != -1; i++) {
		tot += fieldsize_sb[i];
		if (off < tot)
			return i;
	}
	return -1;
}

static const char *which_fieldname_sb(int off)
{
	int w = which_field_sb(off);
	if (w < 0)
		return NULL;
	return fieldnames_sb[w];
}

/* -------------------------------------------------------------------------
 * rgrp
 * ------------------------------------------------------------------------- */
const int fieldsize_rgrp[] = {
	sizeof(lrgrp->rg_header.mh_magic),
	sizeof(lrgrp->rg_header.mh_type),
	sizeof(lrgrp->rg_header.__pad0),
	sizeof(lrgrp->rg_header.mh_format),
	sizeof(lrgrp->rg_header.__pad1),
	sizeof(lrgrp->rg_flags),
	sizeof(lrgrp->rg_free),
	sizeof(lrgrp->rg_dinodes),
	sizeof(lrgrp->__pad),
	sizeof(lrgrp->rg_igeneration),
	sizeof(lrgrp->rg_reserved),
	-1
};

const char *fieldnames_rgrp[] = {
	"mh_magic",
	"mh_type",
	"mh.__pad0",
	"mh_format",
	"mh.__pad1",
	"rg_flags",
	"rg_free",
	"rg_dinodes",
	"__pad",
	"rg_igeneration",
	"rg_reserved",
};

static int which_field_rgrp(int off)
{
	int i, tot;

	tot = 0;
	for (i = 0; fieldsize_rgrp[i] != -1; i++) {
		tot += fieldsize_rgrp[i];
		if (off < tot)
			return i;
	}
	return -1;
}

static const char *which_fieldname_rgrp(int off)
{
	int w = which_field_rgrp(off);
	if (w < 0)
		return NULL;
	return fieldnames_rgrp[w];
}

/* -------------------------------------------------------------------------
 * metaheader
 * ------------------------------------------------------------------------- */
const int fieldsize_mh[] = {
	sizeof(lmh->mh_magic),
	sizeof(lmh->mh_type),
	sizeof(lmh->__pad0),
	sizeof(lmh->mh_format),
	sizeof(lmh->__pad1),
	-1
};

const char *fieldnames_mh[] = {
	"mh_magic",
	"mh_type",
	"mh.__pad0",
	"mh_format",
	"mh.__pad1",
};

static int which_field_mh(int off)
{
	int i, tot;

	tot = 0;
	for (i = 0; fieldsize_mh[i] != -1; i++) {
		tot += fieldsize_mh[i];
		if (off < tot)
			return i;
	}
	return -1;
}

static const char *which_fieldname_mh(int off)
{
	int w = which_field_mh(off);
	if (w < 0)
		return NULL;
	return fieldnames_mh[w];
}

/* -------------------------------------------------------------------------
 * dinode
 * ------------------------------------------------------------------------- */
const int fieldsize_di[] = {
	sizeof(ldi->di_header.mh_magic),
	sizeof(ldi->di_header.mh_type),
	sizeof(ldi->di_header.__pad0),
	sizeof(ldi->di_header.mh_format),
	sizeof(ldi->di_header.__pad1),
	sizeof(ldi->di_num.no_formal_ino),
	sizeof(ldi->di_num.no_addr),
	sizeof(ldi->di_mode),
	sizeof(ldi->di_uid),
	sizeof(ldi->di_gid),
	sizeof(ldi->di_nlink),
	sizeof(ldi->di_size),
	sizeof(ldi->di_blocks),
	sizeof(ldi->di_atime),
	sizeof(ldi->di_mtime),
	sizeof(ldi->di_ctime),
	sizeof(ldi->di_major),
	sizeof(ldi->di_minor),
	sizeof(ldi->di_goal_meta),
	sizeof(ldi->di_goal_data),
	sizeof(ldi->di_generation),
	sizeof(ldi->di_flags),
	sizeof(ldi->di_payload_format),
	sizeof(ldi->__pad1),
	sizeof(ldi->di_height),
	sizeof(ldi->__pad2),
	sizeof(ldi->__pad3),
	sizeof(ldi->di_depth),
	sizeof(ldi->di_entries),
	sizeof(ldi->__pad4.no_formal_ino),
	sizeof(ldi->__pad4.no_addr),
	sizeof(ldi->di_eattr),
	sizeof(ldi->di_atime_nsec),
	sizeof(ldi->di_mtime_nsec),
	sizeof(ldi->di_ctime_nsec),
	sizeof(ldi->di_reserved),
	-1
};

const char *fieldnames_di[] = {
	"mh_magic",
	"mh_type",
	"mh.__pad0",
	"mh_format",
	"mh.__pad1",
	"no_formal_ino",
	"no_addr",
	"di_mode",
	"di_uid",
	"di_gid",
	"di_nlink",
	"di_size",
	"di_blocks",
	"di_atime",
	"di_mtime",
	"di_ctime",
	"di_major",
	"di_minor",
	"di_goal_meta",
	"di_goal_data",
	"di_generation",
	"di_flags",
	"di_payload_format",
	"__pad1",
	"di_height",
	"__pad2",
	"__pad3",
	"di_depth",
	"di_entries",
	"__pad4.no_formal_ino",
	"__pad4.no_addr",
	"di_eattr",
	"di_atime_nsec",
	"di_mtime_nsec",
	"di_ctime_nsec",
	"di_reserved",
};

static int which_field_di(int off)
{
	int i, tot;

	tot = 0;
	for (i = 0; fieldsize_di[i] != -1; i++) {
		tot += fieldsize_di[i];
		if (off < tot)
			return i;
	}
	return -1;
}

static const char *which_fieldname_di(int off)
{
	int w = which_field_di(off);
	if (w < 0)
		return NULL;
	return fieldnames_di[w];
}

/* -------------------------------------------------------------------------
 * directory leaf
 * ------------------------------------------------------------------------- */
const int fieldsize_lf[] = {
	sizeof(lleaf->lf_header.mh_magic),
	sizeof(lleaf->lf_header.mh_type),
	sizeof(lleaf->lf_header.__pad0),
	sizeof(lleaf->lf_header.mh_format),
	sizeof(lleaf->lf_header.__pad1),
	sizeof(lleaf->lf_depth),
	sizeof(lleaf->lf_entries),
	sizeof(lleaf->lf_dirent_format),
	sizeof(lleaf->lf_next),
	sizeof(lleaf->lf_reserved),
	-1
};

const char *fieldnames_lf[] = {
	"mh_magic",
	"mh_type",
	"mh.__pad0",
	"mh_format",
	"mh.__pad1",
	"lf_depth",
	"lf_entries",
	"lf_dirent_format",
	"lf_next",
	"lf_reserved",
};

static int which_field_lf(int off)
{
	int i, tot;

	tot = 0;
	for (i = 0; fieldsize_lf[i] != -1; i++) {
		tot += fieldsize_lf[i];
		if (off < tot)
			return i;
	}
	return -1;
}

static const char *which_fieldname_lf(int off)
{
	int w = which_field_lf(off);
	if (w < 0)
		return NULL;
	return fieldnames_lf[w];
}

/* -------------------------------------------------------------------------
 * log header
 * ------------------------------------------------------------------------- */
const int fieldsize_lh[] = {
	sizeof(llh->lh_header.mh_magic),
	sizeof(llh->lh_header.mh_type),
	sizeof(llh->lh_header.__pad0),
	sizeof(llh->lh_header.mh_format),
	sizeof(llh->lh_header.__pad1),
	sizeof(llh->lh_sequence),
	sizeof(llh->lh_flags),
	sizeof(llh->lh_tail),
	sizeof(llh->lh_blkno),
	sizeof(llh->lh_hash),
	-1
};

const char *fieldnames_lh[] = {
	"mh_magic",
	"mh_type",
	"mh.__pad0",
	"mh_format",
	"mh.__pad1",
	"lh_sequence",
	"lh_flags",
	"lh_tail",
	"lh_blkno",
	"lh_hash",
};

static int which_field_lh(int off)
{
	int i, tot;

	tot = 0;
	for (i = 0; fieldsize_lh[i] != -1; i++) {
		tot += fieldsize_lh[i];
		if (off < tot)
			return i;
	}
	return -1;
}

static const char *which_fieldname_lh(int off)
{
	int w = which_field_lh(off);
	if (w < 0)
		return NULL;
	return fieldnames_lh[w];
}

/* -------------------------------------------------------------------------
 * log descriptor
 * ------------------------------------------------------------------------- */
const int fieldsize_ld[] = {
	sizeof(lld->ld_header.mh_magic),
	sizeof(lld->ld_header.mh_type),
	sizeof(lld->ld_header.__pad0),
	sizeof(lld->ld_header.mh_format),
	sizeof(lld->ld_header.__pad1),
	sizeof(lld->ld_type),
	sizeof(lld->ld_length),
	sizeof(lld->ld_data1),
	sizeof(lld->ld_data2),
	sizeof(lld->ld_reserved),
	-1
};

const char *fieldnames_ld[] = {
	"mh_magic",
	"mh_type",
	"mh.__pad0",
	"mh_format",
	"mh.__pad1",
	"ld_type",
	"ld_length",
	"ld_data1",
	"ld_data2",
	"ld_reserved",
};

static int which_field_ld(int off)
{
	int i, tot;

	tot = 0;
	for (i = 0; fieldsize_ld[i] != -1; i++) {
		tot += fieldsize_ld[i];
		if (off < tot)
			return i;
	}
	return -1;
}

static const char *which_fieldname_ld(int off)
{
	int w = which_field_ld(off);
	if (w < 0)
		return NULL;
	return fieldnames_ld[w];
}

/* -------------------------------------------------------------------------
 * field-related functions:
 * ------------------------------------------------------------------------- */
static int gfs2_sb_printval(struct gfs2_sb *lsb, const char *strfield)
{
	checkprint(strfield, lsb, sb_fs_format);
	checkprint(strfield, lsb, sb_multihost_format);
	checkprint(strfield, lsb, __pad0);
	checkprint(strfield, lsb, sb_bsize);
	checkprint(strfield, lsb, sb_bsize_shift);
	checkprint(strfield, lsb, __pad1);
	checkprint(strfield, lsb, sb_master_dir.no_addr);
	checkprint(strfield, lsb, __pad2.no_addr);
	checkprint(strfield, lsb, sb_root_dir.no_addr);
	checkprints(strfield, lsb, sb_lockproto);
	checkprints(strfield, lsb, sb_locktable);
	checkprint(strfield, lsb, __pad3.no_addr);
	checkprint(strfield, lsb, __pad4.no_addr);
	if (strcmp(strfield, "sb_uuid") == 0) {
		printf("%s\n", str_uuid(lsb->sb_uuid));
		return 0;
	}

	return -1;
}

static int gfs2_sb_assignval(struct gfs2_sb *lsb, const char *strfield,
			     uint64_t value)
{
	checkassign(strfield, lsb, sb_fs_format, value);
	checkassign(strfield, lsb, sb_multihost_format, value);
	checkassign(strfield, lsb, __pad0, value);
	checkassign(strfield, lsb, sb_bsize, value);
	checkassign(strfield, lsb, sb_bsize_shift, value);
	checkassign(strfield, lsb, __pad1, value);
	checkassign(strfield, lsb, sb_master_dir.no_addr, value);
	checkassign(strfield, lsb, __pad2.no_addr, value);
	checkassign(strfield, lsb, sb_root_dir.no_addr, value);
	checkassign(strfield, lsb, __pad3.no_addr, value);
	checkassign(strfield, lsb, __pad4.no_addr, value);

	return -1;
}

static int gfs2_sb_assigns(struct gfs2_sb *lsb, const char *strfield,
			   const char *val)
{
	checkassigns(strfield, lsb, sb_lockproto, val);
	checkassigns(strfield, lsb, sb_locktable, val);
	checkassigns(strfield, lsb, sb_uuid, val);

	return -1;
}

static int gfs2_dinode_printval(struct gfs2_dinode *dip, const char *strfield)
{
	checkprint(strfield, dip, di_mode);
	checkprint(strfield, dip, di_uid);
	checkprint(strfield, dip, di_gid);
	checkprint(strfield, dip, di_nlink);
	checkprint(strfield, dip, di_size);
	checkprint(strfield, dip, di_blocks);
	checkprint(strfield, dip, di_atime);
	checkprint(strfield, dip, di_mtime);
	checkprint(strfield, dip, di_ctime);
	checkprint(strfield, dip, di_major);
	checkprint(strfield, dip, di_minor);
	checkprint(strfield, dip, di_goal_meta);
	checkprint(strfield, dip, di_goal_data);
	checkprint(strfield, dip, di_flags);
	checkprint(strfield, dip, di_payload_format);
	checkprint(strfield, dip, di_height);
	checkprint(strfield, dip, di_depth);
	checkprint(strfield, dip, di_entries);
	checkprint(strfield, dip, di_eattr);

	return -1;
}

static int gfs2_dinode_assignval(struct gfs2_dinode *dia, const char *strfield,
				 uint64_t value)
{
	checkassign(strfield, dia, di_mode, value);
	checkassign(strfield, dia, di_uid, value);
	checkassign(strfield, dia, di_gid, value);
	checkassign(strfield, dia, di_nlink, value);
	checkassign(strfield, dia, di_size, value);
	checkassign(strfield, dia, di_blocks, value);
	checkassign(strfield, dia, di_atime, value);
	checkassign(strfield, dia, di_mtime, value);
	checkassign(strfield, dia, di_ctime, value);
	checkassign(strfield, dia, di_major, value);
	checkassign(strfield, dia, di_minor, value);
	checkassign(strfield, dia, di_goal_meta, value);
	checkassign(strfield, dia, di_goal_data, value);
	checkassign(strfield, dia, di_flags, value);
	checkassign(strfield, dia, di_payload_format, value);
	checkassign(strfield, dia, di_height, value);
	checkassign(strfield, dia, di_depth, value);
	checkassign(strfield, dia, di_entries, value);
	checkassign(strfield, dia, di_eattr, value);

	return -1;
}

static int gfs2_rgrp_printval(struct gfs2_rgrp *rg, const char *strfield)
{
	checkprint(strfield, rg, rg_flags);
	checkprint(strfield, rg, rg_free);
	checkprint(strfield, rg, rg_dinodes);

	return -1;
}

static int gfs2_rgrp_assignval(struct gfs2_rgrp *rg, const char *strfield,
			       uint64_t value)
{
	checkassign(strfield, rg, rg_flags, value);
	checkassign(strfield, rg, rg_free, value);
	checkassign(strfield, rg, rg_dinodes, value);

	return -1;
}

static int gfs2_leaf_printval(struct gfs2_leaf *lf, const char *strfield)
{
	checkprint(strfield, lf, lf_depth);
	checkprint(strfield, lf, lf_entries);
	checkprint(strfield, lf, lf_dirent_format);
	checkprint(strfield, lf, lf_next);
	checkprints(strfield, lf, lf_reserved);

	return -1;
}

static int gfs2_leaf_assignval(struct gfs2_leaf *lf, const char *strfield,
			uint64_t value)
{
	checkassign(strfield, lf, lf_depth, value);
	checkassign(strfield, lf, lf_entries, value);
	checkassign(strfield, lf, lf_dirent_format, value);
	checkassign(strfield, lf, lf_next, value);

	return -1;
}

static int gfs2_leaf_assigns(struct gfs2_leaf *lf, const char *strfield,
			     const char *val)
{
	checkassigns(strfield, lf, lf_reserved, val);

	return -1;
}

static int gfs2_lh_printval(struct gfs2_log_header *lh, const char *strfield)
{
	checkprint(strfield, lh, lh_sequence);
	checkprint(strfield, lh, lh_flags);
	checkprint(strfield, lh, lh_tail);
	checkprint(strfield, lh, lh_blkno);
	checkprint(strfield, lh, lh_hash);

	return -1;
}

static int gfs2_lh_assignval(struct gfs2_log_header *lh, const char *strfield,
			     uint64_t value)
{
	checkassign(strfield, lh, lh_sequence, value);
	checkassign(strfield, lh, lh_flags, value);
	checkassign(strfield, lh, lh_tail, value);
	checkassign(strfield, lh, lh_blkno, value);
	checkassign(strfield, lh, lh_hash, value);

	return -1;
}

static int gfs2_ld_printval(struct gfs2_log_descriptor *ld,
			    const char *strfield)
{
	checkprint(strfield, ld, ld_type);
	checkprint(strfield, ld, ld_length);
	checkprint(strfield, ld, ld_data1);
	checkprint(strfield, ld, ld_data2);
	checkprints(strfield, ld, ld_reserved);

	return -1;
}

static int gfs2_ld_assignval(struct gfs2_log_descriptor *ld,
			     const char *strfield, uint64_t value)
{
	checkassign(strfield, ld, ld_type, value);
	checkassign(strfield, ld, ld_length, value);
	checkassign(strfield, ld, ld_data1, value);
	checkassign(strfield, ld, ld_data2, value);

	return -1;
}

static int gfs2_ld_assigns(struct gfs2_log_descriptor *ld,
			   const char *strfield, const char *val)
{
	checkassigns(strfield, ld, ld_reserved, val);

	return -1;
}

static int gfs2_qc_printval(struct gfs2_quota_change *qc,
			    const char *strfield)
{
	checkprint(strfield, qc, qc_change);
	checkprint(strfield, qc, qc_flags);
	checkprint(strfield, qc, qc_id);

	return -1;
}

static int gfs2_qc_assignval(struct gfs2_quota_change *qc,
			     const char *strfield, uint64_t value)
{
	checkassign(strfield, qc, qc_change, value);
	checkassign(strfield, qc, qc_flags, value);
	checkassign(strfield, qc, qc_id, value);

	return -1;
}

/* ------------------------------------------------------------------------- */
/* erase - clear the screen */
/* ------------------------------------------------------------------------- */
static void Erase(void)
{
	bkgd(A_NORMAL|COLOR_PAIR(COLOR_NORMAL));
	/* clear();*/ /* doesn't set background correctly */
	erase();
	/*bkgd(bg);*/
}

/* ------------------------------------------------------------------------- */
/* display_title_lines */
/* ------------------------------------------------------------------------- */
static void display_title_lines(void)
{
	Erase();
	COLORS_TITLE;
	move(0, 0);
	printw("%-80s",TITLE1);
	move(termlines, 0);
	printw("%-79s",TITLE2);
	COLORS_NORMAL;
}

/* ------------------------------------------------------------------------- */
/* bobgets - get a string                                                    */
/* returns: 1 if user exited by hitting enter                                */
/*          0 if user exited by hitting escape                               */
/* ------------------------------------------------------------------------- */
static int bobgets(char string[],int x,int y,int sz,int *ch)
{
	int done,runningy,rc;

	move(x,y);
	done=FALSE;
	COLORS_INVERSE;
	move(x,y);
	addstr(string);
	move(x,y);
	curs_set(2);
	refresh();
	runningy=y;
	rc=0;
	while (!done) {
		*ch = getch();
		
		if(*ch < 0x0100 && isprint(*ch)) {
			char *p=string+strlen(string); // end of the string

			*(p+1)='\0';
			while (insert && p > &string[runningy-y]) {
				*p=*(p-1);
				p--;
			}
			string[runningy-y]=*ch;
			runningy++;
			move(x,y);
			addstr(string);
			if (runningy-y >= sz) {
				rc=1;
				*ch = KEY_RIGHT;
				done = TRUE;
			}
		}
		else {
			// special character, is it one we recognize?
			switch(*ch)
			{
			case(KEY_ENTER):
			case('\n'):
			case('\r'):
				rc=1;
				done=TRUE;
				string[runningy-y] = '\0';
				break;
			case(KEY_CANCEL):
			case(0x01B):
				rc=0;
				done=TRUE;
				break;
			case(KEY_LEFT):
				if (dmode == HEX_MODE) {
					done = TRUE;
					rc = 1;
				}
				else
					runningy--;
				break;
			case(KEY_RIGHT):
				if (dmode == HEX_MODE) {
					done = TRUE;
					rc = 1;
				}
				else
					runningy++;
				break;
			case(KEY_DC):
			case(0x07F):
				if (runningy>=y) {
					char *p;
					p = &string[runningy - y];
					while (*p) {
						*p = *(p + 1);
						p++;
					}
					*p = '\0';
					runningy--;
					// remove the character from the string 
					move(x,y);
					addstr(string);
					COLORS_NORMAL;
					addstr(" ");
					COLORS_INVERSE;
					runningy++;
				}
				break;
			case(KEY_BACKSPACE):
				if (runningy>y) {
					char *p;

					p = &string[runningy - y - 1];
					while (*p) {
						*p = *(p + 1);
						p++;
					}
					*p='\0';
					runningy--;
					// remove the character from the string 
					move(x,y);
					addstr(string);
					COLORS_NORMAL;
					addstr(" ");
					COLORS_INVERSE;
				}
				break;
			case KEY_DOWN:	// Down
				rc=0x5000U;
				done=TRUE;
				break;
			case KEY_UP:	// Up
				rc=0x4800U;
				done=TRUE;
				break;
			case 0x014b:
				insert=!insert;
				move(0,68);
				if (insert)
					printw("insert ");
				else
					printw("replace");
				break;
			default:
				move(0,70);
				printw("%08x",*ch);
				// ignore all other characters
				break;
			} // end switch on non-printable character
		} // end non-printable character
		move(x,runningy);
		refresh();
	} // while !done
	if (sz>0)
		string[sz]='\0';
	COLORS_NORMAL;
	return rc;
}/* bobgets */

/******************************************************************************
** instr - instructions
******************************************************************************/
static void gfs2instr(const char *s1, const char *s2)
{
	COLORS_HIGHLIGHT;
	move(line,0);
	printw(s1);
	COLORS_NORMAL;
	move(line,17);
	printw(s2);
	line++;
}

/******************************************************************************
*******************************************************************************
**
** void print_usage()
**
** Description:
**   This routine prints out the appropriate commands for this application.
**
*******************************************************************************
******************************************************************************/

static void print_usage(void)
{
	int ch;

	line = 2;
	Erase();
	display_title_lines();
	move(line++,0);
	printw("Supported commands: (roughly conforming to the rules of 'less')");
	line++;
	move(line++,0);
	printw("Navigation:");
	gfs2instr("<pg up>/<down>","Move up or down one screen full");
	gfs2instr("<up>/<down>","Move up or down one line");
	gfs2instr("<left>/<right>","Move left or right one byte");
	gfs2instr("<home>","Return to the superblock.");
	gfs2instr("   f","Forward one 4K block");
	gfs2instr("   b","Backward one 4K block");
	gfs2instr("   g","Goto a given block (number, master, root, rindex, jindex, etc)");
	gfs2instr("   j","Jump to the highlighted 64-bit block number.");
	gfs2instr("    ","(You may also arrow up to the block number and hit enter)");
	gfs2instr("<backspace>","Return to a previous block (a block stack is kept)");
	gfs2instr("<space>","Jump forward to block before backspace (opposite of backspace)");
	line++;
	move(line++, 0);
	printw("Other commands:");
	gfs2instr("   h","This Help display");
	gfs2instr("   c","Toggle the color scheme");
	gfs2instr("   m","Switch display mode: hex -> GFS2 structure -> Extended");
	gfs2instr("   q","Quit (same as hitting <escape> key)");
	gfs2instr("<enter>","Edit a value (enter to save, esc to discard)");
	gfs2instr("       ","(Currently only works on the hex display)");
	gfs2instr("<escape>","Quit the program");
	line++;
	move(line++, 0);
	printw("Notes: Areas shown in red are outside the bounds of the struct/file.");
	move(line++, 0);
	printw("       Areas shown in blue are file contents.");
	move(line++, 0);
	printw("       Characters shown in green are selected for edit on <enter>.");
	move(line++, 0);
	move(line++, 0);
	printw("Press any key to return.");
	refresh();
	while ((ch=getch()) == 0); // wait for input
	Erase();
}

/* ------------------------------------------------------------------------ */
/* get_block_type                                                           */
/* returns: metatype if block is a GFS2 structure block type                */
/*          0 if block is not a GFS2 structure                              */
/* ------------------------------------------------------------------------ */
static int get_block_type(struct gfs2_buffer_head *lbh)
{
	int ret_type = 0;
	char *lpBuffer = lbh->b_data;

	if (*(lpBuffer+0)==0x01 && *(lpBuffer+1)==0x16 &&
	    *(lpBuffer+2)==0x19 && *(lpBuffer+3)==0x70 &&
	    *(lpBuffer+4)==0x00 && *(lpBuffer+5)==0x00 &&
	    *(lpBuffer+6)==0x00) /* If magic number appears at the start */
		ret_type = *(lpBuffer+7);
	return ret_type;
}

/* ------------------------------------------------------------------------ */
/* display_block_type                                                       */
/* returns: metatype if block is a GFS2 structure block type                */
/*          0 if block is not a GFS2 structure                              */
/* ------------------------------------------------------------------------ */
int display_block_type(int from_restore)
{
	int ret_type = 0; /* return type */

	/* first, print out the kind of GFS2 block this is */
	if (termlines) {
		line = 1;
		move(line, 0);
	}
	print_gfs2("Block #");
	if (termlines) {
		if (edit_row[dmode] == -1)
			COLORS_HIGHLIGHT;
	}
	if (block == RGLIST_DUMMY_BLOCK)
		print_gfs2("RG List       ");
	else
		print_gfs2("%lld    (0x%llx)", block, block);
	if (termlines) {
		if (edit_row[dmode] == -1)
			COLORS_NORMAL;
	}
	print_gfs2(" ");
	if (!from_restore)
		print_gfs2("of %llu (0x%llx) ", max_block, max_block);
	if (block == RGLIST_DUMMY_BLOCK) {
		ret_type = GFS2_METATYPE_RG;
		struct_len = sbd.gfs1 ? sizeof(struct gfs_rgrp) :
			sizeof(struct gfs2_rgrp);
	}
	else if ((ret_type = get_block_type(bh))) {
		switch (*(bh->b_data + 7)) {
		case GFS2_METATYPE_SB:   /* 1 */
			print_gfs2("(superblock)");
			if (sbd.gfs1)
				struct_len = sizeof(struct gfs_sb);
			else
				struct_len = sizeof(struct gfs2_sb);
			break;
		case GFS2_METATYPE_RG:   /* 2 */
			print_gfs2("(rsrc grp hdr)");
			struct_len = sizeof(struct gfs2_rgrp);
			break;
		case GFS2_METATYPE_RB:   /* 3 */
			print_gfs2("(rsrc grp bitblk)");
			struct_len = sizeof(struct gfs2_meta_header);
			break;
		case GFS2_METATYPE_DI:   /* 4 */
			print_gfs2("(disk inode)");
			struct_len = sizeof(struct gfs2_dinode);
			break;
		case GFS2_METATYPE_IN:   /* 5 */
			print_gfs2("(indir blklist)");
			if (sbd.gfs1)
				struct_len = sizeof(struct gfs_indirect);
			else
				struct_len = sizeof(struct gfs2_meta_header);
			break;
		case GFS2_METATYPE_LF:   /* 6 */
			print_gfs2("(directory leaf)");
			struct_len = sizeof(struct gfs2_leaf);
			break;
		case GFS2_METATYPE_JD:
			print_gfs2("(journal data)");
			struct_len = sizeof(struct gfs2_meta_header);
			break;
		case GFS2_METATYPE_LH:
			print_gfs2("(log header)");
			struct_len = sizeof(struct gfs2_log_header);
			break;
		case GFS2_METATYPE_LD:
		 	print_gfs2("(log descriptor)");
			if (sbd.gfs1)
				struct_len = sizeof(struct gfs_log_descriptor);
			else
				struct_len =
					sizeof(struct gfs2_log_descriptor);
			break;
		case GFS2_METATYPE_EA:
			print_gfs2("(extended attr hdr)");
			struct_len = sizeof(struct gfs2_meta_header) +
				sizeof(struct gfs2_ea_header);
			break;
		case GFS2_METATYPE_ED:
			print_gfs2("(extended attr data)");
			struct_len = sizeof(struct gfs2_meta_header) +
				sizeof(struct gfs2_ea_header);
			break;
		case GFS2_METATYPE_LB:
			print_gfs2("(log buffer)");
			struct_len = sizeof(struct gfs2_meta_header);
			break;
		case GFS2_METATYPE_QC:
			print_gfs2("(quota change)");
			struct_len = sizeof(struct gfs2_quota_change);
			break;
		default:
			print_gfs2("(wtf?)");
			struct_len = sbd.bsize;
			break;
		}
	} else
		struct_len = sbd.bsize;
	eol(0);
	if (from_restore)
		return ret_type;
	if (termlines && dmode == HEX_MODE) {
		int type;
		struct rgrp_tree *rgd;

		rgd = gfs2_blk2rgrpd(&sbd, block);
		if (rgd) {
			gfs2_rgrp_read(&sbd, rgd);
			if ((*(bh->b_data + 7) == GFS2_METATYPE_RG) ||
			    (*(bh->b_data + 7) == GFS2_METATYPE_RB))
				type = 4;
			else
				type = gfs2_get_bitmap(&sbd, block, rgd);
		} else
			type = 4;
		screen_chunk_size = ((termlines - 4) * 16) >> 8 << 8;
		if (!screen_chunk_size)
			screen_chunk_size = 256;
		pgnum = (offset / screen_chunk_size);
		print_gfs2("(p.%d of %d--%s)", pgnum + 1,
			   (sbd.bsize % screen_chunk_size) > 0 ?
			   sbd.bsize / screen_chunk_size + 1 : sbd.bsize /
			   screen_chunk_size, allocdesc[sbd.gfs1][type]);
		/*eol(9);*/
		if ((*(bh->b_data + 7) == GFS2_METATYPE_RG)) {
			int ptroffset = edit_row[dmode] * 16 + edit_col[dmode];

			if (ptroffset >= struct_len || pgnum) {
				int blknum, b, btype;

				blknum = pgnum * screen_chunk_size;
				blknum += (ptroffset - struct_len);
				blknum *= 4;
				blknum += rgd->ri.ri_data0;

				print_gfs2(" blk ");
				for (b = blknum; b < blknum + 4; b++) {
					btype = gfs2_get_bitmap(&sbd, b, rgd);
					print_gfs2("0x%x-%s  ", b,
						   allocdesc[sbd.gfs1][btype]);
				}
			}
		} else if ((*(bh->b_data + 7) == GFS2_METATYPE_RB)) {
			int ptroffset = edit_row[dmode] * 16 + edit_col[dmode];

			if (ptroffset >= struct_len || pgnum) {
				int blknum, b, btype, rb_number;

				rb_number = block - rgd->ri.ri_addr;
				blknum = 0;
				/* count the number of bytes representing
				   blocks prior to the displayed screen. */
				for (b = 0; b < rb_number; b++) {
					struct_len = (b ?
					      sizeof(struct gfs2_meta_header) :
					      sizeof(struct gfs2_rgrp));
					blknum += (sbd.bsize - struct_len);
				}
				struct_len = sizeof(struct gfs2_meta_header);
				/* add the number of bytes on this screen */
				blknum += (ptroffset - struct_len);
				/* factor in the page number */
				blknum += pgnum * screen_chunk_size;
				/* convert bytes to blocks */
				blknum *= GFS2_NBBY;
				/* add the starting offset for this rgrp */
				blknum += rgd->ri.ri_data0;
				print_gfs2(" blk ");
				for (b = blknum; b < blknum + 4; b++) {
					btype = gfs2_get_bitmap(&sbd, b, rgd);
					print_gfs2("0x%x-%s  ", b,
						   allocdesc[sbd.gfs1][btype]);
				}
			}
		}
		if (rgd)
			gfs2_rgrp_relse(rgd);
 	}
	if (block == sbd.sd_sb.sb_root_dir.no_addr)
		print_gfs2("--------------- Root directory ------------------");
	else if (!sbd.gfs1 && block == sbd.sd_sb.sb_master_dir.no_addr)
		print_gfs2("-------------- Master directory -----------------");
	else if (!sbd.gfs1 && block == RGLIST_DUMMY_BLOCK)
		print_gfs2("------------------ RG List ----------------------");
	else {
		if (sbd.gfs1) {
			if (block == sbd1->sb_rindex_di.no_addr)
				print_gfs2("---------------- rindex file -------------------");
			else if (block == gfs1_quota_di.no_addr)
				print_gfs2("---------------- Quota file --------------------");
			else if (block == sbd1->sb_jindex_di.no_addr)
				print_gfs2("--------------- Journal Index ------------------");
			else if (block == gfs1_license_di.no_addr)
				print_gfs2("--------------- License file -------------------");
		}
		else {
			int d;

			for (d = 2; d < 8; d++) {
				if (block == masterdir.dirent[d].block) {
					if (!strncmp(masterdir.dirent[d].filename, "jindex", 6))
						print_gfs2("--------------- Journal Index ------------------");
					else if (!strncmp(masterdir.dirent[d].filename, "per_node", 8))
						print_gfs2("--------------- Per-node Dir -------------------");
					else if (!strncmp(masterdir.dirent[d].filename, "inum", 4))
						print_gfs2("---------------- Inum file ---------------------");
					else if (!strncmp(masterdir.dirent[d].filename, "statfs", 6))
						print_gfs2("---------------- statfs file -------------------");
					else if (!strncmp(masterdir.dirent[d].filename, "rindex", 6))
						print_gfs2("---------------- rindex file -------------------");
					else if (!strncmp(masterdir.dirent[d].filename, "quota", 5))
						print_gfs2("---------------- Quota file --------------------");
				}
			}
		}
	}
	eol(0);
	return ret_type;
}

/* ------------------------------------------------------------------------ */
/* hexdump - hex dump the filesystem block to the screen                    */
/* ------------------------------------------------------------------------ */
static int hexdump(uint64_t startaddr, int len)
{
	const unsigned char *pointer,*ptr2;
	int i;
	uint64_t l;
	const char *lpBuffer = bh->b_data;
	int print_field, cursor_line;

	strcpy(edit_fmt,"%02x");
	pointer = (unsigned char *)lpBuffer + offset;
	ptr2 = (unsigned char *)lpBuffer + offset;
	l = offset;
	print_entry_ndx = 0;
	while (((termlines && line < termlines &&
		 line <= ((screen_chunk_size / 16) + 2)) ||
		(!termlines && l < len)) && l < sbd.bsize) {
		int ptr_not_null = 0;

		if (termlines) {
			move(line, 0);
			COLORS_OFFSETS; /* cyan for offsets */
		}
		if (startaddr < 0xffffffff)
			print_gfs2("%.8llx", startaddr + l);
		else
			print_gfs2("%.16llx", startaddr + l);
		if (termlines) {
			if (l < struct_len)
				COLORS_NORMAL; /* normal part of structure */
			else if (gfs2_struct_type == GFS2_METATYPE_DI &&
					 l < struct_len + di.di_size)
				COLORS_CONTENTS; /* after struct but not eof */
			else
				COLORS_SPECIAL; /* beyond end of the struct */
		}
		print_field = -1;
		cursor_line = 0;
		for (i = 0; i < 16; i++) { /* first print it in hex */
			/* Figure out if we have a null pointer--for colors */
			if (((gfs2_struct_type == GFS2_METATYPE_IN) ||
			     (gfs2_struct_type == GFS2_METATYPE_DI &&
			      l < struct_len + di.di_size &&
			      (di.di_height > 0 || !S_ISREG(di.di_mode)))) &&
			    (i==0 || i==8)) {
				int j;

				ptr_not_null = 0;
				for (j = 0; j < 8; j++) {
					if (*(pointer + j)) {
						ptr_not_null = 1;
						break;
					}
				}
			}
			if (termlines) {
				if (l + i < struct_len)
					COLORS_NORMAL; /* in the structure */
				else if (gfs2_struct_type == GFS2_METATYPE_DI
					 && l + i < struct_len + di.di_size) {
					if ((!di.di_height &&
					     S_ISREG(di.di_mode)) ||
					    !ptr_not_null)
						COLORS_CONTENTS;/*stuff data */
					else
						COLORS_SPECIAL;/* non-null */
				}
				else if (gfs2_struct_type == GFS2_METATYPE_IN){
					if (ptr_not_null)
						COLORS_SPECIAL;/* non-null */
					else
						COLORS_CONTENTS;/* null */
				} else
					COLORS_SPECIAL; /* past the struct */
			}
			if (i%4 == 0)
				print_gfs2(" ");
			if (termlines && line == edit_row[dmode] + 3 &&
				i == edit_col[dmode]) {
				COLORS_HIGHLIGHT; /* in the structure */
				memset(estring,0,3);
				sprintf(estring,"%02x",*pointer);
				cursor_line = 1;
				print_field = (char *)pointer - bh->b_data;
			}
			print_gfs2("%02x",*pointer);
			if (termlines && line == edit_row[dmode] + 3 &&
				i == edit_col[dmode]) {
				if (l < struct_len + offset)
					COLORS_NORMAL; /* in the structure */
				else
					COLORS_SPECIAL; /* beyond structure */
			}
			pointer++;
		}
		print_gfs2(" [");
		for (i=0; i<16; i++) { /* now print it in character format */
			if ((*ptr2 >=' ') && (*ptr2 <= '~'))
				print_gfs2("%c",*ptr2);
			else
				print_gfs2(".");
			ptr2++;
		}
		print_gfs2("] ");
		if (print_field >= 0) {
			switch (get_block_type(bh)) {
			case GFS2_METATYPE_SB:   /* 1 */
				print_gfs2(which_fieldname_sb(print_field));
				break;
			case GFS2_METATYPE_RG:   /* 2 */
				print_gfs2(which_fieldname_rgrp(print_field));
				break;
			case GFS2_METATYPE_RB:   /* 3 */
				print_gfs2(which_fieldname_mh(print_field));
				break;
			case GFS2_METATYPE_DI:   /* 4 */
				print_gfs2(which_fieldname_di(print_field));
				break;
			case GFS2_METATYPE_IN:   /* 5 */
				print_gfs2(which_fieldname_mh(print_field));
				break;
			case GFS2_METATYPE_LF:   /* 6 */
				print_gfs2(which_fieldname_lf(print_field));
				break;
			case GFS2_METATYPE_JD:
				print_gfs2(which_fieldname_mh(print_field));
				break;
			case GFS2_METATYPE_LH:
				print_gfs2(which_fieldname_lh(print_field));
				break;
			case GFS2_METATYPE_LD:
				print_gfs2(which_fieldname_ld(print_field));
				break;
			case GFS2_METATYPE_EA:
				break;
			case GFS2_METATYPE_ED:
				break;
			case GFS2_METATYPE_LB:
				break;
			case GFS2_METATYPE_QC:
				break;
			default:
				break;
			}
		}
		if (cursor_line) {
			if (((*(bh->b_data + 7) == GFS2_METATYPE_IN) ||
			   (*(bh->b_data + 7) == GFS2_METATYPE_DI &&
			    (*(bh->b_data + 0x8b) || *(bh->b_data + 0x8a))))) {
				int ptroffset = edit_row[dmode] * 16 +
					edit_col[dmode];

				if (ptroffset >= struct_len || pgnum) {
					int pnum;

					pnum = pgnum * screen_chunk_size;
					pnum += (ptroffset - struct_len);
					pnum /= sizeof(uint64_t);

					print_gfs2("pointer 0x%x", pnum);
				}
			}
		}
		if (line - 3 > last_entry_onscreen[dmode])
			last_entry_onscreen[dmode] = line - 3;
		eol(0);
		l+=16;
		print_entry_ndx++;
	} /* while */
	if (sbd.gfs1) {
		COLORS_NORMAL;
		print_gfs2("         *** This seems to be a GFS-1 file system ***");
		eol(0);
	}
	return (offset+len);
}/* hexdump */

/* ------------------------------------------------------------------------ */
/* masterblock - find a file (by name) in the master directory and return   */
/*               its block number.                                          */
/* ------------------------------------------------------------------------ */
uint64_t masterblock(const char *fn)
{
	int d;
	
	for (d = 2; d < 8; d++)
		if (!strncmp(masterdir.dirent[d].filename, fn, strlen(fn)))
			return (masterdir.dirent[d].block);
	return 0;
}

/* ------------------------------------------------------------------------ */
/* rgcount - return how many rgrps there are.                               */
/* ------------------------------------------------------------------------ */
static void rgcount(void)
{
	printf("%lld RGs in this file system.\n",
	       (unsigned long long)sbd.md.riinode->i_di.di_size /
	       sizeof(struct gfs2_rindex));
	inode_put(&sbd.md.riinode);
	gfs2_rgrp_free(&sbd.rgtree);
	exit(EXIT_SUCCESS);
}

/* ------------------------------------------------------------------------ */
/* find_rgrp_block - locate the block for a given rgrp number               */
/* ------------------------------------------------------------------------ */
static uint64_t find_rgrp_block(struct gfs2_inode *dif, int rg)
{
	int amt;
	struct gfs2_rindex fbuf, ri;
	uint64_t foffset, gfs1_adj = 0;

	foffset = rg * sizeof(struct gfs2_rindex);
	if (sbd.gfs1) {
		uint64_t sd_jbsize =
			(sbd.bsize - sizeof(struct gfs2_meta_header));

		gfs1_adj = (foffset / sd_jbsize) *
			sizeof(struct gfs2_meta_header);
		gfs1_adj += sizeof(struct gfs2_meta_header);
	}
	amt = gfs2_readi(dif, (void *)&fbuf, foffset + gfs1_adj,
			 sizeof(struct gfs2_rindex));
	if (!amt) /* end of file */
		return 0;
	gfs2_rindex_in(&ri, (void *)&fbuf);
	return ri.ri_addr;
}

/* ------------------------------------------------------------------------ */
/* gfs_rgrp_print - print a gfs1 resource group                             */
/* ------------------------------------------------------------------------ */
void gfs_rgrp_print(struct gfs_rgrp *rg)
{
	gfs2_meta_header_print(&rg->rg_header);
	pv(rg, rg_flags, "%u", "0x%x");
	pv(rg, rg_free, "%u", "0x%x");
	pv(rg, rg_useddi, "%u", "0x%x");
	pv(rg, rg_freedi, "%u", "0x%x");
	gfs2_inum_print(&rg->rg_freedi_list);
	pv(rg, rg_usedmeta, "%u", "0x%x");
	pv(rg, rg_freemeta, "%u", "0x%x");
}

/* ------------------------------------------------------------------------ */
/* get_rg_addr                                                              */
/* ------------------------------------------------------------------------ */
static uint64_t get_rg_addr(int rgnum)
{
	uint64_t rgblk = 0, gblock;
	struct gfs2_inode *riinode;

	if (sbd.gfs1)
		gblock = sbd1->sb_rindex_di.no_addr;
	else
		gblock = masterblock("rindex");
	riinode = inode_read(&sbd, gblock);
	if (rgnum < riinode->i_di.di_size / sizeof(struct gfs2_rindex))
		rgblk = find_rgrp_block(riinode, rgnum);
	else
		fprintf(stderr, "Error: File system only has %lld RGs.\n",
			(unsigned long long)riinode->i_di.di_size /
			sizeof(struct gfs2_rindex));
	inode_put(&riinode);
	return rgblk;
}

/* ------------------------------------------------------------------------ */
/* set_rgrp_flags - Set an rgrp's flags to a given value                    */
/* rgnum: which rg to print or modify flags for (0 - X)                     */
/* new_flags: value to set new rg_flags to (if modify == TRUE)              */
/* modify: TRUE if the value is to be modified, FALSE if it's to be printed */
/* full: TRUE if the full RG should be printed.                             */
/* ------------------------------------------------------------------------ */
static void set_rgrp_flags(int rgnum, uint32_t new_flags, int modify, int full)
{
	union {
		struct gfs2_rgrp rg2;
		struct gfs_rgrp rg1;
	} rg;
	struct gfs2_buffer_head *rbh;
	uint64_t rgblk;

	rgblk = get_rg_addr(rgnum);
	rbh = bread(&sbd, rgblk);
	if (sbd.gfs1)
		gfs_rgrp_in(&rg.rg1, rbh);
	else
		gfs2_rgrp_in(&rg.rg2, rbh);
	if (modify) {
		printf("RG #%d (block %llu / 0x%llx) rg_flags changed from 0x%08x to 0x%08x\n",
		       rgnum, (unsigned long long)rgblk,
		       (unsigned long long)rgblk, rg.rg2.rg_flags, new_flags);
		rg.rg2.rg_flags = new_flags;
		if (sbd.gfs1)
			gfs_rgrp_out(&rg.rg1, rbh);
		else
			gfs2_rgrp_out(&rg.rg2, rbh);
		brelse(rbh);
	} else {
		if (full) {
			print_gfs2("RG #%d", rgnum);
			print_gfs2(" located at: %llu (0x%llx)", rgblk, rgblk);
                        eol(0);
			if (sbd.gfs1)
				gfs_rgrp_print(&rg.rg1);
			else
				gfs2_rgrp_print(&rg.rg2);
		}
		else
			printf("RG #%d (block %llu / 0x%llx) rg_flags = 0x%08x\n",
			       rgnum, (unsigned long long)rgblk,
			       (unsigned long long)rgblk, rg.rg2.rg_flags);
		brelse(rbh);
	}
	if (modify)
		fsync(sbd.device_fd);
}

/* ------------------------------------------------------------------------ */
/* has_indirect_blocks                                                      */
/* ------------------------------------------------------------------------ */
int has_indirect_blocks(void)
{
	if (indirect_blocks || gfs2_struct_type == GFS2_METATYPE_SB ||
	    gfs2_struct_type == GFS2_METATYPE_LF ||
	    (gfs2_struct_type == GFS2_METATYPE_DI &&
	     (S_ISDIR(di.di_mode) || (sbd.gfs1 && di.__pad1 == GFS_FILE_DIR))))
		return TRUE;
	return FALSE;
}

/* ------------------------------------------------------------------------ */
/* block_is_rindex                                                          */
/* ------------------------------------------------------------------------ */
int block_is_rindex(void)
{
	if ((sbd.gfs1 && block == sbd1->sb_rindex_di.no_addr) ||
	    (block == masterblock("rindex")))
		return TRUE;
	return FALSE;
}

/* ------------------------------------------------------------------------ */
/* block_is_jindex                                                          */
/* ------------------------------------------------------------------------ */
int block_is_jindex(void)
{
	if ((sbd.gfs1 && block == sbd1->sb_jindex_di.no_addr))
		return TRUE;
	return FALSE;
}

/* ------------------------------------------------------------------------ */
/* block_is_inum_file                                                       */
/* ------------------------------------------------------------------------ */
int block_is_inum_file(void)
{
	if (!sbd.gfs1 && block == masterblock("inum"))
		return TRUE;
	return FALSE;
}

/* ------------------------------------------------------------------------ */
/* block_is_statfs_file                                                     */
/* ------------------------------------------------------------------------ */
int block_is_statfs_file(void)
{
	if (sbd.gfs1 && block == gfs1_license_di.no_addr)
		return TRUE;
	if (!sbd.gfs1 && block == masterblock("statfs"))
		return TRUE;
	return FALSE;
}

/* ------------------------------------------------------------------------ */
/* block_is_quota_file                                                      */
/* ------------------------------------------------------------------------ */
int block_is_quota_file(void)
{
	if (sbd.gfs1 && block == gfs1_quota_di.no_addr)
		return TRUE;
	if (!sbd.gfs1 && block == masterblock("quota"))
		return TRUE;
	return FALSE;
}

/* ------------------------------------------------------------------------ */
/* block_is_per_node                                                        */
/* ------------------------------------------------------------------------ */
int block_is_per_node(void)
{
	if (!sbd.gfs1 && block == masterblock("per_node"))
		return TRUE;
	return FALSE;
}

/* ------------------------------------------------------------------------ */
/* block_is_in_per_node                                                     */
/* ------------------------------------------------------------------------ */
int block_is_in_per_node(void)
{
	int d;
	struct gfs2_inode *per_node_di;

	if (sbd.gfs1)
		return FALSE;

	per_node_di = inode_read(&sbd, masterblock("per_node"));

	do_dinode_extended(&per_node_di->i_di, per_node_di->i_bh);
	inode_put(&per_node_di);

	for (d = 0; d < indirect->ii[0].dirents; d++) {
		if (block == indirect->ii[0].dirent[d].block)
			return TRUE;
	}
	return FALSE;
}

/* ------------------------------------------------------------------------ */
/* block_has_extended_info                                                  */
/* ------------------------------------------------------------------------ */
static int block_has_extended_info(void)
{
	if (has_indirect_blocks() ||
	    block_is_rindex() ||
	    block_is_rgtree() ||
	    block_is_jindex() ||
	    block_is_inum_file() ||
	    block_is_statfs_file() ||
	    block_is_quota_file())
		return TRUE;
	return FALSE;
}

/* ------------------------------------------------------------------------ */
/* read_superblock - read the superblock                                    */
/* ------------------------------------------------------------------------ */
static void read_superblock(int fd)
{
	int count, sane;

	sbd1 = (struct gfs_sb *)&sbd.sd_sb;
	ioctl(fd, BLKFLSBUF, 0);
	memset(&sbd, 0, sizeof(struct gfs2_sbd));
	sbd.bsize = GFS2_DEFAULT_BSIZE;
	sbd.device_fd = fd;
	bh = bread(&sbd, 0x10);
	sbd.jsize = GFS2_DEFAULT_JSIZE;
	sbd.rgsize = GFS2_DEFAULT_RGSIZE;
	sbd.qcsize = GFS2_DEFAULT_QCSIZE;
	sbd.time = time(NULL);
	sbd.rgtree.osi_node = NULL;
	gfs2_sb_in(&sbd.sd_sb, bh); /* parse it out into the sb structure */
	/* Check to see if this is really gfs1 */
	if (sbd1->sb_fs_format == GFS_FORMAT_FS &&
		sbd1->sb_header.mh_type == GFS_METATYPE_SB &&
		sbd1->sb_header.mh_format == GFS_FORMAT_SB &&
		sbd1->sb_multihost_format == GFS_FORMAT_MULTI) {
		struct gfs_sb *sbbuf = (struct gfs_sb *)bh->b_data;

		sbd.gfs1 = TRUE;
		sbd1->sb_flags = be32_to_cpu(sbbuf->sb_flags);
		sbd1->sb_seg_size = be32_to_cpu(sbbuf->sb_seg_size);
		gfs2_inum_in(&sbd1->sb_rindex_di, (void *)&sbbuf->sb_rindex_di);
		gfs2_inum_in(&gfs1_quota_di, (void *)&sbbuf->sb_quota_di);
		gfs2_inum_in(&gfs1_license_di, (void *)&sbbuf->sb_license_di);
	}
	else
		sbd.gfs1 = FALSE;
	sbd.bsize = sbd.sd_sb.sb_bsize;
	if (!sbd.bsize)
		sbd.bsize = GFS2_DEFAULT_BSIZE;
	if (lgfs2_get_dev_info(fd, &sbd.dinfo)) {
		perror(sbd.device_name);
		exit(-1);
	}
	compute_constants(&sbd);
	if (sbd.gfs1 || (sbd.sd_sb.sb_header.mh_magic == GFS2_MAGIC &&
		     sbd.sd_sb.sb_header.mh_type == GFS2_METATYPE_SB))
		block = 0x10 * (GFS2_DEFAULT_BSIZE / sbd.bsize);
	else {
		block = starting_blk = 0;
	}
	fix_device_geometry(&sbd);
	if(sbd.gfs1) {
		sbd.sd_inptrs = (sbd.bsize - sizeof(struct gfs_indirect)) /
			sizeof(uint64_t);
		sbd.sd_diptrs = (sbd.bsize - sizeof(struct gfs_dinode)) /
			sizeof(uint64_t);
		sbd.md.riinode = inode_read(&sbd, sbd1->sb_rindex_di.no_addr);
	} else {
		sbd.sd_inptrs = (sbd.bsize - sizeof(struct gfs2_meta_header)) /
			sizeof(uint64_t);
		sbd.sd_diptrs = (sbd.bsize - sizeof(struct gfs2_dinode)) /
			sizeof(uint64_t);
		sbd.master_dir = inode_read(&sbd,
					    sbd.sd_sb.sb_master_dir.no_addr);
		gfs2_lookupi(sbd.master_dir, "rindex", 6, &sbd.md.riinode);
	}
	sbd.fssize = sbd.device.length;
	if (sbd.md.riinode) /* If we found the rindex */
		rindex_read(&sbd, 0, &count, &sane);
}

/* ------------------------------------------------------------------------ */
/* read_master_dir - read the master directory                              */
/* ------------------------------------------------------------------------ */
static void read_master_dir(void)
{
	ioctl(sbd.device_fd, BLKFLSBUF, 0);
	lseek(sbd.device_fd, sbd.sd_sb.sb_master_dir.no_addr * sbd.bsize,
	      SEEK_SET);
	if (read(sbd.device_fd, bh->b_data, sbd.bsize) != sbd.bsize) {
		fprintf(stderr, "read error: %s from %s:%d: "
			"master dir block %lld (0x%llx)\n",
			strerror(errno), __FUNCTION__,
			__LINE__,
			(unsigned long long)sbd.sd_sb.sb_master_dir.no_addr,
			(unsigned long long)sbd.sd_sb.sb_master_dir.no_addr);
		exit(-1);
	}
	gfs2_dinode_in(&di, bh); /* parse disk inode into structure */
	do_dinode_extended(&di, bh); /* get extended data, if any */
	memcpy(&masterdir, &indirect[0], sizeof(struct indirect_info));
}

/* ------------------------------------------------------------------------ */
/* display                                                                  */
/* ------------------------------------------------------------------------ */
int display(int identify_only)
{
	uint64_t blk;

	if (block == RGLIST_DUMMY_BLOCK) {
		if (sbd.gfs1)
			blk = sbd1->sb_rindex_di.no_addr;
		else
			blk = masterblock("rindex");
	} else
		blk = block;
	if (termlines) {
		display_title_lines();
		move(2,0);
	}
	if (block_in_mem != blk) { /* If we changed blocks from the last read */
		dev_offset = blk * sbd.bsize;
		ioctl(sbd.device_fd, BLKFLSBUF, 0);
		if (!(bh = bread(&sbd, blk))) {
			fprintf(stderr, "read error: %s from %s:%d: "
				"offset %lld (0x%llx)\n",
				strerror(errno), __FUNCTION__, __LINE__,
				(unsigned long long)dev_offset,
				(unsigned long long)dev_offset);
			exit(-1);
		}
		block_in_mem = blk; /* remember which block is in memory */
	}
	line = 1;
	gfs2_struct_type = display_block_type(FALSE);
	if (identify_only)
		return 0;
	indirect_blocks = 0;
	lines_per_row[dmode] = 1;
	if (gfs2_struct_type == GFS2_METATYPE_SB || blk == 0x10 * (4096 / sbd.bsize)) {
		gfs2_sb_in(&sbd.sd_sb, bh); /* parse it out into the sb structure */
		memset(indirect, 0, sizeof(struct iinfo));
		indirect->ii[0].block = sbd.sd_sb.sb_master_dir.no_addr;
		indirect->ii[0].is_dir = TRUE;
		indirect->ii[0].dirents = 2;

		memcpy(&indirect->ii[0].dirent[0].filename, "root", 4);
		indirect->ii[0].dirent[0].dirent.de_inum.no_formal_ino =
			sbd.sd_sb.sb_root_dir.no_formal_ino;
		indirect->ii[0].dirent[0].dirent.de_inum.no_addr =
			sbd.sd_sb.sb_root_dir.no_addr;
		indirect->ii[0].dirent[0].block = sbd.sd_sb.sb_root_dir.no_addr;
		indirect->ii[0].dirent[0].dirent.de_type = DT_DIR;

		memcpy(&indirect->ii[0].dirent[1].filename, "master", 7);
		indirect->ii[0].dirent[1].dirent.de_inum.no_formal_ino = 
			sbd.sd_sb.sb_master_dir.no_formal_ino;
		indirect->ii[0].dirent[1].dirent.de_inum.no_addr =
			sbd.sd_sb.sb_master_dir.no_addr;
		indirect->ii[0].dirent[1].block = sbd.sd_sb.sb_master_dir.no_addr;
		indirect->ii[0].dirent[1].dirent.de_type = DT_DIR;
	}
	else if (gfs2_struct_type == GFS2_METATYPE_DI) {
		gfs2_dinode_in(&di, bh); /* parse disk inode into structure */
		do_dinode_extended(&di, bh); /* get extended data, if any */
	}
	else if (gfs2_struct_type == GFS2_METATYPE_IN) { /* indirect block list */
		if (blockhist) {
			int i;

			for (i = 0; i < 512; i++)
				memcpy(&indirect->ii[i].mp,
				       &blockstack[blockhist - 1].mp,
				       sizeof(struct metapath));
		}
		indirect_blocks = do_indirect_extended(bh->b_data, indirect);
	}
	else if (gfs2_struct_type == GFS2_METATYPE_LF) { /* directory leaf */
		do_leaf_extended(bh->b_data, indirect);
	}
	last_entry_onscreen[dmode] = 0;
	if (dmode == EXTENDED_MODE && !block_has_extended_info())
		dmode = HEX_MODE;
	if (termlines) {
		move(termlines, 63);
		if (dmode==HEX_MODE)
			printw("Mode: Hex %s", (editing?"edit ":"view "));
		else
			printw("Mode: %s", (dmode==GFS2_MODE?"Structure":
					    "Pointers "));
		move(line, 0);
	}
	if (dmode == HEX_MODE)          /* if hex display mode           */
		hexdump(dev_offset, (gfs2_struct_type == GFS2_METATYPE_DI)?
			struct_len + di.di_size:sbd.bsize);
	else if (dmode == GFS2_MODE)    /* if structure display          */
		display_gfs2();            /* display the gfs2 structure    */
	else
		display_extended();        /* display extended blocks       */
	/* No else here because display_extended can switch back to hex mode */
	if (termlines)
		refresh();
	return(0);
}

/* ------------------------------------------------------------------------ */
/* push_block - push a block onto the block stack                           */
/* ------------------------------------------------------------------------ */
static void push_block(uint64_t blk)
{
	int i, bhst;

	bhst = blockhist % BLOCK_STACK_SIZE;
	if (blk) {
		blockstack[bhst].dmode = dmode;
		for (i = 0; i < DMODES; i++) {
			blockstack[bhst].start_row[i] = start_row[i];
			blockstack[bhst].end_row[i] = end_row[i];
			blockstack[bhst].edit_row[i] = edit_row[i];
			blockstack[bhst].edit_col[i] = edit_col[i];
			blockstack[bhst].lines_per_row[i] = lines_per_row[i];
		}
		blockstack[bhst].gfs2_struct_type = gfs2_struct_type;
		if (edit_row[dmode] >= 0 && !block_is_rindex())
			memcpy(&blockstack[bhst].mp,
			       &indirect->ii[edit_row[dmode]].mp,
			       sizeof(struct metapath));
		blockhist++;
		blockstack[blockhist % BLOCK_STACK_SIZE].block = blk;
	}
}

/* ------------------------------------------------------------------------ */
/* pop_block - pop a block off the block stack                              */
/* ------------------------------------------------------------------------ */
static uint64_t pop_block(void)
{
	int i, bhst;

	if (!blockhist)
		return block;
	blockhist--;
	bhst = blockhist % BLOCK_STACK_SIZE;
	dmode = blockstack[bhst].dmode;
	for (i = 0; i < DMODES; i++) {
		start_row[i] = blockstack[bhst].start_row[i];
		end_row[i] = blockstack[bhst].end_row[i];
		edit_row[i] = blockstack[bhst].edit_row[i];
		edit_col[i] = blockstack[bhst].edit_col[i];
		lines_per_row[i] = blockstack[bhst].lines_per_row[i];
	}
	gfs2_struct_type = blockstack[bhst].gfs2_struct_type;
	return blockstack[bhst].block;
}

/* ------------------------------------------------------------------------ */
/* find_journal_block - figure out where a journal starts, given the name   */
/* Returns: journal block number, changes j_size to the journal size        */
/* ------------------------------------------------------------------------ */
static uint64_t find_journal_block(const char *journal, uint64_t *j_size)
{
	int journal_num;
	uint64_t jindex_block, jblock = 0;
	int amtread;
	struct gfs2_buffer_head *jindex_bh, *j_bh;
	char jbuf[sbd.bsize];
	struct gfs2_inode *j_inode = NULL;

	journal_num = atoi(journal + 7);
	/* Figure out the block of the jindex file */
	if (sbd.gfs1)
		jindex_block = sbd1->sb_jindex_di.no_addr;
	else
		jindex_block = masterblock("jindex");
	/* read in the block */
	jindex_bh = bread(&sbd, jindex_block);
	/* get the dinode data from it. */
	gfs2_dinode_in(&di, jindex_bh); /* parse disk inode to struct*/

	if (!sbd.gfs1)
		do_dinode_extended(&di, jindex_bh); /* parse dir. */

	if (sbd.gfs1) {
		struct gfs2_inode *jiinode;
		struct gfs_jindex ji;

		jiinode = inode_get(&sbd, jindex_bh);
		amtread = gfs2_readi(jiinode, (void *)&jbuf,
				   journal_num * sizeof(struct gfs_jindex),
				   sizeof(struct gfs_jindex));
		if (amtread) {
			gfs_jindex_in(&ji, jbuf);
			jblock = ji.ji_addr;
			*j_size = ji.ji_nsegment * 0x10;
		}
	} else {
		struct gfs2_dinode jdi;

		jblock = indirect->ii[0].dirent[journal_num + 2].block;
		j_bh = bread(&sbd, jblock);
		j_inode = inode_get(&sbd, j_bh);
		gfs2_dinode_in(&jdi, j_bh);/* parse dinode to struct */
		*j_size = jdi.di_size;
		brelse(j_bh);
	}
	brelse(jindex_bh);
	return jblock;
}

/* ------------------------------------------------------------------------ */
/* Find next metadata block of a given type AFTER a given point in the fs   */
/*                                                                          */
/* This is used to find blocks that aren't represented in the bitmaps, such */
/* as the RGs and bitmaps or the superblock.                                */
/* ------------------------------------------------------------------------ */
static uint64_t find_metablockoftype_slow(uint64_t startblk, int metatype, int print)
{
	uint64_t blk, last_fs_block;
	int found = 0;
	struct gfs2_buffer_head *lbh;

	last_fs_block = lseek(sbd.device_fd, 0, SEEK_END) / sbd.bsize;
	for (blk = startblk + 1; blk < last_fs_block; blk++) {
		lbh = bread(&sbd, blk);
		/* Can't use get_block_type here (returns false "none") */
		if (lbh->b_data[0] == 0x01 && lbh->b_data[1] == 0x16 &&
		    lbh->b_data[2] == 0x19 && lbh->b_data[3] == 0x70 &&
		    lbh->b_data[4] == 0x00 && lbh->b_data[5] == 0x00 &&
		    lbh->b_data[6] == 0x00 && lbh->b_data[7] == metatype) {
			found = 1;
			brelse(lbh);
			break;
		}
		brelse(lbh);
	}
	if (!found)
		blk = 0;
	if (print) {
		if (dmode == HEX_MODE)
			printf("0x%llx\n", (unsigned long long)blk);
		else
			printf("%llu\n", (unsigned long long)blk);
	}
	gfs2_rgrp_free(&sbd.rgtree);
	if (print)
		exit(0);
	return blk;
}

/* ------------------------------------------------------------------------ */
/* Find next "metadata in use" block AFTER a given point in the fs          */
/*                                                                          */
/* This version does its magic by searching the bitmaps of the RG.  After   */
/* all, if we're searching for a dinode, we want a real allocated inode,    */
/* not just some block that used to be an inode in a previous incarnation.  */
/* ------------------------------------------------------------------------ */
static uint64_t find_metablockoftype_rg(uint64_t startblk, int metatype, int print)
{
	struct osi_node *n, *next = NULL;
	uint64_t blk, errblk;
	int first = 1, found = 0;
	struct rgrp_tree *rgd;
	struct gfs2_rindex *ri;

	blk = 0;
	/* Skip the rgs prior to the block we've been given */
	for (n = osi_first(&sbd.rgtree); n; n = next) {
		next = osi_next(n);
		rgd = (struct rgrp_tree *)n;
		ri = &rgd->ri;
		if (first && startblk <= ri->ri_data0) {
			startblk = ri->ri_data0;
			break;
		} else if (ri->ri_addr <= startblk &&
			 startblk < ri->ri_data0 + ri->ri_data)
			break;
		else
			rgd = NULL;
		first = 0;
	}
	if (!rgd) {
		if (print)
			printf("0\n");
		gfs2_rgrp_free(&sbd.rgtree);
		if (print)
			exit(-1);
	}
	for (; !found && n; n = next){
		next = osi_next(n);
		rgd = (struct rgrp_tree *)n;
		errblk = gfs2_rgrp_read(&sbd, rgd);
		if (errblk)
			continue;
		first = 1;
		do {
			if (gfs2_next_rg_metatype(&sbd, rgd, &blk, metatype,
						  first))
				break;
			if (blk > startblk) {
				found = 1;
				break;
			}
			first = 0;
		} while (blk <= startblk);
		gfs2_rgrp_relse(rgd);
	}
	if (!found)
		blk = 0;
	if (print) {
		if (dmode == HEX_MODE)
			printf("0x%llx\n", (unsigned long long)blk);
		else
			printf("%llu\n", (unsigned long long)blk);
	}
	gfs2_rgrp_free(&sbd.rgtree);
	if (print)
		exit(0);
	return blk;
}

/* ------------------------------------------------------------------------ */
/* Find next metadata block AFTER a given point in the fs                   */
/* ------------------------------------------------------------------------ */
static uint64_t find_metablockoftype(const char *strtype, int print)
{
	int mtype = 0;
	uint64_t startblk, blk = 0;

	if (print)
		startblk = blockstack[blockhist % BLOCK_STACK_SIZE].block;
	else
		startblk = block;

	for (mtype = GFS2_METATYPE_NONE;
	     mtype <= GFS2_METATYPE_QC; mtype++)
		if (!strcasecmp(strtype, mtypes[mtype]))
			break;
	if (!strcmp(strtype, "dinode"))
		mtype = GFS2_METATYPE_DI;
	if (mtype >= GFS2_METATYPE_NONE && mtype <= GFS2_METATYPE_RB)
		blk = find_metablockoftype_slow(startblk, mtype, print);
	else if (mtype >= GFS2_METATYPE_DI && mtype <= GFS2_METATYPE_QC)
		blk = find_metablockoftype_rg(startblk, mtype, print);
	else if (print) {
		fprintf(stderr, "Error: metadata type not "
			"specified: must be one of:\n");
		fprintf(stderr, "sb rg rb di in lf jd lh ld"
			" ea ed lb 13 qc\n");
		gfs2_rgrp_free(&sbd.rgtree);
		exit(-1);
	}
	return blk;
}

/* ------------------------------------------------------------------------ */
/* Check if the word is a keyword such as "sb" or "rindex"                  */
/* Returns: block number if it is, else 0                                   */
/* ------------------------------------------------------------------------ */
uint64_t check_keywords(const char *kword)
{
	unsigned long long blk = 0;

	if (!strcmp(kword, "sb") ||!strcmp(kword, "superblock"))
		blk = 0x10 * (4096 / sbd.bsize); /* superblock */
	else if (!strcmp(kword, "root") || !strcmp(kword, "rootdir"))
		blk = sbd.sd_sb.sb_root_dir.no_addr;
	else if (!strcmp(kword, "master")) {
		if (!sbd.gfs1)
			blk = sbd.sd_sb.sb_master_dir.no_addr;
		else
			fprintf(stderr, "This is GFS1; there's no master directory.\n");
	}
	else if (!strcmp(kword, "jindex")) {
		if (sbd.gfs1)
			blk = sbd1->sb_jindex_di.no_addr;
		else
			blk = masterblock("jindex"); /* journal index */
	}
	else if (!sbd.gfs1 && !strcmp(kword, "per_node"))
		blk = masterblock("per_node");
	else if (!sbd.gfs1 && !strcmp(kword, "inum"))
		blk = masterblock("inum");
	else if (!strcmp(kword, "statfs")) {
		if (sbd.gfs1)
			blk = gfs1_license_di.no_addr;
		else
			blk = masterblock("statfs");
	}
	else if (!strcmp(kword, "rindex") || !strcmp(kword, "rgindex")) {
		if (sbd.gfs1)
			blk = sbd1->sb_rindex_di.no_addr;
		else
			blk = masterblock("rindex");
	} else if (!strcmp(kword, "rgs")) {
		blk = RGLIST_DUMMY_BLOCK;
	} else if (!strcmp(kword, "quota")) {
		if (sbd.gfs1)
			blk = gfs1_quota_di.no_addr;
		else
			blk = masterblock("quota");
	} else if (!strncmp(kword, "rg ", 3)) {
		int rgnum = 0;

		rgnum = atoi(kword + 3);
		blk = get_rg_addr(rgnum);
	} else if (!strncmp(kword, "journal", 7) && isdigit(kword[7])) {
		uint64_t j_size;

		blk = find_journal_block(kword, &j_size);
	} else if (kword[0]=='/') /* search */
		blk = find_metablockoftype(&kword[1], 0);
	else if (kword[0]=='0' && kword[1]=='x') /* hex addr */
		sscanf(kword, "%llx", &blk);/* retrieve in hex */
	else
		sscanf(kword, "%llu", &blk); /* retrieve decimal */

	return blk;
}

/* ------------------------------------------------------------------------ */
/* goto_block - go to a desired block entered by the user                   */
/* ------------------------------------------------------------------------ */
static uint64_t goto_block(void)
{
	char string[256];
	int ch, delta;

	memset(string, 0, sizeof(string));
	sprintf(string,"%lld", (long long)block);
	if (bobgets(string, 1, 7, 16, &ch)) {
		if (isalnum(string[0]) || string[0] == '/')
			temp_blk = check_keywords(string);
		else if (string[0] == '+' || string[0] == '-') {
			if (string[1] == '0' && string[2] == 'x')
				sscanf(string, "%x", &delta);
			else
				sscanf(string, "%d", &delta);
			temp_blk = block + delta;
		}

		if (temp_blk == RGLIST_DUMMY_BLOCK || temp_blk < max_block) {
			offset = 0;
			block = temp_blk;
			push_block(block);
		}
	}
	return block;
}

/* ------------------------------------------------------------------------ */
/* init_colors                                                              */
/* ------------------------------------------------------------------------ */
static void init_colors(void)
{

	if (color_scheme) {
		init_pair(COLOR_TITLE, COLOR_BLACK,  COLOR_CYAN);
		init_pair(COLOR_NORMAL, COLOR_WHITE,  COLOR_BLACK);
		init_pair(COLOR_INVERSE, COLOR_BLACK,  COLOR_WHITE);
		init_pair(COLOR_SPECIAL, COLOR_RED,    COLOR_BLACK);
		init_pair(COLOR_HIGHLIGHT, COLOR_GREEN, COLOR_BLACK);
		init_pair(COLOR_OFFSETS, COLOR_CYAN,   COLOR_BLACK);
		init_pair(COLOR_CONTENTS, COLOR_YELLOW, COLOR_BLACK);
	}
	else {
		init_pair(COLOR_TITLE, COLOR_BLACK,  COLOR_CYAN);
		init_pair(COLOR_NORMAL, COLOR_BLACK,  COLOR_WHITE);
		init_pair(COLOR_INVERSE, COLOR_WHITE,  COLOR_BLACK);
		init_pair(COLOR_SPECIAL, COLOR_MAGENTA, COLOR_WHITE);
		init_pair(COLOR_HIGHLIGHT, COLOR_RED, COLOR_WHITE); /*cursor*/
		init_pair(COLOR_OFFSETS, COLOR_CYAN,   COLOR_WHITE);
		init_pair(COLOR_CONTENTS, COLOR_BLUE, COLOR_WHITE);
	}
}

/* ------------------------------------------------------------------------ */
/* hex_edit - Allow the user to edit the page by entering hex digits        */
/* ------------------------------------------------------------------------ */
static void hex_edit(int *exitch)
{
	int left_off;
	int ch;

	left_off = ((block * sbd.bsize) < 0xffffffff) ? 9 : 17;
	/* 8 and 16 char addresses on screen */
	
	if (bobgets(estring, edit_row[HEX_MODE] + 3,
		    (edit_col[HEX_MODE] * 2) + (edit_col[HEX_MODE] / 4) +
		    left_off, 2, exitch)) {
		if (strstr(edit_fmt,"X") || strstr(edit_fmt,"x")) {
			int hexoffset;
			int i, sl = strlen(estring);
			
			for (i = 0; i < sl; i+=2) {
				hexoffset = (edit_row[HEX_MODE] * 16) +
					edit_col[HEX_MODE] + (i / 2);
				ch = 0x00;
				if (isdigit(estring[i]))
					ch = (estring[i] - '0') * 0x10;
				else if (estring[i] >= 'a' &&
					 estring[i] <= 'f')
					ch = (estring[i]-'a' + 0x0a)*0x10;
				else if (estring[i] >= 'A' &&
					 estring[i] <= 'F')
					ch = (estring[i] - 'A' + 0x0a) * 0x10;
				if (isdigit(estring[i+1]))
					ch += (estring[i+1] - '0');
				else if (estring[i+1] >= 'a' &&
					 estring[i+1] <= 'f')
					ch += (estring[i+1] - 'a' + 0x0a);
				else if (estring[i+1] >= 'A' &&
					 estring[i+1] <= 'F')
					ch += (estring[i+1] - 'A' + 0x0a);
				bh->b_data[offset + hexoffset] = ch;
			}
			lseek(sbd.device_fd, dev_offset, SEEK_SET);
			if (write(sbd.device_fd, bh->b_data, sbd.bsize) !=
			    sbd.bsize) {
				fprintf(stderr, "write error: %s from %s:%d: "
					"offset %lld (0x%llx)\n",
					strerror(errno),
					__FUNCTION__, __LINE__,
					(unsigned long long)dev_offset,
					(unsigned long long)dev_offset);
				exit(-1);
			}
			fsync(sbd.device_fd);
		}
	}
}

/* ------------------------------------------------------------------------ */
/* page up                                                                  */
/* ------------------------------------------------------------------------ */
static void pageup(void)
{
	if (dmode == EXTENDED_MODE) {
		if (edit_row[dmode] - (dsplines / lines_per_row[dmode]) > 0)
			edit_row[dmode] -= (dsplines / lines_per_row[dmode]);
		else
			edit_row[dmode] = 0;
		if (start_row[dmode] - (dsplines / lines_per_row[dmode]) > 0)
			start_row[dmode] -= (dsplines / lines_per_row[dmode]);
		else
			start_row[dmode] = 0;
	}
	else {
		start_row[dmode] = edit_row[dmode] = 0;
		if (dmode == GFS2_MODE || offset==0) {
			block--;
			if (dmode == HEX_MODE)
				offset = (sbd.bsize % screen_chunk_size) > 0 ?
					screen_chunk_size *
					(sbd.bsize / screen_chunk_size) :
					sbd.bsize - screen_chunk_size;
			else
				offset = 0;
		} else
			offset -= screen_chunk_size;
	}
}

/* ------------------------------------------------------------------------ */
/* page down                                                                */
/* ------------------------------------------------------------------------ */
static void pagedn(void)
{
	if (dmode == EXTENDED_MODE) {
		if ((edit_row[dmode] + dsplines) / lines_per_row[dmode] + 1 <=
		    end_row[dmode]) {
			start_row[dmode] += dsplines / lines_per_row[dmode];
			edit_row[dmode] += dsplines / lines_per_row[dmode];
		} else {
			edit_row[dmode] = end_row[dmode] - 1;
			while (edit_row[dmode] - start_row[dmode]
			       + 1 > last_entry_onscreen[dmode])
				start_row[dmode]++;
		}
	}
	else {
		start_row[dmode] = edit_row[dmode] = 0;
		if (dmode == GFS2_MODE ||
		    offset + screen_chunk_size >= sbd.bsize) {
			block++;
			offset = 0;
		} else
			offset += screen_chunk_size;
	}
}

/* ------------------------------------------------------------------------ */
/* jump - jump to the address the cursor is on                              */
/* ------------------------------------------------------------------------ */
static void jump(void)
{
	if (dmode == HEX_MODE) {
		unsigned int col2;
		uint64_t *b;
		
		if (edit_row[dmode] >= 0) {
			col2 = edit_col[dmode] & 0x08;/* thus 0-7->0, 8-15->8 */
			b = (uint64_t *)&bh->b_data[edit_row[dmode]*16 +
						    offset + col2];
			temp_blk=be64_to_cpu(*b);
		}
	}
	else
		sscanf(estring, "%"SCNx64, &temp_blk);/* retrieve in hex */
	if (temp_blk < max_block) { /* if the block number is valid */
		int i;
		
		offset = 0;
		push_block(temp_blk);
		block = temp_blk;
		for (i = 0; i < DMODES; i++) {
			start_row[i] = end_row[i] = edit_row[i] = 0;
			edit_col[i] = 0;
		}
	}
}

/* ------------------------------------------------------------------------ */
/* print block type                                                         */
/* ------------------------------------------------------------------------ */
static void print_block_type(uint64_t tblock, int type, const char *additional)
{
	if (type <= GFS2_METATYPE_QC)
		printf("%d (Block %lld is type %d: %s%s)\n", type,
		       (unsigned long long)tblock, type, block_type_str[type],
		       additional);
	else
		printf("%d (Block %lld is type %d: unknown%s)\n", type,
		       (unsigned long long)tblock, type, additional);
}

/* ------------------------------------------------------------------------ */
/* find_print block type                                                    */
/* ------------------------------------------------------------------------ */
static void find_print_block_type(void)
{
	uint64_t tblock;
	struct gfs2_buffer_head *lbh;
	int type;

	tblock = blockstack[blockhist % BLOCK_STACK_SIZE].block;
	lbh = bread(&sbd, tblock);
	type = get_block_type(lbh);
	print_block_type(tblock, type, "");
	brelse(lbh);
	gfs2_rgrp_free(&sbd.rgtree);
	exit(0);
}

/* ------------------------------------------------------------------------ */
/* Find and print the resource group associated with a given block          */
/* ------------------------------------------------------------------------ */
static void find_print_block_rg(int bitmap)
{
	uint64_t rblock, rgblock;
	int i;
	struct rgrp_tree *rgd;

	rblock = blockstack[blockhist % BLOCK_STACK_SIZE].block;
	if (rblock == sbd.sb_addr)
		printf("0 (the superblock is not in the bitmap)\n");
	else {
		rgd = gfs2_blk2rgrpd(&sbd, rblock);
		if (rgd) {
			rgblock = rgd->ri.ri_addr;
			if (bitmap) {
				struct gfs2_bitmap *bits = NULL;

				for (i = 0; i < rgd->ri.ri_length; i++) {
					bits = &(rgd->bits[i]);
					if (rblock - rgd->ri.ri_data0 <
					    ((bits->bi_start + bits->bi_len) *
					     GFS2_NBBY)) {
						break;
					}
				}
				if (i < rgd->ri.ri_length)
					rgblock += i;

			}
			if (dmode == HEX_MODE)
				printf("0x%llx\n",(unsigned long long)rgblock);
			else
				printf("%llu\n", (unsigned long long)rgblock);
		} else {
			printf("-1 (block invalid or part of an rgrp).\n");
		}
	}
	gfs2_rgrp_free(&sbd.rgtree);
	exit(0);
}

/* ------------------------------------------------------------------------ */
/* find/change/print block allocation (what the bitmap says about block)    */
/* ------------------------------------------------------------------------ */
static void find_change_block_alloc(int *newval)
{
	uint64_t ablock;
	int type;
	struct rgrp_tree *rgd;

	if (newval &&
	    (*newval < GFS2_BLKST_FREE || *newval > GFS2_BLKST_DINODE)) {
		int i;

		printf("Error: value %d is not valid.\nValid values are:\n",
		       *newval);
		for (i = GFS2_BLKST_FREE; i <= GFS2_BLKST_DINODE; i++)
			printf("%d - %s\n", i, allocdesc[sbd.gfs1][i]);
		gfs2_rgrp_free(&sbd.rgtree);
		exit(-1);
	}
	ablock = blockstack[blockhist % BLOCK_STACK_SIZE].block;
	if (ablock == sbd.sb_addr)
		printf("3 (the superblock is not in the bitmap)\n");
	else {
		rgd = gfs2_blk2rgrpd(&sbd, ablock);
		if (rgd) {
			gfs2_rgrp_read(&sbd, rgd);
			if (newval) {
				if (gfs2_set_bitmap(&sbd, ablock, *newval))
					printf("-1 (block invalid or part of an rgrp).\n");
				else
					printf("%d\n", *newval);
			} else {
				type = gfs2_get_bitmap(&sbd, ablock, rgd);
				if (type < 0) {
					printf("-1 (block invalid or part of "
					       "an rgrp).\n");
					exit(-1);
				}
				printf("%d (%s)\n", type, allocdesc[sbd.gfs1][type]);
			}
			gfs2_rgrp_relse(rgd);
		} else {
			gfs2_rgrp_free(&sbd.rgtree);
			printf("-1 (block invalid or part of an rgrp).\n");
			exit(-1);
		}
	}
	gfs2_rgrp_free(&sbd.rgtree);
	if (newval)
		fsync(sbd.device_fd);
	exit(0);
}

/* ------------------------------------------------------------------------ */
/* process request to print a certain field from a previously pushed block  */
/* ------------------------------------------------------------------------ */
static void process_field(const char *field, const char *nstr)
{
	uint64_t fblock;
	struct gfs2_buffer_head *rbh;
	int type;
	struct gfs2_rgrp rg;
	struct gfs2_leaf leaf;
	struct gfs2_sb lsb;
	struct gfs2_log_header lh;
	struct gfs2_log_descriptor ld;
	struct gfs2_quota_change qc;
	int setval = 0, setstring = 0;
	uint64_t newval = 0;

	if (nstr[0] == '/') {
		setval = 0;
	} else if (nstr[0] == '0' && nstr[1] == 'x') {
		sscanf(nstr, "%"SCNx64, &newval);
		setval = 1;
	} else {
		newval = (uint64_t)atoll(nstr);
		setval = 1;
	}
	if (setval && newval == 0 && nstr[0] != '0')
		setstring = 1;
	fblock = blockstack[blockhist % BLOCK_STACK_SIZE].block;
	rbh = bread(&sbd, fblock);
	type = get_block_type(rbh);
	switch (type) {
	case GFS2_METATYPE_SB:
		gfs2_sb_in(&lsb, rbh);
		if (setval) {
			if (setstring)
				gfs2_sb_assigns(&lsb, field, nstr);
			else
				gfs2_sb_assignval(&lsb, field, newval);
			gfs2_sb_out(&lsb, rbh);
			if (!termlines)
				gfs2_sb_printval(&lsb, field);
		} else {
			if (!termlines && gfs2_sb_printval(&lsb, field))
				printf("Field '%s' not found.\n", field);
		}
		break;
	case GFS2_METATYPE_RG:
		gfs2_rgrp_in(&rg, rbh);
		if (setval) {
			gfs2_rgrp_assignval(&rg, field, newval);
			gfs2_rgrp_out(&rg, rbh);
			if (!termlines)
				gfs2_rgrp_printval(&rg, field);
		} else {
			if (!termlines && gfs2_rgrp_printval(&rg, field))
				printf("Field '%s' not found.\n", field);
		}
		break;
	case GFS2_METATYPE_RB:
		if (!termlines)
			print_block_type(fblock, type,
					 " which is not implemented");
		break;
	case GFS2_METATYPE_DI:
		gfs2_dinode_in(&di, rbh);
		if (setval) {
			gfs2_dinode_assignval(&di, field, newval);
			gfs2_dinode_out(&di, rbh);
			if (!termlines)
				gfs2_dinode_printval(&di, field);
		} else {
			if (!termlines && gfs2_dinode_printval(&di, field))
				printf("Field '%s' not found.\n", field);
		}
		break;
	case GFS2_METATYPE_IN:
		if (!setval && !setstring)
			print_block_type(fblock, type,
					 " which is not implemented");
		break;
	case GFS2_METATYPE_LF:
		gfs2_leaf_in(&leaf, rbh);
		if (setval) {
			if (setstring)
				gfs2_leaf_assigns(&leaf, field, nstr);
			else
				gfs2_leaf_assignval(&leaf, field, newval);
			gfs2_leaf_out(&leaf, rbh);
			if (!termlines)
				gfs2_leaf_printval(&leaf, field);
		} else {
			if (!termlines && gfs2_leaf_printval(&leaf, field))
				printf("Field '%s' not found.\n", field);
		}
		break;
	case GFS2_METATYPE_LH:
		gfs2_log_header_in(&lh, rbh);
		if (setval) {
			gfs2_lh_assignval(&lh, field, newval);
			gfs2_log_header_out(&lh, rbh);
			if (!termlines)
				gfs2_lh_printval(&lh, field);
		} else {
			if (!termlines && gfs2_lh_printval(&lh, field))
				printf("Field '%s' not found.\n", field);
		}
		break;
	case GFS2_METATYPE_LD:
		gfs2_log_descriptor_in(&ld, rbh);
		if (setval) {
			if (setstring)
				gfs2_ld_assigns(&ld, field, nstr);
			else
				gfs2_ld_assignval(&ld, field, newval);
			gfs2_log_descriptor_out(&ld, rbh);
			if (!termlines)
				gfs2_ld_printval(&ld, field);
		} else {
			if (!termlines && gfs2_ld_printval(&ld, field))
				printf("Field '%s' not found.\n", field);
		}
		break;
	case GFS2_METATYPE_QC:
		gfs2_quota_change_in(&qc, rbh);
		if (setval) {
			gfs2_qc_assignval(&qc, field, newval);
			gfs2_quota_change_out(&qc, rbh);
			if (!termlines)
				gfs2_qc_printval(&qc, field);
		} else {
			if (!termlines && gfs2_qc_printval(&qc, field))
				printf("Field '%s' not found.\n", field);
		}
		break;
	case GFS2_METATYPE_JD: /* journaled data */
	case GFS2_METATYPE_EA: /* extended attribute */
	case GFS2_METATYPE_ED: /* extended attribute */
	case GFS2_METATYPE_LB:
	default:
		if (!termlines)
			print_block_type(fblock, type,
					 " which is not implemented");
		break;
	}
	brelse(rbh);
	fsync(sbd.device_fd);
	exit(0);
}

/* ------------------------------------------------------------------------ */
/* interactive_mode - accept keystrokes from user and display structures    */
/* ------------------------------------------------------------------------ */
static void interactive_mode(void)
{
	int ch, Quit;

	if ((wind = initscr()) == NULL) {
		fprintf(stderr, "Error: unable to initialize screen.");
		eol(0);
		exit(-1);
	}
	getmaxyx(stdscr, termlines, termcols);
	termlines--;
	/* Do our initial screen stuff: */
	clear(); /* don't use Erase */
	start_color();
	noecho();
	keypad(stdscr, TRUE);
	raw();
	curs_set(0);
	init_colors();
	/* Accept keystrokes and act on them accordingly */
	Quit = FALSE;
	editing = FALSE;
	while (!Quit) {
		display(FALSE);
		if (editing) {
			if (edit_row[dmode] == -1)
				block = goto_block();
			else {
				if (dmode == HEX_MODE)
					hex_edit(&ch);
				else if (dmode == GFS2_MODE) {
					bobgets(estring, edit_row[dmode]+4, 24,
						10, &ch);
					process_field(efield, estring);
					block_in_mem = -1;
				} else
					bobgets(estring, edit_row[dmode]+6, 14,
						edit_size[dmode], &ch);
			}
		}
		else
			while ((ch=getch()) == 0); // wait for input

		switch (ch)
		{
		/* --------------------------------------------------------- */
		/* escape or 'q' */
		/* --------------------------------------------------------- */
		case 0x1b:
		case 0x03:
		case 'q':
			if (editing)
				editing = FALSE;
			else
				Quit=TRUE;
			break;
		/* --------------------------------------------------------- */
		/* home - return to the superblock                           */
		/* --------------------------------------------------------- */
		case KEY_HOME:
			if (dmode == EXTENDED_MODE) {
				start_row[dmode] = end_row[dmode] = 0;
				edit_row[dmode] = 0;
			}
			else {
				block = 0x10 * (4096 / sbd.bsize);
				push_block(block);
				offset = 0;
			}
			break;
		/* --------------------------------------------------------- */
		/* backspace - return to the previous block on the stack     */
		/* --------------------------------------------------------- */
		case KEY_BACKSPACE:
		case 0x7f:
			block = pop_block();
			offset = 0;
			break;
		/* --------------------------------------------------------- */
		/* space - go down the block stack (opposite of backspace)   */
		/* --------------------------------------------------------- */
		case ' ':
			blockhist++;
			block = blockstack[blockhist % BLOCK_STACK_SIZE].block;
			offset = 0;
			break;
		/* --------------------------------------------------------- */
		/* arrow up */
		/* --------------------------------------------------------- */
		case KEY_UP:
		case '-':
			if (dmode == EXTENDED_MODE) {
				if (edit_row[dmode] > 0)
					edit_row[dmode]--;
				if (edit_row[dmode] < start_row[dmode])
					start_row[dmode] = edit_row[dmode];
			}
			else {
				if (edit_row[dmode] >= 0)
					edit_row[dmode]--;
			}
			break;
		/* --------------------------------------------------------- */
		/* arrow down */
		/* --------------------------------------------------------- */
		case KEY_DOWN:
		case '+':
			if (dmode == EXTENDED_MODE) {
				if (edit_row[dmode] + 1 < end_row[dmode]) {
					if (edit_row[dmode] - start_row[dmode]
					    + 1 > last_entry_onscreen[dmode])
						start_row[dmode]++;
					edit_row[dmode]++;
				}
			}
			else {
				if (edit_row[dmode] < last_entry_onscreen[dmode])
					edit_row[dmode]++;
			}
			break;
		/* --------------------------------------------------------- */
		/* arrow left */
		/* --------------------------------------------------------- */
		case KEY_LEFT:
			if (dmode == HEX_MODE) {
				if (edit_col[dmode] > 0)
					edit_col[dmode]--;
				else
					edit_col[dmode] = 15;
			}
			break;
		/* --------------------------------------------------------- */
		/* arrow right */
		/* --------------------------------------------------------- */
		case KEY_RIGHT:
			if (dmode == HEX_MODE) {
				if (edit_col[dmode] < 15)
					edit_col[dmode]++;
				else
					edit_col[dmode] = 0;
			}
			break;
		/* --------------------------------------------------------- */
		/* m - change display mode key */
		/* --------------------------------------------------------- */
		case 'm':
			dmode = ((dmode + 1) % DMODES);
			break;
		/* --------------------------------------------------------- */
		/* J - Jump to highlighted block number */
		/* --------------------------------------------------------- */
		case 'j':
			jump();
			break;
		/* --------------------------------------------------------- */
		/* g - goto block */
		/* --------------------------------------------------------- */
		case 'g':
			block = goto_block();
			break;
		/* --------------------------------------------------------- */
		/* h - help key */
		/* --------------------------------------------------------- */
		case 'h':
			print_usage();
			break;
		/* --------------------------------------------------------- */
		/* e - change to extended mode */
		/* --------------------------------------------------------- */
		case 'e':
			dmode = EXTENDED_MODE;
			break;
		/* --------------------------------------------------------- */
		/* b - Back one 4K block */
		/* --------------------------------------------------------- */
		case 'b':
			start_row[dmode] = end_row[dmode] = edit_row[dmode] = 0;
			if (block > 0)
				block--;
			offset = 0;
			break;
		/* --------------------------------------------------------- */
		/* c - Change color scheme */
		/* --------------------------------------------------------- */
		case 'c':
			color_scheme = !color_scheme;
			init_colors();
			break;
		/* --------------------------------------------------------- */
		/* page up key */
		/* --------------------------------------------------------- */
		case 0x19:                    // ctrl-y for vt100
		case KEY_PPAGE:		      // PgUp
		case 0x15:                    // ctrl-u for vi compat.
		case 0x02:                   // ctrl-b for less compat.
			pageup();
			break;
		/* --------------------------------------------------------- */
		/* end - Jump to the end of the list */
		/* --------------------------------------------------------- */
		case 0x168:
			if (dmode == EXTENDED_MODE) {
				int ents_per_screen = dsplines /
					lines_per_row[dmode];

				edit_row[dmode] = end_row[dmode] - 1;
				if ((edit_row[dmode] - ents_per_screen)+1 > 0)
					start_row[dmode] = edit_row[dmode] - 
						ents_per_screen + 1;
				else
					start_row[dmode] = 0;
			}
			/* TODO: Make end key work for other display modes. */
			break;
		/* --------------------------------------------------------- */
		/* f - Forward one 4K block */
		/* --------------------------------------------------------- */
		case 'f':
			start_row[dmode]=end_row[dmode]=edit_row[dmode] = 0;
			lines_per_row[dmode] = 1;
			block++;
			offset = 0;
			break;
		/* --------------------------------------------------------- */
		/* page down key */
		/* --------------------------------------------------------- */
		case 0x16:                    // ctrl-v for vt100
		case KEY_NPAGE:		      // PgDown
		case 0x04:                    // ctrl-d for vi compat.
			pagedn();
			break;
		/* --------------------------------------------------------- */
		/* enter key - change a value */
		/* --------------------------------------------------------- */
		case KEY_ENTER:
		case('\n'):
		case('\r'):
			editing = !editing;
			break;
		case KEY_RESIZE:
			getmaxyx(stdscr, termlines, termcols);
			termlines--;
			break;
		default:
			move(termlines - 1, 0);
			printw("Keystroke not understood: 0x%03x",ch);
			refresh();
			usleep(50000);
			break;
		} /* switch */
	} /* while !Quit */

    Erase();
    refresh();
    endwin();
}/* interactive_mode */

/* ------------------------------------------------------------------------ */
/* gfs_log_header_in - read in a gfs1-style log header                      */
/* ------------------------------------------------------------------------ */
void gfs_log_header_in(struct gfs_log_header *head,
		       struct gfs2_buffer_head *lbh)
{
	struct gfs_log_header *str = (struct gfs_log_header *)lbh->b_data;

	gfs2_meta_header_in(&head->lh_header, lbh);

	head->lh_flags = be32_to_cpu(str->lh_flags);
	head->lh_pad = be32_to_cpu(str->lh_pad);

	head->lh_first = be64_to_cpu(str->lh_first);
	head->lh_sequence = be64_to_cpu(str->lh_sequence);

	head->lh_tail = be64_to_cpu(str->lh_tail);
	head->lh_last_dump = be64_to_cpu(str->lh_last_dump);

	memcpy(head->lh_reserved, str->lh_reserved, 64);
}


/* ------------------------------------------------------------------------ */
/* gfs_log_header_print - print a gfs1-style log header                     */
/* ------------------------------------------------------------------------ */
void gfs_log_header_print(struct gfs_log_header *lh)
{
	gfs2_meta_header_print(&lh->lh_header);
	pv(lh, lh_flags, "%u", "0x%.8x");
	pv(lh, lh_pad, "%u", "%x");
	pv((unsigned long long)lh, lh_first, "%llu", "%llx");
	pv((unsigned long long)lh, lh_sequence, "%llu", "%llx");
	pv((unsigned long long)lh, lh_tail, "%llu", "%llx");
	pv((unsigned long long)lh, lh_last_dump, "%llu", "%llx");
}

/* ------------------------------------------------------------------------ */
/* print_ld_blocks - print all blocks given in a log descriptor             */
/* returns: the number of block numbers it printed                          */
/* ------------------------------------------------------------------------ */
static int print_ld_blocks(const uint64_t *b, const char *end, int start_line)
{
	int bcount = 0, i = 0;
	static char str[256];

	while (*b && (char *)b < end) {
		if (!termlines ||
		    (print_entry_ndx >= start_row[dmode] &&
		     ((print_entry_ndx - start_row[dmode])+1) *
		     lines_per_row[dmode] <= termlines - start_line - 2)) {
			if (i && i % 4 == 0) {
				eol(0);
				print_gfs2("                    ");
			}
			i++;
			sprintf(str, "0x%llx",
				(unsigned long long)be64_to_cpu(*b));
			print_gfs2("%-18.18s ", str);
			bcount++;
		}
		b++;
		if (sbd.gfs1)
			b++;
	}
	eol(0);
	return bcount;
}

/* ------------------------------------------------------------------------ */
/* fsck_readi - same as libgfs2's gfs2_readi, but sets absolute block #     */
/*              of the first bit of data read.                              */
/* ------------------------------------------------------------------------ */
static int fsck_readi(struct gfs2_inode *ip, void *rbuf, uint64_t roffset,
	       unsigned int size, uint64_t *abs_block)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_buffer_head *lbh;
	uint64_t lblock, dblock;
	unsigned int o;
	uint32_t extlen = 0;
	unsigned int amount;
	int not_new = 0;
	int isdir = !!(S_ISDIR(ip->i_di.di_mode));
	int copied = 0;

	*abs_block = 0;
	if (roffset >= ip->i_di.di_size)
		return 0;
	if ((roffset + size) > ip->i_di.di_size)
		size = ip->i_di.di_size - roffset;
	if (!size)
		return 0;
	if (isdir) {
		o = roffset % sdp->sd_jbsize;
		lblock = roffset / sdp->sd_jbsize;
	} else {
		lblock = roffset >> sdp->sd_sb.sb_bsize_shift;
		o = roffset & (sdp->bsize - 1);
	}

	if (!ip->i_di.di_height) /* inode_is_stuffed */
		o += sizeof(struct gfs2_dinode);
	else if (isdir)
		o += sizeof(struct gfs2_meta_header);

	while (copied < size) {
		amount = size - copied;
		if (amount > sdp->bsize - o)
			amount = sdp->bsize - o;
		if (!extlen)
			block_map(ip, lblock, &not_new, &dblock, &extlen,
				  FALSE);
		if (dblock) {
			lbh = bread(sdp, dblock);
			if (*abs_block == 0)
				*abs_block = lbh->b_blocknr;
			dblock++;
			extlen--;
		} else
			lbh = NULL;
		if (lbh) {
			memcpy(rbuf, lbh->b_data + o, amount);
			brelse(lbh);
		} else {
			memset(rbuf, 0, amount);
		}
		copied += amount;
		lblock++;
		o = (isdir) ? sizeof(struct gfs2_meta_header) : 0;
	}
	return copied;
}

static void check_journal_wrap(uint64_t seq, uint64_t *highest_seq)
{
	if (seq < *highest_seq) {
		print_gfs2("------------------------------------------------"
			   "------------------------------------------------");
		eol(0);
		print_gfs2("Journal wrapped here.");
		eol(0);
		print_gfs2("------------------------------------------------"
			   "------------------------------------------------");
		eol(0);
	}
	*highest_seq = seq;
}

static int is_meta(struct gfs2_buffer_head *lbh)
{
	uint32_t check_magic = ((struct gfs2_meta_header *)(lbh->b_data))->mh_magic;

	check_magic = be32_to_cpu(check_magic);
	if (check_magic == GFS2_MAGIC)
		return 1;
	return 0;
}

/* ------------------------------------------------------------------------ */
/* dump_journal - dump a journal file's contents.                           */
/* ------------------------------------------------------------------------ */
static void dump_journal(const char *journal)
{
	struct gfs2_buffer_head *j_bh = NULL, dummy_bh;
	uint64_t jblock, j_size, jb, abs_block, saveblk;
	int error, start_line, journal_num;
	struct gfs2_inode *j_inode = NULL;
	int ld_blocks = 0;
	uint64_t highest_seq = 0;
	char *jbuf = NULL;

	start_line = line;
	lines_per_row[dmode] = 1;
	error = 0;
	journal_num = atoi(journal + 7);
	print_gfs2("Dumping journal #%d.", journal_num);
	eol(0);
	jblock = find_journal_block(journal, &j_size);
	if (!jblock)
		return;
	if (!sbd.gfs1) {
		j_bh = bread(&sbd, jblock);
		j_inode = inode_get(&sbd, j_bh);
		jbuf = malloc(sbd.bsize);
	}

	for (jb = 0; jb < j_size; jb += (sbd.gfs1 ? 1:sbd.bsize)) {
		if (sbd.gfs1) {
			if (j_bh)
				brelse(j_bh);
			j_bh = bread(&sbd, jblock + jb);
			abs_block = jblock + jb;
			dummy_bh.b_data = j_bh->b_data;
		} else {
			error = fsck_readi(j_inode, (void *)jbuf, jb,
					   sbd.bsize, &abs_block);
			if (!error) /* end of file */
				break;
			dummy_bh.b_data = jbuf;
		}
		if (get_block_type(&dummy_bh) == GFS2_METATYPE_LD) {
			uint64_t *b;
			struct gfs2_log_descriptor ld;
			int ltndx;
			uint32_t logtypes[2][6] = {
				{GFS2_LOG_DESC_METADATA,
				 GFS2_LOG_DESC_REVOKE,
				 GFS2_LOG_DESC_JDATA,
				 0, 0, 0},
				{GFS_LOG_DESC_METADATA,
				 GFS_LOG_DESC_IUL,
				 GFS_LOG_DESC_IDA,
				 GFS_LOG_DESC_Q,
				 GFS_LOG_DESC_LAST,
				 0}};
			const char *logtypestr[2][6] = {
				{"Metadata", "Revoke", "Jdata",
				 "Unknown", "Unknown", "Unknown"},
				{"Metadata", "Unlinked inode", "Dealloc inode",
				 "Quota", "Final Entry", "Unknown"}};

			print_gfs2("0x%llx (j+%4llx): Log descriptor, ",
				   abs_block, jb / (sbd.gfs1 ? 1 : sbd.bsize));
			gfs2_log_descriptor_in(&ld, &dummy_bh);
			print_gfs2("type %d ", ld.ld_type);

			for (ltndx = 0;; ltndx++) {
				if (ld.ld_type == logtypes[sbd.gfs1][ltndx] ||
				    logtypes[sbd.gfs1][ltndx] == 0)
					break;
			}
			print_gfs2("(%s) ", logtypestr[sbd.gfs1][ltndx]);
			print_gfs2("len:%u, data1: %u",
				   ld.ld_length, ld.ld_data1);
			eol(0);
			print_gfs2("                    ");
			if (sbd.gfs1)
				b = (uint64_t *)(dummy_bh.b_data +
					sizeof(struct gfs_log_descriptor));
			else
				b = (uint64_t *)(dummy_bh.b_data +
					sizeof(struct gfs2_log_descriptor));
			ld_blocks = ld.ld_data1;
			ld_blocks -= print_ld_blocks(b, (dummy_bh.b_data +
							 sbd.bsize),
						     start_line);
		} else if (get_block_type(&dummy_bh) == GFS2_METATYPE_LH) {
			struct gfs2_log_header lh;
			struct gfs_log_header lh1;

			if (sbd.gfs1) {
				gfs_log_header_in(&lh1, &dummy_bh);
				check_journal_wrap(lh1.lh_sequence,
						   &highest_seq);
				print_gfs2("0x%llx (j+%4llx): Log header: "
					   "Flags:%x, Seq: 0x%x, "
					   "1st: 0x%x, tail: 0x%x, "
					   "last: 0x%x", abs_block,
					   jb, lh1.lh_flags, lh1.lh_sequence,
					   lh1.lh_first, lh1.lh_tail,
					   lh1.lh_last_dump);
			} else {
				gfs2_log_header_in(&lh, &dummy_bh);
				check_journal_wrap(lh.lh_sequence,
						   &highest_seq);
				print_gfs2("0x%llx (j+%4llx): Log header: Seq"
					   ": 0x%x, tail: 0x%x, blk: 0x%x",
					   abs_block,
					   jb / sbd.bsize, lh.lh_sequence,
					   lh.lh_tail, lh.lh_blkno);
			}
			eol(0);
		} else if (sbd.gfs1 && ld_blocks > 0) {
			print_gfs2("0x%llx (j+%4llx): GFS log descriptor"
				   " continuation block", abs_block, jb);
			eol(0);
			print_gfs2("                    ");
			ld_blocks -= print_ld_blocks((uint64_t *)dummy_bh.b_data,
						     (dummy_bh.b_data +
						      sbd.bsize), start_line);
		} else if (details && is_meta(&dummy_bh)) {
			saveblk = block;
			block = abs_block;
			display(0);
			block = saveblk;
		}
	}
	brelse(j_bh);
	blockhist = -1; /* So we don't print anything else */
	free(jbuf);
}

/* ------------------------------------------------------------------------ */
/* usage - print command line usage                                         */
/* ------------------------------------------------------------------------ */
static void usage(void)
{
	fprintf(stderr,"\nFormat is: gfs2_edit [-c 1] [-V] [-x] [-h] [identify] [-z <0-9>] [-p structures|blocks][blocktype][blockalloc [val]][blockbits][blockrg][find sb|rg|rb|di|in|lf|jd|lh|ld|ea|ed|lb|13|qc][field <f>[val]] /dev/device\n\n");
	fprintf(stderr,"If only the device is specified, it enters into hexedit mode.\n");
	fprintf(stderr,"identify - prints out only the block type, not the details.\n");
	fprintf(stderr,"printsavedmeta - prints out the saved metadata blocks from a savemeta file.\n");
	fprintf(stderr,"savemeta <file_system> <file> - save off your metadata for analysis and debugging.\n");
	fprintf(stderr,"   (The intelligent way: assume bitmap is correct).\n");
	fprintf(stderr,"savemetaslow - save off your metadata for analysis and debugging.  The SLOW way (block by block).\n");
	fprintf(stderr,"savergs - save off only the resource group information (rindex and rgs).\n");
	fprintf(stderr,"restoremeta - restore metadata for debugging (DANGEROUS).\n");
	fprintf(stderr,"rgcount - print how many RGs in the file system.\n");
	fprintf(stderr,"rgflags rgnum [new flags] - print or modify flags for rg #rgnum (0 - X)\n");
	fprintf(stderr,"-V   prints version number.\n");
	fprintf(stderr,"-c 1 selects alternate color scheme 1\n");
	fprintf(stderr,"-d   prints details (for printing journals)\n");
	fprintf(stderr,"-p   prints GFS2 structures or blocks to stdout.\n");
	fprintf(stderr,"     sb - prints the superblock.\n");
	fprintf(stderr,"     size - prints the filesystem size.\n");
	fprintf(stderr,"     master - prints the master directory.\n");
	fprintf(stderr,"     root - prints the root directory.\n");
	fprintf(stderr,"     jindex - prints the journal index directory.\n");
	fprintf(stderr,"     per_node - prints the per_node directory.\n");
	fprintf(stderr,"     inum - prints the inum file.\n");
	fprintf(stderr,"     statfs - prints the statfs file.\n");
	fprintf(stderr,"     rindex - prints the rindex file.\n");
	fprintf(stderr,"     rg X - print resource group X.\n");
	fprintf(stderr,"     rgs - prints all the resource groups (rgs).\n");
	fprintf(stderr,"     quota - prints the quota file.\n");
	fprintf(stderr,"     0x1234 - prints the specified block\n");
	fprintf(stderr,"-p   <block> blocktype - prints the type "
		"of the specified block\n");
	fprintf(stderr,"-p   <block> blockrg - prints the resource group "
		"block corresponding to the specified block\n");
	fprintf(stderr,"-p   <block> blockbits - prints the block with "
		"the bitmap corresponding to the specified block\n");
	fprintf(stderr,"-p   <block> blockalloc [0|1|2|3] - print or change "
		"the allocation type of the specified block\n");
	fprintf(stderr,"-p   <block> field [new_value] - prints or change the "
		"structure field\n");
	fprintf(stderr,"-p   <b> find sb|rg|rb|di|in|lf|jd|lh|ld|ea|ed|lb|"
		"13|qc - find block of given type after block <b>\n");
	fprintf(stderr,"     <b> specifies the starting block for search\n");
	fprintf(stderr,"-z 1 use gzip compression level 1 for savemeta (default 9)\n");
	fprintf(stderr,"-z 0 do not use compression\n");
	fprintf(stderr,"-s   specifies a starting block such as root, rindex, quota, inum.\n");
	fprintf(stderr,"-x   print in hexmode.\n");
	fprintf(stderr,"-h   prints this help.\n\n");
	fprintf(stderr,"Examples:\n");
	fprintf(stderr,"   To run in interactive mode:\n");
	fprintf(stderr,"     gfs2_edit /dev/bobs_vg/lvol0\n");
	fprintf(stderr,"   To print out the superblock and master directory:\n");
	fprintf(stderr,"     gfs2_edit -p sb master /dev/bobs_vg/lvol0\n");
	fprintf(stderr,"   To print out the master directory in hex:\n");
	fprintf(stderr,"     gfs2_edit -x -p master /dev/bobs_vg/lvol0\n");
	fprintf(stderr,"   To print out the block-type for block 0x27381:\n");
	fprintf(stderr,"     gfs2_edit identify -p 0x27381 /dev/bobs_vg/lvol0\n");
	fprintf(stderr,"   To print out the fourth Resource Group. (the first R is #0)\n");
	fprintf(stderr,"     gfs2_edit -p rg 3 /dev/sdb1\n");
	fprintf(stderr,"   To print out the metadata type of block 1234\n");
	fprintf(stderr,"     gfs2_edit -p 1234 blocktype /dev/roth_vg/roth_lb\n");
	fprintf(stderr,"   To print out the allocation type of block 2345\n");
	fprintf(stderr,"     gfs2_edit -p 2345 blockalloc /dev/vg/lv\n");
	fprintf(stderr,"   To change the allocation type of block 2345 to a 'free block'\n");
	fprintf(stderr,"     gfs2_edit -p 2345 blockalloc 0 /dev/vg/lv\n");
	fprintf(stderr,"   To print out the file size of the dinode at block 0x118\n");
	fprintf(stderr,"     gfs2_edit -p 0x118 field di_size /dev/roth_vg/roth_lb\n");
	fprintf(stderr,"   To find any dinode higher than the quota file dinode:\n");
	fprintf(stderr,"     gfs2_edit -p quota find di /dev/x/y\n");
	fprintf(stderr,"   To set the Resource Group flags for rg #7 to 3.\n");
	fprintf(stderr,"     gfs2_edit rgflags 7 3 /dev/sdc2\n");
	fprintf(stderr,"   To save off all metadata for /dev/vg/lv without compression:\n");
	fprintf(stderr,"     gfs2_edit savemeta -z 0 /dev/vg/lv /tmp/metasave\n");
}/* usage */

/**
 * getgziplevel - Process the -z parameter to savemeta operations
 * argv - argv
 * i    - a pointer to the argv index at which to begin processing
 * The index pointed to by i will be incremented past the -z option if found
 */
static void getgziplevel(char *argv[], int *i)
{
	char *endptr;
	(*i)++;
	if (!strcasecmp(argv[*i], "-z")) {
		(*i)++;
		errno = 0;
		gziplevel = strtol(argv[*i], &endptr, 10);
		if (errno || endptr == argv[*i] || gziplevel < 0 || gziplevel > 9) {
			fprintf(stderr, "Compression level out of range: %s\n", argv[*i]);
			exit(-1);
		}
	} else {
		(*i)--;
	}
}

/* ------------------------------------------------------------------------ */
/* parameterpass1 - pre-processing for command-line parameters              */
/* ------------------------------------------------------------------------ */
static void parameterpass1(int argc, char *argv[], int i)
{
	if (!strcasecmp(argv[i], "-V")) {
		printf("%s version %s (built %s %s)\n",
		       argv[0], VERSION, __DATE__, __TIME__);
		printf("%s\n", REDHAT_COPYRIGHT);
		exit(0);
	}
	else if (!strcasecmp(argv[i], "-h") ||
		 !strcasecmp(argv[i], "-help") ||
		 !strcasecmp(argv[i], "-usage")) {
		usage();
		exit(0);
	}
	else if (!strcasecmp(argv[i], "-c")) {
		i++;
		color_scheme = atoi(argv[i]);
	}
	else if (!strcasecmp(argv[i], "-p") ||
		 !strcasecmp(argv[i], "-print")) {
		termlines = 0; /* initial value--we'll figure
				  it out later */
		dmode = GFS2_MODE;
	}
	else if (!strcasecmp(argv[i], "-d") ||
		 !strcasecmp(argv[i], "-details"))
		details = 1;
	else if (!strcasecmp(argv[i], "savemeta"))
		termlines = 0;
	else if (!strcasecmp(argv[i], "savemetaslow"))
		termlines = 0;
	else if (!strcasecmp(argv[i], "savergs"))
		termlines = 0;
	else if (!strcasecmp(argv[i], "printsavedmeta")) {
		if (dmode == INIT_MODE)
			dmode = GFS2_MODE;
		restoremeta(argv[i+1], argv[i+2], TRUE);
	} else if (!strcasecmp(argv[i], "restoremeta")) {
		if (dmode == INIT_MODE)
			dmode = HEX_MODE; /* hopefully not used */
		restoremeta(argv[i+1], argv[i+2], FALSE);
	} else if (!strcmp(argv[i], "rgcount"))
		termlines = 0;
	else if (!strcmp(argv[i], "rgflags"))
		termlines = 0;
	else if (!strcmp(argv[i], "rg"))
		termlines = 0;
	else if (!strcasecmp(argv[i], "-x"))
		dmode = HEX_MODE;
	else if (!device[0] && strchr(argv[i],'/'))
		strcpy(device, argv[i]);
}

/* ------------------------------------------------------------------------ */
/* process_parameters - process commandline parameters                      */
/* pass - we make two passes through the parameters; the first pass gathers */
/*        normals parameters, device name, etc.  The second pass is for     */
/*        figuring out what structures to print out.                        */
/* ------------------------------------------------------------------------ */
static void process_parameters(int argc, char *argv[], int pass)
{
	int i;
	uint64_t keyword_blk;

	if (argc < 2) {
		usage();
		die("no device specified\n");
	}
	for (i = 1; i < argc; i++) {
		if (!pass) { /* first pass */
			parameterpass1(argc, argv, i);
			continue;
		}
		/* second pass */
		if (!strcasecmp(argv[i], "-s")) {
			i++;
			if (i >= argc - 1) {
				printf("Error: starting block not specified "
				       "with -s.\n");
				printf("%s -s [starting block | keyword] "
				       "<device>\n", argv[0]);
				printf("For example: %s -s \"rg 3\" "
				       "/dev/exxon_vg/exxon_lv\n", argv[0]);
				exit(EXIT_FAILURE);
			}
			starting_blk = check_keywords(argv[i]);
			continue;
		}
		if (termlines || strchr(argv[i],'/')) /* if print or slash */
			continue;
			
		if (!strncmp(argv[i], "journal", 7) &&
		    isdigit(argv[i][7])) {
			dump_journal(argv[i]);
			continue;
		}
		keyword_blk = check_keywords(argv[i]);
		if (keyword_blk)
			push_block(keyword_blk);
		else if (!strcasecmp(argv[i], "-x"))
			dmode = HEX_MODE;
		else if (argv[i][0] == '-') /* if it starts with a dash */
			; /* ignore it--meant for pass == 0 */
		else if (!strcmp(argv[i], "identify"))
			identify = TRUE;
		else if (!strcmp(argv[i], "size")) {
			printf("Device size: %llu (0x%llx)\n",
			       (unsigned long long)max_block,
			       (unsigned long long)max_block);
			exit(EXIT_SUCCESS);
		} else if (!strcmp(argv[i], "rgcount"))
			rgcount();
		else if (!strcmp(argv[i], "field")) {
			i++;
			if (i >= argc - 1) {
				printf("Error: field not specified.\n");
				printf("Format is: %s -p <block> field "
				       "<field> [newvalue]\n", argv[0]);
				gfs2_rgrp_free(&sbd.rgtree);
				exit(EXIT_FAILURE);
			}
			process_field(argv[i], argv[i + 1]);
		} else if (!strcmp(argv[i], "blocktype")) {
			find_print_block_type();
		} else if (!strcmp(argv[i], "blockrg")) {
			find_print_block_rg(0);
		} else if (!strcmp(argv[i], "blockbits")) {
			find_print_block_rg(1);
		} else if (!strcmp(argv[i], "blockalloc")) {
			if (isdigit(argv[i + 1][0])) {
				int newval;

				if (argv[i + 1][0]=='0' && argv[i + 1][1]=='x')
					sscanf(argv[i + 1], "%x", &newval);
				else
					newval = (uint64_t)atoi(argv[i + 1]);
				find_change_block_alloc(&newval);
			} else {
				find_change_block_alloc(NULL);
			}
		} else if (!strcmp(argv[i], "find")) {
			find_metablockoftype(argv[i + 1], 1);
		} else if (!strcmp(argv[i], "rgflags")) {
			int rg, set = FALSE;
			uint32_t new_flags = 0;

			i++;
			if (i >= argc - 1) {
				printf("Error: rg # not specified.\n");
				printf("Format is: %s rgflags rgnum"
				       "[newvalue]\n", argv[0]);
				gfs2_rgrp_free(&sbd.rgtree);
				exit(EXIT_FAILURE);
			}
			if (argv[i][0]=='0' && argv[i][1]=='x')
				sscanf(argv[i], "%"SCNx32, &rg);
			else
				rg = atoi(argv[i]);
			i++;
			if (i < argc - 1 &&
			    isdigit(argv[i][0])) {
				set = TRUE;
				if (argv[i][0]=='0' && argv[i][1]=='x')
					sscanf(argv[i], "%"SCNx32, &new_flags);
				else
					new_flags = atoi(argv[i]);
			}
			set_rgrp_flags(rg, new_flags, set, FALSE);
			gfs2_rgrp_free(&sbd.rgtree);
			exit(EXIT_SUCCESS);
		} else if (!strcmp(argv[i], "rg")) {
			int rg;
				
			i++;
			if (i >= argc - 1) {
				printf("Error: rg # not specified.\n");
				printf("Format is: %s rg rgnum\n", argv[0]);
				gfs2_rgrp_free(&sbd.rgtree);
				exit(EXIT_FAILURE);
			}
			rg = atoi(argv[i]);
			if (!strcasecmp(argv[i + 1], "find")) {
				temp_blk = get_rg_addr(rg);
				push_block(temp_blk);
			} else {
				set_rgrp_flags(rg, 0, FALSE, TRUE);
				gfs2_rgrp_free(&sbd.rgtree);
				exit(EXIT_SUCCESS);
			}
		}
		else if (!strcasecmp(argv[i], "savemeta")) {
			getgziplevel(argv, &i);
			savemeta(argv[i+2], 0, gziplevel);
		} else if (!strcasecmp(argv[i], "savemetaslow")) {
			getgziplevel(argv, &i);
			savemeta(argv[i+2], 1, gziplevel);
		} else if (!strcasecmp(argv[i], "savergs")) {
			getgziplevel(argv, &i);
			savemeta(argv[i+2], 2, gziplevel);
		} else if (isdigit(argv[i][0])) { /* decimal addr */
			sscanf(argv[i], "%"SCNd64, &temp_blk);
			push_block(temp_blk);
		} else {
			fprintf(stderr,"I don't know what '%s' means.\n",
				argv[i]);
			usage();
			exit(EXIT_FAILURE);
		}
	} /* for */
}/* process_parameters */

/******************************************************************************
*******************************************************************************
**
** main()
**
** Description:
**   Do everything
**
*******************************************************************************
******************************************************************************/
int main(int argc, char *argv[])
{
	int i, j, fd;

	indirect = malloc(sizeof(struct iinfo));
	if (!indirect)
		die("Out of memory.");
	memset(indirect, 0, sizeof(struct iinfo));
	memset(start_row, 0, sizeof(start_row));
	memset(lines_per_row, 0, sizeof(lines_per_row));
	memset(end_row, 0, sizeof(end_row));
	memset(edit_row, 0, sizeof(edit_row));
	memset(edit_col, 0, sizeof(edit_col));
	memset(edit_size, 0, sizeof(edit_size));
	memset(last_entry_onscreen, 0, sizeof(last_entry_onscreen));
	dmode = INIT_MODE;
	sbd.bsize = 4096;
	block = starting_blk = 0x10;
	for (i = 0; i < BLOCK_STACK_SIZE; i++) {
		blockstack[i].dmode = HEX_MODE;
		blockstack[i].block = block;
		for (j = 0; j < DMODES; j++) {
			blockstack[i].start_row[j] = 0;
			blockstack[i].end_row[j] = 0;
			blockstack[i].edit_row[j] = 0;
			blockstack[i].edit_col[j] = 0;
			blockstack[i].lines_per_row[j] = 0;
		}
	}

	edit_row[GFS2_MODE] = 10; /* Start off at root inode
				     pointer in superblock */
	memset(device, 0, sizeof(device));
	termlines = 30;  /* assume interactive mode until we find -p */
	process_parameters(argc, argv, 0);
	if (dmode == INIT_MODE)
		dmode = HEX_MODE;

	fd = open(device, O_RDWR);
	if (fd < 0)
		die("can't open %s: %s\n", device, strerror(errno));
	max_block = lseek(fd, 0, SEEK_END) / sbd.bsize;

	read_superblock(fd);
	max_block = lseek(fd, 0, SEEK_END) / sbd.bsize;
	strcpy(sbd.device_name, device);
	if (sbd.gfs1)
		edit_row[GFS2_MODE]++;
	else
		read_master_dir();
	block_in_mem = -1;
	process_parameters(argc, argv, 1); /* get what to print from cmdline */

	block = blockstack[0].block = starting_blk * (4096 / sbd.bsize);

	if (termlines)
		interactive_mode();
	else { /* print all the structures requested */
		for (i = 0; i <= blockhist; i++) {
			block = blockstack[i + 1].block;
			if (!block)
				break;
			display(identify);
			if (!identify) {
				display_extended();
				printf("-------------------------------------" \
				       "-----------------");
				eol(0);
			}
			block = pop_block();
		}
	}
	close(fd);
	if (indirect)
		free(indirect);
	gfs2_rgrp_free(&sbd.rgtree);
 	exit(EXIT_SUCCESS);
}
