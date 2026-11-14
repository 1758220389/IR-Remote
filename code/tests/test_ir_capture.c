#include <assert.h>
#include <stdio.h>
#include "ir_capture.h"
#include "ir_hardware.h"
#include "ir_dma_probe.h"
#include "bosch144.h"
#include <string.h>
static uint32_t mock_now;
static int mock_high=1, mock_mask, mock_gate;
static unsigned mock_arms;
static int probe_running;
volatile IrDmaProbe g_ir_dma_probe;
static uint16_t live_fall, live_rise;
void IrDmaProbe_Begin(void) {
    memset((void *)&g_ir_dma_probe,0,sizeof(g_ir_dma_probe));
    live_fall=live_rise=0u; probe_running=1;
    g_ir_dma_probe.running=1u;
}
uint16_t IrPlatform_FallCount(void) { return live_fall; }
uint16_t IrPlatform_RiseCount(void) { return live_rise; }
uint16_t IrPlatform_RxTimer(void) { return (uint16_t)mock_now; }
void IrDmaProbe_End(void) {
    if (!probe_running) return;
    probe_running=0; g_ir_dma_probe.running=0;
    g_ir_dma_probe.fall_count=live_fall;
    g_ir_dma_probe.rise_count=live_rise;
}
static uint16_t mock_duration, mock_carrier;
void IrPlatform_Init(void) {}
uint32_t IrPlatform_Now(void) { return mock_now; }
int IrPlatform_High(void) { return mock_high; }
void IrPlatform_RxMask(int enabled) { mock_mask=enabled; }
void IrPlatform_Gate(int mark) { mock_gate=mark; }
void IrPlatform_Arm(uint16_t us) { mock_duration=us; ++mock_arms; }
void IrPlatform_Stop(void) {}
void IrPlatform_Carrier(uint16_t khz) { mock_carrier=khz; }
int IrPlatform_RxPending(void) { return 1; }
int IrPlatform_TxPending(void) { return 1; }
void TIM3_IRQHandler(void);
void EXTI9_5_IRQHandler(void);
static void hardware_test(void) {
    UiSignal a={0};
    a.length=3; a.carrier_khz=38;
    a.timings[0]=560; a.timings[1]=65535u; a.timings[2]=1120;
    IrHardware_Init();
    assert(!mock_mask && !IrHardware_Busy() && !mock_gate);
    assert(!IrHardware_Send(0));
    a.length=0; assert(!IrHardware_Send(&a));
    a.length=UI_SIGNAL_MAX_TIMINGS+1u; assert(!IrHardware_Send(&a));
    a.length=2; assert(!IrHardware_Send(&a));
    a.length=3; a.carrier_khz=29; assert(!IrHardware_Send(&a));
    a.carrier_khz=61; assert(!IrHardware_Send(&a));
    a.carrier_khz=38; a.timings[1]=0; assert(!IrHardware_Send(&a));
    a.timings[1]=65535u;
    assert(IrHardware_Send(&a));
    assert(IrHardware_Busy() && mock_gate && mock_duration==560);
    assert(mock_carrier==38 && !IrHardware_Send(&a));
    a.timings[1]=1; /* Replay MUST use its owned snapshot. */
    TIM3_IRQHandler();
    assert(!mock_gate && mock_duration==65535u);
    TIM3_IRQHandler();
    assert(mock_gate && mock_duration==1120);
    TIM3_IRQHandler();
    assert(!mock_gate && !IrHardware_Busy() && mock_arms==3u);
}
static void hardware_edge(uint32_t us, int high) {
    mock_now=us; mock_high=high;
    if (probe_running) {
        if (high) g_ir_dma_probe.rise[live_rise++]=(uint16_t)us;
        else g_ir_dma_probe.fall[live_fall++]=(uint16_t)us;
    }
    /* EXTI must never be needed to deliver any capture. */
}
static void start_rx(void) {
    mock_now=0; mock_high=1;
    IrHardware_Init(); IrHardware_SetReceiveEnabled(1);
    assert(!probe_running && !mock_mask);
    mock_now=30001; IrHardware_Poll(30);
    assert(probe_running && !mock_mask);
}
static void hardware_rx_test(void) {
    UiSignal out;
    unsigned i;
    start_rx();
    IrHardware_SetReceiveEnabled(1); /* idempotent; do not reset DMA */
    hardware_edge(65000,0);
    for (i=0;i<7;++i) hardware_edge(65000+(i+1)*560u,(i&1u)==0u);
    IrHardware_Poll(69); /* notice live DMA progress */
    mock_now+=30000; IrHardware_Poll(99);
    assert(!probe_running && !mock_mask);
    assert(IrHardware_TryReceive(&out)==1);
    assert(out.length==7u && out.format==UI_SIGNAL_RAW);
    assert(out.timings[0]==560u && out.timings[6]==560u);
    assert(IrHardware_TryReceive(&out)==0);
    IrHardware_SetReceiveEnabled(0);
    assert(!probe_running);
}
static void hardware_discard_test(void) {
    UiSignal out;
    uint16_t saved;
    unsigned i, variant;
    for (variant=0u;variant<6u;++variant) {
        start_rx();
        memset(&out,0xA5,sizeof(out));
        hardware_edge(40000u,0);
        for (i=0u;i<(variant==4u ? 283u : 7u);++i)
            hardware_edge(40000u+(i+1u)*(variant==1u ? 20u : 560u),(i&1u)==0u);
        if (variant==0u) g_ir_dma_probe.flags=IR_DMA_PROBE_OVERCAPTURE;
        if (variant==2u) { hardware_edge(mock_now+560u,0); }
        if (variant==5u) { live_fall=live_rise=512u; }
        IrHardware_Poll(0);
        mock_now+=((variant==2u || variant==3u) ? 1000000u : 30000u);
        IrHardware_Poll(0);
        assert(!probe_running);
        saved=g_ir_dma_probe.fall_count;
        assert(IrHardware_TryReceive(&out)==-1);
        assert(IrHardware_LastReceiveError()!=0u);
        assert(out.length==0xA5A5u); /* no partial output on failure */
        mock_now+=2000000u; IrHardware_Poll(0);
        IrHardware_SetReceiveEnabled(1);
        assert(!probe_running && g_ir_dma_probe.fall_count==saved);
        assert(IrHardware_TryReceive(&out)==-1);
        if (variant==0u) {
            UiSignal replay={0};
            replay.length=3u; replay.carrier_khz=38u;
            replay.timings[0]=replay.timings[1]=replay.timings[2]=560u;
            assert(IrHardware_Send(&replay));
            assert(IrHardware_LastReceiveError()==0u);
            TIM3_IRQHandler(); TIM3_IRQHandler(); TIM3_IRQHandler();
        }
        IrHardware_SetReceiveEnabled(0);
        assert(IrHardware_TryReceive(&out)==0);
    }
}
static void hardware_bosch_test(void) {
    Bosch144State state;
    UiSignal encoded, out, snapshot;
    uint16_t count, i;
    unsigned mode;
    uint32_t t;
    for (mode=0u;mode<4u;++mode) {
        Bosch144_Init(&state);
        if (mode==1u) Bosch144_SetPower(&state,0u);
        assert(Bosch144_Generate(&state,&encoded));
        snapshot=encoded;
        count=Bosch144_DurationCount(&encoded);
        start_rx(); t=40000u; hardware_edge(t,0);
        for (i=0u;i<(mode==3u ? 199u : count);++i) {
            t+=Bosch144_Duration(&encoded,i);
            hardware_edge(t,(i&1u)==0u);
            IrHardware_Poll(0);
        }
        if (mode==2u) { /* corrupt checksum bit in final section */
            g_ir_dma_probe.fall[149]-=1000u;
        }
        /* mode 3 stops after the first two ON sections: not a valid OFF. */
        mock_now+=30000u; IrHardware_Poll(0);
        if (mode>=2u) {
            assert(IrHardware_TryReceive(&out)==-1);
            continue;
        }
        assert(IrHardware_TryReceive(&out)==1);
        assert(out.format==UI_SIGNAL_BOSCH144 && out.length==encoded.length);
        assert(memcmp(out.timings,encoded.timings,out.length*2u)==0);
        mock_arms=0;
        assert(IrHardware_Send(&encoded));
        encoded.timings[0]^=255u; /* TX must use its validated owned snapshot */
        for (i=0u;i<count;++i) {
            assert(mock_duration==Bosch144_Duration(&snapshot,i));
            assert(mock_gate==(!(i&1u)));
            TIM3_IRQHandler();
        }
        assert(!mock_gate && !IrHardware_Busy() && mock_arms==count);
        assert(!IrHardware_Send(&encoded));
        encoded=snapshot; encoded.format=99u;
        assert(!IrHardware_Send(&encoded));
    }
}
static void frame(IrCaptureState *s, uint32_t t) {
    unsigned i;
    IrCapture_Edge(s, t, 0);
    for (i=0; i<7; ++i) IrCapture_Edge(s, t+(i+1)*560u, (i&1u)==0u);
}
static void capture_boundaries_test(void) {
    IrCaptureState s;
    UiSignal out;
    uint32_t t;
    unsigned i;
    IrCapture_Init(&s);
    IrCapture_Poll(&s, 2000000u, 1);
    assert(!IrCapture_Take(&s, &out)); /* stuck-high is not a frame */
    IrCapture_Edge(&s, 2000100u, 0);
    IrCapture_Edge(&s, 2065636u, 1); /* 65536 cannot fit uint16_t */
    IrCapture_Poll(&s, 2100000u, 1);
    assert(!IrCapture_Take(&s, &out));
    IrCapture_Init(&s);
    IrCapture_Edge(&s, 0u, 0);
    for (i=0;i<61;++i) IrCapture_Edge(&s, (i+1u)*20000u, (i&1u)==0u);
    IrCapture_Poll(&s, 1300000u, 1);
    assert(!IrCapture_Take(&s, &out)); /* total timeout, not merely edge timeout */
    IrCapture_Init(&s);
    IrCapture_Edge(&s, 100u, 0);
    t=100u;
    for (i=0;i<199u;++i) {
        t+=(i==99u ? 5600u : 560u);
        IrCapture_Edge(&s, t, (i&1u)==0u);
    }
    IrCapture_Poll(&s, t+IR_CAPTURE_GAP_US-1u, 1);
    assert(!IrCapture_Take(&s, &out));
    IrCapture_Poll(&s, t+IR_CAPTURE_GAP_US, 1);
    assert(!IrCapture_Take(&s, 0));
    assert(IrCapture_Take(&s, &out) && out.length==199u);
    assert(out.timings[99]==5600u);
    IrCapture_Init(&s);
    IrCapture_Edge(&s, 0u, 0);
    IrCapture_Edge(&s, 65535u, 1);
    for (i=1;i<7;++i) IrCapture_Edge(&s, 65535u+i*560u, (i&1u)==0u);
    IrCapture_Poll(&s, 100000u, 1);
    assert(IrCapture_Take(&s, &out) && out.timings[0]==65535u);
}
int main(void) {
    IrCaptureState s;
    UiSignal out;
    IrCapture_Init(&s);
    frame(&s, 100u);
    IrCapture_Poll(&s, 40020u, 1);
    assert(IrCapture_Take(&s, &out));
    assert(out.length==7 && out.carrier_khz==38);
    assert(out.timings[0]==560 && out.timings[6]==560);
    assert(!IrCapture_Take(&s, &out));
    /* Short glitches must not become learnable truncated messages. */
    IrCapture_Init(&s);
    IrCapture_Edge(&s, 0u, 0);
    IrCapture_Edge(&s, 20u, 1);
    IrCapture_Poll(&s, 40000u, 1);
    assert(!IrCapture_Take(&s, &out));
    frame(&s, 50000u);
    IrCapture_Poll(&s, 90000u, 1);
    assert(IrCapture_Take(&s, &out));
    /* A stuck mark is rejected even when release arrives much later. */
    IrCapture_Init(&s);
    IrCapture_Edge(&s, 100u, 0);
    IrCapture_Poll(&s, 1100100u, 0);
    frame(&s, 1200100u);
    IrCapture_Poll(&s, 1240100u, 1);
    assert(!IrCapture_Take(&s, &out));
    /* Delayed foreground: new frame must not splice into the first. */
    IrCapture_Init(&s);
    frame(&s, 100u);
    frame(&s, 50100u);
    assert(IrCapture_Take(&s, &out) && out.length==7u);
    /* Wraparound and a 5600us inter-phase space remain one message. */
    IrCapture_Init(&s);
    frame(&s, UINT32_MAX-2000u);
    frame(&s, (uint32_t)(UINT32_MAX-2000u+3920u+5600u));
    IrCapture_Poll(&s, 50000u, 1);
    assert(IrCapture_Take(&s, &out) && out.length==15u);
    assert(out.timings[7]==5600u);
    /* Capacity overflow rejects the ENTIRE frame, never a prefix. */
    IrCapture_Init(&s);
    IrCapture_Edge(&s, 0u, 0);
    {
        unsigned i;
        for (i=0; i<UI_SIGNAL_MAX_TIMINGS+3u; ++i) {
            IrCapture_Edge(&s, (i+1u)*100u, (i&1u)==0u);
            assert(s.signal.length<=UI_SIGNAL_MAX_TIMINGS);
        }
        IrCapture_Poll(&s, (UI_SIGNAL_MAX_TIMINGS+3u)*100u+40000u, 1);
    }
    assert(!IrCapture_Take(&s, &out));
    capture_boundaries_test();
    hardware_test();
    hardware_rx_test();
    hardware_discard_test();
    hardware_bosch_test();
    puts("IR capture/replay tests passed");
    return 0;
}
