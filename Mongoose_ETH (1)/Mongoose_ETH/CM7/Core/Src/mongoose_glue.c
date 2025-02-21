#include "mongoose_glue.h"
#include "string.h"
#include "main.h"
#include <ctype.h>
#include <string.h>

#define GATEWAY1_MAX_DEVICES 10
#define GATEWAY1_DATA_SIZE 5  // slave_id, function_code, address, size, tagname
#define FLASH_SECTOR         FLASH_SECTOR_7
#define FLASH_ADDR           0x081E0000
#define FLASH_MAGIC          0x47415445  // 'GATE'

typedef struct {
    char slave_id[10];
    char function_code[10];
    char address[10];
    char size[10];
    char tagname[20];
} Gateway1Device;

typedef struct {
    uint32_t magic;
    int device_count;
    Gateway1Device devices[GATEWAY1_MAX_DEVICES];
} Gateway1Storage;

static Gateway1Storage g_gateway1_data = {0};
static struct mg_timer *g_gateway1_timer = NULL;

// Function prototypes
static void gateway1_parse_data(const char *data);
static void gateway1_save_to_flash(void);
static void gateway1_load_from_flash(void);

static uint32_t fast_rand(void) {
  static uint32_t seed = 12345;
  seed = (seed * 1103515245 + 12345) & 0x7FFFFFFF;
  return seed;
}

#define TOPIC_COUNT 1000
typedef struct {
  char name[40];  // Topic name buffer
  int value;      // Current value
} Topic;

static Topic s_topics[TOPIC_COUNT];  // Array of topics

static void timer_fn_all_topics(void *arg) {
  if (g_mqtt_conn == NULL) return;
  static char msg[16];  // Reuse buffer
  struct mg_mqtt_opts opts = {0};  // Initialize once

  for (int i = 0; i < TOPIC_COUNT; i++) {
	  s_topics[i].value = fast_rand() % 1000;
	  snprintf(msg, sizeof(msg), "%d", s_topics[i].value);
    // Publish
    opts.topic = mg_str(s_topics[i].name);
    opts.message = mg_str_n(msg,strlen(msg));
    opts.qos = 0;  // Add this to mg_mqtt_opts initialization
    mg_mqtt_pub(g_mqtt_conn, &opts);
  }
}


static void init_topics(void) {
  for (int i = 0; i < TOPIC_COUNT; i++) {
    mg_snprintf(s_topics[i].name, sizeof(s_topics[i].name),
               "sarayu/d1/topic%d|m/s", i + 1);
  }
}


void glue_init_1(void) {
    init_topics();
    mg_timer_add(&g_mgr, 1000, MG_TIMER_REPEAT, timer_fn_all_topics, NULL);
    gateway1_load_from_flash();  // Load persisted data
    MG_DEBUG(("Custom init done"));
}


void glue_lock_init(void) {  // callback to initialize the MQTT semaphore
}

void glue_lock(void) {  // Lock mutex. Implement only if you use MQTT publish
}

void glue_unlock(void) {  // Unlock mutex. Implement only if you use MQTT publish
}

void glue_mqtt_tls_init(struct mg_connection *c) {
  bool is_tls = mg_url_is_ssl(WIZARD_MQTT_URL);
  MG_DEBUG(("%lu TLS enabled: %s", c->id, is_tls ? "yes" : "no"));
  if (is_tls) {
    struct mg_tls_opts opts;
    memset(&opts, 0, sizeof(opts));
    mg_tls_init(c, &opts);
  }
}

// Called when we connected to the MQTT server
void glue_mqtt_on_connect(struct mg_connection *c, int code) {
	struct mg_mqtt_opts opts = {
	        .clean = true,
	        .user = mg_str("Sarayu"),
	        .pass = mg_str("IOTteam@123"),
	        .qos = 1,
	        .topic = mg_str("Gateway1")
	    };
	    mg_mqtt_sub(c, &opts);
	    gateway1_load_from_flash();
}

void glue_mqtt_on_message(struct mg_connection *c, struct mg_str topic,
                          struct mg_str data) {
	char tmp[100];
	    mg_snprintf(tmp, sizeof(tmp), "Got [%.*s] -> [%.*s]",
	                (int)topic.len, topic.buf, (int)data.len, data.buf);
	    MG_DEBUG(("%lu %s", c->id, tmp));

	    // Check if topic is "Gateway1"
	    if (mg_strcmp(topic, mg_str("Gateway1")) == 0) {
	        char buf[256];
	        snprintf(buf, sizeof(buf), "%.*s", (int)data.len, data.buf);
	        gateway1_parse_data(buf);

	        // Send acknowledgment to Gateway1_res only once
	        struct mg_mqtt_opts ack_opts = {
	            .topic = mg_str("Gateway1_res"),
	            .message = mg_str("Received Successfully"),
	            .qos = 0
	        };
	        mg_mqtt_pub(c, &ack_opts);

	        // Save to flash (no timer needed anymore)
	        gateway1_save_to_flash();
	    }
}

// Add new Gateway1 functions
static void gateway1_parse_data(const char *input) {
	char clean[256];
	    int offset = 0;
	    g_gateway1_data.device_count = 0;

	    // Clean input
	    for (int i = 0; input[i] && offset < sizeof(clean)-1; i++) {
	        if (isalnum(input[i]) || input[i] == ',' || input[i] == '"' || input[i] == ' ') {
	            clean[offset++] = input[i];
	        }
	    }
	    clean[offset] = '\0';

	    // Parse CSV data
	    char *token = strtok(clean, ",");
	    int field = 0;
	    Gateway1Device dev = {0};

	    while (token && g_gateway1_data.device_count < GATEWAY1_MAX_DEVICES) {
	        // Remove quotes
	        char *p = token;
	        while (*p == '"' || *p == ' ') p++;
	        char *end = p + strlen(p) - 1;
	        while (end > p && (*end == '"' || *end == ' ')) end--;
	        *(end+1) = '\0';

	        switch (field % GATEWAY1_DATA_SIZE) {
	            case 0: strncpy(dev.slave_id, p, sizeof(dev.slave_id)-1); break;
	            case 1: strncpy(dev.function_code, p, sizeof(dev.function_code)-1); break;
	            case 2: strncpy(dev.address, p, sizeof(dev.address)-1); break;
	            case 3: strncpy(dev.size, p, sizeof(dev.size)-1); break;
	            case 4:
	                strncpy(dev.tagname, p, sizeof(dev.tagname)-1);
	                g_gateway1_data.devices[g_gateway1_data.device_count++] = dev;
	                memset(&dev, 0, sizeof(dev));
	                break;
	        }
	        field++;
	        token = strtok(NULL, ",");
	    }
}

static void gateway1_save_to_flash(void) {
	HAL_FLASH_Unlock();

	    // Erase sector
	    FLASH_EraseInitTypeDef erase = {
	        .TypeErase = FLASH_TYPEERASE_SECTORS,
	        .Banks = FLASH_BANK_2,
	        .Sector = FLASH_SECTOR,
	        .NbSectors = 1,
	        .VoltageRange = FLASH_VOLTAGE_RANGE_3
	    };
	    uint32_t sectorError;
	    HAL_FLASHEx_Erase(&erase, &sectorError);

	    // Prepare data
	    g_gateway1_data.magic = FLASH_MAGIC;
	    uint64_t *src = (uint64_t *)&g_gateway1_data;

	    // Program flash
	    for (size_t i = 0; i < sizeof(g_gateway1_data); i += 32) {
	        HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD,
	                         FLASH_ADDR + i,
	                         (uint64_t)(uintptr_t)(src + i/8));
	    }

	    HAL_FLASH_Lock();
}

static void gateway1_load_from_flash(void) {
	Gateway1Storage *flash = (Gateway1Storage *)FLASH_ADDR;
	    if (flash->magic == FLASH_MAGIC && flash->device_count <= GATEWAY1_MAX_DEVICES) {
	        memcpy(&g_gateway1_data, flash, sizeof(Gateway1Storage));
	        MG_DEBUG(("Loaded %d devices from flash", g_gateway1_data.device_count));
	    }
}


static void gateway1_publish_res(struct mg_connection *c) {
	char buf[512];
	    int offset = 0;

	    offset += snprintf(buf + offset, sizeof(buf) - offset, "[");
	    for (int i = 0; i < g_gateway1_data.device_count; i++) {
	        Gateway1Device *d = &g_gateway1_data.devices[i];
	        offset += snprintf(buf + offset, sizeof(buf) - offset,
	                          "%s\"%s\",\"%s\",\"%s\",\"%s\",\"%s\"",
	                          i > 0 ? "," : "",
	                          d->slave_id, d->function_code, d->address,
	                          d->size, d->tagname);
	    }
	    offset += snprintf(buf + offset, sizeof(buf) - offset, "]");

	    struct mg_mqtt_opts opts = {
	        .topic = mg_str("Gateway1_res"),
	        .message = mg_str(buf),
	        .qos = 0
	    };
	    mg_mqtt_pub(c, &opts);
}

void glue_mqtt_on_cmd(struct mg_connection *c, struct mg_mqtt_message *mm) {
  MG_DEBUG(("%lu cmd %d qos %d", c->id, mm->cmd, mm->qos));
}

struct mg_connection *glue_mqtt_connect(struct mg_mgr *mgr, mg_event_handler_t fn) {
    const char *url = WIZARD_MQTT_URL; // Uses the defined MQTT URL
    struct mg_mqtt_opts opts;
    memset(&opts, 0, sizeof(opts));

    opts.clean = true;
    opts.user = mg_str("Sarayu");       // Set username
    opts.pass = mg_str("IOTteam@123");  // Set password


    return mg_mqtt_connect(mgr, url, &opts, fn, NULL);
}



void glue_sntp_on_time(uint64_t utc_time_in_milliseconds) {
  MG_INFO(("UTC time in milliseconds from SNTP: %llu, current time: %llu",
           utc_time_in_milliseconds, mg_now()));
}


// Mock a device that has 5 read/write registers at address 1000
static uint16_t s_modbus_regs[] = {11, 22, 33, 44, 55};
static uint16_t s_modbus_base = 1000;  // Base address of our registers

bool glue_modbus_read_reg(uint16_t address, uint16_t *value) {
  bool success = false;
  size_t count = sizeof(s_modbus_regs) / sizeof(s_modbus_regs[0]);
  if (address >= s_modbus_base && address < s_modbus_base + count) {
    *value = s_modbus_regs[address - s_modbus_base];
    success = true;
  }
  MG_INFO(("%s: %hu = %hu", success ? "OK" : "FAIL", address, *value));
  return success;
}

bool glue_modbus_write_reg(uint16_t address, uint16_t value) {
  bool success = false;
  size_t count = sizeof(s_modbus_regs) / sizeof(s_modbus_regs[0]);
  if (address >= s_modbus_base && address < s_modbus_base + count) {
    s_modbus_regs[address - s_modbus_base] = value;
    success = true;
  }
  MG_INFO(("%s: %hu = %hu", success ? "OK" : "FAIL", address, value));
  return success;

}


// Authenticate user/password. Return access level for the authenticated user:
//   0 - authentication error
//   1,2,3... - authentication success. Higher levels are more privileged than lower
int glue_authenticate(const char *user, const char *pass) {
  int level = 0; // Authentication failure
  if (strcmp(user, "Sarayu") == 0 && strcmp(pass, "IOTteam@123") == 0) {
    level = 7;  // Administrator
  } else if (strcmp(user, "user") == 0 && strcmp(pass, "user") == 0) {
    level = 3;  // Ordinary dude
  }
  return level;
}


// reboot
static uint64_t s_action_timeout_reboot;  // Time when reboot ends
bool glue_check_reboot(void) {
  return s_action_timeout_reboot > mg_now(); // Return true if reboot is in progress
}
void glue_start_reboot(void) {
  s_action_timeout_reboot = mg_now() + 1000; // Start reboot, finish after 1 second
}

// firmware_update
void  *glue_ota_begin_firmware_update(char *file_name, size_t total_size) {
  bool ok = mg_ota_begin(total_size);
  MG_DEBUG(("%s size %lu, ok: %d", file_name, total_size, ok));
  return ok ? (void *) 1 : NULL;
}
bool  glue_ota_end_firmware_update(void *context) {
  bool ok = mg_ota_end();
  MG_DEBUG(("ctx: %p, success: %d", context, ok));
  if (ok) {
    MG_INFO(("Rebooting in %lu ms", WIZARD_REBOOT_TIMEOUT_MS));
    mg_timer_add(&g_mgr, WIZARD_REBOOT_TIMEOUT_MS, 0,
      (void(*)(void *)) mg_device_reset, NULL);
  }
  return ok;
}
bool  glue_ota_write_firmware_update(void *context, void *buf, size_t len) {
  MG_DEBUG(("ctx: %p %p/%lu", context, buf, len));
  return mg_ota_write(buf, len);
}

// file_upload
void  *glue_file_open_file_upload(char *file_name, size_t total_size) {
  char path[128], *p = NULL;
  FILE *fp = NULL;
  if ((p = strrchr(file_name, '/')) == NULL) p = file_name;
  mg_snprintf(path, sizeof(path), "/tmp/%s", p);
#if MG_ENABLE_POSIX_FS
  fp = fopen(path, "w+b");
#endif
  MG_DEBUG(("opening [%s] size %lu, fp %p", path, total_size, fp));
  return fp;
}
bool  glue_file_close_file_upload(void *fp) {
  MG_DEBUG(("closing %p", fp));
#if MG_ENABLE_POSIX_FS
  return fclose((FILE *) fp) == 0;
#else
  return false;
#endif
}
bool  glue_file_write_file_upload(void *fp, void *buf, size_t len) {
  MG_DEBUG(("writing fp %p %p %lu bytes", fp, buf, len));
#if MG_ENABLE_POSIX_FS
  return fwrite(buf, 1, len, (FILE *) fp) == len;
#else
  return false;
#endif
}

// graph1
size_t glue_graph_get_graph1(uint32_t from, uint32_t to,
                              uint32_t *x_values, double *y_values, size_t len) {
  size_t i = 0;
  uint32_t timestamps[] = {1724576787,1724576847,1724576907,1724576967,1724577027,1724577087,1724577147,1724577207,1724577267,1724577327};  // Those are example values
  double values[] = {20.3,27.2,29.7,27.9,25.1,23.8,22.5,22.2,23.3,23.9};  // Use real device data
  for (i = 0; i < len; i++) {
    if (i >= sizeof(values) / sizeof(values[0])) break;
    x_values[i] = timestamps[i];
    y_values[i] = values[i];
  }
  (void) from, (void) to;
  return i;
}

static struct state s_state = {42, 27, 70, 10, "1.0.0", true, false, 83};
void glue_get_state(struct state *data) {
  *data = s_state;  // Sync with your device
}
void glue_set_state(struct state *data) {
  s_state = *data; // Sync with your device
}

static struct leds s_leds = {false, true, false};
void glue_get_leds(struct leds *data) {
  *data = s_leds;  // Sync with your device
}
void glue_set_leds(struct leds *data) {
  s_leds = *data; // Sync with your device
}

static struct settings s_settings = {"edit & save me", 2, 123.12345, 17, true};
void glue_get_settings(struct settings *data) {
  *data = s_settings;  // Sync with your device
}
void glue_set_settings(struct settings *data) {
  s_settings = *data; // Sync with your device
}

static struct security s_security = {"admin", "user"};
void glue_get_security(struct security *data) {
  *data = s_security;  // Sync with your device
}
void glue_set_security(struct security *data) {
  s_security = *data; // Sync with your device
}
