#include "contiki.h"
#include "net/rime.h"
#include "random.h"
#include "lib/list.h"
#include "lib/memb.h"
#include <stdio.h>
#include <stdlib.h>
#include "adxl345.h"    // Accelerometer sensor
#include "tmp102.h"     // Temperature sensor
#include "dev/i2cmaster.h"


/* CONSTANTS */
/* --------- */
#define ROUTING_NEWCHILD = 50;
#define MAX_RETRANSMISSIONS 16
#define MAX_DISTANCE infinity() //a voir si ça fonctionne

/* STRUCTURES */
/* ---------- */
struct sensor_node;
typedef struct sensor_node{
	linkaddr_t addr;    
	uint8_t dist_root;    // Number of hops from root
} node;

struct sensor_data;
typedef struct sensor_data{
	char val[32];
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
data data;

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

// @Def: Generic function to send a certain payload to a certain address through a given connection
static void send_packet(runicast_conn *c, char *payload, int length, *linkaddr_t to){

    while(runicast_is_transmitting(c)) {}
    int length=strlen(payload);
    char *buffer = [length];
    snprintf(buffer, sizeof(buffer), "%s", payload);
    packetbuf_copyfrom(&buffer, strlen(buffer));
    runicast_send(c, to, MAX_RETRANSMISSIONS);
    packetbuf_clear();

}

/*
 * @Def : Function used to check that a given node is not already in the children list of the node.
 * @Param: a child node structure representing a node (that has to be tested)
 */
static int
check_children(child x){
	int isIn = 0; // return 0 if not in the children list
	int i;
	for(i = 0; i < children_numb; i++) {
		if(children[i].addr.u8[0] == t.addr.u8[0] && children[i].addr.u8[1] == x.addr.u8[1]){
			return i; // return >0 if it is in the children list
		}
	}
	return isIn;
}

/*
 * @Def: Function used to delete a child node from children list
 * @Param: the index of the child to delete in the children list
 */
void delete_child(int index)
{
	int i;
	for(i = index; i < children_numb - 1; i++){
		children[i] = children[i + 1];
	}
	children_numb--;
  
}


/* BROADCAST ROUTING MESSAGES */
/* -------------------------- */

// Upon ROUTING_HELLO reception:
static void
routing_recv_broadcast(struct broadcast_conn *c, const linkaddr_t *from)
{

	// If already connected to root
	if(parent.addr.u8[0] != 0){
		while(runicast_is_transmitting(&runicast)){}
		packetbuf_clear();
		char *dist_root;
		sprintf(dist_root, "%d", this.dist_root);
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
            send_packet(&routing_conn, ROUTING_NEWCHILD, 16, &parent.addr); // Warn him by sending ROUTING_NEWCHILD
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
/* ------------------------ */

/*
* @Def: Function called when an option packet is received (cfr. callbacks of the options_conn connection) 
* @Param: -*c: connection on which the option packet has been received
*         -*from: address from which the option packet has been received
*         - seqno: the sequence number of the option packet received
*/
options_recv_runicast(struct runicast_conn *c, const rimeaddr_t *from, uint8_t seqno)
{
    printf("Msg received on options_conn from %d.%d, seqno %d\n", from->u8[0], from->u8[1], seqno);

    /* Copy the data from the packetbuf */
	char * opt_payload = (char *) packetbuf_dataptr();
	printf("OPTION PACKET RECEIVED : %s\n", opt_payload);
	uint8_t opt = (uint8_t) atoi(opt_payload);
	if(opt == 0 || opt == 1 || opt == 2){
		option = opt;
        int j;
	    for(j = 0; j < number_children; j++) {
            send_packet(&options_conn, opt_payload, strlen(payload), &children[j].addr)
	    }
	}
    else {printf("Asked option doesn't exist")}
}

/*
* @Def: Function called when an option packet is sent (cfr. callbacks of the options_conn connection) 
* @Param: -*c: connection on which the option packet has been sent
*         -*to: address to which the option packet has been sent
*         - retransmissions: number of the option packet's retransmissions 
*/
options_sent_runicast(struct runicast_conn *c, const rimeaddr_t *to, uint8_t retransmissions)
{
  printf("Option packet sent to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}

/*
* @Def: Function called when an option packet transmission is timed out (cfr. callbacks of the options_conn connection) 
* @Param: -*c: connection on which the option packet has been received
*         -*to: address to which the option packet transmission failed (timed out) 
*         - transmissions: number of the option packet's retransmissions 
*/
options_timedout_runicast(struct runicast_conn *c, const rimeaddr_t *to, uint8_t retransmissions)
{
  	printf("Option packet timed out when sending to %d.%d, retransmissions %d\n ", to->u8[0], to->u8[1], retransmissions);
	
	//Create temporary node for testing
	child test;
	test.addr.u8[0] = to->u8[0];
	test.addr.u8[1] = to->u8[1];
	int isChild = check_children(test);
	//If the node lost is a child node
	if(isChild > 0){
		delete_child(isChild);
	}
	else{
		printf("ERROR: option message sent to unknown node -> timed out\n"); // because options are only sendt to children (down)
	}
}


/* UNICAST DATA MESSAGES */
/* --------------------- */

/*
* @Def: Function called when a data packet is received (cfr. callbacks of the data_conn connection) 
* @Param: -*c: connection on which the data packet has been received
*         -*from: address from which the data packet has been received
*         - seqno: the sequence number of the data packet received
*/
static void data_rcv_runicast(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno)
{
    printf("Msg received on data_conn from %d.%d, seqno %d\n", from->u8[0], from->u8[1], seqno); //changé l'intitulé du message 'msg rcvd on data_conn' au lieu de 'runicast msg'
    
    /* Copy the data from the packetbuf */
    char * data_payload = (char *) packetbuf_dataptr();
    printf("DATA PACKET RECEIVED : %s\n", data_payload); //idem changé l'intitulé du msg
    send_packet(&data_conn, data_payload, strlen(payload)+16, &parent.addr)

}

/*
* @Def: Function called when a data packet is sent (cfr. callbacks of the data_conn connection) 
* @Param: -*c: connection on which the data packet has been sent
*         -*to: address to which the data packet has been sent
*         - retransmissions: number of the data packet's retransmissions 
*/
static void data_sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{
    printf("Data packet sent to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}

/*
* @Def: Function called when a data packet transmission is timed out (cfr. callbacks of the data_conn connection)
* @Param: -*c: connection on which the data packet has been received
*         -*to: address to which the data packet transmission failed (timed out) 
*         - transmissions: number of the data packet's retransmissions 
*/
static void data_timedout_runicast(struct runicsast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{
    printf("Data packet timed out when sending to %d.%d, retransmissions %d\n ", to->u8[0], to->U8[1], retransmissions);

    /* Prevent that the parents aren't attainable anymore*/
    parent.addr.u8[0] = 0;
    parent.dist_root= MAX_DISTANCE; 
}

/* CALLBACKS SETTINGS */
/* ------------------ */
// runicast_callbacks structures contain the function to execute when:
//      - A message has been received
//      - A message has been sent
//      - A message sent has timedout

static const struct broadcast_callbacks broadcast_callbacks = {routing_recv_broadcast};
static const struct runicast_callbacks routing_runicast_callbacks = {routing_recv_runicast, routing_sent_runicast, routing_timedout_runicast};
static const struct runicast_callbacks data_runicast_callbacks = {data_recv_runicast, data_sent_runicast, data_timedout_runicast};
static const struct runicast_callbacks options_runicast_callbacks = {options_recv_runicast, options_sent_runicast, options_timedout_runicast};

/*---------------------------------------------------------------------------*/
PROCESS(sensor_node_process, "Sensor node implementation");
AUTOSTART_PROCESSES(&sensor_node_process);
/*---------------------------------------------------------------------------*/

PROCESS_THREAD(sensor_network_process, ev, data)
{
    // Close all connections
	PROCESS_EXITHANDLER(broadcast_close(&broadcast_conn);)
	PROCESS_EXITHANDLER(runicast_close(&routing_conn);)
	PROCESS_EXITHANDLER(runicast_close(&data_conn);)
	PROCESS_EXITHANDLER(runicast_close(&options_conn);)

    // Begin process
	PROCESS_BEGIN();

    // Initializes sensors
    tmp102_init();
    accm_init();

    // Initially not raccorded to root
    static parent.addr.u8[0] = 0;
    static parent.addr.u8[1] = 0;
    static parent.dist_root = MAX_DISTANCE;

    // Get own address
    this.addr.u8[0] = linkaddr_node_addr.u8[0];
	this.addr.u8[1] = linkaddr_node_addr.u8[1];

    // Open channels
	runicast_open(&routing_conn, 144, &routing_runicast_callbacks);                 // Unicast routing channel
	runicast_open(&data_conn, 154, &data_runicast_callbacks);                       // Data channel
	runicast_open(&options_conn, 164, &options_runicast_callbacks);                 // Options channel
	broadcast_open(&broadcast_conn, 129, &broadcast_callbacks);                     // Broadcast routing channel

    // Timers
    static struct etimer HELLO_timer;
    static struct etimer DATA_timer;

    // Send ROUTING_HELLO to all node within reach, at random HELLO_timer intervals
    HELLO: while(parent.addr.u8[0] != 0) {
        // Delay between two HELLO send
        etimer_set(&HELLO_timer, CLOCK_SECOND * 8 + random_rand() % (CLOCK_SECOND * 16));
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&HELLO_timer));

        broadcast_send(&broadcast_conn);
    }
    
    // Don't send data
    if(option==0){
        // Do nothing
    }
    // Send data periodically
    else if (option==1){
        int16_t acc = accm_read_axis(0);
        int8_t temp = tmp102_read_temp_simple();
        val = ((uint32_t) acc << 16 || (temp & (uint32_t) 0xFF));
        sprintf(data.val, "%u", val);
        send_packet(&data_conn, data.val, length(data)+16, &parent.addr);
    }
    // Send data if change
    else if (option==2){
        int16_t acc = accm_read_axis(0);
        int8_t temp = tmp102_read_temp_simple();
        uint32_t testVal = ((uint32_t) acc << 16 || (temp & (uint32_t) 0xFF));
        
        if (data.val!=testVal){
            sprintf(data.val, "%u", testVal);
            send_packet(&data_conn, data.val, length(data)+16, &parent.addr);
        }
    }

    goto HELLO;
    
}

PROCESS_END(); 