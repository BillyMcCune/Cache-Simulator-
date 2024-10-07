#include <stdio.h>
#include <stdlib.h>
#include <string.h> 

struct data {
    int valid;
    int dirty; 
    int tag;
    unsigned char* data;
    int lru_counter; 
};

int log2(int n) {
    int r = 0;
    while (n >>= 1) r++;
    return r;
}

void storeDataInCache(struct data* line, unsigned char* src, int offset, int size, int block_size) {
    for(int i = 0; i < size; i++) {
        line->data[offset + i] = src[size - i - 1];
    }
    line->dirty = 1;
}

void evictblock(struct data* block, unsigned char* memory, int address, int block_size, int offset_bits, int index_bits, int set_Num, char* command, unsigned int storeData, int offset, int sizeOfData) {

    int logic = (block->tag << (offset_bits + index_bits)) | (set_Num << offset_bits);
    if (block->dirty == 1) {
        printf("replacement 0x%x dirty\n", logic);
        memcpy(memory + logic, block->data, block_size);
        block->dirty = 0;
    } else {
        printf("replacement 0x%x clean\n", logic);
    }
    memcpy(block->data, memory + (address & ~(block_size - 1)), block_size);
    if (strcmp(command, "store") == 0) {
        storeDataInCache(block, (unsigned char *)&storeData, offset, sizeOfData, block_size);
        block->dirty = 1;
    }
}


unsigned char* loadDataFromCache(struct data* line, int offset, int size, int block_size) {
    unsigned char* buffer = (unsigned char*)malloc(size * sizeof(unsigned char));
    memcpy(buffer, line->data + offset, size);
    return buffer;
}


int main(int argc, char const *argv[]) {

    FILE *tracefile = fopen(argv[1], "r");

    int addresslength = 24;
    int cache_size = atoi(argv[2]) * 1024; 
    int associativity = atoi(argv[3]);
    int block_size = atoi(argv[4]);
    int num_blocks = cache_size / block_size;
    int num_sets = num_blocks / associativity;
    int offset_bits = log2(block_size);
    int index_bits = log2(num_sets);
    int tag_bits = 24 - offset_bits - index_bits;

    unsigned char *memory = (unsigned char *)malloc(16 * 1024 * 1024 * sizeof(unsigned char));
    memset(memory, 0, 16 * 1024 * 1024 * sizeof(unsigned char)); 
    
    struct data **cache = (struct data **) malloc(num_sets * sizeof(struct data *));
    for (int i = 0; i < num_sets; i++) {
        cache[i] = (struct data *) malloc(associativity * sizeof(struct data));
        for (int j = 0; j < associativity; j++) {
            cache[i][j].dirty = 0;
            cache[i][j].tag = -1; 
            cache[i][j].data = (unsigned char *) malloc(block_size * sizeof(unsigned char)); 
            memset(cache[i][j].data, 0, block_size * sizeof(unsigned char)); 
            cache[i][j].lru_counter = 0; 
            cache[i][j].valid = 0;
        }
    }

    char command[10];
    unsigned int address;
    unsigned long long storeData;
    unsigned int sizeOfData, set_Num, tag, frameNum, hit, foundempty, max_lru, logic, offset;

    while (fscanf(tracefile, "%s %x %d", command, &address, &sizeOfData) != EOF) {
        set_Num = (address >> offset_bits) & (num_sets - 1);
        tag = address >> (offset_bits + index_bits);
        hit = 0;
        offset = address & (block_size - 1);
        if (strcmp(command, "store") == 0) {
            fscanf(tracefile, "%llx", &storeData);
            //printf("storeData: %x\n", storeData);
        }
       // printf("commmand: %s, address: %x, sizeOfData: %d\n", command, address, sizeOfData);
       // printf("set_Num: %d, tag: %d, offset: %d\n", set_Num, tag, offset);

        for (int i = 0; i < associativity; i++) {
            if (cache[set_Num][i].tag == tag) { //search through cache for tag match
                cache[set_Num][i].lru_counter = 0; 
                hit = 1;
                frameNum = i;
                if (strcmp(command, "store") == 0) {
                    storeDataInCache(&cache[set_Num][i], (unsigned char *)&storeData, offset, sizeOfData, block_size);
                    printf("%s 0x%x hit\n", command, address);
                }
                if (strcmp(command, "load") == 0) {
                    unsigned char* loadedData = loadDataFromCache(&cache[set_Num][i], offset, sizeOfData, block_size);
                    if (loadedData != NULL) {
                        printf("%s 0x%x hit ", command, address);
                        for (int i = 0; i < sizeOfData; i++) {
                            printf("%02hhx", loadedData[i]);
                        }
                        printf("\n");
                        free(loadedData);
                    }
                    }
                for(int j = 0; j < associativity; j++) {
                        if(j != i) {
                            cache[set_Num][j].lru_counter++;
                        }
                    }
                cache[set_Num][i].lru_counter = 0;
                cache[set_Num][i].valid = 1;
                break;
            }
        }

        if (!hit) { //if no hit, bring block into cache
            foundempty = 0;
            for (int i = 0; i < associativity; i++) { //search for empty block in cache
                if (cache[set_Num][i].valid == 0) {
                    cache[set_Num][i].tag = tag;
                    cache[set_Num][i].lru_counter = 0; 
                     for(int j = 0; j < associativity; j++) {
                        if(j != i) {
                            cache[set_Num][j].lru_counter++;
                        }
                    }
                    frameNum = i;
                    foundempty = 1;
                    memcpy(cache[set_Num][i].data, memory + (address & ~(block_size - 1)), block_size); //bring block into cache
                    if (strcmp(command, "store") == 0) {
                        storeDataInCache(&cache[set_Num][i], (unsigned char *)&storeData, offset, sizeOfData, block_size); //store data in cache
                    }
                    cache[set_Num][i].valid = 1;
                    break;
                }
            }

           if (!foundempty) { //if no empty block in cache, evict block
                max_lru = cache[set_Num][0].lru_counter; 
                frameNum = 0;
                for (int i = 1; i < associativity; i++) {
                    if(cache[set_Num][i].lru_counter > max_lru) {
                        max_lru = cache[set_Num][i].lru_counter;
                        frameNum = i;
                    }
                }

                evictblock(&cache[set_Num][frameNum], memory, address, block_size, offset_bits, index_bits, set_Num, command, storeData, offset, sizeOfData);

                cache[set_Num][frameNum].tag = tag;
                cache[set_Num][frameNum].lru_counter = 0;
                cache[set_Num][frameNum].valid = 1;

                for(int j = 0; j < associativity; j++) {
                    if(j != frameNum) {
                        cache[set_Num][j].lru_counter++;
                    }
                }
            }

            if(strcmp(command, "load") == 0) {
                unsigned char* loadedData = loadDataFromCache(&cache[set_Num][frameNum], offset, sizeOfData, block_size);
                    printf("%s 0x%x miss ", command, address);
                    for (int i = 0; i < sizeOfData; i++) {
                        printf("%02x", loadedData[i]);
                    }
                    printf("\n");
                    free(loadedData);
            } else {
                printf("%s 0x%x miss\n", command, address);
                }
        }
    }

    fclose(tracefile);
    for (int i = 0; i < num_sets; i++) {
        for (int j = 0; j < associativity; j++) {
            free(cache[i][j].data);
        }
        free(cache[i]);
    }
    free(cache);
    free(memory);

    return 0;
}





