#include <string.h>
#include <stdlib.h>
#include <mopher.h>

blob_t
blob_calc_size(blob_t datlen)
{
	return datlen + sizeof (blob_t);
}

blob_t
blob_size(blob_t *b)
{
	if (b == NULL)
	{
		return 0;
	}

	return *b + sizeof (blob_t);
}

void *
blob_data(blob_t *b)
{
	char *p = (char *) b;

	if (b == NULL)
	{
		return NULL;
	}

	return p + sizeof (blob_t);
}

int
blob_init(void *buffer, blob_t buflen, void *data, blob_t datlen)
{
	blob_t *b = buffer;
	
	if (blob_calc_size(datlen) > buflen)
	{
		log_error("blob_init: buffer exhausted");
		return -1;
	}

	*b = datlen;
	memcpy(blob_data(b), data, datlen);

	return 0;
}

blob_t *
blob_create(void *data, blob_t size)
{
	blob_t *b;
	unsigned long bs;

	bs = blob_calc_size(size);

	b = malloc(bs);
	if (b == NULL)
	{
		log_sys_error("blob_create: malloc");
		return NULL;
	}

	if (blob_init(b, bs, data, size))
	{
		log_error("blob_create: blob_init failed");
		free(b);
		return NULL;
	}

	return b;
}

int
blob_copy(void *buffer, blob_t size, blob_t *b)
{
	blob_t bs = blob_size(b);

	if (bs > size)
	{
		log_error("blob_copy: buffer exhausted");
		return -1;
	}

	memcpy(buffer, b, bs);

	return 0;
}

blob_t *
blob_get_copy(blob_t *b)
{
        blob_t *copy;

        copy = blob_create(blob_data(b), *b);
        if (copy == NULL)
        {
                log_error("blob_copy: blob_create failed");
        }

        return copy;
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
	return base64_encode(buffer, size, blob_data(b), *b);
}

int
blob_compare(blob_t *left, blob_t *right)
{
	if (*left < *right)
	{
		return -1;
	}

	if (*left > *right)
	{
		return 1;
	}

	return memcmp(blob_data(left), blob_data(right), *left);
}

#ifdef DEBUG

void
blob_test(int n)
{
	char buffer[4096];
	char str[] = "hello world";
	blob_t *b;

	// blob_init
	TEST_ASSERT(blob_init(buffer, sizeof buffer, str, strlen(str) + 1) == 0);
	b = (blob_t *) buffer;
	TEST_ASSERT(*b == blob_size(b) - sizeof (blob_t));
	TEST_ASSERT(*b == strlen(str) + 1);
	TEST_ASSERT(strcmp(blob_data(b), str) == 0);

	// blob_create
	b = blob_create(str, strlen(str) + 1);
	TEST_ASSERT(b != NULL);
	TEST_ASSERT(*b == strlen(str) + 1);
	TEST_ASSERT(blob_compare(b, (blob_t *) buffer) == 0);
	TEST_ASSERT(memcmp(b, buffer, *b) == 0);
	free(b);

	// blob_get_copy
	b = blob_get_copy((blob_t *) buffer);
	TEST_ASSERT(b != NULL);
	TEST_ASSERT(*b == strlen(str) + 1);
	TEST_ASSERT(blob_compare(b, (blob_t *) buffer) == 0);
	TEST_ASSERT(memcmp(b, buffer, *b) == 0);

	// blob_copy
	TEST_ASSERT(blob_copy(buffer, sizeof buffer, b) == 0);
	free(b);
	b = (blob_t *) buffer;
	TEST_ASSERT(*b == strlen(str) + 1);
	TEST_ASSERT(memcmp(b, buffer, *b) == 0);

	// blob_dump
	b = blob_create(str, strlen(str) + 1);
	TEST_ASSERT(b != NULL);
	TEST_ASSERT(blob_dump(buffer, sizeof buffer, b) > 0);
	free(b);

	// blob_scan
	b = blob_scan(buffer);
	TEST_ASSERT(b != NULL);
	TEST_ASSERT(*b == strlen(str) + 1);
	TEST_ASSERT(strcmp(blob_data(b), str) == 0);
	free(b);

	return;
}

#endif
