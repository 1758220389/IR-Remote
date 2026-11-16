/* LGPL-2.1-or-later; real capture attribution: GREE-NOTICE.txt. */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "ac_gree.h"
static uint16_t wave[139];
static const uint8_t expected[8] = {0x19,0x0a,0x60,0x50,2,0x23,0,0xf0};
static uint16_t read_wave(void *context, uint16_t index)
{
    const uint16_t *data = (const uint16_t *)context;
    assert(index < 139u);
    return data[index];
}
static void load_fixture(void)
{
    FILE *file;
    char buffer[8192];
    char *p;
    size_t size;
    unsigned int i;
    file = fopen("tests/fixtures/upstream_gree_issue386.json", "rb");
    assert(file != NULL);
    size = fread(buffer,1,sizeof(buffer)-1,file);
    assert(!ferror(file));
    assert(feof(file));
    fclose(file);
    buffer[size] = '\0';
    p = strstr(buffer,"\"durations_us\"");
    assert(p != NULL);
    p = strchr(p,'[') + 1;
    for (i=0;i<139u;i++) {
        while (*p == ' ' || *p == '\n' || *p == ',' || *p == '\r') p++;
        assert(*p >= '0' && *p <= '9');
        wave[i] = (uint16_t)strtoul(p,&p,10);
    }
    while (*p == ' ' || *p == '\n' || *p == '\r') p++;
    assert(*p == ']');
}
static void test_real_decode(void)
{
    UiSignal signal;
    unsigned int i;
    memset(&signal,0xa5,sizeof(signal));
    assert(AcGree_Decode(read_wave,wave,139,&signal));
    assert(signal.format == UI_SIGNAL_GREE);
    assert(signal.carrier_khz == 38u && signal.length == 8u);
    for (i=0;i<8u;i++) assert(signal.timings[i] == expected[i]);
    assert(AcGree_Validate(&signal));
}
static void test_envelope(void)
{
    UiSignal signal, decoded;
    uint16_t encoded[139];
    unsigned int i;
    assert(AcGree_Decode(read_wave,wave,139,&signal));
    assert(AcGree_DurationCount(&signal)==139u);
    for (i=0;i<139u;i++) {
        encoded[i]=AcGree_Duration(&signal,(uint16_t)i);
        /* Compare encoder to independent published capture, not only itself. */
        assert((unsigned long)wave[i]*4ul >= (unsigned long)encoded[i]*3ul);
        assert((unsigned long)wave[i]*4ul <= (unsigned long)encoded[i]*5ul);
    }
    assert(encoded[0]==9000u && encoded[1]==4500u);
    assert(encoded[72]==620u && encoded[73]==19980u);
    assert(encoded[138]==620u);
    assert(AcGree_Duration(&signal,139)==0u);
    assert(AcGree_Decode(read_wave,encoded,139,&decoded));
    assert(memcmp(signal.timings,decoded.timings,16)==0);
    for (i=0;i<139u;i++) encoded[i]=(uint16_t)((unsigned long)AcGree_Duration(&signal,(uint16_t)i)*3ul/4ul);
    assert(AcGree_Decode(read_wave,encoded,139,&decoded));
    for (i=0;i<139u;i++) encoded[i]=(uint16_t)((unsigned long)AcGree_Duration(&signal,(uint16_t)i)*5ul/4ul);
    assert(AcGree_Decode(read_wave,encoded,139,&decoded));
}
static void test_buttons(void)
{
    UiSignal signal, output;
    AcState state, saved;
    AcStatus status;
    unsigned int i;
    static const uint8_t modes[5]={AC_MODE_DRY,AC_MODE_HEAT,AC_MODE_FAN,AC_MODE_AUTO,AC_MODE_COOL};
    assert(AcGree_Decode(read_wave,wave,139,&signal));
    assert(AcGree_Import(&state,&signal)==AC_IMPORT_STATE);
    assert(state.length==8u && state.format==UI_SIGNAL_GREE);
    assert(memcmp(state.raw,expected,8)==0);
    assert(AcGree_GetStatus(&state,&status));
    assert(status.power==1u && status.temp_half==52u);
    assert(status.mode==AC_MODE_COOL && status.fan==AC_FAN_LOW);
    assert(AcGree_Button(&state,1));
    assert(AcGree_GetStatus(&state,&status) && status.temp_half==54u);
    assert(AcGree_Button(&state,2));
    saved=state;
    assert(AcGree_Button(&state,5));
    assert(state.power==0u && memcmp(state.raw,saved.raw,24)==0);
    assert(AcGree_Encode(&state,&output) && AcGree_Validate(&output));
    assert(!(output.timings[0]&8u) && !(output.timings[2]&0x40u));
    saved=state;
    assert(AcGree_Import(&state,&output)==AC_IMPORT_PARTIAL);
    assert(memcmp(&state,&saved,sizeof(state))==0);
    memset(&saved,0,sizeof(saved));
    assert(!AcGree_Import(&saved,&output));
    assert(AcGree_Button(&state,5));
    assert(AcGree_Encode(&state,&output));
    assert(output.timings[2]&0x40u);
    assert(memcmp(output.timings,signal.timings,16)==0);
    for (i=0;i<5u;i++) {
        assert(AcGree_Button(&state,3));
        assert(AcGree_GetStatus(&state,&status) && status.mode==modes[i]);
        if (status.mode==AC_MODE_DRY) {
            assert(status.fan==AC_FAN_LOW);
            assert(AcGree_Button(&state,4));
            assert(AcGree_GetStatus(&state,&status) && status.fan==AC_FAN_LOW);
        }
        if (status.mode==AC_MODE_AUTO) {
            assert(status.temp_half==50u);
            assert(AcGree_Button(&state,1));
            assert(AcGree_GetStatus(&state,&status) && status.temp_half==50u);
        }
    }
    for (i=0;i<30u;i++) assert(AcGree_Button(&state,1));
    assert(AcGree_GetStatus(&state,&status) && status.temp_half==60u);
    for (i=0;i<30u;i++) assert(AcGree_Button(&state,2));
    assert(AcGree_GetStatus(&state,&status) && status.temp_half==32u);
    for (i=0;i<4u;i++) {
        assert(AcGree_Button(&state,4));
        assert(AcGree_GetStatus(&state,&status) && status.fan==((i+2u)%4u));
    }
    saved=state;
    assert(!AcGree_Button(&state,0) && !AcGree_Button(&state,6));
    assert(memcmp(&state,&saved,sizeof(state))==0);
    signal.timings[2]&=~0x40u;
    assert(AcGree_Validate(&signal));
    assert(AcGree_Import(&state,&signal));
    assert(AcGree_Button(&state,5) && AcGree_Button(&state,5));
    assert(AcGree_Encode(&state,&output) && !(output.timings[2]&0x40u));
}
static void fix_checksum(UiSignal *signal)
{
    unsigned int i, sum;
    sum=10;
    for (i=0;i<4u;i++) sum+=signal->timings[i]&15u;
    for (i=4;i<7u;i++) sum+=signal->timings[i]>>4;
    signal->timings[7]=(uint16_t)((signal->timings[7]&15u)|((sum&15u)<<4));
}
static void test_strict_rejection(void)
{
    UiSignal signal, bad, out, saved;
    AcState state, before;
    AcStatus status, status_before;
    uint16_t damaged[139];
    unsigned int i;
    static const unsigned int bytes[9]={0,1,1,2,3,4,5,6,7};
    static const uint16_t masks[9]={7,5,0x80,1,1,0x80,8,1,1};
    assert(AcGree_Decode(read_wave,wave,139,&signal));
    memset(&out,0x6b,sizeof(out));
    saved=out;
    for (i=0;i<139u;i++) {
        memcpy(damaged,wave,sizeof(wave));
        damaged[i]=1;
        assert(!AcGree_Decode(read_wave,damaged,139,&out));
        assert(memcmp(&out,&saved,sizeof(out))==0);
        assert(!AcGree_Decode(read_wave,wave,(uint16_t)i,&out));
    }
    assert(!AcGree_Decode(read_wave,wave,140,&out));
    assert(!AcGree_Decode(NULL,wave,139,&out));
    assert(!AcGree_Decode(read_wave,wave,139,NULL));
    /* Well-timed footer bit corruption, rather than malformed pulse widths. */
    memcpy(damaged,wave,sizeof(wave));
    damaged[67]=1600;
    assert(!AcGree_Decode(read_wave,damaged,139,&out));
    /* Well-timed temperature bit corruption must fail checksum. */
    memcpy(damaged,wave,sizeof(wave));
    damaged[19]=1600;
    assert(!AcGree_Decode(read_wave,damaged,139,&out));
    assert(AcGree_Import(&state,&signal));
    before=state;
    for (i=0;i<9u;i++) {
        bad=signal;
        bad.timings[bytes[i]]^=masks[i];
        fix_checksum(&bad);
        assert(!AcGree_Validate(&bad));
        assert(!AcGree_Import(&state,&bad));
        assert(memcmp(&state,&before,sizeof(state))==0);
        assert(!AcGree_DurationCount(&bad));
        assert(!AcGree_Duration(&bad,0));
    }
    bad=signal; bad.timings[7]^=0x10;
    assert(!AcGree_Validate(&bad));
    bad=signal; bad.timings[0]=256;
    assert(!AcGree_Validate(&bad));
    bad=signal; bad.length=7;
    assert(!AcGree_Validate(&bad));
    bad=signal; bad.format=UI_SIGNAL_RAW;
    assert(!AcGree_Validate(&bad));
    bad=signal; bad.carrier_khz=40;
    assert(!AcGree_Validate(&bad));
    assert(!AcGree_Validate(NULL));
    assert(!AcGree_Duration(NULL,0) && !AcGree_DurationCount(NULL));
    assert(!AcGree_Import(NULL,&signal));
    assert(!AcGree_GetStatus(NULL,&status));
    assert(!AcGree_GetStatus(&state,NULL));
    assert(!AcGree_Button(NULL,1));
    assert(!AcGree_Encode(NULL,&out) && !AcGree_Encode(&state,NULL));
    state.power=2;
    memset(&status,0x34,sizeof(status)); status_before=status;
    assert(!AcGree_GetStatus(&state,&status));
    assert(memcmp(&status,&status_before,sizeof(status))==0);
    assert(!AcGree_Encode(&state,&out));
    assert(memcmp(&out,&saved,sizeof(out))==0);
    bad=signal; bad.timings[0]=(uint16_t)((bad.timings[0]&0xf8u)|2u);
    bad.timings[0]=(uint16_t)((bad.timings[0]&0xcfu)|0x30u);
    fix_checksum(&bad);
    assert(!AcGree_Validate(&bad)); /* DRY requires LOW. */
    bad=signal; bad.timings[0]&=0xf8u;
    fix_checksum(&bad);
    assert(!AcGree_Validate(&bad)); /* AUTO requires 25 C. */
}
int main(void)
{
    load_fixture();
    test_real_decode();
    test_envelope();
    test_buttons();
    test_strict_rejection();
    puts("GREE tests passed");
    return 0;
}
