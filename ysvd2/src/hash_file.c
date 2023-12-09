#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>	// for pow()
#include <assert.h> // for debugging
#include <limits.h>

#include "bf.h"
#include "../include/hash_file.h"

#define BITS sizeof(int) * 8

#define MAX_OPEN_FILES 20

#define CALL_BF(call)       \
{                           \
  BF_ErrorCode code = call; \
  if (code != BF_OK) {      \
	BF_PrintError(code);    \
	return HP_ERROR;        \
  }                         \
}

int hash_table[MAX_OPEN_FILES]; //hash table contains files and each file contains an ht_array (array of ids)

int DirtyUnpin(BF_Block *block)
{
  BF_Block_SetDirty(block);
  BF_UnpinBlock(block);
}

int checkOpenFiles()
{
  int i;

  for (i = 0; i < MAX_OPEN_FILES; i++)
  {
    if (hash_table[i] == -1)
      break;
  }
  if (i == MAX_OPEN_FILES)
  {
    printf("Open files are at maximum - more files can't be opened");
    return HT_ERROR;
  }
  return HT_OK;
}

void intToBinary(HT_info ht_info, int* binary, int var) {	
	// Loop through each bit
	int* result = malloc(BITS * sizeof(int));

	for (int i = ht_info.global_depth ; i >= 0 ; i--) {
		result[i] = var & 1; // Get the least significant bit
		
		// Right shift to check the next bit
		var = var >> 1;
	}

	for (int i = 0; i < ht_info.global_depth ; i++) {
        binary[i] = result[i];
    }
	
	free(result);
}

int binaryToInt(int* binary, int length) {
    int result = 0;
    for (int i = 0; i < length; i++) {
        result = result * 2 + binary[i];
    }
    return result;
}

// Hash Function
int hash(int id, int ht_array_size){
	return (id * (id+3)) % ht_array_size;
}
int hash2(int id, int ht_array_size){
	return id % ht_array_size;
}

HT_ErrorCode HT_Init() {
  	// for (int i = 0; i < MAX_OPEN_FILES; i++)
    // 	hash_table[i] = NULL;

  	return HT_OK;
}

extern int** ht_array_global;
extern int* fd_array;

HT_ErrorCode HT_CreateIndex(const char *filename, int depth) {
	HT_info ht_info;
	
	BF_Block* headblock;
	BF_Block_Init(&headblock);
	char* headdata;
	
	BF_Block* newblock;
	BF_Block_Init(&newblock);
	char* newdata;

	HT_block_info ht_block_info;

	int fd, fd_pos, starting_indexes;

	for (int i = 0; i < MAX_OPEN_FILES; i++)
		hash_table[i] = -1;

	if (BF_CreateFile(filename) < 0) {
		printf("Error creating file: %s in HT_CreateFile\n", filename);
		return HT_ERROR;
  	}

	if (BF_OpenFile(filename, &fd) < 0) {
		printf("Error opening file: %s in HT_CreateFile\n", filename);
		return HT_ERROR;
  	}

	for (int i=0 ; i<MAX_OPEN_FILES ; i++) {
		if (fd_array[i] == -1) {
			fd_array[i] = fd;
			fd_pos = i;
			break;
		}
	}
	
	if (BF_AllocateBlock(fd, headblock) < 0) {
		printf("Error allocating block in HT_CreateFile\n");
		return -1;
  	}

	headdata = BF_Block_GetData(headblock);

	starting_indexes = pow(2, depth); 	// 2^depth --> starting number of indexes

	ht_info.is_ht = true;
	ht_info.fileDesc = fd;
	ht_info.global_depth = depth;
	ht_info.ht_array_size = starting_indexes;
	ht_array_global[fd_pos] = malloc(ht_info.ht_array_size * sizeof(int));
	ht_info.ht_array = ht_array_global[fd_pos];
	// Half point to Block1 and half to Block2
	for (int i=0 ; i<ht_info.ht_array_size ; i++) {
		if (i < ht_info.ht_array_size/2)
			ht_info.ht_array[i] = 1;
		else
			ht_info.ht_array[i] = 2;
	}
	ht_info.ht_array_head = 0;
	ht_info.ht_array_length = 1;
	ht_info.num_blocks = 2;
	
	memcpy(headdata, &ht_info, sizeof(HT_info));
	BF_Block_SetDirty(headblock);
	if (BF_UnpinBlock(headblock) < 0) {
		printf("Error unpinning headblock in HT_CreateFile\n");
		return HT_ERROR;
  	}

	// Allocate 1st starting block
	if (BF_AllocateBlock(ht_info.fileDesc, newblock) < 0) {
		printf("Error allocating block in HT_CreateFile\n");
		return -1;
	}
	newdata = BF_Block_GetData(newblock);
	
	ht_block_info.local_depth = 1;
	ht_block_info.max_records = (BF_BLOCK_SIZE - sizeof(HT_block_info)) / sizeof(Record);
	ht_block_info.next_block = 0;
	ht_block_info.num_records = 0;
	// Our admittion, we always start with 2 block, no matter the global_depth/starting buckets
	// so half indexes point to this Block 1 and the other half to Block 2 we allocate below
	ht_block_info.indexes_pointed_by = starting_indexes/2;
	memcpy(newdata + BF_BLOCK_SIZE - sizeof(HT_block_info), &ht_block_info, sizeof(HT_block_info));
	BF_Block_SetDirty(newblock);
	if (BF_UnpinBlock(newblock) < 0) {
		printf("Error allocating newblock in HT_CreateFile\n");
		return -1;
	}

	// Allocate 2nd starting block
	if (BF_AllocateBlock(ht_info.fileDesc, newblock) < 0) {
		printf("Error allocating block in HT_CreateFile\n");
		return -1;
	}
	newdata = BF_Block_GetData(newblock);
	ht_block_info.local_depth = 1;
	ht_block_info.max_records = (BF_BLOCK_SIZE - sizeof(HT_block_info)) / sizeof(Record);
	ht_block_info.next_block = 0;
	ht_block_info.num_records = 0;
	ht_block_info.indexes_pointed_by = starting_indexes/2;
	memcpy(newdata + BF_BLOCK_SIZE - sizeof(HT_block_info), &ht_block_info, sizeof(HT_block_info));
	BF_Block_SetDirty(newblock);
	if (BF_UnpinBlock(newblock) < 0) {
		printf("Error allocating newblock in HT_CreateFile\n");
		return -1;
	}

	if (BF_CloseFile(fd) < 0) {
		printf("Error closing fd in HT_CloseFile\n");
		return -1;
  	}
	// If we close the file, we need to unlock its Position on the array of max open files
	fd_array[fd_pos] = -1;

  	return HT_OK;
}

HT_ErrorCode HT_OpenIndex(const char *fileName, int *indexDesc){
  	/* Open files are at maximum - we can't open more */
    if (checkOpenFiles() == HT_ERROR)
      return HT_ERROR;

    HT_info ht_info;

    BF_Block *block;
    BF_Block_Init(&block);
    void *data;

	int fd, fd_pos;

    /*Open the file*/
	if (BF_OpenFile(fileName, &fd) < 0) {
		printf("Error opening file: %s in HT_CreateFile\n", fileName);
		return HT_ERROR;
  	}

	for (int i=0 ; i<MAX_OPEN_FILES ; i++) {
		if (fd_array[i] == -1) {
			fd_array[i] = fd;
			fd_pos = i;
			break;
		}
	}

    if ((BF_GetBlock(fd, 0, block)) < 0) {
		printf("Error getting block in HT_InsertEntry\n");
		return HT_ERROR;
	}
	data = BF_Block_GetData(block);
	memcpy(&ht_info, data, sizeof(HT_info));

	if (!ht_info.is_ht) {
		printf("This is NOT a hash file.");
		return HT_ERROR;
	}

	*indexDesc = fd_pos;

    BF_UnpinBlock(block);
	BF_Block_Destroy(&block);

    return HT_OK;
}

HT_ErrorCode HT_CloseFile(int indexDesc) {
	if (BF_CloseFile(indexDesc) < 0) {
		printf("Error closing fd in HT_CloseFile\n");
		return -1;
  	}
	for(int i=0 ; i<MAX_OPEN_FILES ; i++) {
		if (fd_array[i] == indexDesc) {
			fd_array[i] = -1;
			break;
		}
	}

  	return HT_OK;
}

HT_ErrorCode HT_InsertEntry(int indexDesc, Record record) {

	char* head_data;
	char* target_data;
	HT_info ht_info;
	HT_block_info ht_block_info;
	BF_Block* head_block;
	BF_Block* target_block;
	BF_Block_Init(&head_block);
	BF_Block_Init(&target_block);

	if ((BF_GetBlock(indexDesc, 0, head_block)) < 0) {
		printf("Error getting block in HT_InsertEntry\n");
		return -1;
	}

	// get pointer to block's 0 data
	head_data = BF_Block_GetData(head_block);
	// get the metadata of this head_block so that we can access ht_info.fileDesc
	memcpy(&ht_info, head_data, sizeof(HT_info));

	// In which bucket to insert
	// int bucket_to_insert = hash(record.id, ht_info.ht_array_size);
	int bucket_to_insert = hash2(record.id, ht_info.ht_array_size);

	int* binary = malloc(BITS * sizeof(int));
	intToBinary(ht_info, binary, record.id);

	bucket_to_insert = binaryToInt(binary, ht_info.global_depth);
	free(binary);

	if(bucket_to_insert < 0 || bucket_to_insert >= ht_info.ht_array_size) {
		return -1;
	}
	assert(bucket_to_insert >= 0 && bucket_to_insert < ht_info.ht_array_size);
	
	// Check if bucket has enough space for the Record
	int target_block_id = ht_info.ht_array[bucket_to_insert];
	// Pin target_block
	if (BF_GetBlock(indexDesc, target_block_id, target_block) < 0) {
		printf("Error getting target_block in HT_InsertEntry\n");
		return -1;
	}
	// get pointer to target_block's data
	target_data = BF_Block_GetData(target_block);
	// copy its data to our local var
	memcpy(&ht_block_info, target_data + BF_BLOCK_SIZE - sizeof(HT_block_info), sizeof(HT_block_info));
	// Check if there is room to add the record
	if (ht_block_info.num_records < ht_block_info.max_records) {
		
		// insert the record in the last position of records in the block
		memcpy(target_data + sizeof(Record) * ht_block_info.num_records, &record, sizeof(Record));

		// this block's data changed, so we update its ht_block_info
		ht_block_info.num_records++;

		// ht_array is updated - already checked with prints

		// copy the updated ht_block_info at the end of the block
		memcpy(target_data + BF_BLOCK_SIZE - sizeof(HT_block_info), &ht_block_info, sizeof(HT_block_info));

		// write the block back on disc
		BF_Block_SetDirty(target_block);
		BF_UnpinBlock(target_block);

		BF_Block_Destroy(&target_block);

		return HT_OK;
	}
	else {
		// if there is not enough space in tthe target block
		// printf(" =====PLEON DEN XWRANE RECORDS STO BLOCK %d======== \n", target_block_id);
		
		// Array of 8 records to store the old ones i have to re-insert
		Record copy_records[8];
		// Up to 8 records will be stored, so i init IDs as -1 at first to indicate the end of the valid records, when id = -1 is reached
		// We will use ht_block_info.num_records anyway, but -1 is an extra safety
		for(int i=0 ; i<8 ; i++) {
			copy_records[i].id = -1;
		}

		// Here we have: target_block_id, new_block_id

		// At this point ht_block_info contains target_block's info
		// Check if more than 1 ht_array indexes point to the FULL BLOCK
		if((ht_block_info.local_depth < ht_info.global_depth) || (ht_block_info.indexes_pointed_by > 1)) {

			// Till this line, target_block points to hashed target_block
			// So, now we create a new block* so that we can track both old-target-block and new-inserted-block
			char* new_data;
			BF_Block* new_block;
			BF_Block_Init(&new_block);
			int new_block_id;

			HT_block_info new_ht_block_info;

			// We allocate a new block
			if (BF_AllocateBlock(ht_info.fileDesc, new_block) < 0) {
			printf("Error allocating block in HT_InsertEntry\n");
			return -1;
			}

			// update ht_info's total block/buckets number (+1 allocated block)
			ht_info.num_blocks++;

			// Here we have: target_block_id and new_block_id
			// We get new blocks id
			if ((BF_GetBlockCounter(ht_info.fileDesc, &new_block_id)) < 0) {
				printf("Error getting num of blocks in HT_InsertEntry\n");
				return -1;
			}
			new_block_id--; // we "subtract" block with id: 0, containing ht_info
			
			new_ht_block_info.num_records = 0;
			new_ht_block_info.local_depth = ht_block_info.local_depth;
			new_ht_block_info.max_records = (BF_BLOCK_SIZE - sizeof(HT_block_info)) / sizeof(Record);
			new_ht_block_info.next_block = 0;
			new_ht_block_info.indexes_pointed_by = 0; // will be updated/increase later (otan kanw ++)

			new_data = BF_Block_GetData(new_block);

			memcpy(new_data + BF_BLOCK_SIZE - sizeof(HT_block_info), &new_ht_block_info, sizeof(HT_block_info));
			BF_Block_SetDirty(new_block);

			ht_block_info.local_depth++;
			new_ht_block_info.local_depth++;

			int i = 0;
			int count = 0;
      		int old_num_indexes = ht_block_info.indexes_pointed_by;

			// Iterate through ht_array
			do {
				// For each index pointing to full target block
				if (ht_info.ht_array[i] == target_block_id) {
					// In order to equally distribute the indexes pointing to full block and the new block
					if (old_num_indexes != 1) {
						if (count >= old_num_indexes/2) {
							ht_info.ht_array[i] = new_block_id;
							new_ht_block_info.indexes_pointed_by++;
							ht_block_info.indexes_pointed_by--;
						}
					} // if old_num_indexes == 1, we ust pass
          			count++;
				}
				i++;
			} while (i < ht_info.ht_array_size); // minus the above target re-routed one

			// Re-write new_ht_block_info to new block (changes indexes_pointed_by from inside while)
			memcpy(new_data + BF_BLOCK_SIZE - sizeof(HT_block_info), &new_ht_block_info, sizeof(HT_block_info));
			BF_Block_SetDirty(new_block);
			// we would also memcpy/setDirty ht_block_info here, but we need to changes its num_records to 0 later on, after we use it in ForLoop below

			// Re-write it to Block 0 (changes ht_array + num_blocks from outside IF above)
			memcpy(head_data, &ht_info, sizeof(HT_info));
			// Update ht_info by overwriting it (as always) and store it
			// so that everything is ready to re-call HT_InsertEntry()
			BF_Block_SetDirty(head_block);
			BF_UnpinBlock(head_block);


			i = 0;
			// For each already existing record in FULL BLOCK (target_block) + Record_to_insert -> HT_InsertEntry(...) (hashing with 1 more bucket this time)
      		for(int k = 0 ; k < ht_block_info.num_records * sizeof(Record) ; k = k + sizeof(Record)) {
				// copy in our Record copy_array, the contents of the current record we are reading, in order to re-insert it
        		memcpy(&copy_records[i++], target_data + k, sizeof(Record));
			}

			// !Important - this move marks total records of FULL_BLOCK as 0, making it APPEAR as EMPTY while it is indeed STILL FULL
			int old_num_records = ht_block_info.num_records;
			ht_block_info.num_records = 0;
			// !Important - we do that so we can re-insert with the new hash_func + buckets the old records and the new one
			// by overwriting this full block, as if it was empty
			// we trust that we wont read old remaining records by using ht_block_info.num_records as ceiling! ( new_recs < olds_recs )
			memcpy(target_data + BF_BLOCK_SIZE - sizeof(HT_block_info), &ht_block_info, sizeof(HT_block_info));
			BF_Block_SetDirty(target_block);

			// Finally store all changes
			BF_UnpinBlock(new_block);
			BF_UnpinBlock(target_block);

			// Now that everything is set, we are ready to re-insert all the old records + the new one
			i = 0;

			// For each already existing record in FULL BLOCK + Record_to_insert -> HT_InsertEntry(...) (hashing with 1 more bucket this time)
			for(int k = 0 ; k < old_num_records * sizeof(Record) ; k = k + sizeof(Record)) {
				// We use our record array with the copies
				Record temp_record = copy_records[i++];
				if (HT_InsertEntry(ht_info.fileDesc, temp_record) != HT_OK)
					return HT_ERROR;
			}
			
			// Now once more for the new Record to insert as we said above
			if (HT_InsertEntry(ht_info.fileDesc, record) != HT_OK)
				return HT_ERROR;

			// If we get here, all insertions (old + new) were executed properly, thus
      		BF_Block_Destroy(&new_block);
			return HT_OK;
		}
		else {
			// At this point ht_block_info contains target_block's info
			// Check if more than 1 ht_array indexes point to the FULL BLOCK
      		
			// Double ht_array's size
			int old_ht_array_size = ht_info.ht_array_size;
			int new_ht_array_size = old_ht_array_size * 2;
			// int new_ht_array_size_bin = new_ht_array_size;
			ht_info.ht_array_size = new_ht_array_size;
			ht_info.ht_array = realloc(ht_info.ht_array, new_ht_array_size * sizeof(int));
			assert(ht_info.ht_array != NULL);
			
			// // Temporary array to hold binary bits
			int* binary = malloc(BITS * sizeof(int));

			// Create a temporary copy of the old hash table
			int* old_ht_array = malloc(old_ht_array_size * sizeof(int));
			for(int j=0 ; j < old_ht_array_size ; j++) {
				old_ht_array[j] = ht_info.ht_array[j];
			}
			
			char* curr_data;
			BF_Block* curr_block;
			BF_Block_Init(&curr_block);

			HT_block_info curr_ht_block_info;

			// For each entry in the old hash table
			// Extract the first d bits of the index
			for (int i = 0; i < new_ht_array_size; i++) {
				
				// Firstly, reset indexesPointedBy (so that we recalculate them later)
				for (int j=0 ; j < old_ht_array_size ; j++) {
					BF_GetBlock(ht_info.fileDesc, ht_info.ht_array[i], curr_block);
					curr_data = BF_Block_GetData(curr_block);
					memcpy(&curr_ht_block_info, curr_data + BF_BLOCK_SIZE - sizeof(HT_block_info), sizeof(HT_block_info));
					curr_ht_block_info.indexes_pointed_by = 0;
					memcpy(curr_data + BF_BLOCK_SIZE - sizeof(HT_block_info), &curr_ht_block_info, sizeof(HT_block_info));
					BF_Block_SetDirty(curr_block);
					BF_UnpinBlock(curr_block);					
				}

				intToBinary(ht_info, binary, i);

				int bucket_to_insert = binaryToInt(binary, ht_info.global_depth);

				// Copy the bucket pointer from the old hash table to the new hash table
				ht_info.ht_array[i] = old_ht_array[bucket_to_insert];

				BF_GetBlock(ht_info.fileDesc, ht_info.ht_array[i], curr_block);
				curr_data = BF_Block_GetData(curr_block);
				memcpy(&curr_ht_block_info, curr_data + BF_BLOCK_SIZE - sizeof(HT_block_info), sizeof(HT_block_info));
				curr_ht_block_info.indexes_pointed_by++;
				memcpy(curr_data + BF_BLOCK_SIZE - sizeof(HT_block_info), &curr_ht_block_info, sizeof(HT_block_info));
				BF_Block_SetDirty(curr_block);
				BF_UnpinBlock(curr_block);
			}
			
			BF_Block_Destroy(&curr_block);

			// Free the temporary copy of the old hash table
			free(old_ht_array);

			// Finally, update the global depth
			ht_info.global_depth++;

			// Store updated global depth and th_array
			memcpy(head_data, &ht_info, sizeof(HT_info));
			BF_Block_SetDirty(head_block);
			BF_UnpinBlock(head_block);
			
			// Now once more for the new Record to insert as we said above
			if (HT_InsertEntry(ht_info.fileDesc, record))
				return HT_ERROR;

      		free(binary);
			// If we get here, all insertions (old + new) were executed properly, thus
			BF_Block_Destroy(&target_block);
			BF_Block_Destroy(&head_block);
			return HT_OK;
		}
	}

	// FInally, if every record was inserted properly, we return code successful
	return HT_OK;
}

HT_ErrorCode HT_PrintAllEntries(int indexDesc, int *id) {

	char* data;
	BF_Block* block;
	BF_Block_Init(&block);
	HT_info ht_info;
	HT_block_info ht_block_info;
	Record record;

	if ((BF_GetBlock(indexDesc, 0, block)) < 0) {
		printf("Error getting block in HT_InsertEntry\n");
		return HT_ERROR;
	}

	// get pointer to block's 0 data
	data = BF_Block_GetData(block);
	// get the metadata of this block so that we can access ht_info.fileDesc
	memcpy(&ht_info, data, sizeof(HT_info));

	// Firstly get the total num of blocks in heap file
	int total_blocks;
	if ((BF_GetBlockCounter(ht_info.fileDesc, &total_blocks)) < 0) {
		printf("Error getting num of blocks in HT_InsertEntry\n");
		return HT_ERROR;
	}
	printf("TOTAL BLOCKS: %d\n", total_blocks);

	// Unpin block 0 since we dont need it anymore
	if ((BF_UnpinBlock(block)) < 0) {
		printf("Error unpinning block in HT_InsertEntry\n");
		return HT_ERROR;
	}

	// check if it's the one we are looking for
	if (id != NULL) {
		int found = 0;

		// Hash id to find the bucket where the record is supposed to be
		int bucket_to_insert = hash2(*id, ht_info.ht_array_size);
		int* binary = malloc(BITS * sizeof(int));
		intToBinary(ht_info, binary, *id);

		bucket_to_insert = binaryToInt(binary, ht_info.global_depth);
		free(binary);

		// Bring this block to memory
		if ((BF_GetBlock(ht_info.fileDesc, ht_info.ht_array[bucket_to_insert], block)) < 0) {
		return HT_ERROR;
		}

		// get pointer to this block's data
		data = BF_Block_GetData(block);

		// get the metadata of this last block
		memcpy(&ht_block_info, data + BF_BLOCK_SIZE - sizeof(HT_block_info), sizeof(HT_block_info));

		// for every record in this block
		for(int k = 0 ; k < ht_block_info.num_records * sizeof(Record) ; k = k + sizeof(Record)) { 
			// copy in our variable record, the contents of the current record we are reading
			memcpy(&record, data + k, sizeof(Record));
			// we print it each time it occures
			if (record.id == *id) {
				found = 1;
				printf("Record in offset %d in Block %d: id=%d, name=%s, surname=%s, city=%s\n", k, ht_info.ht_array[bucket_to_insert], record.id, record.name, record.surname, record.city);
			}
		}

		if (!found)
			printf("Record with id: %d was not found.\n", *id);

	}
	else {
		for (int i=1 ; i<total_blocks ; i++) {
			// Bring this block to memory
			if ((BF_GetBlock(ht_info.fileDesc, i, block)) < 0) {
			return -1;
			}

			// get pointer to this block's data
			data = BF_Block_GetData(block);

			// get the metadata of this last block
			memcpy(&ht_block_info, data + BF_BLOCK_SIZE - sizeof(HT_block_info), sizeof(HT_block_info));

			// for every record in this block
			for(int k = 0 ; k < ht_block_info.num_records * sizeof(Record) ; k = k + sizeof(Record)) {
				// copy in our variable record, the contents of the current record we are reading
				memcpy(&record, data + k, sizeof(Record));

				printf("Record in offset %d in Block %d: id=%d, name=%s, surname=%s, city=%s\n", k, i, record.id, record.name, record.surname, record.city);
			
				// Write babck on disk the block we just read
				if ((BF_UnpinBlock(block)) < 0) {
					printf("Error unpinning block in HT_GetAllEntries\n");
					return HT_ERROR;
				}
			}
		}
	}

	BF_Block_Destroy(&block);

	return HT_OK;
}

HT_ErrorCode HashStatistics(char* filename) {
	HT_info ht_info;
	char* data;
	BF_Block* block;
	BF_Block_Init(&block);
	HT_block_info ht_block_info;
	int indexDesc;

	if (BF_OpenFile(filename, &indexDesc) != BF_OK) {
		printf("Error opening file: %s in HT_CreateFile\n", filename);
		return HT_ERROR;
	}

	BF_GetBlock(indexDesc, 0, block);
	data = BF_Block_GetData(block);
	memcpy(&ht_info, data, sizeof(HT_info));

	int total_blocks;
	BF_GetBlockCounter(ht_info.fileDesc, &total_blocks);
	printf("\n");
	printf("File \"%s\" Statistics:\n", filename);
	printf("Total blocks: %d\n", total_blocks);

	int min_records = INT_MAX;
	int max_records = INT_MIN;
	int total_records = 0;
	float avg_records;

	// Skip block 0 containing HT_info
	for (int i=1 ; i<total_blocks ; i++) {
		// Bring this block to memory
		if ((BF_GetBlock(ht_info.fileDesc, i, block)) < 0) {
			return HT_ERROR;
		}

		data = BF_Block_GetData(block);

		// get the metadata of this last block
		memcpy(&ht_block_info, data + BF_BLOCK_SIZE - sizeof(HT_block_info), sizeof(HT_block_info));

		total_records += ht_block_info.num_records;

		if (ht_block_info.num_records < min_records) {
			min_records = ht_block_info.num_records;
		}
		if (ht_block_info.num_records > max_records) {
			max_records = ht_block_info.num_records;
		}

		// Write back on disk the block we just read
		if ((BF_UnpinBlock(block)) < 0) {
			printf("Error unpinning block in HT_GetAllEntries\n");
			return HT_ERROR;
		}
	}

	printf("Minimun number of records in a block: %d\n", min_records);
	printf("Maximun number of records in a block: %d\n", max_records);
	printf("Average number of records in a block: %.2f\n", (float)total_records/(total_blocks-1)); // subtract block 0 containing ht_info

	BF_UnpinBlock(block); // unpin block 0

	BF_Block_Destroy(&block);

	return HT_OK;
}