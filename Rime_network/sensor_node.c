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

/* CONSTANTS */
/* --------- */
#define ROUTING_NEWCHILD = 50;

/* STRUCTURES */
/* ---------- */
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
	linkaddr_t addr;
} child;

/* GLOBAL VARIABLES */
/* ---------------- */
node parent;
node this;
child children[MAX_CHILDREN];
static uint8_t children_numb = 0;
static uint8_t option = 0;

// Initialization
static parent.addr.u8[0] = 0;
static parent.addr.u8[1] = 0;
static parent.dist_root = MAX_DISTANCE;

/* CONNECTIONS */
/* ----------- */

// Single-hop reliable connections
const struct runicast_conn routing_conn;
const struct runicast_conn data_conn;
const struct runicast_conn options_conn;

// Best effort local area broadcast connection
static struct broadcast_conn broadcast_conn;

/* HELPER FUNCTIONS */
/* ---------------- */

/* BROADCAST ROUTING MESSAGES */
/* -------------------------- */

// Upon ROUTING_HELLO reception:
static void
broadcast_recv(struct broadcast_conn *c, const linkaddr_t *from)
{

	// If already connected to root
	if(parent.addr.u8[0] != 0){
		while(runicast_is_transmitting(&runicast)){}
		packetbuf_clear();
		char *dist_root;
		sprintf(dist_root, "%d", me.dist_root);
		packetbuf_copyfrom(dist_root, sizeof(dist_root));
		runicast_send(&routing_conn, from, MAX_RETRANSMISSIONS);  // answer ROUTING_ANS_DIST
	}
}



/* UNICAST ROUTING MESSAGES */
/* ------------------------ */
static void
routing_recv_runicast(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno)
{
	char * payload = (char *) packetbuf_dataptr();    
	uint8_t pl = (uint8_t) atoi(payload);

    // Upon ROUTING_NEWCHILD reception:
    if(pl = ROUTING_NEWCHILD) {
        // If children list not full
        if(children_numb < MAX_CHILDREN){
			tuple_child testChild;
			testChild.addr.u8[0] = from->u8[0];
			testChild.addr.u8[1] = from->u8[1];
			if(check_children(testChild) == 0){        // If children isn't already in children list
				children[children_numb] = testChild;   // Add the children
				children_numb++;
			}
		}
		else{
			printf("Max number of children reached\n");
		}

    }
    // Upon ROUTING_ANS_DIST reception:
    else {
        uint8_t dist_root = pl;

        child testChild;
		testChild.addr.u8[0] = from->u8[0];
		testChild.addr.u8[1] = from->u8[1];

        // If ROUTING_ANS_DIST sender is closer to root than current parent
        if(parent.dist_root >  dist_root && check_children(testChild) == 0) {
            // Replace current parent with ROUTING_ANS_DIST sender
            parent.addr.u8[0] = from->u8[0];
		    parent.addr.u8[1] = from->u8[1];		
		    parent.dist_root = dist_root;
			me.dist_root = dist_root + 1;
			send_child_confirmation();      // Warn him by sending ROUTING_NEWCHILD
        }

    }

}

static void
routing_sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{

}

static void
routing_timedout_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{
    //Create temporary node for testing
	child testChild;
	testChild.addr.u8[0] = to->u8[0];
	testChild.addr.u8[1] = to->u8[1];
	int isChild = check_children(testChild);    // Is the lost node a children?

	// If the node lost is the parent:
	if(temp.addr.u8[0] == parent.addr.u8[0] && temp.addr.u8[1] == parent.addr.u8[1]){
		parent.addr.u8[0] = 0;
		parent.dist_root = MAX_DISTANCE;       
	}
	//If the node lost is a child node: 
	else if(isChild > 0){
		delete_child(isChild); 
	}
	else{
		printf("Error, unicast message sent to unknown node timed out\n");
	}
}



/* UNICAST OPTIONS MESSAGES */

/* UNICAST DATA MESSAGES */


/* CALLBACKS SETTINGS */
// runicast_callbacks structures contain the function to execute when:
//      - A message has been received
//      - A message has been sent
//      - A message sent has timedout

static const struct runicast_callbacks routing_callbacks = {routing_recv_runicast,
							     sent_runicast,
							     timedout_runicast};

static const struct runicast_callbacks data_runicast_callbacks = {data_recv_runicast,
							     	    dat_sent_runicast,
							     	    data_timedout_runicast};

static const struct runicast_callbacks options_runicast_callbacks = {options_recv_runicast,
							     	     options_sent_runicast,
							     	     options_timedout_runicast};



/*---------------------------------------------------------------------------*/
PROCESS(sensor_node_process, "Sensor node implementation");
AUTOSTART_PROCESSES(&sensor_node_process);
/*---------------------------------------------------------------------------*/

PROCESS_THREAD(sensor_network_process, ev, data)
{
    // Send ROUTING_HELLO to all node within reach
    while(parent.addr.u8[0] != 0) {
        broadcast_send(&broadcast_conn);
    }

    
}

PROCESS_END(); 