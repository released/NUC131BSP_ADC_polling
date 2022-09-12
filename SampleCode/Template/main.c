/*_____ I N C L U D E S ____________________________________________________*/
#include <stdio.h>
#include <string.h>
#include "NUC131.h"

#include "project_config.h"


/*_____ D E C L A R A T I O N S ____________________________________________*/

/*_____ D E F I N I T I O N S ______________________________________________*/
volatile uint32_t BitFlag = 0;
volatile uint32_t counter_tick = 0;
volatile uint32_t counter_systick = 0;

#define PLL_CLOCK           50000000

#if defined (ENABLE_TICK_EVENT)
typedef void (*sys_pvTimeFunPtr)(void);   /* function pointer */
typedef struct timeEvent_t
{
    uint8_t             active;
    unsigned long       initTick;
    unsigned long       curTick;
    sys_pvTimeFunPtr    funPtr;
} TimeEvent_T;

#define TICKEVENTCOUNT                                 (8)                   
volatile  TimeEvent_T tTimerEvent[TICKEVENTCOUNT];
volatile uint8_t _sys_uTimerEventCount = 0;             /* Speed up interrupt reponse time if no callback function */
#endif

uint32_t u32AVDDVoltage = 0;

#define VBG_VOLTAGE (1200) /* 1.20V = 1200 mV (Typical band-gap voltage of NUC131 series) */
#define ADC_SAMPLE_COUNT 128 /* The last line of GetAVDDCodeByADC() need revise when ADC_SAMPLE_COUNT is changed. */


/*_____ M A C R O S ________________________________________________________*/

/*_____ F U N C T I O N S __________________________________________________*/

void systick_counter(void)
{
	counter_systick++;
}

uint32_t get_systick(void)
{
	return (counter_systick);
}

void set_systick(uint32_t t)
{
	counter_systick = t;
}

void tick_counter(void)
{
	counter_tick++;
}

uint32_t get_tick(void)
{
	return (counter_tick);
}

void set_tick(uint32_t t)
{
	counter_tick = t;
}

void compare_buffer(uint8_t *src, uint8_t *des, int nBytes)
{
    uint16_t i = 0;	
	
    for (i = 0; i < nBytes; i++)
    {
        if (src[i] != des[i])
        {
            printf("error idx : %4d : 0x%2X , 0x%2X\r\n", i , src[i],des[i]);
			set_flag(flag_error , ENABLE);
        }
    }

	if (!is_flag_set(flag_error))
	{
    	printf("%s finish \r\n" , __FUNCTION__);	
		set_flag(flag_error , DISABLE);
	}

}

void reset_buffer(void *dest, unsigned int val, unsigned int size)
{
    uint8_t *pu8Dest;
//    unsigned int i;
    
    pu8Dest = (uint8_t *)dest;

	#if 1
	while (size-- > 0)
		*pu8Dest++ = val;
	#else
	memset(pu8Dest, val, size * (sizeof(pu8Dest[0]) ));
	#endif
	
}

void copy_buffer(void *dest, void *src, unsigned int size)
{
    uint8_t *pu8Src, *pu8Dest;
    unsigned int i;
    
    pu8Dest = (uint8_t *)dest;
    pu8Src  = (uint8_t *)src;


	#if 0
	  while (size--)
	    *pu8Dest++ = *pu8Src++;
	#else
    for (i = 0; i < size; i++)
        pu8Dest[i] = pu8Src[i];
	#endif
}

void dump_buffer(uint8_t *pucBuff, int nBytes)
{
    uint16_t i = 0;
    
    printf("dump_buffer : %2d\r\n" , nBytes);    
    for (i = 0 ; i < nBytes ; i++)
    {
        printf("0x%2X," , pucBuff[i]);
        if ((i+1)%8 ==0)
        {
            printf("\r\n");
        }            
    }
    printf("\r\n\r\n");
}

void dump_buffer_hex(uint8_t *pucBuff, int nBytes)
{
    int     nIdx, i;

    nIdx = 0;
    while (nBytes > 0)
    {
        printf("0x%04X  ", nIdx);
        for (i = 0; i < 16; i++)
            printf("%02X ", pucBuff[nIdx + i]);
        printf("  ");
        for (i = 0; i < 16; i++)
        {
            if ((pucBuff[nIdx + i] >= 0x20) && (pucBuff[nIdx + i] < 127))
                printf("%c", pucBuff[nIdx + i]);
            else
                printf(".");
            nBytes--;
        }
        nIdx += 16;
        printf("\n");
    }
    printf("\n");
}



void delay_ms(uint16_t ms)
{
	TIMER_Delay(TIMER0, 1000*ms);
}

uint16_t get_ADC_Channel (uint8_t ch)
{
    uint16_t  val = 0;

    /* Clear the A/D interrupt flag for safe */
    ADC_CLR_INT_FLAG(ADC, ADC_ADF_INT);

    /* Start A/D conversion */
    ADC_START_CONV(ADC);

    /* Wait conversion done */
    while(!ADC_GET_INT_FLAG(ADC, ADC_ADF_INT));

    /* Clear the A/D interrupt flag for safe */
    ADC_CLR_INT_FLAG(ADC, ADC_ADF_INT);

    __WFI();
    while(ADC_IS_DATA_VALID(ADC, ch) == 0);     

    val = ADC_GET_CONVERSION_DATA(ADC, ch);

    printf("0X%4X:adc voltage is %dmV if Reference voltage is %4d mv\n", val ,(u32AVDDVoltage*val)/4095 , u32AVDDVoltage);

    return (val);    
}

uint32_t GetAVDDCodeByADC(void)
{
    uint32_t u32Count, u32Sum, u32Data;

    /* Unlock protected registers */
    SYS_UnlockReg();

    /* Set ADC VREF is from AVDD */
    SYS->VREFCR = 0x10;

    /* Lock protected registers */
    SYS_LockReg();

    /* Power on ADC */
    ADC_POWER_ON(ADC);

    /* Configure ADC: single-end input, single scan mode, enable ADC analog circuit. */
    ADC_Open(ADC, ADC_ADCR_DIFFEN_SINGLE_END, ADC_ADCR_ADMD_SINGLE, BIT7);
    /* Configure the analog input source of channel 7 as internal band-gap voltage */
    ADC_CONFIG_CH7(ADC, ADC_ADCHER_PRESEL_INT_BANDGAP);

    /* Clear conversion finish flag */
    ADC_CLR_INT_FLAG(ADC, ADC_ADF_INT);

    u32Sum = 0;

    /* sample times are according to ADC_SAMPLE_COUNT definition */
    for(u32Count = 0; u32Count < ADC_SAMPLE_COUNT; u32Count++)
    {
        /* Delay for band-gap voltage stability */
        CLK_SysTickDelay(100);

        /* Start A/D conversion */
        ADC_START_CONV(ADC);

        u32Data = 0;

        /* Wait conversion done */
        while(!ADC_GET_INT_FLAG(ADC, ADC_ADF_INT));

        /* Clear the A/D interrupt flag for safe */
        ADC_CLR_INT_FLAG(ADC, ADC_ADF_INT);

        __WFI();
        while(ADC_IS_DATA_VALID(ADC, 7) == 0);     

        /* Get the conversion result */
        u32Data = ADC_GET_CONVERSION_DATA(ADC, 7);
        /* Sum each conversion data */
        u32Sum += u32Data;
    }

    /* Disable ADC */
    ADC_POWER_DOWN(ADC);

    /* Return the average of ADC_SAMPLE_COUNT samples */
    return (u32Sum >> 7);
}

uint32_t GetAVDDVoltage(void)
{
    uint32_t  u32ConversionResult;
    uint64_t u64MvAVDD;

    /* Calculate Vref by using conversion result of VBG */
    u32ConversionResult = GetAVDDCodeByADC();

    /* u32ConversionResult = VBG * 4096 / Vref, Vref = AVDD */
    /* => AVDD = VBG * 4096 / u32ConversionResult */
    u64MvAVDD = (VBG_VOLTAGE << 12) / (uint64_t)u32ConversionResult;

    printf("Conversion result: 0x%X\n", u32ConversionResult);

    return (uint32_t)u64MvAVDD;
}


void ADC_Init(void)
{
    u32AVDDVoltage = GetAVDDVoltage();
    printf("AVDD Voltage: %dmV\r\n", u32AVDDVoltage);

    /* Power on ADC module */
    ADC_POWER_ON(ADC);

    /* Set the ADC operation mode as single, input mode as single-end and enable the analog input channel 3 */
    ADC_Open(ADC, ADC_ADCR_DIFFEN_SINGLE_END, ADC_ADCR_ADMD_SINGLE, BIT4 );

    get_ADC_Channel(4);
}

void GPIO_Init (void)
{
    SYS->GPB_MFP &= ~(SYS_GPB_MFP_PB14_Msk | SYS_GPB_MFP_PB14_Msk);
    SYS->GPB_MFP |= (SYS_GPB_MFP_PB14_GPIO | SYS_GPB_MFP_PB15_GPIO);

	
    GPIO_SetMode(PB, BIT14, GPIO_PMD_OUTPUT);
    GPIO_SetMode(PB, BIT15, GPIO_PMD_OUTPUT);	
}

#if defined (ENABLE_TICK_EVENT)
void TickCallback_processB(void)
{
    printf("%s test \r\n" , __FUNCTION__);
}

void TickCallback_processA(void)
{
    printf("%s test \r\n" , __FUNCTION__);
}

void TickClearTickEvent(uint8_t u8TimeEventID)
{
    if (u8TimeEventID > TICKEVENTCOUNT)
        return;

    if (tTimerEvent[u8TimeEventID].active == TRUE)
    {
        tTimerEvent[u8TimeEventID].active = FALSE;
        _sys_uTimerEventCount--;
    }
}

signed char TickSetTickEvent(unsigned long uTimeTick, void *pvFun)
{
    int  i;
    int u8TimeEventID = 0;

    for (i = 0; i < TICKEVENTCOUNT; i++)
    {
        if (tTimerEvent[i].active == FALSE)
        {
            tTimerEvent[i].active = TRUE;
            tTimerEvent[i].initTick = uTimeTick;
            tTimerEvent[i].curTick = uTimeTick;
            tTimerEvent[i].funPtr = (sys_pvTimeFunPtr)pvFun;
            u8TimeEventID = i;
            _sys_uTimerEventCount += 1;
            break;
        }
    }

    if (i == TICKEVENTCOUNT)
    {
        return -1;    /* -1 means invalid channel */
    }
    else
    {
        return u8TimeEventID;    /* Event ID start from 0*/
    }
}

void TickCheckTickEvent(void)
{
    uint8_t i = 0;

    if (_sys_uTimerEventCount)
    {
        for (i = 0; i < TICKEVENTCOUNT; i++)
        {
            if (tTimerEvent[i].active)
            {
                tTimerEvent[i].curTick--;

                if (tTimerEvent[i].curTick == 0)
                {
                    (*tTimerEvent[i].funPtr)();
                    tTimerEvent[i].curTick = tTimerEvent[i].initTick;
                }
            }
        }
    }
}

void TickInitTickEvent(void)
{
    uint8_t i = 0;

    _sys_uTimerEventCount = 0;

    /* Remove all callback function */
    for (i = 0; i < TICKEVENTCOUNT; i++)
        TickClearTickEvent(i);

    _sys_uTimerEventCount = 0;
}
#endif 

void SysTick_Handler(void)
{
	static uint32_t LOG = 0;   

    systick_counter();
    if (get_systick() >= 0xFFFFFFFF)
    {
      set_systick(0);      
    }

    if ((get_systick() % 1000) == 0)
    {
        printf("%s : %4d\r\n",__FUNCTION__,LOG++);
    }

    #if defined (ENABLE_TICK_EVENT)
    TickCheckTickEvent();
    #endif    
}

void enable_sys_tick(int ticks_per_second)
{
    set_systick(0);
    if (SysTick_Config(SystemCoreClock / ticks_per_second))
    {
        /* Setup SysTick Timer for 1 second interrupts  */
        printf("Set system tick error!!\n");
        while (1);
    }

    #if defined (ENABLE_TICK_EVENT)
    TickInitTickEvent();
    #endif
}

void TMR1_IRQHandler(void)
{	
    if(TIMER_GetIntFlag(TIMER1) == 1)
    {
        TIMER_ClearIntFlag(TIMER1);
		tick_counter();

		if ((get_tick() % 1000) == 0)
		{
            set_flag(flag_timer_period_1000ms ,ENABLE);
		}

		if ((get_tick() % 100) == 0)
		{
            set_flag(flag_timer_period_100ms ,ENABLE);
		}
    }
}


void TIMER1_Init(void)
{
    TIMER_Open(TIMER1, TIMER_PERIODIC_MODE, 1000);
    TIMER_EnableInt(TIMER1);
    NVIC_EnableIRQ(TMR1_IRQn);	
    TIMER_Start(TIMER1);
}

void loop(void)
{
	// static uint32_t LOG1 = 0;
	// static uint32_t LOG2 = 0;

    if ((get_systick() % 1000) == 0)
    {
        // printf("%s(systick) : %4d\r\n",__FUNCTION__,LOG2++);    
    }

    if (is_flag_set(flag_timer_period_1000ms))
    {
        set_flag(flag_timer_period_1000ms ,DISABLE);

        // printf("%s(timer) : %4d\r\n",__FUNCTION__,LOG1++);
        PB14 ^= 1;        
    }

    if (is_flag_set(flag_timer_period_100ms))
    {
        set_flag(flag_timer_period_100ms ,DISABLE);
        get_ADC_Channel(4);       
    }
}


void UARTx_Process(void)
{
	uint8_t res = 0;
	res = UART_READ(UART0);

	if (res > 0x7F)
	{
		printf("invalid command\r\n");
	}
	else
	{
		switch(res)
		{
			case '1':
				break;

			case 'X':
			case 'x':
			case 'Z':
			case 'z':
				NVIC_SystemReset();		
				break;
		}
	}
}

void UART02_IRQHandler(void)
{
    if(UART_GET_INT_FLAG(UART0, UART_ISR_RDA_INT_Msk | UART_ISR_TOUT_IF_Msk))     /* UART receive data available flag */
    {
        while(UART_GET_RX_EMPTY(UART0) == 0)
        {
			UARTx_Process();
        }
    }

    if(UART0->FSR & (UART_FSR_BIF_Msk | UART_FSR_FEF_Msk | UART_FSR_PEF_Msk | UART_FSR_RX_OVER_IF_Msk))
    {
        UART_ClearIntFlag(UART0, (UART_ISR_RLS_INT_Msk| UART_ISR_BUF_ERR_INT_Msk));
    }	
}

void UART0_Init(void)
{
    SYS_ResetModule(UART0_RST);

    /* Configure UART0 and set UART0 baud rate */
    UART_Open(UART0, 115200);
    UART_EnableInt(UART0, UART_IER_RDA_IEN_Msk | UART_IER_TOUT_IEN_Msk);
    NVIC_EnableIRQ(UART02_IRQn);
	
	#if (_debug_log_UART_ == 1)	//debug
	printf("\r\nCLK_GetCPUFreq : %8d\r\n",CLK_GetHXTFreq());
	printf("CLK_GetHXTFreq : %8d\r\n",CLK_GetHCLKFreq());
	printf("CLK_GetLXTFreq : %8d\r\n",CLK_GetPCLKFreq());	
	printf("CLK_GetPCLK0Freq : %8d\r\n",CLK_GetCPUFreq());

	#endif
}

void SYS_Init(void)
{
    /* Unlock protected registers */
    SYS_UnlockReg();

    CLK_EnableXtalRC(CLK_PWRCON_OSC22M_EN_Msk);
    CLK_WaitClockReady(CLK_CLKSTATUS_OSC22M_STB_Msk);

//    CLK_EnableXtalRC(CLK_PWRCON_XTL12M_EN_Msk);
//    CLK_WaitClockReady(CLK_CLKSTATUS_XTL12M_STB_Msk);

    /* Select HCLK clock source as HIRC and HCLK source divider as 1 */
    CLK_SetHCLK(CLK_CLKSEL0_HCLK_S_HIRC, CLK_CLKDIV_HCLK(1));

    CLK_SetCoreClock(PLL_CLOCK);

    CLK_EnableModuleClock(UART0_MODULE);
    CLK_SetModuleClock(UART0_MODULE, CLK_CLKSEL1_UART_S_HIRC, CLK_CLKDIV_UART(1));

    CLK_EnableModuleClock(TMR0_MODULE);
  	CLK_SetModuleClock(TMR0_MODULE, CLK_CLKSEL1_TMR0_S_HIRC, 0);

    CLK_EnableModuleClock(TMR1_MODULE);
  	CLK_SetModuleClock(TMR1_MODULE, CLK_CLKSEL1_TMR1_S_HIRC, 0);

    /* Enable ADC module clock */
    CLK_EnableModuleClock(ADC_MODULE);
    /* ADC clock source is 22.1184MHz, set divider to 7, ADC clock is 22.1184/7 MHz */
    CLK_SetModuleClock(ADC_MODULE, CLK_CLKSEL1_ADC_S_HIRC, CLK_CLKDIV_ADC(7));

    /* Set GPB multi-function pins for UART0 RXD and TXD */
    SYS->GPB_MFP &= ~(SYS_GPB_MFP_PB0_Msk | SYS_GPB_MFP_PB1_Msk);
    SYS->GPB_MFP |= (SYS_GPB_MFP_PB0_UART0_RXD | SYS_GPB_MFP_PB1_UART0_TXD);

    /* Disable the GPA0 - GPA3 digital input path to avoid the leakage current. */
    GPIO_DISABLE_DIGITAL_PATH(PA, BIT4);

    /* Configure the GPA0 - GPA3 ADC analog input pins */
    SYS->GPA_MFP &= ~(SYS_GPA_MFP_PA4_Msk) ;
    SYS->GPA_MFP |= SYS_GPA_MFP_PA4_ADC4 ;


   /* Update System Core Clock */
    SystemCoreClockUpdate();

    /* Lock protected registers */
    SYS_LockReg();
}

int main()
{
    SYS_Init();

	GPIO_Init();
	UART0_Init();
	TIMER1_Init();

    #if defined (ENABLE_TICK_EVENT)
    enable_sys_tick(1000);
    TickSetTickEvent(1000, TickCallback_processA);  // 1000 ms
    TickSetTickEvent(5000, TickCallback_processB);  // 5000 ms
    #endif

    ADC_Init();

    /* Got no where to go, just loop forever */
    while(1)
    {
        loop();

    }
}

/*** (C) COPYRIGHT 2017 Nuvoton Technology Corp. ***/
