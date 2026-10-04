#ifndef _LW_MONITOR_FS_H
#define _LW_MONITOR_FS_H

#include "fs.h"

int fs_init(PFS_VOLUME fs);
void fs_scan(PFS_VOLUME fs);

void dir_list(PFS_VOLUME fs, DWORD cluster);

#endif
