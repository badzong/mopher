#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <mopher.h>

static char encoder[]="ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
static char decoder[256];
static int initialized;

static void
base64_init_decoder(void)
{
	int i = 0;

	if (initialized)
	{
		return;
	}

	// -1 is used for error detection
	memset(decoder, -1, sizeof decoder);

	for (; i < 64; decoder[(int) encoder[i]] = i, ++i);

	initialized = 1;

	return;
}

static int
base64_encsize(int size)
{
	return 4 * ((size + 2) / 3);
}

int
base64_encode(char *dest, int size, unsigned char *src, int slen)
{
	int dlen, i, j;
	uint32_t a, b, c, triple;

	dlen = base64_encsize(slen);

	// Sanity checks
	if (src == NULL || dest == NULL)
	{
		return -1;
	}
	if (dlen + 1 > size)
	{
		return -1;
	}
	if (slen == 0)
	{
		if (size > 0)
		{
			dest[0] = 0;
			return 0;
		}
		return -1;
	}

	for (i = 0, j = 0; i < slen;)
	{
		a = src[i++];

		// b and c may be off limit
		b = i < slen ? src[i++] : 0;
		c = i < slen ? src[i++] : 0;

		triple = (a << 16) + (b << 8) + c;

		dest[j++] = encoder[(triple >> 18) & 0x3F];
		dest[j++] = encoder[(triple >> 12) & 0x3F];
		dest[j++] = encoder[(triple >> 6) & 0x3F];
		dest[j++] = encoder[triple & 0x3F];
	}

	// Pad zeroes at the end
	switch (slen % 3)
	{
	case 1:
		dest[j - 2] = '=';
	case 2:
		dest[j - 1] = '=';
	}

	// Always add \0
	dest[j] = 0;

	return dlen;
}

char *
base64_enc_malloc(unsigned char *src, int slen)
{
	int size;
	char *buffer;

	size = base64_encsize(slen) + 1;

	buffer = (char *) malloc(size);
	if (buffer == NULL)
	{
		return NULL;
	}

	size = base64_encode(buffer, size, src, slen);
	if (size == -1)
	{
		free(buffer);
		return NULL;
	}

	return buffer;
}

static int
base64_decsize(char *src)
{
	int slen, size, i;

	if (src == NULL)
	{
		return 0;
	}

	slen = strlen(src);
	size = slen / 4 * 3;

	// Count pad chars
	for (i = 0 ; src[slen - i - 1] == '='; ++i);

	// Remove at most 2 bytes.
	return size - (i >= 2? 2: i);
}


int
base64_decode(unsigned char *dest, int size, char *src)
{
	int slen, dlen, padlen, i, j;
	uint32_t a, b, c, d, triple;

	// Initialize decoder
	base64_init_decoder();

	// Sanity checks
	if (src == NULL || dest == NULL)
	{
		return -1;
	}

	slen = strlen(src);
	if (slen == 0)
	{
		if (size > 0)
		{
			dest[0] = 0;
			return 0;
		}
		return -1;
	}

	// Remove trailing pad characters.
	for (padlen = 0; src[slen - padlen - 1] == '='; ++padlen);
	if (padlen > 2)
	{
		slen -= padlen - 2;
	}
	if (slen % 4)
	{
		return -1;
	}

	dlen = base64_decsize(src);

	// Check buffer size
	if (dlen + 1 > size)
	{
		return -1;
	}

	for (i = 0, j = 0; i < slen;)
	{
		a = decoder[(int) src[i++]];
		b = decoder[(int) src[i++]];
		c = decoder[(int) src[i++]];
		d = decoder[(int) src[i++]];

		// Sextet 3 and 4 may be zero at the end
		if (i == slen)
		{
			if (src[slen - 1] == '=')
			{
				d = 0;
				if (src[slen - 2] == '=')
				{
					c = 0;
				}
			}
		}

		// Non-Base64 char
		if (a == -1 || b == -1 || c == -1 || d == -1)
		{
			return -1;
		}

		triple = (a << 18) + (b << 12) + (c << 6) + d;

		dest[j++] = (triple >> 16) & 0xFF;
		dest[j++] = (triple >> 8) & 0xFF;
		dest[j++] = triple & 0xFF;
	}

	// Always add \0
	dest[j] = 0;

	return dlen;
}

unsigned char *
base64_dec_malloc(char *src)
{
	int size;
	unsigned char *buffer;

	size = base64_decsize(src) + 1;

	buffer = (unsigned char *) malloc(size);
	if (buffer == NULL)
	{
		return NULL;
	}

	size = base64_decode(buffer, size, src);
	if (size == -1)
	{
		free(buffer);
		return NULL;
	}

	return buffer;
}

static var_t *
base64_enc_symbol(int argc, void **argv)
{
	char *buffer;

	if (argv[0] == NULL)
	{
		return EXP_EMPTY;
	}

	buffer = base64_enc_malloc((unsigned char *) argv[0], strlen(argv[0]));

	return var_create(VT_TEXT, NULL, buffer, VF_EXP_FREE);
}


static var_t *
base64_dec_symbol(int argc, void **argv)
{
	char *buffer;

	if (argv[0] == NULL)
	{
		return EXP_EMPTY;
	}

	buffer = (char *) base64_dec_malloc(argv[0]);

	return var_create(VT_TEXT, NULL, buffer, VF_EXP_FREE);
}


int
base64_init(void)
{
	acl_function_register("base64", AF_SIMPLE,
	    (acl_function_callback_t) base64_enc_symbol, VT_STRING, 0);
	acl_function_register("base64_decode", AF_SIMPLE,
	    (acl_function_callback_t) base64_dec_symbol, VT_STRING, 0);

	return 0;
}
