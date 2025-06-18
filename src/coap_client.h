#ifndef __OT_COAP_CLIENT_H__
#define __OT_COAP_CLIENT_H__

#define COAP_PORT 5683

/**
 * Function used to initialize the coap client
 */
int init_coap_client();

/**
 * Function used to send a PUT request to the Toggle ressource
 */
int matter_on_off_toggle_put();

/**
 * Function used to send a GET request to the OnOff ressource
 */
int matter_on_off_onoff_get();

/**
 * Function used to send a PUT request to the OnTime ressource
 */
int matter_on_off_ontime_put();

/**
 * Function used to close the coap client socket
 */
int close_socket();

#endif