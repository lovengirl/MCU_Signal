#include "sensor.h"
#include <string.h>
void sensor_defaults(sensor_config_t *c) {
    memset(c,0,sizeof(*c));
    for(unsigned i=0;i<SENSOR_CHANNELS;i++) {
        c->dac[i]=512; c->gain_q12[i]=4096; c->enter[i]=500; c->leave[i]=400;
    }
    c->select_us=50; c->light_us=50; c->sample_us=10; c->confirm=2; c->polarity=1;
}
bool sensor_config_valid(const sensor_config_t *c) {
    if(!c || c->select_us<5 || c->select_us>10000 || c->light_us<5 ||
       c->light_us>10000 || c->sample_us<10 || c->sample_us>1000 ||
       c->confirm<1 || c->confirm>20 || c->polarity>1) return false;
    for(unsigned i=0;i<SENSOR_CHANNELS;i++)
        if(c->dac[i]>4095 || c->gain_q12[i]==0 || c->gain_q12[i]>16384 ||
           c->leave[i]>=c->enter[i]) return false;
    return true;
}
static void safe(sensor_t *s) {s->hal.tx(false);s->hal.cancel();s->hal.rx(false);s->hal.dac(0);}
static void apply(sensor_t *s) {
    if(s->has_pending) {
        s->config=s->pending; s->has_pending=false; s->config_version++;
        memset(s->count,0,sizeof(s->count));s->detected=0;
    }
}
void sensor_init(sensor_t *s,const sensor_hal_t *h) {
    memset(s,0,sizeof(*s));s->hal=*h;sensor_defaults(&s->config);s->config_version=1;
    s->state=S_IDLE;safe(s);
}
bool sensor_set_config(sensor_t *s,const sensor_config_t *c) {
    if(!sensor_config_valid(c)) return false;
    s->pending=*c;s->has_pending=true;
    if(s->state==S_IDLE && !s->ready) apply(s);
    return true;
}
static void channel_start(sensor_t *s) {
    s->hal.tx(false);s->hal.rx(false);s->hal.select(s->channel);
    s->hal.dac(s->config.dac[s->channel]);s->hal.rx(true);
    s->state=S_SELECT_WAIT;s->hal.wait_us(s->config.select_us);
}
static void frame_start(sensor_t *s) {
    apply(s);memset(&s->frames[s->write_index],0,sizeof(sensor_frame_t));
    s->frames[s->write_index].config_version=s->config_version;
    s->channel=0;channel_start(s);
}
bool sensor_start(sensor_t *s,bool continuous) {
    if(s->state!=S_IDLE || s->ready) return false;
    s->continuous=continuous;s->stop_requested=false;frame_start(s);return true;
}
void sensor_stop(sensor_t *s,bool immediate) {
    s->continuous=false;
    if(immediate) {safe(s);if(s->state!=S_FAULT)s->state=S_IDLE;s->stop_requested=false;}
    else {s->stop_requested=true;if(s->state==S_IDLE)safe(s);}
}
void sensor_wait_done(sensor_t *s) {
    if(s->state==S_SELECT_WAIT) s->state=S_OFF_SAMPLE;
    else if(s->state==S_LIGHT_WAIT) s->state=S_ON_SAMPLE;
    else {sensor_fail(s,2);return;}
    s->hal.sample(s->config.sample_us,s->samples,SENSOR_SAMPLES);
}
void sensor_sample_done(sensor_t *s) {
    if(s->state!=S_OFF_SAMPLE && s->state!=S_ON_SAMPLE){sensor_fail(s,3);return;}
    unsigned i=s->channel;uint32_t sum=0;bool sat=false;
    for(unsigned k=0;k<SENSOR_SAMPLES;k++) {
        if(s->samples[k]>4095){sensor_fail(s,4);return;}
        sum+=s->samples[k];if(s->samples[k]==4095)sat=true;
    }
    sensor_frame_t *f=&s->frames[s->write_index];
    if(sat) f->saturated|=1UL<<i;
    if(s->state==S_OFF_SAMPLE) {
        f->off[i]=(uint16_t)(sum/SENSOR_SAMPLES);s->hal.tx(true);
        s->state=S_LIGHT_WAIT;s->hal.wait_us(s->config.light_us);return;
    }
    s->hal.tx(false);f->on[i]=(uint16_t)(sum/SENSOR_SAMPLES);
    if(!(f->saturated & (1UL<<i))) f->valid|=1UL<<i;
    if(s->stop_requested){safe(s);s->state=S_IDLE;return;}
    if(++s->channel<SENSOR_CHANNELS){channel_start(s);return;}
    safe(s);f->sequence=++s->sequence;s->read_index=s->write_index;s->write_index^=1;
    s->ready=true;s->processed=false;s->state=S_IDLE;
}
static void process(sensor_t *s,sensor_frame_t *f) {
    for(unsigned i=0;i<SENSOR_CHANNELS;i++) {
        uint32_t bit=1UL<<i;
        int32_t v=(int32_t)f->on[i]-f->off[i];if(!s->config.polarity)v=-v;
        v=(v-s->config.offset[i])*(int32_t)s->config.gain_q12[i]/4096;f->signal[i]=v;
        if(!(f->valid&bit)){s->count[i]=0;s->detected&=~bit;continue;}
        bool active=(s->detected&bit)!=0;
        bool change=active ? v<=s->config.leave[i] : v>=s->config.enter[i];
        if(change) {if(++s->count[i]>=s->config.confirm){s->detected^=bit;s->count[i]=0;}}
        else s->count[i]=0;
    }
    f->detected=s->detected & f->valid;
}
const sensor_frame_t *sensor_result(const sensor_t *s){return s->ready?&s->frames[s->read_index]:0;}
void sensor_process_ready(sensor_t *s) {
    if(s->ready && !s->processed){process(s,&s->frames[s->read_index]);s->processed=true;}
}
void sensor_release(sensor_t *s) {
    if(!s->ready)return;
    sensor_process_ready(s);
    s->ready=false;
    if(s->state==S_IDLE && s->continuous && !s->stop_requested) frame_start(s);
    else if(s->state==S_IDLE)apply(s);
}
void sensor_fail(sensor_t *s,uint32_t code){safe(s);s->fault=code;s->state=S_FAULT;s->continuous=false;}
bool sensor_clear_fault(sensor_t *s) {
    if(s->state!=S_FAULT)return false;
    safe(s);s->state=S_IDLE;s->fault=0;s->ready=false;memset(s->count,0,sizeof(s->count));s->detected=0;return true;
}
