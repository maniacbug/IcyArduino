// Undef conflicting defines from Arduino.h.  Only needed for my command-line
// build process
#undef PI

// C includes
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>

// AVR includes
#include <avr/io.h>

// Library Includes
#include <SPI.h>
#include "EtherBright.h"

// Project Includes
#include "uip_log.h"

// Arduino.h should be last
#undef true
#undef false
#undef PI

#if ARDUINO < 100
#include <WProgram.h>
#else
#include <Arduino.h>
#endif

// Except, there are some things that rely on it.
#include "printf.h"
#include <VS1053.h>
#include <SPI.h>

//
// Definitions
//

#define BUF ((struct uip_eth_hdr *)&uip_buf[0])

//
// Globals
//

VS1053 player(/* cs_pin */ 9,/* dcs_pin */ 6,/* dreq_pin */ 7,/* reset_pin */ 8);
bool connected = false;

//
// Externals
//

extern char* __brkval;
extern char __heap_start;
extern "C" void network_prepare_MAC(uint8_t*);

//
// Statics
//

static struct timer periodic_timer, arp_timer;
static struct uip_eth_addr mac = {{0x00, 0xbd, 0x3b, 0x33, 0x05, 0x71}};

//
// Forward declarations of helpers
//

void connect(void);
void dump_uip_stats(void);

//
// Setup
//

void setup(void)
{
  Serial.begin(57600);
  printf_begin();
  printf_P(PSTR(__FILE__ "\r\n"));
  printf_P(PSTR("Free memory = %u bytes\r\n"),SP-(__brkval?(uint16_t)__brkval:(uint16_t)&__heap_start));

  network_prepare_MAC(mac.addr);
  network_init();

  enc28j60Write(ECOCON, 1 & 0x7);	//Get a 25MHz signal from enc28j60

  uip_ipaddr_t ipaddr;

  uip_init();

  uip_setethaddr(mac);

  uip_ipaddr(ipaddr, 192,168,1,55);
  uip_sethostaddr(ipaddr);
  uip_ipaddr(ipaddr, 192,168,1,1);
  uip_setdraddr(ipaddr);
  uip_ipaddr(ipaddr, 255,255,255,0);
  uip_setnetmask(ipaddr);

  webclient_init();
  player.begin();

  timer_set(&periodic_timer, CLOCK_SECOND / 2);
  timer_set(&arp_timer, CLOCK_SECOND * 10);

  connect();
}

//
// Loop
//

void loop(void)
{
  uip_len = network_read();

  if(uip_len > 0)
  {
    if(BUF->type == htons(UIP_ETHTYPE_IP))
    {
      uip_arp_ipin();
      uip_input();
      if(uip_len > 0)
      {
	uip_arp_out();
	network_send();
      }
    }
    else if(BUF->type == htons(UIP_ETHTYPE_ARP))
    {
      uip_arp_arpin();
      if(uip_len > 0)
      {
	network_send();
      }
    }

  }
  else if(timer_expired(&periodic_timer))
  {
    timer_reset(&periodic_timer);

    for(int i = 0; i < UIP_CONNS; i++)
    {
      uip_periodic(i);
      if(uip_len > 0)
      {
	uip_arp_out();
	network_send();
      }
    }

#if UIP_UDP
    for(int i = 0; i < UIP_UDP_CONNS; i++)
    {
      uip_udp_periodic(i);
      if(uip_len > 0)
      {
	uip_arp_out();
	network_send();
      }
    }
#endif /* UIP_UDP */

    if(timer_expired(&arp_timer))
    {
      timer_reset(&arp_timer);
      uip_arp_timer();
      
      // Uncomment to get a periodic dump of stats.
      //dump_uip_stats();

      // Also use this timer to reconnect if we've lost connection
      if (!connected)
	connect();
    }
  }
}

//
// Helpers
//

void connect(void)
{
  // C89.5: Seattle's hottest music www.c985worldwide.com
  webclient_get_P(PSTR("208.76.152.74"), 8000, PSTR("/"));
  uip_log_P(PSTR("Connecting..."));
}

void dump_uip_stats(void)
{
  printf_P(PSTR("%lu: IP rec %u sent %u ICMP rec %u sent %u\r\n"),
    millis(),
    uip_stat.ip.recv,
    uip_stat.ip.sent,
    uip_stat.icmp.recv,
    uip_stat.icmp.sent
  );
  printf_P(PSTR("%lu: CONNS "),millis());
  for( int i=0; i < UIP_CONNS; i++)
  {
    printf_P(PSTR("%u/%u "),ntohs(uip_conns[i].lport),ntohs(uip_conns[i].rport));
  }
  Serial.println();
}

/****************************************************************************/
//
// Statics
//

// Transfer Statistics
static uint32_t size_received;
static uint32_t started_at;
static uint8_t dots_until_cr;
static const uint8_t dots_per_cr = 80;

/****************************************************************************/
/**
 * Callback function that is called from the webclient code when HTTP
 * data has been received.
 *
 * This function must be implemented by the module that uses the
 * webclient code. The function is called from the webclient module
 * when HTTP data has been received. The function is not called when
 * HTTP headers are received, only for the actual data.
 *
 * \note This function is called many times, repetedly, when data is
 * being received, and not once when all data has been received.
 *
 * \param data A pointer to the data that has been received.
 * \param len The length of the data that has been received.
 */
void webclient_datahandler(char *data, u16_t len)
{
  Serial.print('.');
  if ( ! --dots_until_cr )
  {
    Serial.println();
    dots_until_cr = dots_per_cr;
  }

  if ( ! started_at )
    started_at = millis();

  size_received += len;

  player.playChunk(reinterpret_cast<uint8_t*>(data),len);

  if (!data)
  {
    Serial.println();
    printf_P(PSTR("%lu: DONE. Received %lu bytes in %lu msec.\r\n"),millis(),size_received,millis()-started_at);
  }
}

/****************************************************************************/
/**
 * Callback function that is called from the webclient code when the
 * HTTP connection has been connected to the web server.
 *
 * This function must be implemented by the module that uses the
 * webclient code.
 */
void webclient_connected(void)
{
  uip_log_P(PSTR("webclient_connected"));
  player.startSong();
  connected = true;
  dots_until_cr = dots_per_cr;
}

/****************************************************************************/
/**
 * Callback function that is called from the webclient code if the
 * HTTP connection to the web server has timed out.
 *
 * This function must be implemented by the module that uses the
 * webclient code.
 */
void webclient_timedout(void)
{
  uip_log_P(PSTR("TIMEOUT.  Reconnecting within 10s.\r\n"));
  player.stopSong();
  connected = false;
}

/****************************************************************************/
/**
 * Callback function that is called from the webclient code if the
 * HTTP connection to the web server has been aborted by the web
 * server.
 *
 * This function must be implemented by the module that uses the
 * webclient code.
 */
void webclient_aborted(void)
{
  uip_log_P(PSTR("ABORTED.  Reconnecting within 10s.\r\n"));
  player.stopSong();
  connected = false;
}

/****************************************************************************/
/**
 * Callback function that is called from the webclient code when the
 * HTTP connection to the web server has been closed.
 *
 * This function must be implemented by the module that uses the
 * webclient code.
 */
void webclient_closed(void)
{
  uip_log_P(PSTR("webclient_closed\r\n"));
  player.stopSong();
  connected = false;
}

/*---------------------------------------------------------------------------*/
// vim:cin:ai:sts=2 sw=2 ft=cpp
