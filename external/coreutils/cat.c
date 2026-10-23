#include "stdio.h"
#include "abi.h"
#include "resman.h"
#include "fs.h"
#include "crt.h"
#include "path.h"

/* Stream the file so its size is not limited by an application buffer. */
static int cat_file(PCSTR path) {
    FS_VOLUME volume={0};
    FS_FILE file={0};
    BYTE buffer[512];
    int rc=fs_mount(&volume,lw_blk_get(0));
    if (rc)return rc;
    rc=fs_file_open(&volume,path,&file);
    if (rc)return rc;
    for (;;) {
        DWORD count=0;
        rc=fs_file_read(&file,buffer,sizeof(buffer),&count);
        /* A failed read can still have returned a valid prefix. */
        for (DWORD i=0;i<count;i++) {
            if (putchar(buffer[i])==EOF) {rc=EOF;goto done;}
        }
        if (rc || !count)break;
    }
done:
    fs_file_close(&file);
    return rc;
}

int main(int argc,char **argv) {
    if (argc!=2 || !argv[1] || !*argv[1]) {
        printf("Usage: cat FILE\n");
        return RM_ERR_ARGUMENT;
    }
    char path[256];
    int rc=lw_path_resolve(lw_env_cwd(lw_env),argv[1],path,sizeof(path));
    if (!rc)rc=cat_file(path);
    if (rc)printf("cat: %s: error %d\n",argv[1],rc);
    return rc;
}
