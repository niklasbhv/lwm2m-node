/*
 * Copyright (c) 2021 Nordic Semiconductor ASA
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>

LOG_MODULE_REGISTER(main, LOG_LEVEL_DBG);

#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/udp.h>
#include <zephyr/net/coap.h>
#include <zephyr/net/coap_service.h>
#include <zephyr/net/coap_link_format.h>

#include <zephyr/drivers/gpio.h>
#include <zephyr/net/openthread.h>
#include <openthread/thread.h>

#include "coap_client.h"

// led0 -> Red LED
// led1 -> Green LED
// led2 -> Blue LED
// led3 -> Yellow LED
// led4 -> User LED
#define OT_CONNECTION_LED DT_ALIAS(led3)
#define PROVISIONING_LED DT_ALIAS(led1)
#define LIGHT_LED DT_ALIAS(led4)

#define COAP_SERVER_WORKQ_STACK_SIZE 1024
#define COAP_SERVER_WORKQ_PRIORITY 5

#define COAP_PORT 5683
#define SLEEP_TIME_MS 5000

K_THREAD_STACK_DEFINE(coap_server_workq_stack_area, COAP_SERVER_WORKQ_STACK_SIZE);

// LED initialization
static const struct gpio_dt_spec led_connection = GPIO_DT_SPEC_GET(OT_CONNECTION_LED, gpios);
static const struct gpio_dt_spec led_provisioning = GPIO_DT_SPEC_GET(PROVISIONING_LED, gpios);
static const struct gpio_dt_spec led_user = GPIO_DT_SPEC_GET(LIGHT_LED, gpios);

// Button initialization
static const struct gpio_dt_spec button = {DEVICE_DT_GET(DT_NODELABEL(gpio1)), 12, (GPIO_PULL_UP | GPIO_ACTIVE_LOW)};
enum button_evt {
    BUTTON_EVT_PRESSED,
    BUTTON_EVT_RELEASED
};
typedef void (*button_event_handler_t)(enum button_evt evt);
static button_event_handler_t button_cb;
static struct gpio_callback button_cb_data;

// CoAP Server Service Definition
COAP_SERVICE_DEFINE(coap_server, NULL, 5683, COAP_SERVICE_AUTOSTART);

/**
 * Function used to initialize the LEDs
 */
int init_leds()
{
	int ret;

	if (!gpio_is_ready_dt(&led_connection)) {
		LOG_ERR("Error: led device %s is not ready\n",
		       led_connection.port->name);
		return 0;
	}

	ret = gpio_pin_configure_dt(&led_connection, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Error %d: failed to configure %s pin %d\n",
		       ret, led_connection.port->name, led_connection.pin);
		return 0;
	}

	if (!gpio_is_ready_dt(&led_provisioning)) {
		LOG_ERR("Error: led device %s is not ready\n",
		       led_provisioning.port->name);
		return 0;
	}

	ret = gpio_pin_configure_dt(&led_provisioning, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Error %d: failed to configure %s pin %d\n",
		       ret, led_provisioning.port->name, led_provisioning.pin);
		return 0;
	}

	if (!gpio_is_ready_dt(&led_user)) {
		LOG_ERR("Error: led device %s is not ready\n",
		       led_user.port->name);
		return 0;
	}

	// Configure led_user as input and output pin at the same time so you can read the logical value
	// See: https://github.com/zephyrproject-rtos/zephyr/issues/48058
	ret = gpio_pin_configure_dt(&led_user, GPIO_INPUT | GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		LOG_ERR("Error %d: failed to configure %s pin %d\n",
		       ret, led_user.port->name, led_user.pin);
		return 0;
	}

	if (!gpio_is_ready_dt(&button)) {
		LOG_ERR("Error: button device %s is not ready\n",
		       button.port->name);
		return 0;
	}

	return 0;
}

/**
 * Callback function for the delayable work item
 * Calls the actual button callback function
 */
static void cooldown_expired(struct k_work *work)
{
    ARG_UNUSED(work);

    int val = gpio_pin_get_dt(&button);
    enum button_evt evt = val ? BUTTON_EVT_PRESSED : BUTTON_EVT_RELEASED;
    if (button_cb) {
        button_cb(evt);
    }
}

/**
 * Macro to define a delayable work item with its corresponding handler
 */
static K_WORK_DELAYABLE_DEFINE(cooldown_work, cooldown_expired);

/**
 * Button callback function that sets the deadline for the work
 */
void button_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins)
{
	k_work_reschedule(&cooldown_work, K_MSEC(1000));
}

/**
 * Helper structure to turn a button event into a string
 */
static char *helper_button_evt_str(enum button_evt evt)
{
	switch (evt) {
	case BUTTON_EVT_PRESSED:
		return "Pressed";
	case BUTTON_EVT_RELEASED:
		return "Released";
	default:
		return "Unknown";
	}
}

/**
 * Button event handler
 * Callback function that is invoked on a button press
 * Sends CoAP requests to the Matter bridge as part of the PoC
 */
static void button_event_handler(enum button_evt evt)
{
	LOG_INF("Button event: %s\n", helper_button_evt_str(evt));
	int ret;

	ret = init_coap_client();
	if (ret < 0) {
		LOG_ERR("Couldn't start CoAP Client");
		goto end;
	}

	// Send a PUT request to the Toggle ressource
	ret = matter_on_off_toggle_put();
	if (ret < 0) {
		LOG_ERR("Couldn`t send PUT to Toggle");
		goto end;
	}

	// Wait 10 seconds
	k_msleep(10000);

	// Send a PUT request to the OnTime ressource containing the value to write
	ret = matter_on_off_ontime_put();
	if (ret < 0) {
		LOG_ERR("Couldn`t send PUT to OnTime");
		goto end;
	}

	// Wait 10 seconds
	k_msleep(10000);

	// Send a GET request to the OnOff ressource
	ret = matter_on_off_onoff_get();
	if (ret < 0) {
		LOG_ERR("Couldn`t send GET to OnOff");
		goto end;
	}

end:
	close_socket();
	LOG_INF("Closed CoAP Client");
}

/**
 * Function used to initialize the buttons
 */
int init_buttons(button_event_handler_t handler)
{
	int err = -1;

    if (!handler) {
        return -EINVAL;
    }

    button_cb = handler;

	if (!device_is_ready(button.port)) {
		return -EIO;
	}

	err = gpio_pin_configure_dt(&button, GPIO_INPUT);
	if (err) {
        return err;
	}

	err = gpio_pin_interrupt_configure_dt(&button, GPIO_INT_EDGE_BOTH);
	if (err) {
		return err;
	}

	gpio_init_callback(&button_cb_data, button_pressed, BIT(button.pin));
	err = gpio_add_callback(button.port, &button_cb_data);
    if (err) {
        return err;
    }

    return 0;
}

/**
 * GET request handler for the onoff resource
 */
static int on_off_object_state_get(struct coap_resource *resource, struct coap_packet *request,
                  struct sockaddr *addr, socklen_t addr_len)
{
	static const char *on_msg = "1";
	static const char *off_msg = "0";

    uint8_t data[CONFIG_COAP_SERVER_MESSAGE_SIZE];
    struct coap_packet response;
    uint16_t id;
    uint8_t token[COAP_TOKEN_MAX_LEN];
    uint8_t tkl, type;

    type = coap_header_get_type(request);
    id = coap_header_get_id(request);
    tkl = coap_header_get_token(request, token);

    /* Determine response type */
    type = (type == COAP_TYPE_CON) ? COAP_TYPE_ACK : COAP_TYPE_NON_CON;

    coap_packet_init(&response, data, sizeof(data), COAP_VERSION_1, type, tkl, token,
                     COAP_RESPONSE_CODE_CONTENT, id);

    /* Set content format */
    coap_append_option_int(&response, COAP_OPTION_CONTENT_FORMAT,
                           COAP_CONTENT_FORMAT_TEXT_PLAIN);

    /* Append payload */
    coap_packet_append_payload_marker(&response);
	
    if (gpio_pin_get_dt(&led_user)) {
		coap_packet_append_payload(&response, (uint8_t *)on_msg, sizeof(on_msg));
	} else {
		coap_packet_append_payload(&response, (uint8_t *)off_msg, sizeof(off_msg));
	}

    /* Send to response back to the client */
    return coap_resource_send(resource, &response, addr, addr_len, NULL);
}

/**
 * PUT request handler for the onoff resource
 */
static int on_off_object_state_put(struct coap_resource *resource, struct coap_packet *request,
                  struct sockaddr *addr, socklen_t addr_len)
{
	const uint8_t *data;
	uint16_t data_len;
    data = coap_packet_get_payload(request, &data_len);
	char converted_data[data_len];
	strcpy(converted_data, data);
	if (strncmp(converted_data, "0", 1) == 0) {
		LOG_INF("Disabling LED");
		gpio_pin_set_dt(&led_user, 0);
	} else if (strncmp(converted_data, "1", 1) == 0) {
		LOG_INF("Enabling LED");
		gpio_pin_set_dt(&led_user, 1);
	} else {
		LOG_INF("Invalid Payload");
		LOG_INF("Actual String: %s With Length: %i", converted_data, sizeof(converted_data));
		return COAP_RESPONSE_CODE_BAD_REQUEST;
	}

    return COAP_RESPONSE_CODE_CHANGED;
}

/**
 * Add the state ressource as a CoAP ressource
 */
static const char * const on_off_object_state_path[] = { "42769", "0", "1", NULL};
COAP_RESOURCE_DEFINE(on_off_object_state_resource, coap_server, {
    .path = on_off_object_state_path,
    .get = on_off_object_state_get,
	.put = on_off_object_state_put,
});

/**
 * PUT request handler for the on resource
 */
static int on_off_object_on_put(struct coap_resource *resource, struct coap_packet *request,
                  struct sockaddr *addr, socklen_t addr_len)
{
	gpio_pin_set_dt(&led_user, 1);
	return COAP_RESPONSE_CODE_CHANGED;
}

/**
 * Add the on ressource as a CoAP ressource
 */
static const char * const on_off_object_on_path[] = { "42769", "0", "2", NULL};
COAP_RESOURCE_DEFINE(on_off_object_on_resource, coap_server, {
    .path = on_off_object_on_path,
    .put = on_off_object_on_put,
});

/**
 * PUT request handler for the off resource
 */
static int on_off_object_off_put(struct coap_resource *resource, struct coap_packet *request,
                  struct sockaddr *addr, socklen_t addr_len)
{
	gpio_pin_set_dt(&led_user, 0);
	return COAP_RESPONSE_CODE_CHANGED;
}

/**
 * Add the off ressource as a CoAP ressource
 */
static const char * const on_off_object_off_path[] = { "42769", "0", "3", NULL};
COAP_RESOURCE_DEFINE(on_off_object_off_resource, coap_server, {
    .path = on_off_object_off_path,
    .put = on_off_object_off_put,
});

/**
 * PUT request handler for the switch ressource
 */
static int on_off_object_switch_put(struct coap_resource *resource, struct coap_packet *request,
                  struct sockaddr *addr, socklen_t addr_len)
{
	gpio_pin_toggle_dt(&led_user);
	return COAP_RESPONSE_CODE_CHANGED;
}

/**
 * Add the switch ressource as a CoAP ressource
 */
static const char * const on_off_object_switch_path[] = { "42769", "0", "4", NULL};
COAP_RESOURCE_DEFINE(on_off_object_switch_resource, coap_server, {
    .path = on_off_object_switch_path,
    .put = on_off_object_switch_put,
});

/**
 * Main function
 * This function initializes the LEDs as well as the buttons
 * Afterwards it waits for a button press
 */
int main(void)
{
	int ret;

	LOG_INF("Starting CoAP Server and CoAP Client");

	// Initialize the LEDs
	ret = init_leds();
	if (ret) {
		LOG_ERR("Could not initialize leds, err code: %d", ret);
		goto end;
	}

	// Initialize the buttons
	ret = init_buttons(button_event_handler);
	if (ret) {
		LOG_ERR("Cannot init buttons (error: %d)", ret);
		goto end;
	}

	// Endless loop to keep
	while (true)
	{
		k_msleep(SLEEP_TIME_MS);
	}
	
end:
	return 0;
}
