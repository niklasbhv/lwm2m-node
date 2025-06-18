#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(coap_client, LOG_LEVEL_DBG);

#include <zephyr/net/socket.h>
#include <zephyr/net/udp.h>
#include <zephyr/net/coap.h>
#include "net_private.h"

#include "coap_client.h"

/* CoAP socket fd */
static int sock;

#define MAX_COAP_MSG_LEN 256

/**
 * Function used to handle a coap reply
 * Prints the response to a coap request
 */
static int process_simple_coap_reply(void)
{
	struct coap_packet reply;
	uint8_t *data;
	int rcvd;
	int ret;

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	rcvd = recv(sock, data, MAX_COAP_MSG_LEN, MSG_DONTWAIT);
	if (rcvd == 0) {
		ret = -EIO;
		goto end;
	}

	if (rcvd < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK) {
			ret = 0;
		} else {
			ret = -errno;
		}

		goto end;
	}

	net_hexdump("Response", data, rcvd);

	ret = coap_packet_parse(&reply, data, rcvd, NULL, 0);
	if (ret < 0) {
		LOG_ERR("Invalid data received");
	}

end:
	k_free(data);

	return ret;
}


/**
 * Function used to initialize the coap client
 */
int init_coap_client()
{
	int ret = 0;
	struct sockaddr_in6 addr6;

	addr6.sin6_family = AF_INET6;
	addr6.sin6_port = htons(COAP_PORT);
	addr6.sin6_scope_id = 0U;

	inet_pton(AF_INET6, CONFIG_NET_CONFIG_PEER_IPV6_ADDR,
		  &addr6.sin6_addr);

	sock = socket(addr6.sin6_family, SOCK_DGRAM, IPPROTO_UDP);
	if (sock < 0) {
		LOG_ERR("Failed to create UDP socket %d", errno);
		return -errno;
	}

	ret = connect(sock, (struct sockaddr *)&addr6, sizeof(addr6));
	if (ret < 0) {
		LOG_ERR("Cannot connect to UDP remote : %d", errno);
		return -errno;
	}

	return 0;
}

/**
 * Function used to send a PUT request to the Toggle ressource
 */
int matter_on_off_toggle_put(void)
{
	const char * const on_off_toggle_path[] = { "42770", "0", "8", NULL };
	struct coap_packet request;
	const char * const *p;
	uint8_t *data;
	int r;

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	r = coap_packet_init(&request, data, MAX_COAP_MSG_LEN,
			     COAP_VERSION_1, COAP_TYPE_CON,
			     COAP_TOKEN_MAX_LEN, coap_next_token(),
			     COAP_METHOD_PUT, coap_next_id());
	if (r < 0) {
		LOG_ERR("Failed to init CoAP message");
		goto end;
	}

	for (p = on_off_toggle_path; p && *p; p++) {
		r = coap_packet_append_option(&request, COAP_OPTION_URI_PATH,
					      *p, strlen(*p));
		if (r < 0) {
			LOG_ERR("Unable add option to request");
			goto end;
		}
	}

	net_hexdump("Request", request.data, request.offset);

	r = send(sock, request.data, request.offset, 0);

end:
	k_free(data);

	return r;
}

/**
 * Function used to send a GET request to the OnOff ressource
 */
int matter_on_off_onoff_get()
{
	const char * const on_off_onoff_path[] = { "42770", "0", "5", NULL };
	struct coap_packet request;
	const char * const *p;
	uint8_t *data;
	int r;

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	r = coap_packet_init(&request, data, MAX_COAP_MSG_LEN,
			     COAP_VERSION_1, COAP_TYPE_CON,
			     COAP_TOKEN_MAX_LEN, coap_next_token(),
			     COAP_METHOD_GET, coap_next_id());
	if (r < 0) {
		LOG_ERR("Failed to init CoAP message");
		goto end;
	}

	for (p = on_off_onoff_path; p && *p; p++) {
		r = coap_packet_append_option(&request, COAP_OPTION_URI_PATH,
					      *p, strlen(*p));
		if (r < 0) {
			LOG_ERR("Unable add option to request");
			goto end;
		}
	}

	net_hexdump("Request", request.data, request.offset);

	r = send(sock, request.data, request.offset, 0);

	r = process_simple_coap_reply();

end:
	k_free(data);

	return r;
}

/**
 * Function used to send a PUT request to the OnTime ressource
 */
int matter_on_off_ontime_put()
{
	uint8_t payload[] = "20";
	const char * const on_off_ontime_path[] = { "42770", "0", "3", NULL };
	struct coap_packet request;
	const char * const *p;
	uint8_t *data;
	int r;

	data = (uint8_t *)k_malloc(MAX_COAP_MSG_LEN);
	if (!data) {
		return -ENOMEM;
	}

	r = coap_packet_init(&request, data, MAX_COAP_MSG_LEN,
			     COAP_VERSION_1, COAP_TYPE_CON,
			     COAP_TOKEN_MAX_LEN, coap_next_token(),
			     COAP_METHOD_PUT, coap_next_id());
	if (r < 0) {
		LOG_ERR("Failed to init CoAP message");
		goto end;
	}

	for (p = on_off_ontime_path; p && *p; p++) {
		r = coap_packet_append_option(&request, COAP_OPTION_URI_PATH,
					      *p, strlen(*p));
		if (r < 0) {
			LOG_ERR("Unable add option to request");
			goto end;
		}
	}


	r = coap_packet_append_payload_marker(&request);
	if (r < 0) {
		LOG_ERR("Unable to append payload marker");
		goto end;
	}

	r = coap_packet_append_payload(&request, (uint8_t *)payload, sizeof(payload) - 1);
	if (r < 0) {
		LOG_ERR("Not able to append payload");
		goto end;
	}

	net_hexdump("Request", request.data, request.offset);

	r = send(sock, request.data, request.offset, 0);

end:
	k_free(data);

	return r;
}

/**
 * Function used to close the coap client socket
 */
int close_socket(void)
{
    /* Close the socket */
	(void)close(sock);
    return 0;
}
