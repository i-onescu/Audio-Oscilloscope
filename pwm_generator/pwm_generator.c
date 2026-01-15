#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/pwm.h"

#define ADC_SAMPLE_NUM 128

int             dma_adc_chan;
bool            Use_DMA_to_ADC_Buffer_1 = true;
uint16_t        pwm_wrap = 500;
uint16_t        pwm_setpoint = 250;
uint16_t        adc1_mean;
uint16_t        adc2_mean;
uint64_t        auxForMean;
uint16_t        DMA_to_ADC_Buffer_1[ADC_SAMPLE_NUM];
uint16_t        DMA_to_ADC_Buffer_2[ADC_SAMPLE_NUM];
volatile bool   DMA_to_ADC_Buffer_1_ready = false;
volatile bool   DMA_to_ADC_Buffer_2_ready = false;
uint            slice_num;

int frequency = 0;
int duty = 0;

void my_adc_init();
void my_dma_init();
void my_pwm_init();
void dma_adc_handler();
uint16_t calculate_mean(uint16_t buffer[], uint16_t size);
uint16_t calculate_mean_with_channel(uint16_t buffer[], uint16_t size, uint8_t channel);
void update_pwm_parameters(uint16_t freq_pot, uint16_t duty_pot, uint8_t slice_num);

int main() {
    stdio_init_all();

    my_adc_init();
    //start adc    
    adc_run(true);

    my_pwm_init();

    my_dma_init();
    //start dma
    dma_channel_start(dma_adc_chan);
    uint16_t adc0_val = 0;
    uint16_t adc1_val = 0;
    uint count = 0;
    while (true) {

        // gpio_put(25, 1);
        // sleep_ms(500);
        // gpio_put(25, 0);
        // sleep_ms(500);
        if(DMA_to_ADC_Buffer_1_ready) {
            DMA_to_ADC_Buffer_1_ready = false;

            adc1_mean = calculate_mean_with_channel(DMA_to_ADC_Buffer_1, ADC_SAMPLE_NUM, 0);
            adc2_mean = calculate_mean_with_channel(DMA_to_ADC_Buffer_1, ADC_SAMPLE_NUM, 1);

            update_pwm_parameters(adc1_mean, adc2_mean, slice_num);
        }
        if(DMA_to_ADC_Buffer_2_ready) {
            DMA_to_ADC_Buffer_2_ready = false;

            adc1_mean = calculate_mean_with_channel(DMA_to_ADC_Buffer_2, ADC_SAMPLE_NUM, 0);
            adc2_mean = calculate_mean_with_channel(DMA_to_ADC_Buffer_2, ADC_SAMPLE_NUM, 1);

            update_pwm_parameters(adc1_mean, adc2_mean, slice_num);
        }

        if (count == 40000000) {
            count = 0;
            printf(" pwm_wrap = %d -------- pwm_setpoint = %d \n", pwm_wrap, pwm_setpoint);
            frequency = 125000000 / (pwm_wrap  * 125);
            duty = pwm_wrap / pwm_setpoint;
            printf(" frequency = %d -------- duty = %d % \n", frequency, duty);
            printf(" adc0_val = %d -------- adc1_val = %d \n", adc1_mean, adc2_mean);
        }        
        count++;
    }
}

void my_adc_init() {
    adc_gpio_init(26);
    adc_gpio_init(27);

    adc_init();

    adc_set_round_robin((1 << 0) | (1 << 1));  // ADC0 + ADC1
    adc_fifo_setup(
        true,   //write each completed conversion into sample FIFO
        true,   //enamble DMA request (DREQ)
        1,      //DMA transfer when at least 1 sample present
        false,  //dont show err bits
        false   //dont shift one byte
    );

    adc_set_clkdiv(4800);   // 100kHz samplerate
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
        DMA_to_ADC_Buffer_1,      // Destination buffer
        &adc_hw->fifo,            // Source = ADC FIFO
        ADC_SAMPLE_NUM,           // Number of samples
        false                     // Don't start yet
    );

    dma_channel_set_irq0_enabled(dma_adc_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_adc_handler);
    irq_set_enabled(DMA_IRQ_0, true);
}

void my_pwm_init() {
    // //pwm settings
    gpio_set_function(14, GPIO_FUNC_PWM);
    slice_num = pwm_gpio_to_slice_num(14);
    pwm_set_enabled(slice_num, true);
    
    //initial pwm params
    pwm_set_wrap(slice_num, pwm_wrap);
    pwm_set_chan_level(slice_num, PWM_CHAN_A, pwm_setpoint);
    pwm_set_clkdiv(slice_num, 125);
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

    // Re-start DMA for next buffer
    dma_channel_start(dma_adc_chan);
}

uint16_t calculate_mean(uint16_t buffer[], uint16_t size) {
    auxForMean = 0;
    for (size_t i = 0; i < size; i++) {
        auxForMean += buffer[i];
    }
    return auxForMean / size;
}

uint16_t calculate_mean_with_channel(uint16_t buffer[], uint16_t size, uint8_t channel) {
    auxForMean = 0;
    for (size_t i = channel; i < size; i+=2) {
        auxForMean += buffer[i];
    }
    return auxForMean / size * 2;
}

void update_pwm_parameters(uint16_t freq_pot, uint16_t duty_pot, uint8_t slice_num) {
    pwm_wrap = 50 + ((float)freq_pot / 4095.0f) * (10000 - 50);                     //formula for my wrap to be set between 100Hz and 20kHz
    // printf("%f %f",((float)freq_pot / 4095.0f), ((float)freq_pot / 4095.0f) * (10000 - 50));
    pwm_setpoint = (float)pwm_wrap * (0.2f + (float)duty_pot / 4095 * 0.6f);          //interpolate duty from adc and multiply by the wrap

    pwm_set_wrap(slice_num, pwm_wrap);
    pwm_set_chan_level(slice_num, PWM_CHAN_A, pwm_setpoint);
}

