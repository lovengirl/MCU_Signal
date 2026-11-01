#include "platform.h"
#include "ti_msp_dl_config.h"
#define TXMASK 0x1fu
#define RXMASK (0x1fu<<6)
sensor_t g_sensor;
static volatile uint32_t ticks, deadline;
static volatile bool waiting, sampling, wait_event, sample_event, timeout_event;
static volatile uint16_t rx_head,rx_tail,tx_head,tx_tail;
static char rxbuf[128],txbuf[2048];
volatile uint32_t g_uart_rx_overruns,g_uart_tx_dropped;
static void tx(bool on){if(on)DL_GPIO_setPins(GPIOB,BOARD_TX_EN_PIN);else DL_GPIO_clearPins(GPIOB,BOARD_TX_EN_PIN);}
static void rx(bool on){if(on)DL_GPIO_setPins(GPIOB,BOARD_RX_EN_PIN);else DL_GPIO_clearPins(GPIOB,BOARD_RX_EN_PIN);}
static void select_channel(uint8_t ch){DL_GPIO_writePinsVal(GPIOB,TXMASK|RXMASK,ch|((uint32_t)ch<<6));}
static void dac(uint16_t v){DL_DAC12_output12(DAC0,v);}
static void cancel_sampling(void){
    DL_TimerG_stopCounter(TIMG6);DL_ADC12_disableConversions(ADC0);
    DL_ADC12_stopConversion(ADC0);DL_ADC12_disableDMA(ADC0);
    DL_DMA_disableChannel(DMA,0);DL_DMA_clearInterruptStatus(DMA,DL_DMA_INTERRUPT_CHANNEL0);
    NVIC_ClearPendingIRQ(DMA_INT_IRQn);sampling=false;sample_event=false;
}
static void cancel(void){
    DL_TimerG_stopCounter(TIMG0);DL_TimerG_clearInterruptStatus(TIMG0,DL_TIMERG_INTERRUPT_ZERO_EVENT);
    NVIC_ClearPendingIRQ(TIMG0_INT_IRQn);waiting=false;wait_event=false;cancel_sampling();
    DL_GPIO_clearPins(GPIOB,BOARD_SCAN_MARK_PIN);
}
static void wait_us(uint16_t us){
    DL_TimerG_stopCounter(TIMG0);DL_TimerG_setLoadValue(TIMG0,us-1u);
    DL_TimerG_setTimerCount(TIMG0,us-1u);
    DL_TimerG_clearInterruptStatus(TIMG0,DL_TIMERG_INTERRUPT_ZERO_EVENT);
    waiting=true;deadline=ticks+25;DL_TimerG_startCounter(TIMG0);
}
static void sample(uint16_t us,uint16_t *dest,uint8_t n){
    cancel_sampling();
    DL_ADC12_setDMASamplesCnt(ADC0,1);
    DL_DMA_setSrcAddr(DMA,0,DL_ADC12_getMemResultAddress(ADC0,DL_ADC12_MEM_IDX_0));
    DL_DMA_setDestAddr(DMA,0,(uint32_t)dest);DL_DMA_setTransferSize(DMA,0,n);
    DL_ADC12_enableDMA(ADC0);DL_DMA_enableChannel(DMA,0);DL_ADC12_enableConversions(ADC0);
    DL_TimerG_setLoadValue(TIMG6,us-1u);DL_TimerG_setTimerCount(TIMG6,us-1u);
    sampling=true;deadline=ticks+25;
    DL_GPIO_setPins(GPIOB,BOARD_SCAN_MARK_PIN);DL_TimerG_startCounter(TIMG6);
}
void platform_init(void){
    SYSCFG_DL_init();
    sensor_hal_t h={tx,rx,select_channel,dac,wait_us,sample,cancel};sensor_init(&g_sensor,&h);
    DL_DMA_enableInterrupt(DMA,DL_DMA_INTERRUPT_CHANNEL0);
    NVIC_SetPriority(DMA_INT_IRQn,0);NVIC_SetPriority(TIMG0_INT_IRQn,1);
    NVIC_SetPriority(UART0_INT_IRQn,3);NVIC_SetPriority(SysTick_IRQn,2);
    NVIC_EnableIRQ(DMA_INT_IRQn);NVIC_EnableIRQ(TIMG0_INT_IRQn);NVIC_EnableIRQ(UART0_INT_IRQn);
    SysTick_Config(CPUCLK_FREQ/1000u);
}
void TIMG0_IRQHandler(void){
    if(DL_TimerG_getPendingInterrupt(TIMG0)==DL_TIMERG_IIDX_ZERO){
        if(waiting){waiting=false;wait_event=true;}
    }
}
void DMA_IRQHandler(void){
    if(DL_DMA_getRawInterruptStatus(DMA,DL_DMA_INTERRUPT_CHANNEL0)){
        DL_TimerG_stopCounter(TIMG6);
        /* End illumination at acquisition completion, before main-loop work. */
        tx(false);cancel_sampling();DL_GPIO_clearPins(GPIOB,BOARD_SCAN_MARK_PIN);sample_event=true;
    }
}
void SysTick_Handler(void){
    ticks++;
    if((waiting||sampling) && (int32_t)(ticks-deadline)>=0){
        tx(false);cancel(); /* Publish error for the main loop, no state-machine work here. */
        timeout_event=true;
    }
}
void platform_poll(void){
    uint32_t key=__get_PRIMASK();__disable_irq();
    if(timeout_event){timeout_event=false;sensor_fail(&g_sensor,1);}
    if(wait_event){wait_event=false;sensor_wait_done(&g_sensor);}
    if(sample_event){sample_event=false;sensor_sample_done(&g_sensor);}
    __set_PRIMASK(key);
    if(tx_tail!=tx_head && !DL_UART_Main_isTXFIFOFull(UART0)){
        DL_UART_Main_transmitData(UART0,(uint8_t)txbuf[tx_tail]);tx_tail=(tx_tail+1u)%sizeof(txbuf);
    }
}
uint32_t platform_millis(void){return ticks;}
void UART0_IRQHandler(void){
    (void)DL_UART_Main_getPendingInterrupt(UART0);
    while(!DL_UART_Main_isRXFIFOEmpty(UART0)){
        char c=(char)DL_UART_Main_receiveData(UART0);uint16_t next=(rx_head+1u)%sizeof(rxbuf);
        if(next==rx_tail)g_uart_rx_overruns++;else{rxbuf[rx_head]=c;rx_head=next;}
    }
}
bool console_get(char *c){if(rx_head==rx_tail)return false;*c=rxbuf[rx_tail];rx_tail=(rx_tail+1u)%sizeof(rxbuf);return true;}
bool console_puts(const char *s){
    unsigned n=0;while(s[n])n++;
    unsigned free=(tx_tail+sizeof(txbuf)-tx_head-1u)%sizeof(txbuf);
    if(n>free){g_uart_tx_dropped++;return false;}
    while(*s){txbuf[tx_head]=*s++;tx_head=(tx_head+1u)%sizeof(txbuf);}return true;
}
