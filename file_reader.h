#pragma once

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>

#define BYTES_PER_SECTOR 512
#define VOLUME_START 0

typedef uint32_t lba_t;
typedef uint32_t cluster_t;

struct boot_t
{      
    char loader_jump[3];         // 0x00 - 3
    char OEM_name[8];            // 0x03 - 8
    int16_t bytes_per_sector;    // 0x0B - 2
    uint8_t sectors_per_cluster; // 0x0D - 1
    uint16_t reserved_sectors;   // 0x0E - 2
    uint8_t fat_count;           // 0x10 - 1
    uint16_t root_dir_capacity;  // 0x11 - 2
    uint16_t logical_sectors16;  // 0x13 - 2
    uint8_t media_type;          // 0x15 - 1
    uint16_t sectors_per_fat;    // 0x16 - 2
    uint16_t sectors_on_track;   // 0x18 - 2
    uint16_t tracks_on_cylinder; // 0x1A - 2
    uint32_t hidden_sectors;     // 0x1C - 4
    uint32_t logical_sectors32;  // 0x20 - 4
    char disc_number;            // 0x24 - 1
    char disc_head_number;       // 0x25 - 1
    uint8_t boot_signature;      // 0x26 - 1
    uint32_t volume_ID;          // 0x27 - 4
    char volume_name[11];        // 0x2B - 11
    char FAT_ID[8];              // 0x36 - 8
    char bootloader_code[448];   // 0x3E - 448
    uint8_t loader_signature[2]; // 0x1FE - 2
} __attribute__((__packed__));
struct disk_t
{
    FILE *fp;
    /* dane o lokacjach obszarów */
};
/*
    - pointer na plik (FILE *fp) ?
    - dane z boot_sector'a ! aligned
    - pointer na FAT_table 1 & 2
    - pointer na root_sector
    - pointer na data_sector
*/
struct disk_t* disk_open_from_file(const char* volume_file_name);
int disk_read(struct disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read);
int disk_close(struct disk_t* pdisk);

struct volume_t
{
    struct disk_t* disk;
    struct boot_t boot;
    lba_t fat1_position;
    lba_t fat2_position;
    size_t bytes_per_fat;
    lba_t rootdir_position;
    lba_t rootdir_size;
    lba_t cluster2_position;
    lba_t volume_size;
    uint8_t* pfat1;
    uint8_t* pfat2;
};
/*
    - dane z disk_t ?
    kinda, na disk_t jest kilka FATów, a volume_t zczytuje jeden
    a na taki FAT składają się boot, FAT_table, rootdir i data_block
    więc jakby disk_t ma sam pointer na plik, a pozostałe podpunkty ogarnia volume_t
    oj no chyba jednak disk ogarnia boot_sector
*/
struct volume_t* fat_open(struct disk_t* pdisk, uint32_t first_sector);
int fat_close(struct volume_t* pvolume);

struct fat_dir_entry_t
{
    uint8_t filename[8];
    uint8_t ext[3];
    uint8_t file_attributes;
    uint8_t reserved;
    uint8_t file_creation_time;
    uint16_t creation_time;
    uint16_t creation_date;
    uint16_t access_date;
    uint16_t word_high;
    uint16_t modified_time;
    uint16_t modified_date;
    uint16_t word_low;
    uint32_t size;
} __attribute__((__packed__));

struct file_t
{
    //lba_t file_position;
    struct volume_t* volume;
    struct fat_dir_entry_t* entry;
    struct clusters_chain_t* cluster_chain;
    uint32_t offset;
};
/*
    - pojedyńczy plik, albo raczej pointer na wpis pliku w volume_t
*/
struct clusters_chain_t
{
    uint16_t *clusters;
    size_t size;
};
struct clusters_chain_t *get_chain_fat12(const void * const buffer, size_t size, uint16_t first_cluster, size_t max_clusters);
struct file_t* file_open(struct volume_t* pvolume, const char* file_name);
int file_close(struct file_t* stream);
size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream);
int32_t file_seek(struct file_t* stream, int32_t offset, int whence);



struct dir_t
{
    struct volume_t* volume;
    uint32_t location;
    uint32_t offset;
};
/*
    - pojedyńczy katalog z okolic root'a
    
*/
struct dir_entry_t
/*
    - ta, dir_t to pointer na wpis, a dir_entry_t to zczytane dane z wpisu
*/
{
    char name[12];
    size_t size;
    int is_readonly; // 0x01
    int is_hidden; // 0x02
    int is_system; // 0x04
    int is_directory; // 0x10
    int is_archived; // 0x20
};
struct dir_t* dir_open(struct volume_t* pvolume, const char* dir_path);
int dir_read(struct dir_t* pdir, struct dir_entry_t* pentry);
int dir_close(struct dir_t* pdir);