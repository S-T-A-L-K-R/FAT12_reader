#include "file_reader.h"


struct disk_t* disk_open_from_file(const char* volume_file_name)
{
    if(!volume_file_name)
    {
        errno = EFAULT;
        return NULL;
    }
    
    struct disk_t* my_disk = (struct disk_t*)malloc(sizeof(struct disk_t));
    if(!my_disk)
    {
        errno = ENOMEM;
        return NULL;
    }

    FILE *fp = fopen(volume_file_name, "rb");
    if(!fp)
    {
        errno = ENOENT;
        free(my_disk);
        return NULL;
    }

    my_disk->fp = fp;
    return my_disk;
}
int disk_read(struct disk_t* pdisk, int32_t first_sector, void* buffer, int32_t sectors_to_read)
{
    fseek(pdisk->fp, first_sector * BYTES_PER_SECTOR, SEEK_SET);
    int result = fread(buffer, BYTES_PER_SECTOR, sectors_to_read, pdisk->fp);
    return result;
}
int disk_close(struct disk_t* pdisk)
{
    if(!pdisk)
    {
        errno = EFAULT;
        return -1;
    }

    fclose(pdisk->fp);
    free(pdisk);

    return 0;
}


struct volume_t* fat_open(struct disk_t* pdisk, uint32_t first_sector)
{
    if(!pdisk)
    {
        errno = EFAULT;
        return NULL;
    }
    struct volume_t* my_volume = (struct volume_t*)malloc(sizeof(struct volume_t));
    if(!my_volume)
    {
        errno = ENOMEM;
        return NULL;
    }
    if(disk_read(pdisk, 0, &my_volume->boot, 1) == -1)
    {
        return NULL;
    }

    if((my_volume->boot.loader_signature[0] != 0x55 &&
        my_volume->boot.loader_signature[1] != 0xAA   )

       ||
       my_volume->boot.fat_count != 2)
    {
        errno = EINVAL;
        free(my_volume);
        return NULL;
    }

    my_volume->disk = pdisk;
    my_volume->fat1_position = first_sector + my_volume->boot.reserved_sectors; // TO-DO volume_start->first_sector
    my_volume->fat2_position = first_sector + my_volume->boot.reserved_sectors + my_volume->boot.sectors_per_fat;

    // my_volume->rootdir_position = VOLUME_START + my_volume->boot.fat_count + my_volume->boot.sectors_per_fat + my_volume->boot.reserved_sectors;
    my_volume->rootdir_position = first_sector + my_volume->fat2_position + my_volume->boot.sectors_per_fat;
    my_volume->rootdir_size = (my_volume->boot.root_dir_capacity * sizeof(struct fat_dir_entry_t)) / (int)my_volume->boot.bytes_per_sector;

    if(((my_volume->boot.root_dir_capacity * sizeof(struct fat_dir_entry_t)) % (int)my_volume->boot.bytes_per_sector) != 0)
         my_volume->rootdir_size += 1;
    my_volume->cluster2_position = my_volume->rootdir_position + my_volume->rootdir_size;
    my_volume->volume_size = my_volume->boot.logical_sectors16 == 0 ? my_volume->boot.logical_sectors32 : my_volume->boot.logical_sectors16;
    
    my_volume->bytes_per_fat = my_volume->boot.sectors_per_fat * my_volume->boot.bytes_per_sector;
    
    my_volume->pfat1 = (uint8_t*)malloc(my_volume->bytes_per_fat); 
    my_volume->pfat2 = (uint8_t*)malloc(my_volume->bytes_per_fat);
    if(my_volume->pfat1 == NULL || my_volume->pfat2 == NULL)
    {
        free(my_volume->pfat1);
        free(my_volume->pfat2);
        errno = ENOMEM;
        return NULL;
    } 

    // int fat1_read = disk_read(my_volume->pfat1, my_volume->boot.sectors_per_fat, my_volume->fat1_position);
    // int fat2_read = disk_read(my_volume->pfat2, my_volume->boot.sectors_per_fat, my_volume->fat2_position);
    int fat1_read = disk_read(my_volume->disk, my_volume->fat1_position, my_volume->pfat1, my_volume->boot.sectors_per_fat);
    int fat2_read = disk_read(my_volume->disk, my_volume->fat2_position, my_volume->pfat2, my_volume->boot.sectors_per_fat);
    if(fat1_read != my_volume->boot.sectors_per_fat || fat2_read != my_volume->boot.sectors_per_fat)
    {
        free(my_volume->pfat1);
        free(my_volume->pfat2);
        return NULL;
    }
    
    if(memcmp(my_volume->pfat1, my_volume->pfat2, my_volume->bytes_per_fat) != 0)
    {
        printf("Error in FAT data\n");
        free(my_volume->pfat1);
        free(my_volume->pfat2);
        errno = ENOMEM;
        return NULL;
    }
    return my_volume;
}
int fat_close(struct volume_t* pvolume)
{
    if(!pvolume)
    {
        errno = EFAULT;
        return -1;
    }
    free(pvolume->pfat1);
    free(pvolume->pfat2);
    free(pvolume);
    return 0;
}


struct file_t* file_open(struct volume_t* pvolume, const char* file_name)
{
    if(!pvolume)
    {
        errno = EFAULT;
        return NULL;
    }
    if(!file_name)
    {
        errno = ENOENT;
        return NULL;
    }
    struct file_t* result = (struct file_t*)malloc(sizeof(struct file_t));
    if(!result)
    {
        errno = ENOMEM;
        return NULL;
    }
    result->entry =(struct fat_dir_entry_t*)malloc(sizeof(struct fat_dir_entry_t));
    if(!result->entry)
    {
        free(result);
        errno = ENOMEM;
        return NULL;
    }
    result->volume = pvolume;
    result->offset = 0;
    char entry_filename[13];
    fseek(pvolume->disk->fp, pvolume->rootdir_position * BYTES_PER_SECTOR, SEEK_SET);
    fread(result->entry, sizeof(struct fat_dir_entry_t),1,pvolume->disk->fp);
    while(result->entry->filename[0] != 0x00) // && result->entry->filename[0] != 0xE5)
    {
        /*if(result->entry->filename[0] < 'A' || result->entry->filename[0] > 'Z')
        {
            fread(result->entry, sizeof(struct fat_dir_entry_t),1,pvolume->disk->fp);
            continue;
        }*/
        int i = 0;
        while(result->entry->filename[i] >= 'A' && result->entry->filename[i] <= 'Z' && i < 8)
        {
            entry_filename[i] = result->entry->filename[i];
            i++;
        }
        if(result->entry->ext[0] >= 'A' && result->entry->ext[0] <= 'Z')
        {
            entry_filename[i]   = '.';
            entry_filename[i+1] = result->entry->ext[0];
            if(result->entry->ext[1] >= 'A' && result->entry->ext[1] <= 'Z')
                entry_filename[i+2] = result->entry->ext[1];
            else
                entry_filename[i+2] = '\0';
            if(result->entry->ext[2] >= 'A' && result->entry->ext[2] <= 'Z')
                entry_filename[i+3] = result->entry->ext[2];
            else
                entry_filename[i+3] = '\0';
            entry_filename[i+4] = '\0';
        }
        else
        {
            entry_filename[i] = '\0';
        }
        if(!strcmp(entry_filename, file_name))
        {
            if(result->entry->file_attributes & 0x08 || result->entry->file_attributes & 0x10)
            {
                errno = EISDIR;
                free(result->entry);
                free(result);
                return NULL;
            }
            result->cluster_chain = get_chain_fat12(pvolume->pfat1, pvolume->boot.sectors_per_fat * BYTES_PER_SECTOR, result->entry->word_low, result->entry->size / BYTES_PER_SECTOR + !!(result->entry->size % BYTES_PER_SECTOR));
            return result;
        }
        /*int flag = 0;
        for(int j = 0; ;j++)
        {
            if(!entry_filename[j]) break;
            if(entry_filename[j] != file_name[j])
            {
                flag = 1;
                break;
            }
        }
        if(!flag) return result;*/
        fread(result->entry, sizeof(struct fat_dir_entry_t),1,pvolume->disk->fp);
    }

    free(result->entry);
    free(result);
    errno = ENOENT;
    return NULL;
}
int file_close(struct file_t* stream)
{
    if(!stream)
    {
        errno = EFAULT;
        return -1;
    }
    free(stream->cluster_chain->clusters);
    free(stream->cluster_chain);
    free(stream->entry);
    free(stream);
    return 0;
}
size_t file_read(void *ptr, size_t size, size_t nmemb, struct file_t *stream)
{
    // do struktury dodać OFFSET dokąd jest plik przeczytany
    // tia, potem OFFSET w file_seek się przyda
    if(!ptr || !stream)
    {
        errno = EFAULT;
        return -1;
    }
    if(stream->offset >= stream->entry->size)
    {
        return 0;
    }
    // char temp[BYTES_PER_SECTOR * stream->volume->boot.sectors_per_cluster + 1];
    char* temp = (char*)malloc(BYTES_PER_SECTOR * stream->volume->boot.sectors_per_cluster + 2); //TO-DO: 2?
    int i = 0;
    size_t size_total = size * nmemb > stream->entry->size ? stream->entry->size : size * nmemb ;
    size_t result = 0;
    size_t size_now = 0;
    /*for(int i = 0; i < (int)cluster_chain->size; i++)
    {
        printf("%d ", (int)cluster_chain->clusters[i]);
    }
    printf("\n");*/
    int offset_now = 0;
    if(stream->offset != 0)
    {
        i = stream->offset / (BYTES_PER_SECTOR * stream->volume->boot.sectors_per_cluster);
        offset_now = stream->offset % (BYTES_PER_SECTOR * stream->volume->boot.sectors_per_cluster);
    }
    for( ; i < (int)stream->cluster_chain->size && result < size * nmemb; i++)
    {
        /*if(i == 0 && stream->offset != 0)
        {
            fseek(stream->volume->disk->fp, (stream->volume->cluster2_position + cluster_chain->clusters[stream->offset / BYTES_PER_SECTOR + !!(stream->offset % BYTES_PER_SECTOR)] + 3) * BYTES_PER_SECTOR + (stream->offset % BYTES_PER_SECTOR), SEEK_SET);
            size_now = BYTES_PER_SECTOR - stream->offset % BYTES_PER_SECTOR;
        }

        else
        {
            fseek(stream->volume->disk->fp, (cluster_chain->clusters[i] + stream->volume->cluster2_position + 3) * BYTES_PER_SECTOR, SEEK_SET);
            size_now = (size_total >= BYTES_PER_SECTOR) ? BYTES_PER_SECTOR : size_total;
        }*/
        //fseek(stream->volume->disk->fp, (stream->cluster_chain->clusters[i] + stream->volume->cluster2_position - 2) * stream->volume->boot.sectors_per_cluster * BYTES_PER_SECTOR + offset_now, SEEK_SET);
        fseek(stream->volume->disk->fp, ((stream->cluster_chain->clusters[i] - 2) * stream->volume->boot.sectors_per_cluster + stream->volume->cluster2_position) * BYTES_PER_SECTOR + offset_now, SEEK_SET);
        // size_now = (size_total >= BYTES_PER_SECTOR) ? BYTES_PER_SECTOR : size_total;
        size_now = (size_total + offset_now >= BYTES_PER_SECTOR * stream->volume->boot.sectors_per_cluster) ? size_total - (size_total + offset_now - BYTES_PER_SECTOR * stream->volume->boot.sectors_per_cluster ): size_total;
        if(stream->offset + size_now > stream->entry->size)
        {
            size_now = size_now - (stream->offset + size_now - stream->entry->size);
        }
        fread(temp, size_now, 1, stream->volume->disk->fp);
        temp[size_now + 1] = '\0';
        strncpy(((char*)ptr + result), temp, size_now);
        result += size_now;
        stream->offset += size_now;
        offset_now = 0;
        size_total -= size_now;
    }

    // fseek(stream->volume->disk->fp, stream->entry->word_low, SEEK_SET);
    // stream->volume->disk->fp;
    free(temp);
    if(size != 1)
    {
        return result / size;
    }
    return result;
}
struct clusters_chain_t *get_chain_fat12(const void * const buffer, size_t size, uint16_t first_cluster, size_t max_clusters)
{
    // dodać MAXa - rozmiar pliku/BYTES_PER_SECTOR;
    if(!buffer || size <= 0 || first_cluster <= 0)
    {
        return NULL;
    }
    struct clusters_chain_t* my_clusters = (struct clusters_chain_t*)calloc(sizeof(struct clusters_chain_t), 1);
    my_clusters->size = 0;

    uint8_t* my_buffer = (uint8_t*) buffer;
    uint16_t index = first_cluster;
    uint16_t value = 0;


    while(index != 0 && my_clusters->size < max_clusters)
    {
        value = 0;

        my_clusters->clusters = (uint16_t*)realloc(my_clusters->clusters, sizeof(uint16_t) * (my_clusters->size + 1));
        my_clusters->clusters[my_clusters->size] = (uint16_t)index;
        my_clusters->size++;

        int mod = index % 2;
        index += index / 2;
        uint8_t l_cluster = my_buffer[index];
        uint8_t r_cluster = my_buffer[index + 1];

        if (mod)
        {
            value += r_cluster << 4;
            value += l_cluster >> 4;
        }
        else
        {
            r_cluster <<= 4;
            value += r_cluster;
            value <<= 4;
            value += l_cluster;
        }


        if(value <= 0 || value > size) break;
        index = value;
    }

    return my_clusters;
}
int32_t file_seek(struct file_t* stream, int32_t offset, int whence)
{
    if(!stream)
    {
        errno = EFAULT;
        return -1;
    }
    switch(whence)
    {
        case SEEK_SET:
            if(offset < 0 || (uint32_t)offset > stream->entry->size)
            {
                errno = ENXIO;
                return -1;
            }
            stream->offset = offset;
            return offset;
            break;
        case SEEK_CUR:
            if((int32_t)stream->offset + offset < 0 || (uint32_t)offset + stream->offset > stream->entry->size)
            {
                errno = ENXIO;
                return -1;
            }
            stream->offset += offset;
            return stream->offset;
            break;
        case SEEK_END:
            if((int32_t)stream->entry->size + offset < 0 || stream->entry->size + offset > stream->entry->size)
            {
                errno = ENXIO;
                return -1;
            }
            stream->offset = stream->entry->size + offset;
            return stream->offset;
            break;
        default:
            errno = EINVAL;
            return -1;

    }
    return -1;
}


struct dir_t* dir_open(struct volume_t* pvolume, const char* dir_path)
{
    if(!pvolume)
    {
        errno = EFAULT;
        return NULL;
    }
    if(*dir_path != '\\')
    {
        errno = ENOENT;
        return NULL;
    }
    struct dir_t * my_dir = (struct dir_t*)malloc(sizeof(struct dir_t));
    if(!my_dir)
    {
        errno = ENOMEM;
        return NULL;
    }
    my_dir->volume = pvolume;
    my_dir->location = pvolume->rootdir_position;
    my_dir->offset = 0;
    return my_dir;
}
int dir_read(struct dir_t* pdir, struct dir_entry_t* pentry)
{
    if(!pdir || !pentry)
    {
        errno = EFAULT;
        return -1;
    }
    struct fat_dir_entry_t entry;
    fseek(pdir->volume->disk->fp, pdir->location * BYTES_PER_SECTOR + pdir->offset, SEEK_SET);
    //Etykiety woluminu, usunięte pliki, terminatory
    //fread(pentry, sizeof(struct dir_entry_t), 1, pdir->volume->disk->fp);
    do
    {
        fread(&entry, sizeof(struct fat_dir_entry_t), 1, pdir->volume->disk->fp);
        pdir->offset += sizeof(struct fat_dir_entry_t);
    }
    while(entry.filename[0] == 0xE5 || entry.file_attributes & 0x08);
    //Etykieta Voluminu-> dalej
    //Usunięty -> dalej
    //Pusto -> wystarczy
    //Pełno -> wystarczy
    if(entry.filename[0] < 'A' || entry.filename[0] > 'Z')
    {
        //errno = ENXIO;
        return 1;
    }


    int i = 0;
    while(entry.filename[i] >= 'A' && entry.filename[i] <= 'Z' && i < 8)
    {
        pentry->name[i] = entry.filename[i];
        i++;
    }
    if(entry.ext[0] >= 'A' && entry.ext[0] <= 'Z')
    {
        pentry->name[i]   = '.';
        pentry->name[i+1] = entry.ext[0];
        if(entry.ext[1] >= 'A' && entry.ext[1] <= 'Z')
            pentry->name[i+2] = entry.ext[1];
        else
            pentry->name[i+2] = '\0';
        if(entry.ext[2] >= 'A' && entry.ext[2] <= 'Z')
            pentry->name[i+3] = entry.ext[2];
        else
            pentry->name[i+3] = '\0';
        pentry->name[i+4] = '\0';
    }
    else
    {
        pentry->name[i] = '\0';
    }
    pentry->size = entry.size;
    pentry->is_readonly  = entry.file_attributes & 0x01;
    pentry->is_hidden    = entry.file_attributes & 0x02;
    pentry->is_system    = entry.file_attributes & 0x04;
    pentry->is_directory = entry.file_attributes & 0x10;
    pentry->is_archived  = entry.file_attributes & 0x20;
    return 0;
}
int dir_close(struct dir_t* pdir)
{
    if(!pdir)
    {
        errno = EFAULT;
        return -1;
    }
    free(pdir);
    return 0;
}



