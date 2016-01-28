#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <mopher.h>

typedef struct watchdog {
	char   *wd_id;
	char   *wd_stage;
	time_t  wd_received;
	time_t  wd_instage;
} watchdog_t;

static pthread_mutex_t watchdog_mutex = PTHREAD_MUTEX_INITIALIZER;
static sht_t watchdog_table;

static watchdog_t *
watchdog_create(char *id, char *stage)
{
	watchdog_t *wd;

	wd = malloc(sizeof (watchdog_t));
	if (wd == NULL)
	{
		log_sys_error("watchdog_create: malloc");
		return NULL;
	}

	wd->wd_id = id;
	wd->wd_stage = stage;
	wd->wd_instage = wd->wd_received = time(NULL);

	return wd;
}

static void
watchdog_init(void)
{
	// Initialize watchdog_table if neccessary
	if (watchdog_table.sht_ht == NULL)
	{
		if(sht_init(&watchdog_table, 256, free))
		{
			log_error("watchdog: sht_init failed");
		}
	}

	return;
}

void
watchdog(var_t *table, char *stage)
{
	watchdog_t *wd;
	char *id;

	if (pthread_mutex_lock(&watchdog_mutex))
	{
		log_sys_error("watchdog: pthread_mutex_lock");
		return;
	}

	watchdog_init();

	// In init id is not set. No problem.
	id = vtable_get(table, "id");
	if (id == NULL)
	{
		goto exit;
	}


	// Record does not exist
	wd = sht_lookup(&watchdog_table, id);
	if (wd == NULL)
	{
		wd = watchdog_create(id, stage);
		if (wd == NULL)
		{
			log_error("watchdog: watchdog_create failed");
			goto exit;
		}

		if (sht_insert(&watchdog_table, id, wd))
		{
			log_error("watchdog: sht_insert failed");
			goto exit;
		}
	}
	else
	{
		wd->wd_stage = stage;
		wd->wd_instage = time(NULL);
	}


exit:
	if (pthread_mutex_unlock(&watchdog_mutex))
	{
		log_sys_error("watchdog: pthread_mutex_unlock");
	}

	return;
}

void
watchdog_check(void)
{
	watchdog_t *wd;
	ht_pos_t pos;
	time_t diff;
	time_t now = time(NULL);

	if (pthread_mutex_lock(&watchdog_mutex))
	{
		log_sys_error("watchdog: pthread_mutex_lock");
		return;
	}

	watchdog_init();

	// Check all records
	sht_start(&watchdog_table, &pos);
	while ((wd = sht_next(&watchdog_table, &pos)))
	{
		diff = now - wd->wd_received;
		if (diff <= cf_watchdog_stage_timeout)
		{
			continue;
		}

		log_error("%s: %s: slow connection: age=%d %s=%d",
			wd->wd_id, wd->wd_stage, diff, wd->wd_stage,
			now - wd->wd_instage);
	}

	if (pthread_mutex_unlock(&watchdog_mutex))
	{
		log_sys_error("watchdog: pthread_mutex_unlock");
	}

	return;
}

void
watchdog_close(var_t *table)
{
	char *id;

	if (pthread_mutex_lock(&watchdog_mutex))
	{
		log_sys_error("watchdog_remove: pthread_mutex_lock");
		goto exit;
	}

	id = vtable_get(table, "id");
	if (id == NULL)
	{
		log_error("watchdog_remove: vtable_get returned no id");
		goto exit;
	}

	sht_remove(&watchdog_table, id);

exit:
	if (pthread_mutex_unlock(&watchdog_mutex))
	{
		log_sys_error("watchdog_remove: pthread_mutex_unlock");
	}

	return;
}
