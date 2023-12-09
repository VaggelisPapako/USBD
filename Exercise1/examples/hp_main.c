#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "bf.h"
#include "hp_file.h"

#define RECORDS_NUM 200 // you can change it if you want
#define FILE_NAME "data1.db"

// #define CALL_OR_DIE(call)     \
//   {                           \
//     BF_ErrorCode code = call; \
//     if (code != BF_OK) {      \
//       BF_PrintError(code);    \
//       exit(code);             \
//     }                         \
//   }

int main() {
  BF_Init(LRU);
  printf("Creating HP File... \n");
  HP_CreateFile(FILE_NAME);
  int file_desc;

  printf("Opening HP File... \n");
  HP_info* hp_info = HP_OpenFile(FILE_NAME, &file_desc);
  if (hp_info == NULL) {
    printf("Error in HP_OpenFile return pointer\n");
    return -1;
  }
  // printf(" hp_info->fileDesc = %d \n", hp_info->fileDesc);
  // printf(" hp_info->id_last = %d \n", hp_info->id_last);
  // printf(" hp_info->records = %d \n", hp_info->max_records);
  // printf(" hp_info->isHeap = %d \n", hp_info->is_heap);
  int blocks_num;
  BF_GetBlockCounter(file_desc, &blocks_num);

  Record record;
  srand(time(NULL));
  int last_block;
  printf("Inserting Entries... \n");
  for (int id = 0; id < RECORDS_NUM; ++id) {
    record = randomRecord();
    last_block = HP_InsertEntry(file_desc, hp_info, record);
  }

  int id = rand() % (RECORDS_NUM+1);
  // id = RECORDS_NUM + 1; // to check if it handles this correctly
  // id = 0; // to check if it handles this correctly
  // int id = 195;
  // we asked for Record id, which is in the id-1 position
  printf("Searching all entries for: %d\n", id);
  if (id <= RECORDS_NUM) {
    int blocks_read;
    if ((blocks_read = HP_GetAllEntries(file_desc, hp_info, id)) < 0) {
      printf("Error in GetAllEntries\n");
    }
  }
  else {
    printf("Record with id: %d does not exist. There are only %d records in this heap file.\n", id, RECORDS_NUM);
  }

  printf("Closing HP File. \n");

  HP_CloseFile(hp_info->fileDesc, hp_info);
  BF_Close();
}