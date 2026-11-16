#include "ac_codec.h"
#include "bosch144.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
static uint16_t read_bosch(void *context,uint16_t index)
{ return Bosch144_Duration((const UiSignal *)context,index); }
int main(void)
{
    UiSignal signal,decoded,off;
    UiProfile profile;
    Bosch144State bosch;
    AcState state;
    AcStatus status;
    Bosch144_Init(&bosch);
    assert(Bosch144_Generate(&bosch,&signal));
    assert(Ac_Validate(&signal));
    assert(Ac_Detect(read_bosch,&signal,299u,&decoded)==AC_DETECT_MATCH);
    assert(decoded.format==UI_SIGNAL_BOSCH144 && decoded.length==18u);
    assert(Ac_Import(&state,&decoded)==AC_IMPORT_STATE);
    assert(Ac_GetStatus(&state,&status) && status.power && status.temp_half==51u);
    assert(Ac_ProfileFromSignal(&profile,&signal));
    assert(Ac_ProfileValid(&profile));
    assert(Ac_ProfileLoad(&state,&profile));
    assert(Ac_Button(&state,1u) && Ac_GetStatus(&state,&status));
    assert(status.temp_half==52u);
    assert(Ac_Button(&state,5u) && !state.power);
    assert(Ac_Encode(&state,&off) && off.length==12u);
    Ac_ProfileCommit(&profile,&state);
    assert(profile.kind==UI_PROFILE_BOSCH_OFF && profile.keys[0].length==18u);
    assert(Ac_ProfileLoad(&state,&profile) && !state.power);
    assert(Ac_Button(&state,5u) && Ac_GetStatus(&state,&status));
    assert(status.power && status.temp_half==52u);
    assert(!Ac_ProfileFromSignal(&profile,&off));
    decoded=signal;decoded.timings[17]^=1u;assert(!Ac_Validate(&decoded));
    assert(Ac_Detect(read_bosch,&signal,283u,&decoded)!=AC_DETECT_MATCH);
#ifndef AC_BOSCH_ONLY
    decoded=signal;
    assert(Ac_Detect(read_bosch,&signal,199u,&decoded)==AC_DETECT_AMBIGUOUS);
    assert(!memcmp(&decoded,&signal,sizeof(signal)));
#endif
    puts("AC registry: Bosch identification/import/control/preserved OFF/profile PASS");
    return 0;
}
