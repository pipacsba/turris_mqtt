//---------------------- RPI_MONITOR.C --------------------------------------
/*
ADDRESS: mqtt server IP
sleep_sec: how often the measurement data shall be sent
Inputs to call:
 - input 1: verbose settings
            0: no output
            1: full output
to compile:
     sudo gcc -Wall -Ofast /home/pi/rpi_monitor/rpi_monitor.c -I /usr/include/libnl3/ -lpaho-mqtt3c -lm -lnl-genl-3 -lnl-3 -o /home/pi/rpi_monitor/rpi_monitor
*/
//--------------------------------------------------------------------


//---------------------- INCLUDES AND DEFINES --------------------------------------

#include <stdio.h>
#include <math.h>
#include <time.h>
#include <signal.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <termios.h>
#include <fcntl.h>
#include "MQTTClient.h"
#include <sys/sysinfo.h>
#include <sys/statvfs.h>
#include <netlink/netlink.h>    //lots of netlink functions
#include <netlink/genl/genl.h>  //genl_connect, genlmsg_put
#include <netlink/genl/family.h>
#include <netlink/genl/ctrl.h>  //genl_ctrl_resolve
#include <linux/nl80211.h>      //NL80211 definitions

//Defines for MQTT
#define ADDRESS     "tcp://192.168.17.118:1883"
#define CLIENTID    "RpiMonitor"
#define TOPIC       "clock/host"
#define QOS         0
#define TIMEOUT     5000L

//---------------------- END OF INCLUDES AND DEFINES -------------------------------

//----------------------------STRUCTURE DEFINITIONS--------------------

typedef struct {
    int id;
    struct nl_sock* socket;
    struct nl_cb* cb1,* cb2;
    int result1, result2;
} Netlink;

typedef struct {
    char ifname[30];
    int ifindex;
    int signal;
    int txrate;
} Wifi;

static struct nla_policy stats_policy[NL80211_STA_INFO_MAX + 1] = {
    [NL80211_STA_INFO_INACTIVE_TIME] = { .type = NLA_U32 },
    [NL80211_STA_INFO_RX_BYTES] = { .type = NLA_U32 },
    [NL80211_STA_INFO_TX_BYTES] = { .type = NLA_U32 },
    [NL80211_STA_INFO_RX_PACKETS] = { .type = NLA_U32 },
    [NL80211_STA_INFO_TX_PACKETS] = { .type = NLA_U32 },
    [NL80211_STA_INFO_SIGNAL] = { .type = NLA_U8 },
    [NL80211_STA_INFO_TX_BITRATE] = { .type = NLA_NESTED },
    [NL80211_STA_INFO_LLID] = { .type = NLA_U16 },
    [NL80211_STA_INFO_PLID] = { .type = NLA_U16 },
    [NL80211_STA_INFO_PLINK_STATE] = { .type = NLA_U8 },
};

static struct nla_policy rate_policy[NL80211_RATE_INFO_MAX + 1] = {
    [NL80211_RATE_INFO_BITRATE] = { .type = NLA_U16 },
    [NL80211_RATE_INFO_MCS] = { .type = NLA_U8 },
    [NL80211_RATE_INFO_40_MHZ_WIDTH] = { .type = NLA_FLAG },
    [NL80211_RATE_INFO_SHORT_GI] = { .type = NLA_FLAG },
};


//---------------------END OF STRUCTURE DEFINITIONS--------------------

//------------------------FUNCTION DECLARATIONS------------------------

/* FUNCTION: PROGRAM_SLEEP
this subfunction calles the nanosleep() function for the defined seconds
Input:
    sec: the seconds till the program needs to sleep
    verbose: writes slept time to the standard output
*/

void program_sleep(float sec);

// stuff for detecting keypress
int getkey();
int is_key_pressed(void);

// stuff to properly shutdown the process
void term(int signo);

static int initNl80211(Netlink* nl, Wifi* w);
static int finish_handler(struct nl_msg *msg, void *arg);
static int getWifiName_callback(struct nl_msg *msg, void *arg);
static int getWifiInfo_callback(struct nl_msg *msg, void *arg);
static int getWifiStatus(Netlink* nl, Wifi* w);


//------------------END OF FUNCTION DECLARATIONS------------------------

//-------------------------CONSTANTS------------------------------------

// frequency of reading stats in seconds
const int sleep_sec = 1200;
const int mqtt_keepalive_sec = 200;

//--------------------END OF CONSTANTS----------------------------------

//-------------------------GLOBAL VARIABLES-----------------------------
// variable created to handle external KILL signal
volatile sig_atomic_t done = 0;
volatile int first_run = 1;
static volatile int keepRunning = 1;
static volatile int verbose = 0;
// variable to store argv[0] without passing it

//---------------------END OF GLOBAL VARIABLES--------------------------



int main (int argc, char *argv[])
{
    Netlink nl;
    Wifi w;
    
    // prepare function to be killed properly
    struct sigaction action;
    memset(&action, 0, sizeof(struct sigaction));
    action.sa_handler = term;
    sigaction(SIGTERM, &action, NULL);
    sigaction(SIGINT, &action, NULL);
    sigaction(SIGTRAP, &action, NULL);

    // create variables to have terminal messages
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
    float f_load = 1.f / (1 << SI_LOAD_SHIFT);

    // initialize wifi properties access
    nl.id = initNl80211(&nl, &w);
    if (nl.id < 0) 
    {
        if (verbose)
        {
            fprintf(stderr, "Error initializing netlink 802.11\n");
        }
        return -1;
    }

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

        statvfs("/var/log", &log2ram_df);
        float log2ram_total = log2ram_df.f_blocks;
        float log2ram_free  = log2ram_df.f_bfree;
        float log2ram_used = 100.0 - log2ram_free / log2ram_total * 100.0;

        getWifiStatus(&nl, &w);
        if (verbose)
        {
            printf("Interface: %s | signal: %d dB | txrate: %.1f MBit/s\n",
                   w.ifname, w.signal, (float)w.txrate/10);
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
            snprintf(mqtt_payload,250,"{\"CPU_temp\": %.3f, "
                                      "\"load_1\": %.2f, "
                                      "\"mqtt\": %i, "
                                      "\"load_5\": %.2f, "
                                      "\"load_15\": %.2f, "
                                      "\"uptime\": %li, "
                                      "\"log2ram_used\": %.2f, "
                                      "\"wifi_rssi\": %d, "
                                      "\"wifi_txrate\": %.1f }", 
                     systemp, load_1, MQTT_Connected, load_5, load_15, 
                     info.uptime, log2ram_used, w.signal, (float)w.txrate/10);
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
            program_sleep((float)mqtt_keepalive_sec);
            remaining_sleep_sec = remaining_sleep_sec - mqtt_keepalive_sec;
            MQTTClient_yield();
            if (verbose)
            {
                printf("%s\n", MQTTClient_isConnected(client) == 1 ? "Connected" : "Disconnected");
            }
        }

        program_sleep((float)remaining_sleep_sec);

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
    
    //printf("\nExiting gracefully... ");
    nl_cb_put(nl.cb1);
    nl_cb_put(nl.cb2);
    nl_close(nl.socket);
    nl_socket_free(nl.socket);
    //printf("OK\n");
    
    return 0;
}


/* FUNCTION: PROGRAM_SLEEP
this subfunction calles the nanosleep() function for the defined seconds
Input:
    sec: the seconds till the program needs to sleep
    verbose: writes slept time to the standard output
*/
void program_sleep(float sec)
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

/* this function initialize the Netlink connection to the NL80211 interface */
static int initNl80211(Netlink* nl, Wifi* w) 
{
    nl->socket = nl_socket_alloc();
    if (!nl->socket)
    {
        if (verbose == 1)
        {
            fprintf(stderr, "Failed to allocate netlink socket.\n");
        }
        return -ENOMEM;
    }

    nl_socket_set_buffer_size(nl->socket, 8192, 8192);

    if (genl_connect(nl->socket))
    {
        if (verbose == 1)
        {
            fprintf(stderr, "Failed to connect to netlink socket.\n");
        }
        nl_close(nl->socket);
        nl_socket_free(nl->socket);
        return -ENOLINK;
    }

    nl->id = genl_ctrl_resolve(nl->socket, "nl80211");
    if (nl->id< 0) 
    {
        if (verbose == 1)
        {
            fprintf(stderr, "Nl80211 interface not found.\n");
        }
        nl_close(nl->socket);
        nl_socket_free(nl->socket);
        return -ENOENT;
    }

    nl->cb1 = nl_cb_alloc(NL_CB_DEFAULT);
    nl->cb2 = nl_cb_alloc(NL_CB_DEFAULT);
    if ((!nl->cb1) || (!nl->cb2)) 
    {
        if (verbose == 1)
        {
            fprintf(stderr, "Failed to allocate netlink callback.\n");
        }
        
        nl_close(nl->socket);
        nl_socket_free(nl->socket);
        return -ENOMEM;
    }

    //define callback functions
    nl_cb_set(nl->cb1, NL_CB_VALID , NL_CB_CUSTOM, getWifiName_callback, w);
    nl_cb_set(nl->cb1, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &(nl->result1));
    nl_cb_set(nl->cb2, NL_CB_VALID , NL_CB_CUSTOM, getWifiInfo_callback, w);
    nl_cb_set(nl->cb2, NL_CB_FINISH, NL_CB_CUSTOM, finish_handler, &(nl->result2));

    return nl->id;
}


static int finish_handler(struct nl_msg *msg, void *arg) 
{
    int *ret = arg;
    *ret = 0;
    return NL_SKIP;
}


static int getWifiName_callback(struct nl_msg *msg, void *arg)
{
  struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
  struct nlattr *tb_msg[NL80211_ATTR_MAX + 1];

  //nl_msg_dump(msg, stdout);

    nla_parse(tb_msg,
            NL80211_ATTR_MAX,
            genlmsg_attrdata(gnlh, 0),
            genlmsg_attrlen(gnlh, 0),
            NULL);

    if (tb_msg[NL80211_ATTR_IFNAME])
    {
        strcpy(((Wifi*)arg)->ifname, nla_get_string(tb_msg[NL80211_ATTR_IFNAME]));
    }

    if (tb_msg[NL80211_ATTR_IFINDEX]) 
    {
        ((Wifi*)arg)->ifindex = nla_get_u32(tb_msg[NL80211_ATTR_IFINDEX]);
    }

    return NL_SKIP;
}


static int getWifiInfo_callback(struct nl_msg *msg, void *arg) {
    struct nlattr *tb[NL80211_ATTR_MAX + 1];
    struct genlmsghdr *gnlh = nlmsg_data(nlmsg_hdr(msg));
    struct nlattr *sinfo[NL80211_STA_INFO_MAX + 1];
    struct nlattr *rinfo[NL80211_RATE_INFO_MAX + 1];
    //nl_msg_dump(msg, stdout);

    nla_parse(tb,
            NL80211_ATTR_MAX,
            genlmsg_attrdata(gnlh, 0),
            genlmsg_attrlen(gnlh, 0),
            NULL);
            
    /*
    * TODO: validate the interface and mac address!
    * Otherwise, there's a race condition as soon as
    * the kernel starts sending station notifications.
    */

    if (!tb[NL80211_ATTR_STA_INFO])
    {
        if (verbose)
        {
            fprintf(stderr, "sta stats missing!\n"); 
        }
        return NL_SKIP;
    }

    if (nla_parse_nested(sinfo, NL80211_STA_INFO_MAX,
                       tb[NL80211_ATTR_STA_INFO], stats_policy))
    {
        if (verbose)
        {
            fprintf(stderr, "failed to parse nested attributes!\n"); 
        }
        return NL_SKIP;
    }

    if (sinfo[NL80211_STA_INFO_SIGNAL]) 
    {
        ((Wifi*)arg)->signal = (int8_t)nla_get_u8(sinfo[NL80211_STA_INFO_SIGNAL]);
    }

    if (sinfo[NL80211_STA_INFO_TX_BITRATE]) 
    {
        if (nla_parse_nested(rinfo, NL80211_RATE_INFO_MAX, sinfo[NL80211_STA_INFO_TX_BITRATE], rate_policy))
        {
            if (verbose)
            {
                fprintf(stderr, "failed to parse nested rate attributes!\n");
            }
        }
        else 
        {
            if (rinfo[NL80211_RATE_INFO_BITRATE])
            {
                ((Wifi*)arg)->txrate = nla_get_u16(rinfo[NL80211_RATE_INFO_BITRATE]);
            }
        }
    }
    return NL_SKIP;
}

static int getWifiStatus(Netlink* nl, Wifi* w)
{
    nl->result1 = 1;
    nl->result2 = 1;

    if (first_run == 1)
    {
        struct nl_msg* msg1 = nlmsg_alloc();
        if (!msg1)
        {
            if (verbose)
            {
                fprintf(stderr, "Failed to allocate netlink message.\n");
            }
            return -2;
        }

        genlmsg_put(msg1,
                    NL_AUTO_PORT,
                    NL_AUTO_SEQ,
                    nl->id,
                    0,
                    NLM_F_DUMP,
                    NL80211_CMD_GET_INTERFACE,
                    0);

        nl_send_auto(nl->socket, msg1);

        while (nl->result1 > 0) { nl_recvmsgs(nl->socket, nl->cb1); }
        nlmsg_free(msg1);

        if (w->ifindex < 0) 
        {
            return -1;
        }
        else
        {
            first_run = 0;
        }
    }

    struct nl_msg* msg2 = nlmsg_alloc();

    if (!msg2)
    {
        if (verbose)
        {
            fprintf(stderr, "Failed to allocate netlink message.\n");
        }
        return -2;
    }

    genlmsg_put(msg2,
                NL_AUTO_PORT,
                NL_AUTO_SEQ,
                nl->id,
                0,
                NLM_F_DUMP,
                NL80211_CMD_GET_STATION,
                0);

    nla_put_u32(msg2, NL80211_ATTR_IFINDEX, w->ifindex);
    nl_send_auto(nl->socket, msg2);
    while (nl->result2 > 0) { nl_recvmsgs(nl->socket, nl->cb2); }
    nlmsg_free(msg2);

    return 0;
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
Type            Bits    Possible Values
char                8       -127 to 127
unsigned char       8       0 to 255
short               16      -32,767 to 32,767
unsigned short      16      0 to 65,535
int                 32      -2,147,483,647 to 2,147,483,647
unsigned int        32      0 to 4,294,967,295
long                32      -2,147,483,647 to 2,147,483,647
unsigned long       32      0 to 4,294,967,295
long long           64      -9,223,372,036,854,775,807 to 9,223,372,036,854,775,807
unsigned long long  64      0 to 18,446,744,073,709,551,615
float               32      1e-38 to 1e+38
double              64      2e-308 to 1e+308
long double         64      2e-308 to 1e+308
*/
