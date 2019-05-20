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
#define MAX_RETRANSMISSIONS 16
#define MAX_DISTANCE infinity() //a voir si ça fonctionne

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
static struct runicast_conn data_conn;
static struct runicast_conn options_conn;

/* HELPER FUNCTIONS */

/*
* @Def: this function is used to transmit the data that come from an other node (down) to an upper one (towards the root)
* @Param: - char * data_payload: the data to be relayed (string representation)
*/
static void
relay_data(char * data_payload)
{
    printf("MSG TO RELAY : %s\n", data_payload);
    /* Waits the connection */
    while(runicast_is_transmitting(&data_conn)) {}
    /* Create the data packet */
    int length = strlen(data_payload);
    char * buf = [length+16];
    snprintf(buf, sizeof(buf), "%s", data_payload);
    printf("DATA CONTAINED : %s\n", buf);
    packetbuf_copyfrom(&buf, strlen(buf));
    /* Send the packet */
    runicast_send(&data_conn, &parent.addr, MAX_RETRANSMISSIONS);
    /* Free the packetbuf content */
    packetbuf_clear();
    printf("DATA SENDT");

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

/* UNICAST ROUTING MESSAGES */

/* UNICAST OPTIONS MESSAGES */

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
		relay_options(opt_payload);
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
    relay_data(data_payload);
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