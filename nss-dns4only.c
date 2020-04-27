/* nss-dns4only - disabling AAAA lookup in AF_UNSPEC getaddrinfo()
 * License LGPLv3+; Copyright (C) 2020 Walter Doekes, OSSO B.V. */
#ifdef DEBUG
#include <assert.h>
#endif
#include <errno.h>
#include <netdb.h>
#include <nss.h>
#include <sys/types.h>

#ifdef AS_DNS4SUFFIX
#define _nss_MODULE_gethostbyname4_r _nss_dns4suffix_gethostbyname4_r
#else
#define _nss_MODULE_gethostbyname4_r _nss_dns4only_gethostbyname4_r
#endif

enum nss_status _nss_dns_gethostbyname4_r(
        const char *name, struct gaih_addrtuple **pat,
        char *buffer, size_t buflen, int *errnop,
        int *herrnop, int32_t *ttlp);

enum nss_status _nss_dns_gethostbyname3_r(
        const char *name, int af, struct hostent *result,
        char *buffer, size_t buflen, int *errnop,
        int *herrnop, int32_t *ttlp, char **canonp);

/* static libc equivalents */
/* #define ERANGE 34 Math result not representable */
#define NULL ((void *const)0)
static size_t static_strlen(const char *s) {
    size_t ret = 0;
    while (*s++ != '\0') {
        ++ret;
    }
    return ret;
}
static void* static_memcpy(void *dest, const void *src, size_t len) {
    const char *s = src;
    char *d = dest;
    while (len--) {
        *d++ = *s++;
    }
    return dest;
}
#ifdef AS_DNS4SUFFIX
static int static_memcmp(const void *s1, const void *s2, size_t len) {
    const unsigned char *a = s1;
    const unsigned char *b = s2;
    while (len--) {
        if (*a == *b) {
            a++;
            b++;
        } else if (*a < *b) {
            return -1;
        } else {
            return 1;
        }
    }
    return 0;
}
#endif

/* _our_ export */
enum nss_status _nss_MODULE_gethostbyname4_r(
        const char *name, struct gaih_addrtuple **pat,
        char *buffer, size_t buflen, int *errnop,
        int *herrnop, int32_t *ttlp)
{
    int ret;
    size_t sz;

#ifdef DEBUG
    assert(name);
    assert(pat);
    assert(buffer);
    assert(buflen);
#endif

#ifdef AS_DNS4SUFFIX
    sz = static_strlen(name);
    if (sz > 4 && (!static_memcmp(name + sz - 3, ".v4", 3) ||
                   !static_memcmp(name + sz - 4, ".v4.", 4)))
#endif
    {
        struct hostent result;
        char buffer2[buflen];
#ifdef AS_DNS4SUFFIX
        char lookupname[sz];
        static_memcpy(lookupname, name, sz);
        lookupname[sz - 3] = '\0'; /* drop ".v4" or "v4."; keep period */
#else
        const char *lookupname = name;
#endif

        ret = _nss_dns_gethostbyname3_r(
            lookupname, AF_INET, &result, buffer2, buflen, errnop, herrnop,
            ttlp, NULL);

        if (ret == NSS_STATUS_SUCCESS) {
            struct gaih_addrtuple *pat0, *patp;
            char *bufend = buffer + buflen;
            char *namep;
            char **addrp;

            /* claim first mem for patp linked list */
            pat0 = patp = (struct gaih_addrtuple*)buffer;
            for (addrp = result.h_addr_list; *addrp; ++addrp) {
                if ((void*)(patp + 1) > (void*)bufend) { /* unlikely */
                    goto too_small;
                }
                patp->next = (patp + 1);
                patp->name = NULL;
                patp->family = AF_INET;
                static_memcpy(patp->addr, *addrp, sizeof(result.h_length));
                ++patp;
            }
#ifdef DEBUG
            assert(patp != pat0); /* we did have >0 results */
#endif
            (patp - 1)->next = NULL; /* set last result */

            /* claim mem for namep */
            namep = (char*)patp;
            sz = static_strlen(result.h_name) + 1;
            if ((bufend - namep) < sz) { /* unlikely */
                goto too_small;
            }
            static_memcpy(namep, result.h_name, sz);

            /* go back and set name */
            for (patp = pat0; patp; patp = patp->next) {
                patp->name = namep;
            }

            /* pass result */
            *pat = pat0;
        }
        return ret;
    }
#ifdef AS_DNS4SUFFIX
    else
    {
        return _nss_dns_gethostbyname4_r(
            name, pat, buffer, buflen, errnop, herrnop, ttlp);
    }
#endif

too_small:
    *errnop = ERANGE;
    *herrnop = NETDB_INTERNAL;
    return NSS_STATUS_TRYAGAIN;
}
