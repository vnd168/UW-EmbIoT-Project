#include <zephyr/kernel.h>
#include <zephyr/drivers/gpio.h>

#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>
#include <zephyr/net/http/client.h>

/* Project: */
#include <zephyr/types.h>
/* IOTEMBSYS4: Add required includes for TCP/sockets */
#include <zephyr/net/net_ip.h>
#include <zephyr/net/socket.h>

#include <zephyr/logging/log.h>
LOG_MODULE_REGISTER(main, CONFIG_APP_LOG_LEVEL);

/* IOTEMBSYS: Add required iheadersmport shell and/or others */
//#include <zephyr/shell/shell.h>

/* IOTEMBSYS: Add required headers for settings */
#include <zephyr/settings/settings.h>
#include <zephyr/storage/flash_map.h>

/* IOTEMBSYS: Add required headers for protobufs */
#include <pb_encode.h>
#include <pb_decode.h>
#include "api/api.pb.h"

#include <stdlib.h>
#include <stdio.h>
#include "app_version.h"

// Helper for converting macros into strings
#define str(s) #s
#define xstr(s) str(s)

/* 1000 msec = 1 sec */
#define DEFAULT_SLEEP_TIME_MS   1000

/* The devicetree node identifier for the "led0" alias. */
#define LED0_NODE DT_ALIAS(led0)

/* IOTEMBSYS: Add joystick key declarations. */
#define SW0_NODE	DT_ALIAS(sw0)
#define SW1_NODE	DT_ALIAS(sw1)
#define SW2_NODE	DT_ALIAS(sw2)
#define SW3_NODE	DT_ALIAS(sw3)
#define SW4_NODE	DT_ALIAS(sw4)
static const struct gpio_dt_spec sw0 = GPIO_DT_SPEC_GET_OR(SW0_NODE, gpios,
							      {0});
static const struct gpio_dt_spec sw1 = GPIO_DT_SPEC_GET_OR(SW1_NODE, gpios,
							      {0});
static const struct gpio_dt_spec sw2 = GPIO_DT_SPEC_GET_OR(SW2_NODE, gpios,
							      {0});
static const struct gpio_dt_spec sw3 = GPIO_DT_SPEC_GET_OR(SW3_NODE, gpios,
							      {0});								  								  								  
static const struct gpio_dt_spec sw4 = GPIO_DT_SPEC_GET_OR(SW4_NODE, gpios,
							      {0});								  
static struct gpio_callback button_cb_data_0;
static struct gpio_callback button_cb_data_1;
static struct gpio_callback button_cb_data_2;
static struct gpio_callback button_cb_data_3;
static struct gpio_callback button_cb_data_4;

/* IOTEMBSYS: Define/declare partitions here */
#define SLOT1_PARTITION slot1_partition
#define SLOT1_PARTITION_ID FIXED_PARTITION_ID(SLOT1_PARTITION)

#define STORAGE_PARTITION storage_partition
#define STORAGE_PARTITION_ID FIXED_PARTITION_ID(STORAGE_PARTITION)

/*
 * A build error on this line means your board is unsupported.
 * See the blinky sample documentation for information on how to fix this.
 */
static const struct gpio_dt_spec led = GPIO_DT_SPEC_GET(LED0_NODE, gpios);

/* The amount of time between GPIO blinking. */
static uint32_t blink_interval_ = DEFAULT_SLEEP_TIME_MS;

/* IOTEMBSYS: Add synchronization to unblock the sender task */
static struct k_event unblock_sender_;
typedef enum {
	BUTTON_ACTION_NONE = 0,
	BUTTON_ACTION_WATER_DATA_SEND,
	BUTTON_ACTION_OTA_DOWNLOAD,
	BUTTON_ACTION_WATER_INDEX_COUNT,
	BUTTON_ACTION_GET_OTA_PATH,
} button_action_e;

/* IOTEMBSYS: Add synchronization to pass the socket to the receiver task */
struct k_fifo socket_queue_;

/* IOTEMBSYS: Create a buffer for receiving HTTP responses */
#define MAX_RECV_BUF_LEN 1024
static uint8_t recv_buf_[MAX_RECV_BUF_LEN];

/* IOTEMBSYS: Create a buffer for receiving the OTA path */
// TODO(mskobov): this should not be static!
static char ota_path_[128] = "/zephyr.signed.bin";

/* IOTEMBSYS: Consider provisioning a device ID. */
static const char kDeviceId[] = "12345";

static void change_blink_interval(uint32_t new_interval_ms) {
	blink_interval_ = new_interval_ms;
}

/* Project: Define water app */
#define WATER_REPORT_CYCLE 30000 //In milisecond
#define LEAKAGE_THRESHOLD 30 //Diference index of 2 cycles

/* IOTEMBSYS7: Define a default settings val and configuration access */
#define DEFAULT_BOOT_COUNT_VALUE 0
static uint8_t boot_count = DEFAULT_BOOT_COUNT_VALUE;

/* IOTEMBSYS4: Configure the appropriate IP address and port */
// WARNING: This IP might not be static! Use a DNS lookup tool
// to get the latest IP.
#define TCPBIN_IP "54.210.199.104"
#define TCPBIN_PORT 4242

static int setup_socket(sa_family_t family, const char *server, int port,
			int *sock, struct sockaddr *addr, socklen_t addr_len)
{
	const char *family_str = family == AF_INET ? "IPv4" : "IPv6";
	int ret = 0;

	memset(addr, 0, addr_len);

	net_sin(addr)->sin_family = AF_INET;
	net_sin(addr)->sin_port = htons(port);
	inet_pton(family, server, &net_sin(addr)->sin_addr);

	*sock = socket(family, SOCK_STREAM, IPPROTO_TCP);

	if (*sock < 0) {
		LOG_ERR("Failed to create %s HTTP socket (%d)", family_str,
			-errno);
	}

	return ret;
}

static int connect_socket(sa_family_t family, const char *server, int port,
			  int *sock, struct sockaddr *addr, socklen_t addr_len)
{
	int ret;

	ret = setup_socket(family, server, port, sock, addr, addr_len);
	if (ret < 0 || *sock < 0) {
		return -1;
	}

	ret = connect(*sock, addr, addr_len);
	if (ret < 0) {
		LOG_ERR("Cannot connect to %s remote (%d)",
			family == AF_INET ? "IPv4" : "IPv6",
			-errno);
		ret = -errno;
	}

	return ret;
}

static bool await_data(int sock, int32_t timeout) {
	int ret;
	struct zsock_pollfd fds[1];
	int nfds = 1;

	fds[0].fd = sock;
	fds[0].events = ZSOCK_POLLIN;

	ret = zsock_poll(fds, nfds, timeout);
	if (ret == 0) {
		LOG_DBG("Timeout");
		return false;
	} else if (ret < 0) {
		return false;
	}
	if (fds[0].revents & (ZSOCK_POLLERR | ZSOCK_POLLNVAL)) {
		return false;
	} else if (fds[0].revents & ZSOCK_POLLHUP) {
		/* Connection closed */
		LOG_DBG("Connection closed");
		return false;
	} else if (fds[0].revents & ZSOCK_POLLIN) {
		return true;
	}

	return false;
}

struct socket_queue_item {
	void* fifo_reserved;
	int sock;
};

/* Project: Define a water index */
#define DEFAULT_WATER_INDEX_COUNT_VALUE 0
static uint8_t water_index_count = DEFAULT_WATER_INDEX_COUNT_VALUE;
static int provisioning_settings_set(const char *name, size_t len,
                            settings_read_cb read_cb, void *cb_arg)
{
    const char *next;
    int rc;

    if (settings_name_steq(name, "boot_count", &next) && !next) {
        if (len != sizeof(boot_count)) {
            return -EINVAL;
        }

        rc = read_cb(cb_arg, &boot_count, sizeof(boot_count));
        if (rc >= 0) {
            /* key-value pair was properly read.
             * rc contains value length.
             */
            return 0;
        }
        /* read-out error */
        return rc;
    }

    return -ENOENT;
}

/* Project:  */
static int provisioning_settings_set_water(const char *name, size_t len,
                            settings_read_cb read_cb, void *cb_arg)
{
    const char *next;
    int rc;

    if (settings_name_steq(name, "water_index_count", &next) && !next) {
        if (len != sizeof(water_index_count)) {
            return -EINVAL;
        }

        rc = read_cb(cb_arg, &water_index_count, sizeof(water_index_count));
        if (rc >= 0) {
            /* key-value pair was properly read.
             * rc contains value length.
             */
            return 0;
        }
        /* read-out error */
        return rc;
    }

    return -ENOENT;
}
static int provisioning_settings_export(int (*storage_func)(const char *name,
                                                   const void *value,
                                                   size_t val_len))
{
    return storage_func("provisioning/boot_count", &boot_count, sizeof(boot_count));
}

static int provisioning_settings_export_water(int (*storage_func)(const char *name,
                                                   const void *value,
                                                   size_t val_len))
{
    return storage_func("provisioning/water_index_count", &water_index_count, sizeof(water_index_count));
}

struct settings_handler my_conf = {
    .name = "provisioning",
    .h_set = provisioning_settings_set,
    .h_export = provisioning_settings_export
};

struct settings_handler my_conf_water = {
    .name = "provisioning_water",
	.h_set = provisioning_settings_set_water,
    .h_export = provisioning_settings_export_water,
};
/* IOTEMBSYS: Add joystick press handler. Metaphorical bonus points for debouncing. */
static void button_pressed(const struct device *dev, struct gpio_callback *cb,
		    uint32_t pins) {
	printk("Button %d pressed at %" PRIu32 "\n", pins, k_cycle_get_32());
	k_msleep(100);

	uint32_t interval_ms = 0;
	if (pins == BIT(sw0.pin)) {
		interval_ms = 100;
	} else if (pins == BIT(sw1.pin)) {
		// Down
		interval_ms = 200;
		k_event_set(&unblock_sender_, (1 << BUTTON_ACTION_OTA_DOWNLOAD));
	} else if (pins == BIT(sw2.pin)) {
		// Right
		interval_ms = 500;
		k_event_set(&unblock_sender_, (1 << BUTTON_ACTION_WATER_DATA_SEND));
	} else if (pins == BIT(sw3.pin)) {
		// Up
		interval_ms = 1000;
		k_event_set(&unblock_sender_, (1 << BUTTON_ACTION_WATER_INDEX_COUNT));
	} else if (pins == BIT(sw4.pin)) {
		// Left
		k_event_set(&unblock_sender_, (1 << BUTTON_ACTION_GET_OTA_PATH));
		interval_ms = 2000;
	} else {
		printk("Unrecognized pin");
	}

	if (interval_ms != 0) {
		printk("Setting interval to %d", interval_ms);
		change_blink_interval(interval_ms);
	}
}

static int init_joystick_gpio(const struct gpio_dt_spec* button, struct gpio_callback* data) {
	int ret = -1;

	if (!gpio_is_ready_dt(button)) {
		printk("Error: button device %s is not ready\n",
		       button->port->name);
		return ret;
	}

	ret = gpio_pin_configure_dt(button, GPIO_INPUT);
	if (ret != 0) {
		printk("Error %d: failed to configure %s pin %d\n",
		       ret, button->port->name, button->pin);
		return ret;
	}

	ret = gpio_pin_interrupt_configure_dt(button,
					      GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		printk("Error %d: failed to configure interrupt on %s pin %d\n",
			ret, button->port->name, button->pin);
		return ret;
	}

	gpio_init_callback(data, button_pressed, BIT(button->pin));
	gpio_add_callback(button->port, data);
	return ret;
}
// Water meter handler
/* Project:  */
static void count_water_index() {
	/* Project: Increment, save, and log the water count. */
	water_index_count++;
	settings_save_one("provisioning/water_index_count", &water_index_count, sizeof(water_index_count));

	LOG_INF("water_index_count: %d\n", water_index_count);
}

// The IP of your EC2 instance
static const char kEchoServerIP[] = "115.78.92.253";
static const int kEchoServerPort = 20003;

void water_tcp_request(uint32_t water_index_count) {
	int sock;
	struct sockaddr_in addr4;

	char kData[100];
	static const char kIndex[20];
	struct socket_queue_item socket_item;

	static char recv_buf_[100];

	int ret = snprintf(kIndex, sizeof(kIndex), "%u", 10000);

	if (ret >= 0) {
		// buffer contains the string representation of your_number
		printk("uint32_t as string: %s\n", kIndex);
	} else {
		// Handle error, buffer may be truncated
		printk("snprintf failed\n");
	}

	ret = snprintf(kData, sizeof(kData), "{\"water_index\": %d}\r\n", water_index_count);

	if (ret >= 0) {
		// buffer contains the string representation of your_number
		printk("uint32_t as string: %s\n", kIndex);
	} else {
		// Handle error, buffer may be truncated
		printk("snprintf failed\n");
	}

	// req_in_progress_ = true;

	printk("Connecting\n");

	if (connect_socket(AF_INET, kEchoServerIP, kEchoServerPort,  &sock, (struct sockaddr *)&addr4, sizeof(addr4)) < 0) {
		LOG_ERR("Connect failed");
		return;
	}

	printk("Connected\n");

	(void)send(sock, kData, sizeof(kData), 0);
	printk("Data sent\n");

	// Relinquish ownership of this socket by passing it to the receiver task
	socket_item.sock = sock;
	k_fifo_put(&socket_queue_, &socket_item);
	printk("Socket passed to receiver\n");

	printk("Receiving data...");
	bool data_exists = await_data(socket_item.sock, 5000);
	if (!data_exists) {
		goto close;
	}
	if (recv(socket_item.sock, recv_buf_, sizeof(recv_buf_) - 1, 0) == 0) {
		printk("No data was received");
	} else {
		printk("Received: %s\n", recv_buf_);
	}
close:
		printk("Closing socket\n");
		close(socket_item.sock);
		printk("Socket closed\n");
}

void alert_tcp_request() {
	int sock;
	struct sockaddr_in addr4;

	static const char kData[] = "{\"alert\":\"leakage\"}\r\n";
	static const char kIndex[20];
	struct socket_queue_item socket_item;

	static char recv_buf_[100];

	int ret = snprintf(kIndex, sizeof(kIndex), "%u", 10000);

	if (ret >= 0) {
		// buffer contains the string representation of your_number
		printk("uint32_t as string: %s\n", kIndex);
	} else {
		// Handle error, buffer may be truncated
		printk("snprintf failed\n");
	}

	// req_in_progress_ = true;

	printk("Connecting\n");

	if (connect_socket(AF_INET, kEchoServerIP, kEchoServerPort,  &sock, (struct sockaddr *)&addr4, sizeof(addr4)) < 0) {
		LOG_ERR("Connect failed");
		return;
	}

	printk("Connected\n");

	(void)send(sock, kData, sizeof(kData), 0);
	printk("Data sent\n");

	// Relinquish ownership of this socket by passing it to the receiver task
	socket_item.sock = sock;
	k_fifo_put(&socket_queue_, &socket_item);
	printk("Socket passed to receiver\n");

	printk("Receiving data...");
	bool data_exists = await_data(socket_item.sock, 5000);
	if (!data_exists) {
		goto close;
	}
	if (recv(socket_item.sock, recv_buf_, sizeof(recv_buf_) - 1, 0) == 0) {
		printk("No data was received");
	} else {
		printk("Received: %s\n", recv_buf_);
	}
close:
		printk("Closing socket\n");
		close(socket_item.sock);
		printk("Socket closed\n");
}
				
//
// Networking/sockets helpers
//
static void dump_addrinfo(const struct addrinfo *ai) {
	printf("addrinfo @%p: ai_family=%d, ai_socktype=%d, ai_protocol=%d, "
	       "sa_family=%d, sin_port=%x\n",
	       ai, ai->ai_family, ai->ai_socktype, ai->ai_protocol,
	       ai->ai_addr->sa_family,
	       ((struct sockaddr_in *)ai->ai_addr)->sin_port);
}

static int get_addr_if_needed(struct addrinfo **ai, const char* host, const char* port) {
	if (*ai != NULL) {
		// We already have the address.
		return 0;
	}
	struct addrinfo hints;
	int st;

	memset(&hints, 0, sizeof(hints));
	hints.ai_family = AF_INET;
	hints.ai_socktype = SOCK_STREAM;
	st = getaddrinfo(host, port, &hints, ai);
	LOG_INF("getaddrinfo status: %d\n", st);
	if (st == 0) {
		dump_addrinfo(*ai);
	}
	return st;
}

//
// Generic HTTP Request Section
//

// WARNING: These IPs are not static! Use a DNS lookup tool
// to get the latest IP.
#define TCPBIN_IP "45.79.112.203"
#define HTTPBIN_IP "54.204.94.184"
#define TCP_PORT 4242
#define IS_POST_REQ 1
#define USE_PROTO 1

#define HTTPBIN_PORT 80
#define HTTPBIN_HOST "httpbin.org"
static struct addrinfo* httpbin_addr_;

/* IOTEMBSYS: Create a HTTP response handler/callback. */
void http_response_cb(struct http_response *rsp,
			enum http_final_call final_data,
			void *user_data)
{
	if (final_data == HTTP_DATA_MORE) {
		LOG_INF("Partial data received (%zd bytes)", rsp->data_len);
	} else if (final_data == HTTP_DATA_FINAL) {
		LOG_INF("All the data received (%zd bytes)", rsp->data_len);
		
		// This assumes the response fits in a single buffer.
		recv_buf_[rsp->data_len] = '\0';
	}

	LOG_INF("Response to %s", (const char *)user_data);
	LOG_INF("Response status %s", rsp->http_status);
}

int http_payload_cb(int sock, struct http_request *req, void *user_data) {
	const char *content[] = {
		"foobar",
		"chunked",
		"last"
	};
	char tmp[64];
	int i, pos = 0;

	for (i = 0; i < ARRAY_SIZE(content); i++) {
		pos += snprintk(tmp + pos, sizeof(tmp) - pos,
				"%x\r\n%s\r\n",
				(unsigned int)strlen(content[i]),
				content[i]);
	}

	pos += snprintk(tmp + pos, sizeof(tmp) - pos, "0\r\n\r\n");

	(void)send(sock, tmp, pos, 0);

	return pos;
}

/* IOTEMBSYS: Implement the HTTP client functionality */
static void generic_http_request(void) {
	int sock;
	const int32_t timeout = 5 * MSEC_PER_SEC;

	// Get the IP address of the domain
	if (get_addr_if_needed(&httpbin_addr_, HTTPBIN_HOST, xstr(HTTPBIN_PORT)) != 0) {
		LOG_ERR("DNS lookup failed");
		return;
	}

	// Create a socket using parameters that the modem allows.
	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		LOG_ERR("Creating socket failed");
		return;
	}
	if (connect(sock, httpbin_addr_->ai_addr, httpbin_addr_->ai_addrlen) < 0) {
		LOG_ERR("Connecting to socket failed");
		return;
	}

	struct http_request req;

	memset(&req, 0, sizeof(req));
	memset(recv_buf_, 0, sizeof(recv_buf_));

#if !IS_POST_REQ
	req.method = HTTP_GET;
	req.url = "/get";
#else
	req.method = HTTP_POST;
	req.url = "/post";
	req.payload_cb = http_payload_cb;
	// This must match the payload-generating function!
	req.payload_len = 37;
#endif // IS_POST_REQ
	req.host = HTTPBIN_HOST;
	req.protocol = "HTTP/1.1";
	req.response = http_response_cb;
	req.recv_buf = recv_buf_;
	req.recv_buf_len = sizeof(recv_buf_);

	// This request is synchronous and blocks the thread.
	LOG_INF("Sending HTTP request");
	int ret = http_client_req(sock, &req, timeout, "IPv4 GET");
	if (ret > 0) {
		LOG_INF("HTTP request sent %d bytes", ret);
	} else {
		LOG_ERR("HTTP request failed: %d", ret);
	}

	LOG_INF("Closing the socket");
	close(sock);
}

//
// Backend Request Section
//

// You will need to change this to match your host
// WARNING: This will change with each new EC2 instance!
#define EC2_HOST "ec2-3-84-6-30.compute-1.amazonaws.com"
#define BACKEND_PORT 8080
#define BACKEND_HOST EC2_HOST ":8080"
static struct addrinfo* backend_addr_;

/* IOTEMBSYS: Add protobuf encoding and decoding. */
static bool encode_status_update_request(uint8_t *buffer, size_t buffer_size, size_t *message_length)
{
	bool status;

	/* Allocate space on the stack to store the message data.
	 *
	 * Nanopb generates simple struct definitions for all the messages.
	 * - check out the contents of api.pb.h!
	 * It is a good idea to always initialize your structures
	 * so that you do not have garbage data from RAM in there.
	 */
	StatusUpdateRequest message = StatusUpdateRequest_init_zero;

	/* Create a stream that will write to our buffer. */
	pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);

	/* Fill in the reboot count */
	message.boot_count = boot_count;

	/* Now we are ready to encode the message! */
	status = pb_encode(&stream, StatusUpdateRequest_fields, &message);
	*message_length = stream.bytes_written;

	if (!status) {
		printk("Encoding failed: %s\n", PB_GET_ERROR(&stream));
	}

	return status;
}

static bool decode_status_update_response(uint8_t *buffer, size_t message_length)
{
	bool status = false;
	if (message_length == 0) {
		LOG_WRN("Message length is 0");
		return status;
	}

	/* Allocate space for the decoded message. */
	StatusUpdateResponse message = StatusUpdateResponse_init_zero;

	/* Create a stream that reads from the buffer. */
	pb_istream_t stream = pb_istream_from_buffer(buffer, message_length);

	/* Now we are ready to decode the message. */
	status = pb_decode(&stream, StatusUpdateResponse_fields, &message);

	/* Check for errors... */
	if (status) {
		/* Print the data contained in the message. */
		printk("Response message: %s\n", message.message);
	} else {
		printk("Decoding failed: %s\n", PB_GET_ERROR(&stream));
	}

	return status;
}

int http_proto_payload_gen(uint8_t* buffer, size_t buf_size) {
	size_t message_length;

	/* Encode our message */
	if (!encode_status_update_request(buffer, buf_size, &message_length)) {
		LOG_ERR("Encoding request failed");
		return 0;
	} else {
		LOG_INF("Sending proto to server. Length: %d", (int)message_length);
	}

	return (int)message_length;
}

void http_proto_response_cb(struct http_response *rsp,
			enum http_final_call final_data,
			void *user_data)
{
	if (final_data == HTTP_DATA_MORE) {
		LOG_INF("Partial data received (%zd bytes)", rsp->data_len);
	} else if (final_data == HTTP_DATA_FINAL) {
		LOG_INF("All the data received (%zd bytes)", rsp->data_len);

		// Decode the protobuf response.
		decode_status_update_response(rsp->body_frag_start, rsp->body_frag_len);
	}

	LOG_INF("Response to %s", (const char *)user_data);
	LOG_INF("Response status %s", rsp->http_status);
}


/* IOTEMBSYS: Implement the HTTP client functionality */
static void backend_http_request(void) {
	int sock;
	const int32_t timeout = 5 * MSEC_PER_SEC;

	// Get the IP address of the domain
	if (get_addr_if_needed(&backend_addr_, EC2_HOST, xstr(BACKEND_PORT)) != 0) {
		LOG_ERR("DNS lookup failed");
		return;
	}

	// Create a socket using parameters that the modem allows.
	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		LOG_ERR("Creating socket failed");
		return;
	}
	if (connect(sock, backend_addr_->ai_addr, backend_addr_->ai_addrlen) < 0) {
		LOG_ERR("Connecting to socket failed");
		return;
	}

	struct http_request req;

	memset(&req, 0, sizeof(req));
	memset(recv_buf_, 0, sizeof(recv_buf_));

	req.method = HTTP_POST;
	req.url = "/status_update";
	req.host = BACKEND_HOST;
	req.protocol = "HTTP/1.1";
	req.payload_len = http_proto_payload_gen(recv_buf_, sizeof(recv_buf_));
	req.payload = req.payload_len ? recv_buf_ : NULL;
	req.response = http_proto_response_cb;
	req.recv_buf = recv_buf_;
	req.recv_buf_len = sizeof(recv_buf_);

	// This request is synchronous and blocks the thread.
	LOG_INF("Sending HTTP request");
	int ret = http_client_req(sock, &req, timeout, "IPv4 GET");
	if (ret > 0) {
		LOG_INF("HTTP request sent %d bytes", ret);
	} else {
		LOG_ERR("HTTP request failed: %d", ret);
	}

	LOG_INF("Closing the socket");
	close(sock);
}

/* IOTEMBSYS: Create a HTTP request and response with protobuf. */
static bool encode_ota_update_request(uint8_t *buffer, size_t buffer_size, size_t *message_length)
{
	bool status;

	/* Allocate space on the stack to store the message data.
	 *
	 * Nanopb generates simple struct definitions for all the messages.
	 * - check out the contents of api.pb.h!
	 * It is a good idea to always initialize your structures
	 * so that you do not have garbage data from RAM in there.
	 */
	OTAUpdateRequest message = OTAUpdateRequest_init_zero;

	/* Create a stream that will write to our buffer. */
	pb_ostream_t stream = pb_ostream_from_buffer(buffer, buffer_size);
	
	/* TODO: fill out the actual state. */
	message.state = OTAState_OTA_STATE_NONE;
	strncpy(message.version, APP_VERSION_STR, sizeof(message.version));
	strncpy(message.device_id, kDeviceId, sizeof(message.device_id));

	/* Now we are ready to encode the message! */
	status = pb_encode(&stream, OTAUpdateRequest_fields, &message);
	*message_length = stream.bytes_written;

	if (!status) {
		printk("Encoding failed: %s\n", PB_GET_ERROR(&stream));
	}

	return status;
}

/* IOTEMBSYS9: Implement decoding of the server OTA response */
static bool decode_ota_update_response(uint8_t *buffer, size_t message_length)
{
	bool status = false;
	if (message_length == 0) {
		LOG_WRN("Message length is 0");
		return status;
	}

	/* Allocate space for the decoded message. */
	OTAUpdateResponse message = OTAUpdateResponse_init_zero;

	/* Create a stream that reads from the buffer. */
	pb_istream_t stream = pb_istream_from_buffer(buffer, message_length);

	/* Now we are ready to decode the message. */
	status = pb_decode(&stream, OTAUpdateResponse_fields, &message);

	/* Check for errors... */
	if (status) {
		/* Print the data contained in the message. */
		printk("OTA path: %s\n", message.path);
		strncpy(ota_path_, message.path, sizeof(ota_path_));
	} else {
		printk("Decoding failed: %s\n", PB_GET_ERROR(&stream));
	}

	return status;
}

/* IOTEMBSYS9: Implement encoding of the server OTA request */
static int http_ota_proto_payload_get(uint8_t* buffer, size_t buf_size) {
	size_t message_length;

	/* Encode our message */
	if (!encode_ota_update_request(buffer, buf_size, &message_length)) {
		LOG_ERR("Encoding request failed");
		return 0;
	} else {
		LOG_INF("Sending proto to server. Length: %d", (int)message_length);
	}
	return (int)message_length;
}

/* IOTEMBSYS9: Implement the server OTA response callback. */
static void http_ota_proto_response_cb(struct http_response *rsp,
			enum http_final_call final_data,
			void *user_data)
{
	if (final_data == HTTP_DATA_MORE) {
		LOG_INF("Partial data received (%zd bytes)", rsp->data_len);
	} else if (final_data == HTTP_DATA_FINAL) {
		LOG_INF("All the data received (%zd bytes)", rsp->data_len);

		// Decode the protobuf response.
		decode_ota_update_response(rsp->body_frag_start, rsp->body_frag_len);
	}

	LOG_INF("Response to %s", (const char *)user_data);
	LOG_INF("Response status %s", rsp->http_status);
}

/* 
 * IOTEMBSYS9: Implement the server OTA request functionality. This should
 * open a socket, encode the OTA request, send that request to the EC2 instance
 * and get back a response. The response should contain the image path, or some
 * indication (like a bool) that the OTA is not needed. When an OTA is needed,
 * the URL or path should be used for making the request in `http_ota_request`.
 * Look at `backend_http_request` if you need a reminder of how to do this.
 */
static void backend_ota_http_request(void) {
	int sock;
	const int32_t timeout = 5 * MSEC_PER_SEC;

	// Get the IP address of the domain
	if (get_addr_if_needed(&backend_addr_, EC2_HOST, xstr(BACKEND_PORT)) != 0) {
		LOG_ERR("DNS lookup failed");
		return;
	}

	// Create a socket using parameters that the modem allows.
	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		LOG_ERR("Creating socket failed");
		return;
	}
	if (connect(sock, backend_addr_->ai_addr, backend_addr_->ai_addrlen) < 0) {
		LOG_ERR("Connecting to socket failed");
		return;
	}

	struct http_request req;

	memset(&req, 0, sizeof(req));
	memset(recv_buf_, 0, sizeof(recv_buf_));

	req.host = BACKEND_HOST;
	req.protocol = "HTTP/1.1";
	req.method = HTTP_POST;
	req.url = "/ota";
	req.payload = recv_buf_;
	req.payload_len = http_ota_proto_payload_get(recv_buf_, sizeof(recv_buf_));
	req.payload = req.payload_len ? recv_buf_ : NULL;
	req.response = http_ota_proto_response_cb;
	req.recv_buf = recv_buf_;
	req.recv_buf_len = sizeof(recv_buf_);

	LOG_INF("OTA SERVER is %s", req.recv_buf);

	// This request is synchronous and blocks the thread.
	LOG_INF("Sending OTA HTTP request");
	int ret = http_client_req(sock, &req, timeout, "IPv4 GET");
	if (ret > 0) {
		LOG_INF("HTTP request sent %d bytes", ret);
	} else {
		LOG_ERR("HTTP request failed: %d", ret);
	}

	LOG_INF("Closing the socket");
	close(sock);
}

//
// OTA Download Section
//
/* IOTEMBSYS9: Implement the OTA HTTP download. */
#define OTA_HTTP_PORT 80
/* IOTEMBSYS9: Your host is probably different. */
#define OTA_HOST "vietnd28-uw-firmware-releases.s3.amazonaws.com"
static int total_read_size;
static int total_write_size;
static int content_length_;
static struct flash_area *image_area;
static struct addrinfo* ota_addr_;

/* 
 * IOTEMBSYS9: This function is given to you and should be used as the payload callback.
 * There are a few cases it doesn't handle correctly, but you are unlikely to run into them.
 */
void http_ota_response_cb(struct http_response *rsp,
			enum http_final_call final_data,
			void *user_data)
{
	static uint8_t overflow_[8];
	static uint8_t overflow_count_;
	uint8_t overflow_inverse = 0;
	size_t bytes_to_write = rsp->body_frag_len;

	if (total_read_size == 0) {
		overflow_count_ = 0;
	}

	if (overflow_count_ != 0) {
		overflow_inverse = sizeof(overflow_) - overflow_count_;
		
		if (overflow_inverse > rsp->body_frag_len) {
			LOG_INF("Not enough data; copy all to overflow");
			memcpy(overflow_ + overflow_count_, rsp->body_frag_start, rsp->body_frag_len);
			overflow_count_ += rsp->body_frag_len;
			bytes_to_write -= rsp->body_frag_len;
		} else {
			memcpy(overflow_ + overflow_count_, rsp->body_frag_start, overflow_inverse);
			bytes_to_write -= overflow_inverse;
			int err = flash_area_write(image_area, total_read_size - overflow_count_, overflow_, sizeof(overflow_));
			if (err != 0) {
				LOG_ERR("Flash area write failed");
			} else {
				total_write_size += sizeof(overflow_);
			}
		}
	}

	if (bytes_to_write != 0) {
		// This is specific to STM32 flash
		// TODO(mskobov): don't write if < 8 bytes to write
		overflow_count_ = bytes_to_write % 8;
		int err = flash_area_write(image_area, total_read_size + overflow_inverse, rsp->body_frag_start + overflow_inverse, bytes_to_write - overflow_count_);
		if (err != 0) {
			LOG_ERR("Flash area write failed");
		} else {
			total_write_size += bytes_to_write - overflow_count_;
		}
		if (overflow_count_ != 0) {
			memset(overflow_, 0, sizeof(overflow_));
			memcpy(overflow_, rsp->body_frag_start + overflow_inverse + bytes_to_write - overflow_count_, overflow_count_);
			if (final_data == HTTP_DATA_FINAL) {
				err = flash_area_write(image_area, total_read_size + overflow_inverse + bytes_to_write - overflow_count_, overflow_, sizeof(overflow_));
				if (err != 0) {
					LOG_ERR("Flash area write failed");
				} else {
					// Technically, we could have written more, but we don't care about the alignment bytes.
					total_write_size += overflow_count_;
				}
			}
		}
	}

	// Count the read size to make sure it matches the content length header at the end.
	total_read_size += rsp->body_frag_len;
	content_length_ = rsp->content_length;
}

/* IOTEMBSYS9: Implement the HTTP OTA request. This is where the actual download happens! */
static void http_ota_request() {
	int sock;
	const int32_t timeout = 120 * MSEC_PER_SEC;

	LOG_INF("Starting OTA...");

	total_read_size = 0;
	total_write_size = 0;

	/* IOTEMBSYS9: Open the slot1 flash area as `image_area` (declared for you) */
	// Erase a flash area if previously written to.
	int err = flash_area_open(SLOT1_PARTITION_ID, (const struct flash_area **)&image_area);
	if (err != 0) {
		LOG_ERR("Flash area open failed");
		return;
	}
	/* IOTEMBSYS9: Erase the slot1 flash area */
	err = flash_area_erase(image_area, 0, image_area->fa_size);
	if (err != 0) {
		LOG_ERR("Flash area erase failed");
		return;
	}

	/* IOTEMBSYS9: Get the IP address for the host, if using DNS, and open the socket. */

	// Get the IP address of the domain
	if (get_addr_if_needed(&ota_addr_, OTA_HOST, xstr(OTA_HTTP_PORT)) != 0) {
		LOG_ERR("DNS lookup failed");
		return;
	}

	// Create a socket using parameters that the modem allows.
	sock = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
	if (sock < 0) {
		LOG_ERR("Creating socket failed");
		return;
	}
	if (connect(sock, ota_addr_->ai_addr, ota_addr_->ai_addrlen) < 0) {
		LOG_ERR("Connecting to socket failed");
		return;
	}

	/* 
	 * IOTEMBSYS9: Create and run the HTTP request; ensure that response is set to `http_ota_response_cb`
	 */
	struct http_request req;

	memset(&req, 0, sizeof(req));
	memset(recv_buf_, 0, sizeof(recv_buf_));

	req.method = HTTP_GET;
	req.url = ota_path_;
	req.host = OTA_HOST;
	req.protocol = "HTTP/1.1";
	req.payload_len = 0;
	req.payload_cb = NULL;
	req.response = http_ota_response_cb;
	req.recv_buf = recv_buf_;
	req.recv_buf_len = sizeof(recv_buf_);

	/* 
	 * IOTEMBSYS9: Send the HTTP request and ensure that the image is downloaded.
	 * If successful, confirm that content_length_ == total_read_size and 
	 * total_write_size == total_read_size
	 */
	// This request is synchronous and blocks the thread.
	int ret = http_client_req(sock, &req, timeout, "IPv4 GET");
	if (ret > 0) {
		LOG_INF("HTTP request sent %d bytes", ret);
		LOG_INF("Received: %d", total_read_size);
		if (content_length_ != total_read_size || total_write_size != total_read_size) {
			LOG_ERR("Content length mismatch. Read: %d\tWrote: %d\tExpected: %d", total_read_size, total_write_size, content_length_);
		}
		k_msleep(1000);
	} else {
		LOG_ERR("HTTP request failed: %d", ret);
		k_msleep(1000);
	}

	/* IOTEMBSYS9: Close the socket and close the image area. */
	LOG_INF("Closing the socket");
	close(sock);
	LOG_INF("Close image area");
	flash_area_close(image_area);
}

// This thread is responsible for making all HTTP requests in the app.
// This enforces simplicity, and prevents requests from stepping on one another.
void http_client_thread(void* p1, void* p2, void* p3) {
	k_event_init(&unblock_sender_);

	while (true) {
		uint32_t  events;

		LOG_INF("Waiting for button");
		events = k_event_wait(&unblock_sender_, 0xFFF, true, K_FOREVER);
		if (events == 0) {
			printk("This should not be happening!");
			continue;
		}

		// Multiple button events are possible, so handle all without exclusion.
		if (events & (1 << BUTTON_ACTION_WATER_DATA_SEND)) {
			water_tcp_request(water_index_count);
		}
		if (events & (1 << BUTTON_ACTION_OTA_DOWNLOAD)) {
			alert_tcp_request();
		}
		if (events & (1 << BUTTON_ACTION_WATER_INDEX_COUNT)) {
			count_water_index();
		}
		if (events & (1 << BUTTON_ACTION_GET_OTA_PATH)) {
			backend_ota_http_request();
		}
	}
}

K_THREAD_DEFINE(http_client_tid, 4000 /*stack size*/,
                http_client_thread, NULL, NULL, NULL,
                5 /*priority*/, 0, 0);

void main(void)
{
	int ret;
	uint32_t last_water_index;
	const struct device *modem;

	if (!gpio_is_ready_dt(&led)) {
		return;
	}

	ret = gpio_pin_configure_dt(&led, GPIO_OUTPUT_ACTIVE);
	if (ret < 0) {
		return;
	}

	// This code is commented out to aid you in development
	// if you ever need to erase a flash area. If you find yourself
	// in a situation where the previously saved settings aren't loading
	// then it's likely that the partition is corrupted and needs to be erased.

	// Erase a flash area if previously written to.
	// const struct flash_area *my_area;
	// int err = flash_area_open(STORAGE_PARTITION_ID, &my_area);
	// if (err != 0) {
	// 	printk("Flash area open failed");
	// } else {
	// 	err = flash_area_erase(my_area, 0, FLASH_AREA_SIZE(storage));
	// }

	/* IOTEMBSYS: Initialize settings subsystem. */
	settings_subsys_init();
    settings_register(&my_conf);
	settings_register(&my_conf_water);
    settings_load();

	/* IOTEMBSYS: Increment boot count. */
	boot_count++;
    settings_save_one("provisioning/boot_count", &boot_count, sizeof(boot_count));

    LOG_INF("boot_count: %d\n", boot_count);

	/* IOTEMBSYS: Configure joystick GPIOs. */
	init_joystick_gpio(&sw0, &button_cb_data_0);
	init_joystick_gpio(&sw1, &button_cb_data_1);
	init_joystick_gpio(&sw2, &button_cb_data_2);
	init_joystick_gpio(&sw3, &button_cb_data_3);
	init_joystick_gpio(&sw4, &button_cb_data_4);

	modem = DEVICE_DT_GET(DT_NODELABEL(quectel_bg96));
	if (!device_is_ready(modem)) {
		LOG_ERR("Modem is not ready");
		return;
	}
	LOG_INF("Firmware 2");
	LOG_INF("Running blinky");
	while (1) {
		ret = gpio_pin_toggle_dt(&led);
		/* IOTEMBSYS: Print GPIO state to console. */
		if (ret < 0) {
			return;
		}

		if((water_index_count-last_water_index)>LEAKAGE_THRESHOLD){
			alert_tcp_request();
		}
		else{
			water_tcp_request(water_index_count);
		}
		last_water_index = water_index_count;

		k_sleep(WATER_REPORT_CYCLE);
	}
}

