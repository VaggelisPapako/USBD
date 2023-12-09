#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bf.h"
#include "hp_file.h"
#include "record.h"

// #define CALL_BF(call)         \
//   {                           \
//     BF_ErrorCode code = call; \
//     if (code != BF_OK)        \
//     {                         \
//       BF_PrintError(code);    \
//       return HP_ERROR;        \
//     }                         
//   }

int Check(int call)
{
  BF_ErrorCode code = call;
  if (code != BF_OK)
  {
    BF_PrintError(code);
    printf("bf_error_code = %d\n", code);
    return -1;
  }
}

int DirtyUnpin(BF_Block* block) {
  BF_Block_SetDirty(block);
  if (Check(BF_UnpinBlock(block)) < 0) {
    printf("Error allocating block in HP_CreateFile\n");
    return -1;
  }
}

int HP_CreateFile(char *fileName)
{
  HP_info hp_info;
  BF_Block *block;
  void *headblock;
  void *data;
  int F;

  //BF_Init(LRU);

  if (Check(BF_CreateFile(fileName)) < 0) {
    printf("Error creating file: %s in HP_CreateFile\n", fileName);
    return -1;
  }

  if (Check(BF_OpenFile(fileName, &hp_info.fileDesc)) < 0) {
    printf("Error opening file: %s in HP_CreateFile\n", fileName);
    return -1;
  }

  /* first block -> heap file hp_info */
  BF_Block_Init(&block);

  // allocate a block
  if (Check(BF_AllocateBlock(hp_info.fileDesc, block)) < 0) {                 
    printf("Error allocating block in HP_CreateFile\n");
    return -1;
  }

  headblock = BF_Block_GetData(block);                                            // pointer to the start of block headblock
  BF_GetBlock(hp_info.fileDesc, 0, block);                                        // finds and brings in memory block number: 2nd arg
  hp_info.is_heap = true;                                                         // heap file
  hp_info.first_block_id = block;
  hp_info.max_records = (BF_BLOCK_SIZE - sizeof(HP_block_info)) / sizeof(Record); // max number of records that fit in the block
  hp_info.id_last = 0;                                                            // there is no other record

  /* copy struct HP_info in the first block */
  memcpy(headblock, &hp_info, sizeof(HP_info));


  /* make it dirty and unpin so that it is saved in disc */
  F = DirtyUnpin(block);
  if (F==-1) {return -1;}

  // Allocate another block
  if (Check(BF_AllocateBlock(hp_info.fileDesc, block)) < 0) {
    printf("Error allocating block in HP_CreateFile\n");
    return -1;
  }

  data = BF_Block_GetData(block);
  HP_block_info hp_block_info;

  /* initialization of hp_block_info that goes to the end of this specific block */
  hp_block_info.num_rec = 0;
  hp_block_info.next = 0;
  memcpy(data, &hp_block_info, sizeof(HP_block_info));

  if (Check(DirtyUnpin(block)) < 0) {
    printf("Error unpinning block in HP_InsertEntry\n");
    return -1;
  }

  BF_Block_Destroy(&block);

  Check(BF_CloseFile(hp_info.fileDesc));

  return 0;
}

HP_info *HP_OpenFile(char *fileName, int *file_desc)
{
  HP_info *hpInfo;
  BF_Block* block;

  if (Check(BF_OpenFile(fileName, file_desc)) < 0) {
    printf("Error opening file: %s in HP_OpenFile\n", fileName);
    return NULL;
  }

  BF_Block_Init(&block);

  if (Check(BF_GetBlock(*file_desc, 0, block)) < 0) { // automatically Pins -> needs unpoinBlock
    return NULL;
  }

  void* data = BF_Block_GetData(block);
  hpInfo = data;

  if (Check(BF_UnpinBlock(block)) < 0) {
    printf("Error allocating block in HP_OpenFile\n");
    return NULL;
  }
  
  return &hpInfo[0];
}

int HP_CloseFile(int file_desc, HP_info *hp_info)
{
  if (hp_info == NULL) {
    printf(" it's null in close_file\n");
    return -1;
  }
  if (Check(BF_CloseFile(hp_info->fileDesc)) < 0) {
    printf("Error closing fd in HP_CloseFile\n");
    return -1;
  }
  // Double Free error
  // free(hp_info->first_block_id); // free bf_block pointer in hp_info struct
  // free(hp_info); // free struct hp_info
  return 0;
}

int HP_InsertEntry(int file_desc, HP_info *hp_info, Record record)
{
  void* data; // points at the BEGINNING of a block's data
  // char* data; // etsi den exei errors to data h cast
  bool new_block_needed = false;

  HP_block_info hp_block_info;

  BF_Block* block;
  BF_Block_Init(&block);

  int total_blocks;

  if (Check(BF_GetBlockCounter(hp_info->fileDesc, &total_blocks)) < 0) {
    printf("Error getting num of blocks in HP_InsertEntry\n");
    return -1;
  }

  // If there is more than 1 block
  if (total_blocks > 1) {
    // bring the block to memory
    // if (Check(BF_GetBlock(file_desc, hp_info->id_last, block)) < 0) {
    if (Check(BF_GetBlock(file_desc, total_blocks-1, block)) < 0) {
      return -1;
    }

    // get pointer to last block's data
    data = BF_Block_GetData(block);

    // get the metadata of this last block
    memcpy(&hp_block_info, data + BF_BLOCK_SIZE - sizeof(HP_block_info), sizeof(HP_block_info));

    // if there is enough space for the record
    if (hp_block_info.num_rec < hp_info->max_records) {
      // insert the record in the last position (for records) in the block
      memcpy(data + sizeof(Record) * hp_block_info.num_rec, &record, sizeof(Record));

      // this block's data changed, so we update its hp_block_indo
      hp_block_info.num_rec++;

      // copy the updated hp_info at the end of the block
      memcpy(data + BF_BLOCK_SIZE - sizeof(HP_block_info), &hp_block_info, sizeof(HP_block_info));

      // write the block back on disc
      if (Check(DirtyUnpin(block)) < 0) {
        printf("Error unpinning block in HP_InsertEntry\n");
        return -1;
      }
    }
    else {
      // there is not enough space
      if (Check(BF_UnpinBlock(block)) < 0) {
        printf("Error unpinning block in HP_InsertEntry\n");
        return -1;
      }

      // we need to allocate a new block to get out of this If
      new_block_needed = true;
    }
  }

  // We get here if its the first ever block or there was not enough space in the last one
  if (new_block_needed) {
    // we have to allocate a block and an hp_block_info for it
    if (Check(BF_AllocateBlock(hp_info->fileDesc, block)) < 0) {
      printf("Error allocating block in HP_InsertEntry\n");
      return -1;
    }
    data = BF_Block_GetData(block);

    hp_block_info.num_rec = 1;
    hp_block_info.next = 0;

    // copy hp info at the end of the block
    memcpy(data + BF_BLOCK_SIZE - sizeof(HP_block_info), &hp_block_info, sizeof(HP_block_info));

    // and copy the record as the first one in the lock
    memcpy(data, &record, sizeof(Record));
    
    // write the block back on disc
    if (Check(DirtyUnpin(block)) < 0) {
      printf("Error unpinning block in HP_InsertEntry\n");
      return -1;
    }
  }

  // refresh total blocks in case it increased (can only do this in heap files)
  if (Check(BF_GetBlockCounter(hp_info->fileDesc, &total_blocks)) < 0) {
    printf("Error getting num of blocks in HP_InsertEntry\n");
    return -1;
  }

  // we do the same to update the "global" hp_info struct
  if (Check(BF_GetBlock(file_desc, 0, block)) < 0) {
    return -1;
  }
  data = BF_Block_GetData(block);
  hp_info->id_last = total_blocks;
  memcpy(block, hp_info, sizeof(HP_info));
  BF_Block_SetDirty(block);
    
  BF_Block_Destroy(&block);

  return total_blocks;
}

int HP_GetAllEntries(int file_desc, HP_info *hp_info, int id)
{
  // Dikh mas paradoxh -> otan o xrhsths zhtaei thn 1h eggrafh -> epistrefoume auth sto "position 1"
  // ara an 8elei thn prwth eggrafh -> prepei na zhthsei thn id = 0

  void* data;
  BF_Block* block;
  BF_Block_Init(&block);
  
  HP_block_info hp_block_info;
  Record record;
  
  // number of blocks we had to read till the last record with this id is printed
  int num_blocks_read = 0;

  // Firstly get the total num of blocks in heap file
  int total_blocks;
  if (Check(BF_GetBlockCounter(hp_info->fileDesc, &total_blocks)) < 0) {
    printf("Error getting num of blocks in HP_InsertEntry\n");
    return -1;
  }

  // then for every block, apart from the first hp_info block
  for (int i=1 ; i<total_blocks ; i++) {
    // Bring this block to memory
    if (Check(BF_GetBlock(file_desc, i, block)) < 0) {
      return -1;
    }

    // get pointer to this block's data
    data = BF_Block_GetData(block);

    // get the metadata of this last block
    memcpy(&hp_block_info, data + BF_BLOCK_SIZE - sizeof(HP_block_info), sizeof(HP_block_info));

    // for every record in this block
    for(int k = 0 ; k < hp_block_info.num_rec * sizeof(Record) ; k = k + sizeof(Record)) {      
      // copy in our variable record, the contents of the current record we are reading
      memcpy(&record, data + k, sizeof(Record));

      // check if it's the one we are looking for
      if (record.id == id) {
        // we store as "number of total blocks read" the numbber of the block we just found the last occurance of the id we are searching for
        num_blocks_read = i;
        // we print it each time it occures
        printRecord(record);
      }
    }

    // Write babck on disk the block we just read
    if (Check(BF_UnpinBlock(block)) < 0) {
      printf("Error unpinning block in HP_GetAllEntries\n");
      return -1;
    }
  }

  // we used bf_init so now we destroy
  BF_Block_Destroy(&block);

  printf("Number of blocks read to find record with id %d is: %d\n", id, num_blocks_read);
  return num_blocks_read;
}
