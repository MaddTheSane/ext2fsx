/*
 *  modified for Lites 1.1
 *
 *  Aug 1995, Godmar Back (gback@cs.utah.edu)
 *  University of Utah, Department of Computer Science
 */
/*
 * Copyright (c) 1982, 1986, 1989, 1993
 *	The Regents of the University of California.  All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 4. Neither the name of the University nor the names of its contributors
 *    may be used to endorse or promote products derived from this software
 *    without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 *	@(#)ffs_alloc.c	8.8 (Berkeley) 2/21/94
 * $FreeBSD: src/sys/gnu/ext2fs/ext2_alloc.c,v 1.37 2002/05/18 21:33:07 iedowse Exp $
 */
 
static const char whatid[] __attribute__ ((unused)) =
"@(#) $Id: ext2_alloc.c,v 1.14 2006/07/01 20:23:45 bbergstrand Exp $";

#include <sys/param.h>
#include <sys/systm.h>
#include <sys/conf.h>
#include <sys/vnode.h>
#include <sys/stat.h>
#include <sys/mount.h>
#include <sys/syslog.h>

#ifdef APPLE
#include "ext2_apple.h"
#endif

#include <gnu/ext2fs/inode.h>
#include <gnu/ext2fs/ext2_mount.h>
#include <gnu/ext2fs/ext2_fs.h>
#include <gnu/ext2fs/ext2_fs_sb.h>
#include <gnu/ext2fs/fs.h>
#include <gnu/ext2fs/ext2_extern.h>
#include <ext2_byteorder.h>

static void	ext2_fserr(struct ext2_sb_info *, u_int, char *);

/*
 * Linux calls this functions at the following locations:
 * (1) the inode is freed
 * (2) a preallocation miss occurs
 * (3) truncate is called
 * (4) release_file is called and f_mode & 2
 *
 * I call it in ext2_inactive, ext2_truncate, ext2_vfree and in (2)
 * the call in vfree might be redundant
 */
void
ext2_discard_prealloc(struct inode * ip)
{
#ifdef EXT2_PREALLOCATE
        if (ip->i_prealloc_count) {
                unsigned long i = ip->i_prealloc_count;
                unsigned long blk = ip->i_prealloc_block;
                ip->i_prealloc_count = 0;
                
                mount_t mp = ITOVFS(ip);
                
                IULOCK(ip);
                ext2_free_blocks (mp, blk, i);
                IXLOCK(ip);
        }
#endif
}

/*
 * Allocate a block in the file system.
 * 
 * this takes the framework from ffs_alloc. To implement the
 * actual allocation, it calls ext2_new_block, the ported version
 * of the same Linux routine.
 *
 * we note that this is always called in connection with ext2_blkpref
 *
 * preallocation is done as Linux does it
 */
int
ext2_alloc(struct inode *ip, ext2_daddr_t lbn, ext2_daddr_t bpref, int size, struct ucred *cred, ext2_daddr_t *bnp)
{
	struct ext2_sb_info *fs;
	ext2_daddr_t bno;
    mount_t mp = ITOVFS(ip);
	
	*bnp = 0;
	fs = ip->i_e2fs;
#if DIAGNOSTIC
	if ((u_int)size > fs->s_blocksize || blkoff(fs, size) != 0) {
		printf("dev = %s, bsize = %lu, size = %d, fs = %s\n",
		    devtoname(ip->i_dev), fs->s_blocksize, size, fs->fs_fsmnt);
		panic("ext2_alloc: bad size");
	}
	if (cred == NOCRED)
		panic("ext2_alloc: missing credential");
#endif /* DIAGNOSTIC */
	if (size == fs->s_blocksize && fs->s_es->s_free_blocks_count == 0)
		goto nospace;
	if (cred->cr_posix.cr_uid != 0 && 
		le32_to_cpu(fs->s_es->s_free_blocks_count) < le32_to_cpu(fs->s_es->s_r_blocks_count))
		goto nospace;
	if (bpref >= le32_to_cpu(fs->s_es->s_blocks_count))
		bpref = 0;
	/* call the Linux code */
#ifdef EXT2_PREALLOCATE
	/* To have a preallocation hit, we must
	 * - have at least one block preallocated
	 * - and our preferred block must have that block number or one below
	 */
        if (ip->i_prealloc_count &&
            (bpref == ip->i_prealloc_block ||
             bpref + 1 == ip->i_prealloc_block))
        {
                bno = ip->i_prealloc_block++;
                ip->i_prealloc_count--;
                /* ext2_debug ("preallocation hit (%lu/%lu).\n",
                            ++alloc_hits, ++alloc_attempts); */

		/* Linux gets, clears, and releases the buffer at this
		   point - we don't have to do that; we leave it to the caller
		 */
        } else {
                ext2_discard_prealloc (ip);
                /* ext2_debug ("preallocation miss (%lu/%lu).\n",
                            alloc_hits, ++alloc_attempts); */
                if (S_ISREG(ip->i_mode)) {
                        IULOCK(ip);
                        typeof(ip->i_prealloc_count) icount;
                        typeof(ip->i_prealloc_block) ibn;
                        bno = ext2_new_block (mp, bpref, &icount, &ibn);
                        IXLOCK(ip);
                        ip->i_prealloc_count = icount;
                        ip->i_prealloc_block = ibn;
                } else {
                    IULOCK(ip);
                    bno = (ext2_daddr_t)ext2_new_block(mp, bpref, 0, 0);
                    IXLOCK(ip);
                }
        }
#else
	IULOCK(ip);
    bno = (int32_t)ext2_new_block(mp, bpref, 0, 0);
    IXLOCK(ip);
#endif

	if (bno > 0) {
		/* set next_alloc fields as done in block_getblk */
		ip->i_next_alloc_block = lbn;
		ip->i_next_alloc_goal = bno;
      
      ip->i_blocks += btodb(size, ip->i_e2fs->s_d_blocksize);
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		*bnp = bno;
		return (0);
	}
nospace:
	ext2_fserr(fs, cred->cr_posix.cr_uid, "file system full");
	uprintf("\n%s: write failed, file system is full\n", fs->fs_fsmnt);
	return (ENOSPC);
}

/*
 * Reallocate a sequence of blocks into a contiguous sequence of blocks.
 *
 * The vnode and an array of buffer pointers for a range of sequential
 * logical blocks to be made contiguous is given. The allocator attempts
 * to find a range of sequential blocks starting as close as possible to
 * an fs_rotdelay offset from the end of the allocation for the logical
 * block immediately preceding the current range. If successful, the
 * physical block numbers in the buffer pointers and in the inode are
 * changed to reflect the new allocation. If unsuccessful, the allocation
 * is left unchanged. The success in doing the reallocation is returned.
 * Note that the error return is not reflected back to the user. Rather
 * the previous block allocation will be used.
 */

#ifdef __APPLE__
/* Note: This routine is unused in UBC cluster I/O */
#ifdef FANCY_REALLOC
#undef FANCY_REALLOC
#endif
#endif

#ifdef FANCY_REALLOC
#include <sys/sysctl.h>
static int doasyncfree = 1;
#ifdef	OPT_DEBUG
SYSCTL_INT(_debug, 14, doasyncfree, CTLFLAG_RW, &doasyncfree, 0, "");
#endif	/* OPT_DEBUG */
#endif

int
ext2_reallocblks(struct vop_reallocblks_args /* {
											  vnode_t a_vp;
				   struct cluster_save *a_buflist;
			   } */ *ap)
{
#ifndef FANCY_REALLOC
/* printf("ext2_reallocblks not implemented\n"); */
return ENOSPC;
#else

	struct ext2_sb_info *fs;
	struct inode *ip;
	vnode_t vp;
	buf_t  sbp, ebp;
	int32_t *bap, *sbap, *ebap;
	struct cluster_save *buflist;
	int32_t start_lbn, end_lbn, soff, eoff, newblk, blkno;
	struct indir start_ap[NIADDR + 1], end_ap[NIADDR + 1], *idp;
	int i, len, start_lvl, end_lvl, pref, ssize;

	vp = ap->a_vp;
	ip = VTOI(vp);
	fs = ip->i_e2fs;
#ifdef UNKLAR
	if (fs->fs_contigsumsize <= 0)
		return (ENOSPC);
#endif
	buflist = ap->a_buflist;
	len = buflist->bs_nchildren;
	start_lbn = buflist->bs_children[0]->b_lblkno;
	end_lbn = start_lbn + len - 1;
#if DIAGNOSTIC
	for (i = 1; i < len; i++)
		if (buflist->bs_children[i]->b_lblkno != start_lbn + i)
			panic("ext2_reallocblks: non-cluster");
#endif
	/*
	 * If the latest allocation is in a new cylinder group, assume that
	 * the filesystem has decided to move and do not force it back to
	 * the previous cylinder group.
	 */
	if (dtog(fs, dbtofsb(fs, buflist->bs_children[0]->b_blkno)) !=
	    dtog(fs, dbtofsb(fs, buflist->bs_children[len - 1]->b_blkno)))
		return (ENOSPC);
	if (ufs_getlbns(vp, start_lbn, start_ap, &start_lvl) ||
	    ufs_getlbns(vp, end_lbn, end_ap, &end_lvl))
		return (ENOSPC);
	/*
	 * Get the starting offset and block map for the first block.
	 */
	if (start_lvl == 0) {
		sbap = &ip->i_db[0];
		soff = start_lbn;
	} else {
		idp = &start_ap[start_lvl - 1];
		if (meta_bread(vp, idp->in_lbn, (int)fs->s_blocksize, NOCRED, &sbp)) {
			brelse(sbp);
			return (ENOSPC);
		}
		sbap = (int32_t *)sbp->b_data;
		soff = idp->in_off;
	}
	/*
	 * Find the preferred location for the cluster.
	 */
	pref = ext2_blkpref(ip, start_lbn, soff, sbap);
	/*
	 * If the block range spans two block maps, get the second map.
	 */
	if (end_lvl == 0 || (idp = &end_ap[end_lvl - 1])->in_off + 1 >= len) {
		ssize = len;
	} else {
#if DIAGNOSTIC
		if (start_ap[start_lvl-1].in_lbn == idp->in_lbn)
			panic("ext2_reallocblk: start == end");
#endif
		ssize = len - (idp->in_off + 1);
		if (meta_bread(vp, idp->in_lbn, (int)fs->s_blocksize, NOCRED, &ebp))
			goto fail;
		ebap = (int32_t *)ebp->b_data;
	}
	/*
	 * Search the block map looking for an allocation of the desired size.
	 */
	if ((newblk = (int32_t)ext2_hashalloc(ip, dtog(fs, pref), (long)pref,
	    len, (u_long (*)())ext2_clusteralloc)) == 0)
		goto fail;
	/*
	 * We have found a new contiguous block.
	 *
	 * First we have to replace the old block pointers with the new
	 * block pointers in the inode and indirect blocks associated
	 * with the file.
	 */
	blkno = newblk;
	for (bap = &sbap[soff], i = 0; i < len; i++, blkno += fs->s_frags_per_block) {
		if (i == ssize)
			bap = ebap;
#if DIAGNOSTIC
		if (buflist->bs_children[i]->b_blkno != fsbtodb(fs, cpu_to_le32(*bap)))
			panic("ext2_reallocblks: alloc mismatch");
#endif
		*bap++ = cpu_to_le32(blkno);
	}
	/*
	 * Next we must write out the modified inode and indirect blocks.
	 * For strict correctness, the writes should be synchronous since
	 * the old block values may have been written to disk. In practise
	 * they are almost never written, but if we are concerned about 
	 * strict correctness, the `doasyncfree' flag should be set to zero.
	 *
	 * The test on `doasyncfree' should be changed to test a flag
	 * that shows whether the associated buffers and inodes have
	 * been written. The flag should be set when the cluster is
	 * started and cleared whenever the buffer or inode is flushed.
	 * We can then check below to see if it is set, and do the
	 * synchronous write only when it has been cleared.
	 */
	if (sbap != &ip->i_db[0]) {
		if (doasyncfree)
			bdwrite(sbp);
		else
			bwrite(sbp);
	} else {
		ip->i_flag |= IN_CHANGE | IN_UPDATE;
		if (!doasyncfree)
			ext2_update(vp, 1);
	}
	if (ssize < len)
		if (doasyncfree)
			bdwrite(ebp);
		else
			bwrite(ebp);
	/*
	 * Last, free the old blocks and assign the new blocks to the buffers.
	 */
	for (blkno = newblk, i = 0; i < len; i++, blkno += fs->s_frags_per_block) {
		ext2_blkfree(ip, dbtofsb(fs, buflist->bs_children[i]->b_blkno),
		    fs->s_blocksize);
		buflist->bs_children[i]->b_blkno = fsbtodb(fs, blkno);
	}
	return (0);

fail:
	if (ssize < len)
		brelse(ebp);
	if (sbap != &ip->i_db[0])
		brelse(sbp);
	return (ENOSPC);

#endif /* FANCY_REALLOC */
}

/*
 * Allocate an inode in the file system.
 * 
 * we leave the actual allocation strategy to the (modified)
 * ext2_new_inode(), to make sure we get the policies right
 */
int
ext2_valloc(vnode_t pvp, int mode, evalloc_args_t *vaargsp, vnode_t *vpp)
{
	struct inode *pip;
	struct ext2_sb_info *fs;
	struct inode *ip;
	ino_t ino;
	int i, error;
	
    assert (NULL != vaargsp->va_vctx);
	
    *vpp = NULL;
	pip = VTOI(pvp);
	fs = pip->i_e2fs;
	if (fs->s_es->s_free_inodes_count == 0)
		goto noinodes;

	/* call the Linux routine - it returns the inode number only */
	ino = ext2_new_inode(pip, mode);

	if (ino == 0)
		goto noinodes;
   
   vaargsp->va_ino = ino;
   vaargsp->va_flags |= EVALLOC_CREATE;
   vaargsp->va_createmode = mode;
   error = EXT2_VGET(vnode_mount(pvp), vaargsp, vpp, vaargsp->va_vctx);
   vaargsp->va_flags &= ~EVALLOC_CREATE;
   
	if (error) {
		ext2_vfree(pvp, ino, mode);
		return (error);
	}
	ip = VTOI(*vpp);

	/* 
	  the question is whether using VGET was such good idea at all -
	  Linux doesn't read the old inode in when it's allocating a
	  new one. I will set at least i_size & i_blocks the zero. 
	*/ 
	IXLOCK(ip);
    ip->i_mode = 0;
	ip->i_size = 0;
	ip->i_blocks = 0;
	ip->i_flags = 0;
    /* now we want to make sure that the block pointers are zeroed out */
    for (i = 0; i < NDADDR; i++)
        ip->i_db[i] = 0;
    for (i = 0; i < NIADDR; i++)
        ip->i_ib[i] = 0;

	/*
	 * Set up a new generation number for this inode.
	 * XXX check if this makes sense in ext2
	 */
	if (ip->i_gen == 0 || ++ip->i_gen == 0)
		ip->i_gen = random() / 2 + 1;
    IULOCK(ip);
/*
printf("ext2_valloc: allocated inode %d\n", ino);
*/
	return (0);
noinodes:
	ext2_fserr(fs, (vfs_context_ucred(vaargsp->va_vctx))->cr_posix.cr_uid, "out of inodes");
	uprintf("\n%s: create/symlink failed, no inodes free\n", fs->fs_fsmnt);
	return (ENOSPC);
}

/*
 * Select the desired position for the next block in a file.  
 *
 * we try to mimic what Remy does in inode_getblk/block_getblk
 *
 * we note: blocknr == 0 means that we're about to allocate either
 * a direct block or a pointer block at the first level of indirection
 * (In other words, stuff that will go in i_db[] or i_ib[])
 *
 * blocknr != 0 means that we're allocating a block that is none
 * of the above. Then, blocknr tells us the number of the block
 * that will hold the pointer
 */
ext2_daddr_t
ext2_blkpref(struct inode *ip, ext2_daddr_t lbn, int indx, ext2_daddr_t *bap, ext2_daddr_t blocknr)
{
	int	tmp;

	/* if the next block is actually what we thought it is,
	   then set the goal to what we thought it should be
	*/
	if(ip->i_next_alloc_block && ip->i_next_alloc_block == lbn)
		return ip->i_next_alloc_goal;

	/* now check whether we were provided with an array that basically
	   tells us previous blocks to which we want to stay closeby
	*/
	if(bap) {
      for (tmp = indx - 1; tmp >= 0; tmp--) {
         if (bap[tmp]) 
      #if BYTE_ORDER == BIG_ENDIAN
         /* --BDB--
          * Block addrs in the inode are stored in cpu order, but
          * indirect addrs are in LE order. So here, we have to
          * find out which we are dealing with.
          *
          * It may be better to keep all addrs in LE order
          * (like Linux), but I wanted to avoid byte swaps
          * (which cause load/stores) as much as possible.
          */
         {
            if (bap == &ip->i_db[0] || bap == &ip->i_ib[0])
               return (bap[tmp]);
            else
               return le32_to_cpu(bap[tmp]);
         }
      #else
				return bap[tmp];
      #endif
      }
   }
   
	/* else let's fall back to the blocknr, or, if there is none,
	   follow the rule that a block should be allocated near its inode
	*/
	return blocknr ? blocknr :
			(int32_t)(ip->i_block_group * 
			EXT2_BLOCKS_PER_GROUP(ip->i_e2fs)) + 
			le32_to_cpu(ip->i_e2fs->s_es->s_first_data_block);
}

/*
 * Free a block or fragment.
 *
 * pass it on to the Linux code
 */
void
ext2_blkfree(struct inode *ip, ext2_daddr_t bno, long size)
{
	struct ext2_sb_info *fs;

	fs = ip->i_e2fs;
	/*
	 *	call Linux code with mount *, block number, count
	 */
	ext2_free_blocks(ITOVFS(ip), bno, size / fs->s_frag_size);
}

/*
 * Free an inode.
 *
 * the maintenance of the actual bitmaps is again up to the linux code
 */
int
ext2_vfree(vnode_t pvp, ino_t ino, int mode)
{
	struct ext2_sb_info *fs;
	struct inode *pip;
	mode_t save_i_mode;

	pip = VTOI(pvp);
	fs = pip->i_e2fs;
	if ((u_int)ino > fs->s_inodes_per_group * fs->s_groups_count)
		panic("ext2_vfree: range: dev = (%d, %d), ino = %d, fs = %s",
		    major(pip->i_dev), minor(pip->i_dev), ino, fs->fs_fsmnt);

/* ext2_debug("ext2_vfree (%d, %d) called\n", pip->i_number, mode);
 */
	IXLOCK(pip);
    ext2_discard_prealloc(pip);

	/* we need to make sure that ext2_free_inode can adjust the
	   used_dir_counts in the group summary information - I'd
	   really like to know what the rationale behind this
	   'set i_mode to zero to denote an unused inode' is
	 */
	save_i_mode = pip->i_mode;
	pip->i_mode = mode;
	ext2_free_inode(pip);	
	pip->i_mode = save_i_mode;
    IULOCK(pip);
	return (0);
}

/*
 * Fserr prints the name of a file system with an error diagnostic.
 * 
 * The form of the error message is:
 *	fs: error message
 */
static void
ext2_fserr(struct ext2_sb_info *fs, u_int uid, char *cp)
{

	printf("uid %d on %s: %s\n", uid, fs->fs_fsmnt, cp);
}
