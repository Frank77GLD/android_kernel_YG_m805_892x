/****************************************************************************
 *  FileName    : tca_ckc.c
 *  Description :
 ****************************************************************************
 *
 *  TCC Version 1.0
 *  Copyright (c) Telechips, Inc.
 *  ALL RIGHTS RESERVED
 *
 ****************************************************************************/

#include <mach/pms_table.h>
#include <mach/bsp.h>
#include <asm/io.h>
#include <linux/mm.h>       // for PAGE_ALIGN
#include <linux/kernel.h>
#include <linux/module.h>

#ifdef CONFIG_MEM_BUS_SCALING
extern void tcc_ddr_set_clock(unsigned int freq);
#endif

#define CPU_SRC_CH  0
#define CPU_SRC_PLL DIRECTPLL0

#ifdef CONFIG_MEM_BUS_SCALING
#define MEM_SRC_CH  1
#define MEM_SRC_PLL DIRECTPLL1
#endif

#if (0) //defined(CONFIG_GPU_BUS_SCALING)
#define USE_GPU_SCALING
#endif
#if defined(USE_GPU_SCALING)
#define GPU_SRC_CH  5
#define GPU_SRC_PLL DIRECTPLL5
#endif

#define MAX_PERI_DIV    4096
#define MAX_FBUS_DIV    16

#define tca_wait()              { volatile int i; for (i=0; i<100; i++); }

#define tca_pll_lock_wait(addr) { \
                                    volatile unsigned int i; \
                                    for (i=100; i ; i--) \
                                        while((*addr & 0x00200000) == 0); \
                                    for (i=0x200; i ; i--); \
                                }

#define MAX_TCC_PLL             6
#define MAX_CLK_SRC             (MAX_TCC_PLL*2 + 1)     // XIN


/****************************************************************************************
* Local Variables
* ***************************************************************************************/
static volatile PCKC            pCKC ;
static volatile PPMU            pPMU ;
static volatile PIOBUSCFG       pIOBUSCFG;
static volatile PDDICONFIG      pDDIBUSCFG;
static volatile PGRPBUSCFG      pGPUBUSCFG;
static volatile PVIDEOBUSCFG    pVIDEOBUSCFG;
static volatile PHSIOBUSCFG     pHSIOBUSCFG;
static volatile PMEMBUSCFG      pMEMBUSCFG;
static volatile unsigned int    stIPPWDNReg;
static volatile PCLK_ZZZ_TYPE   stPKTGENReg[4];

static unsigned int stClockSource[MAX_CLK_SRC];
static int initialized = 0;


/****************************************************************************************
* FUNCTION :void tca_ckc_init(void)
* DESCRIPTION :
* ***************************************************************************************/
void tca_ckc_init(void)
{
    int i;
    pCKC = (CKC *)io_p2v(HwCKC_BASE);
    pPMU = (PMU *)io_p2v(HwPMU_BASE);
    pIOBUSCFG = (IOBUSCFG *)io_p2v(HwIOBUSCFG_BASE);
    pDDIBUSCFG = (DDICONFIG *)io_p2v(HwDDI_CONFIG_BASE);
    pGPUBUSCFG = (GRPBUSCFG *)io_p2v(HwGRPBUSCONFIG_BASE);
    pVIDEOBUSCFG = (VIDEOBUSCFG *)io_p2v(HwVIDEOBUSCONFIG_BASE);
    pHSIOBUSCFG = (HSIOBUSCFG *)io_p2v(HwHSIOBUSCFG_BASE);
    pMEMBUSCFG = (MEMBUSCFG *)io_p2v(HwMBUSCFG_BASE);

    /* IOBUS AHB2AXI: flushs prefetch buffer when bus state is IDLE or WRITE
       enable:  A2XMOD1 (Audio DMA, GPSB, DMA2/3, EHI1)
       disable: A2XMOD0 (USB1.1Host, USB OTG, SD/MMC, IDE, DMA0/1, MPEFEC, EHI0)
    */
#if 0
    pIOBUSCFG->IO_A2X.bREG.A2XMOD2 = 1;
    pIOBUSCFG->IO_A2X.bREG.A2XMOD1 = 1;
    pIOBUSCFG->IO_A2X.bREG.A2XMOD0 = 1;
    pHSIOBUSCFG->HSIO_CFG.bREG.A2X_USB20H = 1;

    pCKC->CLKDIVC0.nREG    = 0x01010101;    // PLL0,PLL1,PLL2,PLL3
    pCKC->CLKDIVC1.nREG    = 0x01010101;    // PLL4,PLL5,XIN,XTIN
#endif

    if (initialized)
        return;

    stIPPWDNReg = 0x00000000;
    stPKTGENReg[0].nREG = 0x00000000;
    stPKTGENReg[1].nREG = 0x00000000;
    stPKTGENReg[2].nREG = 0x00000000;
    stPKTGENReg[3].nREG = 0x00000000;


    for (i=0 ; i<MAX_TCC_PLL ; i++) {
        if (i == CPU_SRC_CH) {
            stClockSource[i] = 0;
            pr_info("    pll_%d:  cpu clock source\n", i);
        }
#ifdef CONFIG_MEM_BUS_SCALING
		else if (i == MEM_SRC_CH) {
            stClockSource[i] = 0;
            pr_info("    pll_%d:  mem clock source\n", i);
        }
#endif
#if defined(USE_GPU_SCALING)
        else if (i == GPU_SRC_CH) {
            stClockSource[i] = 0;
            pr_info("    pll_%d:  graphic clock source\n", i);
        }
#endif
        else {
            stClockSource[i] = tca_ckc_getpll(i);
            pr_info("    pll_%d:  %d kHz (Fixed)\n", i, stClockSource[i]/10);
        }
    }
    for ( ; i<(MAX_TCC_PLL*2) ; i++) {
        if ((i-MAX_TCC_PLL) == CPU_SRC_CH) {
            stClockSource[i] = 0;
            pr_info("div_pll_%d:  cpu clock source\n", i-MAX_TCC_PLL);
        }
#ifdef CONFIG_MEM_BUS_SCALING
        else if ((i-MAX_TCC_PLL) == MEM_SRC_CH) {
            stClockSource[i] = 0;
            pr_info("div_pll_%d:  mem clock source\n", i-MAX_TCC_PLL);
        }
#endif
#if defined(USE_GPU_SCALING)
        else if ((i-MAX_TCC_PLL) == GPU_SRC_CH) {
            stClockSource[i] = 0;
            pr_info("div_pll_%d:  graphic clock source\n", i-MAX_TCC_PLL);
        }
#endif
        else {
            stClockSource[i] = 0;
            pr_info("div_pll_%d:  0 kHz (Fixed)\n", i-MAX_TCC_PLL);
//            stClockSource[i] = tca_ckc_getdividpll(i-MAX_TCC_PLL);
//            pr_info("div_pll_%d:  %d kHz (Fixed)\n", i-MAX_TCC_PLL, stClockSource[i]/10);
        }
    }

    stClockSource[i] = XIN_CLK_RATE;    // XIN
    pr_info("      xin:  %d kHz (Fixed)\n", stClockSource[i]/10);

    initialized = 1;

}

/****************************************************************************************
* FUNCTION :int tca_ckc_setpll(unsigned int pll, unsigned int ch, unsigned int src)
* DESCRIPTION :
* ***************************************************************************************/
int tca_ckc_setpll(unsigned int pll, unsigned int ch, unsigned int src)
{
    unsigned uCnt;
    sfPLL    *pPLL;
    volatile PLLCFG_TYPE *pPLLCFG = (PLLCFG_TYPE *)((&pCKC->PLL0CFG)+ch);

    if (ch >= MAX_TCC_PLL)
        return -1;

    if(pll != 0) {
        if (src == PLLSRC_XIN) {
            pPLL = &pIO_CKC_PLL[0];
            for (uCnt = 0; uCnt < NUM_PLL; uCnt ++, pPLL++) {
                if (pPLL->uFpll <= pll)
                    break;
            }

            if (uCnt >= NUM_PLL) {
                uCnt = NUM_PLL - 1;
                pPLL = &pIO_CKC_PLL[uCnt];
            }
        }else {
            // TODO:
            while (1);    // test code
        }

        pPLLCFG->nREG = ((pPLL->VSEL&0x1) << 30) | ((pPLL->S&0x7) << 24) | (1 << 20) | (src&0x3) << 18 | ((pPLL->M&0x3FF) << 8) | (pPLL->P&0x3F);
        pPLLCFG->nREG |= (1 << 31);
        tca_pll_lock_wait(&(pPLLCFG->nREG));

        if (ch == CPU_SRC_CH) {
            stClockSource[ch] = 0;
            stClockSource[MAX_TCC_PLL+ch] = 0;
            return 0;
        }
#ifdef CONFIG_MEM_BUS_SCALING
        else if (ch == MEM_SRC_CH) {
            stClockSource[ch] = 0;
            stClockSource[MAX_TCC_PLL+ch] = 0;
            return 0;
        }
#endif
        else {
            stClockSource[ch] = pPLL->uFpll;
            stClockSource[MAX_TCC_PLL+ch] = tca_ckc_getdividpll(ch);
        }
    }
    else {
        pPLLCFG->bREG.EN = 0;
        stClockSource[ch] = 0;
        stClockSource[MAX_TCC_PLL+ch] = 0;
    }
    return 0;
}

/****************************************************************************************
* FUNCTION :unsigned int tca_ckc_getpll(unsigned int ch)
* DESCRIPTION :
* ***************************************************************************************/
unsigned int tca_ckc_getpll(unsigned int ch)
{
    unsigned int tPLL;
    unsigned int tPCO;
    unsigned int tSrc;
    unsigned    iP=0, iM=0, iS=0, iEN = 0, iSrc = 0;

    switch(ch) {
        case 0:
            iEN = pCKC->PLL0CFG.bREG.EN;
            iP = pCKC->PLL0CFG.bREG.P;
            iM = pCKC->PLL0CFG.bREG.M;
            iS = pCKC->PLL0CFG.bREG.S;
            iSrc = pCKC->PLL0CFG.bREG.SRC;
            break;
        case 1:
            iEN = pCKC->PLL1CFG.bREG.EN;
            iP = pCKC->PLL1CFG.bREG.P;
            iM = pCKC->PLL1CFG.bREG.M;
            iS = pCKC->PLL1CFG.bREG.S;
            iSrc = pCKC->PLL1CFG.bREG.SRC;
            break;
        case 2:
            iEN = pCKC->PLL2CFG.bREG.EN;
            iP = pCKC->PLL2CFG.bREG.P;
            iM = pCKC->PLL2CFG.bREG.M;
            iS = pCKC->PLL2CFG.bREG.S;
            iSrc = pCKC->PLL2CFG.bREG.SRC;
            break;
        case 3:
            iEN = pCKC->PLL3CFG.bREG.EN;
            iP = pCKC->PLL3CFG.bREG.P;
            iM = pCKC->PLL3CFG.bREG.M;
            iS = pCKC->PLL3CFG.bREG.S;
            iSrc = pCKC->PLL3CFG.bREG.SRC;
            break;
        case 4:
            iEN = pCKC->PLL4CFG.bREG.EN;
            iP = pCKC->PLL4CFG.bREG.P;
            iM = pCKC->PLL4CFG.bREG.M;
            iS = pCKC->PLL4CFG.bREG.S;
            iSrc = pCKC->PLL4CFG.bREG.SRC;
            break;
        case 5:
            iEN = pCKC->PLL5CFG.bREG.EN;
            iP = pCKC->PLL5CFG.bREG.P;
            iM = pCKC->PLL5CFG.bREG.M;
            iS = pCKC->PLL5CFG.bREG.S;
            iSrc = pCKC->PLL5CFG.bREG.SRC;
            break;
        default:
            return 0;
    }

    if (iEN == 0)
        return 0;

    switch(iSrc) {
        case PLLSRC_XIN:
            tSrc = XIN_CLK_RATE;
            break;
        case PLLSRC_HDMIXI:
            tSrc = HDMI_CLK_RATE;
            break;
        case PLLSRC_EXTCLK0:
            tSrc = tca_ckc_getperi(PERI_OUT0);
            break;
        case PLLSRC_EXTCLK1:
            tSrc = tca_ckc_getperi(PERI_OUT1);
            break;
        default:
            return 0;
    }

    tPCO = (tSrc * iM ) / iP;
    tPLL= ((tPCO) >> (iS));

    return tPLL;
}

/****************************************************************************************
* FUNCTION :unsigned int tca_ckc_getdividpll(unsigned int ch)
* DESCRIPTION :
* ***************************************************************************************/
unsigned int tca_ckc_getdividpll(unsigned int ch)
{
    unsigned int tDIVPLL;
    unsigned int tPLL = tca_ckc_getpll(ch);
    unsigned int uiPDIV = 0;

    if (tPLL == 0)
        return 0;

    switch(ch) {
        case 0:
            if (pCKC->CLKDIVC0.bREG.P0TE == 0)
                return 0;
            uiPDIV = pCKC->CLKDIVC0.bREG.P0DIV;
            break;
        case 1:
            if (pCKC->CLKDIVC0.bREG.P1TE == 0)
                return 0;
            uiPDIV = pCKC->CLKDIVC0.bREG.P1DIV;
            break;
        case 2:
            if (pCKC->CLKDIVC0.bREG.P2TE == 0)
                return 0;
            uiPDIV = pCKC->CLKDIVC0.bREG.P2DIV;
            break;
        case 3:
            if (pCKC->CLKDIVC0.bREG.P3TE == 0)
                return 0;
            uiPDIV = pCKC->CLKDIVC0.bREG.P3DIV;
            break;
        case 4:
            if (pCKC->CLKDIVC1.bREG.P4TE == 0)
                return 0;
            uiPDIV = pCKC->CLKDIVC1.bREG.P4DIV;
            break;
        case 5:
            if (pCKC->CLKDIVC1.bREG.P5TE == 0)
                return 0;
            uiPDIV = pCKC->CLKDIVC1.bREG.P5DIV;
            break;
    }

    //Fdivpll Clock
    tDIVPLL = (unsigned int)tPLL/(uiPDIV+1);

    return tDIVPLL;
}

/****************************************************************************************
* FUNCTION :void tca_ckc_setcpu(unsigned int n)
* DESCRIPTION :  n is n/16
* example : CPU == PLL : n=16 - CPU == PLL/2 : n=8
* ***************************************************************************************/
static unsigned int tca_ckc_setcpu(unsigned int freq)
{
    unsigned uCnt;
    sfPLL    *pPLL;
    volatile PLLCFG_TYPE *pPLLCFG = (PLLCFG_TYPE *)((&pCKC->PLL0CFG)+CPU_SRC_CH);

    // 1. temporally change the cpu clock source.(XIN)
#if (1)
    pCKC->CLKCTRL0.bREG.SEL = DIRECTXIN;
    while(pCKC->CLKCTRL0.bREG.CHGREQ);
    pCKC->CLKCTRL0.bREG.CONFIG = 0xFFFF;
    while(pCKC->CLKCTRL0.bREG.CFGREQ);
    pCKC->CLKCTRL0.bREG.EN = 1;
    while(pCKC->CLKCTRL0.bREG.CFGREQ);
#else
    pCKC->CLKCTRL0.nREG = (pCKC->CLKCTRL0.nREG & 0xFFF00000) | 0xFFFF4;
#endif

    // 2. change pll(for cpu) clock.
#if (0)
    tca_ckc_setpll(freq, CPU_SRC_CH, PLLSRC_XIN);
#else
    pPLL = &pIO_CPU_PLL[0];
    for (uCnt = 0; uCnt < NUM_CPU_PLL; uCnt ++, pPLL++) {
        if (pPLL->uFpll <= freq)
            break;
    }

    if (uCnt >= NUM_CPU_PLL) {
        uCnt = NUM_CPU_PLL - 1;
        pPLL = &pIO_CPU_PLL[uCnt];
    }

    pPLLCFG->nREG = ((pPLL->VSEL&0x1) << 30) | ((pPLL->S&0x7) << 24) | (1 << 20) | (0 << 18) | ((pPLL->M&0x3FF) << 8) | (pPLL->P&0x3F);
    pPLLCFG->nREG |= (1 << 31);
    tca_pll_lock_wait(&(pPLLCFG->nREG));
#endif

    // 3. change th cpu clock source.
#if (1)
    pCKC->CLKCTRL0.bREG.SEL = CPU_SRC_PLL;
    while(pCKC->CLKCTRL0.bREG.CHGREQ);
    pCKC->CLKCTRL0.bREG.CONFIG = 0xFFFF;
    while(pCKC->CLKCTRL0.bREG.CFGREQ);
    pCKC->CLKCTRL0.bREG.EN = 1;
    while(pCKC->CLKCTRL0.bREG.CFGREQ);
#else
    pCKC->CLKCTRL0.nREG = (pCKC->CLKCTRL0.nREG & 0xFFF00000) | 0xFFFF0 | CPU_SRC_PLL;
    tca_wait();
#endif

    return tca_ckc_getpll(CPU_SRC_CH);
}

/****************************************************************************************
* FUNCTION :unsigned int tca_ckc_getcpu(void)
* DESCRIPTION :
* ***************************************************************************************/
static unsigned int tca_ckc_getcpu(void)
{
    unsigned int lcpu = 0;
    unsigned int lconfig = 0;
    unsigned int lcnt = 0;
    unsigned int li = 0;
    unsigned int lclksource = 0;

    lconfig = pCKC->CLKCTRL0.bREG.CONFIG;

    for(li = 0; li < 16; li++) {
        if((lconfig & Hw0) == 1)
            lcnt++;
        lconfig = (lconfig >> 1);
    }

    switch(pCKC->CLKCTRL0.bREG.SEL) {
        case DIRECTPLL0 :
            lclksource =  tca_ckc_getpll(0);
            break;
        case DIRECTPLL1 :
            lclksource =  tca_ckc_getpll(1);
            break;
        case DIRECTPLL2 :
            lclksource =  tca_ckc_getpll(2);
            break;
        case DIRECTPLL3 :
            lclksource =  tca_ckc_getpll(3);
            break;
        case DIRECTXIN:
            lclksource =  XIN_CLK_RATE;
            break;
        case DIVIDPLL0:
            lclksource = tca_ckc_getdividpll(0);
            break;
        case DIVIDPLL1:
            lclksource = tca_ckc_getdividpll(1);
            break;
        case DIRECTXTIN:
            lclksource =  XTIN_CLK_RATE;
            break;
        case DIRECTPLL4:
            lclksource =  tca_ckc_getpll(4);
            break;
        case DIRECTPLL5:
            lclksource =  tca_ckc_getpll(5);
            break;
        case DIVIDPLL2:
            lclksource = tca_ckc_getdividpll(2);
            break;
        case DIVIDPLL3:
            lclksource = tca_ckc_getdividpll(3);
            break;
        case DIVIDPLL4:
            lclksource = tca_ckc_getdividpll(4);
            break;
        case DIVIDPLL5:
            lclksource = tca_ckc_getdividpll(5);
            break;
        /*
        case DIVIDXIN:
            break;
        case DIVIDXTIN:
            break;
        */
        default :
            lclksource =  tca_ckc_getpll(CPU_SRC_CH);
            break;
    }

    lcpu = (lclksource * lcnt)/16;

    return lcpu;
}

/****************************************************************************************
* FUNCTION :void tca_ckc_setfpll(unsigned int pll, unsigned int fbus, unsigned int freq)
* ***************************************************************************************/
static unsigned int tca_ckc_setfpll(unsigned int pll_ch, unsigned int pll_src, unsigned int fbus_ch, unsigned int freq)
{
    unsigned uCnt;
    sfPLL    *pPLL;
    volatile PLLCFG_TYPE *pPLLCFG = (PLLCFG_TYPE *)((&pCKC->PLL0CFG)+pll_ch);
    volatile CLKCTRL_TYPE *pCLKCTRL = (CLKCTRL_TYPE *)((&pCKC->CLKCTRL0)+fbus_ch);

    if (freq < 480000)
        freq = 480000;

    // 1. temporally change the fbus clock source.(XIN)
    pCLKCTRL->nREG = (pCLKCTRL->nREG & 0xFFF00000) | 0x00014;

    // 2. change pll(for fbus) clock.
#if (0)
    tca_ckc_setpll(freq*2, CPU_SRC_CH, PLLSRC_XIN);
#else
    pPLL = &pIO_CKC_PLL[0];
    for (uCnt = 0; uCnt < NUM_PLL; uCnt ++, pPLL++) {
        if (pPLL->uFpll <= freq*2)
            break;
    }

    if (uCnt >= NUM_PLL) {
        uCnt = NUM_PLL - 1;
        pPLL = &pIO_CKC_PLL[uCnt];
    }

    pPLLCFG->nREG = ((pPLL->VSEL&0x1) << 30) | ((pPLL->S&0x7) << 24) | (1 << 20) | (0 << 18) | ((pPLL->M&0x3FF) << 8) | (pPLL->P&0x3F);
    pPLLCFG->nREG |= (1 << 31);
    tca_pll_lock_wait(&(pPLLCFG->nREG));
#endif

    // 3. change the fbus clock source.
    pCLKCTRL->nREG = (pCLKCTRL->nREG & 0xFFF00000) | 0x00010 | pll_src;

    tca_wait();

    return tca_ckc_getpll(pll_ch)/2;
}

/****************************************************************************************
* FUNCTION :void tca_ckc_setfbusctrl(unsigned int clkname,unsigned int isenable,unsigned int freq)
* DESCRIPTION :
*   return: real clock rate. ( unit: 100 Hz)
* ***************************************************************************************/
unsigned int tca_ckc_setfbusctrl(unsigned int clkname, unsigned int isenable, unsigned int freq)
{
    volatile CLKCTRL_TYPE *pCLKCTRL = (CLKCTRL_TYPE *)((&pCKC->CLKCTRL0)+clkname);
    unsigned int div[MAX_CLK_SRC], div_100[MAX_CLK_SRC], i, clksrc, searchsrc, overclksrc;
    unsigned int clkrate=0, clkdiv=0;

    searchsrc = 0xFFFFFFFF;
    overclksrc = 0xFFFFFFFF;

    if (clkname == FBUS_CPU)        // CPU
        return tca_ckc_setcpu(freq);
    else if (clkname == FBUS_MEM) {    // Memory
#ifdef CONFIG_MEM_BUS_SCALING
        tcc_ddr_set_clock(freq/10);
        return freq;
#else
        // do not change memory clock. just return current memroy clock rate.
        return tca_ckc_getfbusctrl(clkname);
#endif
    }
#if defined(USE_GPU_SCALING)
    else if (clkname == FBUS_GPU) {
        return tca_ckc_setfpll(GPU_SRC_CH, GPU_SRC_PLL, FBUS_GPU, freq);
    }
#endif
    if (freq < 480000)
        freq = 480000;

    if (freq <= 60000) {
        clksrc = DIRECTXIN;
        clkrate = 60000;
        clkdiv = 1;
    }
    else {
        for (i=0 ; i<MAX_CLK_SRC ; i++) {
            if (stClockSource[i] == 0)
                continue;
        if (stClockSource[i] < freq)
            continue;
            div_100[i] = stClockSource[i]/(freq/100);

            if (div_100[i] > MAX_FBUS_DIV*100)
                div_100[i] = MAX_FBUS_DIV*100;

            /* find maximum frequency pll source */
            if (div_100[i] <= 100) {
                if (overclksrc == 0xFFFFFFFF)
                    overclksrc = i;
                else if (stClockSource[i] > stClockSource[overclksrc])
                    overclksrc = i;
                continue;
            }

            div[i]= div_100[i]/100;
            if (div_100[i]%100)
                div[i] += 1;

            if (div[i] < 2)
                div[i] = 2;

            div_100[i] = freq - stClockSource[i]/div[i];

            if (searchsrc == 0xFFFFFFFF)
                searchsrc = i;
            else {
                /* find similar clock */
                if (div_100[i] < div_100[searchsrc])
                    searchsrc = i;
                /* find even division vlaue */
                else if(div_100[i] == div_100[searchsrc]) {
                    if (div[searchsrc]%2)
                        searchsrc = i;
                    else if (div[searchsrc] > div[i])
                        searchsrc = i;
                }
            }
        }
        if (searchsrc == 0xFFFFFFFF) {
            if (overclksrc == 0xFFFFFFFF) {
                overclksrc = 0;
                for (i=1 ; i<MAX_CLK_SRC ; i++) {
                    if (stClockSource[i] > stClockSource[overclksrc])
                        overclksrc = i;
                }
            }
            searchsrc = overclksrc;
            div[searchsrc] = 2;
        }        
        switch(searchsrc) {
            case 0: clksrc = DIRECTPLL0; break;
            case 1: clksrc = DIRECTPLL1; break;
            case 2: clksrc = DIRECTPLL2; break;
            case 3: clksrc = DIRECTPLL3; break;
            case 4: clksrc = DIRECTPLL4; break;
            case 5: clksrc = DIRECTPLL5; break;
            case 6: clksrc = DIVIDPLL0; break;
            case 7: clksrc = DIVIDPLL1; break;
            case 8: clksrc = DIVIDPLL2; break;
            case 9: clksrc = DIVIDPLL3; break;
            case 10: clksrc = DIVIDPLL4; break;
            case 11: clksrc = DIVIDPLL5; break;
            case 12: clksrc = DIRECTXIN; break;
            default: return 0;
        }
        clkrate = stClockSource[searchsrc]/((div[searchsrc]>16)?16:div[searchsrc]);

        if (div[searchsrc] > MAX_FBUS_DIV)
            clkdiv = MAX_FBUS_DIV - 1;
        else
            clkdiv = div[searchsrc] - 1;
    }
    
    if(clkdiv == CLKDIV0)
        clkdiv = 1;

#if (0)
    pCLKCTRL->nREG = (pCLKCTRL->nREG & (1<<21)) | ((clkdiv&0xF) << 4) | (clksrc&0xF);
    if(isenable == ENABLE)
        pCLKCTRL->nREG |= 1<<21;
    else if (isenable == DISABLE)
        pCLKCTRL->nREG &= ~(1<<21);
#else
    pCLKCTRL->nREG = (pCLKCTRL->nREG & 0xFFF0000F) | 0xF0;
    pCLKCTRL->bREG.SEL = clksrc;
    while(pCLKCTRL->bREG.CHGREQ);
    pCLKCTRL->bREG.CONFIG = clkdiv;
    while(pCLKCTRL->bREG.CFGREQ);
    if(isenable == ENABLE)
        pCLKCTRL->bREG.EN = 1;
    else if (isenable == DISABLE)
        pCLKCTRL->bREG.EN = 0;
    while(pCLKCTRL->bREG.CFGREQ);
#endif
    tca_wait();
    return clkrate;
}

/****************************************************************************************
* FUNCTION :unsigned int tca_ckc_getfbusctrl(unsigned int clkname)
* DESCRIPTION :
* ***************************************************************************************/
unsigned int tca_ckc_getfbusctrl(unsigned int clkname)
{
    volatile CLKCTRL_TYPE *pCLKCTRL = (CLKCTRL_TYPE *)((&pCKC->CLKCTRL0)+clkname);
    unsigned int clksource = 0;

    if (pCLKCTRL->bREG.EN == 0)
        return 0;

    if(clkname == FBUS_CPU)
        return tca_ckc_getcpu();

    switch(pCLKCTRL->bREG.SEL) {
        case DIRECTPLL0:
            clksource =  tca_ckc_getpll(0);
            break;
        case DIRECTPLL1:
            clksource =  tca_ckc_getpll(1);
            break;
        case DIRECTPLL2:
            clksource =  tca_ckc_getpll(2);
            break;
        case DIRECTPLL3:
            clksource =  tca_ckc_getpll(3);
            break;
        case DIRECTXIN:
            clksource =  XIN_CLK_RATE;
            break;
        case DIVIDPLL0:
            clksource =  tca_ckc_getdividpll(0);
            break;
        case DIVIDPLL1:
            clksource =  tca_ckc_getdividpll(1);
            break;
        case DIRECTXTIN:
            clksource =  XTIN_CLK_RATE;
            break;
        case DIRECTPLL4:
            clksource =  tca_ckc_getpll(4);
            break;
        case DIRECTPLL5:
            clksource =  tca_ckc_getpll(5);
            break;
        case DIVIDPLL2:
            clksource =  tca_ckc_getdividpll(2);
            break;
        case DIVIDPLL3:
            clksource =  tca_ckc_getdividpll(3);
            break;
        case DIVIDPLL4:
            clksource =  tca_ckc_getdividpll(4);
            break;
        case DIVIDPLL5:
            clksource =  tca_ckc_getdividpll(5);
            break;
        /*
        case DIVIDXIN:
            clksource =  60000;
            break;
        case DIVIDXTIN:
            clksource =  163;
            break;
        */
        default: return 0;
    }

    return (clksource / (pCLKCTRL->bREG.CONFIG+1));
}

/****************************************************************************************
* FUNCTION :void tca_ckc_setperi(unsigned int periname,unsigned int isenable, unsigned int freq, unsigned int sor)
* DESCRIPTION :
* ***************************************************************************************/
unsigned int tca_ckc_setperi(unsigned int periname,unsigned int isenable, unsigned int freq)
{
    volatile PCLK_XXX_TYPE *pPCLKCTRL_XXX = (PCLK_XXX_TYPE *)((&pCKC->PCLKCTRL00)+periname);
    volatile PCLK_YYY_TYPE *pPCLKCTRL_YYY = (PCLK_YYY_TYPE *)((&pCKC->PCLKCTRL00)+periname);
    volatile PCLK_YYY_TYPE *pPCLKCTRL_ZZZ = (PCLK_ZZZ_TYPE *)((&pCKC->PCLKCTRL00)+periname);
    unsigned int div[MAX_CLK_SRC], div_100[MAX_CLK_SRC], i, searchsrc, overclksrc, dco_shift;
    unsigned int clkmd, clksrc, clkdiv, clkrate, onoff_status;

    if (pPCLKCTRL_XXX->bREG.EN)
        onoff_status = 1;

    searchsrc = 0xFFFFFFFF;
    overclksrc = 0xFFFFFFFF;

    /* PCK_YYY (n=0~63, n== 18,25,26,27,28,29,30,33) */
    if (periname == PERI_HDMIA || periname == PERI_ADAI1 || periname == PERI_ADAM1 || periname == PERI_SPDIF1 ||
        periname == PERI_ADAI0 || periname == PERI_ADAM0 || periname == PERI_SPDIF0 || periname == PERI_ADC)
    {
        if (periname == PERI_ADC) {
            clkmd = 1;        // Divider mode
            clksrc = PCDIRECTXIN;

            searchsrc = 0;
            div[searchsrc] = (XIN_CLK_RATE + freq - 1)/freq;
            clkdiv = div[searchsrc];
            clkrate = XIN_CLK_RATE/clkdiv;
            if (clkdiv > 0)
                clkdiv -= 1;
        }
        else {
            clkmd = 0;        // DCO mode
            if (freq < 65536)
                dco_shift = 0;
            else if (freq < 65536*2)  //  13.1072 MHz
                dco_shift = 1;
            else if (freq < 65536*4)  //  26.2144 MHz
                dco_shift = 2;
            else if (freq < 65536*8)  //  52.4288 MHz
                dco_shift = 3;
            else if (freq < 65536*16) // 104.8596 MHz
                dco_shift = 4;
            else                         // 209.7152 MHz
                dco_shift = 5;

            for (i=0 ; i<MAX_CLK_SRC ; i++) {
                if (stClockSource[i] == 0 || stClockSource[i] == XIN_CLK_RATE)	// remove XIN clock source
                    continue;
                if (stClockSource[i] < freq)
                    continue;
                div_100[i] = ((freq*(65536>>dco_shift))/(stClockSource[i]/100))<<dco_shift;
                if ((div_100[i]%100) > 50) {
                    div[i] = div_100[i]/100 + 1;
                    div_100[i] = 100 - (div_100[i]%100);
                }
                else {
                    div[i] = div_100[i]/100;
                    div_100[i] %= 100;
                }
                if (searchsrc == 0xFFFFFFFF)
                    searchsrc = i;
                else {
                    /* find similar clock */
                    if (div_100[i] < div_100[searchsrc])
                        searchsrc = i;
                }
            }
            if (searchsrc == 0xFFFFFFFF) {
                if (overclksrc == 0xFFFFFFFF) {
                    overclksrc = 0;
                    for (i=1 ; i<MAX_CLK_SRC ; i++) {
                        if (stClockSource[i] > stClockSource[overclksrc])
                            overclksrc = i;
                    }
                }
                searchsrc = overclksrc;
                div[searchsrc] = 1;
            }

            switch(searchsrc) {
                case 0: clksrc = PCDIRECTPLL0; break;
                case 1: clksrc = PCDIRECTPLL1; break;
                case 2: clksrc = PCDIRECTPLL2; break;
                case 3: clksrc = PCDIRECTPLL3; break;
                case 4: clksrc = PCDIRECTPLL4; break;
                case 5: clksrc = PCDIRECTPLL5; break;
                case 6: clksrc = PCDIVIDPLL0; break;
                case 7: clksrc = PCDIVIDPLL1; break;
                case 8: clksrc = PCDIVIDPLL2; break;
                case 9: clksrc = PCDIVIDPLL3; break;
                case 10: clksrc = PCDIVIDPLL4; break;
                case 11: clksrc = PCDIVIDPLL5; break;
                case 12: clksrc = PCDIRECTXIN; break;
                default: return 0;
            }

            clkdiv = div[searchsrc];
            if (clkdiv > 32768)
                clkrate = ((stClockSource[searchsrc]>>dco_shift)*(65536-div[searchsrc]))/(65536>>dco_shift);
            else
                clkrate = ((stClockSource[searchsrc]>>dco_shift)*div[searchsrc])/(65536>>dco_shift);
        }

#if 1
        pPCLKCTRL_YYY->nREG = ((clkmd&0x1)<<31) | ((clksrc&0x1F)<<24) | (clkdiv&0xFFFF);
        if (isenable == ENABLE || (isenable == NOCHANGE && onoff_status))
            pPCLKCTRL_YYY->nREG |= (1<<29);
#else
        pPCLKCTRL_YYY->bREG.MD = clkmd;
        pPCLKCTRL_YYY->bREG.EN = 0;
        pPCLKCTRL_YYY->bREG.DIV = clkdiv;
        pPCLKCTRL_YYY->bREG.SEL = clksrc;

        if (isenable == ENABLE || (isenable == NOCHANGE && onoff_status))
            pPCLKCTRL_YYY->bREG.EN = 1;
#endif
    }
    /* PCK_ZZZ (n=0~63, n== 60,61,62,63) */
    else if (periname == PERI_PKTGEN0 || periname == PERI_PKTGEN1 || periname == PERI_PKTGEN2 || periname == PERI_PKTGEN3) {
        if (freq == 240000 || freq == 120000 || freq == 80000 || freq == 60000 || freq == 40000 || freq == 30000 || freq <= 20000) {
            clksrc = PCDIRECTXIN;
            searchsrc = 0;
            div[searchsrc] = (XIN_CLK_RATE + freq - 1)/freq;
            clkdiv = div[searchsrc];
            clkrate = XIN_CLK_RATE/clkdiv;
            if (clkdiv > 0)
                clkdiv -= 1;
        }
        else {
            for (i=0 ; i<MAX_CLK_SRC ; i++) {
                if (stClockSource[i] == 0)
                    continue;
                if (stClockSource[i] < freq)
                    continue;
                div_100[i] = stClockSource[i]/(freq/100);
                if (div_100[i] > MAX_PERI_DIV*100)
                    div_100[i] = MAX_PERI_DIV*100;
                if ((div_100[i]%100) > 50) {
                    div[i] = div_100[i]/100 + 1;
                    div_100[i] = 100 - (div_100[i]%100);
                }
                else {
                    div[i] = div_100[i]/100;
                    div_100[i] %= 100;
                }
                if (searchsrc == 0xFFFFFFFF)
                    searchsrc = i;
                else {
                    /* find similar clock */
                    if (div_100[i] < div_100[searchsrc])
                        searchsrc = i;
                    /* find even division vlaue */
                    else if(div_100[i] == div_100[searchsrc]) {
                        if (div[searchsrc]%2)
                            searchsrc = i;
                        else if (div[searchsrc] > div[i])
                            searchsrc = i;
                    }
                }
            }
            if (searchsrc == 0xFFFFFFFF) {
                if (overclksrc == 0xFFFFFFFF) {
                    overclksrc = 0;
                    for (i=1 ; i<MAX_CLK_SRC ; i++) {
                        if (stClockSource[i] > stClockSource[overclksrc])
                            overclksrc = i;
                    }
                }
                searchsrc = overclksrc;
                div[searchsrc] = 1;
            }

            switch(searchsrc) {
                case 0: clksrc = PCDIRECTPLL0; break;
                case 1: clksrc = PCDIRECTPLL1; break;
                case 2: clksrc = PCDIRECTPLL2; break;
                case 3: clksrc = PCDIRECTPLL3; break;
                case 4: clksrc = PCDIRECTPLL4; break;
                case 5: clksrc = PCDIRECTPLL5; break;
                case 6: clksrc = PCDIVIDPLL0; break;
                case 7: clksrc = PCDIVIDPLL1; break;
                case 8: clksrc = PCDIVIDPLL2; break;
                case 9: clksrc = PCDIVIDPLL3; break;
                case 10: clksrc = PCDIVIDPLL4; break;
                case 11: clksrc = PCDIVIDPLL5; break;
                case 12: clksrc = PCDIRECTXIN; break;
                default: return 0;
            }

            clkdiv = div[searchsrc];
            clkrate = stClockSource[searchsrc]/clkdiv;
            if (clkdiv > 0)
                clkdiv -= 1;
        }

        if (stPKTGENReg[periname-PERI_PKTGEN0].bREG.EN)
            onoff_status = 1;

        stPKTGENReg[periname-PERI_PKTGEN0].bREG.MD = 1;
        stPKTGENReg[periname-PERI_PKTGEN0].bREG.EN = 0;
        stPKTGENReg[periname-PERI_PKTGEN0].bREG.DIV = clkdiv;
        stPKTGENReg[periname-PERI_PKTGEN0].bREG.SEL = clksrc;
        if (isenable == ENABLE || (isenable == NOCHANGE && onoff_status))
            stPKTGENReg[periname-PERI_PKTGEN0].bREG.EN = 1;

        pPCLKCTRL_ZZZ->nREG = stPKTGENReg[periname-PERI_PKTGEN0].nREG;        
    }
    /* PCK_XXX (n=0~63, n != PCK_YYY, PCK_ZZZ) */
    else {
        if (freq == XTIN_CLK_RATE) {
            if ((periname == PERI_LCD0 || periname == PERI_LCD1)) {
                clksrc = PCDIVIDXTIN_HDMIPCLK;
                searchsrc = 0;
                div[searchsrc] = 0;
                clkdiv = div[searchsrc];
                clkrate = XTIN_CLK_RATE;
            }
            else {
                clksrc = PCDIRECTXTIN;
                searchsrc = 0;
                div[searchsrc] = 0;
                clkdiv = div[searchsrc];
                clkrate = XTIN_CLK_RATE;
            }
        }
#if !defined(CONFIG_HDMI_CLK_USE_INTERNAL_PLL)
        else if (periname == PERI_HDMI && freq == HDMI_CLK_RATE) {
            #if defined(CONFIG_HDMI_CLK_USE_XIN_24MHZ)
            clksrc = PCDIRECTXIN;
            searchsrc = 0;
            div[searchsrc] = 0;
            clkdiv = 0;//div[searchsrc];
            clkrate = XIN_CLK_RATE;
            #else
            clksrc = PCHDMI;
            searchsrc = 0;
            div[searchsrc] = 0;
            clkdiv = div[searchsrc];
            clkrate = HDMI_CLK_RATE;
            #endif /* CONFIG_HDMI_CLK_USE_XIN_24MHZ */
        }
#endif
        else if (freq == 240000 || freq == 120000 || freq == 80000 || freq == 60000 || freq == 40000 || freq == 30000 || freq <= 20000) {
//        if (periname == PERI_I2C0 || periname == PERI_I2C1 || periname == PERI_I2C2 || periname == PERI_I2C3 || periname == PERI_TCZ) {
            clksrc = PCDIRECTXIN;
            searchsrc = 0;
            div[searchsrc] = (XIN_CLK_RATE + freq - 1)/freq;
            clkdiv = div[searchsrc];
            clkrate = XIN_CLK_RATE/clkdiv;
            if (clkdiv > 0)
                clkdiv -= 1;
        }
        else {
            for (i=0 ; i<MAX_CLK_SRC ; i++) {
                if (stClockSource[i] == 0)
                    continue;
                if (stClockSource[i] < freq)
                    continue;
                div_100[i] = stClockSource[i]/(freq/100);
                if (div_100[i] > MAX_PERI_DIV*100)
                    div_100[i] = MAX_PERI_DIV*100;
                if ((div_100[i]%100) > 50) {
                    div[i] = div_100[i]/100 + 1;
                    div_100[i] = 100 - (div_100[i]%100);
                }
                else {
                    div[i] = div_100[i]/100;
                    div_100[i] %= 100;
                }
                if (searchsrc == 0xFFFFFFFF)
                    searchsrc = i;
                else {
                    /* find similar clock */
                    if (div_100[i] < div_100[searchsrc])
                        searchsrc = i;
                    /* find even division vlaue */
                    else if(div_100[i] == div_100[searchsrc]) {
                        if (div[searchsrc]%2)
                            searchsrc = i;
                        else if (div[searchsrc] > div[i])
                            searchsrc = i;
                    }
                }
            }
            if (searchsrc == 0xFFFFFFFF) {
                if (overclksrc == 0xFFFFFFFF) {
                    overclksrc = 0;
                    for (i=1 ; i<MAX_CLK_SRC ; i++) {
                        if (stClockSource[i] > stClockSource[overclksrc])
                            overclksrc = i;
                    }
                }
                searchsrc = overclksrc;
                div[searchsrc] = 1;
            }

            switch(searchsrc) {
                case 0: clksrc = PCDIRECTPLL0; break;
                case 1: clksrc = PCDIRECTPLL1; break;
                case 2: clksrc = PCDIRECTPLL2; break;
                case 3: clksrc = PCDIRECTPLL3; break;
                case 4: clksrc = PCDIRECTPLL4; break;
                case 5: clksrc = PCDIRECTPLL5; break;
                case 6: clksrc = PCDIVIDPLL0; break;
                case 7: clksrc = PCDIVIDPLL1; break;
                case 8: clksrc = PCDIVIDPLL2; break;
                case 9: clksrc = PCDIVIDPLL3; break;
                case 10: clksrc = PCDIVIDPLL4; break;
                case 11: clksrc = PCDIVIDPLL5; break;
                case 12: clksrc = PCDIRECTXIN; break;
                default: return 0;
            }

            clkdiv = div[searchsrc];
            clkrate = stClockSource[searchsrc]/clkdiv;
            if (clkdiv > 0)
                clkdiv -= 1;
        }

        pPCLKCTRL_XXX->bREG.EN = 0;
        pPCLKCTRL_XXX->bREG.DIV = clkdiv;
        pPCLKCTRL_XXX->bREG.SEL = clksrc;
        if (isenable == ENABLE || (isenable == NOCHANGE && onoff_status))
            pPCLKCTRL_XXX->bREG.EN = 1;
    }

    return clkrate;
}

/****************************************************************************************
* FUNCTION : int tca_ckc_getperi(unsigned int periname)
* DESCRIPTION :
* ***************************************************************************************/
unsigned int tca_ckc_getperi(unsigned int periname)
{
    volatile PCLK_XXX_TYPE *pPCLKCTRL_XXX = (PCLK_XXX_TYPE *)((&pCKC->PCLKCTRL00)+periname);
    volatile PCLK_YYY_TYPE *pPCLKCTRL_YYY = (PCLK_YYY_TYPE *)((&pCKC->PCLKCTRL00)+periname);
    volatile PCLK_ZZZ_TYPE *pPCLKCTRL_ZZZ = (PCLK_ZZZ_TYPE *)((&pCKC->PCLKCTRL00)+periname);
    unsigned int lclksource = 0;

    if (periname == PERI_PKTGEN0 || periname == PERI_PKTGEN1 || periname == PERI_PKTGEN2 || periname == PERI_PKTGEN3) {
        pPCLKCTRL_XXX = (PCLK_XXX_TYPE *)(&stPKTGENReg[periname-PERI_PKTGEN0]);
        pPCLKCTRL_ZZZ = (PCLK_ZZZ_TYPE *)(&stPKTGENReg[periname-PERI_PKTGEN0]);
    }

    if (pPCLKCTRL_XXX->bREG.EN == 0)
        return 0;

    switch(pPCLKCTRL_XXX->bREG.SEL) {
        case PCDIRECTPLL0 :
            lclksource =  tca_ckc_getpll(0);
            break;
        case PCDIRECTPLL1 :
            lclksource =  tca_ckc_getpll(1);
            break;
        case PCDIRECTPLL2 :
            lclksource =  tca_ckc_getpll(2);
            break;
        case PCDIRECTPLL3 :
            lclksource =  tca_ckc_getpll(3);
            break;
        case PCDIRECTXIN :
            lclksource =  XIN_CLK_RATE;
            break;
        case PCDIVIDPLL0:
            lclksource =  tca_ckc_getdividpll(0);
            break;
        case PCDIVIDPLL1:
            lclksource =  tca_ckc_getdividpll(1);
            break;
        case PCDIVIDPLL2:
            lclksource =  tca_ckc_getdividpll(2);
            break;
        case PCDIVIDPLL3:
            lclksource =  tca_ckc_getdividpll(3);
            break;
        case PCDIRECTXTIN:
            lclksource =  XTIN_CLK_RATE;
            break;
        /*
        case PCEXITERNAL:
            lclksource =  tca_ckc_getpll(3);
            break;
        case PCDIVIDXIN_HDMITMDS:
            lclksource =  tca_ckc_getpll(3);
            break;
        case PCDIVIDXTIN_HDMIPCLK:
            lclksource =  tca_ckc_getpll(3);
            break;
        */
        case PCHDMI:
            lclksource =  HDMI_CLK_RATE;
            break;
        /*
        case PCSATA:
            lclksource =  tca_ckc_getpll(3);
            break;
        case PCUSBPHY:
            lclksource =  tca_ckc_getpll(3);
            break;
        case PCDIVIDXIN:
            lclksource =  60000;
            break;
        case PCDIVIDXTIN:
            lclksource =  163;
            break;
        */
        case PCDIRECTPLL4:
            lclksource =  tca_ckc_getpll(4);
            break;
        case PCDIRECTPLL5:
            lclksource =  tca_ckc_getpll(5);
            break;
        case PCDIVIDPLL4:
            lclksource =  tca_ckc_getdividpll(4);
            break;
        case PCDIVIDPLL5:
            lclksource =  tca_ckc_getdividpll(5);
            break;
        default :
            return 0;
    }

    /* PCLK_YYY */
    if (periname == PERI_HDMIA || periname == PERI_ADAI1 || periname == PERI_ADAM1 || periname == PERI_SPDIF1 ||
        periname == PERI_ADAI0 || periname == PERI_ADAM0 || periname == PERI_SPDIF0 || periname == PERI_ADC)
    {
        if (pPCLKCTRL_YYY->nREG & (1<<31)) {
            return (lclksource/((pPCLKCTRL_YYY->nREG&0xFFFF)+1));
        }
        else {
            if (pPCLKCTRL_YYY->bREG.DIV > 32768)
                return ((lclksource * (65536 - (pPCLKCTRL_YYY->nREG&0xFFFF))) / 65536);
            else
                return ((lclksource * (pPCLKCTRL_YYY->nREG&0xFFFF)) / 65536);
        }
    }
    /* PCK_ZZZ (n=0~63, n== 60,61,62,63) */
    else if (periname == PERI_PKTGEN0 || periname == PERI_PKTGEN1 || periname == PERI_PKTGEN2 || periname == PERI_PKTGEN3) {
        if (pPCLKCTRL_ZZZ->nREG & (1<<31)) {
            return (lclksource/((pPCLKCTRL_ZZZ->nREG&0xFFFFFF)+1));
        }
        else {
            return 0;
        }
    }
    else {
        return (lclksource/(pPCLKCTRL_XXX->bREG.DIV+1));
    }

    return 0;
}

/****************************************************************************************
* FUNCTION :void tca_ckc_setpmuippwdn( unsigned int sel , unsigned int ispwdn)
* DESCRIPTION :
*   - fbusname : IP Isolation index
*   - ispwdn : (1:pwdn, 0:wkup)
* ***************************************************************************************/
int tca_ckc_setippwdn( unsigned int sel, unsigned int ispwdn)
{
    switch(sel) {
//      case PMU_ISOL_OTP:    /* Controlled by PM */
//      case PMU_ISOL_RTC:    /* Controlled by PM */
//      case PMU_ISOL_PLL:    /* Controlled by PM */
//      case PMU_ISOL_ECID:   /* Controlled by PM */
        case PMU_ISOL_HDMI:
        case PMU_ISOL_VDAC:
        case PMU_ISOL_TSADC:
        case PMU_ISOL_USBHP:
        case PMU_ISOL_USBOP:
        case PMU_ISOL_LVDS:
            if (ispwdn)
                stIPPWDNReg |= (1<<sel);
            else
                stIPPWDNReg &= ~(1<<sel);

            pPMU->PMU_ISOL.nREG = stIPPWDNReg;
            break;
        default:
            return -1;
    }
    return 0;
}

/****************************************************************************************
* FUNCTION :void tca_ckc_setfbuspwdn( unsigned int fbusname , unsigned int ispwdn)
* DESCRIPTION :
*   - fbusname : CLKCTRL(n) index
*   - ispwdn : (1:pwdn, 0:wkup)
* ***************************************************************************************/
int tca_ckc_setfbuspwdn( unsigned int fbusname, unsigned int ispwdn)
{
    while(pPMU->PMU_PWRSTS.bREG.MAIN_STATE);

    switch (fbusname) {
        case FBUS_MEM:
            if (ispwdn) {
                do {
                    pPMU->PWRDN_MBUS.bREG.DATA = 1;
                } while (pPMU->PMU_PWRSTS.bREG.PD_MB == 0);
            }
            else {
                do {
                    pPMU->PWRUP_MBUS.bREG.DATA = 1;
                } while (pPMU->PMU_PWRSTS.bREG.PU_MB == 0);
            }
            break;
        case FBUS_DDI:
            if (ispwdn) {
                do {
                    pMEMBUSCFG->HCLKMASK.bREG.DBUS = 0;
                } while ((pMEMBUSCFG->MBUSSTS.bREG.DBUS0 | pMEMBUSCFG->MBUSSTS.bREG.DBUS1) == 1);

                pPMU->PMU_SYSRST.bREG.DB = 0;
                do {
                    pPMU->PWRDN_DBUS.bREG.DATA = 1;
                } while (pPMU->PMU_PWRSTS.bREG.PD_DB == 0);
            }
            else {
                do {
                    pPMU->PWRUP_DBUS.bREG.DATA = 1;
                } while (pPMU->PMU_PWRSTS.bREG.PU_DB == 0);
                pPMU->PMU_SYSRST.bREG.DB = 1;

                do {
                    pMEMBUSCFG->HCLKMASK.bREG.DBUS = 1;
                } while ((pMEMBUSCFG->MBUSSTS.bREG.DBUS0 & pMEMBUSCFG->MBUSSTS.bREG.DBUS1) == 0);
            }
            break;
        case FBUS_GPU:
            if (ispwdn) {
                do {
                    pMEMBUSCFG->HCLKMASK.bREG.GBUS = 0;
                } while (pMEMBUSCFG->MBUSSTS.bREG.GBUS == 1);

                pPMU->PMU_SYSRST.bREG.GB = 0;
                do {
                    pPMU->PWRDN_GBUS.bREG.DATA = 1;
                } while (pPMU->PMU_PWRSTS.bREG.PD_GB == 0);
            }
            else {
                do {
                    pPMU->PWRUP_GBUS.bREG.DATA = 1;
                } while (pPMU->PMU_PWRSTS.bREG.PU_GB == 0);
                pPMU->PMU_SYSRST.bREG.GB = 1;

                do {
                    pMEMBUSCFG->HCLKMASK.bREG.GBUS = 1;
                } while (pMEMBUSCFG->MBUSSTS.bREG.GBUS == 0);
            }
            break;
        case FBUS_VBUS:
            if (ispwdn) {
                do {pMEMBUSCFG->HCLKMASK.bREG.VBUS = 0;
                } while ((pMEMBUSCFG->MBUSSTS.bREG.VBUS0 | pMEMBUSCFG->MBUSSTS.bREG.VBUS1) == 1);

                pPMU->PMU_SYSRST.bREG.VB = 0;
                do {
                    pPMU->PWRDN_VBUS.bREG.DATA = 1;
                } while (pPMU->PMU_PWRSTS.bREG.PD_VB == 0);
            }
            else {
                do {
                    pPMU->PWRUP_VBUS.bREG.DATA = 1;
                } while (pPMU->PMU_PWRSTS.bREG.PU_VB == 0);
                pPMU->PMU_SYSRST.bREG.VB = 1;

                do {
                    pMEMBUSCFG->HCLKMASK.bREG.VBUS = 1;
                } while ((pMEMBUSCFG->MBUSSTS.bREG.VBUS0 & pMEMBUSCFG->MBUSSTS.bREG.VBUS1) == 0);
            }
            break;
        case FBUS_HSIO:
            if (ispwdn) {
                do {
                    pMEMBUSCFG->HCLKMASK.bREG.HSIOBUS = 0;
                } while (pMEMBUSCFG->MBUSSTS.bREG.HSIOBUS == 1);

                pPMU->PMU_SYSRST.bREG.HSB = 0;
                do {
                    pPMU->PWRDN_HSBUS.bREG.DATA = 1;
                } while (pPMU->PMU_PWRSTS.bREG.PD_HSB == 0);
            }
            else {
                do {
                    pPMU->PWRUP_HSBUS.bREG.DATA = 1;
                } while (pPMU->PMU_PWRSTS.bREG.PU_HSB == 0);
                pPMU->PMU_SYSRST.bREG.HSB = 1;

                do {
                    pMEMBUSCFG->HCLKMASK.bREG.HSIOBUS = 1;
                } while (pMEMBUSCFG->MBUSSTS.bREG.HSIOBUS == 0);
            }
            break;
        default:
            return -1;
    }

    return 0;
}

/****************************************************************************************
* FUNCTION :void tca_ckc_getfbuspwdn( unsigned int fbusname)
* DESCRIPTION :
*   - fbusname : CLKCTRL(n) index
*   - return : 1:pwdn, 0:wkup
* ***************************************************************************************/
int tca_ckc_getfbuspwdn( unsigned int fbusname)
{
    int retVal = 0;

    switch (fbusname) {
        case FBUS_MEM:
            if (pPMU->PMU_PWRSTS.bREG.PU_MB)
                retVal = 1;
            break;
        case FBUS_DDI:
            if (pPMU->PMU_PWRSTS.bREG.PU_DB)
                retVal = 1;
            break;
        case FBUS_GPU:
            if (pPMU->PMU_PWRSTS.bREG.PU_GB)
                retVal = 1;
            break;
//        case FBUS_VCORE:
        case FBUS_VBUS:
            if (pPMU->PMU_PWRSTS.bREG.PU_VB)
                retVal = 1;
            break;
        case FBUS_HSIO:
            if (pPMU->PMU_PWRSTS.bREG.PU_HSB)
                retVal = 1;
            break;
        default :
            retVal = -1;
            break;
    }

     return retVal;
}

/****************************************************************************************
* FUNCTION :void tca_ckc_setswreset(unsigned int fbus_name, unsigned int isreset)
* DESCRIPTION :
* ***************************************************************************************/
int tca_ckc_setfbusswreset(unsigned int fbusname, unsigned int isreset)
{
    unsigned long old_value, ctrl_value;

    return 0;

    old_value = (0x1FF | ~(pCKC->SWRESET.nREG));

    switch (fbusname) {
        case FBUS_DDI:
            ctrl_value = 0x00000004;
            break;
        case FBUS_GPU:
            ctrl_value = 0x00000008;
            break;
        case FBUS_VBUS:
//        case FBUS_VCORE:  
            ctrl_value = 0x00000060;
            break;
        case FBUS_HSIO:
            ctrl_value = 0x00000080;
            break;
        case FBUS_CM3:
            ctrl_value = 0x00000100;
            break;
        default:
            return -1;
    }

    if (isreset)
        pCKC->SWRESET.nREG = old_value & ~ctrl_value;
    else
        pCKC->SWRESET.nREG = old_value | ctrl_value;

    return 0;
}

/****************************************************************************************
* FUNCTION :  int tca_ckc_setiopwdn(unsigned int sel, unsigned int ispwdn)
* DESCRIPTION :
* ***************************************************************************************/
int tca_ckc_setiopwdn(unsigned int sel, unsigned int ispwdn)
{
    if (sel >= RB_MAX)
        return -1;

    if (pCKC->CLKCTRL4.bREG.EN == 0)
        return -2;

    if (sel >= 32) {
        if (ispwdn)
            pIOBUSCFG->HCLKEN1.nREG &= ~(0x1 << (sel-32));
        else
            pIOBUSCFG->HCLKEN1.nREG |= (0x1 << (sel-32));
    }
    else {
        if (ispwdn)
            pIOBUSCFG->HCLKEN0.nREG &= ~(0x1 << sel);
        else
            pIOBUSCFG->HCLKEN0.nREG |= (0x1 << sel);
    }

    return 0;
}

/****************************************************************************************
* FUNCTION :  int tca_ckc_getiobus(unsigned int sel)
* DESCRIPTION :
*   - return : (1:pwdn, 0:wkup)
* ***************************************************************************************/
int tca_ckc_getiopwdn(unsigned int sel)
{
    int retVal = 0;

    if (sel >= RB_MAX)
        return -1;

    if (pCKC->CLKCTRL4.bREG.EN == 0)
        return -2;

    if (sel >= 32) {
        if (pIOBUSCFG->HCLKEN1.nREG & (0x1 << (sel-32)))
            retVal = 0;
        else
            retVal = 1;
    }
    else {
        if (pIOBUSCFG->HCLKEN0.nREG & (0x1 << sel))
            retVal = 0;
        else
            retVal = 1;
    }

    return retVal;
}

/****************************************************************************************
* FUNCTION :unsigned int tca_ckc_setioswreset(unsigned int sel, unsigned int isreset)
* DESCRIPTION :
* ***************************************************************************************/
int tca_ckc_setioswreset(unsigned int sel, unsigned int isreset)
{
    if (sel >= RB_MAX)
        return -1;

    if (pCKC->CLKCTRL4.bREG.EN == 0)
        return -2;

    if (sel >= 32) {
        if (isreset)
            pIOBUSCFG->HRSTEN1.nREG &= ~(0x1 << (sel-32));
        else
            pIOBUSCFG->HRSTEN1.nREG |= (0x1 << (sel-32));
    }
    else {
        if (isreset)
            pIOBUSCFG->HRSTEN0.nREG &= ~(0x1 << sel);
        else
            pIOBUSCFG->HRSTEN0.nREG |= (0x1 << sel);
    }

    return 0;
}

/****************************************************************************************
* FUNCTION : void tca_ckc_setddipwdn(unsigned int sel , unsigned int ispwdn)
* DESCRIPTION :
* ***************************************************************************************/
int tca_ckc_setddipwdn(unsigned int sel , unsigned int ispwdn)
{
    if (sel >= DDIBUS_MAX)
        return -1;

    if (pCKC->CLKCTRL2.bREG.EN == 0)
        return -2;

    if (ispwdn)
        pDDIBUSCFG->PWDN.nREG &= ~(0x1 << sel);
    else
        pDDIBUSCFG->PWDN.nREG |= (0x1 << sel);

    return 0;
}

/****************************************************************************************
* FUNCTION : int tca_ckc_getddipwdn(unsigned int sel)
* DESCRIPTION :
*   - return : (1:pwdn, 0:wkup)
* ***************************************************************************************/
int tca_ckc_getddipwdn(unsigned int sel)
{
    int retVal = 0;

    if (sel >= DDIBUS_MAX)
        return -1;

    if (pCKC->CLKCTRL2.bREG.EN == 0)
        return -2;

    if (pDDIBUSCFG->PWDN.nREG & (0x1 << sel))
        retVal = 0;
    else
        retVal = 1;

    return retVal;
}

/****************************************************************************************
* FUNCTION :unsigned int tca_ckc_setddiswreset(unsigned int sel, unsigned int isreset)
* DESCRIPTION :
* ***************************************************************************************/
int tca_ckc_setddiswreset(unsigned int sel, unsigned int isreset)
{
    if (sel >= DDIBUS_MAX)
        return -1;

    if (pCKC->CLKCTRL2.bREG.EN == 0)
        return -2;

    if (isreset)
        pDDIBUSCFG->SWRESET.nREG &= ~(0x1 << sel);
    else
        pDDIBUSCFG->SWRESET.nREG |= (0x1 << sel);

    return 0;
}

/****************************************************************************************
* FUNCTION : void tca_ckc_setgpupwdn(unsigned int sel , unsigned int ispwdn)
* DESCRIPTION :
* ***************************************************************************************/
int tca_ckc_setgpupwdn(unsigned int sel , unsigned int ispwdn)
{
    if (sel >= GPUBUS_MAX)
        return -1;

    if (pCKC->CLKCTRL3.bREG.EN == 0)
        return -2;

    if (ispwdn)
        pGPUBUSCFG->PWDN.nREG &= ~(0x1 << sel);
    else
        pGPUBUSCFG->PWDN.nREG |= (0x1 << sel);

    return 0;
}

/****************************************************************************************
* FUNCTION : int tca_ckc_getgpupwdn(unsigned int sel)
* DESCRIPTION :
*   - return : (1:pwdn, 0:wkup)
* ***************************************************************************************/
int tca_ckc_getgpupwdn(unsigned int sel)
{
    int retVal = 0;

    if (sel >= GPUBUS_MAX)
        return -1;

    if (pCKC->CLKCTRL3.bREG.EN == 0)
        return -2;

    if (pGPUBUSCFG->PWDN.nREG & (0x1 << sel))
        retVal = 0;
    else
        retVal = 1;

    return retVal;
}

/****************************************************************************************
* FUNCTION :unsigned int tca_ckc_setgpuswreset(unsigned int sel, unsigned int isreset)
* DESCRIPTION :
* ***************************************************************************************/
int tca_ckc_setgpuswreset(unsigned int sel, unsigned int isreset)
{
    if (sel >= GPUBUS_MAX)
        return -1;

    if (pCKC->CLKCTRL3.bREG.EN == 0)
        return -2;

    if (isreset)
        pGPUBUSCFG->SWRESET.nREG &= ~(0x1 << sel);
    else
        pGPUBUSCFG->SWRESET.nREG |= (0x1 << sel);

    return 0;
}

/****************************************************************************************
* FUNCTION : void tca_ckc_setvideopwdn(unsigned int sel , unsigned int ispwdn)
* DESCRIPTION :
* ***************************************************************************************/
int tca_ckc_setvideopwdn(unsigned int sel , unsigned int ispwdn)
{
    if (sel >= VIDEOBUS_MAX)
        return -1;

    if (pCKC->CLKCTRL5.bREG.EN == 0)
        return -2;

    if (ispwdn)
        pVIDEOBUSCFG->PWDN.nREG &= ~(0x1 << sel);
    else
        pVIDEOBUSCFG->PWDN.nREG |= (0x1 << sel);
    
    return 0;
}

/****************************************************************************************
* FUNCTION : int tca_ckc_getvideopwdn(unsigned int sel)
* DESCRIPTION :
*   - return : (1:pwdn, 0:wkup)
* ***************************************************************************************/
int tca_ckc_getvideopwdn(unsigned int sel)
{
    int retVal = 0;

    if (sel >= VIDEOBUS_MAX)
        return -1;

    if (pCKC->CLKCTRL5.bREG.EN == 0)
        return -2;

    if (pVIDEOBUSCFG->PWDN.nREG & (0x1 << sel))
        retVal = 0;
    else
        retVal = 1;

    return retVal;
}

/****************************************************************************************
* FUNCTION :unsigned int tca_ckc_setvideoswreset(unsigned int sel, unsigned int isreset)
* DESCRIPTION :
* ***************************************************************************************/
int tca_ckc_setvideoswreset(unsigned int sel, unsigned int isreset)
{
    if (sel >= VIDEOBUS_MAX)
        return -1;

    if (pCKC->CLKCTRL5.bREG.EN == 0)
        return -2;

    if (isreset)
        pVIDEOBUSCFG->SWRESET.nREG &= ~(0x1 << sel);
    else
        pVIDEOBUSCFG->SWRESET.nREG |= (0x1 << sel);

    return 0;
}

/****************************************************************************************
* FUNCTION : void tca_ckc_sethsiopwdn(unsigned int sel , unsigned int ispwdn)
* DESCRIPTION :
* ***************************************************************************************/
int tca_ckc_sethsiopwdn(unsigned int sel , unsigned int ispwdn)
{
    if (sel >= HSIOBUS_MAX)
        return -1;

    if (pCKC->CLKCTRL7.bREG.EN == 0)
        return -2;

    if (ispwdn)
        pHSIOBUSCFG->PWDN.nREG &= ~(0x1 << sel);
    else
        pHSIOBUSCFG->PWDN.nREG |= (0x1 << sel);

    return 0;
}

/****************************************************************************************
* FUNCTION : int tca_ckc_gethsiopwdn(unsigned int sel)
* DESCRIPTION : Power Down Register of DDI_CONFIG
*   - return : (1:pwdn, 0:wkup)
* ***************************************************************************************/
int tca_ckc_gethsiopwdn(unsigned int sel)
{
    int retVal = 0;

    if (sel >= HSIOBUS_MAX)
        return -1;

    if (pCKC->CLKCTRL7.bREG.EN == 0)
        return -2;

    if (pHSIOBUSCFG->PWDN.nREG & (0x1 << sel))
        retVal = 0;
    else
        retVal = 1;

    return retVal;
}

/****************************************************************************************
* FUNCTION :unsigned int tca_ckc_sethsioswreset(unsigned int sel, unsigned int isreset)
* DESCRIPTION :
* ***************************************************************************************/
int tca_ckc_sethsioswreset(unsigned int sel, unsigned int isreset)
{
    if (sel >= HSIOBUS_MAX)
        return -1;

    if (pCKC->CLKCTRL7.bREG.EN == 0)
        return -2;

    if (isreset)
        pHSIOBUSCFG->SWRESET.nREG &= ~(0x1 << sel);
    else
        pHSIOBUSCFG->SWRESET.nREG |= (0x1 << sel);

    return 0;
}

/****************************************************************************************
* FUNCTION : void tca_ckc_setcm3pwdn(unsigned int sel , unsigned int ispwdn)
* DESCRIPTION :
* ***************************************************************************************/
int tca_ckc_setcm3pwdn(unsigned int sel , unsigned int ispwdn)
{
    if (sel >= M3HCLK_MAX)
        return -1;

    if (pCKC->CLKCTRL10.bREG.EN == 0)
        return -2;

#if 0
    if (ispwdn)
        pHSIOBUSCFG->PWDN.nREG &= ~(0x1 << sel);
    else
        pHSIOBUSCFG->PWDN.nREG |= (0x1 << sel);
#endif

    return 0;
}

/****************************************************************************************
* FUNCTION : int tca_ckc_getcm3pwdn(unsigned int sel)
* DESCRIPTION : Power Down Register of DDI_CONFIG
*   - return : (1:pwdn, 0:wkup)
* ***************************************************************************************/
int tca_ckc_getcm3pwdn(unsigned int sel)
{
    int retVal = 0;

    if (sel >= M3HCLK_MAX)
        return -1;

    if (pCKC->CLKCTRL10.bREG.EN == 0)
        return -2;

#if 0
    if (pHSIOBUSCFG->PWDN.nREG & (0x1 << sel))
        retVal = 0;
    else
        retVal = 1;
#endif

    return retVal;
}

/****************************************************************************************
* FUNCTION :unsigned int tca_ckc_setcm3swreset(unsigned int sel, unsigned int isreset)
* DESCRIPTION :
* ***************************************************************************************/
int tca_ckc_setcm3swreset(unsigned int sel, unsigned int isreset)
{
    if (sel >= M3RESET_MAX)
        return -1;

    if (pCKC->CLKCTRL10.bREG.EN == 0)
        return -2;

#if 0
    if (isreset)
        pHSIOBUSCFG->SWRESET.nREG &= ~(0x1 << sel);
    else
        pHSIOBUSCFG->SWRESET.nREG |= (0x1 << sel);
#endif

    return 0;
}

int tca_ckc_fclk_enable(unsigned int fclk, unsigned int enable)
{
    volatile CLKCTRL_TYPE *pCLKCTRL;
    pCLKCTRL = (volatile CLKCTRL_TYPE *)((&pCKC->CLKCTRL0)+fclk);

    if (enable)
        pCLKCTRL->bREG.EN = 1;
    else
        pCLKCTRL->bREG.EN = 0;
    while(pCLKCTRL->bREG.CFGREQ);

    return 0;
}

int tca_ckc_pclk_enable(unsigned int pclk, unsigned int enable)
{
    volatile PCLK_XXX_TYPE *pPERI;
    pPERI = (volatile PCLK_XXX_TYPE *)((&pCKC->PCLKCTRL00)+pclk);

    if (pclk == PERI_PKTGEN0 || pclk == PERI_PKTGEN1 || pclk == PERI_PKTGEN2 || pclk == PERI_PKTGEN3) {
        if (enable)
            stPKTGENReg[pclk-PERI_PKTGEN0].bREG.EN = 1;
        else
            stPKTGENReg[pclk-PERI_PKTGEN0].bREG.EN = 0;
        pPERI->nREG = stPKTGENReg[pclk-PERI_PKTGEN0].nREG;
    }
    else {
        if (enable)
            pPERI->bREG.EN = 1;
        else
            pPERI->bREG.EN = 0;
    }

    return 0;
}


/****************************************************************************************
* EXPORT_SYMBOL clock functions for Linux
* ***************************************************************************************/
#if defined(_LINUX_)

EXPORT_SYMBOL(tca_ckc_init);
EXPORT_SYMBOL(tca_ckc_setpll);
EXPORT_SYMBOL(tca_ckc_getpll);
EXPORT_SYMBOL(tca_ckc_getdividpll);
EXPORT_SYMBOL(tca_ckc_setfbusctrl);
EXPORT_SYMBOL(tca_ckc_getfbusctrl);
EXPORT_SYMBOL(tca_ckc_setperi);
EXPORT_SYMBOL(tca_ckc_getperi);
EXPORT_SYMBOL(tca_ckc_setippwdn);
EXPORT_SYMBOL(tca_ckc_setfbuspwdn);
EXPORT_SYMBOL(tca_ckc_getfbuspwdn);
EXPORT_SYMBOL(tca_ckc_setfbusswreset);
EXPORT_SYMBOL(tca_ckc_setiopwdn);
EXPORT_SYMBOL(tca_ckc_getiopwdn);
EXPORT_SYMBOL(tca_ckc_setioswreset);
EXPORT_SYMBOL(tca_ckc_setddipwdn);
EXPORT_SYMBOL(tca_ckc_getddipwdn);
EXPORT_SYMBOL(tca_ckc_setddiswreset);
EXPORT_SYMBOL(tca_ckc_setgpupwdn);
EXPORT_SYMBOL(tca_ckc_getgpupwdn);
EXPORT_SYMBOL(tca_ckc_setgpuswreset);
EXPORT_SYMBOL(tca_ckc_setvideopwdn);
EXPORT_SYMBOL(tca_ckc_getvideopwdn);
EXPORT_SYMBOL(tca_ckc_setvideoswreset);
EXPORT_SYMBOL(tca_ckc_sethsiopwdn);
EXPORT_SYMBOL(tca_ckc_gethsiopwdn);
EXPORT_SYMBOL(tca_ckc_sethsioswreset);
EXPORT_SYMBOL(tca_ckc_fclk_enable);
EXPORT_SYMBOL(tca_ckc_pclk_enable);

#endif

/* end of file */
