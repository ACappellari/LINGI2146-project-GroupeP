#include "contiki.h"
#include "net/rime/rime.h"
#include "random.h"
#include "lib/list.h"
#include "lib/memb.h"
#include <stdio.h>
#include <stdlib.h>
#include "adxl345.h"    // Accelerometer sensor
#include "tmp102.h"     // Temperature sensor
#include "dev/i2cmaster.h"
#include "net/rime/runicast.h"
#include "net/rime/broadcast.h"

/* CONSTANTS */
/* --------- */
#define ROUTING_NEWCHILD 50
#define MAX_RETRANSMISSIONS 16
#define MAX_DISTANCE 300 //a voir si ça fonctionne
#define MAX_CHILDREN 500
#define TOPIC_TEMP 20
#define TOPIC_ACC 22

/* STRUCTURES */
/* ---------- */
struct sensor_node;
typedef struct sensor_node{
	linkaddr_t addr;    
	uint8_t dist_root;    // Number of hops from root
} node;

struct sensor_data;
typedef struct sensor_data{
	uint16_t acc;
    uint16_t temp;
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
static uint8_t option = 1;
data dat;

/* CONNECTIONS */
/* ----------- */

// Single-hop reliable connections
static struct runicast_conn routing_conn;
static struct runicast_conn data_conn;
static struct runicast_conn transfer_conn;
static struct runicast_conn options_conn;

// Best effort local area broadcast connection
struct broadcast_conn broadcast_conn;

/* HELPER FUNCTIONS */
/* ---------------- */

/*
 * @Def : Function used to check that a given node is not already in the children list of the node.
 * @Param: a child node structure representing a node (that has to be tested)
 */
static int
check_children(child x){
	int isIn = 0; // return 0 if not in the children list
	int i;
	for(i = 0; i < children_numb; i++) {
		if(children[i].addr.u8[0] == x.addr.u8[0] && children[i].addr.u8[1] == x.addr.u8[1]){
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

static void send_data(int topic)
{
    while(runicast_is_transmitting(&data_conn)){}
    packetbuf_clear();

    if(topic==20) {
        char *top = "TOPIC_TEMP";
        char buffer[strlen(top)+16];
        snprintf(buffer, sizeof(buffer), "%s %d", top, dat.temp);
        packetbuf_copyfrom(&buffer, strlen(buffer));
    }
    else if(topic==22){
        char *top = "TOPIC_ACC";
        char buffer[strlen(top)+16];
        snprintf(buffer, sizeof(buffer), "%s %d", top, dat.acc);
        packetbuf_copyfrom(&buffer, strlen(buffer));
    }
    
    runicast_send(&data_conn, &parent.addr, MAX_RETRANSMISSIONS);
    printf("SENT OWN DATA: Own data sent to %d.%d\n", parent.addr.u8[0], parent.addr.u8[1]);
    packetbuf_clear();
}

static void transfer_data(char* payload)
{
	while(runicast_is_transmitting(&transfer_conn)){}
	packetbuf_clear();                             
	int len = strlen(payload);
	char buffer[len+16];                              
	snprintf(buffer, sizeof(buffer), "%s", payload);
	packetbuf_copyfrom(&buffer, strlen(buffer));
	runicast_send(&transfer_conn, &parent.addr, MAX_RETRANSMISSIONS);
	packetbuf_clear();
    printf("TRANSFER DATA: Data transfered to %d.%d\n", parent.addr.u8[0], parent.addr.u8[1]); 
}

static void transfer_option(char *opt)
{
	while(runicast_is_transmitting(&options_conn)){}
	int len = strlen(opt);
	char buffer[len];
	snprintf(buffer, sizeof(buffer), "%s", opt);
	packetbuf_clear();
	packetbuf_copyfrom(&buffer, strlen(buffer));
	int j;
	for(j = 0; j < children_numb; j++) {
		runicast_send(&options_conn, &children[j].addr, MAX_RETRANSMISSIONS);
	}
	packetbuf_clear();
}

static void send_routing_newchild()
{
	while(runicast_is_transmitting(&routing_conn)){}
	packetbuf_clear();
	uint16_t notif = ROUTING_NEWCHILD;
	char buffer[16];
	snprintf(buffer, sizeof(buffer), "%d", notif);
	packetbuf_copyfrom(&buffer, strlen(buffer));
	runicast_send(&routing_conn, &parent.addr, MAX_RETRANSMISSIONS);
	packetbuf_clear();

}



/* BROADCAST ROUTING MESSAGES */
/* -------------------------- */

// Upon ROUTING_HELLO reception:
static void routing_recv_broadcast(struct broadcast_conn *c, const linkaddr_t *from)
{
    printf("Received ROUTING_HELLO from %d.%d\n", from->u8[0], from->u8[1]);
	// If already connected to root
	if(parent.addr.u8[0] != 0){
		while(runicast_is_transmitting(&routing_conn)){}
		packetbuf_clear();
		char *dist_root;
		sprintf(dist_root, "%d", this.dist_root);
		packetbuf_copyfrom(dist_root, sizeof(dist_root));
		runicast_send(&routing_conn, from, MAX_RETRANSMISSIONS);  // answer ROUTING_ANS_DIST
        packetbuf_clear();
        printf("Replied to %d.%d with ROUTING_ANS_DIST = %u\n", from->u8[0], from->u8[1], (uint8_t) atoi(dist_root));
	}
    else {
        printf("Didn't reply to %d.%d because I am not attached to root yet\n", from->u8[0], from->u8[1]);
    }
}

/* UNICAST ROUTING MESSAGES */
/* ------------------------ */
static void
routing_recv_runicast(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno)
{
	char *payload = (char *) packetbuf_dataptr();    
	uint8_t pl = (uint8_t) atoi(payload);

    // Upon ROUTING_NEWCHILD reception:
    if(pl == ROUTING_NEWCHILD) {
        printf("Received ROUTING_NEWCHILD from node %d.%d\n", from->u8[0], from->u8[1]);
		child testChild;
		testChild.addr.u8[0] = from->u8[0];
		testChild.addr.u8[1] = from->u8[1];
		if(check_children(testChild) == 0){        // If children isn't already in children list
			children[children_numb] = testChild;   // Add the children
			children_numb++;
		}
        printf("Added node %d.%d to my children\n", from->u8[0], from->u8[1]);

    }
    // Upon ROUTING_ANS_DIST reception:
    else {
        uint8_t dist_root = pl;

        child testChild;
	    testChild.addr.u8[0] = from->u8[0];
	    testChild.addr.u8[1] = from->u8[1];

        printf("Received ROUTING_ANS_DIST = %d from %d.%d\n", dist_root, from->u8[0], from->u8[1]);

        // If ROUTING_ANS_DIST sender is closer to root than current parent
        if((parent.dist_root > dist_root) && (check_children(testChild) == 0)) {
	    // Replace current parent with ROUTING_ANS_DIST sender
            parent.addr.u8[0] = from->u8[0];
	        parent.addr.u8[1] = from->u8[1];		
	        parent.dist_root = dist_root;
	        this.dist_root = dist_root + 1;
            printf("Node %d.%d is closer to root than my current parent: replace it and send him ROUTING_NEWCHILD\n", from->u8[0], from->u8[1]);
            printf("My new parent is %d.%d\n", parent.addr.u8[0], parent.addr.u8[1]);
            send_routing_newchild(); // Warn him by sending ROUTING_NEWCHILD
        }
        else {
            printf("Node %d.%d is further or at same distance to root that my current parent %d.%d: keep it\n", from->u8[0], from->u8[1], parent.addr.u8[0], parent.addr.u8[1]);
        }
    }
}

static void
routing_sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{
    printf("Routing packet sent to node %d.%d, retransmission %d\n", to->u8[0], to->u8[1], retransmissions);
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
	if(testChild.addr.u8[0] == parent.addr.u8[0] && testChild.addr.u8[1] == parent.addr.u8[1]){
		parent.addr.u8[0] = 0;
        parent.addr.u8[1] = 0;
		parent.dist_root = MAX_DISTANCE;       
        printf("Routing packet timed out when sending to parent %d.%d, retransmissions %d\n ", to->u8[0], to->u8[1], retransmissions);
        printf("Deleted parent %d.%d\n", to->u8[0], to->u8[1]);
	}
	//If the node lost is a child node: 
	else if(isChild > 0){
		delete_child(isChild);
        printf("Routing packet timed out when sending to children %d.%d, retransmissions %d\n ", to->u8[0], to->u8[1], retransmissions);
        printf("Deleted children %d.%d\n", to->u8[0], to->u8[1]);
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
static void options_recv_runicast(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno)
{

    /* Copy the data from the packetbuf */
	char * opt_payload = (char *) packetbuf_dataptr();
	printf("Received option = %s from my parent %d.%d\n", opt_payload, from->u8[0], from->u8[1]);
	uint8_t opt = (uint8_t) atoi(opt_payload);
	if(opt == 0 || opt == 1 || opt == 2){
		option = opt;
        transfer_option(opt_payload);
	}
    else {printf("Received option doesn't exist\n");}
}

/*
* @Def: Function called when an option packet is sent (cfr. callbacks of the options_conn connection) 
* @Param: -*c: connection on which the option packet has been sent
*         -*to: address to which the option packet has been sent
*         - retransmissions: number of the option packet's retransmissions 
*/
static void options_sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{

  printf("Option packet sent to child %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}

/*
* @Def: Function called when an option packet transmission is timed out (cfr. callbacks of the options_conn connection) 
* @Param: -*c: connection on which the option packet has been received
*         -*to: address to which the option packet transmission failed (timed out) 
*         - transmissions: number of the option packet's retransmissions 
*/
static void options_timedout_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{
  	printf("Option packet timed out when sending to child %d.%d, retransmissions %d\n ", to->u8[0], to->u8[1], retransmissions);
	
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
static void data_recv_runicast(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno)
{
    /* Copy the data from the packetbuf */
    char * data_payload = (char *) packetbuf_dataptr();
    printf("Received data = %s from %d.%d, seqno %d\n", data_payload, from->u8[0], from->u8[1], seqno);
    transfer_data(data_payload);
}

/*
* @Def: Function called when a data packet is sent (cfr. callbacks of the data_conn connection) 
* @Param: -*c: connection on which the data packet has been sent
*         -*to: address to which the data packet has been sent
*         - retransmissions: number of the data packet's retransmissions 
*/
static void data_sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{
    printf("Data packet sent to parent %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}

/*
* @Def: Function called when a data packet transmission is timed out (cfr. callbacks of the data_conn connection)
* @Param: -*c: connection on which the data packet has been received
*         -*to: address to which the data packet transmission failed (timed out) 
*         - transmissions: number of the data packet's retransmissions 
*/
static void data_timedout_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{
    printf("Data packet timed out when sending to parent %d.%d, retransmissions %d\n ", to->u8[0], to->u8[1], retransmissions);

    /* Prevent that the parents aren't attainable anymore*/
    parent.addr.u8[0] = 0;
    parent.addr.u8[1] = 0;
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
static const struct runicast_callbacks transfer_runicast_callbacks = {data_recv_runicast, data_sent_runicast, data_timedout_runicast};
static const struct runicast_callbacks options_runicast_callbacks = {options_recv_runicast, options_sent_runicast, options_timedout_runicast};

/*---------------------------------------------------------------------------*/
PROCESS(sensor_node_process, "Sensor node implementation");
AUTOSTART_PROCESSES(&sensor_node_process);
/*---------------------------------------------------------------------------*/

PROCESS_THREAD(sensor_node_process, ev, data)
{
    // Close all connections
	PROCESS_EXITHANDLER(broadcast_close(&broadcast_conn);)
	PROCESS_EXITHANDLER(runicast_close(&routing_conn);)
	PROCESS_EXITHANDLER(runicast_close(&data_conn);)
	PROCESS_EXITHANDLER(runicast_close(&options_conn);)
    PROCESS_EXITHANDLER(runicast_close(&transfer_conn);)

    // Begin process
	PROCESS_BEGIN();

    // Initializes sensors
    tmp102_init();
    accm_init();

    // Initially not raccorded to root
    parent.addr.u8[0] = 0;
    parent.addr.u8[1] = 0;
    parent.dist_root = MAX_DISTANCE;
    this.dist_root = MAX_DISTANCE;

    // Get own address
    this.addr.u8[0] = linkaddr_node_addr.u8[0];
	this.addr.u8[1] = linkaddr_node_addr.u8[1];

    // Initially no data
    dat.temp=-555;
    dat.acc=-555;

    // Open channels
	runicast_open(&routing_conn, 144, &routing_runicast_callbacks);               // Unicast routing channel
	runicast_open(&data_conn, 154, &data_runicast_callbacks);                     // Data channel
	runicast_open(&options_conn, 164, &options_runicast_callbacks);               // Options channel
	broadcast_open(&broadcast_conn, 129, &broadcast_callbacks);                   // Broadcast routing channel
    runicast_open(&transfer_conn, 174, &transfer_runicast_callbacks);             // Broadcast routing channel

    // Timers
    static struct etimer HELLO_timer;
    static struct etimer DATA_timer;
    static struct etimer et;

    // Send ROUTING_HELLO to all node within reach, at random HELLO_timer intervals
    HELLO: while((parent.addr.u8[0] == 0) && (parent.addr.u8[1] == 0)) {
        // Delay between two HELLO send
        etimer_set(&HELLO_timer, 3000);
		PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&HELLO_timer));
        if(parent.addr.u8[0] == 0){
            broadcast_send(&broadcast_conn);
        }
        else {
            break;
        }
	    printf("Broadcast ROUTING_HELLO\n");
    }

	printf("Attached to parent %d.%d\n", parent.addr.u8[0], parent.addr.u8[1]);
    
    while(parent.addr.u8[0] != 0) {
        printf("My current parent is %d.%d\n", parent.addr.u8[0], parent.addr.u8[1]);
        etimer_set(&DATA_timer, CLOCK_SECOND * 2 + random_rand() % (CLOCK_SECOND * 8));
        etimer_set(&et, 3000);
        PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&DATA_timer));
    
        // Don't send data
        if(option==0){
            printf("Option 0: Silenced\n");
            // Do nothing
        }   
    
        // Send data periodically
        else if (option==1){
            dat.acc = (uint16_t) rand(); //accm_read_axis(0);
            printf("Acquired ACC data: %u\n", dat.acc);
            dat.temp = (uint16_t) rand(); //tmp102_read_temp_simple();
            printf("Acquired TEMP data: %u\n", dat.temp);

            if(parent.addr.u8[0]!=0) {
                send_data(TOPIC_TEMP);
            }
            else { break; }
            PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
            if(parent.addr.u8[0]!=0){
                send_data(TOPIC_ACC);
            }
            else {
                break;
            }
        }
    
        // Send data if change
        else if (option==2){
            uint16_t acc = (uint16_t) rand();   //accm_read_axis(0);
            printf("Acquired ACC data: %u\n", dat.acc);
            uint8_t temp = (uint16_t) rand();    //tmp102_read_temp_simple();
            printf("Acquired TEMP data: %u\n", dat.temp);

            if (dat.acc!=acc){
                printf("Option 2: ACC Data changed!\n");
                dat.acc=acc;
                send_data(TOPIC_ACC);
                PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
            }
            else if (dat.temp!=temp){
                printf("Option 2: TEMP Data changed!\n");
                dat.temp=temp;
                send_data(TOPIC_TEMP);
                PROCESS_WAIT_EVENT_UNTIL(etimer_expired(&et));
            }
        }
    }

    printf("Out of while\n");

    goto HELLO;
    PROCESS_END(); 
}
