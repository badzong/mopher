typedef int (*db_connect_t)(db_t *db);
typedef int (*db_close_t)(db_t *db);
typedef int (*db_insert_t)(db_t *db, ll_t *record);
typedef int (*db_update_t)(db_t *db, ll_t *record);
typedef int (*db_delete_t)(db_t *db, ll_t *record);
typedef int (*db_select_t)(db_t *db, ll_t *record, ll_t **result);
typedef int (*db_count_t)(db_t *db, ll_t *record);

typedef struct db_driver {
	db_connect_t	dbd_connect;
	db_close_t	dbd_close;
	db_insert_t	dbd_insert;
	db_update_t	dbd_update;
	db_delete_t	dbd_delete;
	db_select_t	dbd_select;
	db_count_t	dbd_count;
} db_driver_t;

typedef struct db {
	char *db_id;
	char *db_link;
	void *db_handle;
} db_t;

static ht_t *db_connections;

int
db_init(void);
{
	
}
