#ifndef _MSGMOD_H_
#define _MSGMOD_H_

enum msgmod_type
{
	MM_ADDHDR,
	MM_CHGHDR,
	MM_CHGHDR_X,
	MM_DELHDR,
	MM_INSHDR,
	MM_INSHDR_X,
	MM_CHGFROM,
	MM_CHGFROM_X,
	MM_ADDRCPT,
	MM_ADDRCPT_X,
	MM_DELRCPT,
	MM_CHGBODY
};
typedef enum msgmod_type msgmod_type_t;

struct msgmod
{
	msgmod_type_t	 mm_type;
	ll_t		*mm_args;
};
typedef struct msgmod msgmod_t;

/*
 * Prototypes
 */

msgmod_t * msgmod_create(msgmod_type_t type, ...);
void msgmod_delete(void *data);
acl_action_type_t msgmod(milter_stage_t stage, char *stagename, var_t *mailspec, void *data);
#endif /* _MSGMOD_H_ */
