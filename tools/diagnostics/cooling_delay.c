/*
 * How late is the buzzer when the cookware profile is slower than the pan? A lowered flame (150 -> 90 degC,
 * the pan's real time constant 5 min), the person gone, the warning due at 120 s: when does it come for each
 * profile time constant? Model curve, no noise; not a measurement of real cookware.
 *
 *   cc -std=gnu11 -I firmware/components/safety/include -I firmware/components/rb_types/include \
 *      tools/diagnostics/cooling_delay.c firmware/components/safety/src/safety.c -lm -o /tmp/cooling_delay
 *   /tmp/cooling_delay
 */
#include <stdio.h>
#include <math.h>
#include "safety.h"
/* low flame (150 -> 90 C, true tau 5 min), person gone, warning due at 120 s: when does the buzzer come? */
int main(void){
  float taus[]={150,300,450,600,900,1200,1800,2400};
  printf("profile tau / true tau : buzzer delay after due (s)\n");
  for(int k=0;k<8;k++){
    safety_config_t cfg={.self_test_timeout_ms=5000,.heat_on_temp_c=50,.heat_off_temp_c=40,.absence_debounce_ms=1000,.warning_timeout_ms=120000,.shutdown_timeout_ms=1200000,.cooling_policy=SAFETY_COOLING_RETURN_TO_IDLE,.fault_shutdown_timeout_ms=90000,.cookware_tau_s=taus[k]};
    safety_sm_t sm; safety_init(&sm,&cfg,0,0,0);
    safety_inputs_t in={.presence=RB_TRUE,.thermal_valid=true,.cooking_temp_c=25,.self_test=SAFETY_SELFTEST_PASS};
    uint32_t now=0; for(int i=0;i<2;i++){now+=100;safety_step(&sm,&in,now);}
    in.cooking_temp_c=150; for(;now<150100;now+=100) safety_step(&sm,&in,now);
    in.presence=RB_FALSE; uint32_t t0=now; int warn=-1;
    for(;now<t0+1500000;now+=100){ float t=(now-t0)/1000.0f; in.cooking_temp_c=90+60*expf(-t/300); safety_step(&sm,&in,now); if(sm.state==SAFETY_WARNING){warn=(now-t0)/1000;break;} }
    printf("%5.1fx (%4.0f s)        : %d s after leaving -> delay %d s\n",taus[k]/300,taus[k],warn,warn-120);
  }
}
