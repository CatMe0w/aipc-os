#include <stdint.h>
#include "fat.h"
#include "sd.h"

static uint8_t  io_buf[512]   __attribute__((aligned(4)));  /* MBR / BPB / dir / data */
static uint8_t  fat_buf[512]  __attribute__((aligned(4)));  /* cached FAT sector */
static uint32_t fat_buf_lba = 0xFFFFFFFFu;

#define MAX_SECTORS_PER_CLUSTER 128u
#define MAX_FAT32_ROOT_SECTORS  4096u

static struct {
    uint32_t bytes_per_sector;
    uint32_t sectors_per_cluster;
    uint32_t fat_start_lba;
    uint32_t root_start_lba;   /* FAT16 only: absolute LBA of root dir */
    uint32_t root_dir_sectors; /* FAT16 only */
    uint32_t data_start_lba;   /* LBA of cluster 2 */
    uint32_t root_cluster;     /* FAT32 only */
    int      is_fat32;
} v;

static uint16_t rd16(const uint8_t *p)
{
    return (uint16_t)p[0] | ((uint16_t)p[1] << 8);
}

static uint32_t rd32(const uint8_t *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8)
         | ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int is_power_of_two(uint32_t v)
{
    return v != 0 && (v & (v - 1u)) == 0;
}

static int read_sector(uint32_t lba, void *dst)
{
    return sd_read_block(lba, dst);
}

static uint32_t fat_entry(uint32_t cluster)
{
    uint32_t byte_off = v.is_fat32 ? (cluster * 4u) : (cluster * 2u);
    uint32_t lba = v.fat_start_lba + byte_off / 512u;
    uint32_t off = byte_off & 0x1FFu;
    if (lba != fat_buf_lba) {
        if (read_sector(lba, fat_buf) < 0)
            return v.is_fat32 ? 0x0FFFFFFFu : 0xFFFFu;
        fat_buf_lba = lba;
    }
    if (v.is_fat32)
        return rd32(fat_buf + off) & 0x0FFFFFFFu;
    return rd16(fat_buf + off);
}

static int name_match(const uint8_t *ent, const char *name11)
{
    for (int i = 0; i < 11; i++) {
        if (ent[i] != (uint8_t)name11[i])
            return 0;
    }
    return 1;
}

static int scan_dir_sector(const uint8_t *sec, const char *name11,
                           uint32_t *first_cluster, uint32_t *file_size)
{
    for (uint32_t e = 0; e < 512u; e += 32u) {
        const uint8_t *ent = sec + e;
        if (ent[0] == 0x00u)
            return -1;
        if (ent[0] == 0xE5u)
            continue;
        if (ent[11] == 0x0Fu)
            continue;  /* LFN slot */
        if (ent[11] & 0x08u)
            continue;  /* volume label */
        if (!name_match(ent, name11))
            continue;
        uint32_t lo = rd16(ent + 26);
        uint32_t hi = v.is_fat32 ? rd16(ent + 20) : 0u;
        *first_cluster = (hi << 16) | lo;
        *file_size = rd32(ent + 28);
        return 1;
    }
    return 0;
}

static int find_in_root(const char *name11, uint32_t *first_cluster,
                        uint32_t *file_size)
{
    if (v.is_fat32) {
        uint32_t cluster = v.root_cluster;
        uint32_t scanned = 0;
        while (cluster < 0x0FFFFFF8u && cluster >= 2u &&
               scanned < MAX_FAT32_ROOT_SECTORS) {
            uint32_t lba = v.data_start_lba + (cluster - 2u) * v.sectors_per_cluster;
            for (uint32_t s = 0; s < v.sectors_per_cluster; s++) {
                if (scanned++ >= MAX_FAT32_ROOT_SECTORS)
                    return -7;
                if (read_sector(lba + s, io_buf) < 0)
                    return -6;
                int r = scan_dir_sector(io_buf, name11, first_cluster, file_size);
                if (r == 1)
                    return 0;
                if (r == -1)
                    return -7;
            }
            cluster = fat_entry(cluster);
        }
        return -7;
    }
    for (uint32_t s = 0; s < v.root_dir_sectors; s++) {
        if (read_sector(v.root_start_lba + s, io_buf) < 0)
            return -6;
        int r = scan_dir_sector(io_buf, name11, first_cluster, file_size);
        if (r == 1)
            return 0;
        if (r == -1)
            return -7;
    }
    return -7;
}

int fat_load_file(const char *name11, void *dst, uint32_t max_bytes,
                  uint32_t *out_size)
{
    fat_buf_lba = 0xFFFFFFFFu;

    if (read_sector(0, io_buf) < 0)
        return -1;
    if (io_buf[0x1FE] != 0x55u || io_buf[0x1FF] != 0xAAu)
        return -2;

    uint32_t part_lba = 0;
    for (int i = 0; i < 4; i++) {
        const uint8_t *e = io_buf + 0x1BE + 16 * i;
        uint8_t type = e[4];
        if (type == 0x04u || type == 0x06u || type == 0x0Bu ||
            type == 0x0Cu || type == 0x0Eu) {
            part_lba = rd32(e + 8);
            break;
        }
    }
    if (part_lba == 0)
        return -3;

    if (read_sector(part_lba, io_buf) < 0)
        return -4;

    v.bytes_per_sector    = rd16(io_buf + 0x0B);
    v.sectors_per_cluster = io_buf[0x0D];
    uint32_t reserved     = rd16(io_buf + 0x0E);
    uint32_t num_fats     = io_buf[0x10];
    uint32_t root_entries = rd16(io_buf + 0x11);
    uint32_t spf16        = rd16(io_buf + 0x16);

    if (v.bytes_per_sector != 512u)
        return -5;
    if (!is_power_of_two(v.sectors_per_cluster) ||
        v.sectors_per_cluster > MAX_SECTORS_PER_CLUSTER)
        return -5;
    if (reserved == 0 || num_fats == 0 || num_fats > 4)
        return -5;

    uint32_t sectors_per_fat;
    if (spf16 == 0) {
        v.is_fat32 = 1;
        sectors_per_fat = rd32(io_buf + 0x24);
        v.root_cluster = rd32(io_buf + 0x2C);
        if (sectors_per_fat == 0 || v.root_cluster < 2u)
            return -5;
    } else {
        v.is_fat32 = 0;
        sectors_per_fat = spf16;
        if (root_entries == 0)
            return -5;
    }

    v.fat_start_lba = part_lba + reserved;

    if (v.is_fat32) {
        v.data_start_lba = v.fat_start_lba + num_fats * sectors_per_fat;
    } else {
        v.root_start_lba   = v.fat_start_lba + num_fats * sectors_per_fat;
        v.root_dir_sectors = (root_entries * 32u + 511u) / 512u;
        v.data_start_lba   = v.root_start_lba + v.root_dir_sectors;
    }

    uint32_t first_cluster = 0, file_size = 0;
    int frc = find_in_root(name11, &first_cluster, &file_size);
    if (frc != 0)
        return frc;
    if (file_size == 0 || first_cluster < 2u)
        return -9;
    if (file_size > max_bytes)
        return -9;

    uint32_t eoc = v.is_fat32 ? 0x0FFFFFF8u : 0xFFF8u;
    uint32_t cluster = first_cluster;
    uint32_t loaded = 0;
    uint8_t *out = (uint8_t *)dst;

    while (cluster < eoc && cluster >= 2u && loaded < file_size) {
        uint32_t lba = v.data_start_lba + (cluster - 2u) * v.sectors_per_cluster;
        for (uint32_t s = 0; s < v.sectors_per_cluster && loaded < file_size; s++) {
            uint32_t remain = file_size - loaded;
            if (remain >= 512u) {
                if (read_sector(lba + s, out + loaded) < 0)
                    return -8;
                loaded += 512u;
            } else {
                if (read_sector(lba + s, io_buf) < 0)
                    return -8;
                for (uint32_t k = 0; k < remain; k++)
                    out[loaded + k] = io_buf[k];
                loaded += remain;
            }
        }
        cluster = fat_entry(cluster);
    }

    if (loaded != file_size)
        return -10;

    if (out_size)
        *out_size = file_size;
    return 0;
}
