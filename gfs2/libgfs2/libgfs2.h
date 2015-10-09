#ifndef __LIBGFS2_DOT_H__
#define __LIBGFS2_DOT_H__

#include <features.h>
#include <inttypes.h>
#include <stdarg.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <linux/types.h>
#include <linux/limits.h>
#include <endian.h>
#include <byteswap.h>

#include <linux/gfs2_ondisk.h>
#include "osi_list.h"
#include "osi_tree.h"

__BEGIN_DECLS

#ifndef TRUE
#define TRUE (1)
#endif

#ifndef FALSE
#define FALSE (0)
#endif

#if __BYTE_ORDER == __BIG_ENDIAN

#define be16_to_cpu(x) (x)
#define be32_to_cpu(x) (x)
#define be64_to_cpu(x) (x)

#define cpu_to_be16(x) (x)
#define cpu_to_be32(x) (x)
#define cpu_to_be64(x) (x)

#define le16_to_cpu(x) (bswap_16((x)))
#define le32_to_cpu(x) (bswap_32((x)))
#define le64_to_cpu(x) (bswap_64((x)))

#define cpu_to_le16(x) (bswap_16((x)))
#define cpu_to_le32(x) (bswap_32((x)))
#define cpu_to_le64(x) (bswap_64((x)))

#endif  /*  __BYTE_ORDER == __BIG_ENDIAN  */


#if __BYTE_ORDER == __LITTLE_ENDIAN

#define be16_to_cpu(x) (bswap_16((x)))
#define be32_to_cpu(x) (bswap_32((x)))
#define be64_to_cpu(x) (bswap_64((x)))

#define cpu_to_be16(x) (bswap_16((x)))
#define cpu_to_be32(x) (bswap_32((x)))
#define cpu_to_be64(x) (bswap_64((x))) 

#define le16_to_cpu(x) (x)
#define le32_to_cpu(x) (x)
#define le64_to_cpu(x) (x)

#define cpu_to_le16(x) (x)
#define cpu_to_le32(x) (x)
#define cpu_to_le64(x) (x)

#endif  /*  __BYTE_ORDER == __LITTLE_ENDIAN  */

#define BLOCKMAP_SIZE4(size) (size >> 1)
#define BLOCKMAP_BYTE_OFFSET4(x) ((x & 0x0000000000000001) << 2)
#define BLOCKMAP_MASK4 (0xf)


struct lgfs2_dev_info {
	struct stat stat;
	unsigned readonly:1;
	long ra_pages;
	int soft_block_size;
	int logical_block_size;
	unsigned int physical_block_size;
	unsigned int io_min_size;
	unsigned int io_optimal_size;
	int io_align_offset;
	uint64_t size;
};

static __inline__ __attribute__((noreturn, format (printf, 1, 2)))
void die(const char *fmt, ...)
{
	va_list ap;
	fprintf(stderr, "%s: ", __FILE__);
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(-1);
}

struct device {
	uint64_t length;
};

struct gfs2_bitmap
{
	uint32_t   bi_offset;  /* The offset in the buffer of the first byte */
	uint32_t   bi_start;   /* The position of the first byte in this block */
	uint32_t   bi_len;     /* The number of bytes in this block */
};

struct rgrp_tree {
	struct osi_node node;
	uint64_t start;	   /* The offset of the beginning of this resource group */
	uint64_t length;	/* The length of this resource group */

	struct gfs2_rindex ri;
	struct gfs2_rgrp rg;
	struct gfs2_bitmap *bits;
	struct gfs2_buffer_head **bh;
};

struct gfs2_buffer_head {
	osi_list_t b_altlist; /* alternate list */
	uint64_t b_blocknr;
	int b_modified;
	char *b_data;
	struct gfs2_sbd *sdp;
};

struct special_blocks {
	osi_list_t list;
	uint64_t block;
};

struct gfs2_sbd;
struct gfs2_inode {
	int bh_owned; /* Is this bh owned, iow, should we release it later? */
	struct gfs2_dinode i_di;
	struct gfs2_buffer_head *i_bh;
	struct gfs2_sbd *i_sbd;
};

/* FIXME not sure that i want to keep a record of the inodes or the
 * contents of them, or both ... if I need to write back to them, it
 * would be easier to hold the inode as well  */
struct per_node
{
	struct gfs2_inode *inum;
	struct gfs2_inum_range inum_range;
	struct gfs2_inode *statfs;
	struct gfs2_statfs_change statfs_change;
	struct gfs2_inode *unlinked;
	struct gfs2_inode *quota;
	struct gfs2_quota_change quota_change;
};

struct master_dir
{
	struct gfs2_inode *inum;
	uint64_t next_inum;
	struct gfs2_inode *statfs;
	struct gfs2_statfs_change statfs_change;

	struct gfs2_rindex rindex;
	struct gfs2_inode *qinode;
	struct gfs2_quota quotas;

	struct gfs2_inode       *jiinode;
	struct gfs2_inode       *riinode;
	struct gfs2_inode       *rooti;
	struct gfs2_inode       *pinode;
	
	struct gfs2_inode **journal;      /* Array of journals */
	uint32_t journals;                /* Journal count */
	struct per_node *pn;              /* Array of per_node entries */
};

struct gfs2_sbd {
	struct gfs2_sb sd_sb;    /* a copy of the ondisk structure */
	char lockproto[GFS2_LOCKNAME_LEN];
	char locktable[GFS2_LOCKNAME_LEN];

	unsigned int bsize;	     /* The block size of the FS (in bytes) */
	unsigned int jsize;	     /* Size of journals (in MB) */
	unsigned int rgsize;     /* Size of resource groups (in MB) */
	unsigned int utsize;     /* Size of unlinked tag files (in MB) */
	unsigned int qcsize;     /* Size of quota change files (in MB) */

	int debug;
	int quiet;
	int expert;
	int override;

	char device_name[PATH_MAX];
	char *path_name;

	/* Constants */

	uint32_t sd_fsb2bb;
	uint32_t sd_fsb2bb_shift;
	uint32_t sd_diptrs;
	uint32_t sd_inptrs;
	uint32_t sd_jbsize;
	uint32_t sd_hash_bsize;
	uint32_t sd_hash_bsize_shift;
	uint32_t sd_hash_ptrs;
	uint32_t sd_max_dirres;
	uint32_t sd_max_height;
	uint64_t sd_heightsize[GFS2_MAX_META_HEIGHT];
	uint32_t sd_max_jheight;
	uint64_t sd_jheightsize[GFS2_MAX_META_HEIGHT];

	/* Not specified on the command line, but... */

	int64_t time;

	struct lgfs2_dev_info dinfo;
	struct device device;

	int device_fd;
	int path_fd;

	uint64_t sb_addr;

	uint64_t orig_fssize;
	uint64_t fssize;
	uint64_t blks_total;
	uint64_t blks_alloced;
	uint64_t dinodes_alloced;

	uint64_t orig_rgrps;
	uint64_t rgrps;
	uint64_t new_rgrps;
	struct osi_root rgtree;
	struct osi_root rgcalc;

	unsigned int orig_journals;

	struct gfs2_inode *master_dir;
	struct master_dir md;

	unsigned int writes;
	int metafs_fd;
	char metafs_path[PATH_MAX]; /* where metafs is mounted */
	struct special_blocks eattr_blocks;

	uint64_t rg_one_length;
	uint64_t rg_length;
	int gfs1;
};

struct metapath {
	unsigned int mp_list[GFS2_MAX_META_HEIGHT];
};


#define GFS2_DEFAULT_BSIZE          (4096)
#define GFS2_DEFAULT_JSIZE          (128)
#define GFS2_DEFAULT_RGSIZE         (256)
#define GFS2_DEFAULT_UTSIZE         (1)
#define GFS2_DEFAULT_QCSIZE         (1)
#define GFS2_DEFAULT_LOCKPROTO      "lock_dlm"
#define GFS2_MIN_GROW_SIZE          (10)
#define GFS2_EXCESSIVE_RGS          (10000)

#define GFS2_EXP_MIN_RGSIZE         (1)
#define GFS2_MIN_RGSIZE             (32)
/* Look at this!  Why can't we go bigger than 2GB? */
#define GFS2_MAX_RGSIZE             (2048)

/* bitmap.c */
struct gfs2_bmap {
	uint64_t size;
	uint64_t mapsize;
	unsigned char *map;
};

/* block_list.c */

extern struct special_blocks *blockfind(struct special_blocks *blist, uint64_t num);
extern void gfs2_special_add(struct special_blocks *blocklist, uint64_t block);
extern void gfs2_special_set(struct special_blocks *blocklist, uint64_t block);
extern void gfs2_special_free(struct special_blocks *blist);
extern void gfs2_special_clear(struct special_blocks *blocklist,
			       uint64_t block);

/* buf.c */
extern struct gfs2_buffer_head *bget(struct gfs2_sbd *sdp, uint64_t num);
extern struct gfs2_buffer_head *__bread(struct gfs2_sbd *sdp, uint64_t num,
					int line, const char *caller);
extern int bwrite(struct gfs2_buffer_head *bh);
extern int brelse(struct gfs2_buffer_head *bh);

#define bmodified(bh) do { bh->b_modified = 1; } while(0)

#define bread(bl, num) __bread(bl, num, __LINE__, __FUNCTION__)

/* device_geometry.c */
extern int lgfs2_get_dev_info(int fd, struct lgfs2_dev_info *i);
extern void fix_device_geometry(struct gfs2_sbd *sdp);

/* fs_bits.c */
#define BFITNOENT (0xFFFFFFFF)

/* functions with blk #'s that are buffer relative */
extern uint32_t gfs2_bitcount(unsigned char *buffer, unsigned int buflen,
			      unsigned char state);
extern unsigned long gfs2_bitfit(const unsigned char *buffer,
				 const unsigned int buflen,
				 unsigned long goal, unsigned char old_state);

/* functions with blk #'s that are rgrp relative */
extern uint32_t gfs2_blkalloc_internal(struct rgrp_tree *rgd, uint32_t goal,
				       unsigned char old_state,
				       unsigned char new_state, int do_it);
extern int gfs2_check_range(struct gfs2_sbd *sdp, uint64_t blkno);

/* functions with blk #'s that are file system relative */
extern int valid_block(struct gfs2_sbd *sdp, uint64_t blkno);
extern int gfs2_get_bitmap(struct gfs2_sbd *sdp, uint64_t blkno,
			   struct rgrp_tree *rgd);
extern int gfs2_set_bitmap(struct gfs2_sbd *sdp, uint64_t blkno, int state);

/* fs_geometry.c */
extern uint32_t rgblocks2bitblocks(const unsigned int bsize, const uint32_t rgblocks,
                                    uint32_t *ri_data) __attribute__((nonnull(3)));
extern uint64_t how_many_rgrps(struct gfs2_sbd *sdp, struct device *dev,
			       int rgsize_specified);
extern void compute_rgrp_layout(struct gfs2_sbd *sdp, struct osi_root *rgtree,
				int rgsize_specified);
extern void build_rgrps(struct gfs2_sbd *sdp, int write);

/* fs_ops.c */
#define IS_LEAF     (1)
#define IS_DINODE   (2)

extern struct metapath *find_metapath(struct gfs2_inode *ip, uint64_t block);
extern void lookup_block(struct gfs2_inode *ip, struct gfs2_buffer_head *bh,
			 unsigned int height, struct metapath *mp,
			 int create, int *new, uint64_t *block);
extern struct gfs2_inode *inode_get(struct gfs2_sbd *sdp,
				    struct gfs2_buffer_head *bh);
extern struct gfs2_inode *inode_read(struct gfs2_sbd *sdp, uint64_t di_addr);
extern struct gfs2_inode *is_system_inode(struct gfs2_sbd *sdp,
					  uint64_t block);
extern void inode_put(struct gfs2_inode **ip);
extern uint64_t data_alloc(struct gfs2_inode *ip);
extern uint64_t meta_alloc(struct gfs2_inode *ip);
extern uint64_t dinode_alloc(struct gfs2_sbd *sdp);
extern int gfs2_readi(struct gfs2_inode *ip, void *buf, uint64_t offset,
		      unsigned int size);
#define gfs2_writei(ip, buf, offset, size) \
	__gfs2_writei(ip, buf, offset, size, 1)
extern int __gfs2_writei(struct gfs2_inode *ip, void *buf, uint64_t offset,
			 unsigned int size, int resize);
extern struct gfs2_buffer_head *get_file_buf(struct gfs2_inode *ip,
					     uint64_t lbn, int prealloc);
extern struct gfs2_buffer_head *init_dinode(struct gfs2_sbd *sdp,
					    struct gfs2_inum *inum,
					    unsigned int mode, uint32_t flags,
					    struct gfs2_inum *parent);
extern struct gfs2_buffer_head *init_gfs_dinode(struct gfs2_sbd *sdp,
						struct gfs2_inum *inum,
						unsigned int mode,
						uint32_t flags,
						struct gfs2_inum *parent);
extern struct gfs2_inode *createi(struct gfs2_inode *dip, const char *filename,
				  unsigned int mode, uint32_t flags);
extern struct gfs2_inode *gfs_createi(struct gfs2_inode *dip,
				      const char *filename, unsigned int mode,
				      uint32_t flags);
extern void dirent2_del(struct gfs2_inode *dip, struct gfs2_buffer_head *bh,
			struct gfs2_dirent *prev, struct gfs2_dirent *cur);
extern int dir_search(struct gfs2_inode *dip, const char *filename, int len,
		      unsigned int *type, struct gfs2_inum *inum);
extern int gfs2_lookupi(struct gfs2_inode *dip, const char *filename, int len,
			struct gfs2_inode **ipp);
extern int dir_add(struct gfs2_inode *dip, const char *filename, int len,
		    struct gfs2_inum *inum, unsigned int type);
extern int gfs2_dirent_del(struct gfs2_inode *dip, const char *filename,
			   int filename_len);
extern void block_map(struct gfs2_inode *ip, uint64_t lblock, int *new,
		      uint64_t *dblock, uint32_t *extlen, int prealloc);
extern void gfs2_get_leaf_nr(struct gfs2_inode *dip, uint32_t index,
			     uint64_t *leaf_out);
extern void gfs2_put_leaf_nr(struct gfs2_inode *dip, uint32_t inx, uint64_t leaf_out);
extern void gfs2_free_block(struct gfs2_sbd *sdp, uint64_t block);
extern int gfs2_freedi(struct gfs2_sbd *sdp, uint64_t block);
extern int gfs2_get_leaf(struct gfs2_inode *dip, uint64_t leaf_no,
			 struct gfs2_buffer_head **bhp);
extern int gfs2_dirent_first(struct gfs2_inode *dip,
			     struct gfs2_buffer_head *bh,
			     struct gfs2_dirent **dent);
extern int gfs2_dirent_next(struct gfs2_inode *dip, struct gfs2_buffer_head *bh,
			    struct gfs2_dirent **dent);
extern void build_height(struct gfs2_inode *ip, int height);
extern void unstuff_dinode(struct gfs2_inode *ip);
extern unsigned int calc_tree_height(struct gfs2_inode *ip, uint64_t size);
extern int write_journal(struct gfs2_sbd *sdp, unsigned int j,
			 unsigned int blocks);

/* gfs1.c - GFS1 backward compatibility structures and functions */

#define GFS_FORMAT_SB           (100)  /* Super-Block */
#define GFS_METATYPE_SB         (1)    /* Super-Block */
#define GFS_FORMAT_FS           (1309) /* Filesystem (all-encompassing) */
#define GFS_FORMAT_MULTI        (1401) /* Multi-Host */
/* GFS1 Dinode types  */
#define GFS_FILE_NON            (0)
#define GFS_FILE_REG            (1)    /* regular file */
#define GFS_FILE_DIR            (2)    /* directory */
#define GFS_FILE_LNK            (5)    /* link */
#define GFS_FILE_BLK            (7)    /* block device node */
#define GFS_FILE_CHR            (8)    /* character device node */
#define GFS_FILE_FIFO           (101)  /* fifo/pipe */
#define GFS_FILE_SOCK           (102)  /* socket */

/* GFS 1 journal block types: */
#define GFS_LOG_DESC_METADATA   (300)    /* metadata */
#define GFS_LOG_DESC_IUL        (400)    /* unlinked inode */
#define GFS_LOG_DESC_IDA        (401)    /* de-allocated inode */
#define GFS_LOG_DESC_Q          (402)    /* quota */
#define GFS_LOG_DESC_LAST       (500)    /* final in a logged transaction */

struct gfs_indirect {
	struct gfs2_meta_header in_header;

	char in_reserved[64];
};

struct gfs_dinode {
	struct gfs2_meta_header di_header;

	struct gfs2_inum di_num; /* formal inode # and block address */

	uint32_t di_mode;	/* mode of file */
	uint32_t di_uid;	/* owner's user id */
	uint32_t di_gid;	/* owner's group id */
	uint32_t di_nlink;	/* number (qty) of links to this file */
	uint64_t di_size;	/* number (qty) of bytes in file */
	uint64_t di_blocks;	/* number (qty) of blocks in file */
	int64_t di_atime;	/* time last accessed */
	int64_t di_mtime;	/* time last modified */
	int64_t di_ctime;	/* time last changed */

	/*  Non-zero only for character or block device nodes  */
	uint32_t di_major;	/* device major number */
	uint32_t di_minor;	/* device minor number */

	/*  Block allocation strategy  */
	uint64_t di_rgrp;	/* dinode rgrp block number */
	uint64_t di_goal_rgrp;	/* rgrp to alloc from next */
	uint32_t di_goal_dblk;	/* data block goal */
	uint32_t di_goal_mblk;	/* metadata block goal */

	uint32_t di_flags;	/* GFS_DIF_... */

	/*  struct gfs_rindex, struct gfs_jindex, or struct gfs_dirent */
	uint32_t di_payload_format;  /* GFS_FORMAT_... */
	uint16_t di_type;	/* GFS_FILE_... type of file */
	uint16_t di_height;	/* height of metadata (0 == stuffed) */
	uint32_t di_incarn;	/* incarnation (unused, see gfs_meta_header) */
	uint16_t di_pad;

	/*  These only apply to directories  */
	uint16_t di_depth;	/* Number of bits in the table */
	uint32_t di_entries;	/* The # (qty) of entries in the directory */

	/*  This formed an on-disk chain of unused dinodes  */
	struct gfs2_inum di_next_unused;  /* used in old versions only */

	uint64_t di_eattr;	/* extended attribute block number */

	char di_reserved[56];
};

struct gfs_sb {
	/*  Order is important; need to be able to read old superblocks
	    in order to support on-disk version upgrades */
	struct gfs2_meta_header sb_header;

	uint32_t sb_fs_format;         /* GFS_FORMAT_FS (on-disk version) */
	uint32_t sb_multihost_format;  /* GFS_FORMAT_MULTI */
	uint32_t sb_flags;             /* ?? */

	uint32_t sb_bsize;             /* fundamental FS block size in bytes */
	uint32_t sb_bsize_shift;       /* log2(sb_bsize) */
	uint32_t sb_seg_size;          /* Journal segment size in FS blocks */

	/* These special inodes do not appear in any on-disk directory. */
	struct gfs2_inum sb_jindex_di;  /* journal index inode */
	struct gfs2_inum sb_rindex_di;  /* resource group index inode */
	struct gfs2_inum sb_root_di;    /* root directory inode */

	/* Default inter-node locking protocol (lock module) and namespace */
	char sb_lockproto[GFS2_LOCKNAME_LEN]; /* lock protocol name */
	char sb_locktable[GFS2_LOCKNAME_LEN]; /* unique name for this FS */

	/* More special inodes */
	struct gfs2_inum sb_quota_di;   /* quota inode */
	struct gfs2_inum sb_license_di; /* license inode */

	char sb_reserved[96];
};

struct gfs_rgrp {
	struct gfs2_meta_header rg_header;

	uint32_t rg_flags;      /* ?? */

	uint32_t rg_free;       /* Number (qty) of free data blocks */

	/* Dinodes are USEDMETA, but are handled separately from other METAs */
	uint32_t rg_useddi;     /* Number (qty) of dinodes (used or free) */
	uint32_t rg_freedi;     /* Number (qty) of unused (free) dinodes */
	struct gfs2_inum rg_freedi_list; /* 1st block in chain of free dinodes */

	/* These META statistics do not include dinodes (used or free) */
	uint32_t rg_usedmeta;   /* Number (qty) of used metadata blocks */
	uint32_t rg_freemeta;   /* Number (qty) of unused metadata blocks */

	char rg_reserved[64];
};

struct gfs_log_header {
	struct gfs2_meta_header lh_header;

	uint32_t lh_flags;      /* GFS_LOG_HEAD_... */
	uint32_t lh_pad;

	uint64_t lh_first;     /* Block number of first header in this trans */
	uint64_t lh_sequence;   /* Sequence number of this transaction */

	uint64_t lh_tail;       /* Block number of log tail */
	uint64_t lh_last_dump;  /* Block number of last dump */

	char lh_reserved[64];
};

struct gfs_rindex {
	uint64_t ri_addr;     /* block # of 1st block (header) in rgrp */
	uint32_t ri_length;   /* # fs blocks containing rgrp header & bitmap */
	uint32_t ri_pad;

	uint64_t ri_data1;    /* block # of first data/meta block in rgrp */
	uint32_t ri_data;     /* number (qty) of data/meta blocks in rgrp */

	uint32_t ri_bitbytes; /* total # bytes used by block alloc bitmap */

	char ri_reserved[64];
};

struct gfs_jindex {
        uint64_t ji_addr;       /* starting block of the journal */
        uint32_t ji_nsegment;   /* number (quantity) of segments in journal */
        uint32_t ji_pad;

        char ji_reserved[64];
};

struct gfs_log_descriptor {
	struct gfs2_meta_header ld_header;

	uint32_t ld_type;       /* GFS_LOG_DESC_... Type of this log chunk */
	uint32_t ld_length;     /* Number of buffers in this chunk */
	uint32_t ld_data1;      /* descriptor-specific field */
	uint32_t ld_data2;      /* descriptor-specific field */
	char ld_reserved[64];
};

extern int is_gfs_dir(struct gfs2_dinode *dinode);
extern void gfs1_lookup_block(struct gfs2_inode *ip,
			      struct gfs2_buffer_head *bh,
			      unsigned int height, struct metapath *mp,
			      int create, int *new, uint64_t *block);
extern void gfs1_block_map(struct gfs2_inode *ip, uint64_t lblock, int *new,
			   uint64_t *dblock, uint32_t *extlen, int prealloc);
extern int gfs1_writei(struct gfs2_inode *ip, char *buf, uint64_t offset,
		       unsigned int size);
extern int gfs1_ri_update(struct gfs2_sbd *sdp, int fd, int *rgcount, int quiet);
extern struct gfs2_inode *gfs_inode_get(struct gfs2_sbd *sdp,
					struct gfs2_buffer_head *bh);
extern struct gfs2_inode *gfs_inode_read(struct gfs2_sbd *sdp,
					 uint64_t di_addr);
extern void gfs_jindex_in(struct gfs_jindex *jindex, char *buf);
extern void gfs_rgrp_in(struct gfs_rgrp *rg, struct gfs2_buffer_head *bh);
extern void gfs_rgrp_out(struct gfs_rgrp *rg, struct gfs2_buffer_head *bh);
extern void gfs_get_leaf_nr(struct gfs2_inode *dip, uint32_t lindex,
			    uint64_t *leaf_out);
extern void gfs_put_leaf_nr(struct gfs2_inode *dip, uint32_t inx,
			    uint64_t leaf_out);

/* gfs2_log.c */

extern int print_level;

#define MSG_DEBUG       7
#define MSG_INFO        6
#define MSG_NOTICE      5
#define MSG_WARN        4
#define MSG_ERROR       3
#define MSG_CRITICAL    2
#define MSG_NULL        1

#define log_debug(format...) \
	do { if (print_level >= MSG_DEBUG) { \
		printf("(%s:%d) ", __FUNCTION__, __LINE__); \
		printf(format); } } while(0)

#define log_info(format...) \
	do { if (print_level >= MSG_INFO) printf(format); } while(0)

#define log_notice(format...) \
	do { if (print_level >= MSG_NOTICE) printf(format); } while(0)

#define log_warn(format...) \
	do { if (print_level >= MSG_WARN) printf(format); } while(0)

#define log_err(format...) \
	do { if (print_level >= MSG_ERROR) fprintf(stderr, format); } while(0)

#define log_crit(format...) \
	do { if (print_level >= MSG_CRITICAL) fprintf(stderr, format); } while(0)

extern void increase_verbosity(void);
extern void decrease_verbosity(void);
/* misc.c */

extern int compute_heightsize(struct gfs2_sbd *sdp, uint64_t *heightsize,
		uint32_t *maxheight, uint32_t bsize1, int diptrs, int inptrs);
extern int compute_constants(struct gfs2_sbd *sdp);
extern int is_pathname_mounted(struct gfs2_sbd *sdp, int *ro_mount);
extern int is_gfs2(struct gfs2_sbd *sdp);
extern int find_gfs2_meta(struct gfs2_sbd *sdp);
extern int dir_exists(const char *dir);
extern int check_for_gfs2(struct gfs2_sbd *sdp);
extern int mount_gfs2_meta(struct gfs2_sbd *sdp);
extern void cleanup_metafs(struct gfs2_sbd *sdp);
extern char *find_debugfs_mount(void);
extern char *mp2fsname(char *mp);
extern char *get_sysfs(const char *fsname, const char *filename);
extern int set_sysfs(const char *fsname, const char *filename, const char *val);
extern int is_fsname(char *name);
extern void get_random_bytes(void *buf, int nbytes);

/* recovery.c */
extern void gfs2_replay_incr_blk(struct gfs2_inode *ip, unsigned int *blk);
extern int gfs2_replay_read_block(struct gfs2_inode *ip, unsigned int blk,
				  struct gfs2_buffer_head **bh);
extern int gfs2_revoke_add(struct gfs2_sbd *sdp, uint64_t blkno, unsigned int where);
extern int gfs2_revoke_check(struct gfs2_sbd *sdp, uint64_t blkno,
			     unsigned int where);
extern void gfs2_revoke_clean(struct gfs2_sbd *sdp);
extern int get_log_header(struct gfs2_inode *ip, unsigned int blk,
			  struct gfs2_log_header *head);
extern int find_good_lh(struct gfs2_inode *ip, unsigned int *blk,
			struct gfs2_log_header *head);
extern int jhead_scan(struct gfs2_inode *ip, struct gfs2_log_header *head);
extern int gfs2_find_jhead(struct gfs2_inode *ip, struct gfs2_log_header *head);
extern int clean_journal(struct gfs2_inode *ip, struct gfs2_log_header *head);

/* rgrp.c */
extern int gfs2_compute_bitstructs(struct gfs2_sbd *sdp, struct rgrp_tree *rgd);
extern struct rgrp_tree *gfs2_blk2rgrpd(struct gfs2_sbd *sdp, uint64_t blk);
extern uint64_t gfs2_rgrp_read(struct gfs2_sbd *sdp, struct rgrp_tree *rgd);
extern void gfs2_rgrp_relse(struct rgrp_tree *rgd);
extern struct rgrp_tree *rgrp_insert(struct osi_root *rgtree,
				     uint64_t rgblock);
extern void gfs2_rgrp_free(struct osi_root *rgrp_tree);
/* figure out the size of the given resource group, in blocks */
static inline unsigned int rgrp_size(struct rgrp_tree *rgrp)
{
	return rgrp->ri.ri_data + rgrp->ri.ri_length;
}

/* structures.c */
extern int build_master(struct gfs2_sbd *sdp);
extern void build_sb(struct gfs2_sbd *sdp, const unsigned char *uuid);
extern int build_journal(struct gfs2_sbd *sdp, int j,
			 struct gfs2_inode *jindex);
extern int build_jindex(struct gfs2_sbd *sdp);
extern int build_per_node(struct gfs2_sbd *sdp);
extern int build_inum(struct gfs2_sbd *sdp);
extern int build_statfs(struct gfs2_sbd *sdp);
extern int build_rindex(struct gfs2_sbd *sdp);
extern int build_quota(struct gfs2_sbd *sdp);
extern int build_root(struct gfs2_sbd *sdp);
extern int do_init_inum(struct gfs2_sbd *sdp);
extern int do_init_statfs(struct gfs2_sbd *sdp);
extern int gfs2_check_meta(struct gfs2_buffer_head *bh, int type);
extern int gfs2_next_rg_meta(struct rgrp_tree *rgd, uint64_t *block,
			     int first);
extern int gfs2_next_rg_metatype(struct gfs2_sbd *sdp, struct rgrp_tree *rgd,
				 uint64_t *block, uint32_t type, int first);
extern int gfs2_next_rg_freemeta(struct rgrp_tree *rgd, uint64_t *block,
				 int first);

/* super.c */
extern int check_sb(struct gfs2_sb *sb);
extern int read_sb(struct gfs2_sbd *sdp);
extern int rindex_read(struct gfs2_sbd *sdp, int fd, int *count1, int *sane);
extern int ri_update(struct gfs2_sbd *sdp, int fd, int *rgcount, int *sane);
extern int write_sb(struct gfs2_sbd *sdp);

/* ondisk.c */
extern uint32_t gfs2_disk_hash(const char *data, int len);
extern const char *str_uuid(const unsigned char *uuid);
extern void gfs2_print_uuid(const unsigned char *uuid);
extern void print_it(const char *label, const char *fmt, const char *fmt2, ...)
	__attribute__((format(printf,2,4)));

/* Translation functions */

extern void gfs2_inum_in(struct gfs2_inum *no, char *buf);
extern void gfs2_inum_out(struct gfs2_inum *no, char *buf);
extern void gfs2_meta_header_in(struct gfs2_meta_header *mh,
				struct gfs2_buffer_head *bh);
extern void gfs2_meta_header_out(struct gfs2_meta_header *mh,
				 struct gfs2_buffer_head *bh);
extern void gfs2_sb_in(struct gfs2_sb *sb, struct gfs2_buffer_head *bh);
extern void gfs2_sb_out(struct gfs2_sb *sb, struct gfs2_buffer_head *bh);
extern void gfs2_rindex_in(struct gfs2_rindex *ri, char *buf);
extern void gfs2_rindex_out(struct gfs2_rindex *ri, char *buf);
extern void gfs2_rgrp_in(struct gfs2_rgrp *rg, struct gfs2_buffer_head *bh);
extern void gfs2_rgrp_out(struct gfs2_rgrp *rg, struct gfs2_buffer_head *bh);
extern void gfs2_quota_in(struct gfs2_quota *qu, char *buf);
extern void gfs2_quota_out(struct gfs2_quota *qu, char *buf);
extern void gfs2_dinode_in(struct gfs2_dinode *di,
			   struct gfs2_buffer_head *bh);
extern void gfs2_dinode_out(struct gfs2_dinode *di,
			    struct gfs2_buffer_head *bh);
extern void gfs2_dirent_in(struct gfs2_dirent *de, char *buf);
extern void gfs2_dirent_out(struct gfs2_dirent *de, char *buf);
extern void gfs2_leaf_in(struct gfs2_leaf *lf, struct gfs2_buffer_head *bh);
extern void gfs2_leaf_out(struct gfs2_leaf *lf, struct gfs2_buffer_head *bh);
extern void gfs2_ea_header_in(struct gfs2_ea_header *ea, char *buf);
extern void gfs2_ea_header_out(struct gfs2_ea_header *ea, char *buf);
extern void gfs2_log_header_in(struct gfs2_log_header *lh,
			       struct gfs2_buffer_head *bh);
extern void gfs2_log_header_out(struct gfs2_log_header *lh,
				struct gfs2_buffer_head *bh);
extern void gfs2_log_descriptor_in(struct gfs2_log_descriptor *ld,
				   struct gfs2_buffer_head *bh);
extern void gfs2_log_descriptor_out(struct gfs2_log_descriptor *ld,
				    struct gfs2_buffer_head *bh);
extern void gfs2_statfs_change_in(struct gfs2_statfs_change *sc, char *buf);
extern void gfs2_statfs_change_out(struct gfs2_statfs_change *sc, char *buf);
extern void gfs2_quota_change_in(struct gfs2_quota_change *qc,
				 struct gfs2_buffer_head *bh);
extern void gfs2_quota_change_out(struct gfs2_quota_change *qc,
				  struct gfs2_buffer_head *bh);

/* Printing functions */

extern void gfs2_inum_print(struct gfs2_inum *no);
extern void gfs2_meta_header_print(struct gfs2_meta_header *mh);
extern void gfs2_sb_print(struct gfs2_sb *sb);
extern void gfs2_rindex_print(struct gfs2_rindex *ri);
extern void gfs2_rgrp_print(struct gfs2_rgrp *rg);
extern void gfs2_quota_print(struct gfs2_quota *qu);
extern void gfs2_dinode_print(struct gfs2_dinode *di);
extern void gfs2_dirent_print(struct gfs2_dirent *de, char *name);
extern void gfs2_leaf_print(struct gfs2_leaf *lf);
extern void gfs2_ea_header_print(struct gfs2_ea_header *ea, char *name);
extern void gfs2_log_header_print(struct gfs2_log_header *lh);
extern void gfs2_log_descriptor_print(struct gfs2_log_descriptor *ld);
extern void gfs2_statfs_change_print(struct gfs2_statfs_change *sc);
extern void gfs2_quota_change_print(struct gfs2_quota_change *qc);

__END_DECLS

#endif /* __LIBGFS2_DOT_H__ */
