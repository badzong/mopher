#include <string.h>
#include <stdlib.h>
#include <mopher.h>


int
blob_init(void *buffer, int buflen, void *data, int datlen)
{
	blob_t *b = buffer;
	int offset = sizeof (blob_t);
	int full_size = offset + datlen;

	if (full_size > buflen)
	{
		log_error("blob_init: buffer exhausted");
		return -1;
	}

	b->b_offset = offset;
	b->b_size   = full_size;
	b->b_data   = ((unsigned char *) b) + offset;

	memcpy(b->b_data, data, datlen);

	return 0;
}

blob_t *
blob_create(void *data, int size)
{
	blob_t *b;
	int offset = sizeof (blob_t);
	int full_size = offset + size;

	b = malloc(full_size);
	if (b == NULL)
	{
		log_sys_error("blob_create: malloc");
		return NULL;
	}

	if (blob_init(b, full_size, data, size))
	{
		log_error("blob_create: blob_init failed");
		free(b);
		return NULL;
	}

	return b;
}

blob_t *
blob_copy(blob_t *b)
{
	return blob_create(b->b_data, b->b_size - b->b_offset);
}

blob_t *
blob_scan(char *str)
{
	blob_t * b;
	void *buffer = NULL;
	int size;

	buffer = base64_dec_malloc(str, &size);
	if (buffer == NULL)
	{
		log_error("blob_scan: base64_dec_malloc failed");
		goto error;
	}

	b = blob_create(buffer, size);
	if (b == NULL)
	{
		log_error("blob_scan: blob_create failed");
		goto error;
	}

	free(buffer);

	return b;

error:

	if (buffer)
	{
		free(buffer);
	}

	return NULL;
}

int
blob_dump(char *buffer, int size, blob_t *b)
{
	return base64_encode(buffer, size, b->b_data, b->b_size - b->b_offset);
}

int
blob_compare(blob_t *left, blob_t *right)
{
	if (left->b_size < right->b_size)
	{
		return -1;
	}

	if (left->b_size > right->b_size)
	{
		return 1;
	}

	return memcmp(left->b_data, right->b_data, left->b_size - left->b_offset);
}

#ifdef DEBUG

void
blob_test(int n)
{
	blob_t *b;
	char buffer[4096];
	int size;

	b = blob_create(&n, sizeof n);
	TEST_ASSERT(b != NULL);
	TEST_ASSERT(blob_dump(buffer, sizeof buffer, b) > 0);
	free(b);
	b = blob_scan(buffer);
	TEST_ASSERT(b != NULL);
	TEST_ASSERT(b->b_size - b->b_offset == sizeof n);
	size = *((int *) b->b_data);
	TEST_ASSERT(size == n);
	free(b);

	return;
}

#endif
