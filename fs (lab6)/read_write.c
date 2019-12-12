#include "testfs.h"
#include "list.h"
#include "super.h"
#include "block.h"
#include "inode.h"
#include <string.h>

/* given logical block number, read the corresponding physical block into block.
 * return physical block number.
 * returns 0 if physical block does not exist.
 * returns negative value on other errors. */
static int
testfs_read_block(struct inode *in, int log_block_nr, char *block)
{
	int phy_block_nr = 0;

	assert(log_block_nr >= 0);

	assert(NR_DIRECT_BLOCKS + NR_INDIRECT_BLOCKS + NR_INDIRECT_BLOCKS*NR_INDIRECT_BLOCKS == 4196362);
	if (log_block_nr >= 4196362)
		return -EFBIG;

	if (log_block_nr < NR_DIRECT_BLOCKS) {
		phy_block_nr = (int)in->in.i_block_nr[log_block_nr];
	} else {
		log_block_nr -= NR_DIRECT_BLOCKS;

		if (log_block_nr >= NR_INDIRECT_BLOCKS){
			log_block_nr -= NR_INDIRECT_BLOCKS;
			assert(log_block_nr < NR_INDIRECT_BLOCKS*NR_INDIRECT_BLOCKS);

			if (in -> in.i_dindirect > 0){
				//Simply extract indirect ptr from dindirect
				read_blocks(in -> sb, block, in -> in.i_dindirect, 1);
				int index = log_block_nr / NR_INDIRECT_BLOCKS;
				long temp = ((int *)block)[index];
				if (temp > 0){
					read_blocks(in -> sb, block, temp, 1);
					phy_block_nr = ((int *)block)[log_block_nr % NR_INDIRECT_BLOCKS];
				} else {
					return temp;
				}
			}
		} else if (in->in.i_indirect > 0) {
			read_blocks(in->sb, block, in->in.i_indirect, 1);
			phy_block_nr = ((int *)block)[log_block_nr];
		}
	}

	//below is given code
	if (phy_block_nr > 0) {
		read_blocks(in->sb, block, phy_block_nr, 1);
	} else {
		/* we support sparse files by zeroing out a block that is not
		 * allocated on disk. */
		bzero(block, BLOCK_SIZE);
	}
	return phy_block_nr;
}

int
testfs_read_data(struct inode *in, char *buf, off_t start, size_t size)
{
	char block[BLOCK_SIZE];
	long block_nr = start / BLOCK_SIZE; // logical block number in the file
	long block_ix = start % BLOCK_SIZE; //  index or offset in the block
	int ret;

	//assert((NR_DIRECT_BLOCKS + NR_INDIRECT_BLOCKS + NR_INDIRECT_BLOCKS*NR_INDIRECT_BLOCKS) * 8192 == 34376597504);
	if (size > 34376597504)
		return -EFBIG;

	assert(buf);
	if (start + (off_t) size > in->in.i_size) {
		size = in->in.i_size - start;
	}
	if (block_ix + size > BLOCK_SIZE) {
		int temp = size;
		temp -= BLOCK_SIZE - block_ix;
		//Investigate: why doesn't strcat work?
		//char* temp = "";
		if ((ret = testfs_read_block(in, block_nr, block)) < 0)
			return ret;
		memcpy(buf, block + block_ix, BLOCK_SIZE - block_ix);
		//memcpy(temp, block + block_ix, BLOCK_SIZE - block_ix);
		//strcat(buf, temp);
		int i = 1;
		while (temp > BLOCK_SIZE){
			if ((ret = testfs_read_block(in, block_nr + i, block)) < 0)
				return ret;
			memcpy(buf + (BLOCK_SIZE*i) - block_ix, block, BLOCK_SIZE);
			//memcpy(temp, block, BLOCK_SIZE);
			//strcat(buf, temp);
			i++;
			temp -= BLOCK_SIZE;
		}
		if ((ret = testfs_read_block(in, block_nr + i, block)) < 0)
			return ret;
		memcpy(buf + (BLOCK_SIZE*i) - block_ix, block, temp);
		//assert(temp == size - BLOCK_SIZE*i + block_ix);
		//see above
		//memcpy(temp, block, size);
		//strcat(buf, temp);
	}
	else {
		if ((ret = testfs_read_block(in, block_nr, block)) < 0)
			return ret;
		memcpy(buf, block + block_ix, size);
		/* return the number of bytes read or any error */
	}
	return size;
}

/* given logical block number, allocate a new physical block, if it does not
 * exist already, and return the physical block number that is allocated.
 * returns negative value on error. */
static int
testfs_allocate_block(struct inode *in, int log_block_nr, char *block)
{
	int phy_block_nr;
	char indirect[BLOCK_SIZE];
	int indirect_allocated = 0;

	assert(log_block_nr >= 0);
	phy_block_nr = testfs_read_block(in, log_block_nr, block);

	//given code below handles EFBIG error
	/* phy_block_nr > 0: block exists, so we don't need to allocate it, 
	   phy_block_nr < 0: some error */
	if (phy_block_nr != 0)
		return phy_block_nr;

	/* allocate a direct block */
	if (log_block_nr < NR_DIRECT_BLOCKS) {
		assert(in->in.i_block_nr[log_block_nr] == 0);
		phy_block_nr = testfs_alloc_block_for_inode(in);
		if (phy_block_nr >= 0) {
			in->in.i_block_nr[log_block_nr] = phy_block_nr;
		}
		return phy_block_nr;
	}

	log_block_nr -= NR_DIRECT_BLOCKS;
	if (log_block_nr >= NR_INDIRECT_BLOCKS){
		log_block_nr -= NR_INDIRECT_BLOCKS;
		char dindirect[BLOCK_SIZE];
		int dindirect_allocated = 0;
		int index = log_block_nr / NR_INDIRECT_BLOCKS;
		assert (log_block_nr < NR_INDIRECT_BLOCKS*NR_INDIRECT_BLOCKS);

		//dindirect
		if (in -> in.i_dindirect == 0){
			bzero(dindirect, BLOCK_SIZE);
			phy_block_nr = testfs_alloc_block_for_inode(in);
			if (phy_block_nr < 0)
				return phy_block_nr;
			dindirect_allocated = 1;
			in -> in.i_dindirect = phy_block_nr;
		} else {
			read_blocks(in -> sb, dindirect, in -> in.i_dindirect, 1);
		}

		//indirect
    	if(((int*) dindirect)[index] == 0) {
    		bzero(indirect, BLOCK_SIZE);
    		phy_block_nr = testfs_alloc_block_for_inode(in);
    		if (phy_block_nr < 0){ //and == -ENOSPC?
        		if (dindirect_allocated) {
            		testfs_free_block_from_inode(in, in->in.i_dindirect);
					in -> in.i_dindirect = 0;
        		}
				return phy_block_nr;
    		}
        	indirect_allocated = 1;
        	((int *) dindirect)[index] = phy_block_nr;
			write_blocks(in -> sb, indirect, ((int *)dindirect)[index], 1);
       		write_blocks(in -> sb, dindirect, in -> in.i_dindirect, 1);
    	} else {
        	read_blocks(in->sb, indirect, ((int*) dindirect)[index], 1);
    	}

    	//direct
    	assert(((int *) indirect)[log_block_nr % NR_INDIRECT_BLOCKS] == 0);
    	phy_block_nr = testfs_alloc_block_for_inode(in);
    	if(phy_block_nr < 0){
       		if(dindirect_allocated){
        		testfs_free_block_from_inode(in, ((int *)dindirect)[index]);
        		testfs_free_block_from_inode(in, in->in.i_dindirect);
				in -> in.i_dindirect = 0;
    		} else if(indirect_allocated) {
        		testfs_free_block_from_inode(in, ((int *)dindirect)[index]);
				in -> in.i_indirect = 0;
			}
    		return phy_block_nr;
    	}
    	else {
        	((int *) indirect)[log_block_nr % NR_INDIRECT_BLOCKS] = phy_block_nr;
			write_blocks(in->sb, indirect, ((int *)dindirect)[index], 1);
			write_blocks(in -> sb, dindirect, in -> in.i_dindirect, 1);
    	} 
    	return phy_block_nr;
	}

	//below is given code
	//must be indirect
	if (in->in.i_indirect == 0) {	/* allocate an indirect block */
		bzero(indirect, BLOCK_SIZE);
		phy_block_nr = testfs_alloc_block_for_inode(in);
		if (phy_block_nr < 0)
			return phy_block_nr;
		indirect_allocated = 1;
		in->in.i_indirect = phy_block_nr;
	} else {	/* read indirect block */
		read_blocks(in->sb, indirect, in->in.i_indirect, 1);
	}

	/* allocate direct block */
	assert(((int *)indirect)[log_block_nr] == 0);	
	phy_block_nr = testfs_alloc_block_for_inode(in);

	if (phy_block_nr >= 0) {
		/* update indirect block */
		((int *)indirect)[log_block_nr] = phy_block_nr;
		write_blocks(in->sb, indirect, in->in.i_indirect, 1);
	} else if (indirect_allocated) {
		/* there was an error while allocating the direct block, 
		 * free the indirect block that was previously allocated */
		testfs_free_block_from_inode(in, in->in.i_indirect);
		in->in.i_indirect = 0;
	}
	return phy_block_nr;
}

int
testfs_write_data(struct inode *in, const char *buf, off_t start, size_t size)
{
	char block[BLOCK_SIZE];
	long block_nr = start / BLOCK_SIZE; // logical block number in the file
	long block_ix = start % BLOCK_SIZE; //  index or offset in the block
	int ret;
	size_t temp = size;

	if (block_ix + size > BLOCK_SIZE) {
		if (size > 34376597504)
			return -EFBIG;
		temp -= BLOCK_SIZE - block_ix;
		if ((ret = testfs_allocate_block(in, block_nr, block)) < 0)
			return ret;
		memcpy(block + block_ix, buf, BLOCK_SIZE - block_ix);
		write_blocks(in -> sb, block, ret, 1);
		int i = 1;
		while (temp > BLOCK_SIZE){
			if ((ret = testfs_allocate_block(in, block_nr + i, block)) < 0)
				return ret;
			memcpy(block, buf + (BLOCK_SIZE * i) - block_ix, BLOCK_SIZE);
			write_blocks(in -> sb, block, ret, 1);
			i++;
			temp -= BLOCK_SIZE;
		}
		if ((ret = testfs_allocate_block(in, block_nr + i, block)) < 0){
			//
			//note that this covers the error handling in while loop as well
			in -> in.i_size = 34376597504;
			in -> i_flags |= I_FLAGS_DIRTY;
			return ret;
		}
		memcpy(block, buf + (BLOCK_SIZE * i) - block_ix, temp);
		write_blocks(in -> sb, block, ret, 1);
		assert(temp == size - BLOCK_SIZE*i + block_ix);
		if (size > 0)
			in->in.i_size = MAX(in->in.i_size, start + (off_t) size);
		in->i_flags |= I_FLAGS_DIRTY;
	}

	//below is given code
	else {
		/* ret is the newly allocated physical block number */
		ret = testfs_allocate_block(in, block_nr, block);
		if (ret < 0)
			return ret;
		memcpy(block + block_ix, buf, size);
		write_blocks(in->sb, block, ret, 1);
		/* increment i_size by the number of bytes written. */
		if (size > 0)
			in->in.i_size = MAX(in->in.i_size, start + (off_t) size);
		in->i_flags |= I_FLAGS_DIRTY;
		/* return the number of bytes written or any error */
	}
	return size;
}

int
testfs_free_blocks(struct inode *in)
{
	int i;
	int e_block_nr;

	/* last logical block number */
	e_block_nr = DIVROUNDUP(in->in.i_size, BLOCK_SIZE);

	/* remove direct blocks */
	for (i = 0; i < e_block_nr && i < NR_DIRECT_BLOCKS; i++) {
		if (in->in.i_block_nr[i] == 0)
			continue;
		testfs_free_block_from_inode(in, in->in.i_block_nr[i]);
		in->in.i_block_nr[i] = 0;
	}
	e_block_nr -= NR_DIRECT_BLOCKS;

	/* remove indirect blocks */
	if (in->in.i_indirect > 0) {
		char block[BLOCK_SIZE];
		//assert(e_block_nr > 0);???
		read_blocks(in->sb, block, in->in.i_indirect, 1);
		for (i = 0; i < e_block_nr && i < NR_INDIRECT_BLOCKS; i++) {
			if (((int *)block)[i] == 0)
				continue;
			testfs_free_block_from_inode(in, ((int *)block)[i]);
			((int *)block)[i] = 0;
		}
		testfs_free_block_from_inode(in, in->in.i_indirect);
		in->in.i_indirect = 0;
	}

	e_block_nr -= NR_INDIRECT_BLOCKS;
	/* handle double indirect blocks */
	if (e_block_nr > 0) {
		int count = 0;
		char block[BLOCK_SIZE];
		char temp[BLOCK_SIZE];
		read_blocks(in -> sb, block, in -> in.i_dindirect, 1);
        
		for(int i = 0; count < e_block_nr && i < NR_INDIRECT_BLOCKS; i++){
    		if(((int *)block)[i] > 0){
        		read_blocks(in->sb, temp, ((int *)block)[i], 1);
        		for(int j = 0; count < e_block_nr && j < NR_INDIRECT_BLOCKS; j++){
            		testfs_free_block_from_inode(in, ((int *) temp)[j]);
            		((int *) temp)[j] = 0;
            		count++;
        		}
        		testfs_free_block_from_inode(in, ((int *) block)[i]);
        		((int *) block)[i] = 0;
    		}
    	}
    	testfs_free_block_from_inode(in, in->in.i_dindirect);
    	in->in.i_dindirect = 0;
	}

	in->in.i_size = 0;
	in->i_flags |= I_FLAGS_DIRTY;
	return 0;
}
