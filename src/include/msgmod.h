#ifndef _MSGMOD_H_
#define _MSGMOD_H_

enum msgmod_mod
{
	MO_ADD,
	MO_INS,
	MO_CHG,
	MO_DEL
};
typedef enum msgmod_mod msgmod_mod_t;

enum msgmod_target
{
	MT_FROM,
	MT_RCPT,
	MT_HEADER,
	MT_BODY
};
typedef enum msgmod_target msgmod_target_t;

typedef int (*msgmod_callback_t)(void *ctx, int argc, var_t *args[]);

struct msgmod
{
	ll_t			*mm_args;
	msgmod_callback_t	 mm_callback;
};
typedef struct msgmod msgmod_t;

/*
 * Prototypes
 */

msgmod_target_t msgmod_get_target(char *id);
msgmod_t * msgmod_create(msgmod_mod_t mod, msgmod_target_t target, ...);
void msgmod_delete(void *data);
acl_action_type_t msgmod(milter_stage_t stage, char *stagename, var_t *mailspec, void *data, int depth);

int msgmod_add_header(void *ctx, int argc, var_t *args[]);
int msgmod_change_header(void *ctx, int argc, var_t *args[]);
int msgmod_delete_header(void *ctx, int argc, var_t *args[]);
int msgmod_insert_header(void *ctx, int argc, var_t *args[]);
int msgmod_change_from(void *ctx, int argc, var_t *args[]);
int msgmod_add_rcpt(void *ctx, int argc, var_t *args[]);
int msgmod_delete_rcpt(void *ctx, int argc, var_t *args[]);
int msgmod_change_body(void *ctx, int argc, var_t *args[]);

void msgmod_test(int n);

#endif /* _MSGMOD_H_ */
