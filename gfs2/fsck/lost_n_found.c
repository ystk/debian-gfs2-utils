#include "clusterautoconfig.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <dirent.h>
#include <libintl.h>
#define _(String) gettext(String)

#include "fsck.h"
#include "libgfs2.h"
#include "lost_n_found.h"
#include "link.h"
#include "metawalk.h"
#include "util.h"

static void add_dotdot(struct gfs2_inode *ip)
{
	struct dir_info *di;
	struct gfs2_sbd *sdp = ip->i_sbd;
	int err;

	log_info( _("Adding .. entry to directory %llu (0x%llx) pointing back "
		    "to lost+found\n"),
		  (unsigned long long)ip->i_di.di_num.no_addr,
		  (unsigned long long)ip->i_di.di_num.no_addr);

	/* If there's a pre-existing .. directory entry, we have to
	   back out the links. */
	di = dirtree_find(ip->i_di.di_num.no_addr);
	if (di && valid_block(sdp, di->dotdot_parent)) {
		struct gfs2_inode *dip;

		log_debug(_("Directory %lld (0x%llx) already had a "
			    "\"..\" link to %lld (0x%llx).\n"),
			  (unsigned long long)ip->i_di.di_num.no_addr,
			  (unsigned long long)ip->i_di.di_num.no_addr,
			  (unsigned long long)di->dotdot_parent,
			  (unsigned long long)di->dotdot_parent);
		decr_link_count(di->dotdot_parent, ip->i_di.di_num.no_addr,
				_(".. unlinked, moving to lost+found"));
		dip = fsck_load_inode(sdp, di->dotdot_parent);
		if (dip->i_di.di_nlink > 0) {
			dip->i_di.di_nlink--;
			set_di_nlink(dip); /* keep inode tree in sync */
			log_debug(_("Decrementing its links to %d\n"),
				  dip->i_di.di_nlink);
			bmodified(dip->i_bh);
		} else if (!dip->i_di.di_nlink) {
			log_debug(_("Its link count is zero.\n"));
		} else {
			log_debug(_("Its link count is %d!  Changing "
				    "it to 0.\n"), dip->i_di.di_nlink);
			dip->i_di.di_nlink = 0;
			set_di_nlink(dip); /* keep inode tree in sync */
			bmodified(dip->i_bh);
		}
		fsck_inode_put(&dip);
		di = NULL;
	} else {
		if (di)
			log_debug(_("Couldn't find a valid \"..\" entry "
				    "for orphan directory %lld (0x%llx): "
				    "'..' = 0x%llx\n"),
				  (unsigned long long)ip->i_di.di_num.no_addr,
				  (unsigned long long)ip->i_di.di_num.no_addr,
				  (unsigned long long)di->dotdot_parent);
		else
			log_debug(_("Couldn't find directory %lld (0x%llx) "
				    "in directory tree.\n"),
				  (unsigned long long)ip->i_di.di_num.no_addr,
				  (unsigned long long)ip->i_di.di_num.no_addr);
	}
	if (gfs2_dirent_del(ip, "..", 2))
		log_warn( _("add_inode_to_lf:  Unable to remove "
			    "\"..\" directory entry.\n"));

	err = dir_add(ip, "..", 2, &(lf_dip->i_di.di_num),
		      (sdp->gfs1 ? GFS_FILE_DIR : DT_DIR));
	if (err) {
		log_crit(_("Error adding .. directory: %s\n"),
			 strerror(errno));
		exit(FSCK_ERROR);
	}
}

static uint64_t find_free_blk(struct gfs2_sbd *sdp)
{
	struct osi_node *n, *next = NULL;
	struct rgrp_tree *rl = NULL;
	struct gfs2_rindex *ri;
	struct gfs2_rgrp *rg;
	unsigned int block, bn = 0, x = 0, y = 0;
	unsigned int state;
	struct gfs2_buffer_head *bh;

	memset(&rg, 0, sizeof(rg));
	for (n = osi_first(&sdp->rgtree); n; n = next) {
		next = osi_next(n);
		rl = (struct rgrp_tree *)n;
		if (rl->rg.rg_free)
			break;
	}

	if (n == NULL)
		return 0;

	ri = &rl->ri;
	rg = &rl->rg;

	for (block = 0; block < ri->ri_length; block++) {
		bh = rl->bh[block];
		x = (block) ? sizeof(struct gfs2_meta_header) : sizeof(struct gfs2_rgrp);

		for (; x < sdp->bsize; x++)
			for (y = 0; y < GFS2_NBBY; y++) {
				state = (bh->b_data[x] >> (GFS2_BIT_SIZE * y)) & 0x03;
				if (state == GFS2_BLKST_FREE)
					return ri->ri_data0 + bn;
				bn++;
			}
	}
	return 0;
}

/* add_inode_to_lf - Add dir entry to lost+found for the inode
 * @ip: inode to add to lost + found
 *
 * This function adds an entry into the lost and found dir
 * for the given inode.  The name of the entry will be
 * "lost_<ip->i_num.no_addr>".
 *
 * Returns: 0 on success, -1 on failure.
 */
int add_inode_to_lf(struct gfs2_inode *ip){
	char tmp_name[256];
	__be32 inode_type;
	uint64_t lf_blocks;
	struct gfs2_sbd *sdp = ip->i_sbd;
	struct dir_info *di;
	int err = 0;
	uint32_t mode;

	if (!lf_dip) {
		uint8_t q;

		log_info( _("Locating/Creating lost+found directory\n"));

		/* if this is gfs1, we have to trick createi into using
		   no_formal_ino = no_addr, so we set next_inum to the
		   free block we're about to allocate. */
		if (sdp->gfs1)
			sdp->md.next_inum = find_free_blk(sdp);
		mode = (sdp->gfs1 ? DT2IF(GFS_FILE_DIR) : S_IFDIR) | 0700;
		if (sdp->gfs1)
			lf_dip = gfs_createi(sdp->md.rooti, "lost+found",
					     mode, 0);
		else
			lf_dip = createi(sdp->md.rooti, "lost+found",
					 S_IFDIR | 0700, 0);
		if (lf_dip == NULL) {
			log_crit(_("Error creating lost+found: %s\n"),
			         strerror(errno));
			exit(FSCK_ERROR);
		}

		/* createi will have incremented the di_nlink link count for
		   the root directory.  We must set the nlink value
		   in the hash table to keep them in sync so that pass4 can
		   detect and fix any descrepancies. */
		set_di_nlink(sdp->md.rooti);

		q = block_type(lf_dip->i_di.di_num.no_addr);
		if (q != gfs2_inode_dir) {
			/* This is a new lost+found directory, so set its
			 * block type and increment link counts for
			 * the directories */
			/* FIXME: i'd feel better about this if
			 * fs_mkdir returned whether it created a new
			 * directory or just found an old one, and we
			 * used that instead of the block_type to run
			 * this */
			fsck_blockmap_set(ip, lf_dip->i_di.di_num.no_addr,
					  _("lost+found dinode"),
					  gfs2_inode_dir);
			/* root inode links to lost+found */
			incr_link_count(sdp->md.rooti->i_di.di_num.no_addr,
				       lf_dip->i_di.di_num.no_addr, _("root"));
			/* lost+found link for '.' from itself */
			incr_link_count(lf_dip->i_di.di_num.no_addr,
					lf_dip->i_di.di_num.no_addr, "\".\"");
			/* lost+found link for '..' back to root */
			incr_link_count(lf_dip->i_di.di_num.no_addr,
					sdp->md.rooti->i_di.di_num.no_addr,
				       "\"..\"");
			if (sdp->gfs1)
				lf_dip->i_di.__pad1 = GFS_FILE_DIR;
		}
		log_info( _("lost+found directory is dinode %lld (0x%llx)\n"),
			  (unsigned long long)lf_dip->i_di.di_num.no_addr,
			  (unsigned long long)lf_dip->i_di.di_num.no_addr);
		di = dirtree_find(lf_dip->i_di.di_num.no_addr);
		if (di) {
			log_info( _("Marking lost+found inode connected\n"));
			di->checked = 1;
			di = NULL;
		}
	}
	if (ip->i_di.di_num.no_addr == lf_dip->i_di.di_num.no_addr) {
		log_err( _("Trying to add lost+found to itself...skipping"));
		return 0;
	}
	lf_blocks = lf_dip->i_di.di_blocks;

	if (sdp->gfs1)
		mode = gfs_to_gfs2_mode(ip);
	else
		mode = ip->i_di.di_mode & S_IFMT;

	switch (mode) {
	case S_IFDIR:
		add_dotdot(ip);
		sprintf(tmp_name, "lost_dir_%llu",
			(unsigned long long)ip->i_di.di_num.no_addr);
		inode_type = (sdp->gfs1 ? GFS_FILE_DIR : DT_DIR);
		break;
	case S_IFREG:
		sprintf(tmp_name, "lost_file_%llu",
			(unsigned long long)ip->i_di.di_num.no_addr);
		inode_type = (sdp->gfs1 ? GFS_FILE_REG : DT_REG);
		break;
	case S_IFLNK:
		sprintf(tmp_name, "lost_link_%llu",
			(unsigned long long)ip->i_di.di_num.no_addr);
		inode_type = (sdp->gfs1 ? GFS_FILE_LNK : DT_LNK);
		break;
	case S_IFBLK:
		sprintf(tmp_name, "lost_blkdev_%llu",
			(unsigned long long)ip->i_di.di_num.no_addr);
		inode_type = (sdp->gfs1 ? GFS_FILE_BLK : DT_BLK);
		break;
	case S_IFCHR:
		sprintf(tmp_name, "lost_chrdev_%llu",
			(unsigned long long)ip->i_di.di_num.no_addr);
		inode_type = (sdp->gfs1 ? GFS_FILE_CHR : DT_CHR);
		break;
	case S_IFIFO:
		sprintf(tmp_name, "lost_fifo_%llu",
			(unsigned long long)ip->i_di.di_num.no_addr);
		inode_type = (sdp->gfs1 ? GFS_FILE_FIFO : DT_FIFO);
		break;
	case S_IFSOCK:
		sprintf(tmp_name, "lost_socket_%llu",
			(unsigned long long)ip->i_di.di_num.no_addr);
		inode_type = (sdp->gfs1 ? GFS_FILE_SOCK : DT_SOCK);
		break;
	default:
		sprintf(tmp_name, "lost_%llu",
			(unsigned long long)ip->i_di.di_num.no_addr);
		inode_type = (sdp->gfs1 ? GFS_FILE_REG : DT_REG);
		break;
	}

	err = dir_add(lf_dip, tmp_name, strlen(tmp_name), &(ip->i_di.di_num),
		inode_type);
	if (err) {
		log_crit(_("Error adding directory %s: %s\n"),
			 tmp_name, strerror(errno));
		exit(FSCK_ERROR);
	}
	/* If the lf directory had new blocks added we have to mark them
	   properly in the bitmap so they're not freed. */
	if (lf_dip->i_di.di_blocks != lf_blocks)
		reprocess_inode(lf_dip, "lost+found");

	/* This inode is linked from lost+found */
	incr_link_count(ip->i_di.di_num.no_addr, lf_dip->i_di.di_num.no_addr,
			_("from lost+found"));
	/* If it's a directory, lost+found is back-linked to it via .. */
	if (mode == S_IFDIR)
		incr_link_count(lf_dip->i_di.di_num.no_addr,
				ip->i_di.di_num.no_addr, _("to lost+found"));

	log_notice( _("Added inode #%llu (0x%llx) to lost+found\n"),
		    (unsigned long long)ip->i_di.di_num.no_addr,
		    (unsigned long long)ip->i_di.di_num.no_addr);
	gfs2_dinode_out(&lf_dip->i_di, lf_dip->i_bh);
	bwrite(lf_dip->i_bh);
	return 0;
}
