#ifndef _MILTER_H_
#define _MILTER_H_

#include <libmilter/mfapi.h>

#include <var.h>

//typedef enum milter_stage { MS_NONE = 0, MS_CONNECT, MS_HELO, MS_FROM,
//    MS_RCPT, MS_HEADER, MS_EOH, MS_BODY, MS_EOM } milter_stage_t;

typedef enum milter_stage {
	MS_NULL		= 0,
	MS_CONNECT	= 1<<0,
	MS_UNKNOWN	= 1<<1,
	MS_HELO		= 1<<2,
	MS_ENVFROM	= 1<<3,
	MS_ENVRCPT	= 1<<4,
	MS_DATA		= 1<<5,
	MS_HEADER	= 1<<6,
	MS_EOH		= 1<<7,
	MS_BODY		= 1<<8,
	MS_EOM		= 1<<9,
	MS_ABORT	= 1<<10,
	MS_CLOSE	= 1<<11,

	MS_ANY		= MS_CONNECT | MS_HELO | MS_ENVFROM | MS_ENVRCPT |
			  MS_DATA | MS_HEADER | MS_EOH | MS_BODY | MS_EOM |
			  MS_ABORT | MS_CLOSE,
	MS_OFF_CONNECT	= MS_ANY,
	MS_OFF_HELO	= MS_HELO | MS_ENVFROM | MS_ENVRCPT | MS_DATA |
			  MS_HEADER | MS_EOH | MS_BODY | MS_EOM | MS_ABORT |
			  MS_CLOSE,
	MS_OFF_ENVFROM	= MS_ENVFROM | MS_ENVRCPT | MS_DATA | MS_HEADER |
			  MS_EOH | MS_BODY | MS_EOM | MS_ABORT | MS_CLOSE,
	MS_OFF_ENVRCPT	= MS_ENVRCPT | MS_DATA | MS_HEADER | MS_EOH | MS_BODY |
			  MS_EOM | MS_ABORT | MS_CLOSE,
	MS_OFF_DATA	= MS_DATA | MS_HEADER | MS_EOH | MS_BODY | MS_EOM |
			  MS_ABORT | MS_CLOSE,
	MS_OFF_HEADER	= MS_HEADER | MS_EOH | MS_BODY | MS_EOM | MS_ABORT |
			  MS_CLOSE,
	MS_OFF_EOH	= MS_EOH | MS_BODY | MS_EOM | MS_ABORT | MS_CLOSE,
	MS_OFF_BODY	= MS_BODY | MS_EOM | MS_ABORT | MS_CLOSE
} milter_stage_t;

typedef struct milter_priv {
    var_t   *mp_table;
    int      mp_recipients;
    char    *mp_header;
    int      mp_headerlen;
    char    *mp_body;
    int      mp_bodylen;
} milter_priv_t;

typedef struct milter_macro {
	char		*mm_name;
	char		*mm_macro;
	milter_stage_t	 mm_stage;
} milter_macro_t;

typedef struct milter_symbol {
	char		*ms_name;
	milter_stage_t	 ms_stage;
} milter_symbol_t;


extern int milter_running;

/*
 * Prototypes
 */

void milter_init(void);
void milter_clear(void);
int8_t milter(void);
int milter_set_reply(var_t *mailspec, char *code, char *xcode, char *message);
#endif /* _MILTER_H_ */
