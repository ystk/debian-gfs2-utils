#include "clusterautoconfig.h"

#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <string.h>
#include <dirent.h>
#include <libintl.h>
#define _(String) gettext(String)

#include "libgfs2.h"
#include "osi_list.h"
#include "fsck.h"
#include "lost_n_found.h"
#include "link.h"
#include "metawalk.h"
#include "util.h"

static int attach_dotdot_to(struct gfs2_sbd *sdp, uint64_t newdotdot,
			    uint64_t olddotdot, uint64_t block)
{
	char *filename;
	int filename_len, err;
	struct gfs2_inode *ip, *pip;
	uint64_t cur_blks;

	ip = fsck_load_inode(sdp, block);
	pip = fsck_load_inode(sdp, newdotdot);
	/* FIXME: Need to add some interactive
	 * options here and come up with a
	 * good default for non-interactive */
	/* FIXME: do i need to correct the
	 * '..' entry for this directory in
	 * this case? */

	filename_len = strlen("..");
	if (!(filename = malloc((sizeof(char) * filename_len) + 1))) {
		log_err( _("Unable to allocate name\n"));
		fsck_inode_put(&ip);
		fsck_inode_put(&pip);
		stack;
		return -1;
	}
	if (!memset(filename, 0, (sizeof(char) * filename_len) + 1)) {
		log_err( _("Unable to zero name\n"));
		fsck_inode_put(&ip);
		fsck_inode_put(&pip);
		stack;
		return -1;
	}
	memcpy(filename, "..", filename_len);
	if (gfs2_dirent_del(ip, filename, filename_len))
		log_warn( _("Unable to remove \"..\" directory entry.\n"));
	else
		decr_link_count(olddotdot, block, _("old \"..\""));
	cur_blks = ip->i_di.di_blocks;
	err = dir_add(ip, filename, filename_len, &pip->i_di.di_num,
		      (sdp->gfs1 ? GFS_FILE_DIR : DT_DIR));
	if (err) {
		log_err(_("Error adding directory %s: %s\n"),
		        filename, strerror(err));
		exit(FSCK_ERROR);
	}
	if (cur_blks != ip->i_di.di_blocks) {
		char dirname[80];

		sprintf(dirname, _("Directory at %lld (0x%llx)"),
			(unsigned long long)ip->i_di.di_num.no_addr,
			(unsigned long long)ip->i_di.di_num.no_addr);
		reprocess_inode(ip, dirname);
	}
	incr_link_count(newdotdot, block, _("new \"..\""));
	fsck_inode_put(&ip);
	fsck_inode_put(&pip);
	free(filename);
	return 0;
}

static struct dir_info *mark_and_return_parent(struct gfs2_sbd *sdp,
					       struct dir_info *di)
{
	struct dir_info *pdi;
	uint8_t q_dotdot, q_treewalk;
	int error = 0;

	di->checked = 1;

	if (!di->treewalk_parent)
		return NULL;

	if (di->dotdot_parent == di->treewalk_parent) {
		q_dotdot = block_type(di->dotdot_parent);
		if (q_dotdot != gfs2_inode_dir) {
			log_err( _("Orphaned directory at block %llu (0x%llx) "
				   "moved to lost+found\n"),
				 (unsigned long long)di->dinode,
				 (unsigned long long)di->dinode);
			return NULL;
		}
		goto out;
	}

	log_warn( _("Directory '..' and treewalk connections disagree for "
		    "inode %llu (0x%llx)\n"), (unsigned long long)di->dinode,
		  (unsigned long long)di->dinode);
	log_notice( _("'..' has %llu (0x%llx), treewalk has %llu (0x%llx)\n"),
		    (unsigned long long)di->dotdot_parent,
		    (unsigned long long)di->dotdot_parent,
		    (unsigned long long)di->treewalk_parent,
		    (unsigned long long)di->treewalk_parent);
	q_dotdot = block_type(di->dotdot_parent);
	q_treewalk = block_type(di->treewalk_parent);
	/* if the dotdot entry isn't a directory, but the
	 * treewalk is, treewalk is correct - if the treewalk
	 * entry isn't a directory, but the dotdot is, dotdot
	 * is correct - if both are directories, which do we
	 * choose? if neither are directories, we have a
	 * problem - need to move this directory into lost+found
	 */
	if (q_dotdot != gfs2_inode_dir) {
		if (q_treewalk != gfs2_inode_dir) {
			log_err( _("Orphaned directory, move to "
				   "lost+found\n"));
			return NULL;
		} else {
			log_warn( _("Treewalk parent is correct, fixing "
				    "dotdot -> %llu (0x%llx)\n"),
				  (unsigned long long)di->treewalk_parent,
				  (unsigned long long)di->treewalk_parent);
			attach_dotdot_to(sdp, di->treewalk_parent,
					 di->dotdot_parent, di->dinode);
			di->dotdot_parent = di->treewalk_parent;
		}
		goto out;
	}
	if (q_treewalk == gfs2_inode_dir) {
		log_err( _("Both .. and treewalk parents are directories, "
			   "going with treewalk...\n"));
		attach_dotdot_to(sdp, di->treewalk_parent,
				 di->dotdot_parent, di->dinode);
		di->dotdot_parent = di->treewalk_parent;
		goto out;
	}
	log_warn( _(".. parent is valid, but treewalk is bad - reattaching to "
		    "lost+found"));

	/* FIXME: add a dinode for this entry instead? */

	if (!query( _("Remove directory entry for bad inode %llu (0x%llx) in "
		      "%llu (0x%llx)? (y/n)"),
		    (unsigned long long)di->dinode,
		    (unsigned long long)di->dinode,
		    (unsigned long long)di->treewalk_parent,
		    (unsigned long long)di->treewalk_parent)) {
		log_err( _("Directory entry to invalid inode remains\n"));
		return NULL;
	}
	error = remove_dentry_from_dir(sdp, di->treewalk_parent, di->dinode);
	if (error < 0) {
		stack;
		return NULL;
	}
	if (error > 0)
		log_warn( _("Unable to find dentry for block %llu"
			    " (0x%llx) in %llu (0x%llx)\n"),
			  (unsigned long long)di->dinode,
			  (unsigned long long)di->dinode,
			  (unsigned long long)di->treewalk_parent,
			  (unsigned long long)di->treewalk_parent);
	log_warn( _("Directory entry removed\n"));
	log_info( _("Marking directory unlinked\n"));

	return NULL;

out:
	pdi = dirtree_find(di->dotdot_parent);

	return pdi;
}

/**
 * pass3 - check connectivity of directories
 *
 * handle disconnected directories
 * handle lost+found directory errors (missing, not a directory, no space)
 */
int pass3(struct gfs2_sbd *sdp)
{
	struct osi_node *tmp, *next = NULL;
	struct dir_info *di, *tdi;
	struct gfs2_inode *ip;
	uint8_t q;

	di = dirtree_find(sdp->md.rooti->i_di.di_num.no_addr);
	if (di) {
		log_info( _("Marking root inode connected\n"));
		di->checked = 1;
	}
	if (sdp->gfs1) {
		di = dirtree_find(sdp->md.statfs->i_di.di_num.no_addr);
		if (di) {
			log_info( _("Marking GFS1 statfs file inode "
				    "connected\n"));
			di->checked = 1;
		}
		di = dirtree_find(sdp->md.jiinode->i_di.di_num.no_addr);
		if (di) {
			log_info( _("Marking GFS1 jindex file inode "
				    "connected\n"));
			di->checked = 1;
		}
		di = dirtree_find(sdp->md.riinode->i_di.di_num.no_addr);
		if (di) {
			log_info( _("Marking GFS1 rindex file inode "
				    "connected\n"));
			di->checked = 1;
		}
		di = dirtree_find(sdp->md.qinode->i_di.di_num.no_addr);
		if (di) {
			log_info( _("Marking GFS1 quota file inode "
				    "connected\n"));
			di->checked = 1;
		}
	} else {
		di = dirtree_find(sdp->master_dir->i_di.di_num.no_addr);
		if (di) {
			log_info( _("Marking master directory inode "
				    "connected\n"));
			di->checked = 1;
		}
	}

	/* Go through the directory list, working up through the parents
	 * until we find one that's been checked already.  If we don't
	 * find a parent, put in lost+found.
	 */
	log_info( _("Checking directory linkage.\n"));
	for (tmp = osi_first(&dirtree); tmp; tmp = next) {
		next = osi_next(tmp);
		di = (struct dir_info *)tmp;
		while (!di->checked) {
			/* FIXME: Change this so it returns success or
			 * failure and put the parent inode in a
			 * param */
			if (skip_this_pass || fsck_abort) /* if asked to skip the rest */
				return FSCK_OK;
			tdi = mark_and_return_parent(sdp, di);

			if (tdi) {
				log_debug( _("Directory at block %llu (0x%llx) connected\n"),
					   (unsigned long long)di->dinode,
					   (unsigned long long)di->dinode);
				di = tdi;
				continue;
			}
			q = block_type(di->dinode);
			if (q == gfs2_bad_block) {
				log_err( _("Found unlinked directory "
					   "containing bad block\n"));
				if (query(_("Clear unlinked directory "
					   "with bad blocks? (y/n) "))) {
					log_warn( _("inode %lld (0x%llx) is "
						    "now marked as free\n"),
						  (unsigned long long)
						  di->dinode,
						  (unsigned long long)
						  di->dinode);
					/* Can't use fsck_blockmap_set
					   because we don't have ip */
					gfs2_blockmap_set(bl, di->dinode,
							  gfs2_block_free);
					check_n_fix_bitmap(sdp, di->dinode,
							   gfs2_block_free);
					break;
				} else
					log_err( _("Unlinked directory with bad block remains\n"));
			}
			if (q != gfs2_inode_dir && q != gfs2_inode_file &&
			   q != gfs2_inode_lnk && q != gfs2_inode_device &&
			   q != gfs2_inode_fifo && q != gfs2_inode_sock) {
				log_err( _("Unlinked block marked as an inode "
					   "is not an inode\n"));
				if (!query(_("Clear the unlinked block?"
					    " (y/n) "))) {
					log_err( _("The block was not "
						   "cleared\n"));
					break;
				}
				log_warn( _("inode %lld (0x%llx) is now "
					    "marked as free\n"),
					  (unsigned long long)di->dinode,
					  (unsigned long long)di->dinode);
				/* Can't use fsck_blockmap_set
				   because we don't have ip */
				gfs2_blockmap_set(bl, di->dinode,
						  gfs2_block_free);
				check_n_fix_bitmap(sdp, di->dinode,
						   gfs2_block_free);
				log_err( _("The block was cleared\n"));
				break;
			}

			log_err( _("Found unlinked directory at block %llu"
				   " (0x%llx)\n"), (unsigned long long)di->dinode,
				 (unsigned long long)di->dinode);
			ip = fsck_load_inode(sdp, di->dinode);
			/* Don't skip zero size directories with eattrs */
			if (!ip->i_di.di_size && !ip->i_di.di_eattr){
				log_err( _("Unlinked directory has zero "
					   "size.\n"));
				if (query( _("Remove zero-size unlinked "
					    "directory? (y/n) "))) {
					fsck_blockmap_set(ip, di->dinode,
						_("zero-sized unlinked inode"),
							  gfs2_block_free);
					fsck_inode_put(&ip);
					break;
				} else {
					log_err( _("Zero-size unlinked "
						   "directory remains\n"));
				}
			}
			if (query( _("Add unlinked directory to "
				    "lost+found? (y/n) "))) {
				if (add_inode_to_lf(ip)) {
					fsck_inode_put(&ip);
					stack;
					return FSCK_ERROR;
				}
				log_warn( _("Directory relinked to lost+found\n"));
			} else {
				log_err( _("Unlinked directory remains unlinked\n"));
			}
			fsck_inode_put(&ip);
			break;
		}
	}
	if (lf_dip)
		log_debug( _("At end of pass3, lost+found entries is %u\n"),
				  lf_dip->i_di.di_entries);
	return FSCK_OK;
}
