#include "stdio.h"
#include "abi.h"
#include "crt.h"
#include "fs.h"
#include "path.h"

static int touch_file(PFS_VOLUME volume,PCSTR name) {
    char path[256],parent[256];
    unsigned length=0;
    while (name[length])length++;
    if (!length || name[length-1]=='/')return FS_PATH_ERROR;
    int rc=lw_path_resolve(lw_env_cwd(lw_env),name,path,sizeof(path));
    if (rc)return rc;
    unsigned slash=0;
    for (unsigned i=0;path[i];i++)if (path[i]=='/')slash=i;
    unsigned n=slash ? slash : 1;
    for (unsigned i=0;i<n;i++)parent[i]=path[i];
    parent[n]=0;
    SFT_T directory;
    rc=fs_lookup(volume,parent,&directory);
    if (rc)return rc;
    if (!(directory.attr&0x10))return FS_PATH_ERROR;
    /* fs_touch can create parents; the command requires an existing parent. */
    return fs_touch(volume,path);
}

int main(int argc,char **argv) {
    int first=1;
    if (argc>1 && argv[1][0]=='-' && argv[1][1]=='-' && !argv[1][2])first++;
    if (first>=argc) {
        printf("Usage: touch [--] FILE...\n");
        return RM_ERR_ARGUMENT;
    }
    for (int i=first;i<argc;i++) {
        if (first==1 && argv[i][0]=='-') {
            printf("touch: unsupported option %s\n",argv[i]);
            return RM_ERR_ARGUMENT;
        }
    }
    FS_VOLUME volume={0};
    int rc=fs_mount(&volume,lw_blk_get(0));
    if (rc) {printf("touch: error %d\n",rc);return rc;}
    int result=0;
    for (int i=first;i<argc;i++) {
        rc=touch_file(&volume,argv[i]);
        if (rc) {
            printf("touch: %s: error %d\n",argv[i],rc);
            if (!result)result=rc;
        }
    }
    return result;
}
