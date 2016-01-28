#ifndef _WATCHDOG_H_
#define _WATCHDOG_H_

void watchdog(var_t *table, char *stage);
void watchdog_check(void);
void watchdog_close(var_t *table);

#endif /* _WATCHDOG_H_ */
