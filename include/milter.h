#ifndef _MILTER_H_
#define _MILTER_H_

#include <libmilter/mfapi.h>

#include "ht.h"

//typedef enum milter_stage { MS_NONE = 0, MS_CONNECT, MS_HELO, MS_FROM,
//    MS_RCPT, MS_HEADER, MS_EOH, MS_BODY, MS_EOM } milter_stage_t;

typedef struct milter_priv {
    ht_t    *mp_table;
    int      mp_recipients;
    char    *mp_message;
    int      mp_size;
} milter_priv_t;

int8_t
milter(void);

#endif
