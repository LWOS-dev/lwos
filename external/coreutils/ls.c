#include "stdio.h"
#include "string.h"
#include "abi.h"
#include "resman.h"
#include "fs.h"
#include "crt.h"
#include "path.h"

/* Same FAT traversal as TUI proc_fe_update_list, streamed to stdout so ls
 * is not limited to the explorer's 256-entry display cache. */
static int dir_list(int long_format) {
    FS_VOLUME volume={0};
    SFT_T directory;
    int rc=fs_mount(&volume,lw_blk_get(0));
    if (rc)return rc;
    char path[256];
    rc=lw_path_resolve("/",lw_env_cwd(lw_env),path,sizeof(path));
    if (rc)return rc;
    rc=fs_lookup(&volume,path,&directory);
    if (rc)return rc;
    if (!(directory.attr & 0x10))return FS_ERR_ARGUMENT;

    DWORD current=directory.cluster;
    for (DWORD hops=0;hops<volume.cluster_count;hops++) {
        int entries=volume.sectors_per_cluster*16;
        for (int i=0;i<entries;i++) {
            SFT_T entry;
            rc=fat_read_fte(&volume,current,i,&entry);
            if (rc==FS_ENTRY_END)return 0;
            if (rc<0)return rc;
            if (rc!=FS_ENTRY_VALID)continue;

            /* FAT stores space-padded name/ext fields, without a dot. */
            char name[13];
            int base=8, ext=3, n=0;
            while (base && entry.name[base-1]==' ')base--;
            while (ext && entry.name[8+ext-1]==' ')ext--;
            for (int j=0;j<base;j++)name[n++]=entry.name[j];
            if (ext) {
                name[n++]='.';
                for (int j=0;j<ext;j++)name[n++]=entry.name[8+j];
            }
            name[n]=0;
            int written=long_format
                ? printf("%-12s %s\n",name,(entry.attr&0x10)?"<DIR>":"")
                : printf("%s\n",name);
            if (written<0)
                return EOF;
        }
        rc=next_cluster(&volume,current,&current);
        if (rc<=0)return rc;
    }
    return FS_ERR_FORMAT; /* Bound corrupt/cyclic chains like the TUI. */
}
/* List the inherited working directory; direct launches default to root. */
int main(int argc,char **argv) {
    int long_format=0;
    for (int i=1;i<argc;i++) {
        PCSTR arg=argv[i];
        if (!strcmp(arg,"--")) {
            if (i+1<argc)return RM_ERR_ARGUMENT;
            break;
        }
        if (*arg++!='-' || !*arg)return RM_ERR_ARGUMENT;
        while (*arg) {
            switch (*arg++) {
                case 'l':long_format=1;break;
                default:return RM_ERR_ARGUMENT;
            }
        }
    }
    return dir_list(long_format);
}
