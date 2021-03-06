////////////////////////////////////////////////////////////////
//
// JSON5 Parser
//
//
/*
int main(void) {
    zpl_file_contents fc;
    fc = zpl_file_read_contents(zpl_heap(), true, "../data/test.json5");

    zpl_json_object root = {0};

    u8 err;
    zpl_json_parse(&root, fc.size, (char const *)fc.data, zpl_heap_allocator(), true, &err);

    zpl_json_object *replace = NULL;
    isize index = zpl_json_find(&root, "replace_me", false, &replace);

    if (index != -1)
    {
        zpl_printf("Field was found! Current value: %lld\nReplacing with an array!\n", replace->integer);

        replace->type = ZPL_JSON_TYPE_ARRAY;
        zpl_array_init(replace->nodes, replace->backing);

        for (size_t i = 0; i < 5; i++)
        {
            zpl_json_object *o = zpl_json_add(replace, NULL, ZPL_JSON_TYPE_INTEGER);

            if (o) {
                o->integer = (i64)i+1;
            }
        }

        replace->name = "i_am_replaced ";
    }

    zpl_printf("Error code: %d\n", err);

    zpl_json_write(zpl_file_get_standard(ZPL_FILE_STANDARD_OUTPUT), &root, 0);

    zpl_json_free(&root);

    zpl_file_free_contents(&fc);
    return 0;
}
*/

#ifdef ZPL_JSON_DEBUG
#define ZPL_JSON_ASSERT ZPL_ASSERT(0)
#else
#define ZPL_JSON_ASSERT
#endif

typedef enum zpljType {
    ZPL_JSON_TYPE_OBJECT,
    ZPL_JSON_TYPE_STRING,
    ZPL_JSON_TYPE_MULTISTRING,
    ZPL_JSON_TYPE_ARRAY,
    ZPL_JSON_TYPE_INTEGER,
    ZPL_JSON_TYPE_REAL,
    ZPL_JSON_TYPE_CONSTANT
} zpljType;

typedef enum zpljProps {
    ZPL_JSON_PROPS_NONE = 0,
    ZPL_JSON_PROPS_NAN = 1,
    ZPL_JSON_PROPS_NAN_NEG = 2,
    ZPL_JSON_PROPS_INFINITY = 3,
    ZPL_JSON_PROPS_INFINITY_NEG = 4,
    ZPL_JSON_PROPS_IS_EXP = 5,
    ZPL_JSON_PROPS_IS_HEX = 6,
} zpljProps;

typedef enum zpljConst {
    ZPL_JSON_CONST_NULL,
    ZPL_JSON_CONST_FALSE,
    ZPL_JSON_CONST_TRUE,
} zpljConst;

// TODO(ZaKlaus): Error handling
typedef enum zpljError {
    ZPL_JSON_ERROR_NONE,
    ZPL_JSON_ERROR_INVALID_NAME,
    ZPL_JSON_ERROR_INVALID_VALUE,
    ZPL_JSON_ERROR_OBJECT_OR_SOURCE_WAS_NULL,
} zpljError;

typedef enum zpljNameStyle {
    ZPL_JSON_NAME_STYLE_DOUBLE_QUOTE,
    ZPL_JSON_NAME_STYLE_SINGLE_QUOTE,
    ZPL_JSON_NAME_STYLE_NO_QUOTES,
} zpljNameStyle;

typedef enum zpljAssignStyle {
    ZPL_JSON_ASSIGN_STYLE_COLON,
    ZPL_JSON_ASSIGN_STYLE_EQUALS,
    ZPL_JSON_ASSIGN_STYLE_LINE,
} zpljAssignStyle;

typedef enum zpljDelimStyle {
    ZPL_JSON_DELIM_STYLE_COMMA,
    ZPL_JSON_DELIM_STYLE_LINE,
    ZPL_JSON_DELIM_STYLE_NEWLINE,
} zpljDelimStyle;

#define zpl_json_object_t zpl_json_object
typedef struct zpl_json_object {
    zpl_allocator backing;
    char *name;
    u8 type : 6;
    u8 name_style : 2;
    u8 props : 7;
    u8 cfg_mode : 1;
    u8 assign_style : 4;
    u8 delim_style : 4;
    u8 delim_line_width;
    
    union {
        zpl_array(struct zpl_json_object) nodes;
        i64 integer;
        char *string;
        struct {
            f64 real;
            i32 base;
            i32 base2;
            i32 exp;
            u8 exp_neg : 1;
            u8 lead_digit : 1;
        };
        u8 constant;
    };
} zpl_json_object;

ZPL_DEF void zpl_json_parse(zpl_json_object *root, usize len, char const *source, zpl_allocator a, b32 strip_comments,
                            u8 *err_code);
ZPL_DEF void zpl_json_write(zpl_file *f, zpl_json_object *o, isize indent);
ZPL_DEF void zpl_json_free(zpl_json_object *obj);

ZPL_DEF isize zpl_json_find(zpl_json_object *obj, char const *name, b32 deep_search, zpl_json_object **node);
ZPL_DEF void zpl_json_init_node(zpl_json_object *obj, zpl_allocator backing, char const *name, u8 type);
ZPL_DEF zpl_json_object *zpl_json_add_at(zpl_json_object *obj, isize index, char const *name, u8 type);
ZPL_DEF zpl_json_object *zpl_json_add(zpl_json_object *obj, char const *name, u8 type);

ZPL_DEF char *zpl__json_parse_object(zpl_json_object *obj, char *base, zpl_allocator a, u8 *err_code);
ZPL_DEF char *zpl__json_parse_value(zpl_json_object *obj, char *base, zpl_allocator a, u8 *err_code);
ZPL_DEF char *zpl__json_parse_array(zpl_json_object *obj, char *base, zpl_allocator a, u8 *err_code);

#define zpl__trim zpl_str_trim
#define zpl__skip zpl__json_skip
ZPL_DEF char *zpl__json_skip(char *str, char c);
ZPL_DEF b32 zpl__json_validate_name(char *str, char *err);

//!!

////////////////////////////////////////////////////////////////
//
// JSON5 Parser
//
//

b32 zpl__json_is_control_char(char c);
b32 zpl__json_is_assign_char(char c);
b32 zpl__json_is_delim_char(char c);

void zpl_json_parse(zpl_json_object *root, usize len, char const *source, zpl_allocator_t a, b32 strip_comments,
                    u8 *err_code) {
    
    if (!root || !source)
    {
        ZPL_JSON_ASSERT;
        if (err_code) *err_code = ZPL_JSON_ERROR_OBJECT_OR_SOURCE_WAS_NULL;
        return;
    }
    
    zpl_unused(len);

    char *dest = (char *)source;

    if (strip_comments) {
        b32 is_lit = false;
        char lit_c = '\0';
        char *p = dest;
        char *b = dest;
        isize l = 0;

        while (*p) {
            if (!is_lit) {
                if ((*p == '"' || *p == '\'')) {
                    lit_c = *p;
                    is_lit = true;
                    ++p;
                    continue;
                }
            } else {
                if (*p == '\\' && *(p + 1) && *(p + 1) == lit_c) {
                    p += 2;
                    continue;
                } else if (*p == lit_c) {
                    is_lit = false;
                    ++p;
                    continue;
                }
            }

            if (!is_lit) {
                // NOTE(ZaKlaus): block comment
                if (p[0] == '/' && p[1] == '*') {
                    b = p;
                    l = 2;
                    p += 2;

                    while (p[0] != '*' && p[1] != '/') {
                        ++p;
                        ++l;
                    }
                    p += 2;
                    l += 2;
                    zpl_memset(b, ' ', l);
                }

                // NOTE(ZaKlaus): inline comment
                if (p[0] == '/' && p[1] == '/') {
                    b = p;
                    l = 2;
                    p += 2;

                    while (p[0] != '\n') {
                        ++p;
                        ++l;
                    }
                    ++l;
                    zpl_memset(b, ' ', l);
                }
            }

            ++p;
        }
    }

    if (err_code) *err_code = ZPL_JSON_ERROR_NONE;
    zpl_json_object root_ = { 0 };

    dest = zpl_str_trim(dest, false);

    if (*dest != '{' || *dest != '[') { root_.cfg_mode = true; }

    zpl__json_parse_object(&root_, dest, a, err_code);

    *root = root_;
}

#define zpl___ind(x)                                                                                                   \
    for (int i = 0; i < x; ++i) zpl_fprintf(f, " ");

void zpl__json_write_value(zpl_file *f, zpl_json_object_t *o, zpl_json_object *t, isize indent, b32 is_inline, b32 is_last);

void zpl_json_write(zpl_file *f, zpl_json_object *o, isize indent) {
    if (!o)
        return;
    
    zpl___ind(indent - 4);
    if (!o->cfg_mode)
        zpl_fprintf(f, "{\n");
    else {
        indent -= 4;
    }

    if (o->nodes)
    {
        isize cnt = zpl_array_count(o->nodes);

        for (int i = 0; i < cnt; ++i) {
            if (i < cnt - 1) {
                zpl__json_write_value(f, o->nodes + i, o, indent, false, false);
            } else {
                zpl__json_write_value(f, o->nodes + i, o, indent, false, true);
            }
        }
    }

    zpl___ind(indent);

    if (indent > 0) {
        zpl_fprintf(f, "}");
    } else {
        if (!o->cfg_mode) zpl_fprintf(f, "}\n");
    }
}

void zpl__json_write_value(zpl_file *f, zpl_json_object_t *o, zpl_json_object *t, isize indent, b32 is_inline, b32 is_last) {
    zpl_json_object_t *node = o;
    indent += 4;

    if (!is_inline) {
        zpl___ind(indent);
        switch (node->name_style) {
            case ZPL_JSON_NAME_STYLE_DOUBLE_QUOTE: {
                zpl_fprintf(f, "\"%s\"", node->name);
            } break;

            case ZPL_JSON_NAME_STYLE_SINGLE_QUOTE: {
                zpl_fprintf(f, "\'%s\'", node->name);
            } break;

            case ZPL_JSON_NAME_STYLE_NO_QUOTES: {
                zpl_fprintf(f, "%s", node->name);
            } break;
        }

        if (o->assign_style == ZPL_JSON_ASSIGN_STYLE_COLON)
            zpl_fprintf(f, ": ");
        else {
            if (o->name_style != ZPL_JSON_NAME_STYLE_NO_QUOTES)
                zpl___ind(1);

            if (o->assign_style == ZPL_JSON_ASSIGN_STYLE_EQUALS)
                zpl_fprintf(f, "= ");
            else if (o->assign_style == ZPL_JSON_ASSIGN_STYLE_LINE)
                zpl_fprintf(f, "| ");    
        }
        
    }

    switch (node->type) {
    case ZPL_JSON_TYPE_STRING: {
        zpl_fprintf(f, "\"%s\"", node->string);
    } break;

    case ZPL_JSON_TYPE_MULTISTRING: {
        zpl_fprintf(f, "`%s`", node->string);
    } break;

    case ZPL_JSON_TYPE_ARRAY: {
        zpl_fprintf(f, "[");
        isize elemn = zpl_array_count(node->nodes);
        for (int j = 0; j < elemn; ++j) {
            zpl__json_write_value(f, node->nodes + j, o, -4, true, true);

            if (j < elemn - 1) { zpl_fprintf(f, ", "); }
        }
        zpl_fprintf(f, "]");
    } break;

    case ZPL_JSON_TYPE_INTEGER: {
        if (node->props == ZPL_JSON_PROPS_IS_HEX) {
            zpl_fprintf(f, "0x%llx", (long long)node->integer);
        } else {
            zpl_fprintf(f, "%lld", (long long)node->integer);
        }
    } break;

    case ZPL_JSON_TYPE_REAL: {
        if (node->props == ZPL_JSON_PROPS_NAN) {
            zpl_fprintf(f, "NaN");
        } else if (node->props == ZPL_JSON_PROPS_NAN_NEG) {
            zpl_fprintf(f, "-NaN");
        } else if (node->props == ZPL_JSON_PROPS_INFINITY) {
            zpl_fprintf(f, "Infinity");
        } else if (node->props == ZPL_JSON_PROPS_INFINITY_NEG) {
            zpl_fprintf(f, "-Infinity");
        } else if (node->props == ZPL_JSON_PROPS_IS_EXP) {
            zpl_fprintf(f, "%lld.%llde%c%lld", (long long)node->base, (long long)node->base2, node->exp_neg ? '-' : '+',
                        (long long)node->exp);
        } else {
            if (!node->lead_digit)
                zpl_fprintf(f, ".%lld", (long long)node->base2);
            else
                zpl_fprintf(f, "%lld.%lld", (long long)node->base, (long long)node->base2);
        }
    } break;

    case ZPL_JSON_TYPE_OBJECT: {
        zpl_json_write(f, node, indent);
    } break;

    case ZPL_JSON_TYPE_CONSTANT: {
        if (node->constant == ZPL_JSON_CONST_TRUE) {
            zpl_fprintf(f, "true");
        } else if (node->constant == ZPL_JSON_CONST_FALSE) {
            zpl_fprintf(f, "false");
        } else if (node->constant == ZPL_JSON_CONST_NULL) {
            zpl_fprintf(f, "null");
        }
    } break;
    }

    if (!is_inline) {

        if (o->delim_style != ZPL_JSON_DELIM_STYLE_COMMA)
        {
            if (o->delim_style == ZPL_JSON_DELIM_STYLE_NEWLINE)
                zpl_fprintf(f, "\n");
            else if (o->delim_style == ZPL_JSON_DELIM_STYLE_LINE)
            {
                zpl___ind(o->delim_line_width);
                zpl_fprintf(f, "|\n");
            }
        }
        else
        {
            if (!is_last) {
                zpl_fprintf(f, ",\n");
            } else {
                zpl_fprintf(f, "\n");
            }
        }
    }
}
#undef zpl___ind

void zpl_json_free(zpl_json_object *obj) {
    if ((obj->type == ZPL_JSON_TYPE_OBJECT || obj->type == ZPL_JSON_TYPE_ARRAY) && obj->nodes) {
        for (isize i = 0; i < zpl_array_count(obj->nodes); ++i) { zpl_json_free(obj->nodes + i); }

        zpl_array_free(obj->nodes);
    }
}

char *zpl__json_parse_array(zpl_json_object *obj, char *base, zpl_allocator_t a, u8 *err_code) {
    ZPL_ASSERT(obj && base);
    char *p = base;

    obj->type = ZPL_JSON_TYPE_ARRAY;
    zpl_array_init(obj->nodes, a);
    obj->backing = a;

    while (*p) {
        p = zpl_str_trim(p, false);

        zpl_json_object elem = { 0 };
        elem.backing = a;
        p = zpl__json_parse_value(&elem, p, a, err_code);

        if (err_code && *err_code != ZPL_JSON_ERROR_NONE) { return NULL; }

        zpl_array_append(obj->nodes, elem);

        p = zpl_str_trim(p, false);

        if (*p == ',') {
            ++p;
            continue;
        } else {
            return p;
        }
    }
    return p;
}

char *zpl__json_parse_value(zpl_json_object *obj, char *base, zpl_allocator_t a, u8 *err_code) {
    ZPL_ASSERT(obj && base);
    char *p = base;
    char *b = base;
    char *e = base;

    if (*p == '"' || *p == '\'') {
        char c = *p;
        obj->type = ZPL_JSON_TYPE_STRING;
        b = p + 1;
        e = b;
        obj->string = b;

        while (*e) {
            if (*e == '\\' && *(e + 1) == c) {
                e += 2;
                continue;
            } else if (*e == '\\' && (*(e + 1) == '\r' || *(e + 1) == '\n')) {
                *e = ' ';
                e++;
                continue;
            } else if (*e == c) {
                break;
            }
            ++e;
        }

        *e = '\0';
        p = e + 1;
    } else if (*p == '`') {
        obj->type = ZPL_JSON_TYPE_MULTISTRING;
        b = p + 1;
        e = b;
        obj->string = b;

        while (*e) {
            if (*e == '\\' && *(e + 1) == '`') {
                e += 2;
                continue;
            } else if (*e == '`') {
                break;
            }
            ++e;
        }

        *e = '\0';
        p = e + 1;
    } else if (zpl_char_is_alpha(*p) || (*p == '-' && !zpl_char_is_digit(*(p + 1)))) {
        obj->type = ZPL_JSON_TYPE_CONSTANT;

        if (!zpl_strncmp(p, "true", 4)) {
            obj->constant = ZPL_JSON_CONST_TRUE;
            p += 4;
        } else if (!zpl_strncmp(p, "false", 5)) {
            obj->constant = ZPL_JSON_CONST_FALSE;
            p += 5;
        } else if (!zpl_strncmp(p, "null", 4)) {
            obj->constant = ZPL_JSON_CONST_NULL;
            p += 4;
        } else if (!zpl_strncmp(p, "Infinity", 8)) {
            obj->type = ZPL_JSON_TYPE_REAL;
            obj->real = INFINITY;
            obj->props = ZPL_JSON_PROPS_INFINITY;
            p += 8;
        } else if (!zpl_strncmp(p, "-Infinity", 9)) {
            obj->type = ZPL_JSON_TYPE_REAL;
            obj->real = -INFINITY;
            obj->props = ZPL_JSON_PROPS_INFINITY_NEG;
            p += 9;
        } else if (!zpl_strncmp(p, "NaN", 3)) {
            obj->type = ZPL_JSON_TYPE_REAL;
            obj->real = NAN;
            obj->props = ZPL_JSON_PROPS_NAN;
            p += 3;
        } else if (!zpl_strncmp(p, "-NaN", 4)) {
            obj->type = ZPL_JSON_TYPE_REAL;
            obj->real = -NAN;
            obj->props = ZPL_JSON_PROPS_NAN_NEG;
            p += 4;
        } else {
            ZPL_JSON_ASSERT;
            if (err_code) *err_code = ZPL_JSON_ERROR_INVALID_VALUE;
            return NULL;
        }
    } else if (zpl_char_is_digit(*p) || *p == '+' || *p == '-' || *p == '.') {
        obj->type = ZPL_JSON_TYPE_INTEGER;

        b = p;
        e = b;

        isize ib = 0;
        char buf[16] = { 0 };

        if (*e == '+')
            ++e;
        else if (*e == '-') {
            buf[ib++] = *e++;
        }

        if (*e == '.') {
            obj->type = ZPL_JSON_TYPE_REAL;
            buf[ib++] = '0';
            obj->lead_digit = false;

            do {
                buf[ib++] = *e;
            } while (zpl_char_is_digit(*++e));
        } else {
            if (*e == '0' && (*(e + 1) == 'x' || *(e + 1) == 'X')) { obj->props = ZPL_JSON_PROPS_IS_HEX; }
            while (zpl_char_is_hex_digit(*e) || *e == 'x' || *e == 'X') { buf[ib++] = *e++; }

            if (*e == '.') {
                obj->type = ZPL_JSON_TYPE_REAL;
                obj->lead_digit = true;
                u32 step = 0;

                do {
                    buf[ib++] = *e;
                    ++step;
                } while (zpl_char_is_digit(*++e));

                if (step < 2) { buf[ib++] = '0'; }
            }
        }

        i32 exp = 0;
        f32 eb = 10;
        char expbuf[6] = { 0 };
        isize expi = 0;

        if (*e == 'e' || *e == 'E') {
            ++e;
            if (*e == '+' || *e == '-' || zpl_char_is_digit(*e)) {
                if (*e == '-') { eb = 0.1f; }

                if (!zpl_char_is_digit(*e)) { ++e; }

                while (zpl_char_is_digit(*e)) { expbuf[expi++] = *e++; }
            }

            exp = (i32)zpl_str_to_i64(expbuf, NULL, 10);
        }

        if (*e == '\0') {
            ZPL_JSON_ASSERT;
            if (err_code) *err_code = ZPL_JSON_ERROR_INVALID_VALUE;
        }

        if (obj->type == ZPL_JSON_TYPE_INTEGER) {
            obj->integer = zpl_str_to_i64(buf, 0, 0);

            while (exp-- > 0) { obj->integer *= (i64)eb; }
        } else {
            obj->real = zpl_str_to_f64(buf, 0);

            char *q = buf, *qp = q, *qp2 = q;
            while (*qp != '.') ++qp;
            *qp = '\0';
            qp2 = qp + 1;

            obj->base = (i32)zpl_str_to_i64(q, 0, 0);
            obj->base2 = (i32)zpl_str_to_i64(qp2, 0, 0);

            if (exp) {
                obj->exp = exp;
                obj->exp_neg = !(eb == 10.f);
                obj->props = ZPL_JSON_PROPS_IS_EXP;
            }

            while (exp-- > 0) { obj->real *= eb; }
        }
        p = e;
    } else if (*p == '[') {
        p = zpl_str_trim(p + 1, false);
        if (*p == ']') return p;
        p = zpl__json_parse_array(obj, p, a, err_code);

        if (err_code && *err_code != ZPL_JSON_ERROR_NONE) { return NULL; }

        ++p;
    } else if (*p == '{') {
        p = zpl_str_trim(p + 1, false);
        p = zpl__json_parse_object(obj, p, a, err_code);

        if (err_code && *err_code != ZPL_JSON_ERROR_NONE) { return NULL; }

        ++p;
    }

    return p;
}

char *zpl__json_parse_object(zpl_json_object *obj, char *base, zpl_allocator_t a, u8 *err_code) {
    ZPL_ASSERT(obj && base);
    char *p = base;
    char *b = base;
    char *e = base;

    zpl_array_init(obj->nodes, a);
    obj->backing = a;

    p = zpl_str_trim(p, false);
    if (*p == '{') { ++p; }

    while (*p) {
        zpl_json_object node = { 0 };
        p = zpl_str_trim(p, false);
        if (*p == '}') return p;

        if (*p == '"' || *p == '\'') {
            if (*p == '"') {
                node.name_style = ZPL_JSON_NAME_STYLE_DOUBLE_QUOTE;
            } else {
                node.name_style = ZPL_JSON_NAME_STYLE_SINGLE_QUOTE;
            }

            char c = *p;
            b = ++p;
            e = zpl__json_skip(b, c);
            node.name = b;
            *e = '\0';

            p = ++e;
            p = zpl_str_trim(p, false);

            if (*p && !zpl__json_is_assign_char(*p)) {
                ZPL_JSON_ASSERT;
                if (err_code) *err_code = ZPL_JSON_ERROR_INVALID_NAME;
                return NULL;
            }
        } else {
            if (*p == '[') {
                if (node.name) *node.name = '\0';
                p = zpl__json_parse_value(&node, p, a, err_code);
                goto l_parsed;
            } else if (zpl_char_is_alpha(*p) || *p == '_' || *p == '$') {
                b = p;
                e = b;

                do {
                    ++e;
                } while (*e && (zpl_char_is_alphanumeric(*e) || *e == '_') && !zpl_char_is_space(*e) && !zpl__json_is_assign_char(*e));

                if (zpl__json_is_assign_char(*e)) {
                    p = e;

                    if (*e == '=')
                        node.assign_style = ZPL_JSON_ASSIGN_STYLE_EQUALS;
                    else if (*e == '|')
                        node.assign_style = ZPL_JSON_ASSIGN_STYLE_LINE;

                } else {
                    while (*e) {
                        if (*e && (!zpl_char_is_space(*e) || zpl__json_is_assign_char(*e))) 
                        { 
                            if (*e == '=')
                                node.assign_style = ZPL_JSON_ASSIGN_STYLE_EQUALS;
                            else if (*e == '|')
                                node.assign_style = ZPL_JSON_ASSIGN_STYLE_LINE;

                            break; 
                        }
                        ++e;
                    }
                    e = zpl_str_trim(e, false);
                    p = e;

                    if (*p && !zpl__json_is_assign_char(*p)) {
                        ZPL_JSON_ASSERT;
                        if (err_code) *err_code = ZPL_JSON_ERROR_INVALID_NAME;
                        return NULL;
                    }
                    else
                    {
                        if (*p == '=')
                            node.assign_style = ZPL_JSON_ASSIGN_STYLE_EQUALS;
                        else if (*p == '|')
                            node.assign_style = ZPL_JSON_ASSIGN_STYLE_LINE;
                    }
                }

                *e = '\0';
                node.name = b;
                node.name_style = ZPL_JSON_NAME_STYLE_NO_QUOTES;
            }
        }

        char errc;
        if (!zpl__json_validate_name(node.name, &errc)) {
            ZPL_JSON_ASSERT;
            if (err_code) *err_code = ZPL_JSON_ERROR_INVALID_NAME;
            return NULL;
        }

        p = zpl_str_trim(p + 1, false);
        p = zpl__json_parse_value(&node, p, a, err_code);
        node.backing = obj->backing;

        if (err_code && *err_code != ZPL_JSON_ERROR_NONE) { return NULL; }

    l_parsed:

        zpl_array_append(obj->nodes, node);

        char *wp = p;
        p = zpl_str_trim(p, true);
        u8 wl = cast(u8)(p-wp);

        if (zpl__json_is_delim_char(*p)) {
            zpl_json_object *n = zpl_array_end(obj->nodes);

            if (*p == '\n')
                n->delim_style = ZPL_JSON_DELIM_STYLE_NEWLINE;
            else if (*p == '|') {
                n->delim_style = ZPL_JSON_DELIM_STYLE_LINE;
                n->delim_line_width = wl;
            }

            p = zpl_str_trim(p + 1, false);
            if (*p == '\0' || *p == '}')
                return p;
            else
                continue;
        } else if (*p == '\0' || *p == '}') {
            return p;
        } else {
            ZPL_JSON_ASSERT;
            if (err_code) *err_code = ZPL_JSON_ERROR_INVALID_VALUE;
            return NULL;
        }
    }
    return p;
}

isize zpl_json_find(zpl_json_object *obj, char const *name, b32 deep_search, zpl_json_object **node)
{
    if (obj->type != ZPL_JSON_TYPE_OBJECT)
    {
        if (node) *node = NULL;
        return -1;
    }

    for (isize i = 0; i < zpl_array_count(obj->nodes); i++)
    {
        if (!zpl_strncmp(obj->nodes[i].name, name, zpl_strlen(name)))
        {
            if (node) *node = obj->nodes + i;
            return i;
        }
    }
    
    if (deep_search)
    {
        for (isize i = 0; i < zpl_array_count(obj->nodes); i++)
        {
            isize res = zpl_json_find(obj->nodes + i, name, deep_search, node);

            if (res != -1)
                return res;
        }
    }
    
    if (node) *node = NULL;
    return -1;
}

void zpl_json_init_node(zpl_json_object *obj, zpl_allocator backing, char const *name, u8 type)
{
    obj->name = (char *)name;
    obj->type = type;
    obj->backing = backing;

    if (type == ZPL_JSON_TYPE_ARRAY || type == ZPL_JSON_TYPE_OBJECT)
    {
        zpl_array_init(obj->nodes, backing);
    }
}

zpl_json_object *zpl_json_add_at(zpl_json_object *obj, isize index, char const *name, u8 type)
{
    if (!obj || (obj->type != ZPL_JSON_TYPE_OBJECT && obj->type != ZPL_JSON_TYPE_ARRAY))
    {
        return NULL;
    }

    if (!obj->nodes)
        return NULL;

    if (index < 0 || index > zpl_array_count(obj->nodes))
        return NULL;

    zpl_json_object o = {0};
    zpl_json_init_node(&o, obj->backing, name, type);
    
    zpl_array_append_at(obj->nodes, o, index);

    return obj->nodes + index;
}

zpl_json_object *zpl_json_add(zpl_json_object *obj, char const *name, u8 type)
{
    if (!obj || (obj->type != ZPL_JSON_TYPE_OBJECT && obj->type != ZPL_JSON_TYPE_ARRAY))
    {
        return NULL;
    }

    if (!obj->nodes)
        return NULL;

    return zpl_json_add_at(obj, zpl_array_count(obj->nodes), name, type);
}

zpl_inline b32 zpl__json_is_control_char(char c) {
    return !!zpl_strchr("\"\\/bfnrt", c);
}

zpl_inline b32 zpl__json_is_special_char(char c) { return !!zpl_strchr("<>:/", c); }
zpl_inline b32 zpl__json_is_assign_char(char c) { return !!zpl_strchr(":=|", c); }
zpl_inline b32 zpl__json_is_delim_char(char c) { return !!zpl_strchr(",|\n", c); }

#define jx(x) !zpl_char_is_hex_digit(str[x])
zpl_inline b32 zpl__json_validate_name(char *str, char *err) {
    while (*str) {
        if ((str[0] == '\\' && !zpl__json_is_control_char(str[1])) &&
            (str[0] == '\\' && jx(1) && jx(2) && jx(3) && jx(4))) {
            *err = *str;
            return false;
        }

        ++str;
    }

    return true;
}
#undef jx

zpl_inline char *zpl__json_skip(char *str, char c) {
    while ((*str && *str != c) || (*(str - 1) == '\\' && *str == c && zpl__json_is_control_char(c))) { ++str; }

    return str;
}
