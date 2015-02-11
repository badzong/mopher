#ifndef _BLOB_H_
#define _BLOB_H_

typedef struct blob {
	int            b_size;
	int            b_offset;
	unsigned char *b_data;
} blob_t;

int blob_init(void *buffer, int buflen, void *data, int datlen);
blob_t *blob_create(void *data, int size);
blob_t *blob_copy(blob_t *b);
blob_t *blob_scan(char *str);
int blob_dump(char *buffer, int size, blob_t *b);
int blob_compare(blob_t *left, blob_t *right);
void blob_test(int n);

#endif /* _BLOB_H_ */
