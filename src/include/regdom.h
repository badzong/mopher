#ifndef _REGDOM_H_
#define _REGDOM_H_

struct regdom_rule
{
	char *r_name;
	char  r_exception;
	char  r_wildcard;
};
typedef struct regdom_rule regdom_rule_t;

void regdom_init (void);
char* regdom (char* name);
void regdom_test (void);

#endif /* _REGDOM_H_ */
