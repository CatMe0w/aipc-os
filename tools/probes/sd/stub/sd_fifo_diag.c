/*
 * sd_fifo_diag - minimal FIFO read test after CMD17
 */
#include <stdint.h>

#define REG32(addr) (*(volatile uint32_t *)(uintptr_t)(addr))
#define SYSCTRL(off)  REG32(0x08000000u + (off))
#define MCI(off)      REG32(0x20020000u + (off))

#define MCI_CLOCK     MCI(0x04)
#define MCI_ARG       MCI(0x08)
#define MCI_CMD       MCI(0x0C)
#define MCI_STA       MCI(0x34)
#define MCI_MASK      MCI(0x38)
#define MCI_RESP0     MCI(0x14)
#define MCI_DATATIMER MCI(0x24)
#define MCI_DATALEN   MCI(0x28)
#define MCI_DATACTRL  MCI(0x2C)
#define MCI_DMACTRL   MCI(0x3C)
#define MCI_FIFO      MCI(0x40)

#define CPSM_ENABLE   (1u << 0)
#define CPSM_RESPONSE (1u << 7)
#define DPSM_ENABLE   (1u << 0)
#define DPSM_DIR_READ (1u << 1)

#define RESULT_BASE 0x48001100u
#define OUT ((volatile uint32_t *)(uintptr_t)RESULT_BASE)

static void sd(void) { for(volatile uint32_t i=0;i<2000u;i++) __asm__(""); }

static uint32_t ws(uint32_t m, uint32_t L) {
    for(uint32_t i=0;i<L;i++){uint32_t s=MCI_STA;if(s&m)return((i+1)<<16)|(s&0xFFFF);}
    return 0;
}

static int sr(uint32_t c, uint32_t a, uint32_t *r, uint32_t *s) {
    MCI_ARG=a; MCI_CMD=CPSM_ENABLE|CPSM_RESPONSE|(c<<1);
    uint32_t v=ws(0x15,300000); *s=v&0xFFFF;
    if(*s&0x15){*r=MCI_RESP0; return 0;}
    return -1;
}

void stub_main(void) {
    for(uint32_t i=0;i<64;i++)OUT[i]=0;
    OUT[0]=0xF1F0D1A0u;

    /* init same as before */
    uint32_t clk=SYSCTRL(0x0C);
    SYSCTRL(0x0C)=clk|4u; sd(); SYSCTRL(0x0C)=clk&~4u; sd();
    SYSCTRL(0x78)=(SYSCTRL(0x78)&~((7u<<16)|(1u<<29)))|(7u<<16);
    SYSCTRL(0x74)=(SYSCTRL(0x74)&~(3u<<3))|(2u<<3);
    SYSCTRL(0x9C)|=0x180u; SYSCTRL(0xA0)|=0x180u; SYSCTRL(0xA4)|=0x180u;
    MCI_CLOCK=0; sd(); MCI_CLOCK=(1u<<20)|(1u<<19); sd();
    MCI_CLOCK=(1u<<20)|(1u<<19)|(1u<<16)|0xF0u; sd();
    MCI_DATATIMER=0x30000u; MCI_MASK=0xFFFFFFFFu;
    MCI_CMD=0; MCI_DATACTRL=0; MCI_DMACTRL=0;

    for(volatile uint32_t i=0;i<8000u;i++)__asm__("");
    MCI_ARG=0; MCI_CMD=CPSM_ENABLE; ws(0x24,300000); sd();

    uint32_t r8,s8;
    if(sr(8,0x1AAu,&r8,&s8)<0||r8!=0x1AAu){OUT[1]=0xBAD8;return;}
    OUT[1]=r8;

    /* ACMD41 loop */
    uint32_t oa=0x40000000u,oc=0;
    for(uint32_t a=0;a<200;a++){
        if(sr(55,0,&r8,&s8)<0){OUT[2]=0xBAD55|a;return;}
        if(sr(41,oa,&oc,&s8)<0){OUT[2]=0xBAD41|a;return;}
        if(oc&0x80000000u)break;
        if(a==0&&(oc&0x00FFFFFFu))oa=oc&0x40FF8000u;
        sd();
    }
    if(!(oc&0x80000000u)){OUT[2]=0xBAD41F;return;}

    int sh=(oc&0x40000000u)?1:0;
    OUT[2]=oc; OUT[3]=sh;

    /* CMD2 + CMD3 + CMD7 */
    MCI_ARG=0; MCI_CMD=CPSM_ENABLE|CPSM_RESPONSE|(1u<<8)|(2u<<1);
    ws(0x15,300000); sd();
    uint32_t rca,s;
    if(sr(3,0,&rca,&s)<0){OUT[4]=0xBAD3;return;}
    OUT[4]=rca;
    if(sr(7,rca&0xFFFF0000u,&r8,&s)<0){OUT[5]=0xBAD7;return;}
    OUT[5]=r8;

    /* CMD17 with DMACTRL=0, read FIFO as fast as possible */
    MCI_DATALEN=512u;
    MCI_DATACTRL=DPSM_ENABLE|DPSM_DIR_READ|(512u<<16);
    MCI_ARG=sh?0u:0u;
    MCI_CMD=CPSM_ENABLE|CPSM_RESPONSE|(17u<<1);

    /* wait briefly for command response */
    uint32_t cr=ws(0x15,300000);
    OUT[6]=cr;
    if(!((cr&0xFFFF)&0x10)){OUT[7]=0xBAD17C;return;}

    /* read fifo 32 times as fast as possible, no polling */
    volatile uint32_t *f=(volatile uint32_t*)(uintptr_t)MCI_FIFO;
    for(int i=0;i<32;i++)OUT[8+i]=*f;
    for(int i=32;i<127;i++)(void)*f;
    OUT[39]=*f;
    OUT[7]=MCI_STA;
}
