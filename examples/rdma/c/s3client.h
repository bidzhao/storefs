#ifndef S3CLIENT_H
#define S3CLIENT_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const char *endpoint;    /* e.g. "http://127.0.0.1:8901" or "http://{bucket}.127.0.0.1:8901" */
    const char *region;      /* e.g. "us-east-1" */
    const char *access_key;
    const char *secret_key;
} s3_config_t;

/* HTTP HEAD on bucket/key, returns Content-Length in *size_out. */
int s3_head_object(const s3_config_t *cfg, const char *bucket, const char *key,
                   long long *size_out, char *err, size_t errlen);

/* HTTP GET on bucket/key, body is discarded. Extra header can be added post-signing. */
int s3_get_object(const s3_config_t *cfg, const char *bucket, const char *key,
                  const char *extra_header_name, const char *extra_header_value,
                  char *err, size_t errlen);

/* HTTP PUT on bucket/key with empty body. Extra header can be added post-signing. */
int s3_put_object(const s3_config_t *cfg, const char *bucket, const char *key,
                  const char *extra_header_name, const char *extra_header_value,
                  char *err, size_t errlen);

#ifdef __cplusplus
}
#endif
#endif /* S3CLIENT_H */