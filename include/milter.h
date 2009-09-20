#ifndef _MILTER_H_
#define _MILTER_H_

#include <libmilter/mfapi.h>

#include "var.h"

//typedef enum milter_stage { MS_NONE = 0, MS_CONNECT, MS_HELO, MS_FROM,
//    MS_RCPT, MS_HEADER, MS_EOH, MS_BODY, MS_EOM } milter_stage_t;

typedef enum milter_stage { MS_NULL = 0, MS_CONNECT = 1<<0, MS_HELO = 1<<1,
	MS_ENVFROM = 1<<2, MS_ENVRCPT = 1<<3, MS_HEADER = 1<<4, MS_EOH =
	    1<<5, MS_BODY = 1<<6, MS_EOM = 1<<7 } milter_stage_t;

typedef struct milter_priv {
    var_t    *mp_table;
    int      mp_recipients;
    char    *mp_message;
    int      mp_size;
} milter_priv_t;

extern int milter_running;

int8_t
milter(void);

#endif
