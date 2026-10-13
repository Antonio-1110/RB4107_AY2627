# Test: unit tests (state machine and logic)

> **Test only, not part of the running system.** It runs the unit tests. To run the system,
> flash [`presence_node`](../../presence_node) on the two presence C6
> boards, [`thermal_node`](../../thermal_node) on the thermal C6 board and
> [`controller`](../../controller) on the S3.

Targets: **ESP32-S3** and the **host PC** (ESP-IDF `linux` target).

Unity tests for the logic that decides safety. It is all plain C with
timestamps passed in, so the tests run a simulated clock and need no
hardware.

```bash
# on the PC (needs libbsd-dev on Linux)
idf.py -B build_linux -DSDKCONFIG=build_linux/sdkconfig --preview set-target linux
idf.py -B build_linux -DSDKCONFIG=build_linux/sdkconfig build
./build_linux/rb4107_unit_tests.elf        # exit code 0 = all passed

# on the board
idf.py build flash monitor                              # prints "ALL TESTS PASSED"

# everything (firmware + Django)
../../tools/run_host_tests.sh
```

## Required scenarios

| Scenario | Test |
|---|---|
| IDLE → MONITORING | `test_idle_to_monitoring` (plus hysteresis) |
| MONITORING → UNATTENDED | `test_monitoring_to_unattended_after_debounce` |
| UNATTENDED → MONITORING | `test_unattended_to_monitoring_person_returns` |
| UNATTENDED → WARNING | `test_unattended_to_warning` |
| WARNING → SHUTDOWN | `test_warning_to_shutdown_total_time`, `test_shutdown_timing_after_warning_mode` |
| Person returns during UNATTENDED | `test_unattended_to_monitoring_person_returns` |
| Person returns during WARNING | `test_person_returns_during_warning` (both policies) |
| Temperature drops during the unattended period | `test_temperature_drops_during_unattended` (both policies) |
| C4002 becomes unavailable | `test_presence_unknown_no_heat_flag_only`, `test_presence_unknown_while_cooking_runs_timeline`, `test_presence_unknown_then_confirmed_return_cancels`, `test_presence_unknown_keeps_running_timeline`, `test_invalid_reading_from_live_node` |
| MLX90640 becomes unavailable | `test_thermal_lost_flag_only`, `test_thermal_lost_keeps_running_timeline`, `test_thermal_lost_with_person_present_stays_monitoring` |
| FAULT only for the controller's own faults | `test_fault_only_for_controller_faults`, `test_sensor_faults_do_not_enter_fault_state` |
| Cooling pan holds the timers, then cancels | `test_cooling_pan_holds_the_timers_then_cancels`, `test_cooling_hold_is_reported`, `test_slow_cooling_pot_with_liquid_is_held_too` |
| Heat going in is not mistaken for cooling | `test_low_flame_is_not_mistaken_for_cooling`, `test_boiling_plateau_counts_as_heat_going_in`, `test_cooling_hold_ends_when_the_trend_flattens_hot`, `test_wrong_profile_delays_a_lowered_flame_at_most_a_minute`, `test_unknown_trend_counts_as_heat_going_in`, `test_trend_off_keeps_the_old_behaviour` |
| A due timer waits for a return in progress (bounded) | `test_warning_waits_for_a_return_in_progress`, `test_warning_fires_when_the_return_does_not_materialise`, `test_shutdown_waits_for_a_return_in_progress`, `test_long_return_holds_the_warning_until_it_is_confirmed`, `test_sparse_blips_do_not_hold_a_due_warning` |
| Temperature tiers: the hotter the pan, the shorter the time | `test_a_simmering_pot_gets_the_base_times`, `test_a_hot_pan_gets_shorter_times`, `test_oil_near_its_smoke_point_gets_the_shortest_times`, `test_tiers_never_lengthen_the_base_times`, `test_a_pan_that_heats_up_while_nobody_is_there_shortens_the_time`, `test_a_two_second_spike_does_not_shorten_the_time`, `test_rising_fast_toward_the_smoke_point_counts_as_critical`, `test_a_tier_gives_way_only_well_below_its_temperature`, `test_a_lost_camera_keeps_the_tier`, `test_tier_config_is_checked` |
| The menuconfig defaults per situation (simmer, hot pan, hot oil; burner on, off, turned down) | `test_kconfig_a_simmer_gets_the_base_times`, `test_kconfig_a_simmer_with_the_burner_off_is_left_alone`, `test_kconfig_a_hot_pan_on_low_gets_the_shorter_times`, `test_kconfig_hot_oil_with_the_burner_on_gets_the_shortest_times`, `test_kconfig_hot_oil_turned_down_is_still_shut_down`, `test_kconfig_hot_oil_with_the_burner_off_gets_a_buzzer_but_no_shutdown` |
| Someone passing pauses the clock, staying cancels it; the pause is bounded | `test_a_short_pass_stops_the_clock_and_resets_nothing`, `test_five_seconds_of_presence_cancel_the_timeline`, `test_the_pause_budget_limits_the_delay`, `test_the_budget_is_a_share_of_the_shutdown_time`, `test_no_budget_means_no_pause`, `test_the_pause_postpones_the_shutdown_by_what_it_paused` |
| At the stove needs every working radar | `test_presence_fuse_all_truth_table`, `test_node_set_require_all_and_nearby` |
| Cookware profile, temperature tiers and attendance commands | `test_cookware_is_parsed`, `test_bad_cookware_is_rejected`, `test_tiers_are_parsed`, `test_bad_tiers_are_rejected`, `test_attendance_is_parsed`, `test_bad_attendance_is_rejected` |
| Radar noise (blips) while absent | `test_presence_blips_do_not_block_absence`, `test_blip_inside_debounce_keeps_absent_since`, `test_real_return_after_noise_cancels_timer` |
| Node sends presence on change | `test_presence_change_is_sent_at_once` |
| Radar holds fixed at 0 / 0.2 s | `test_params_check`, `test_cmd_rejects_radar_holds` |
| Sensor node disappears / returns | `test_health_online_stale_offline_and_recovery`, `test_radar_disappears_and_returns` (node health → state machine) |
| Two radars combined (strict) | `test_presence_fuse_truth_table` (either sees a person → PRESENT; ABSENT only if both valid and empty; otherwise UNKNOWN), `test_node_set_routes_and_combines`, `test_either_radar_keeps_attended`, `test_node_set_single_radar` |
| Misconfigured node | `test_wrong_role_is_dropped`, `test_role_must_match_data`, `test_node_set_config_validation` |
| MQTT disconnects / reconnects | `test_mqtt_disconnect_and_reconnect_do_not_affect_safety` |

Plus: the menuconfig safety settings are valid and give the configured
warning/shutdown timing (`test_kconfig.c`); self-test pass/fail/timeout, latched SHUTDOWN and reset, reset
restarting the timing, heat detected after the person already left, the
optional rate threshold, the timing hook only ever shortening, 32-bit clock
wrap-around, config validation, the sequence tracking edge cases, protocol
presence/thermal packet round trips and corruption on every byte, heat-map picture pieces (round trip, corruption, roles) and their reassembly (lost or mixed-up pieces), C4002 frame resync and errors, thermal features,
JSON null/escaping/overflow, and the topic table.

All tests pass on the host (linux target) and on the ESP32-S3 (QEMU).

These tests found two real bugs, both fixed in `safety.c`. After a self-test
failure or timeout, FAULT recovered to IDLE straight away because its exit
condition only looked at the sensors. The self-test must now read PASS
before FAULT can clear.
