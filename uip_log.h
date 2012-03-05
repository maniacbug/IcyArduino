#ifndef __UIP_LOG_H__
#define __UIP_LOG_H__
#include <avr/pgmspace.h>

extern "C" void uip_log_P(const char* msg);
extern "C" void uip_log(char* msg);
#endif // __UIP_LOG_H__
