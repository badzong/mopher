#ifndef _REGDOM_H_
#define _REGDOM_H_

#define REGDOM_FREE_NAME 1<<0

struct regdom_rule
{
	char *r_name;
	char  r_exception;
	char  r_wildcard;
	int   r_flags;
};
typedef struct regdom_rule regdom_rule_t;

void regdom_clear (void);
void regdom_init (void);
char * regdom_strdup_idna(char *name);
int regdom_punycode (char *buffer, int size, char* name);
void regdom_test (int n);

#endif /* _REGDOM_H_ */
