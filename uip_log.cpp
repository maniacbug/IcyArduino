#include <Arduino.h>

extern "C" void uip_log(char *msg)
{
  printf_P(PSTR("%lu: %s\r\n"),millis(),msg);
}

extern "C" void uip_log_P(prog_char* msg)
{
  printf_P(PSTR("%lu: %S\r\n"),millis(),msg);
}
