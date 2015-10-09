#include "clusterautoconfig.h"

#include <inttypes.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "libgfs2.h"

#if BITS_PER_LONG == 32
#define LBITMASK   (0x55555555UL)
#define LBITSKIP55 (0x55555555UL)
#define LBITSKIP00 (0x00000000UL)
#else
#define LBITMASK   (0x5555555555555555UL)
#define LBITSKIP55 (0x5555555555555555UL)
#define LBITSKIP00 (0x0000000000000000UL)
#endif

#define ALIGN(x,a) (((x)+(a)-1)&~((a)-1))

/**
 * gfs2_bit_search
 * @ptr: Pointer to bitmap data
 * @mask: Mask to use (normally 0x55555.... but adjusted for search start)
 * @state: The state we are searching for
 *
 * We xor the bitmap data with a patter which is the bitwise opposite
 * of what we are looking for, this gives rise to a pattern of ones
 * wherever there is a match. Since we have two bits per entry, we
 * take this pattern, shift it down by one place and then and it with
 * the original. All the even bit positions (0,2,4, etc) then represent
 * successful matches, so we mask with 0x55555..... to remove the unwanted
 * odd bit positions.
 *
 * This allows searching of a whole u64 at once (32 blocks) with a
 * single test (on 64 bit arches).
 */

static inline uint64_t gfs2_bit_search(const unsigned long long *ptr,
				       unsigned long long mask,
				       uint8_t state)
{
	unsigned long long tmp;
	static const unsigned long long search[] = {
		[0] = 0xffffffffffffffffULL,
		[1] = 0xaaaaaaaaaaaaaaaaULL,
		[2] = 0x5555555555555555ULL,
		[3] = 0x0000000000000000ULL,
	};
	tmp = le64_to_cpu(*ptr) ^ search[state];
	tmp &= (tmp >> 1);
	tmp &= mask;
	return tmp;
}

/**
 * gfs2_bitfit - Find a free block in the bitmaps
 * @buffer: the buffer that holds the bitmaps
 * @buflen: the length (in bytes) of the buffer
 * @goal: the block to try to allocate
 * @old_state: the state of the block we're looking for
 *
 * Return: the block number that was allocated
 */
unsigned long gfs2_bitfit(const unsigned char *buf, const unsigned int len,
			  unsigned long goal, unsigned char state)
{
	unsigned long spoint = (goal << 1) & ((8 * sizeof(unsigned long long)) - 1);
	const unsigned long long *ptr = ((unsigned long long *)buf) + (goal >> 5);
	const unsigned long long *end = (unsigned long long *)
		(buf + ALIGN(len, sizeof(unsigned long long)));
	unsigned long long tmp;
	unsigned long long mask = 0x5555555555555555ULL;
	unsigned long bit;

	if (state > 3)
		return 0;

	/* Mask off bits we don't care about at the start of the search */
	mask <<= spoint;
	tmp = gfs2_bit_search(ptr, mask, state);
	ptr++;
	while(tmp == 0 && ptr < end) {
		tmp = gfs2_bit_search(ptr, 0x5555555555555555ULL, state);
		ptr++;
	}
	/* Mask off any bits which are more than len bytes from the start */
	if (ptr == end && (len & (sizeof(unsigned long long) - 1)))
		tmp &= (((unsigned long long)~0) >>
			(64 - 8 * (len & (sizeof(unsigned long long) - 1))));
	/* Didn't find anything, so return */
	if (tmp == 0)
		return BFITNOENT;
	ptr--;
	bit = ffsll(tmp);
	bit /= 2;	/* two bits per entry in the bitmap */
	return (((const unsigned char *)ptr - buf) * GFS2_NBBY) + bit;
}

/**
 * fs_bitcount - count the number of bits in a certain state
 * @buffer: the buffer that holds the bitmaps
 * @buflen: the length (in bytes) of the buffer
 * @state: the state of the block we're looking for
 *
 * Returns: The number of bits
 */
uint32_t gfs2_bitcount(unsigned char *buffer, unsigned int buflen,
		       unsigned char state)
{
	unsigned char *byte, *end;
	unsigned int bit;
	uint32_t count = 0;

	byte = buffer;
	bit = 0;
	end = buffer + buflen;

	while (byte < end){
		if (((*byte >> bit) & GFS2_BIT_MASK) == state)
			count++;

		bit += GFS2_BIT_SIZE;
		if (bit >= 8){
			bit = 0;
			byte++;
		}
	}
	return count;
}

/*
 * check_range - check if blkno is within FS limits
 * @sdp: super block
 * @blkno: block number
 *
 * Returns: 0 if ok, -1 if out of bounds
 */
int gfs2_check_range(struct gfs2_sbd *sdp, uint64_t blkno)
{
	if((blkno > sdp->fssize) || (blkno <= sdp->sb_addr))
		return -1;
	return 0;
}

/*
 * valid_block - check if blkno is valid and not part of our rgrps or bitmaps
 * @sdp: super block
 * @blkno: block number
 *
 * Returns: 1 if ok, 0 if out of bounds
 */
int valid_block(struct gfs2_sbd *sdp, uint64_t blkno)
{
	if((blkno > sdp->fssize) || (blkno <= sdp->sb_addr))
		return 0;
	/* Check if the block is one of our rgrp or bitmap blocks */
	if (gfs2_get_bitmap(sdp, blkno, NULL) < 0)
		return 0;
	return 1;
}

/*
 * gfs2_set_bitmap
 * @sdp: super block
 * @blkno: block number relative to file system
 * @state: one of three possible states
 *
 * This function sets the value of a bit of the
 * file system bitmap.
 *
 * Returns: 0 on success, -1 on error
 */
int gfs2_set_bitmap(struct gfs2_sbd *sdp, uint64_t blkno, int state)
{
	int           buf;
	uint32_t        rgrp_block;
	struct gfs2_bitmap *bits = NULL;
	struct rgrp_tree *rgd;
	unsigned char *byte, cur_state;
	unsigned int bit;

	/* FIXME: should GFS2_BLKST_INVALID be allowed */
	if ((state < GFS2_BLKST_FREE) || (state > GFS2_BLKST_DINODE))
		return -1;

	rgd = gfs2_blk2rgrpd(sdp, blkno);

	if(!rgd || blkno < rgd->ri.ri_data0)
		return -1;

	rgrp_block = (uint32_t)(blkno - rgd->ri.ri_data0);
	for(buf= 0; buf < rgd->ri.ri_length; buf++){
		bits = &(rgd->bits[buf]);
		if(rgrp_block < ((bits->bi_start + bits->bi_len)*GFS2_NBBY))
			break;
	}

	byte = (unsigned char *)(rgd->bh[buf]->b_data + bits->bi_offset) +
		(rgrp_block/GFS2_NBBY - bits->bi_start);
	bit = (rgrp_block % GFS2_NBBY) * GFS2_BIT_SIZE;

	cur_state = (*byte >> bit) & GFS2_BIT_MASK;
	*byte ^= cur_state << bit;
	*byte |= state << bit;

	bmodified(rgd->bh[buf]);
	return 0;
}

/*
 * gfs2_get_bitmap - get value of FS bitmap
 * @sdp: super block
 * @blkno: block number relative to file system
 *
 * This function gets the value of a bit of the
 * file system bitmap.
 * Possible state values for a block in the bitmap are:
 *  GFS_BLKST_FREE     (0)
 *  GFS_BLKST_USED     (1)
 *  GFS_BLKST_INVALID  (2)
 *  GFS_BLKST_DINODE   (3)
 *
 * Returns: state on success, -1 on error
 */
int gfs2_get_bitmap(struct gfs2_sbd *sdp, uint64_t blkno,
		    struct rgrp_tree *rgd)
{
	int           i, val;
	uint32_t        rgrp_block;
	struct gfs2_bitmap	*bits = NULL;
	unsigned int  bit;
	unsigned char *byte;

	if (rgd == NULL) {
		rgd = gfs2_blk2rgrpd(sdp, blkno);
		if(rgd == NULL)
			return -1;
	}

	rgrp_block = (uint32_t)(blkno - rgd->ri.ri_data0);

	for (i = 0; i < rgd->ri.ri_length; i++) {
		bits = &(rgd->bits[i]);
		if(rgrp_block < ((bits->bi_start + bits->bi_len)*GFS2_NBBY))
			break;
	}

	if (i >= rgd->ri.ri_length)
		return -1;
	if (!rgd->bh || !rgd->bh[i])
		return 0;
	byte = (unsigned char *)(rgd->bh[i]->b_data + bits->bi_offset) +
		(rgrp_block/GFS2_NBBY - bits->bi_start);
	bit = (rgrp_block % GFS2_NBBY) * GFS2_BIT_SIZE;

	val = ((*byte >> bit) & GFS2_BIT_MASK);

	return val;
}
