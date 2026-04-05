
#ifndef APP_GPIO_H
#define APP_GPIO_H

#include "driver/gpio.h"

#define NUM_OUTPUTS 5
#define NUM_INPUTS 4
#define NUM_PINS (NUM_OUTPUTS + NUM_INPUTS)

extern const gpio_num_t output_pins[];
extern const gpio_num_t input_pins[];

void gpio_init_outputs(void);
void gpio_restore_states(void);
void gpio_init_inputs(void);
void gpio_event_task(void *arg);
const char* gpio_to_name(gpio_num_t pin);
gpio_num_t name_to_gpio(const char *name);

#endif
