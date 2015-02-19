#ifndef _BLOB_H_
#define _BLOB_H_

typedef long blob_t;

blob_t blob_size(blob_t datlen);
blob_t blob_data_size(blob_t *b);
void * blob_data(blob_t *b);
int blob_init(void *buffer, blob_t buflen, void *data, blob_t datlen);
blob_t * blob_create(void *data, blob_t datlen);
int blob_copy(void *buffer, blob_t size, blob_t *b);
blob_t * blob_get_copy(blob_t *b);
blob_t * blob_scan(char *str);
int blob_dump(char *buffer, int size, blob_t *b);
int blob_compare(blob_t *left, blob_t *right);
void blob_test(int n);

#endif /* _BLOB_H_ */
