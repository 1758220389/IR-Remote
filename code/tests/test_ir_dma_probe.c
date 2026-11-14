#include "ir_dma_probe.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    uint16_t fall[2]={100u,2300u};
    uint16_t rise[2]={650u,3950u};
    uint16_t out[4], n;
    assert(IrDmaProbe_Reconstruct(fall,2u,rise,2u,0u,0u,out,4u,&n)==0u);
    assert(n==3u && out[0]==550u && out[1]==1650u && out[2]==1650u);
    puts("ir_dma_probe: alternating marks/spaces PASS");
    assert(IrDmaProbe_Reconstruct(fall,0u,rise,1u,0u,0u,out,4u,&n)
           & IR_DMA_PROBE_INVALID);
    assert(n==0u);
    assert(IrDmaProbe_Reconstruct(fall,2u,rise,2u,0u,0u,out,1u,&n)
           & IR_DMA_PROBE_OUTPUT_SHORT);
    assert(n==0u);
    assert(IrDmaProbe_Reconstruct(fall,513u,rise,0u,0u,0u,out,4u,&n)
           & IR_DMA_PROBE_INVALID);
    assert(IrDmaProbe_Reconstruct(0,1u,rise,1u,0u,0u,out,4u,&n)
           & IR_DMA_PROBE_INVALID);
    assert(IrDmaProbe_Reconstruct(fall,1u,rise,1u,2u,0u,out,4u,&n)
           & IR_DMA_PROBE_INVALID);
    assert(IrDmaProbe_Reconstruct(fall,0u,rise,0u,0u,0u,0,0u,&n)==0u);
    assert(n==0u);
    assert(IrDmaProbe_Reconstruct(fall,1u,rise,0u,0u,0u,0,0u,&n)==0u);
    assert(n==0u);
    assert(IrDmaProbe_Reconstruct(fall,0u,rise,0u,0u,0u,0,0u,0)
           & IR_DMA_PROBE_INVALID);
    puts("ir_dma_probe: invalid counts/pointers/capacity/empty PASS");
    fall[0]=65500u; rise[0]=514u; fall[1]=2164u; rise[1]=2714u;
    assert(IrDmaProbe_Reconstruct(fall,2u,rise,2u,0u,0u,out,4u,&n)==0u);
    assert(n==3u && out[0]==550u && out[1]==1650u && out[2]==550u);
    assert(IrDmaProbe_Reconstruct(rise,2u,fall,2u,1u,0u,out,4u,&n)==0u);
    assert(n==3u && out[0]==550u && out[1]==1650u && out[2]==550u);
    rise[0]=fall[0];
    assert(IrDmaProbe_Reconstruct(fall,1u,rise,1u,0u,0u,out,4u,&n)
           & IR_DMA_PROBE_ZERO_INTERVAL);
    assert(n==0u);
    {
        uint16_t f[512], r[512], durations[1023], i;
        uint32_t flags;
        for (i=0u;i<512u;++i) {
            f[i]=(uint16_t)(65000ul+(uint32_t)i*2200ul);
            r[i]=(uint16_t)(65000ul+(uint32_t)i*2200ul+550ul);
        }
        flags=IrDmaProbe_Reconstruct(f,512u,r,512u,0u,
            IR_DMA_PROBE_DMA_ERROR,durations,1023u,&n);
        assert(flags==(IR_DMA_PROBE_DMA_ERROR | IR_DMA_PROBE_FULL_FALL |
                       IR_DMA_PROBE_FULL_RISE));
        assert(n==1023u);
        for (i=0u;i<n;++i) assert(durations[i]==((i&1u)?1650u:550u));
        assert(IrDmaProbe_Reconstruct(f,150u,r,150u,0u,0u,
               durations,1023u,&n)==0u);
        assert(n==299u);
    }
    assert(g_ir_dma_probe.running==0u && g_ir_dma_probe.fall_count==0u &&
           g_ir_dma_probe.rise_count==0u);
    puts("ir_dma_probe: wrap/rising-first/zero/full/299 durations PASS");
    return 0;
}
