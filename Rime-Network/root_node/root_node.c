#include <stdio.h>
#include <stdlib.h>
#include "contiki.h"
#include "contiki-net.h"
#include "net/rime/rime.h"
#include "random.h"
#include "lib/list.h"
#include "lib/memb.h"
#include "dev/uart0.h"
#include "dev/button-sensor.h"
#include "dev/leds.h"
#include "dev/serial-line.h"


/* CONSTANTS */
#define ROUTING_NEWCHILD  50
#define MAX_RETRANSMISSIONS 16
#define SERIAL_BUF_SIZE 128 //Size of the communication buffer with the gateway
#define MAX_CHILDREN 15

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
node this;
child children[MAX_CHILDREN];
static uint8_t children_numb = 0;



/* CONNECTIONS */
/* ----------- */

// Single-hop reliable connections
static struct runicast_conn routing_conn;
static struct runicast_conn data_conn;
static struct runicast_conn options_conn;

// Best effort local area broadcast connection
static struct broadcast_conn broadcast_conn;

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

/* BROADCAST ROUTING MESSAGES */
/* -------------------------- */

// Upon ROUTING_HELLO reception:
static void
routing_recv_broadcast(struct broadcast_conn *c, const linkaddr_t *from)
{
	printf("I A M ROOT, broadcast message received from %d.%d: '%s'\n",
	from->u8[0], from->u8[1], (char *)packetbuf_dataptr());

	if(children_numb < MAX_CHILDREN)
		{
			while(runicast_is_transmitting(&routing_conn)){}
			packetbuf_clear();
			packetbuf_copyfrom("0", 1);
			runicast_send(&routing_conn, from, MAX_RETRANSMISSIONS);  // answer ROUTING_ANS_DIST
		}
		else 
		{
			while(runicast_is_transmitting(&routing_conn)){}
			packetbuf_clear();
			char *dist_root ;
			sprintf(dist_root, "%d", 500); //unreachable as it doesn't have place for an other child in its children list
			printf("ROOT doesn't have place anymore\n");
			packetbuf_copyfrom(dist_root, sizeof(dist_root));
			runicast_send(&routing_conn, from, MAX_RETRANSMISSIONS);
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
    if(pl == ROUTING_NEWCHILD) {
        printf("Received ROUTING_NEWCHILD from node %d.%d\n", from->u8[0], from->u8[1]);
        // If children list not full
        if(children_numb < MAX_CHILDREN){
			child testChild;
			testChild.addr.u8[0] = from->u8[0];
			testChild.addr.u8[1] = from->u8[1];
			if(check_children(testChild) == 0){        // If children isn't already in children list
				children[children_numb] = testChild;   // Add the children
				children_numb++;
			}
            printf("Added node %d.%d to my children\n", from->u8[0], from->u8[1]);
		}
		else{
			printf("Max number of children reached\n");
		}

    }
    // Upon ROUTING_ANS_DIST reception:
    else {
        printf("Error: unknown unicast message type on routing connection \n");
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


	//If the node lost is a child node: 
	if(isChild > 0){
        printf("Routing packet timed out when sending to children %d.%d, retransmissions %d\n ", to->u8[0], to->u8[1], retransmissions);
        printf("Deleted children %d.%d\n", to->u8[0], to->u8[1]);
		delete_child(isChild); 
	}
	else{
		printf("Error, unicast message sent to unknown node timed out\n");
	}
}


/* UNICAST OPTIONS MESSAGES */
/* ------------------------ */

/*
* @Def: Function called when an option packet is sent (cfr. callbacks of the options_conn connection) 
* @Param: -*c: connection on which the option packet has been sent
*         -*to: address to which the option packet has been sent
*         - retransmissions: number of the option packet's retransmissions 
*/
static void options_sent_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
{
  printf("Option packet sent to %d.%d, retransmissions %d\n", to->u8[0], to->u8[1], retransmissions);
}

/*
* @Def: Function called when an option packet transmission is timed out (cfr. callbacks of the options_conn connection) 
* @Param: -*c: connection on which the option packet has been received
*         -*to: address to which the option packet transmission failed (timed out) 
*         - transmissions: number of the option packet's retransmissions 
*/
static void options_timedout_runicast(struct runicast_conn *c, const linkaddr_t *to, uint8_t retransmissions)
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
static void data_recv_runicast(struct runicast_conn *c, const linkaddr_t *from, uint8_t seqno)
{
  char * data_payload = (char *) packetbuf_dataptr();
  printf("Root received data %s from %d.%d, seqno %d\n", data_payload, from->u8[0], from->u8[1], seqno);

}

/* SERIAL COMMUNICATION */
/* -------------------- */
static char serial_buf[SERIAL_BUF_SIZE]; //keep the options given in the command line interface of the gateway
static int serial_buf_index;

/*
* @Def: Callback function for the serial communication (given as argument to uart0_set_input)
* @Param: an unsigned char c (input of the terminal) - if c corresponds to an option, this last will be stored in serial_buf
*/
static void uart_serial_callback(unsigned char * c) { 
  if(c!= '\n'){
	serial_buf[serial_buf_index] = c;   	
  }  
  if(c == '\n' || c == EOF || c == '\0'){ 
   //printf("%s\n", (char *)serial_buf);
   packetbuf_clear();
   serial_buf[strcspn ( serial_buf, "\n" )] = '\0';
   packetbuf_copyfrom(serial_buf, strlen(serial_buf));

   printf("Received option %s\n", serial_buf);

   //Send the option to all the child nodes
   int j;
   for(j = 0; j < children_numb; j++) {
		runicast_send(&options_conn, &children[j].addr, MAX_RETRANSMISSIONS);
   }

   memset(serial_buf, 0, serial_buf_index); 
   serial_buf_index = 0; 
  }else{ 
    serial_buf_index = serial_buf_index + 1; 
  } 
}

/* CALLBACKS SETTINGS */
/* ------------------ */
// runicast_callbacks structures contain the function to execute when:
//      - A message has been received
//      - A message has been sent
//      - A message sent has timedout

static const struct broadcast_callbacks broadcast_callbacks = {routing_recv_broadcast};
static const struct runicast_callbacks routing_runicast_callbacks = {routing_recv_runicast, routing_sent_runicast, routing_timedout_runicast};
static const struct runicast_callbacks data_runicast_callbacks = {data_recv_runicast};
static const struct runicast_callbacks options_runicast_callbacks = {options_sent_runicast, options_timedout_runicast};

/*---------------------------------------------------------------------------*/
PROCESS(root_node_process, "Root node implementation");
AUTOSTART_PROCESSES(&root_node_process);
/*---------------------------------------------------------------------------*/

/* PROCESS THREAD */

PROCESS_THREAD(root_node_process, ev, data)
{
	// Close all connections
	PROCESS_EXITHANDLER(broadcast_close(&broadcast_conn);)
	PROCESS_EXITHANDLER(runicast_close(&routing_conn);)
	PROCESS_EXITHANDLER(runicast_close(&data_conn);)
	PROCESS_EXITHANDLER(runicast_close(&options_conn);)

    // Begin process
	PROCESS_BEGIN();
    	this.addr.u8[0] = linkaddr_node_addr.u8[0];
	this.addr.u8[1] = linkaddr_node_addr.u8[1];
	this.dist_root = 0;

    // Open channels
    runicast_open(&routing_conn, 144, &routing_runicast_callbacks);
    runicast_open(&data_conn, 154, &data_runicast_callbacks);
    runicast_open(&options_conn, 164, &options_runicast_callbacks);
    broadcast_open(&broadcast_conn, 129, &broadcast_callbacks);

    uart0_init(BAUD2UBR(115200)); //set the baud rate as necessary 
  	uart0_set_input(&uart_serial_callback); //set the callback function for serial input

	for(;;){
		PROCESS_WAIT_EVENT();
		if(ev == serial_line_event_message){ // Still looking for input in the command lines
			printf("Serial message read\n");
		}
	}

    PROCESS_END();
}
