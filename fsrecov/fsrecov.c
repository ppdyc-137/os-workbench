#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <string.h>
#include <stdbool.h>
#include <unistd.h>

#include "fat32.h"

struct fat32ldent {
    u8 LDIR_Ord;
    u8 LDIR_Name1[10];
    u8 LDIR_Attr;
    u8 LDIR_Type;
    u8 LDIR_Chksum;
    u8 LDIR_Name2[12];
    u16 LDIR_FstClusLO;
    u8 LDIR_Name3[4];
};

// for this lab only
static inline void getLFN(char *buf, struct fat32ldent *e) {
    char c, *t = buf;
    for (int i = 0; i < 5; i++) {
        c = e->LDIR_Name1[i*2];
        *t++ = c;
    }
    for (int i = 0; i < 6; i++) {
        c = e->LDIR_Name2[i*2];
        *t++ = c;
    }
    for (int i = 0; i < 2; i++) {
        c = e->LDIR_Name3[i*2];
        *t++ = c;
    }
    *t = '\0';
}

#define ATTR_LONG_NAME (ATTR_READ_ONLY | ATTR_HIDDEN | ATTR_SYSTEM | ATTR_VOLUME_ID)
static inline bool isLegalAttr(u8 attr) {
    switch (attr) {
    case ATTR_ARCHIVE:
    case ATTR_DIRECTORY:
    case ATTR_HIDDEN:
    case ATTR_READ_ONLY:
    case ATTR_SYSTEM:
    case ATTR_VOLUME_ID:
    case ATTR_LONG_NAME:
        return true;
    }
    return false;
}

static inline bool isLegalEntry(struct fat32dent *e) {
    return e->DIR_NTRes == 0 && isLegalAttr(e->DIR_Attr);
}

static inline bool isLastLFNEntry(struct fat32ldent *e) {
    bool condition = true;
    condition = condition && e->LDIR_Attr == ATTR_LONG_NAME;
    condition = condition && e->LDIR_Ord & 0x40;
    condition = condition && e->LDIR_FstClusLO == 0;
    return condition;
}

static inline void getsh1sum(const char *filename, char *buf) {
    char command[64];
    sprintf(command, "sha1sum %s", filename);
    FILE *fp = popen(command, "r");
    if (!fp) {
        perror("popen");
        exit(EXIT_FAILURE);
    }
    fscanf(fp, "%s", buf);
    pclose(fp);
}

#define SectorofCluster(cluster) (hdr->BPB_RsvdSecCnt + hdr->BPB_NumFATs * hdr->BPB_FATSz32 + (cluster - 2) * hdr->BPB_SecPerClus)
#define OffsetofSector(sector)   (file + sector * hdr->BPB_BytsPerSec)
#define OffsetofCluster(cluster) (OffsetofSector(SectorofCluster(cluster)))
#define FstClusofEntry(entry)    ((entry->DIR_FstClusHI << 16) + entry->DIR_FstClusLO)

int main(int argc, char *argv[]) {
    FILE *fp = fopen("fsrecov.img", "r");
    if (fp == NULL) {
        perror("fopen");
        goto error;
    }
    if (fseek(fp, 0, SEEK_END)) {
        perror("fseek");
        goto error;
    }
    long file_size = ftell(fp);
    void *file = mmap(NULL, (size_t)file_size, PROT_READ, MAP_PRIVATE, fileno(fp), 0);
    if (file == MAP_FAILED) {
        perror("mmap failed");
        goto error;
    }

    struct fat32hdr *hdr = file;
    const u32 totalClusters = hdr->BPB_TotSec32 / hdr->BPB_SecPerClus;

    struct fat32dent *rootdir = OffsetofCluster(hdr->BPB_RootClus);
    const u32 rootFirstcluster = FstClusofEntry(rootdir);

    void *data;
    char name[64], buf[16], sha1sum[64];
    // char tmpfile[256];
    const char *tmpfile = "tmp_fsrecov";

    struct fat32ldent *entry, *end;
    for (u32 cluster = rootFirstcluster; cluster <= totalClusters; cluster++) {
        entry = OffsetofCluster(cluster);

        if (!isLegalEntry((struct fat32dent *)entry)) {
            continue;
        }

        end = OffsetofCluster(cluster + 1);
        while (entry <= end) {
            if (!isLastLFNEntry(entry)) {
                entry++;
                continue;
            }
            int len = entry->LDIR_Ord & ~0x40;
            if (entry + len > end) {
                break;
            }

            *name = '\0';
            for (struct fat32ldent* e = entry + len - 1; e >= entry; e--) {
                getLFN(buf, e);
                strcat(name, buf);
            }
            struct fat32dent *e = (struct fat32dent *)entry + len;
            data = OffsetofCluster(FstClusofEntry(e));

            // sprintf(tmpfile, "tmp/%s", name);
            FILE *fp = fopen(tmpfile, "w+");
            if (!fp) {
                perror("fopen");
                return EXIT_FAILURE;
            }
            fwrite(data, 1,  e->DIR_FileSize, fp);
            fclose(fp);

            getsh1sum(tmpfile, sha1sum);

            printf("%s %s\n", sha1sum, name);

            entry += len + 1;
        }
    }

    unlink(tmpfile);

error:
    if(fp) fclose(fp);
    return EXIT_FAILURE;
}

