#include "contiki.h"
#include "net/rime.h"
#include "random.h"
#include "lib/list.h"
#include "lib/memb.h"
#include <stdio.h>
#include <stdlib.h>
#include "dev/sht11.h"
#include "dev/i2cmaster.h"
#include "dev/light-ziglet.h"

/* STRUCTURES */
struct sensor_node;
typedef struct sensor_node{
	linkaddr_t addr;    
	uint8_t dist_root;    // Number of hops from root
} node;

struct sensor_data;
typedef struct sensor_data{
	uint16_t val;
	char* type;         // Temperature or accelerometer data
} data;

struct child_node;
typedef struct child_node{
	rimeaddr_t addr;
} child;

/* GLOBAL VARIABLES */
node parent;
node this;
child children[MAX_CHILDREN];
static uint8_t children_numb = 0;
static uint8_t option = 0;

/* CONNECTIONS */
/* ----------- */

// Single-hop reliable connections
static struct runicast_conn routing_conn;
static struct runicast_conn sensor_conn;
static struct runicast_conn options_conn;

/* HELPER FUNCTIONS */

/* UNICAST ROUTING MESSAGES */

/* UNICAST OPTIONS MESSAGES */

/* UNICAST DATA MESSAGES */

/* BROADCAST ROUTING MESSAGES */

/* CALLBACKS SETTINGS */



/*---------------------------------------------------------------------------*/
PROCESS(sensor_node_process, "Sensor node implementation");
AUTOSTART_PROCESSES(&sensor_node_process);
/*---------------------------------------------------------------------------*/

PROCESS_THREAD(sensor_network_process, ev, data)
{
    
}

PROCESS_END(); 