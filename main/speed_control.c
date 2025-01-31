#include "speed_control.h"
#include <driver/mcpwm_prelude.h>
#include <driver/mcpwm_cap.h>
#include <driver/gpio.h>

#define MOTOR_PIN1 1
#define MOTOR_PIN2 2
#define MOTOR_PIN3 3
#define MOTOR_PIN4 4

mcpwm_cmpr_handle_t g_comparators[4] = {NULL, NULL, NULL, NULL};

void setup_motor(int idx, int motor_pin, int group_id) {
  mcpwm_timer_handle_t timer = NULL;
  mcpwm_timer_config_t timer_config = {
    .group_id = group_id,
    .clk_src = MCPWM_TIMER_CLK_SRC_DEFAULT,
    .resolution_hz = 80000000, // 0.25 Microsecond
    .period_ticks = 10000, // 2500 microseconds
    .count_mode = MCPWM_TIMER_COUNT_MODE_UP,
  };
  ESP_ERROR_CHECK(mcpwm_new_timer(&timer_config, &timer));

  mcpwm_oper_handle_t oper = NULL;
  mcpwm_operator_config_t operator_config = {
    .group_id = group_id, // operator must be in the same group to the timer
  };
  ESP_ERROR_CHECK(mcpwm_new_operator(&operator_config, &oper));

  ESP_ERROR_CHECK(mcpwm_operator_connect_timer(oper, timer));

  mcpwm_comparator_config_t comparator_config = {
    .flags.update_cmp_on_tez = true,
  };
  ESP_ERROR_CHECK(mcpwm_new_comparator(oper, &comparator_config, &g_comparators[idx]));

  mcpwm_gen_handle_t generator = NULL;
  mcpwm_generator_config_t generator_config = {
    .gen_gpio_num = motor_pin,
  };
  ESP_ERROR_CHECK(mcpwm_new_generator(oper, &generator_config, &generator));

  // Set init speed
  ESP_ERROR_CHECK(mcpwm_comparator_set_compare_value(g_comparators[idx], 0));

  // Go high on counter empty
  ESP_ERROR_CHECK(mcpwm_generator_set_action_on_timer_event(generator,
    MCPWM_GEN_TIMER_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, MCPWM_TIMER_EVENT_EMPTY, MCPWM_GEN_ACTION_HIGH)));
  // Go low on compare threshold
  ESP_ERROR_CHECK(mcpwm_generator_set_action_on_compare_event(generator,
    MCPWM_GEN_COMPARE_EVENT_ACTION(MCPWM_TIMER_DIRECTION_UP, g_comparators[idx], MCPWM_GEN_ACTION_LOW)));

  ESP_ERROR_CHECK(mcpwm_timer_enable(timer));
  mcpwm_timer_start_stop(timer, MCPWM_TIMER_START_NO_STOP);
}

void speed_control_init() {
  setup_motor(0, MOTOR_PIN1, 0);
  setup_motor(1, MOTOR_PIN2, 0);
  setup_motor(2, MOTOR_PIN3, 1);
  setup_motor(3, MOTOR_PIN4, 1);
}

void speed_control_set(float s1, float s2, float s3, float s4) {
  mcpwm_comparator_set_compare_value(g_comparators[0], (uint32_t)s1);
  mcpwm_comparator_set_compare_value(g_comparators[1], (uint32_t)s2);
  mcpwm_comparator_set_compare_value(g_comparators[2], (uint32_t)s3);
  mcpwm_comparator_set_compare_value(g_comparators[3], (uint32_t)s4);
}
