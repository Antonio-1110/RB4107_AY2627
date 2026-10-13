/*
 * Sweep of the cooking judgment (firmware/components/safety) on model cooling curves:
 * how long until a cooling pan is recognised for each cookware profile and true time
 * constant, and whether a lowered flame is ever taken for cooling. Camera noise 0.3 degC.
 * Not a measurement of real cookware: it shows what a wrong profile costs.
 *
 *   cc -std=gnu11 -I firmware/components/safety/include -I firmware/components/rb_types/include \
 *      tools/diagnostics/cooling_sweep.c firmware/components/safety/src/safety.c -lm -o /tmp/cooling_sweep
 *   /tmp/cooling_sweep
 */
#include <stdio.h>
#include <stdlib.h>
#include <math.h>
#include "safety.h"
static float noise(float s){ float u=(rand()+1.0f)/(RAND_MAX+2.0f), v=(rand()+1.0f)/(RAND_MAX+2.0f); return s*sqrtf(-2*logf(u))*cosf(6.2831853f*v); }
/* returns: first time (s) the phase is COOLING and stays so until T<40; -1 if never. flicker = steps after first cooling not cooling while T>=45 */
static void run(float tau_true,float tinf,float t0,safety_cookware_t prof,float sigma,float *t_rec,int *bad){
  safety_config_t cfg={.self_test_timeout_ms=5000,.heat_on_temp_c=50,.heat_off_temp_c=40,.absence_debounce_ms=2000,.warning_timeout_ms=1000000,.shutdown_timeout_ms=2000000,.cooling_policy=SAFETY_COOLING_RETURN_TO_IDLE,.fault_shutdown_timeout_ms=90000,.cookware=prof,.cookware_tau_s=safety_cookware_tau_s(prof)};
  safety_sm_t sm; safety_init(&sm,&cfg,0,0,0);
  safety_inputs_t in={.presence=RB_FALSE,.thermal_valid=true,.cooking_temp_c=25,.self_test=SAFETY_SELFTEST_PASS};
  uint32_t now=0; for(int i=0;i<3;i++){now+=100;safety_step(&sm,&in,now);}
  // 3 minutes of steady heat at t0 first
  for(int i=0;i<1800;i++){ in.cooking_temp_c=t0+noise(sigma); now+=100; safety_step(&sm,&in,now);}
  uint32_t s=now; *t_rec=-1; *bad=0; int rec=0;
  for(;now<s+7200000u;now+=100){ float t=(now-s)/1000.0f; float T=tinf+(t0-tinf)*expf(-t/tau_true); in.cooking_temp_c=T+noise(sigma); safety_step(&sm,&in,now);
    if(sm.state==SAFETY_IDLE) break;
    if(sm.heat_phase==SAFETY_HEAT_COOLING){ if(!rec){rec=1;*t_rec=t;} } else if(rec && T>=46) (*bad)++;
  }
}
int main(void){
  srand(7);
  const char*names[]={"mixed","light","heavy","liquid"};
  printf("PASSIVE COOLING from 150 C (burner off), noise 0.3 C: seconds until recognised as cooling / flicker steps after\n");
  float taus[]={180,300,480,900,1200,2400,3600};
  printf("true tau(min): "); for(int j=0;j<7;j++) printf("%6.0f",taus[j]/60); printf("\n");
  for(int p=0;p<4;p++){ printf("%-7s       ",names[p]); for(int j=0;j<7;j++){float tr;int bad; run(taus[j],25,150,p,0.3f,&tr,&bad); if(tr<0) printf("  never"); else printf(" %3.0f/%-2d",tr,bad>99?99:bad);} printf("\n"); }
  printf("\nLOW FLAME (settles at 90 C / 60 C / 45 C after being at 150 C): is it ever called cooling? (1=yes = missed heat)\n");
  float inf[]={90,60,45};
  for(int p=0;p<4;p++){ printf("%-7s ",names[p]); for(int k=0;k<3;k++){ for(int j=0;j<7;j+=3){ float tr;int bad; /* only check phase never cooling */ 
      safety_cookware_t pr=p; safety_config_t cfg={.self_test_timeout_ms=5000,.heat_on_temp_c=50,.heat_off_temp_c=40,.absence_debounce_ms=2000,.warning_timeout_ms=1000000,.shutdown_timeout_ms=2000000,.cooling_policy=SAFETY_COOLING_RETURN_TO_IDLE,.fault_shutdown_timeout_ms=90000,.cookware=pr,.cookware_tau_s=safety_cookware_tau_s(pr)};
      safety_sm_t sm; safety_init(&sm,&cfg,0,0,0); safety_inputs_t in={.presence=RB_FALSE,.thermal_valid=true,.cooking_temp_c=25,.self_test=SAFETY_SELFTEST_PASS};
      uint32_t now=0; for(int i=0;i<3;i++){now+=100;safety_step(&sm,&in,now);} for(int i=0;i<1800;i++){in.cooking_temp_c=150+noise(0.3f);now+=100;safety_step(&sm,&in,now);} uint32_t s=now; int cooled=0;
      for(;now<s+3600000u;now+=100){float t=(now-s)/1000.0f; in.cooking_temp_c=inf[k]+(150-inf[k])*expf(-t/taus[j])+noise(0.3f); safety_step(&sm,&in,now); if(sm.heat_phase==SAFETY_HEAT_COOLING && t>600) cooled=1;}
      printf(" inf%.0f/tau%.0fm:%d",inf[k],taus[j]/60,cooled);} } printf("\n"); }
  return 0; }
