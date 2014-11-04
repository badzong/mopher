#ifndef _VP_H_
#define _VP_H_

#define VP_FIELD_LIMIT (sizeof (vp_null_t) * 8)

typedef unsigned long long vp_null_t;

typedef struct vp {
	void			*vp_data;
	int			 vp_dlen;
	void			*vp_key;
	int			 vp_klen;
	vp_null_t		*vp_null_fields;
	void			*vp_fields;
} vp_t;

/*
 * Prototypes
 */
void vp_init(vp_t *vp, void *key, int klen, void *data, int dlen);
void vp_delete(vp_t *vp);
vp_t * vp_pack(var_t *v);
var_t * vp_unpack(vp_t *vp, var_t *scheme);
void vp_test(int n);

#endif /* _VP_H_ */
