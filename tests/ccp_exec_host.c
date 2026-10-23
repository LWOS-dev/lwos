/* Disposable text host exercising the real CCP and synchronous CRT children. */
#include "abi.h"
#include "app.h"
#include "task.h"
PVOID *lw_abi_base;
PVOID *rm_base;
volatile DWORD stage,error;
static DWORD id,window;
static const char *prompt_text="[/]$ ";
static RM_TEXT_SNAPSHOT snapshot;
#define CHECK(c) do {if (!(c)) {error=__LINE__;goto done;}} while (0)
static int get(void) {
    unsigned windows=0;
    for (unsigned i=0;i<32;i++) {
        RM_TEXT_SNAPSHOT s;
        if (rm_text_snapshot(i,&s)==RM_OK) {
            windows++;
            if (s.task_id==id)snapshot=s;
        }
    }
    return windows==1 && snapshot.window_id==window;
}
static int wait_text(const char *expected,int prompt) {
    for (unsigned attempt=0;attempt<2000;attempt++) {
        if (get() && snapshot.task_state==TASK_WAITING) {
            int found=0, prompt_ok=!prompt;
            for (unsigned row=0;row<25;row++) {
                int match=1;
                for (unsigned j=0;expected[j] && match;j++)
                    if (j>=80 || (BYTE)snapshot.cells[row*80+j]!=(BYTE)expected[j])match=0;
                if (match)found=1;
            }
            unsigned prompt_length=0;while (prompt_text[prompt_length])prompt_length++;
            if (prompt && snapshot.cursor%80==prompt_length) {
                prompt_ok=1;
                unsigned row=snapshot.cursor/80;
                for (unsigned j=0;j<prompt_length;j++)
                    if ((BYTE)snapshot.cells[row*80+j]!=(BYTE)prompt_text[j])prompt_ok=0;
            }
            if (found && prompt_ok)return 1;
        }
        rm_sleep(1);
    }
    return 0;
}
static int send(int key) {
    LW_WINDOW_EVENT event={LW_WINDOW_KEY,key};
    return rm_window_send(window,&event)==RM_OK;
}
static int wait_tail(const char *text) {
    unsigned n=0;while (text[n])n++;
    for (unsigned attempt=0;attempt<2000;attempt++) {
        if (get() && snapshot.task_state==TASK_WAITING && snapshot.cursor%80==n) {
            unsigned row=snapshot.cursor/80;int match=1;
            for (unsigned j=0;j<n;j++)if ((BYTE)snapshot.cells[row*80+j]!=(BYTE)text[j])match=0;
            if (match)return 1;
        }
        rm_sleep(1);
    }
    return 0;
}
static int command(const char *s) {
    while (*s) {
        if (!send(*s++))return 0;
        rm_sleep(1);
    }
    return send(VK_ENTER);
}
static int wait_listing(int long_format) {
    const char *expected=long_format ? "TESTDIR      <DIR>" : "TESTDIR";
    unsigned length=long_format ? 18 : 7;
    for (unsigned attempt=0;attempt<2000;attempt++) {
        int found=0;
        if (get() && snapshot.task_state==TASK_WAITING && snapshot.cursor%80==5) {
            unsigned prompt_row=snapshot.cursor/80;
            int prompt_ok=1;
            for (unsigned j=0;j<5;j++)
                if ((BYTE)snapshot.cells[prompt_row*80+j]!=(BYTE)"[/]$ "[j])prompt_ok=0;
            for (unsigned row=0;row<prompt_row && prompt_ok;row++) {
                int match=1;
                for (unsigned j=0;j<80;j++) {
                    char ch=j<length ? expected[j] : ' ';
                    if ((BYTE)snapshot.cells[row*80+j]!=(BYTE)ch)match=0;
                }
                if (match)found=1;
            }
        }
        if (found)return 1;
        rm_sleep(1);
    }
    return 0;
}
__attribute__((section(".text.start")))
int lwp_main(PCLW_ENV env) {
    lw_abi_base=env->abi;rm_base=env->rm;
    CHECK(rm_text_host(1)==RM_OK);
    CHECK(lw_app_launch(lw_blk_get(0),"/CCP.EXE",0,&id)==RM_OK);
    for (unsigned i=0;i<2000;i++) {
        for (unsigned slot=0;slot<32;slot++)
            if (rm_text_snapshot(slot,&snapshot)==RM_OK && snapshot.task_id==id)
                window=snapshot.window_id;
        if (window)break;
        rm_sleep(1);
    }
    prompt_text="[/BIN]$ ";
    CHECK(window && wait_text(prompt_text,1));
    CHECK(command("pwd"));CHECK(wait_text("/BIN",1));
    CHECK(command("ls"));CHECK(wait_text("LS.EXE",1));
    CHECK(command("cd .."));prompt_text="[/]$ ";
    CHECK(wait_text(prompt_text,1));stage=1;
    for (unsigned i=0;i<3;i++) {
        CHECK(command("hello"));
        CHECK(wait_text("Hello, world!",1));
    }
    stage=2;
    CHECK(command("./argtest.exe one two"));
    CHECK(wait_text("ARGS OK",0));
    CHECK(send('z'));
    CHECK(wait_text("INPUT OK",1) && wait_text("42",1));stage=3;
    CHECK(command("./missing.exe"));
    CHECK(wait_text("[/]$ ",1));
    CHECK(command("pwd"));
    CHECK(wait_text("/",1));
    CHECK(command("path /MISSING:/BIN"));CHECK(wait_text(prompt_text,1));
    CHECK(command("ls"));
    CHECK(wait_listing(0));
    CHECK(command("ls -l"));
    CHECK(wait_listing(1));
    CHECK(command("cat CATTEST.TXT"));
    CHECK(wait_text("CAT END",1) && wait_text("Time: ",1));
    CHECK(command("cd /BIN"));prompt_text="[/BIN]$ ";CHECK(wait_text(prompt_text,1));
    CHECK(command("cat ../CATTEST.TXT"));CHECK(wait_text("CAT END",1) && wait_text("Time: ",1));
    CHECK(command("cd /"));prompt_text="[/]$ ";CHECK(wait_text(prompt_text,1));
    CHECK(command("xxd -l5 /SEDIN.TXT"));CHECK(wait_text("00000000: 616c 7068 61",1));
    CHECK(command("xxd -p -s1 -l4 /SEDIN.TXT"));CHECK(wait_text("6c706861",1));
    CHECK(command("sed 's/alpha/SED_OK/g' /SEDIN.TXT"));CHECK(wait_text("SED_OK 56",1));
    CHECK(command("sed -n '2p' /SEDIN.TXT"));CHECK(wait_text("beta 34",1));
    CHECK(command("sed -f /SCRIPT.SED /SEDIN.TXT"));CHECK(wait_text("56:alpha",1));
    CHECK(command("sed -n '1w /SEDOUT.TXT' /SEDIN.TXT"));CHECK(wait_tail(prompt_text));
    CHECK(command("sed '[' /SEDIN.TXT"));CHECK(wait_text("2",1));
    CHECK(command("sed p /MISSING.TXT"));CHECK(wait_tail(prompt_text));
    CHECK(command("sed 's/foo/bar/'"));
    CHECK(command("foo"));CHECK(wait_tail(""));CHECK(send(4));CHECK(wait_text("bar",1));
    CHECK(command("ed -p 'ed> ' /EDIN.TXT"));CHECK(wait_tail("ed> "));
    CHECK(command("999p"));CHECK(wait_tail("ed> "));
    CHECK(command("2s/old/new/g"));CHECK(wait_tail("ed> "));
    CHECK(command("2p"));CHECK(wait_text("new new",0) && wait_tail("ed> "));
    CHECK(command("3d"));CHECK(wait_tail("ed> "));
    CHECK(command("w /EDOUT.TXT"));CHECK(wait_tail("ed> "));
    CHECK(command("q"));CHECK(wait_tail(prompt_text));
    CHECK(command("ed -p 'ed> '"));CHECK(wait_tail("ed> "));
    CHECK(command("a"));CHECK(command("hello ed"));CHECK(command("."));CHECK(wait_tail("ed> "));
    CHECK(command("w /NEW.TXT"));CHECK(wait_tail("ed> "));
    CHECK(command("q"));CHECK(wait_tail(prompt_text));
    CHECK(command("ed -p 'ed> ' /EDIN.TXT"));CHECK(wait_tail("ed> "));
    CHECK(command("1,$d"));CHECK(wait_tail("ed> "));
    CHECK(command("w /EMPTY.TXT"));CHECK(wait_tail("ed> "));
    CHECK(command("q"));CHECK(wait_tail(prompt_text));
    CHECK(command("hed /HEDIN.BIN"));CHECK(wait_tail("hed> "));
    CHECK(command("0x1 o 7f 00"));CHECK(wait_tail("hed> "));
    CHECK(command("0x3,0x4 c de ad be ef"));CHECK(wait_tail("hed> "));
    CHECK(command("q"));CHECK(wait_tail("hed> ")); /* Dirty quit must retain the editor. */
    CHECK(command("u"));CHECK(wait_tail("hed> "));
    CHECK(command("u"));CHECK(wait_tail("hed> "));
    CHECK(command("w /HEDOUT.BIN"));CHECK(wait_tail("hed> "));
    CHECK(command("q"));CHECK(wait_tail(prompt_text));
    CHECK(command("hed"));CHECK(wait_tail("hed> "));
    CHECK(command("i"));CHECK(command("00 ff 0a"));CHECK(command("."));CHECK(wait_tail("hed> "));
    CHECK(command("w /NEWHEX.BIN"));CHECK(wait_tail("hed> "));
    CHECK(command("q"));CHECK(wait_tail(prompt_text));
    CHECK(command("touch /TOUCH.TXT /EDOUT.TXT"));CHECK(wait_tail(prompt_text));
    CHECK(command("touch /TOUCH.TXT"));CHECK(wait_tail(prompt_text));
    CHECK(command("cd /TESTDIR"));prompt_text="[/TESTDIR]$ ";CHECK(wait_tail(prompt_text));
    CHECK(command("touch ./REL.TXT ../TOUCH.TXT"));CHECK(wait_tail(prompt_text));
    CHECK(command("cd /"));prompt_text="[/]$ ";CHECK(wait_tail(prompt_text));
    CHECK(command("touch /NOEXIST/A.TXT"));CHECK(wait_tail(prompt_text));
    /* Close the terminal while a foreground child is blocked on input. */
    CHECK(command("./argtest.exe one two"));
    CHECK(wait_text("ARGS OK",0));stage=4;
done:
    if (id) {int code;rm_task_kill(id);rm_task_reap(id,&code);}
    rm_text_host(0);
    return error ? -(int)error : 42;
}
