#include <assert.h>
#include "convert.h"

int main(void) {
    const struct {PCSTR text;DWORD value;} valid[]={
        {"0",0},{"123",123},{"0010",10},{"10h",16},{"10H",16},
        {"FFh",255},{"aBcH",0xabc},{"0FFh",255},
        {"4294967295",0xffffffffu},{"FFFFFFFFh",0xffffffffu},
        {" \t123\t ",123},{" 0aH ",10}
    };
    for (unsigned i=0;i<sizeof(valid)/sizeof(valid[0]);i++) {
        DWORD small=42;QWORD wide=42;
        assert(num_parse(valid[i].text,&small) && small==valid[i].value);
        assert(num_parse_w(valid[i].text,&wide) && wide==valid[i].value);
    }
    const char *invalid[]={""," \t ","h","H","12a","0x10","0x10h", "+1","-1",
        "1 2","FF h","1hh","Gh","12h3","123\n",0};
    for (unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        DWORD small=42;QWORD wide=42;
        assert(!num_parse(invalid[i],&small) && small==42);
        assert(!num_parse_w(invalid[i],&wide) && wide==42);
    }
    DWORD small=42;QWORD wide=42;
    assert(!num_parse("4294967296",&small) && small==42);
    assert(!num_parse("100000000h",&small) && small==42);
    assert(num_parse_w("18446744073709551615",&wide) && wide==~(QWORD)0);
    assert(num_parse_w("FFFFFFFFFFFFFFFFH",&wide) && wide==~(QWORD)0);
    wide=42;
    assert(!num_parse_w("18446744073709551616",&wide) && wide==42);
    assert(!num_parse_w("10000000000000000h",&wide) && wide==42);
    assert(!num_parse("1",0) && !num_parse_w("1",0));
    return 0;
}
