/* Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to You under the Apache License, Version 2.0
 * (the "License"); you may not use this file except in compliance with
 * the License.  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/*
 * util_ldap.c: LDAP things
 * 
 * Original code from auth_ldap module for Apache v1.3:
 * Copyright 1998, 1999 Enbridge Pipelines Inc. 
 * Copyright 1999-2001 Dave Carrigan
 */

#include <apr_ldap.h>
#include <apr_strings.h>

#include "ap_config.h"
#include "httpd.h"
#include "http_config.h"
#include "http_core.h"
#include "http_log.h"
#include "http_protocol.h"
#include "http_request.h"
#include "util_ldap.h"
#include "util_ldap_cache.h"

#if APR_HAVE_UNISTD_H
#include <unistd.h>
#endif

#ifndef APU_HAS_LDAP
#error mod_ldap requires APR-util to have LDAP support built in
#endif

#if !defined(OS2) && !defined(WIN32) && !defined(BEOS) && !defined(NETWARE)
#include "unixd.h"
#define UTIL_LDAP_SET_MUTEX_PERMS
#endif

    /* defines for certificate file types
    */
#define LDAP_CA_TYPE_UNKNOWN            0
#define LDAP_CA_TYPE_DER                1
#define LDAP_CA_TYPE_BASE64             2
#define LDAP_CA_TYPE_CERT7_DB           3


module AP_MODULE_DECLARE_DATA ldap_module;

int util_ldap_handler(request_rec *r);
void *util_ldap_create_config(apr_pool_t *p, server_rec *s);


/*
 * Some definitions to help between various versions of apache.
 */

#ifndef DOCTYPE_HTML_2_0
#define DOCTYPE_HTML_2_0  "<!DOCTYPE HTML PUBLIC \"-//IETF//" \
                          "DTD HTML 2.0//EN\">\n"
#endif

#ifndef DOCTYPE_HTML_3_2
#define DOCTYPE_HTML_3_2  "<!DOCTYPE HTML PUBLIC \"-//W3C//" \
                          "DTD HTML 3.2 Final//EN\">\n"
#endif

#ifndef DOCTYPE_HTML_4_0S
#define DOCTYPE_HTML_4_0S "<!DOCTYPE HTML PUBLIC \"-//W3C//" \
                          "DTD HTML 4.0//EN\"\n" \
                          "\"http://www.w3.org/TR/REC-html40/strict.dtd\">\n"
#endif

#ifndef DOCTYPE_HTML_4_0T
#define DOCTYPE_HTML_4_0T "<!DOCTYPE HTML PUBLIC \"-//W3C//" \
                          "DTD HTML 4.0 Transitional//EN\"\n" \
                          "\"http://www.w3.org/TR/REC-html40/loose.dtd\">\n"
#endif

#ifndef DOCTYPE_HTML_4_0F
#define DOCTYPE_HTML_4_0F "<!DOCTYPE HTML PUBLIC \"-//W3C//" \
                          "DTD HTML 4.0 Frameset//EN\"\n" \
                          "\"http://www.w3.org/TR/REC-html40/frameset.dtd\">\n"
#endif

#define LDAP_CACHE_LOCK() \
    if (st->util_ldap_cache_lock) \
      apr_global_mutex_lock(st->util_ldap_cache_lock)
#define LDAP_CACHE_UNLOCK() \
    if (st->util_ldap_cache_lock) \
      apr_global_mutex_unlock(st->util_ldap_cache_lock)


static void util_ldap_strdup (char **str, const char *newstr)
{
    if (*str) {
        free(*str);
        *str = NULL;
    }

    if (newstr) {
        *str = calloc(1, strlen(newstr)+1);
        strcpy (*str, newstr);
    }
}

/*
 * Status Handler
 * --------------
 *
 * This handler generates a status page about the current performance of
 * the LDAP cache. It is enabled as follows:
 *
 * <Location /ldap-status>
 *   SetHandler ldap-status
 * </Location>
 *
 */
int util_ldap_handler(request_rec *r)
{
    util_ldap_state_t *st = (util_ldap_state_t *)ap_get_module_config(r->server->module_config, &ldap_module);

    r->allowed |= (1 << M_GET);
    if (r->method_number != M_GET)
        return DECLINED;

    if (strcmp(r->handler, "ldap-status")) {
        return DECLINED;
    }

    r->content_type = "text/html; charset=ISO-8859-1";
    if (r->header_only)
        return OK;

    ap_rputs(DOCTYPE_HTML_3_2
             "<html><head><title>LDAP Cache Information</title></head>\n", r);
    ap_rputs("<body bgcolor='#ffffff'><h1 align=center>LDAP Cache Information</h1>\n", r);

    util_ald_cache_display(r, st);

    return OK;
}

/* ------------------------------------------------------------------ */


/*
 * Closes an LDAP connection by unlocking it. The next time
 * util_ldap_connection_find() is called this connection will be
 * available for reuse.
 */
LDAP_DECLARE(void) util_ldap_connection_close(util_ldap_connection_t *ldc)
{

    /*
     * QUESTION:
     *
     * Is it safe leaving bound connections floating around between the
     * different modules? Keeping the user bound is a performance boost,
     * but it is also a potential security problem - maybe.
     *
     * For now we unbind the user when we finish with a connection, but
     * we don't have to...
     */

    /* mark our connection as available for reuse */

#if APR_HAS_THREADS
    apr_thread_mutex_unlock(ldc->lock);
#endif
}


/*
 * Destroys an LDAP connection by unbinding and closing the connection to
 * the LDAP server. It is used to bring the connection back to a known
 * state after an error, and during pool cleanup.
 */
LDAP_DECLARE_NONSTD(apr_status_t) util_ldap_connection_unbind(void *param)
{
    util_ldap_connection_t *ldc = param;

    if (ldc) {
        if (ldc->ldap) {
            ldap_unbind_s(ldc->ldap);
            ldc->ldap = NULL;
        }
        ldc->bound = 0;
    }

    return APR_SUCCESS;
}


/*
 * Clean up an LDAP connection by unbinding and unlocking the connection.
 * This function is registered with the pool cleanup function - causing
 * the LDAP connections to be shut down cleanly on graceful restart.
 */
LDAP_DECLARE_NONSTD(apr_status_t) util_ldap_connection_cleanup(void *param)
{
    util_ldap_connection_t *ldc = param;

    if (ldc) {

        /* unbind and disconnect from the LDAP server */
        util_ldap_connection_unbind(ldc);

        /* free the username and password */
        if (ldc->bindpw) {
            free((void*)ldc->bindpw);
        }
        if (ldc->binddn) {
            free((void*)ldc->binddn);
        }

        /* unlock this entry */
        util_ldap_connection_close(ldc);
    
    }

    return APR_SUCCESS;
}


/*
 * Connect to the LDAP server and binds. Does not connect if already
 * connected (i.e. ldc->ldap is non-NULL.) Does not bind if already bound.
 *
 * Returns LDAP_SUCCESS on success; and an error code on failure
 */
LDAP_DECLARE(int) util_ldap_connection_open(request_rec *r, 
                                            util_ldap_connection_t *ldc)
{
    int result = 0;
    int failures = 0;
    int version  = LDAP_VERSION3;
    int rc = LDAP_SUCCESS;
    struct timeval timeOut = {10,0};    /* 10 second connection timeout */

    util_ldap_state_t *st = (util_ldap_state_t *)ap_get_module_config(
                                r->server->module_config, &ldap_module);

    /* If the connection is already bound, return
    */
    if (ldc->bound)
    {
        ldc->reason = "LDAP: connection open successful (already bound)";
        return LDAP_SUCCESS;
    }

    /* create the ldap session handle
    */
    if (NULL == ldc->ldap)
    {
            /* clear connection requested */
        if (!ldc->secure)
        {
            ldc->ldap = ldap_init(const_cast(ldc->host), ldc->port);
        }
        else /* ssl connnection requested */
        {
                /* check configuration to make sure it supports SSL
                */
            if (st->ssl_support)
            {
                #if APR_HAS_LDAP_SSL
                
                #if APR_HAS_NOVELL_LDAPSDK 
                ldc->ldap = ldapssl_init(ldc->host, ldc->port, 1);

                #elif APR_HAS_NETSCAPE_LDAPSDK
                ldc->ldap = ldapssl_init(ldc->host, ldc->port, 1);

                #elif APR_HAS_OPENLDAP_LDAPSDK
                ldc->ldap = ldap_init(ldc->host, ldc->port);
                if (NULL != ldc->ldap)
                {
                    int SSLmode = LDAP_OPT_X_TLS_HARD;
                    result = ldap_set_option(ldc->ldap, LDAP_OPT_X_TLS, &SSLmode);
                    if (LDAP_SUCCESS != result)
                    {
                        ldap_unbind_s(ldc->ldap);
                        ldc->reason = "LDAP: ldap_set_option - LDAP_OPT_X_TLS_HARD failed";
                        ldc->ldap = NULL;
                    }
                }

                #elif APR_HAS_MICROSOFT_LDAPSDK
                ldc->ldap = ldap_sslinit(const_cast(ldc->host), ldc->port, 1);

                #else
                    ldc->reason = "LDAP: ssl connections not supported";
                #endif /* APR_HAS_NOVELL_LDAPSDK */
            
                #endif /* APR_HAS_LDAP_SSL */
            }
            else
                ldc->reason = "LDAP: ssl connections not supported";
        }

        if (NULL == ldc->ldap)
        {
            ldc->bound = 0;
            if (NULL == ldc->reason)
                ldc->reason = "LDAP: ldap initialization failed";
            return(-1);
        }

        /* Set the alias dereferencing option */
        ldap_set_option(ldc->ldap, LDAP_OPT_DEREF, &(ldc->deref));

        /* always default to LDAP V3 */
        ldap_set_option(ldc->ldap, LDAP_OPT_PROTOCOL_VERSION, &version);

#ifdef LDAP_OPT_NETWORK_TIMEOUT
        if (st->connectionTimeout > 0) {
            timeOut.tv_sec = st->connectionTimeout;
        }

        if (st->connectionTimeout >= 0) {
            rc = ldap_set_option(ldc->ldap, LDAP_OPT_NETWORK_TIMEOUT, (void *)&timeOut);
            if (APR_SUCCESS != rc) {
                ap_log_error(APLOG_MARK, APLOG_ERR, 0, r->server,
                                 "LDAP: Could not set the connection timeout" );
            }
        }
#endif
    }


    /* loop trying to bind up to 10 times if LDAP_SERVER_DOWN error is
     * returned.  Break out of the loop on Success or any other error.
     *
     * NOTE: Looping is probably not a great idea. If the server isn't 
     * responding the chances it will respond after a few tries are poor.
     * However, the original code looped and it only happens on
     * the error condition.
      */
    for (failures=0; failures<10; failures++)
    {
        result = ldap_simple_bind_s(ldc->ldap, const_cast(ldc->binddn), const_cast(ldc->bindpw));
        if (LDAP_SERVER_DOWN != result)
            break;
    }

    /* free the handle if there was an error
    */
    if (LDAP_SUCCESS != result)
    {
        ldap_unbind_s(ldc->ldap);
        ldc->ldap = NULL;
        ldc->bound = 0;
        ldc->reason = "LDAP: ldap_simple_bind_s() failed";
    }
    else {
        ldc->bound = 1;
        ldc->reason = "LDAP: connection open successful";
    }

    return(result);
}


/*
 * Find an existing ldap connection struct that matches the
 * provided ldap connection parameters.
 *
 * If not found in the cache, a new ldc structure will be allocated from st->pool
 * and returned to the caller. If found in the cache, a pointer to the existing
 * ldc structure will be returned.
 */
LDAP_DECLARE(util_ldap_connection_t *)util_ldap_connection_find(request_rec *r, const char *host, int port,
                                              const char *binddn, const char *bindpw, deref_options deref,
                                              int secure )
{
    struct util_ldap_connection_t *l, *p;	/* To traverse the linked list */

    util_ldap_state_t *st = 
        (util_ldap_state_t *)ap_get_module_config(r->server->module_config,
        &ldap_module);


#if APR_HAS_THREADS
    /* mutex lock this function */
    apr_thread_mutex_lock(st->mutex);
#endif

    /* Search for an exact connection match in the list that is not
     * being used.
     */
    for (l=st->connections,p=NULL; l; l=l->next) {
#if APR_HAS_THREADS
        if (APR_SUCCESS == apr_thread_mutex_trylock(l->lock)) {
#endif
        if ((l->port == port) && (strcmp(l->host, host) == 0) && 
            ((!l->binddn && !binddn) || (l->binddn && binddn && !strcmp(l->binddn, binddn))) && 
            ((!l->bindpw && !bindpw) || (l->bindpw && bindpw && !strcmp(l->bindpw, bindpw))) && 
            (l->deref == deref) && (l->secure == secure)) {

            break;
        }
#if APR_HAS_THREADS
            /* If this connection didn't match the criteria, then we
             * need to unlock the mutex so it is available to be reused.
             */
            apr_thread_mutex_unlock(l->lock);
        }
#endif
        p = l;
    }

    /* If nothing found, search again, but we don't care about the
     * binddn and bindpw this time.
     */
    if (!l) {
        for (l=st->connections,p=NULL; l; l=l->next) {
#if APR_HAS_THREADS
            if (APR_SUCCESS == apr_thread_mutex_trylock(l->lock)) {

#endif
            if ((l->port == port) && (strcmp(l->host, host) == 0) && 
                (l->deref == deref) && (l->secure == secure)) {

                /* the bind credentials have changed */
                l->bound = 0;
                util_ldap_strdup((char**)&(l->binddn), binddn);
                util_ldap_strdup((char**)&(l->bindpw), bindpw);
                break;
            }
#if APR_HAS_THREADS
                /* If this connection didn't match the criteria, then we
                 * need to unlock the mutex so it is available to be reused.
                 */
                apr_thread_mutex_unlock(l->lock);
            }
#endif
            p = l;
        }
    }

/* artificially disable cache */
/* l = NULL; */

    /* If no connection what found after the second search, we
     * must create one.
     */
    if (!l) {

        /* 
         * Add the new connection entry to the linked list. Note that we
         * don't actually establish an LDAP connection yet; that happens
         * the first time authentication is requested.
         */
        /* create the details to the pool in st */
        l = apr_pcalloc(st->pool, sizeof(util_ldap_connection_t));
#if APR_HAS_THREADS
        apr_thread_mutex_create(&l->lock, APR_THREAD_MUTEX_DEFAULT, st->pool);
        apr_thread_mutex_lock(l->lock);
#endif
        l->pool = st->pool;
        l->bound = 0;
        l->host = apr_pstrdup(st->pool, host);
        l->port = port;
        l->deref = deref;
        util_ldap_strdup((char**)&(l->binddn), binddn);
        util_ldap_strdup((char**)&(l->bindpw), bindpw);
        l->secure = secure;

        /* add the cleanup to the pool */
        apr_pool_cleanup_register(l->pool, l,
                                  util_ldap_connection_cleanup,
                                  apr_pool_cleanup_null);

        if (p) {
            p->next = l;
        }
        else {
            st->connections = l;
        }
    }

#if APR_HAS_THREADS
    apr_thread_mutex_unlock(st->mutex);
#endif
    return l;
}

/* ------------------------------------------------------------------ */

/*
 * Compares two DNs to see if they're equal. The only way to do this correctly is to 
 * search for the dn and then do ldap_get_dn() on the result. This should match the 
 * initial dn, since it would have been also retrieved with ldap_get_dn(). This is
 * expensive, so if the configuration value compare_dn_on_server is
 * false, just does an ordinary strcmp.
 *
 * The lock for the ldap cache should already be acquired.
 */
LDAP_DECLARE(int) util_ldap_cache_comparedn(request_rec *r, util_ldap_connection_t *ldc, 
                            const char *url, const char *dn, const char *reqdn, 
                            int compare_dn_on_server)
{
    int result = 0;
    util_url_node_t *curl; 
    util_url_node_t curnode;
    util_dn_compare_node_t *node;
    util_dn_compare_node_t newnode;
    int failures = 0;
    LDAPMessage *res, *entry;
    char *searchdn;

    util_ldap_state_t *st =  (util_ldap_state_t *)ap_get_module_config(r->server->module_config, &ldap_module);

    /* get cache entry (or create one) */
    LDAP_CACHE_LOCK();

    curnode.url = url;
    curl = util_ald_cache_fetch(st->util_ldap_cache, &curnode);
    if (curl == NULL) {
        curl = util_ald_create_caches(st, url);
    }
    LDAP_CACHE_UNLOCK();

    /* a simple compare? */
    if (!compare_dn_on_server) {
        /* unlock this read lock */
        if (strcmp(dn, reqdn)) {
            ldc->reason = "DN Comparison FALSE (direct strcmp())";
            return LDAP_COMPARE_FALSE;
        }
        else {
            ldc->reason = "DN Comparison TRUE (direct strcmp())";
            return LDAP_COMPARE_TRUE;
        }
    }

    if (curl) {
        /* no - it's a server side compare */
        LDAP_CACHE_LOCK();
    
        /* is it in the compare cache? */
        newnode.reqdn = (char *)reqdn;
        node = util_ald_cache_fetch(curl->dn_compare_cache, &newnode);
        if (node != NULL) {
            /* If it's in the cache, it's good */
            /* unlock this read lock */
            LDAP_CACHE_UNLOCK();
            ldc->reason = "DN Comparison TRUE (cached)";
            return LDAP_COMPARE_TRUE;
        }
    
        /* unlock this read lock */
        LDAP_CACHE_UNLOCK();
    }

start_over:
    if (failures++ > 10) {
	/* too many failures */
        return result;
    }

    /* make a server connection */
    if (LDAP_SUCCESS != (result = util_ldap_connection_open(r, ldc))) {
	/* connect to server failed */
        return result;
    }

    /* search for reqdn */
    if ((result = ldap_search_ext_s(ldc->ldap, const_cast(reqdn), LDAP_SCOPE_BASE, 
				    "(objectclass=*)", NULL, 1, 
				    NULL, NULL, NULL, -1, &res)) == LDAP_SERVER_DOWN) {
        ldc->reason = "DN Comparison ldap_search_ext_s() failed with server down";
        util_ldap_connection_unbind(ldc);
        goto start_over;
    }
    if (result != LDAP_SUCCESS) {
        /* search for reqdn failed - no match */
        ldc->reason = "DN Comparison ldap_search_ext_s() failed";
        return result;
    }

    entry = ldap_first_entry(ldc->ldap, res);
    searchdn = ldap_get_dn(ldc->ldap, entry);

    ldap_msgfree(res);
    if (strcmp(dn, searchdn) != 0) {
        /* compare unsuccessful */
        ldc->reason = "DN Comparison FALSE (checked on server)";
        result = LDAP_COMPARE_FALSE;
    }
    else {
        if (curl) {
            /* compare successful - add to the compare cache */
            LDAP_CACHE_LOCK();
            newnode.reqdn = (char *)reqdn;
            newnode.dn = (char *)dn;
            
            node = util_ald_cache_fetch(curl->dn_compare_cache, &newnode);
            if ((node == NULL) || 
                (strcmp(reqdn, node->reqdn) != 0) || (strcmp(dn, node->dn) != 0)) {

                util_ald_cache_insert(curl->dn_compare_cache, &newnode);
            }
            LDAP_CACHE_UNLOCK();
        }
        ldc->reason = "DN Comparison TRUE (checked on server)";
        result = LDAP_COMPARE_TRUE;
    }
    ldap_memfree(searchdn);
    return result;

}

/*
 * Does an generic ldap_compare operation. It accepts a cache that it will use
 * to lookup the compare in the cache. We cache two kinds of compares 
 * (require group compares) and (require user compares). Each compare has a different
 * cache node: require group includes the DN; require user does not because the
 * require user cache is owned by the 
 *
 */
LDAP_DECLARE(int) util_ldap_cache_compare(request_rec *r, util_ldap_connection_t *ldc,
                          const char *url, const char *dn,
                          const char *attrib, const char *value)
{
    int result = 0;
    util_url_node_t *curl; 
    util_url_node_t curnode;
    util_compare_node_t *compare_nodep;
    util_compare_node_t the_compare_node;
    apr_time_t curtime = 0; /* silence gcc -Wall */
    int failures = 0;

    util_ldap_state_t *st = 
        (util_ldap_state_t *)ap_get_module_config(r->server->module_config,
        &ldap_module);

    /* get cache entry (or create one) */
    LDAP_CACHE_LOCK();
    curnode.url = url;
    curl = util_ald_cache_fetch(st->util_ldap_cache, &curnode);
    if (curl == NULL) {
        curl = util_ald_create_caches(st, url);
    }
    LDAP_CACHE_UNLOCK();

    if (curl) {
        /* make a comparison to the cache */
        LDAP_CACHE_LOCK();
        curtime = apr_time_now();
    
        the_compare_node.dn = (char *)dn;
        the_compare_node.attrib = (char *)attrib;
        the_compare_node.value = (char *)value;
        the_compare_node.result = 0;
    
        compare_nodep = util_ald_cache_fetch(curl->compare_cache, &the_compare_node);
    
        if (compare_nodep != NULL) {
            /* found it... */
            if (curtime - compare_nodep->lastcompare > st->compare_cache_ttl) {
                /* ...but it is too old */
                util_ald_cache_remove(curl->compare_cache, compare_nodep);
            }
            else {
                /* ...and it is good */
                /* unlock this read lock */
                LDAP_CACHE_UNLOCK();
                if (LDAP_COMPARE_TRUE == compare_nodep->result) {
                    ldc->reason = "Comparison true (cached)";
                    return compare_nodep->result;
                }
                else if (LDAP_COMPARE_FALSE == compare_nodep->result) {
                    ldc->reason = "Comparison false (cached)";
                    return compare_nodep->result;
                }
                else if (LDAP_NO_SUCH_ATTRIBUTE == compare_nodep->result) {
                    ldc->reason = "Comparison no such attribute (cached)";
                    return compare_nodep->result;
                }
                else {
                    ldc->reason = "Comparison undefined (cached)";
                    return compare_nodep->result;
                }
            }
        }
        /* unlock this read lock */
        LDAP_CACHE_UNLOCK();
    }

start_over:
    if (failures++ > 10) {
        /* too many failures */
        return result;
    }
    if (LDAP_SUCCESS != (result = util_ldap_connection_open(r, ldc))) {
        /* connect failed */
        return result;
    }

    if ((result = ldap_compare_s(ldc->ldap, const_cast(dn), const_cast(attrib), const_cast(value)))
        == LDAP_SERVER_DOWN) { 
        /* connection failed - try again */
        ldc->reason = "ldap_compare_s() failed with server down";
        util_ldap_connection_unbind(ldc);
        goto start_over;
    }

    ldc->reason = "Comparison complete";
    if ((LDAP_COMPARE_TRUE == result) || 
        (LDAP_COMPARE_FALSE == result) ||
        (LDAP_NO_SUCH_ATTRIBUTE == result)) {
        if (curl) {
            /* compare completed; caching result */
            LDAP_CACHE_LOCK();
            the_compare_node.lastcompare = curtime;
            the_compare_node.result = result;

            /* If the node doesn't exist then insert it, otherwise just update it with
               the last results */
            compare_nodep = util_ald_cache_fetch(curl->compare_cache, &the_compare_node);
            if ((compare_nodep == NULL) || 
                (strcmp(the_compare_node.dn, compare_nodep->dn) != 0) || 
                (strcmp(the_compare_node.attrib, compare_nodep->attrib) != 0) || 
                (strcmp(the_compare_node.value, compare_nodep->value) != 0)) {

                util_ald_cache_insert(curl->compare_cache, &the_compare_node);
            }
            else {
                compare_nodep->lastcompare = curtime;
                compare_nodep->result = result;
            }
            LDAP_CACHE_UNLOCK();
        }
        if (LDAP_COMPARE_TRUE == result) {
            ldc->reason = "Comparison true (adding to cache)";
            return LDAP_COMPARE_TRUE;
        }
        else if (LDAP_COMPARE_FALSE == result) {
            ldc->reason = "Comparison false (adding to cache)";
            return LDAP_COMPARE_FALSE;
        }
        else {
            ldc->reason = "Comparison no such attribute (adding to cache)";
            return LDAP_NO_SUCH_ATTRIBUTE;
        }
    }
    return result;
}

LDAP_DECLARE(int) util_ldap_cache_checkuserid(request_rec *r, util_ldap_connection_t *ldc,
                              const char *url, const char *basedn, int scope, char **attrs,
                              const char *filter, const char *bindpw, const char **binddn,
                              const char ***retvals)
{
    const char **vals = NULL;
    int numvals = 0;
    int result = 0;
    LDAPMessage *res, *entry;
    char *dn;
    int count;
    int failures = 0;
    util_url_node_t *curl;		/* Cached URL node */
    util_url_node_t curnode;
    util_search_node_t *search_nodep;	/* Cached search node */
    util_search_node_t the_search_node;
    apr_time_t curtime;

    util_ldap_state_t *st = 
        (util_ldap_state_t *)ap_get_module_config(r->server->module_config,
        &ldap_module);

    /* Get the cache node for this url */
    LDAP_CACHE_LOCK();
    curnode.url = url;
    curl = (util_url_node_t *)util_ald_cache_fetch(st->util_ldap_cache, &curnode);
    if (curl == NULL) {
        curl = util_ald_create_caches(st, url);
    }
    LDAP_CACHE_UNLOCK();

    if (curl) {
        LDAP_CACHE_LOCK();
        the_search_node.username = filter;
        search_nodep = util_ald_cache_fetch(curl->search_cache, &the_search_node);
        if (search_nodep != NULL) {
    
            /* found entry in search cache... */
            curtime = apr_time_now();
    
            /*
             * Remove this item from the cache if its expired.
             * If the sent password doesn't match the stored password,
             * the entry will be removed and readded later if the
             * credentials pass authentication.
             */
            if ((curtime - search_nodep->lastbind) > st->search_cache_ttl) {
                /* ...but entry is too old */
                util_ald_cache_remove(curl->search_cache, search_nodep);
            }
            else if ((search_nodep->bindpw) &&
                (search_nodep->bindpw[0] != '\0') &&
                (strcmp(search_nodep->bindpw, bindpw) == 0)) {
                /* ...and entry is valid */
                *binddn = search_nodep->dn;
                *retvals = search_nodep->vals;
                LDAP_CACHE_UNLOCK();
                ldc->reason = "Authentication successful (cached)";
                return LDAP_SUCCESS;
            }
        }
        /* unlock this read lock */
        LDAP_CACHE_UNLOCK();
    }

    /*	
     * At this point, there is no valid cached search, so lets do the search.
     */

    /*
     * If any LDAP operation fails due to LDAP_SERVER_DOWN, control returns here.
     */
start_over:
    if (failures++ > 10) {
        return result;
    }
    if (LDAP_SUCCESS != (result = util_ldap_connection_open(r, ldc))) {
        return result;
    }

    /* try do the search */
    if ((result = ldap_search_ext_s(ldc->ldap,
				    const_cast(basedn), scope, 
				    const_cast(filter), attrs, 0, 
				    NULL, NULL, NULL, -1, &res)) == LDAP_SERVER_DOWN) {
        ldc->reason = "ldap_search_ext_s() for user failed with server down";
        util_ldap_connection_unbind(ldc);
        goto start_over;
    }

    /* if there is an error (including LDAP_NO_SUCH_OBJECT) return now */
    if (result != LDAP_SUCCESS) {
        ldc->reason = "ldap_search_ext_s() for user failed";
        return result;
    }

    /* 
     * We should have found exactly one entry; to find a different
     * number is an error.
     */
    count = ldap_count_entries(ldc->ldap, res);
    if (count != 1) 
    {
        if (count == 0 )
            ldc->reason = "User not found";
        else
            ldc->reason = "User is not unique (search found two or more matches)";
        ldap_msgfree(res);
        return LDAP_NO_SUCH_OBJECT;
    }

    entry = ldap_first_entry(ldc->ldap, res);

    /* Grab the dn, copy it into the pool, and free it again */
    dn = ldap_get_dn(ldc->ldap, entry);
    *binddn = apr_pstrdup(r->pool, dn);
    ldap_memfree(dn);

    /* 
     * A bind to the server with an empty password always succeeds, so
     * we check to ensure that the password is not empty. This implies
     * that users who actually do have empty passwords will never be
     * able to authenticate with this module. I don't see this as a big
     * problem.
     */
    if (!bindpw || strlen(bindpw) <= 0) {
        ldap_msgfree(res);
        ldc->reason = "Empty password not allowed";
        return LDAP_INVALID_CREDENTIALS;
    }

    /* 
     * Attempt to bind with the retrieved dn and the password. If the bind
     * fails, it means that the password is wrong (the dn obviously
     * exists, since we just retrieved it)
     */
    if ((result = 
         ldap_simple_bind_s(ldc->ldap, const_cast(*binddn), const_cast(bindpw))) == 
         LDAP_SERVER_DOWN) {
        ldc->reason = "ldap_simple_bind_s() to check user credentials failed with server down";
        ldap_msgfree(res);
        util_ldap_connection_unbind(ldc);
        goto start_over;
    }

    /* failure? if so - return */
    if (result != LDAP_SUCCESS) {
        ldc->reason = "ldap_simple_bind_s() to check user credentials failed";
        ldap_msgfree(res);
        util_ldap_connection_unbind(ldc);
        return result;
    }
    else {
        /*
         * We have just bound the connection to a different user and password
         * combination, which might be reused unintentionally next time this
         * connection is used from the connection pool. To ensure no confusion,
         * we mark the connection as unbound.
         */
        ldc->bound = 0;
    }

    /*
     * Get values for the provided attributes.
     */
    if (attrs) {
        int k = 0;
        int i = 0;
        while (attrs[k++]);
        vals = apr_pcalloc(r->pool, sizeof(char *) * (k+1));
        numvals = k;
        while (attrs[i]) {
            char **values;
            int j = 0;
            char *str = NULL;
            /* get values */
            values = ldap_get_values(ldc->ldap, entry, attrs[i]);
            while (values && values[j]) {
                str = str ? apr_pstrcat(r->pool, str, "; ", values[j], NULL) : apr_pstrdup(r->pool, values[j]);
                j++;
            }
            ldap_value_free(values);
            vals[i] = str;
            i++;
        }
        *retvals = vals;
    }

    /* 		
     * Add the new username to the search cache.
     */
    if (curl) {
        LDAP_CACHE_LOCK();
        the_search_node.username = filter;
        the_search_node.dn = *binddn;
        the_search_node.bindpw = bindpw;
        the_search_node.lastbind = apr_time_now();
        the_search_node.vals = vals;
        the_search_node.numvals = numvals;

        /* Search again to make sure that another thread didn't ready insert this node
           into the cache before we got here. If it does exist then update the lastbind */
        search_nodep = util_ald_cache_fetch(curl->search_cache, &the_search_node);
        if ((search_nodep == NULL) || 
            (strcmp(*binddn, search_nodep->dn) != 0)) {

            /* Nothing in cache, insert new entry */
            util_ald_cache_insert(curl->search_cache, &the_search_node);
        }
        else if ((!search_nodep->bindpw) ||
            (strcmp(bindpw, search_nodep->bindpw) != 0)) {

            /* Entry in cache is invalid, remove it and insert new one */
            util_ald_cache_remove(curl->search_cache, search_nodep);
            util_ald_cache_insert(curl->search_cache, &the_search_node);
        }
        else {
            /* Cache entry is valid, update lastbind */
            search_nodep->lastbind = the_search_node.lastbind;
        }
        LDAP_CACHE_UNLOCK();
    }
    ldap_msgfree(res);

    ldc->reason = "Authentication successful";
    return LDAP_SUCCESS;
}

/*
 * This function will return the DN of the entry matching userid.
 * It is used to get the DN in case some other module than mod_auth_ldap
 * has authenticated the user.
 * The function is basically a copy of util_ldap_cache_checkuserid
 * with password checking removed.
 */
LDAP_DECLARE(int) util_ldap_cache_getuserdn(request_rec *r, util_ldap_connection_t *ldc,
                              const char *url, const char *basedn, int scope, char **attrs,
                              const char *filter, const char **binddn,
                              const char ***retvals)
{
    const char **vals = NULL;
    int numvals = 0;
    int result = 0;
    LDAPMessage *res, *entry;
    char *dn;
    int count;
    int failures = 0;
    util_url_node_t *curl;		/* Cached URL node */
    util_url_node_t curnode;
    util_search_node_t *search_nodep;	/* Cached search node */
    util_search_node_t the_search_node;
    apr_time_t curtime;

    util_ldap_state_t *st = 
        (util_ldap_state_t *)ap_get_module_config(r->server->module_config,
        &ldap_module);

    /* Get the cache node for this url */
    LDAP_CACHE_LOCK();
    curnode.url = url;
    curl = (util_url_node_t *)util_ald_cache_fetch(st->util_ldap_cache, &curnode);
    if (curl == NULL) {
        curl = util_ald_create_caches(st, url);
    }
    LDAP_CACHE_UNLOCK();

    if (curl) {
        LDAP_CACHE_LOCK();
        the_search_node.username = filter;
        search_nodep = util_ald_cache_fetch(curl->search_cache, &the_search_node);
        if (search_nodep != NULL) {
    
            /* found entry in search cache... */
            curtime = apr_time_now();
    
            /*
             * Remove this item from the cache if its expired.
             */
            if ((curtime - search_nodep->lastbind) > st->search_cache_ttl) {
                /* ...but entry is too old */
                util_ald_cache_remove(curl->search_cache, search_nodep);
            }
            else {
                /* ...and entry is valid */
                *binddn = search_nodep->dn;
                *retvals = search_nodep->vals;
                LDAP_CACHE_UNLOCK();
                ldc->reason = "Search successful (cached)";
                return LDAP_SUCCESS;
            }
        }
        /* unlock this read lock */
        LDAP_CACHE_UNLOCK();
    }

    /*	
     * At this point, there is no valid cached search, so lets do the search.
     */

    /*
     * If any LDAP operation fails due to LDAP_SERVER_DOWN, control returns here.
     */
start_over:
    if (failures++ > 10) {
        return result;
    }
    if (LDAP_SUCCESS != (result = util_ldap_connection_open(r, ldc))) {
        return result;
    }

    /* try do the search */
    if ((result = ldap_search_ext_s(ldc->ldap,
				    const_cast(basedn), scope, 
				    const_cast(filter), attrs, 0, 
				    NULL, NULL, NULL, -1, &res)) == LDAP_SERVER_DOWN) {
        ldc->reason = "ldap_search_ext_s() for user failed with server down";
        util_ldap_connection_unbind(ldc);
        goto start_over;
    }

    /* if there is an error (including LDAP_NO_SUCH_OBJECT) return now */
    if (result != LDAP_SUCCESS) {
        ldc->reason = "ldap_search_ext_s() for user failed";
        return result;
    }

    /* 
     * We should have found exactly one entry; to find a different
     * number is an error.
     */
    count = ldap_count_entries(ldc->ldap, res);
    if (count != 1) 
    {
        if (count == 0 )
            ldc->reason = "User not found";
        else
            ldc->reason = "User is not unique (search found two or more matches)";
        ldap_msgfree(res);
        return LDAP_NO_SUCH_OBJECT;
    }

    entry = ldap_first_entry(ldc->ldap, res);

    /* Grab the dn, copy it into the pool, and free it again */
    dn = ldap_get_dn(ldc->ldap, entry);
    *binddn = apr_pstrdup(r->pool, dn);
    ldap_memfree(dn);

    /*
     * Get values for the provided attributes.
     */
    if (attrs) {
        int k = 0;
        int i = 0;
        while (attrs[k++]);
        vals = apr_pcalloc(r->pool, sizeof(char *) * (k+1));
        numvals = k;
        while (attrs[i]) {
            char **values;
            int j = 0;
            char *str = NULL;
            /* get values */
            values = ldap_get_values(ldc->ldap, entry, attrs[i]);
            while (values && values[j]) {
                str = str ? apr_pstrcat(r->pool, str, "; ", values[j], NULL) : apr_pstrdup(r->pool, values[j]);
                j++;
            }
            ldap_value_free(values);
            vals[i] = str;
            i++;
        }
        *retvals = vals;
    }

    /* 		
     * Add the new username to the search cache.
     */
    if (curl) {
        LDAP_CACHE_LOCK();
        the_search_node.username = filter;
        the_search_node.dn = *binddn;
        the_search_node.bindpw = NULL;
        the_search_node.lastbind = apr_time_now();
        the_search_node.vals = vals;
        the_search_node.numvals = numvals;

        /* Search again to make sure that another thread didn't ready insert this node
           into the cache before we got here. If it does exist then update the lastbind */
        search_nodep = util_ald_cache_fetch(curl->search_cache, &the_search_node);
        if ((search_nodep == NULL) || 
            (strcmp(*binddn, search_nodep->dn) != 0)) {

            /* Nothing in cache, insert new entry */
            util_ald_cache_insert(curl->search_cache, &the_search_node);
        }
        /*
         * Don't update lastbind on entries with bindpw because
         * we haven't verified that password. It's OK to update
         * the entry if there is no password in it.
         */
        else if (!search_nodep->bindpw) {
            /* Cache entry is valid, update lastbind */
            search_nodep->lastbind = the_search_node.lastbind;
        }
        LDAP_CACHE_UNLOCK();
    }
    ldap_msgfree(res);

    ldc->reason = "Search successful";
    return LDAP_SUCCESS;
}

/*
 * Reports if ssl support is enabled 
 *
 * 1 = enabled, 0 = not enabled
 */
LDAP_DECLARE(int) util_ldap_ssl_supported(request_rec *r)
{
   util_ldap_state_t *st = (util_ldap_state_t *)ap_get_module_config(
                                r->server->module_config, &ldap_module);

   return(st->ssl_support);
}


/* ---------------------------------------- */
/* config directives */


static const char *util_ldap_set_cache_bytes(cmd_parms *cmd, void *dummy, const char *bytes)
{
    util_ldap_state_t *st = 
        (util_ldap_state_t *)ap_get_module_config(cmd->server->module_config, 
						  &ldap_module);

    st->cache_bytes = atol(bytes);

    ap_log_error(APLOG_MARK, APLOG_DEBUG|APLOG_NOERRNO, 0, cmd->server, 
                 "[%" APR_PID_T_FMT "] ldap cache: Setting shared memory "
                 " cache size to %" APR_SIZE_T_FMT " bytes.", 
                 getpid(), st->cache_bytes);

    return NULL;
}

static const char *util_ldap_set_cache_file(cmd_parms *cmd, void *dummy, const char *file)
{
    util_ldap_state_t *st = 
        (util_ldap_state_t *)ap_get_module_config(cmd->server->module_config, 
                                                  &ldap_module);

    if (file) {
        st->cache_file = ap_server_root_relative(st->pool, file);
    }
    else {
        st->cache_file = NULL;
    }

    ap_log_error(APLOG_MARK, APLOG_DEBUG|APLOG_NOERRNO, 0, cmd->server, 
                 "LDAP cache: Setting shared memory cache file to %s bytes.", 
                 st->cache_file);

    return NULL;
}

static const char *util_ldap_set_cache_ttl(cmd_parms *cmd, void *dummy, const char *ttl)
{
    util_ldap_state_t *st = 
        (util_ldap_state_t *)ap_get_module_config(cmd->server->module_config, 
						  &ldap_module);

    st->search_cache_ttl = atol(ttl) * 1000000;

    ap_log_error(APLOG_MARK, APLOG_DEBUG|APLOG_NOERRNO, 0, cmd->server, 
                      "[%d] ldap cache: Setting cache TTL to %ld microseconds.", 
                      getpid(), st->search_cache_ttl);

    return NULL;
}

static const char *util_ldap_set_cache_entries(cmd_parms *cmd, void *dummy, const char *size)
{
    util_ldap_state_t *st = 
        (util_ldap_state_t *)ap_get_module_config(cmd->server->module_config, 
						  &ldap_module);


    st->search_cache_size = atol(size);
    if (st->search_cache_size < 0) {
        st->search_cache_size = 0;
    }

    ap_log_error(APLOG_MARK, APLOG_DEBUG|APLOG_NOERRNO, 0, cmd->server, 
                      "[%d] ldap cache: Setting search cache size to %ld entries.", 
                      getpid(), st->search_cache_size);

    return NULL;
}

static const char *util_ldap_set_opcache_ttl(cmd_parms *cmd, void *dummy, const char *ttl)
{
    util_ldap_state_t *st = 
        (util_ldap_state_t *)ap_get_module_config(cmd->server->module_config, 
						  &ldap_module);

    st->compare_cache_ttl = atol(ttl) * 1000000;

    ap_log_error(APLOG_MARK, APLOG_DEBUG|APLOG_NOERRNO, 0, cmd->server, 
                      "[%d] ldap cache: Setting operation cache TTL to %ld microseconds.", 
                      getpid(), st->compare_cache_ttl);

    return NULL;
}

static const char *util_ldap_set_opcache_entries(cmd_parms *cmd, void *dummy, const char *size)
{
    util_ldap_state_t *st = 
        (util_ldap_state_t *)ap_get_module_config(cmd->server->module_config, 
						  &ldap_module);

    st->compare_cache_size = atol(size);
    if (st->compare_cache_size < 0) {
        st->compare_cache_size = 0;
    }

    ap_log_error(APLOG_MARK, APLOG_DEBUG|APLOG_NOERRNO, 0, cmd->server, 
                      "[%d] ldap cache: Setting operation cache size to %ld entries.", 
                      getpid(), st->compare_cache_size);

    return NULL;
}

static const char *util_ldap_set_cert_auth(cmd_parms *cmd, void *dummy, const char *file)
{
    util_ldap_state_t *st = 
        (util_ldap_state_t *)ap_get_module_config(cmd->server->module_config, 
						  &ldap_module);
    const char *err = ap_check_cmd_context(cmd, GLOBAL_ONLY);
    apr_finfo_t finfo;
    apr_status_t rv;

    if (err != NULL) {
        return err;
    }

    ap_log_error(APLOG_MARK, APLOG_DEBUG|APLOG_NOERRNO, 0, cmd->server, 
                      "LDAP: SSL trusted certificate authority file - %s", 
                       file);

    st->cert_auth_file = ap_server_root_relative(cmd->pool, file);

    if (st->cert_auth_file && 
        ((rv = apr_stat (&finfo, st->cert_auth_file, APR_FINFO_MIN, cmd->pool)) != APR_SUCCESS))
    {
        ap_log_error(APLOG_MARK, APLOG_ERR, rv, cmd->server, 
                     "LDAP: Could not open SSL trusted certificate authority file - %s", 
                     st->cert_auth_file == NULL ? file : st->cert_auth_file);
        return "Invalid file path";
    }

    return(NULL);
}


static const char *util_ldap_set_cert_type(cmd_parms *cmd, void *dummy, const char *Type)
{
    util_ldap_state_t *st = 
    (util_ldap_state_t *)ap_get_module_config(cmd->server->module_config, 
                                              &ldap_module);
    const char *err = ap_check_cmd_context(cmd, GLOBAL_ONLY);
    if (err != NULL) {
        return err;
    }

    ap_log_error(APLOG_MARK, APLOG_DEBUG|APLOG_NOERRNO, 0, cmd->server, 
                      "LDAP: SSL trusted certificate authority file type - %s", 
                       Type);

    if (0 == strcmp("DER_FILE", Type))
        st->cert_file_type = LDAP_CA_TYPE_DER;

    else if (0 == strcmp("BASE64_FILE", Type))
        st->cert_file_type = LDAP_CA_TYPE_BASE64;

    else if (0 == strcmp("CERT7_DB_PATH", Type))
        st->cert_file_type = LDAP_CA_TYPE_CERT7_DB;

    else
        st->cert_file_type = LDAP_CA_TYPE_UNKNOWN;

    return(NULL);
}

static const char *util_ldap_set_connection_timeout(cmd_parms *cmd, void *dummy, const char *ttl)
{
    util_ldap_state_t *st = 
        (util_ldap_state_t *)ap_get_module_config(cmd->server->module_config, 
						  &ldap_module);
    const char *err = ap_check_cmd_context(cmd, GLOBAL_ONLY);

    if (err != NULL) {
        return err;
    }

#ifdef LDAP_OPT_NETWORK_TIMEOUT
    st->connectionTimeout = atol(ttl);

    ap_log_error(APLOG_MARK, APLOG_DEBUG|APLOG_NOERRNO, 0, cmd->server, 
                      "[%d] ldap connection: Setting connection timeout to %ld seconds.", 
                      getpid(), st->connectionTimeout);
#else
    ap_log_error(APLOG_MARK, APLOG_NOTICE, 0, cmd->server,
                     "LDAP: Connection timout option not supported by the LDAP SDK in use." );
#endif

    return NULL;
}

void *util_ldap_create_config(apr_pool_t *p, server_rec *s)
{
    util_ldap_state_t *st = 
        (util_ldap_state_t *)apr_pcalloc(p, sizeof(util_ldap_state_t));

    /* Create a per vhost pool for mod_ldap to use, serialized with 
     * st->mutex (also one per vhost) 
     */
    apr_pool_create(&st->pool, p);
#if APR_HAS_THREADS
    apr_thread_mutex_create(&st->mutex, APR_THREAD_MUTEX_DEFAULT, st->pool);
#endif

    st->cache_bytes = 100000;
    st->search_cache_ttl = 600000000;
    st->search_cache_size = 1024;
    st->compare_cache_ttl = 600000000;
    st->compare_cache_size = 1024;
    st->connections = NULL;
    st->cert_auth_file = NULL;
    st->cert_file_type = LDAP_CA_TYPE_UNKNOWN;
    st->ssl_support = 0;
    st->connectionTimeout = 10;

    return st;
}

static apr_status_t util_ldap_cleanup_module(void *data)
{
#if APR_HAS_LDAP_SSL && APR_HAS_NOVELL_LDAPSDK
    server_rec *s = data;
    util_ldap_state_t *st = (util_ldap_state_t *)ap_get_module_config(
        s->module_config, &ldap_module);
    
    if (st->ssl_support)
        ldapssl_client_deinit();

#endif
    return APR_SUCCESS;
}

static int util_ldap_post_config(apr_pool_t *p, apr_pool_t *plog, 
                                 apr_pool_t *ptemp, server_rec *s)
{
    int rc = LDAP_SUCCESS;
    apr_status_t result;
    char buf[MAX_STRING_LEN];
    server_rec *s_vhost;
    util_ldap_state_t *st_vhost;

    util_ldap_state_t *st =
        (util_ldap_state_t *)ap_get_module_config(s->module_config, &ldap_module);

    void *data;
    const char *userdata_key = "util_ldap_init";

    /* util_ldap_post_config() will be called twice. Don't bother
     * going through all of the initialization on the first call
     * because it will just be thrown away.*/
    apr_pool_userdata_get(&data, userdata_key, s->process->pool);
    if (!data) {
        apr_pool_userdata_set((const void *)1, userdata_key,
                               apr_pool_cleanup_null, s->process->pool);

#if APR_HAS_SHARED_MEMORY
        /* If the cache file already exists then delete it.  Otherwise we are
         * going to run into problems creating the shared memory. */
        if (st->cache_file) {
            char *lck_file = apr_pstrcat (ptemp, st->cache_file, ".lck", NULL);
            apr_file_remove(st->cache_file, ptemp);
            apr_file_remove(lck_file, ptemp);
        }
#endif
        return OK;
    }

#if APR_HAS_SHARED_MEMORY
    /* initializing cache if shared memory size is not zero and we already don't have shm address */
    if (!st->cache_shm && st->cache_bytes > 0) {
#endif
        result = util_ldap_cache_init(p, st);
        if (result != APR_SUCCESS) {
            apr_strerror(result, buf, sizeof(buf));
            ap_log_error(APLOG_MARK, APLOG_ERR, result, s,
                         "LDAP cache: error while creating a shared memory segment: %s", buf);
        }


#if APR_HAS_SHARED_MEMORY
        if (st->cache_file) {
            st->lock_file = apr_pstrcat (st->pool, st->cache_file, ".lck", NULL);
        }
        else
#endif
            st->lock_file = ap_server_root_relative(st->pool, tmpnam(NULL));

        result = apr_global_mutex_create(&st->util_ldap_cache_lock, st->lock_file, APR_LOCK_DEFAULT, st->pool);
        if (result != APR_SUCCESS) {
            return result;
        }

#ifdef UTIL_LDAP_SET_MUTEX_PERMS
        result = unixd_set_global_mutex_perms(st->util_ldap_cache_lock);
        if (result != APR_SUCCESS) {
            ap_log_error(APLOG_MARK, APLOG_CRIT, result, s, 
                         "LDAP cache: failed to set mutex permissions");
            return result;
        }
#endif

        /* merge config in all vhost */
        s_vhost = s->next;
        while (s_vhost) {
            st_vhost = (util_ldap_state_t *)ap_get_module_config(s_vhost->module_config, &ldap_module);

#if APR_HAS_SHARED_MEMORY
            st_vhost->cache_shm = st->cache_shm;
            st_vhost->cache_rmm = st->cache_rmm;
            st_vhost->cache_file = st->cache_file;
            ap_log_error(APLOG_MARK, APLOG_DEBUG|APLOG_NOERRNO, result, s, 
                         "LDAP merging Shared Cache conf: shm=0x%pp rmm=0x%pp for VHOST: %s",
                         st->cache_shm, st->cache_rmm, s_vhost->server_hostname);
#endif
            st_vhost->lock_file = st->lock_file;
            s_vhost = s_vhost->next;
        }
#if APR_HAS_SHARED_MEMORY
    }
    else {
        ap_log_error(APLOG_MARK, APLOG_DEBUG, 0, s, "LDAP cache: LDAPSharedCacheSize is zero, disabling shared memory cache");
    }
#endif
    
    /* log the LDAP SDK used 
     */
    #if APR_HAS_NETSCAPE_LDAPSDK 
    
        ap_log_error(APLOG_MARK, APLOG_NOTICE, 0, s, 
             "LDAP: Built with Netscape LDAP SDK" );

    #elif APR_HAS_NOVELL_LDAPSDK

        ap_log_error(APLOG_MARK, APLOG_NOTICE, 0, s, 
             "LDAP: Built with Novell LDAP SDK" );

    #elif APR_HAS_OPENLDAP_LDAPSDK

        ap_log_error(APLOG_MARK, APLOG_NOTICE, 0, s, 
             "LDAP: Built with OpenLDAP LDAP SDK" );

    #elif APR_HAS_MICROSOFT_LDAPSDK
    
        ap_log_error(APLOG_MARK, APLOG_NOTICE, 0, s, 
             "LDAP: Built with Microsoft LDAP SDK" );
    #else
    
        ap_log_error(APLOG_MARK, APLOG_NOTICE, 0, s, 
             "LDAP: Built with unknown LDAP SDK" );

    #endif /* APR_HAS_NETSCAPE_LDAPSDK */



    apr_pool_cleanup_register(p, s, util_ldap_cleanup_module,
                              util_ldap_cleanup_module); 

    /* initialize SSL support if requested
    */
    if (st->cert_auth_file)
    {
        #if APR_HAS_LDAP_SSL /* compiled with ssl support */

        #if APR_HAS_NETSCAPE_LDAPSDK 

            /* Netscape sdk only supports a cert7.db file 
            */
            if (st->cert_file_type == LDAP_CA_TYPE_CERT7_DB)
            {
                rc = ldapssl_client_init(st->cert_auth_file, NULL);
            }
            else
            {
                ap_log_error(APLOG_MARK, APLOG_CRIT, 0, s, 
                         "LDAP: Invalid LDAPTrustedCAType directive - "
                          "CERT7_DB_PATH type required");
                rc = -1;
            }

        #elif APR_HAS_NOVELL_LDAPSDK
        
            /* Novell SDK supports DER or BASE64 files
            */
            if (st->cert_file_type == LDAP_CA_TYPE_DER  ||
                st->cert_file_type == LDAP_CA_TYPE_BASE64 )
            {
                rc = ldapssl_client_init(NULL, NULL);
                if (LDAP_SUCCESS == rc)
                {
                    if (st->cert_file_type == LDAP_CA_TYPE_BASE64)
                        rc = ldapssl_add_trusted_cert(st->cert_auth_file, 
                                                  LDAPSSL_CERT_FILETYPE_B64);
                    else
                        rc = ldapssl_add_trusted_cert(st->cert_auth_file, 
                                                  LDAPSSL_CERT_FILETYPE_DER);

                    if (LDAP_SUCCESS != rc)
                        ldapssl_client_deinit();
                }
            }
            else
            {
                ap_log_error(APLOG_MARK, APLOG_CRIT, 0, s, 
                             "LDAP: Invalid LDAPTrustedCAType directive - "
                             "DER_FILE or BASE64_FILE type required");
                rc = -1;
            }

        #elif APR_HAS_OPENLDAP_LDAPSDK

            /* OpenLDAP SDK supports BASE64 files
            */
            if (st->cert_file_type == LDAP_CA_TYPE_BASE64)
            {
                rc = ldap_set_option(NULL, LDAP_OPT_X_TLS_CACERTFILE, st->cert_auth_file);
            }
            else
            {
                ap_log_error(APLOG_MARK, APLOG_CRIT, 0, s, 
                             "LDAP: Invalid LDAPTrustedCAType directive - "
                             "BASE64_FILE type required");
                rc = -1;
            }


        #elif APR_HAS_MICROSOFT_LDAPSDK
            
            /* Microsoft SDK use the registry certificate store - always
             * assume support is always available
            */
            rc = LDAP_SUCCESS;

        #else
            rc = -1;
        #endif /* APR_HAS_NETSCAPE_LDAPSDK */

        #else  /* not compiled with SSL Support */

            ap_log_error(APLOG_MARK, APLOG_NOTICE, 0, s, 
                     "LDAP: Not built with SSL support." );
            rc = -1;

        #endif /* APR_HAS_LDAP_SSL */

        if (LDAP_SUCCESS == rc)
        {
            st->ssl_support = 1;
        }
        else
        {
            ap_log_error(APLOG_MARK, APLOG_WARNING, 0, s, 
                         "LDAP: SSL initialization failed");
            st->ssl_support = 0;
        }
    }
      
        /* The Microsoft SDK uses the registry certificate store -
         * always assume support is available
        */
    #if APR_HAS_MICROSOFT_LDAPSDK
        st->ssl_support = 1;
    #endif
    

        /* log SSL status - If SSL isn't available it isn't necessarily
         * an error because the modules asking for LDAP connections 
         * may not ask for SSL support
        */
    if (st->ssl_support)
    {
       ap_log_error(APLOG_MARK, APLOG_NOTICE, 0, s, 
                         "LDAP: SSL support available" );
    }
    else
    {
       ap_log_error(APLOG_MARK, APLOG_NOTICE, 0, s, 
                         "LDAP: SSL support unavailable" );
    }
    
    return(OK);
}

static void util_ldap_child_init(apr_pool_t *p, server_rec *s)
{
    apr_status_t sts;
    util_ldap_state_t *st = ap_get_module_config(s->module_config, &ldap_module);

    if (!st->util_ldap_cache_lock) return;

    sts = apr_global_mutex_child_init(&st->util_ldap_cache_lock, st->lock_file, p);
    if (sts != APR_SUCCESS) {
        ap_log_error(APLOG_MARK, APLOG_CRIT, sts, s,
                     "Failed to initialise global mutex %s in child process %"
                     APR_PID_T_FMT
                     ".",
                     st->lock_file, getpid());
        return;
    }
    else {
        ap_log_error(APLOG_MARK, APLOG_DEBUG|APLOG_NOERRNO, 0, s, 
                     "Initialisation of global mutex %s in child process %"
                     APR_PID_T_FMT
                     " successful.",
                     st->lock_file, getpid());
    }
}

command_rec util_ldap_cmds[] = {
    AP_INIT_TAKE1("LDAPSharedCacheSize", util_ldap_set_cache_bytes, NULL, RSRC_CONF,
                  "Sets the size of the shared memory cache in bytes. "
                  "Zero means disable the shared memory cache. Defaults to 100KB."),

    AP_INIT_TAKE1("LDAPSharedCacheFile", util_ldap_set_cache_file, NULL, RSRC_CONF,
                  "Sets the file of the shared memory cache."
                  "Nothing means disable the shared memory cache."),

    AP_INIT_TAKE1("LDAPCacheEntries", util_ldap_set_cache_entries, NULL, RSRC_CONF,
                  "Sets the maximum number of entries that are possible in the LDAP "
                  "search cache. "
                  "Zero means no limit; -1 disables the cache. Defaults to 1024 entries."),

    AP_INIT_TAKE1("LDAPCacheTTL", util_ldap_set_cache_ttl, NULL, RSRC_CONF,
                  "Sets the maximum time (in seconds) that an item can be cached in the LDAP "
                  "search cache. Zero means no limit. Defaults to 600 seconds (10 minutes)."),

    AP_INIT_TAKE1("LDAPOpCacheEntries", util_ldap_set_opcache_entries, NULL, RSRC_CONF,
                  "Sets the maximum number of entries that are possible in the LDAP "
                  "compare cache. "
                  "Zero means no limit; -1 disables the cache. Defaults to 1024 entries."),

    AP_INIT_TAKE1("LDAPOpCacheTTL", util_ldap_set_opcache_ttl, NULL, RSRC_CONF,
                  "Sets the maximum time (in seconds) that an item is cached in the LDAP "
                  "operation cache. Zero means no limit. Defaults to 600 seconds (10 minutes)."),

    AP_INIT_TAKE1("LDAPTrustedCA", util_ldap_set_cert_auth, NULL, RSRC_CONF,
                  "Sets the file containing the trusted Certificate Authority certificate. "
                  "Used to validate the LDAP server certificate for SSL connections."),

    AP_INIT_TAKE1("LDAPTrustedCAType", util_ldap_set_cert_type, NULL, RSRC_CONF,
                 "Specifies the type of the Certificate Authority file.  "
                 "The following types are supported:  "
                 "    DER_FILE      - file in binary DER format "
                 "    BASE64_FILE   - file in Base64 format "
                 "    CERT7_DB_PATH - Netscape certificate database file "),

    AP_INIT_TAKE1("LDAPConnectionTimeout", util_ldap_set_connection_timeout, NULL, RSRC_CONF,
                  "Specifies the LDAP socket connection timeout in seconds. "
                  "Default is 10 seconds. "),

    {NULL}
};

static void util_ldap_register_hooks(apr_pool_t *p)
{
    ap_hook_post_config(util_ldap_post_config,NULL,NULL,APR_HOOK_MIDDLE);
    ap_hook_handler(util_ldap_handler, NULL, NULL, APR_HOOK_MIDDLE);
    ap_hook_child_init(util_ldap_child_init, NULL, NULL, APR_HOOK_MIDDLE);
}

module ldap_module = {
   STANDARD20_MODULE_STUFF,
   NULL,				/* dir config creater */
   NULL,				/* dir merger --- default is to override */
   util_ldap_create_config,		/* server config */
   NULL,				/* merge server config */
   util_ldap_cmds,			/* command table */
   util_ldap_register_hooks,		/* set up request processing hooks */
};
