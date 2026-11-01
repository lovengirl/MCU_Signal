#ifndef SENSOR_H
#define SENSOR_H
#include <stdint.h>
#include <stdbool.h>
#define SENSOR_CHANNELS 20u
#define SENSOR_SAMPLES 4u
#define SENSOR_ALL ((1UL << SENSOR_CHANNELS)-1UL)
typedef enum {S_IDLE,S_SELECT_WAIT,S_OFF_SAMPLE,S_LIGHT_WAIT,S_ON_SAMPLE,S_FAULT} sensor_state_t;
typedef struct {
    uint16_t dac[SENSOR_CHANNELS];
    int16_t offset[SENSOR_CHANNELS];
    uint16_t gain_q12[SENSOR_CHANNELS];
    int16_t enter[SENSOR_CHANNELS], leave[SENSOR_CHANNELS];
    uint16_t select_us, light_us, sample_us;
    uint8_t confirm, polarity;
} sensor_config_t;
typedef struct {
    uint32_t sequence, config_version, valid, detected, saturated;
    uint16_t off[SENSOR_CHANNELS], on[SENSOR_CHANNELS];
    int32_t signal[SENSOR_CHANNELS];
} sensor_frame_t;
typedef struct {
    void (*tx)(bool);
    void (*rx)(bool);
    void (*select)(uint8_t);
    void (*dac)(uint16_t);
    void (*wait_us)(uint16_t);
    void (*sample)(uint16_t,uint16_t *,uint8_t);
    void (*cancel)(void);
} sensor_hal_t;
typedef struct {
    sensor_hal_t hal;
    sensor_config_t config, pending;
    sensor_frame_t frames[2];
    uint16_t samples[SENSOR_SAMPLES];
    sensor_state_t state;
    uint8_t channel, write_index, read_index, count[SENSOR_CHANNELS];
    bool continuous, stop_requested, ready, processed, has_pending;
    uint32_t sequence, config_version, fault, detected;
} sensor_t;
void sensor_defaults(sensor_config_t *);
bool sensor_config_valid(const sensor_config_t *);
void sensor_init(sensor_t *, const sensor_hal_t *);
bool sensor_set_config(sensor_t *,const sensor_config_t *);
bool sensor_start(sensor_t *,bool);
void sensor_stop(sensor_t *,bool);
void sensor_wait_done(sensor_t *);
void sensor_sample_done(sensor_t *);
void sensor_fail(sensor_t *,uint32_t);
bool sensor_clear_fault(sensor_t *);
const sensor_frame_t *sensor_result(const sensor_t *);
void sensor_process_ready(sensor_t *);
void sensor_release(sensor_t *);
#endif
