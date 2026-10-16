/* Actual TUI event handlers + FAT implementation, backed by an in-memory disk. */
#include <assert.h>
#include <stdio.h>
#include "../app/monitor/src/tui.c"
FS_VOLUME monitor_fs;
static BYTE disk[256][512];
static int fail_read, fail_write, fail_flush, writes;
static WORD screen[80*25];
static int rd(PBLKDEV d,QWORD lba,DWORD n,PVOID out) {
    (void)d;
    if (fail_read || lba>=256 || n!=1)return -1;
    memcpy(out,disk[lba],512); return 0;
}
static int wr(PBLKDEV d,QWORD lba,DWORD n,PCVOID in) {
    (void)d;
    writes++;
    if (fail_write || lba>=256 || n!=1)return -1;
    memcpy(disk[lba],in,512); return 0;
}
static int flush(PBLKDEV d) { (void)d; return fail_flush ? -1 : 0; }
static BLKDEV dev={.sector_size=512,.sectors=256,.read=rd,.write=wr,.flush=flush};
static void put32(PBYTE p,DWORD v) {
    for(int i=0;i<4;i++)p[i]=(BYTE)(v>>(8*i));
}
static void fixture(void) {
    fs_file_close(hex_file);
    memzero(disk,sizeof(disk));
    memzero(&monitor_fs,sizeof(monitor_fs));
    monitor_fs.device=&dev; monitor_fs.mounted=1;
    monitor_fs.sectors_per_cluster=1; monitor_fs.cluster_count=100;
    monitor_fs.fat_lba=monitor_fs.fat_base_lba=1;
    monitor_fs.fat_sectors=1; monitor_fs.fat_count=1;
    monitor_fs.data_lba=2; monitor_fs.root_cluster=5;
    put32(disk[1]+5*4,0x0fffffff);
    fail_read=fail_write=fail_flush=writes=0;
    hex_active=hex_dirty=hex_confirm=hex_digits=hex_failed=0;
    video=screen;
    strcpy(fe_path,"/"); fe_current_cluster=5;
    area_file_expl=(TUI_AREA){.x=52,.y=1,.w=27,.h=21,.index=3};
    area_file_info=(TUI_AREA){.x=40,.y=1,.w=10,.h=9,.index=-1u};
}
static void seed(void) {
    FS_FILE f={0}; DWORD n;
    assert(fs_touch(&monitor_fs,"/TEST.BIN")==0);
    assert(fs_file_open(&monitor_fs,"/TEST.BIN",&f)==0);
    assert(fs_file_write(&f,"ABC",3,&n)==0 && n==3);
    fs_file_close(&f);
    proc_fe_update_list();
    assert(filelist_length==1 && !fe_error);
    focus=3;
    proc_fe(&area_file_expl,VK_F6);
    assert(hex_active && hex_length==3 && hex_buffer[0]=='A');
}
static void byte_input(char a,char b) {
    proc_hex(a); proc_hex(b); proc_hex(VK_ENTER);
}
int main(void) {
    fixture(); proc_fe_update_list();
    assert(filelist_length==0 && !fe_error);
    focus=3; proc_fe(&area_file_expl,VK_F2); proc_fe(&area_file_expl,VK_F6);
    assert(!hex_active);
    monitor_fs.mounted=0; proc_fe_update_list();
    assert(fe_error==FS_ERR_NOT_MOUNTED && !filelist_length);
    monitor_fs.mounted=1;
    seed();
    proc_hex(VK_F4); assert(!hex_digits); /* Extended key must not become hex. */
    byte_input('4','6'); assert(hex_buffer[0]=='F' && hex_dirty);
    proc_hex('a'); proc_hex(VK_RIGHT); assert(!hex_digits && hex_buffer[1]=='B');
    proc_hex(VK_F2); assert(!hex_dirty && !hex_error && hex_written==3);
    proc_hex(VK_F1); assert(hex_buffer[0]=='F');
    proc_hex(VK_PGUP); assert(hex_cursor==0);
    proc_hex(VK_END); assert(hex_cursor==3);
    proc_hex(VK_RIGHT); byte_input('7','f');
    assert(hex_length==5 && hex_buffer[3]==0 && hex_buffer[4]==0x7f);
    proc_hex(VK_F2); assert(!hex_dirty);
    proc_hex(VK_F1); assert(hex_length==5 && hex_buffer[4]==0x7f);
    byte_input('1','2');
    fail_read=1; proc_hex(VK_F1); assert(hex_confirm==1);
    proc_hex('y'); assert(hex_error && hex_dirty && hex_buffer[0]==0x12);
    fail_read=0;
    proc_hex(VK_ESC); assert(hex_confirm==2);
    proc_hex('n'); assert(hex_active && hex_dirty);
    proc_hex(VK_ESC); proc_hex('y'); assert(!hex_active);
    proc_fe(&area_file_expl,VK_F6); assert(hex_buffer[0]=='F');
    byte_input('3','4'); fail_flush=1;
    proc_hex(VK_F2); assert(hex_failed && hex_dirty && hex_error);
    int before=writes; proc_hex(VK_F2); assert(writes==before);
    fail_flush=0;
    proc_hex(VK_F1); proc_hex('y'); assert(!hex_failed && !hex_dirty);
    byte_input('5','6'); fail_write=1;
    proc_hex(VK_F2); assert(hex_failed && hex_dirty && hex_error);
    fail_write=0;
    proc_hex(VK_F1); proc_hex('y'); assert(!hex_failed);
    hex_file->attr|=1;
    before=writes; proc_hex('a'); proc_hex(VK_F2);
    assert(!hex_digits && writes==before);
    hex_file->attr&=~1;
    hex_cursor=HEX_CAPACITY-1; proc_hex(VK_RIGHT); proc_hex(VK_PGDN);
    assert(hex_cursor==HEX_CAPACITY-1);
    byte_input('f','e'); assert(hex_length==HEX_CAPACITY && hex_buffer[HEX_CAPACITY-1]==0xfe);
    proc_hex(VK_ESC); proc_hex('y');
    /* Oversized directory metadata must be rejected before a buffer read. */
    put32(disk[5]+28,HEX_CAPACITY+1);
    proc_fe_update_list(); proc_fe(&area_file_expl,VK_F6);
    assert(!hex_active && fe_error==FS_ERR_RANGE);
    fixture();
    assert(fs_touch(&monitor_fs,"/EMPTY.BIN")==0);
    proc_fe_update_list(); proc_fe(&area_file_expl,VK_F6);
    assert(hex_active && hex_length==0);
    byte_input('a','b'); proc_hex(VK_F2); proc_hex(VK_F1);
    assert(hex_length==1 && hex_buffer[0]==0xab);
    hex_close();
    fixture();
    assert(fs_touch(&monitor_fs,"/DIR/SUB/ITEM.BIN")==0);
    proc_fe_update_list(); focus=3;
    proc_fe(&area_file_expl,VK_F2); assert(!strcmp(fe_path,"/DIR"));
    /* Dotdot returns to actual root 5, not a hard-coded cluster 2. */
    for (fe_index=0;fe_index<filelist_length;fe_index++)
        if (!strncmp(filelist_ptr[fe_index].name,"..         ",11))break;
    assert(fe_index<filelist_length);
    proc_fe(&area_file_expl,VK_F2);
    assert(fe_current_cluster==5 && !strcmp(fe_path,"/"));
    proc_fe(&area_file_expl,VK_F2);
    for (fe_index=0;fe_index<filelist_length;fe_index++)
        if (!strncmp(filelist_ptr[fe_index].name,"SUB        ",11))break;
    assert(fe_index<filelist_length);
    proc_fe(&area_file_expl,VK_F2);
    assert(!strcmp(fe_path,"/DIR/SUB"));
    for (fe_index=0;fe_index<filelist_length;fe_index++)
        if (!strncmp(filelist_ptr[fe_index].name,"ITEM    BIN",11))break;
    assert(fe_index<filelist_length);
    proc_fe(&area_file_expl,VK_F6);
    assert(hex_active && !strcmp(hex_path,"/DIR/SUB/ITEM.BIN"));
    hex_close();
    fail_read=1; proc_fe_update_list(); assert(fe_error && !filelist_length);
    fail_read=0;
    fixture();
    for (int i=0;i<260;i++) {
        char path[20]; snprintf(path,sizeof(path),"/F%03d.BIN",i);
        assert(fs_touch(&monitor_fs,path)==0);
    }
    proc_fe_update_list(); assert(filelist_length==256 && fe_truncated);
    focus=3;
    for(int i=0;i<300;i++)proc_fe(&area_file_expl,VK_DOWN);
    assert(fe_index==255);
    puts("tui_hex_test: PASS");
    return 0;
}
