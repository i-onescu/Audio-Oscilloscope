#include <stdio.h>
#include "tusb.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/irq.h"


#define ADC_SAMPLE_NUM 992
#define HEADER1 0xAA
#define HEADER2 0x55


int             dma_adc_chan;
bool            Use_DMA_to_ADC_Buffer_1 = true;
uint16_t        DMA_to_ADC_Buffer_1[ADC_SAMPLE_NUM];
uint16_t        DMA_to_ADC_Buffer_2[ADC_SAMPLE_NUM];
volatile bool   DMA_to_ADC_Buffer_1_ready = false;
volatile bool   DMA_to_ADC_Buffer_2_ready = false;
uint16_t        dummy_buffer[992] = {0};


void my_adc_init();
void my_dma_init();
void dma_adc_handler();

void send_buffer(uint16_t *buf, int len);
int main()
{
    stdio_init_all();
    
    my_adc_init();
    //start adc    
    adc_run(true);

    my_dma_init();
    //start dma
    dma_channel_start(dma_adc_chan);

    while (true) {

        if(DMA_to_ADC_Buffer_1_ready) {
            DMA_to_ADC_Buffer_1_ready = false;
            send_buffer(DMA_to_ADC_Buffer_1, ADC_SAMPLE_NUM);
        }
        if(DMA_to_ADC_Buffer_2_ready) {
            DMA_to_ADC_Buffer_2_ready = false;
            send_buffer(DMA_to_ADC_Buffer_2, ADC_SAMPLE_NUM);
        }

    }
}

void my_adc_init() {
    adc_gpio_init(26);
    // adc_gpio_init(27);

    adc_init();

    adc_fifo_setup(
        true,   
        true,   
        1,      
        false,  
        false   
    );

    adc_set_clkdiv(480);   // 100kHz samplerate
}

void my_dma_init() {
    //dma setup
    dma_adc_chan = dma_claim_unused_channel(true);
    dma_channel_config cfg = dma_channel_get_default_config(dma_adc_chan);

    channel_config_set_transfer_data_size(&cfg, DMA_SIZE_16);
    channel_config_set_read_increment(&cfg, false);
    channel_config_set_write_increment(&cfg, true);
    channel_config_set_dreq(&cfg, DREQ_ADC);

    dma_channel_configure(
        dma_adc_chan,
        &cfg,
        DMA_to_ADC_Buffer_1,      
        &adc_hw->fifo,            // Source = ADC FIFO
        ADC_SAMPLE_NUM,           
        false                     // Don't start yet
    );

    dma_channel_set_irq0_enabled(dma_adc_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_adc_handler);
    irq_set_enabled(DMA_IRQ_0, true);
}

void dma_adc_handler() {
    dma_hw->ints0 = 1u << dma_adc_chan;

    // Raise flag and switch buffer
    if (Use_DMA_to_ADC_Buffer_1) {
        DMA_to_ADC_Buffer_1_ready = true;
        dma_channel_set_write_addr(dma_adc_chan, DMA_to_ADC_Buffer_2, true);
        Use_DMA_to_ADC_Buffer_1 = false;
    } else {
        DMA_to_ADC_Buffer_2_ready = true;
        dma_channel_set_write_addr(dma_adc_chan, DMA_to_ADC_Buffer_1, true);
        Use_DMA_to_ADC_Buffer_1 = true;
    }

    // Re-start DMA 
    dma_channel_start(dma_adc_chan);
}


void send_buffer(uint16_t *buf, int len) {
    uint16_t temp_buf [31];
    int j = 0;
    usb_buffer[0] = HEADER1;
    usb_buffer[1] = HEADER2;
    for (size_t i = 0; i < 992; i++) {
        if (j == 31) {
            j = 0;
            if (tud_cdc_connected() && tud_cdc_write_available()) {
                tud_cdc_write(usb_buffer, 64);
                tud_cdc_write_flush();
            }
            usb_buffer[0] = HEADER1;
            usb_buffer[1] = HEADER2;
        } else {
            usb_buffer[2 + j * 2] = buf[i] & 0xFF;
            usb_buffer[3 + j * 2] = buf[i] >> 8;
            j++;
        }
        
    }
}