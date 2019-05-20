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

/* HELPER FUNCTIONS */

/* UNICAST ROUTING MESSAGES */

/* UNICAST OPTIONS MESSAGES */

/* UNICAST  DATA MESSAGES */

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