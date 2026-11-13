/*
 * A small JSON reader (glTF, the answers of the image-to-3D services):
 * the text becomes a tree of nodes; strings are unescaped, numbers are
 * doubles. Plain C, kernel and PC.
 */
#ifndef JSON_H
#define JSON_H

#include <stddef.h>

typedef enum { JSON_NULL, JSON_BOOL, JSON_NUMBER, JSON_STRING, JSON_ARRAY, JSON_OBJECT } json_type_t;

typedef struct json json_t;
struct json {
    json_type_t type;
    char *key;                  /* in an object: the member's name */
    char *str;                  /* JSON_STRING */
    double num;                 /* JSON_NUMBER, JSON_BOOL (0 or 1) */
    json_t *child, *next;       /* members / elements, and the next sibling */
    int count;                  /* members / elements */
};

/* The tree, or NULL (err says where it broke). json_free() it. */
json_t *json_parse(const char *text, size_t len, char *err, size_t errlen);
void json_free(json_t *j);

/* NULL when the member / element is not there. */
const json_t *json_get(const json_t *obj, const char *key);
const json_t *json_at(const json_t *arr, int i);
/* With a default when the member is missing or not of that type. */
double json_num(const json_t *obj, const char *key, double dflt);
const char *json_str(const json_t *obj, const char *key, const char *dflt);

#endif
