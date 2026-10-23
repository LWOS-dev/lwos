#include "stdio.h"
#include "abi.h"
#include "crt.h"
#include "fs.h"
#include "path.h"

static int remove_path(PFS_VOLUME volume,PCSTR name) {
    char path[256],cwd[256];
    if (!*name)return FS_ERR_ARGUMENT;
    int rc=lw_path_resolve(lw_env_cwd(lw_env),name,path,sizeof(path));
    if (rc)return rc;
    rc=lw_path_resolve("/",lw_env_cwd(lw_env),cwd,sizeof(cwd));
    if (rc)return rc;
    /* Do not delete the root, current directory or its parents. FAT ignores case. */
    unsigned i=0;
    while (path[i] && cwd[i]) {
        char a=path[i],b=cwd[i];
        if (a>='a' && a<='z')a-='a'-'A';
        if (b>='a' && b<='z')b-='a'-'A';
        if (a!=b)break;
        i++;
    }
    if (!path[1] || (!path[i] && (!cwd[i] || cwd[i]=='/')))
        return FS_ERR_ARGUMENT;
    return fs_remove(volume,path);
}

int main(int argc,char **argv) {
    int first=1;
    if (argc>1 && argv[1][0]=='-' && argv[1][1]=='-' && !argv[1][2])first++;
    if (first>=argc) {
        printf("Usage: rm [--] PATH...\n");
        return RM_ERR_ARGUMENT;
    }
    for (int i=first;i<argc;i++) {
        if (first==1 && argv[i][0]=='-') {
            printf("rm: unsupported option %s\n",argv[i]);
            return RM_ERR_ARGUMENT;
        }
    }
    FS_VOLUME volume={0};
    int rc=fs_mount(&volume,lw_blk_get(0));
    if (rc) {printf("rm: error %d\n",rc);return rc;}
    int result=0;
    for (int i=first;i<argc;i++) {
        rc=remove_path(&volume,argv[i]);
        if (rc) {
            printf("rm: %s: error %d\n",argv[i],rc);
            if (!result)result=rc;
        }
    }
    return result;
}
