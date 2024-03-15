//---------------------- CLOCK.C --------------------------------------
/*
This clock.c is driving a 4x7 segment display (ht16K33v110) to display the time.
The dimming is set based on a light sensor value from TSL2561 (CS package!).
The display and the light sensor are connected to a raspberry pi 2 I2C outputs.

Inputs to call:
 - input 1: verbose settings
			0: no output
			1: full output

to compile:
	 gcc -Wall -Ofast rpi_monitor.c -lpaho-mqtt3c -lm -o rpi_monitor

*/
//--------------------------------------------------------------------

//---------------------- INCLUDES AND DEFINES --------------------------------------

#include <stdio.h>
#include <math.h>
#include <time.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <termios.h>
#include <fcntl.h>
#include <signal.h>
#include "MQTTClient.h"
#include <sys/sysinfo.h>
#include <sys/statvfs.h>

//Defines for MQTT
#define ADDRESS     "tcp://192.168.17.118:1883"
#define CLIENTID    "Rpi400Monitor"
#define TOPIC       "hass/host"
#define QOS         0
#define TIMEOUT     5000L

//---------------------- END OF INCLUDES AND DEFINES -------------------------------

//----------------------------STRUCTURE DEFINITIONS--------------------


//---------------------END OF STRUCTURE DEFINITIONS--------------------

//------------------------FUNCTION DECLARATIONS------------------------

/* FUNCTION: PROGRAM_SLEEP
this subfunction calles the nanosleep() function for the defined seconds
Input:
	sec: the seconds till the program needs to sleep
	verbose: writes slept time to the standard output
*/

void program_sleep(float sec, int verbose);

// stuff for detecting keypress
int getkey();
int is_key_pressed(void);

// stuff to properly shutdown the process
void term(int signo);

//------------------END OF FUNCTION DECLARATIONS------------------------

//-------------------------CONSTANTS------------------------------------

// frequency of reading stats in seconds
const int sleep_sec = 300;
const int mqtt_keepalive_sec = 120;

//--------------------END OF CONSTANTS----------------------------------

//-------------------------GLOBAL VARIABLES-----------------------------
// variable created to handle external KILL signal
volatile sig_atomic_t done = 0;
// variable to store argv[0] without passing it

//---------------------END OF GLOBAL VARIABLES--------------------------


int main (int argc, char *argv[])
{
	// prepare function to be killed properly
	struct sigaction action;
	memset(&action, 0, sizeof(struct sigaction));
	action.sa_handler = term;
	sigaction(SIGTERM, &action, NULL);
	sigaction(SIGINT, &action, NULL);
	sigaction(SIGTRAP, &action, NULL);

	// create variables to have terminal messages
	int verbose = 0;
	// if the program is started with a number argument above or equal to 1, than turn on terminal messages
	if (argc > 1)
	{
		verbose = atol(argv[1]);
	}

	int key;

	//set up MQTT
	char mqtt_payload[100];
	int MQTT_Connected = 0;
	MQTTClient client;
	MQTTClient_connectOptions conn_opts = MQTTClient_connectOptions_initializer;
	MQTTClient_message pubmsg = MQTTClient_message_initializer;
	MQTTClient_deliveryToken token;

	MQTTClient_create(&client, ADDRESS, CLIENTID, MQTTCLIENT_PERSISTENCE_NONE, NULL);
	conn_opts.keepAliveInterval = mqtt_keepalive_sec;
	conn_opts.cleansession = 1;

	// initialize temperature measurement
	float systemp, millideg;
	FILE *thermal;
	int n;

	// initialize sysinfo measurements
	struct sysinfo info;
        struct statvfs log2ram_df;
	struct statvfs ramdisk_df;
	float f_load = 1.f / (1 << SI_LOAD_SHIFT);

        //initialize system update info
        char path[55];
        int update_count;

	// continous operation (while(1))
	// done parameter can be changed by application kill signal for proper shutdown
	while(!done)
	{
	        thermal = fopen("/sys/class/thermal/thermal_zone0/temp", "r");
	        n = fscanf(thermal, "%f", &millideg);
	        fclose(thermal);
		if (n == 1)
		{
	        	systemp = millideg / 1000.0f;
		}
		else { systemp = 0; }

		// get uptime & loads
		sysinfo(&info);
		float load_1 = info.loads[0] * f_load;
		float load_5 = info.loads[1] * f_load;
		float load_15 = info.loads[2] * f_load;

                // ramdisk usage log2ram
                statvfs("/var/log", &log2ram_df);
                float log2ram_total = log2ram_df.f_blocks;
                float log2ram_free  = log2ram_df.f_bfree;
                float log2ram_used = 100.0 - log2ram_free / log2ram_total * 100.0;

                // ramdisk uage homeassistant ramdisk
                statvfs("/usr/share/hassio/homeassistant/ramdisk", &ramdisk_df);
                float ramdisk_total = ramdisk_df.f_blocks;
                float ramdisk_free  = ramdisk_df.f_bfree;
                float ramdisk_used = 100.0 - ramdisk_free / ramdisk_total * 100.0;

                // system update
                update_count = -1;
                FILE *fp = popen("apt list --upgradable 2> /dev/nul","r");
                while (fgets(path, sizeof(path), fp) != NULL)
                {
                   update_count = update_count + 1;
                }


		if (MQTTClient_isConnected(client) == 1)
		{
			MQTT_Connected = 1;
			if (verbose)
			{
				printf("MQTT connection is alive\n");
			}
		}
		else
		{
			MQTT_Connected = 0;
			if (MQTTClient_connect(client, &conn_opts) == MQTTCLIENT_SUCCESS)
			{
				MQTT_Connected = 2;
				if (verbose)
				{
					printf("MQTT connection was not alive, connected\n");
				}
			}
		}
		if (MQTT_Connected >= 1)
		{
			snprintf(mqtt_payload,200,"{\"CPU_temp\": %.3f, \"load_1\": %.2f, \"mqtt\": %i, \"load_5\": %.2f, \"load_15\": %.2f, \"uptime\": %li, \"log2ram_used\": %.2f, \"ramdisk_used\": %.2f, \"system_updates\": %d}",
				systemp, load_1, MQTT_Connected, load_5, load_15, info.uptime, log2ram_used, ramdisk_used, update_count);
			pubmsg.payload = mqtt_payload;
			pubmsg.payloadlen = strlen(mqtt_payload);
			pubmsg.qos = QOS;
			pubmsg.retained = 1;
			MQTTClient_publishMessage(client, TOPIC, &pubmsg, &token);
			MQTTClient_waitForCompletion(client, token, 5000);
			if (verbose)
			{
				printf("%s\n", mqtt_payload);
			}
		}

		int remaining_sleep_sec = sleep_sec;
		while (remaining_sleep_sec > mqtt_keepalive_sec)
		{
			program_sleep((float)mqtt_keepalive_sec, verbose);
			remaining_sleep_sec = remaining_sleep_sec - mqtt_keepalive_sec;
			MQTTClient_yield();
			if (verbose)
			{
				printf("%s\n", MQTTClient_isConnected(client) == 1 ? "Connected" : "Disconnected");
			}
		}

		program_sleep((float)remaining_sleep_sec,verbose);



		// exit on key-press, only works if verbose > 1
		if (verbose && is_key_pressed())
		{
			key = getkey();
			printf("key pressed: %c \n",key);
			break;
		}
	}
	//Disconnect and Destroy MQTT
	MQTTClient_disconnect(client, 10000);
	MQTTClient_destroy(&client);
	return 0;
}


/* FUNCTION: PROGRAM_SLEEP
this subfunction calles the nanosleep() function for the defined seconds
Input:
	sec: the seconds till the program needs to sleep
	verbose: writes slept time to the standard output
*/
void program_sleep(float sec, int verbose)
{
	// create nanosleep structure
	struct timespec ts;
	// convert input to nanosleep structure
	ts.tv_sec = (int)sec;
	ts.tv_nsec = (sec-(int)sec)*1000000000;
	// perform the sleep
	nanosleep(&ts, NULL);
	if (verbose == 1)
	{
		printf("slept for %g sec\n",sec);
	}
}


/* stuff to detect key press */
int getkey()
{
	int character;
	struct termios orig_term_attr;
	struct termios new_term_attr;

	// Set the terminal to raw mode
	tcgetattr(fileno(stdin), &orig_term_attr);
	memcpy(&new_term_attr, &orig_term_attr, sizeof(struct termios));
	new_term_attr.c_lflag &= ~(ECHO|ICANON);
	new_term_attr.c_cc[VTIME] = 0;
	new_term_attr.c_cc[VMIN] = 0;
	tcsetattr(fileno(stdin), TCSANOW, &new_term_attr);

	// read a character from the stdin stream without blocking
	//  returns EOF (-1) if no character is available
	character = fgetc(stdin);

	// Restore the original terminal attributes
	tcsetattr(fileno(stdin), TCSANOW, &orig_term_attr);

	return character;
}

/* stuff to avoid the program from waiting for key press */
int is_key_pressed(void)
{
	struct timeval tv;
	fd_set fds;
	tv.tv_sec = 0;
	tv.tv_usec = 0;

	FD_ZERO(&fds);
	FD_SET(STDIN_FILENO, &fds);

	select(STDIN_FILENO+1, &fds, NULL, NULL, &tv);
	return FD_ISSET(STDIN_FILENO, &fds);
}

/* stuff to handle KILL request */
void term(int signo)
{
	done = 1;
}

/*
DATA TYPES:
Type 			Bits 	Possible Values
char 			8 		-127 to 127
unsigned char 		8 		0 to 255
short			16 		-32,767 to 32,767
unsigned short 		16 		0 to 65,535
int 			32 		-2,147,483,647 to 2,147,483,647
unsigned int 		32 		0 to 4,294,967,295
long			32 		-2,147,483,647 to 2,147,483,647
unsigned long 		32 		0 to 4,294,967,295
long long 		64 		-9,223,372,036,854,775,807 to 9,223,372,036,854,775,807
unsigned long long 	64 		0 to 18,446,744,073,709,551,615
float 			32 		1e-38 to 1e+38
double 			64 		2e-308 to 1e+308
long double 		64 		2e-308 to 1e+308
*/
