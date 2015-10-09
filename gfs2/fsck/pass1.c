/* pass1 checks inodes for format & type, duplicate blocks, & incorrect
 * block count.
 *
 * It builds up tables that contains the state of each block (free,
 * block in use, metadata type, etc), as well as bad blocks and
 * duplicate blocks.  (See block_list.[ch] for more info)
 *
 */

#include <stdio.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#include <inttypes.h>
#include <libintl.h>
#define _(String) gettext(String)

#include "libgfs2.h"
#include "fsck.h"
#include "inode_hash.h"
#include "util.h"
#include "link.h"
#include "metawalk.h"

struct special_blocks gfs1_rindex_blks;

struct block_count {
	uint64_t indir_count;
	uint64_t data_count;
	uint64_t ea_count;
};

static int check_leaf(struct gfs2_inode *ip, uint64_t block, void *private);
static int check_metalist(struct gfs2_inode *ip, uint64_t block,
			  struct gfs2_buffer_head **bh, int h, void *private);
static int undo_check_metalist(struct gfs2_inode *ip, uint64_t block,
			       struct gfs2_buffer_head **bh, int h,
			       void *private);
static int check_data(struct gfs2_inode *ip, uint64_t block, void *private);
static int undo_check_data(struct gfs2_inode *ip, uint64_t block,
			   void *private);
static int check_eattr_indir(struct gfs2_inode *ip, uint64_t indirect,
			     uint64_t parent, struct gfs2_buffer_head **bh,
			     void *private);
static int check_eattr_leaf(struct gfs2_inode *ip, uint64_t block,
			    uint64_t parent, struct gfs2_buffer_head **bh,
			    void *private);
static int check_eattr_entries(struct gfs2_inode *ip,
			       struct gfs2_buffer_head *leaf_bh,
			       struct gfs2_ea_header *ea_hdr,
			       struct gfs2_ea_header *ea_hdr_prev,
			       void *private);
static int check_extended_leaf_eattr(struct gfs2_inode *ip, uint64_t *data_ptr,
				     struct gfs2_buffer_head *leaf_bh,
				     struct gfs2_ea_header *ea_hdr,
				     struct gfs2_ea_header *ea_hdr_prev,
				     void *private);
static int check_num_ptrs(struct gfs2_inode *ip, uint64_t leafno,
			  int *ref_count, int *lindex, struct gfs2_leaf *leaf);
static int finish_eattr_indir(struct gfs2_inode *ip, int leaf_pointers,
			      int leaf_pointer_errors, void *private);
static int invalidate_metadata(struct gfs2_inode *ip, uint64_t block,
			       struct gfs2_buffer_head **bh, int h,
			       void *private);
static int invalidate_leaf(struct gfs2_inode *ip, uint64_t block,
			   void *private);
static int invalidate_data(struct gfs2_inode *ip, uint64_t block,
			   void *private);
static int invalidate_eattr_indir(struct gfs2_inode *ip, uint64_t block,
				  uint64_t parent,
				  struct gfs2_buffer_head **bh,
				  void *private);
static int invalidate_eattr_leaf(struct gfs2_inode *ip, uint64_t block,
				 uint64_t parent, struct gfs2_buffer_head **bh,
				 void *private);
static int handle_ip(struct gfs2_sbd *sdp, struct gfs2_inode *ip);

struct metawalk_fxns pass1_fxns = {
	.private = NULL,
	.check_leaf = check_leaf,
	.check_metalist = check_metalist,
	.check_data = check_data,
	.check_eattr_indir = check_eattr_indir,
	.check_eattr_leaf = check_eattr_leaf,
	.check_dentry = NULL,
	.check_eattr_entry = check_eattr_entries,
	.check_eattr_extentry = check_extended_leaf_eattr,
	.finish_eattr_indir = finish_eattr_indir,
	.big_file_msg = big_file_comfort,
	.check_num_ptrs = check_num_ptrs,
};

struct metawalk_fxns undo_fxns = {
	.private = NULL,
	.check_metalist = undo_check_metalist,
	.check_data = undo_check_data,
};

struct metawalk_fxns invalidate_fxns = {
	.private = NULL,
	.check_metalist = invalidate_metadata,
	.check_data = invalidate_data,
	.check_leaf = invalidate_leaf,
	.check_eattr_indir = invalidate_eattr_indir,
	.check_eattr_leaf = invalidate_eattr_leaf,
};

/*
 * resuscitate_metalist - make sure a system directory entry's metadata blocks
 *                        are marked "in use" in the bitmap.
 *
 * This function makes sure metadata blocks for system and root directories are
 * marked "in use" by the bitmap.  You don't want root's indirect blocks
 * deleted, do you? Or worse, reused for lost+found.
 */
static int resuscitate_metalist(struct gfs2_inode *ip, uint64_t block,
				struct gfs2_buffer_head **bh, int h,
				void *private)
{
	struct block_count *bc = (struct block_count *)private;

	*bh = NULL;
	if (!valid_block(ip->i_sbd, block)){ /* blk outside of FS */
		fsck_blockmap_set(ip, ip->i_di.di_num.no_addr,
				  _("itself"), gfs2_bad_block);
		log_err( _("Bad indirect block pointer (invalid or out of "
			   "range) found in system inode %lld (0x%llx).\n"),
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)ip->i_di.di_num.no_addr);
		return 1;
	}
	if (fsck_system_inode(ip->i_sbd, block))
		fsck_blockmap_set(ip, block, _("system file"), gfs2_indir_blk);
	else
		check_n_fix_bitmap(ip->i_sbd, block, gfs2_indir_blk);
	bc->indir_count++;
	return 0;
}

/*
 * resuscitate_dentry - make sure a system directory entry is alive
 *
 * This function makes sure directory entries in system directories are
 * kept alive.  You don't want journal0 deleted from jindex, do you?
 */
static int resuscitate_dentry(struct gfs2_inode *ip, struct gfs2_dirent *dent,
			      struct gfs2_dirent *prev_de,
			      struct gfs2_buffer_head *bh, char *filename,
			      uint32_t *count, void *priv)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_dirent dentry, *de;
	char tmp_name[PATH_MAX];
	uint64_t block;
	enum gfs2_mark_block dinode_type;

	memset(&dentry, 0, sizeof(struct gfs2_dirent));
	gfs2_dirent_in(&dentry, (char *)dent);
	de = &dentry;
	block = de->de_inum.no_addr;
	/* Start of checks */
	memset(tmp_name, 0, sizeof(tmp_name));
	if (de->de_name_len < sizeof(tmp_name))
		strncpy(tmp_name, filename, de->de_name_len);
	else
		strncpy(tmp_name, filename, sizeof(tmp_name) - 1);
	if (!valid_block(sdp, block)) {
		log_err( _("Block # referenced by system directory entry %s "
			   "in inode %lld (0x%llx) is invalid or out of range;"
			   " ignored.\n"),
			 tmp_name, (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)ip->i_di.di_num.no_addr);
		return 0;
	}
	if (block == sdp->md.jiinode->i_di.di_num.no_addr)
		dinode_type = gfs2_inode_dir;
	else if (!sdp->gfs1 && (block == sdp->md.pinode->i_di.di_num.no_addr ||
				block == sdp->master_dir->i_di.di_num.no_addr))
		dinode_type = gfs2_inode_dir;
	else
		dinode_type = gfs2_inode_file;
	/* If this is a system dinode, we'll handle it later in
	   check_system_inodes.  If not, it'll be handled by pass1 but
	   since it's in a system directory we need to make sure it's
	   represented in the rgrp bitmap. */
	if (fsck_system_inode(sdp, block))
		fsck_blockmap_set(ip, block, _("system file"), dinode_type);
	else
		check_n_fix_bitmap(sdp, block, dinode_type);
	/* Return the number of leaf entries so metawalk doesn't flag this
	   leaf as having none. */
	*count = be16_to_cpu(((struct gfs2_leaf *)bh->b_data)->lf_entries);
	return 0;
}

struct metawalk_fxns sysdir_fxns = {
	.private = NULL,
	.check_metalist = resuscitate_metalist,
	.check_dentry = resuscitate_dentry,
};

/*
 * fix_leaf_pointers - fix a directory dinode that has a number of pointers
 *                     that is not a multiple of 2.
 * dip - the directory inode having the problem
 * lindex - the index of the leaf right after the problem (need to back up)
 * cur_numleafs - current (incorrect) number of instances of the leaf block
 * correct_numleafs - the correct number instances of the leaf block
 */
static int fix_leaf_pointers(struct gfs2_inode *dip, int *lindex,
			     int cur_numleafs, int correct_numleafs)
{
	int count;
	char *ptrbuf;
	int start_lindex = *lindex - cur_numleafs; /* start of bad ptrs */
	int tot_num_ptrs = (1 << dip->i_di.di_depth) - start_lindex;
	int bufsize = tot_num_ptrs * sizeof(uint64_t);
	int off_by = cur_numleafs - correct_numleafs;

	ptrbuf = malloc(bufsize);
	if (!ptrbuf) {
		log_err( _("Error: Cannot allocate memory to fix the leaf "
			   "pointers.\n"));
		return -1;
	}
	/* Read all the pointers, starting with the first bad one */
	count = gfs2_readi(dip, ptrbuf, start_lindex * sizeof(uint64_t),
			   bufsize);
	if (count != bufsize) {
		log_err( _("Error: bad read while fixing leaf pointers.\n"));
		free(ptrbuf);
		return -1;
	}

	bufsize -= off_by * sizeof(uint64_t); /* We need to write fewer */
	/* Write the same pointers, but offset them so they fit within the
	   smaller factor of 2. So if we have 12 pointers, write out only
	   the last 8 of them.  If we have 7, write the last 4, etc.
	   We need to write these starting at the current lindex and adjust
	   lindex accordingly. */
	if (dip->i_sbd->gfs1)
		count = gfs1_writei(dip, ptrbuf + (off_by * sizeof(uint64_t)),
				    start_lindex * sizeof(uint64_t), bufsize);
	else
		count = gfs2_writei(dip, ptrbuf + (off_by * sizeof(uint64_t)),
				    start_lindex * sizeof(uint64_t), bufsize);
	if (count != bufsize) {
		log_err( _("Error: bad read while fixing leaf pointers.\n"));
		free(ptrbuf);
		return -1;
	}
	/* Now zero out the hole left at the end */
	memset(ptrbuf, 0, off_by * sizeof(uint64_t));
	if (dip->i_sbd->gfs1)
		gfs1_writei(dip, ptrbuf, (start_lindex * sizeof(uint64_t)) +
			    bufsize, off_by * sizeof(uint64_t));
	else
		gfs2_writei(dip, ptrbuf, (start_lindex * sizeof(uint64_t)) +
			    bufsize, off_by * sizeof(uint64_t));
	free(ptrbuf);
	*lindex -= off_by; /* adjust leaf index to account for the change */
	return 0;
}

/**
 * check_num_ptrs - check a previously processed leaf's pointer count in the
 *                  hash table.
 *
 * The number of pointers in a directory hash table that point to any given
 * leaf block should always be a factor of two.  The difference between the
 * leaf block's depth and the dinode's di_depth gives us the factor.
 * This function makes sure the leaf follows the rules properly.
 *
 * ip - pointer to the in-core inode structure
 * leafno - the leaf number we're operating on
 * ref_count - the number of pointers to this leaf we actually counted.
 * exp_count - the number of pointers to this leaf we expect based on
 *             ip depth minus leaf depth.
 * lindex - leaf index number
 * leaf - the leaf structure for the leaf block to check
 */
static int check_num_ptrs(struct gfs2_inode *ip, uint64_t leafno,
			  int *ref_count, int *lindex, struct gfs2_leaf *leaf)
{
	int factor = 0, divisor = *ref_count, multiple = 1, error = 0;
	struct gfs2_buffer_head *lbh;
	int exp_count;

	/* Check to see if the number of pointers we found is a power of 2.
	   It needs to be and if it's not we need to fix it.*/
	while (divisor > 1) {
		factor++;
		divisor /= 2;
		multiple = multiple << 1;
	}
	if (*ref_count != multiple) {
		log_err( _("Directory #%llu (0x%llx) has an invalid number of "
			   "pointers to leaf #%llu (0x%llx)\n\tFound: %u, "
			   "which is not a factor of 2.\n"),
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)leafno,
			 (unsigned long long)leafno, *ref_count);
		if (!query( _("Attempt to fix it? (y/n) "))) {
			log_err( _("Directory inode was not fixed.\n"));
			return 1;
		}
		error = fix_leaf_pointers(ip, lindex, *ref_count, multiple);
		if (error)
			return error;
		*ref_count = multiple;
		log_err( _("Directory inode was fixed.\n"));
	}
	/* Check to see if the counted number of leaf pointers is what we
	   expect based on the leaf depth. */
	exp_count = (1 << (ip->i_di.di_depth - leaf->lf_depth));
	if (*ref_count != exp_count) {
		log_err( _("Directory #%llu (0x%llx) has an incorrect number "
			   "of pointers to leaf #%llu (0x%llx)\n\tFound: "
			   "%u,  Expected: %u\n"),
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)leafno,
			 (unsigned long long)leafno, *ref_count, exp_count);
		if (!query( _("Attempt to fix it? (y/n) "))) {
			log_err( _("Directory leaf was not fixed.\n"));
			return 1;
		}
		lbh = bread(ip->i_sbd, leafno);
		gfs2_leaf_in(leaf, lbh);
		log_err( _("Leaf depth was %d, changed to %d\n"),
			 leaf->lf_depth, ip->i_di.di_depth - factor);
		leaf->lf_depth = ip->i_di.di_depth - factor;
		gfs2_leaf_out(leaf, lbh);
		brelse(lbh);
		log_err( _("Directory leaf was fixed.\n"));
	}
	return 0;
}

static int check_leaf(struct gfs2_inode *ip, uint64_t block, void *private)
{
	struct block_count *bc = (struct block_count *) private;
	uint8_t q;

	/* Note if we've gotten this far, the block has already passed the
	   check in metawalk: gfs2_check_meta(lbh, GFS2_METATYPE_LF).
	   So we know it's a leaf block. */
	q = block_type(block);
	if (q != gfs2_block_free) {
		log_err( _("Found duplicate block %llu (0x%llx) referenced "
			   "as a directory leaf in dinode "
			   "%llu (0x%llx) - was marked %d (%s)\n"),
			 (unsigned long long)block,
			 (unsigned long long)block,
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)ip->i_di.di_num.no_addr, q,
			 block_type_string(q));
		add_duplicate_ref(ip, block, ref_as_meta, 0, INODE_VALID);
		if (q == gfs2_leaf_blk) /* If the previous reference also saw
					   this as a leaf, it was already
					   checked, so don't check again. */
			return -EEXIST;
	}
	fsck_blockmap_set(ip, block, _("directory leaf"), gfs2_leaf_blk);
	bc->indir_count++;
	return 0;
}

static int check_metalist(struct gfs2_inode *ip, uint64_t block,
			  struct gfs2_buffer_head **bh, int h, void *private)
{
	uint8_t q;
	int found_dup = 0, iblk_type;
	struct gfs2_buffer_head *nbh;
	struct block_count *bc = (struct block_count *)private;
	const char *blktypedesc;

	*bh = NULL;

	if (!valid_block(ip->i_sbd, block)) { /* blk outside of FS */
		/* The bad dinode should be invalidated later due to
		   "unrecoverable" errors.  The inode itself should be
		   set "free" and removed from the inodetree by
		   undo_check_metalist. */
		fsck_blockmap_set(ip, ip->i_di.di_num.no_addr,
				  _("bad block referencing"), gfs2_bad_block);
		log_debug( _("Bad indirect block (invalid/out of range) "
			     "found in inode %lld (0x%llx).\n"),
			   (unsigned long long)ip->i_di.di_num.no_addr,
			   (unsigned long long)ip->i_di.di_num.no_addr);

		return 1;
	}
	if (is_dir(&ip->i_di, ip->i_sbd->gfs1) && h == ip->i_di.di_height) {
		iblk_type = GFS2_METATYPE_JD;
		blktypedesc = _("a directory hash table block");
	} else {
		iblk_type = GFS2_METATYPE_IN;
		blktypedesc = _("a journaled data block");
	}
	q = block_type(block);
	if (q != gfs2_block_free) {
		log_err( _("Found duplicate block %llu (0x%llx) referenced "
			   "as metadata in indirect block for dinode "
			   "%llu (0x%llx) - was marked %d (%s)\n"),
			 (unsigned long long)block,
			 (unsigned long long)block,
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)ip->i_di.di_num.no_addr, q,
			 block_type_string(q));
		add_duplicate_ref(ip, block, ref_as_meta, 0, INODE_VALID);
		found_dup = 1;
	}
	nbh = bread(ip->i_sbd, block);

	if (gfs2_check_meta(nbh, iblk_type)){
		log_err( _("Inode %lld (0x%llx) has a bad indirect block "
			   "pointer %lld (0x%llx) (points to something "
			   "that is not %s).\n"),
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)block,
			 (unsigned long long)block, blktypedesc);
		if (!found_dup) {
			fsck_blockmap_set(ip, block, _("bad indirect"),
					  gfs2_meta_inval);
			brelse(nbh);
			nbh = NULL;
			return 1;
		}
		brelse(nbh);
		nbh = NULL;
	} else /* blk check ok */
		*bh = nbh;

	bc->indir_count++;
	if (found_dup) {
		if (nbh) {
			brelse(nbh);
			nbh = NULL;
			*bh = NULL;
		}
		return 1; /* don't process the metadata again */
	} else
		fsck_blockmap_set(ip, block, _("indirect"),
				  gfs2_indir_blk);

	return 0;
}

static int undo_check_metalist(struct gfs2_inode *ip, uint64_t block,
			       struct gfs2_buffer_head **bh, int h,
			       void *private)
{
	int found_dup = 0, iblk_type;
	struct gfs2_buffer_head *nbh;
	struct block_count *bc = (struct block_count *)private;

	*bh = NULL;

	if (!valid_block(ip->i_sbd, block)) { /* blk outside of FS */
		fsck_blockmap_set(ip, ip->i_di.di_num.no_addr,
				  _("bad block referencing"), gfs2_block_free);
		return 1;
	}
	if (is_dir(&ip->i_di, ip->i_sbd->gfs1) && h == ip->i_di.di_height)
		iblk_type = GFS2_METATYPE_JD;
	else
		iblk_type = GFS2_METATYPE_IN;

	found_dup = find_remove_dup(ip, block, _("Metadata"));
	nbh = bread(ip->i_sbd, block);

	if (gfs2_check_meta(nbh, iblk_type)) {
		if (!found_dup) {
			fsck_blockmap_set(ip, block, _("bad indirect"),
					  gfs2_block_free);
			brelse(nbh);
			return 1;
		}
		brelse(nbh);
		nbh = NULL;
	} else /* blk check ok */
		*bh = nbh;

	bc->indir_count--;
	if (found_dup) {
		if (nbh)
			brelse(nbh);
		*bh = NULL;
		return 1; /* don't process the metadata again */
	} else
		fsck_blockmap_set(ip, block, _("bad indirect"),
				  gfs2_block_free);
	return 0;
}

static int check_data(struct gfs2_inode *ip, uint64_t block, void *private)
{
	uint8_t q;
	struct block_count *bc = (struct block_count *) private;

	if (!valid_block(ip->i_sbd, block)) {
		log_err( _("inode %lld (0x%llx) has a bad data block pointer "
			   "%lld (invalid or out of range)\n"),
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)block);
		/* Mark the owner of this block with the bad_block
		 * designator so we know to check it for out of range
		 * blocks later */
		fsck_blockmap_set(ip, ip->i_di.di_num.no_addr,
				  _("bad (out of range) data"),
				  gfs2_bad_block);
		return 1;
	}
	q = block_type(block);
	if (q != gfs2_block_free) {
		log_err( _("Found duplicate %s block %llu (0x%llx) "
			   "referenced as data by dinode %llu (0x%llx)\n"),
			 block_type_string(q),
			 (unsigned long long)block,
			 (unsigned long long)block,
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)ip->i_di.di_num.no_addr);
		if (q != gfs2_meta_inval) {
			log_info( _("Seems to be a normal duplicate; I'll "
				    "sort it out in pass1b.\n"));
			add_duplicate_ref(ip, block, ref_as_data, 0,
					  INODE_VALID);
			/* If the prev ref was as data, this is likely a data
			   block, so keep the block count for both refs. */
			if (q == gfs2_block_used)
				bc->data_count++;
			return 1;
		}
		log_info( _("The block was invalid as metadata but might be "
			    "okay as data.  I'll sort it out in pass1b.\n"));
		add_duplicate_ref(ip, block, ref_as_data, 0, INODE_VALID);
		bc->data_count++;
		return 1;
	}
	/* In gfs1, rgrp indirect blocks are marked in the bitmap as "meta".
	   In gfs2, "meta" is only for dinodes. So here we dummy up the
	   blocks so that the bitmap isn't changed improperly. */
	if (ip->i_sbd->gfs1 && ip == ip->i_sbd->md.riinode) {
		log_info(_("Block %lld (0x%llx) is a GFS1 rindex block\n"),
			 (unsigned long long)block, (unsigned long long)block);
		gfs2_special_set(&gfs1_rindex_blks, block);
		fsck_blockmap_set(ip, block, _("rgrp"), gfs2_indir_blk);
		/*gfs2_meta_rgrp);*/
	} else if (ip->i_sbd->gfs1 && ip->i_di.di_flags & GFS2_DIF_JDATA) {
		log_info(_("Block %lld (0x%llx) is a GFS1 journaled data "
			   "block\n"),
			 (unsigned long long)block, (unsigned long long)block);
		fsck_blockmap_set(ip, block, _("jdata"), gfs2_jdata);
	} else
		fsck_blockmap_set(ip, block, _("data"), gfs2_block_used);
	bc->data_count++;
	return 0;
}

static int undo_check_data(struct gfs2_inode *ip, uint64_t block,
			   void *private)
{
	struct block_count *bc = (struct block_count *) private;

	if (!valid_block(ip->i_sbd, block)) {
		/* Mark the owner of this block with the bad_block
		 * designator so we know to check it for out of range
		 * blocks later */
		fsck_blockmap_set(ip, ip->i_di.di_num.no_addr,
				  _("bad (invalid or out of range) data"),
				  gfs2_block_free);
		return 1;
	}
	bc->data_count--;
	return free_block_if_notdup(ip, block, _("data"));
}

static int remove_inode_eattr(struct gfs2_inode *ip, struct block_count *bc)
{
	struct duptree *dt;
	struct inode_with_dups *id;
	osi_list_t *ref;
	int moved = 0;

	/* If it's a duplicate reference to the block, we need to check
	   if the reference is on the valid or invalid inodes list.
	   If it's on the valid inode's list, move it to the invalid
	   inodes list.  The reason is simple: This inode, although
	   valid, has an now-invalid reference, so we should not give
	   this reference preferential treatment over others. */
	dt = dupfind(ip->i_di.di_eattr);
	if (dt) {
		osi_list_foreach(ref, &dt->ref_inode_list) {
			id = osi_list_entry(ref, struct inode_with_dups, list);
			if (id->block_no == ip->i_di.di_num.no_addr) {
				log_debug( _("Moving inode %lld (0x%llx)'s "
					     "duplicate reference to %lld "
					     "(0x%llx) from the valid to the "
					     "invalid reference list.\n"),
					   (unsigned long long)
					   ip->i_di.di_num.no_addr,
					   (unsigned long long)
					   ip->i_di.di_num.no_addr,
					   (unsigned long long)
					   ip->i_di.di_eattr,
					   (unsigned long long)
					   ip->i_di.di_eattr);
				/* Move from the normal to the invalid list */
				osi_list_del(&id->list);
				osi_list_add_prev(&id->list,
						  &dt->ref_invinode_list);
				moved = 1;
				break;
			}
		}
		if (!moved)
			log_debug( _("Duplicate reference to %lld "
				     "(0x%llx) not moved.\n"),
				   (unsigned long long)ip->i_di.di_eattr,
				   (unsigned long long)ip->i_di.di_eattr);
	} else {
		delete_block(ip, ip->i_di.di_eattr, NULL,
			     "extended attribute", NULL);
	}
	ip->i_di.di_eattr = 0;
	bc->ea_count = 0;
	ip->i_di.di_blocks = 1 + bc->indir_count + bc->data_count;
	ip->i_di.di_flags &= ~GFS2_DIF_EA_INDIRECT;
	bmodified(ip->i_bh);
	return 0;
}

static int ask_remove_inode_eattr(struct gfs2_inode *ip,
				  struct block_count *bc)
{
	log_err( _("Inode %lld (0x%llx) has unrecoverable Extended Attribute "
		   "errors.\n"), (unsigned long long)ip->i_di.di_num.no_addr,
		 (unsigned long long)ip->i_di.di_num.no_addr);
	if (query( _("Clear all Extended Attributes from the inode? (y/n) "))){
		if (!remove_inode_eattr(ip, bc))
			log_err( _("Extended attributes were removed.\n"));
		else
			log_err( _("Unable to remove inode eattr pointer; "
				   "the error remains.\n"));
	} else {
		log_err( _("Extended attributes were not removed.\n"));
	}
	return 0;
}

/* clear_eas - clear the extended attributes for an inode
 *
 * @ip       - in core inode pointer
 * @bc       - pointer to a block count structure
 * block     - the block that had the problem
 * duplicate - if this is a duplicate block, don't set it "free"
 * emsg      - what to tell the user about the eas being checked
 * Returns: 1 if the EA is fixed, else 0 if it was not fixed.
 */
static int clear_eas(struct gfs2_inode *ip, struct block_count *bc,
		     uint64_t block, int duplicate, const char *emsg)
{
	log_err( _("Inode #%llu (0x%llx): %s"),
		(unsigned long long)ip->i_di.di_num.no_addr,
		(unsigned long long)ip->i_di.di_num.no_addr, emsg);
	log_err( _(" at block #%lld (0x%llx).\n"),
		 (unsigned long long)block, (unsigned long long)block);
	if (query( _("Clear the bad Extended Attribute? (y/n) "))) {
		if (block == ip->i_di.di_eattr) {
			remove_inode_eattr(ip, bc);
			log_err( _("The bad extended attribute was "
				   "removed.\n"));
		} else if (!duplicate) {
			delete_block(ip, block, NULL,
				     _("bad extended attribute"), NULL);
		}
		return 1;
	} else {
		log_err( _("The bad Extended Attribute was not fixed.\n"));
		bc->ea_count++;
		return 0;
	}
}

static int check_eattr_indir(struct gfs2_inode *ip, uint64_t indirect,
			     uint64_t parent, struct gfs2_buffer_head **bh,
			     void *private)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	int ret = 0;
	uint8_t q;
	struct block_count *bc = (struct block_count *) private;

	/* This inode contains an eattr - it may be invalid, but the
	 * eattr attributes points to a non-zero block */
	if (!valid_block(sdp, indirect)) {
		/* Doesn't help to mark this here - this gets checked
		 * in pass1c */
		return 1;
	}
	q = block_type(indirect);

	/* Special duplicate processing:  If we have an EA block,
	   check if it really is an EA.  If it is, let duplicate
	   handling sort it out.  If it isn't, clear it but don't
	   count it as a duplicate. */
	*bh = bread(sdp, indirect);
	if (gfs2_check_meta(*bh, GFS2_METATYPE_IN)) {
		if (q != gfs2_block_free) { /* Duplicate? */
			add_duplicate_ref(ip, indirect, ref_as_ea, 0,
					  INODE_VALID);
			if (!clear_eas(ip, bc, indirect, 1,
				       _("Bad indirect Extended Attribute "
					 "duplicate found")))
				bc->ea_count++;
			return 1;
		}
		clear_eas(ip, bc, indirect, 0,
			  _("Extended Attribute indirect block has incorrect "
			    "type"));
		return 1;
	}
	if (q != gfs2_block_free) { /* Duplicate? */
		log_err( _("Inode #%llu (0x%llx): Duplicate Extended "
			   "Attribute indirect block found at #%llu "
			   "(0x%llx).\n"),
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)indirect,
			 (unsigned long long)indirect);
		add_duplicate_ref(ip, indirect, ref_as_ea, 0, INODE_VALID);
		bc->ea_count++;
		ret = 1;
	} else {
		fsck_blockmap_set(ip, indirect,
				  _("indirect Extended Attribute"),
				  gfs2_indir_blk);
		bc->ea_count++;
	}
	return ret;
}

static int finish_eattr_indir(struct gfs2_inode *ip, int leaf_pointers,
			      int leaf_pointer_errors, void *private)
{
	struct block_count *bc = (struct block_count *) private;
	osi_list_t *head;
	struct special_blocks *b = NULL;

	if (leaf_pointer_errors == leaf_pointers) /* All eas were bad */
		return ask_remove_inode_eattr(ip, bc);
	log_debug( _("Marking inode #%llu (0x%llx) with extended "
		     "attribute block\n"),
		   (unsigned long long)ip->i_di.di_num.no_addr,
		   (unsigned long long)ip->i_di.di_num.no_addr);
	/* Mark the inode as having an eattr in the block map
	   so pass1c can check it. We may have previously added this inode
	   to the eattr_blocks list and if we did, it would be the first
	   one on the list.  So check that one only (to save time) and
	   if that one matches, no need to add it again. */
	if (!osi_list_empty(&ip->i_sbd->eattr_blocks.list)) {
		head = &ip->i_sbd->eattr_blocks.list;
		b = osi_list_entry(head->next, struct special_blocks, list);
	}
	if (!b || b->block != ip->i_di.di_num.no_addr)
		gfs2_special_add(&ip->i_sbd->eattr_blocks,
				 ip->i_di.di_num.no_addr);
	if (!leaf_pointer_errors)
		return 0;
	log_err( _("Inode %lld (0x%llx) has recoverable indirect "
		   "Extended Attribute errors.\n"),
		   (unsigned long long)ip->i_di.di_num.no_addr,
		   (unsigned long long)ip->i_di.di_num.no_addr);
	if (query( _("Okay to fix the block count for the inode? (y/n) "))) {
		ip->i_di.di_blocks = 1 + bc->indir_count +
			bc->data_count + bc->ea_count;
		bmodified(ip->i_bh);
		log_err( _("Block count fixed.\n"));
		return 1;
	}
	log_err( _("Block count not fixed.\n"));
	return 1;
}

/* check_ealeaf_block
 *      checks an extended attribute (not directory) leaf block
 */
static int check_ealeaf_block(struct gfs2_inode *ip, uint64_t block, int btype,
			      struct gfs2_buffer_head **bh, void *private)
{
	struct gfs2_buffer_head *leaf_bh = NULL;
	struct gfs2_sbd *sdp = ip->i_sbd;
	uint8_t q;
	struct block_count *bc = (struct block_count *) private;

	q = block_type(block);
	/* Special duplicate processing:  If we have an EA block, check if it
	   really is an EA.  If it is, let duplicate handling sort it out.
	   If it isn't, clear it but don't count it as a duplicate. */
	leaf_bh = bread(sdp, block);
	if (gfs2_check_meta(leaf_bh, btype)) {
		if (q != gfs2_block_free) { /* Duplicate? */
			add_duplicate_ref(ip, block, ref_as_ea, 0,
					  INODE_VALID);
			clear_eas(ip, bc, block, 1,
				  _("Bad Extended Attribute duplicate found"));
		} else {
			clear_eas(ip, bc, block, 0,
				  _("Extended Attribute leaf block "
				    "has incorrect type"));
		}
		brelse(leaf_bh);
		return 1;
	}
	if (q != gfs2_block_free) { /* Duplicate? */
		log_debug( _("Duplicate block found at #%lld (0x%llx).\n"),
			   (unsigned long long)block,
			   (unsigned long long)block);
		add_duplicate_ref(ip, block, ref_as_data, 0, INODE_VALID);
		bc->ea_count++;
		brelse(leaf_bh);
		return 1;
	}
	if (ip->i_di.di_eattr == 0) {
		/* Can only get in here if there were unrecoverable ea
		   errors that caused clear_eas to be called.  What we
		   need to do here is remove the subsequent ea blocks. */
		clear_eas(ip, bc, block, 0,
			  _("Extended Attribute block removed due to "
			    "previous errors.\n"));
		brelse(leaf_bh);
		return 1;
	}
	/* Point of confusion: We've got to set the ea block itself to
	   gfs2_meta_eattr here.  Elsewhere we mark the inode with
	   gfs2_eattr_block meaning it contains an eattr for pass1c. */
	fsck_blockmap_set(ip, block, _("Extended Attribute"), gfs2_meta_eattr);
	bc->ea_count++;
	*bh = leaf_bh;
	return 0;
}

/**
 * check_extended_leaf_eattr
 * @ip
 * @el_blk: block number of the extended leaf
 *
 * An EA leaf block can contain EA's with pointers to blocks
 * where the data for that EA is kept.  Those blocks still
 * have the gfs2 meta header of type GFS2_METATYPE_EA
 *
 * Returns: 0 if correct[able], -1 if removal is needed
 */
static int check_extended_leaf_eattr(struct gfs2_inode *ip, uint64_t *data_ptr,
				     struct gfs2_buffer_head *leaf_bh,
				     struct gfs2_ea_header *ea_hdr,
				     struct gfs2_ea_header *ea_hdr_prev,
				     void *private)
{
	uint64_t el_blk = be64_to_cpu(*data_ptr);
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct gfs2_buffer_head *bh = NULL;
	int error;

	if (!valid_block(sdp, el_blk)) {
		log_err( _("Inode #%llu (0x%llx): Extended Attribute block "
			   "%llu (0x%llx) has an extended leaf block #%llu "
			   "(0x%llx) that is invalid or out of range.\n"),
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)ip->i_di.di_eattr,
			 (unsigned long long)ip->i_di.di_eattr,
			 (unsigned long long)el_blk,
			 (unsigned long long)el_blk);
		fsck_blockmap_set(ip, ip->i_di.di_eattr,
				  _("bad (out of range) Extended Attribute "),
				  gfs2_bad_block);
		return 1;
	}
	error = check_ealeaf_block(ip, el_blk, GFS2_METATYPE_ED, &bh, private);
	if (bh)
		brelse(bh);
	return error;
}

static int check_eattr_leaf(struct gfs2_inode *ip, uint64_t block,
			    uint64_t parent, struct gfs2_buffer_head **bh,
			    void *private)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	osi_list_t *head;
	struct special_blocks *b = NULL;

	/* This inode contains an eattr - it may be invalid, but the
	 * eattr attributes points to a non-zero block.
	 * Clarification: If we're here we're checking a leaf block, and the
	 * source dinode needs to be marked as having extended attributes.
	 * That instructs pass1c to check the contents of the ea blocks. */
	log_debug( _("Setting inode %lld (0x%llx) as having eattr "
		     "block(s) attached.\n"),
		   (unsigned long long)ip->i_di.di_num.no_addr,
		   (unsigned long long)ip->i_di.di_num.no_addr);
	if (!osi_list_empty(&ip->i_sbd->eattr_blocks.list)) {
		head = &ip->i_sbd->eattr_blocks.list;
		b = osi_list_entry(head->next, struct special_blocks, list);
	}
	if (!b || b->block != ip->i_di.di_num.no_addr)
		gfs2_special_add(&sdp->eattr_blocks, ip->i_di.di_num.no_addr);
	if (!valid_block(sdp, block)) {
		log_warn( _("Inode #%llu (0x%llx): Extended Attribute leaf "
			    "block #%llu (0x%llx) is invalid or out of "
			    "range.\n"),
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)block, (unsigned long long)block);
		fsck_blockmap_set(ip, ip->i_di.di_eattr,
				  _("bad (out of range) Extended "
				    "Attribute leaf"), gfs2_bad_block);
		return 1;
	}
	return check_ealeaf_block(ip, block, GFS2_METATYPE_EA, bh, private);
}

static int check_eattr_entries(struct gfs2_inode *ip,
			       struct gfs2_buffer_head *leaf_bh,
			       struct gfs2_ea_header *ea_hdr,
			       struct gfs2_ea_header *ea_hdr_prev,
			       void *private)
{
	struct gfs2_sbd *sdp = ip->i_sbd;
	char ea_name[256];

	if (!ea_hdr->ea_name_len){
		/* Skip this entry for now */
		return 1;
	}

	memset(ea_name, 0, sizeof(ea_name));
	strncpy(ea_name, (char *)ea_hdr + sizeof(struct gfs2_ea_header),
		ea_hdr->ea_name_len);

	if (!GFS2_EATYPE_VALID(ea_hdr->ea_type) &&
	   ((ea_hdr_prev) || (!ea_hdr_prev && ea_hdr->ea_type))){
		/* Skip invalid entry */
		return 1;
	}

	if (ea_hdr->ea_num_ptrs){
		uint32_t avail_size;
		int max_ptrs;

		avail_size = sdp->sd_sb.sb_bsize - sizeof(struct gfs2_meta_header);
		max_ptrs = (be32_to_cpu(ea_hdr->ea_data_len)+avail_size-1)/avail_size;

		if (max_ptrs > ea_hdr->ea_num_ptrs) {
			return 1;
		} else {
			log_debug( _("  Pointers Required: %d\n  Pointers Reported: %d\n"),
				  max_ptrs, ea_hdr->ea_num_ptrs);
		}
	}
	return 0;
}

/**
 * mark_block_invalid - mark blocks associated with an inode as invalid
 *                      unless the block is a duplicate.
 *
 * An "invalid" block is now considered free in the bitmap, and pass2 will
 * delete any invalid blocks.  This is nearly identical to function
 * delete_block_if_notdup.
 */
static int mark_block_invalid(struct gfs2_inode *ip, uint64_t block,
			      enum dup_ref_type reftype, const char *btype)
{
	uint8_t q;

	/* If the block isn't valid, we obviously can't invalidate it.
	 * However, if we return an error, invalidating will stop, and
	 * we want it to continue to invalidate the valid blocks.  If we
	 * don't do this, block references that follow that are also
	 * referenced elsewhere (duplicates) won't be flagged as such,
	 * and as a result, they'll be freed when this dinode is deleted,
	 * despite being used by another dinode as a valid block. */
	if (!valid_block(ip->i_sbd, block))
		return 0;

	q = block_type(block);
	if (q != gfs2_block_free) {
		add_duplicate_ref(ip, block, reftype, 0, INODE_INVALID);
		log_info( _("%s block %lld (0x%llx), part of inode "
			    "%lld (0x%llx), was previously referenced so "
			    "the invalid reference is ignored.\n"),
			  btype, (unsigned long long)block,
			  (unsigned long long)block,
			  (unsigned long long)ip->i_di.di_num.no_addr,
			  (unsigned long long)ip->i_di.di_num.no_addr);
		return 0;
	}
	fsck_blockmap_set(ip, block, btype, gfs2_meta_inval);
	return 0;
}

static int invalidate_metadata(struct gfs2_inode *ip, uint64_t block,
			       struct gfs2_buffer_head **bh, int h,
			       void *private)
{
	return mark_block_invalid(ip, block, ref_as_meta, _("metadata"));
}

static int invalidate_leaf(struct gfs2_inode *ip, uint64_t block,
			   void *private)
{
	return mark_block_invalid(ip, block, ref_as_meta, _("leaf"));
}

static int invalidate_data(struct gfs2_inode *ip, uint64_t block,
			   void *private)
{
	return mark_block_invalid(ip, block, ref_as_data, _("data"));
}

static int invalidate_eattr_indir(struct gfs2_inode *ip, uint64_t block,
				  uint64_t parent,
				  struct gfs2_buffer_head **bh, void *private)
{
	return mark_block_invalid(ip, block, ref_as_ea,
				  _("indirect extended attribute"));
}

static int invalidate_eattr_leaf(struct gfs2_inode *ip, uint64_t block,
				 uint64_t parent, struct gfs2_buffer_head **bh,
				 void *private)
{
	return mark_block_invalid(ip, block, ref_as_ea,
				  _("extended attribute"));
}

/**
 * Check for massive amounts of pointer corruption.  If the block has
 * lots of out-of-range pointers, we can't trust any of the pointers.
 * For example, a stray pointer with a value of 0x1d might be
 * corruption/nonsense, and if so, we don't want to delete an
 * important file (like master or the root directory) because of it.
 * We need to check for a large number of bad pointers BEFORE we start
 * messing with them because we don't want to mark a block as a
 * duplicate (for example) until we know if the pointers in general can
 * be trusted. Thus it needs to be in a separate loop.
 * Returns: 0 if good range, otherwise != 0
 */
enum b_types { btype_meta, btype_leaf, btype_data, btype_ieattr, btype_eattr};
const char *btypes[5] = {
	"metadata", "leaf", "data", "indirect extended attribute",
	"extended attribute" };

static int rangecheck_block(struct gfs2_inode *ip, uint64_t block,
			    struct gfs2_buffer_head **bh, enum b_types btype,
			    void *private)
{
	long *bad_pointers = (long *)private;
	uint8_t q;

	if (!valid_block(ip->i_sbd, block)) {
		(*bad_pointers)++;
		log_info( _("Bad %s block pointer (invalid or out of range "
			    "#%ld) found in inode %lld (0x%llx).\n"),
			  btypes[btype], *bad_pointers,
			  (unsigned long long)ip->i_di.di_num.no_addr,
			  (unsigned long long)ip->i_di.di_num.no_addr);
		if ((*bad_pointers) <= BAD_POINTER_TOLERANCE)
			return ENOENT;
		else
			return -ENOENT; /* Exits check_metatree quicker */
	}
	/* See how many duplicate blocks it has */
	q = block_type(block);
	if (q != gfs2_block_free) {
		(*bad_pointers)++;
		log_info( _("Duplicated %s block pointer (violation %ld, block"
			    " %lld (0x%llx)) found in inode %lld (0x%llx).\n"),
			  btypes[btype], *bad_pointers,
			  (unsigned long long)block, (unsigned long long)block,
			  (unsigned long long)ip->i_di.di_num.no_addr,
			  (unsigned long long)ip->i_di.di_num.no_addr);
		if ((*bad_pointers) <= BAD_POINTER_TOLERANCE)
			return ENOENT;
		else
			return -ENOENT; /* Exits check_metatree quicker */
	}
	return 0;
}

static int rangecheck_metadata(struct gfs2_inode *ip, uint64_t block,
			       struct gfs2_buffer_head **bh, int h,
			       void *private)
{
	return rangecheck_block(ip, block, bh, btype_meta, private);
}

static int rangecheck_leaf(struct gfs2_inode *ip, uint64_t block,
			   void *private)
{
	return rangecheck_block(ip, block, NULL, btype_leaf, private);
}

static int rangecheck_data(struct gfs2_inode *ip, uint64_t block,
			   void *private)
{
	return rangecheck_block(ip, block, NULL, btype_data, private);
}

static int rangecheck_eattr_indir(struct gfs2_inode *ip, uint64_t block,
				  uint64_t parent,
				  struct gfs2_buffer_head **bh, void *private)
{
	return rangecheck_block(ip, block, NULL, btype_ieattr, private);
}

static int rangecheck_eattr_leaf(struct gfs2_inode *ip, uint64_t block,
				 uint64_t parent, struct gfs2_buffer_head **bh,
				 void *private)
{
	return rangecheck_block(ip, block, NULL, btype_eattr, private);
}

struct metawalk_fxns rangecheck_fxns = {
        .private = NULL,
        .check_metalist = rangecheck_metadata,
        .check_data = rangecheck_data,
        .check_leaf = rangecheck_leaf,
        .check_eattr_indir = rangecheck_eattr_indir,
        .check_eattr_leaf = rangecheck_eattr_leaf,
};

/*
 * handle_ip - process an incore structure representing a dinode.
 */
static int handle_ip(struct gfs2_sbd *sdp, struct gfs2_inode *ip)
{
	int error;
	struct block_count bc = {0};
	long bad_pointers;
	uint64_t block = ip->i_bh->b_blocknr;

	bad_pointers = 0L;

	/* First, check the metadata for massive amounts of pointer corruption.
	   Such corruption can only lead us to ruin trying to clean it up,
	   so it's better to check it up front and delete the inode if
	   there is corruption. */
	rangecheck_fxns.private = &bad_pointers;
	error = check_metatree(ip, &rangecheck_fxns);
	if (bad_pointers > BAD_POINTER_TOLERANCE) {
		log_err( _("Error: inode %llu (0x%llx) has more than "
			   "%d bad pointers.\n"),
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 BAD_POINTER_TOLERANCE);
		fsck_blockmap_set(ip, ip->i_di.di_num.no_addr,
				  _("badly corrupt"), gfs2_block_free);
		return 0;
	}

	error = set_ip_blockmap(ip, 1);
	if (error == -EINVAL) {
		/* We found a dinode that has an invalid mode, so we can't
		   tell if it's a data file, directory or a socket.
		   Regardless, we have to invalidate its metadata in case there
		   are duplicate blocks referenced.  If we don't call
		   check_metatree, the blocks it references will be deleted
		   wholesale by pass2, and if any of those blocks are
		   duplicates--referenced by another dinode for some reason--
		   we will mark it free, even though it's in use.  In other
		   words, we would introduce file system corruption. So we
		   need to keep track of the fact that it's invalid and
		   skip parts that we can't be sure of based on dinode type. */
		log_debug("Invalid mode dinode found at block %lld (0x%llx): "
			  "Invalidating all its metadata.\n",
			  (unsigned long long)ip->i_di.di_num.no_addr,
			  (unsigned long long)ip->i_di.di_num.no_addr);
		check_metatree(ip, &invalidate_fxns);
		check_inode_eattr(ip, &invalidate_fxns);
		return 0;
	} else if (error)
		goto bad_dinode;

	if (set_di_nlink(ip))
		goto bad_dinode;

	if (is_dir(&ip->i_di, sdp->gfs1) && (ip->i_di.di_flags & GFS2_DIF_EXHASH)) {
		if (((1 << ip->i_di.di_depth) * sizeof(uint64_t)) != ip->i_di.di_size){
			log_warn( _("Directory dinode block #%llu (0x%llx"
				 ") has bad depth.  Found %u, Expected %u\n"),
				 (unsigned long long)ip->i_di.di_num.no_addr,
				 (unsigned long long)ip->i_di.di_num.no_addr,
				 ip->i_di.di_depth,
				 (1 >> (ip->i_di.di_size/sizeof(uint64_t))));
			if (fsck_blockmap_set(ip, block, _("bad depth"),
					     gfs2_block_free))
				goto bad_dinode;
			return 0;
		}
	}

	pass1_fxns.private = &bc;
	error = check_metatree(ip, &pass1_fxns);
	if (fsck_abort || error < 0)
		return 0;
	if (error > 0) {
		log_err( _("Error: inode %llu (0x%llx) has unrecoverable "
			   "errors; invalidating.\n"),
			 (unsigned long long)ip->i_di.di_num.no_addr,
			 (unsigned long long)ip->i_di.di_num.no_addr);
		undo_fxns.private = &bc;
		check_metatree(ip, &undo_fxns);
		/* If we undo the metadata accounting, including metadatas
		   duplicate block status, we need to make sure later passes
		   don't try to free up the metadata referenced by this inode.
		   Therefore we mark the inode as free space. */
		fsck_blockmap_set(ip, ip->i_di.di_num.no_addr,
				  _("corrupt"), gfs2_block_free);
		return 0;
	}

	error = check_inode_eattr(ip, &pass1_fxns);

	if (error &&
	    !(ip->i_di.di_flags & GFS2_DIF_EA_INDIRECT))
		ask_remove_inode_eattr(ip, &bc);

	if (ip->i_di.di_blocks != 
		(1 + bc.indir_count + bc.data_count + bc.ea_count)) {
		log_err( _("Inode #%llu (0x%llx): Ondisk block count (%llu"
			") does not match what fsck found (%llu)\n"),
			(unsigned long long)ip->i_di.di_num.no_addr,
			(unsigned long long)ip->i_di.di_num.no_addr,
			(unsigned long long)ip->i_di.di_blocks,
			(unsigned long long)1 + bc.indir_count +
			bc.data_count + bc.ea_count);
		log_info( _("inode has: %lld, but fsck counts: Dinode:1 + "
			    "indir:%lld + data: %lld + ea: %lld\n"),
			  (unsigned long long)ip->i_di.di_blocks,
			  (unsigned long long)bc.indir_count,
			  (unsigned long long)bc.data_count,
			  (unsigned long long)bc.ea_count);
		if (query( _("Fix ondisk block count? (y/n) "))) {
			ip->i_di.di_blocks = 1 + bc.indir_count + bc.data_count +
				bc.ea_count;
			bmodified(ip->i_bh);
			log_err( _("Block count for #%llu (0x%llx) fixed\n"),
				(unsigned long long)ip->i_di.di_num.no_addr,
				(unsigned long long)ip->i_di.di_num.no_addr);
		} else
			log_err( _("Bad block count for #%llu (0x%llx"
				") not fixed\n"),
				(unsigned long long)ip->i_di.di_num.no_addr,
				(unsigned long long)ip->i_di.di_num.no_addr);
	}

	return 0;
bad_dinode:
	stack;
	return -1;
}

/*
 * handle_di - This is now a wrapper function that takes a gfs2_buffer_head
 *             and calls handle_ip, which takes an in-code dinode structure.
 */
static int handle_di(struct gfs2_sbd *sdp, struct gfs2_buffer_head *bh)
{
	uint8_t q;
	int error = 0;
	uint64_t block = bh->b_blocknr;
	struct gfs2_inode *ip;

	ip = fsck_inode_get(sdp, bh);
	q = block_type(block);
	if (q != gfs2_block_free) {
		log_err( _("Found a duplicate inode block at #%llu"
			   " (0x%llx) previously marked as a %s\n"),
			 (unsigned long long)block,
			 (unsigned long long)block, block_type_string(q));
		add_duplicate_ref(ip, block, ref_as_meta, 0, INODE_VALID);
		fsck_inode_put(&ip);
		return 0;
	}

	if (ip->i_di.di_num.no_addr != block) {
		log_err( _("Inode #%llu (0x%llx): Bad inode address found: %llu "
			"(0x%llx)\n"), (unsigned long long)block,
			(unsigned long long)block,
			(unsigned long long)ip->i_di.di_num.no_addr,
			(unsigned long long)ip->i_di.di_num.no_addr);
		if (query( _("Fix address in inode at block #%llu"
			    " (0x%llx)? (y/n) "),
			  (unsigned long long)block, (unsigned long long)block)) {
			ip->i_di.di_num.no_addr = ip->i_di.di_num.no_formal_ino = block;
			bmodified(ip->i_bh);
		} else
			log_err( _("Address in inode at block #%llu"
				 " (0x%llx) not fixed\n"),
				(unsigned long long)block,
				(unsigned long long)block);
	}
	error = handle_ip(sdp, ip);
	fsck_inode_put(&ip);
	return error;
}

/* Check system inode and verify it's marked "in use" in the bitmap:       */
/* Should work for all system inodes: root, master, jindex, per_node, etc. */
/* We have to pass the sysinode as ** because the pointer may change out from
   under the reference by way of the builder() function.  */
static int check_system_inode(struct gfs2_sbd *sdp,
			      struct gfs2_inode **sysinode,
			      const char *filename,
			      int builder(struct gfs2_sbd *sdp),
			      enum gfs2_mark_block mark)
{
	uint64_t iblock = 0;
	struct dir_status ds = {0};
	int error, err = 0;

	log_info( _("Checking system inode '%s'\n"), filename);
	if (*sysinode) {
		/* Read in the system inode, look at its dentries, and start
		 * reading through them */
		iblock = (*sysinode)->i_di.di_num.no_addr;
		log_info( _("System inode for '%s' is located at block %llu"
			 " (0x%llx)\n"), filename,
			 (unsigned long long)iblock,
			 (unsigned long long)iblock);
		if (gfs2_check_meta((*sysinode)->i_bh, GFS2_METATYPE_DI)) {
			log_err( _("Found invalid system dinode at block #"
				   "%llu (0x%llx)\n"),
				 (unsigned long long)iblock,
				 (unsigned long long)iblock);
			gfs2_blockmap_set(bl, iblock, gfs2_block_free);
			check_n_fix_bitmap(sdp, iblock, gfs2_block_free);
			inode_put(sysinode);
		}
	}
	if (*sysinode) {
		ds.q = block_type(iblock);
		/* If the inode exists but the block is marked free, we might
		   be recovering from a corrupt bitmap.  In that case, don't
		   rebuild the inode.  Just reuse the inode and fix the
		   bitmap. */
		if (ds.q == gfs2_block_free) {
			log_info( _("The inode exists but the block is not "
				    "marked 'in use'; fixing it.\n"));
			fsck_blockmap_set(*sysinode,
					  (*sysinode)->i_di.di_num.no_addr,
					  filename, mark);
			ds.q = mark;
			if (mark == gfs2_inode_dir)
				dirtree_insert((*sysinode)->i_di.di_num.no_addr);
		}
	} else
		log_info( _("System inode for '%s' is corrupt or missing.\n"),
			  filename);
	/* If there are errors with the inode here, we need to create a new
	   inode and get it all setup - of course, everything will be in
	   lost+found then, but we *need* our system inodes before we can
	   do any of that. */
	if (!(*sysinode) || ds.q != mark) {
		log_err( _("Invalid or missing %s system inode (should be %d, "
			   "is %d).\n"), filename, mark, ds.q);
		if (query(_("Create new %s system inode? (y/n) "), filename)) {
			log_err( _("Rebuilding system file \"%s\"\n"),
				 filename);
			error = builder(sdp);
			if (error) {
				log_err( _("Error trying to rebuild system "
					   "file %s: Cannot continue\n"),
					 filename);
				return error;
			}
			fsck_blockmap_set(*sysinode,
					  (*sysinode)->i_di.di_num.no_addr,
					  filename, mark);
			ds.q = mark;
			if (mark == gfs2_inode_dir)
				dirtree_insert((*sysinode)->i_di.di_num.no_addr);
		} else {
			log_err( _("Cannot continue without valid %s inode\n"),
				filename);
			return -1;
		}
	}
	if (is_dir(&(*sysinode)->i_di, sdp->gfs1)) {
		struct block_count bc = {0};

		sysdir_fxns.private = &bc;
		if ((*sysinode)->i_di.di_flags & GFS2_DIF_EXHASH)
			check_metatree(*sysinode, &sysdir_fxns);
		else {
			err = check_linear_dir(*sysinode, (*sysinode)->i_bh,
					       &sysdir_fxns);
			/* If we encountered an error in our directory check
			   we should still call handle_ip, but return the
			   error later. */
			if (err)
				log_err(_("Error found in %s while checking "
					  "directory entries.\n"), filename);
		}
	}
	error = handle_ip(sdp, *sysinode);
	return error ? error : err;
}

static int build_a_journal(struct gfs2_sbd *sdp)
{
	char name[256];
	int err = 0;

	/* First, try to delete the journal if it's in jindex */
	sprintf(name, "journal%u", sdp->md.journals);
	gfs2_dirent_del(sdp->md.jiinode, name, strlen(name));
	/* Now rebuild it */
	err = build_journal(sdp, sdp->md.journals, sdp->md.jiinode);
	if (err) {
		log_crit(_("Error %d building journal\n"), err);
		exit(FSCK_ERROR);
	}
	return 0;
}

static int check_system_inodes(struct gfs2_sbd *sdp)
{
	int journal_count;

	/*******************************************************************
	 *******  Check the system inode integrity             *************
	 *******************************************************************/
	/* Mark the master system dinode as a "dinode" in the block map.
	   All other system dinodes in master will be taken care of by function
	   resuscitate_metalist.  But master won't since it has no parent.*/
	if (!sdp->gfs1) {
		fsck_blockmap_set(sdp->master_dir,
				  sdp->master_dir->i_di.di_num.no_addr,
				  "master", gfs2_inode_dir);
		if (check_system_inode(sdp, &sdp->master_dir, "master",
				       build_master, gfs2_inode_dir)) {
			stack;
			return -1;
		}
	}
	/* Mark the root dinode as a "dinode" in the block map as we did
	   for master, since it has no parent. */
	fsck_blockmap_set(sdp->md.rooti, sdp->md.rooti->i_di.di_num.no_addr,
			  "root", gfs2_inode_dir);
	if (check_system_inode(sdp, &sdp->md.rooti, "root", build_root,
			       gfs2_inode_dir)) {
		stack;
		return -1;
	}
	if (!sdp->gfs1 &&
	    check_system_inode(sdp, &sdp->md.inum, "inum", build_inum,
			       gfs2_inode_file)) {
		stack;
		return -1;
	}
	if (check_system_inode(sdp, &sdp->md.statfs, "statfs", build_statfs,
			       gfs2_inode_file)) {
		stack;
		return -1;
	}
	if (check_system_inode(sdp, &sdp->md.jiinode, "jindex", build_jindex,
			       (sdp->gfs1 ? gfs2_inode_file : gfs2_inode_dir))) {
		stack;
		return -1;
	}
	if (check_system_inode(sdp, &sdp->md.riinode, "rindex", build_rindex,
			       gfs2_inode_file)) {
		stack;
		return -1;
	}
	if (check_system_inode(sdp, &sdp->md.qinode, "quota", build_quota,
			       gfs2_inode_file)) {
		stack;
		return -1;
	}
	if (!sdp->gfs1 &&
	    check_system_inode(sdp, &sdp->md.pinode, "per_node",
			       build_per_node, gfs2_inode_dir)) {
		stack;
		return -1;
	}
	/* We have to play a trick on build_journal:  We swap md.journals
	   in order to keep a count of which journal we need to build. */
	journal_count = sdp->md.journals;
	/* gfs1's journals aren't dinode, they're just a bunch of blocks. */
	if (sdp->gfs1) {
		/* gfs1 has four dinodes that are set in the superblock and
		   therefore not linked to anything else. We need to adjust
		   the link counts so pass4 doesn't get confused. */
		incr_link_count(sdp->md.statfs->i_di.di_num.no_addr, 0,
				_("gfs1 statfs inode"));
		incr_link_count(sdp->md.jiinode->i_di.di_num.no_addr, 0,
				_("gfs1 jindex inode"));
		incr_link_count(sdp->md.riinode->i_di.di_num.no_addr, 0,
				_("gfs1 rindex inode"));
		incr_link_count(sdp->md.qinode->i_di.di_num.no_addr, 0,
				_("gfs1 quota inode"));
		return 0;
	}
	for (sdp->md.journals = 0; sdp->md.journals < journal_count;
	     sdp->md.journals++) {
		char jname[16];

		sprintf(jname, "journal%d", sdp->md.journals);
		if (check_system_inode(sdp, &sdp->md.journal[sdp->md.journals],
				       jname, build_a_journal,
				       gfs2_inode_file)) {
			stack;
			return -1;
		}
	}

	return 0;
}

/**
 * pass1 - walk through inodes and check inode state
 *
 * this walk can be done using root inode and depth first search,
 * watching for repeat inode numbers
 *
 * format & type
 * link count
 * duplicate blocks
 * bad blocks
 * inodes size
 * dir info
 */
int pass1(struct gfs2_sbd *sdp)
{
	struct osi_node *n, *next = NULL;
	struct gfs2_buffer_head *bh;
	uint64_t block = 0;
	struct rgrp_tree *rgd;
	int first;
	uint64_t i;
	uint64_t rg_count = 0;

	osi_list_init(&gfs1_rindex_blks.list);

	/* FIXME: In the gfs fsck, we had to mark things like the
	 * journals and indices and such as 'other_meta' - in gfs2,
	 * the journals are files and are found in the normal file
	 * sweep - is there any metadata we need to mark here before
	 * the sweeps start that we won't find otherwise? */

	/* Make sure the system inodes are okay & represented in the bitmap. */
	check_system_inodes(sdp);

	/* So, do we do a depth first search starting at the root
	 * inode, or use the rg bitmaps, or just read every fs block
	 * to find the inodes?  If we use the depth first search, why
	 * have pass3 at all - if we use the rg bitmaps, pass5 is at
	 * least partially invalidated - if we read every fs block,
	 * things will probably be intolerably slow.  The current fsck
	 * uses the rg bitmaps, so maybe that's the best way to start
	 * things - we can change the method later if necessary.
	 */
	for (n = osi_first(&sdp->rgtree); n; n = next, rg_count++) {
		next = osi_next(n);
		log_debug( _("Checking metadata in Resource Group #%llu\n"),
				 (unsigned long long)rg_count);
		rgd = (struct rgrp_tree *)n;
		for (i = 0; i < rgd->ri.ri_length; i++) {
			log_debug( _("rgrp block %lld (0x%llx) "
				     "is now marked as 'rgrp data'\n"),
				   rgd->ri.ri_addr + i, rgd->ri.ri_addr + i);
			if (gfs2_blockmap_set(bl, rgd->ri.ri_addr + i,
					      gfs2_indir_blk)) {
				stack;
				gfs2_special_free(&gfs1_rindex_blks);
				return FSCK_ERROR;
			}
			/* rgrps and bitmaps don't have bits to represent
			   their blocks, so don't do this:
			check_n_fix_bitmap(sdp, rgd->ri.ri_addr + i,
			gfs2_meta_rgrp);*/
		}

		first = 1;

		while (1) {
			/* "block" is relative to the entire file system */
			/* Get the next dinode in the file system, according
			   to the bitmap.  This should ONLY be dinodes unless
			   it's GFS1, in which case it can be any metadata. */
			if (gfs2_next_rg_meta(rgd, &block, first))
				break;
			/* skip gfs1 rindex indirect blocks */
			if (sdp->gfs1 && blockfind(&gfs1_rindex_blks, block)) {
				log_debug(_("Skipping rindex indir block "
					    "%lld (0x%llx)\n"),
					  (unsigned long long)block,
					  (unsigned long long)block);
				first = 0;
				continue;
			}
			warm_fuzzy_stuff(block);

			if (fsck_abort) { /* if asked to abort */
				gfs2_special_free(&gfs1_rindex_blks);
				return FSCK_OK;
			}
			if (skip_this_pass) {
				printf( _("Skipping pass 1 is not a good idea.\n"));
				skip_this_pass = FALSE;
				fflush(stdout);
			}
			if (fsck_system_inode(sdp, block)) {
				log_debug(_("Already processed system inode "
					    "%lld (0x%llx)\n"),
					  (unsigned long long)block,
					  (unsigned long long)block);
				first = 0;
				continue;
			}
			bh = bread(sdp, block);

			/*log_debug( _("Checking metadata block #%" PRIu64
			  " (0x%" PRIx64 ")\n"), block, block);*/

			if (gfs2_check_meta(bh, GFS2_METATYPE_DI)) {
				/* In gfs2, a bitmap mark of 2 means an inode,
				   but in gfs1 it means any metadata.  So if
				   this is gfs1 and not an inode, it may be
				   okay.  If it's non-dinode metadata, it will
				   be referenced by an inode, so we need to
				   skip it here and it will be sorted out
				   when the referencing inode is checked. */
				if (sdp->gfs1) {
					uint32_t check_magic;

					check_magic = ((struct
							gfs2_meta_header *)
						       (bh->b_data))->mh_magic;
					if (be32_to_cpu(check_magic) ==
					    GFS2_MAGIC) {
						log_debug( _("Deferring GFS1 "
							     "metadata block #"
							     "%" PRIu64" (0x%"
							     PRIx64 ")\n"),
							   block, block);
						brelse(bh);
						first = 0;
						continue;
					}
				}
				log_err( _("Found invalid inode at block #"
					   "%llu (0x%llx)\n"),
					 (unsigned long long)block,
					 (unsigned long long)block);
				if (gfs2_blockmap_set(bl, block,
						      gfs2_block_free)) {
					stack;
					brelse(bh);
					gfs2_special_free(&gfs1_rindex_blks);
					return FSCK_ERROR;
				}
				check_n_fix_bitmap(sdp, block,
						   gfs2_block_free);
			} else if (handle_di(sdp, bh) < 0) {
				stack;
				brelse(bh);
				gfs2_special_free(&gfs1_rindex_blks);
				return FSCK_ERROR;
			}
			/* Ignore everything else - they should be hit by the
			   handle_di step.  Don't check NONE either, because
			   check_meta passes everything if GFS2_METATYPE_NONE
			   is specified.  Hopefully, other metadata types such
			   as indirect blocks will be handled when the inode
			   itself is processed, and if it's not, it should be
			   caught in pass5. */
			brelse(bh);
			first = 0;
		}
		/*
		  For GFS1, we have to count the "free meta" blocks in the
		  resource group and mark them specially so we can count them
		  properly in pass5.
		 */
		if (!sdp->gfs1)
			continue;
		first = 1;
		while (gfs2_next_rg_freemeta(rgd, &block, first) == 0) {
			gfs2_blockmap_set(bl, block, gfs2_freemeta);
			first = 0;
		}
	}
	gfs2_special_free(&gfs1_rindex_blks);
	return FSCK_OK;
}
