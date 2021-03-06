
/*
 * Copyright (C) hongzhidao
 */


#include <ngx_http_ctrl.h>


static void ngx_http_ctrl_cleanup(void *data);
static ngx_int_t ngx_http_ctrl_init_module(ngx_cycle_t *cycle);
static void ngx_http_ctrl_notify_cleanup(void *data);
static ngx_int_t ngx_http_ctrl_init_process(ngx_cycle_t *cycle);
static ngx_int_t ngx_http_ctrl_init(ngx_conf_t *cf);
static void *ngx_http_ctrl_create_main_conf(ngx_conf_t *cf);
static char *ngx_http_ctrl_init_main_conf(ngx_conf_t *cf, void *conf);
static void *ngx_http_ctrl_create_loc_conf(ngx_conf_t *cf);
static char *ngx_http_ctrl_merge_loc_conf(ngx_conf_t *cf, void *parent,
    void *child);
static char *ngx_http_ctrl_zone(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_ctrl_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_ctrl_config(ngx_conf_t *cf, ngx_command_t *cmd, void *conf);
static char *ngx_http_ctrl_stats_display(ngx_conf_t *cf, ngx_command_t *cmd,void *conf);


static ngx_command_t  ngx_http_ctrl_commands[] = {

    { ngx_string("ctrl_zone"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_http_ctrl_zone,
      0,
      0,
      NULL },

    { ngx_string("ctrl_state"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE1,
      ngx_conf_set_str_slot,
      NGX_HTTP_MAIN_CONF_OFFSET,
      offsetof(ngx_http_ctrl_main_conf_t, state),
      NULL },

    { ngx_string("ctrl_set"),
      NGX_HTTP_MAIN_CONF|NGX_CONF_TAKE2,
      ngx_http_ctrl_set,
      NGX_HTTP_MAIN_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("ctrl"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ctrl_loc_conf_t, conf_enable),
      NULL },

    { ngx_string("ctrl_config"),
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
      ngx_http_ctrl_config,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

    { ngx_string("ctrl_stats"),
      NGX_HTTP_MAIN_CONF|NGX_HTTP_SRV_CONF|NGX_HTTP_LOC_CONF|NGX_CONF_FLAG,
      ngx_conf_set_flag_slot,
      NGX_HTTP_LOC_CONF_OFFSET,
      offsetof(ngx_http_ctrl_loc_conf_t, stats_enable),
      NULL },

    { ngx_string("ctrl_stats_display"),
      NGX_HTTP_LOC_CONF|NGX_CONF_NOARGS,
      ngx_http_ctrl_stats_display,
      NGX_HTTP_LOC_CONF_OFFSET,
      0,
      NULL },

      ngx_null_command
};


static ngx_http_module_t  ngx_http_ctrl_module_ctx = {
    NULL,                           /* preconfiguration */
    ngx_http_ctrl_init,             /* postconfiguration */

    ngx_http_ctrl_create_main_conf, /* create main configuration */
    ngx_http_ctrl_init_main_conf,   /* init main configuration */

    NULL,                           /* create server configuration */
    NULL,                           /* merge server configuration */

    ngx_http_ctrl_create_loc_conf,  /* create location configuration */
    ngx_http_ctrl_merge_loc_conf    /* merge location configuration */
};


ngx_module_t  ngx_http_ctrl_module = {
    NGX_MODULE_V1,
    &ngx_http_ctrl_module_ctx,      /* module context */
    ngx_http_ctrl_commands,         /* module directives */
    NGX_HTTP_MODULE,                /* module type */
    NULL,                           /* init master */
    ngx_http_ctrl_init_module,      /* init module */
    ngx_http_ctrl_init_process,     /* init process */
    NULL,                           /* init thread */
    NULL,                           /* exit thread */
    NULL,                           /* exit process */
    NULL,                           /* exit master */
    NGX_MODULE_V1_PADDING
};


static ngx_http_output_header_filter_pt  ngx_http_next_header_filter;


static ngx_int_t
ngx_http_ctrl_rewrite_handler(ngx_http_request_t *r)
{
    ngx_int_t                  rc;
    ngx_http_ctrl_ctx_t       *ctx;
    ngx_http_ctrl_loc_conf_t  *clcf;

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_ctrl_module);

    if (!(clcf->conf_enable || clcf->stats_enable)) {
        return NGX_DECLINED;
    }

    ctx = ngx_http_ctrl_get_ctx(r);

    if (ctx == NULL) {
        return NGX_DECLINED;
    }

    if (clcf->conf_enable) {
        rc = ngx_http_ctrl_request_init(r);
        if (rc == NGX_ERROR) {
            return rc;
        }

        if (ctx->action != NULL) {
            rc = ngx_http_ctrl_set_variables(r, ctx->action->variables);
            if (rc == NGX_ERROR) {
                return rc;
            }
        }
    }

    return NGX_DECLINED;
}


ngx_http_ctrl_ctx_t *
ngx_http_ctrl_get_ctx(ngx_http_request_t *r)
{
    nxt_mp_t                     *mp;
    ngx_pool_cleanup_t           *cln;
    ngx_http_ctrl_ctx_t          *ctx;

    if (r != r->main) {
        return NULL;
    }

    ctx = ngx_http_get_module_ctx(r, ngx_http_ctrl_module);

    if (ctx != NULL) {
        return ctx;
    }

    mp = nxt_mp_create(4096, 128, 1024, 64);
    if (mp == NULL) {
        return NULL;
    }

    ctx = ngx_pcalloc(r->pool, sizeof(ngx_http_ctrl_ctx_t));
    if (ctx == NULL) {
        goto fail;
    }

    ngx_http_set_ctx(r, ctx, ngx_http_ctrl_module);

    ctx->mem_pool = mp;
    ctx->request = r;

    cln = ngx_pool_cleanup_add(r->pool, 0);
    if (cln == NULL) {
        goto fail;
    }

    cln->handler = ngx_http_ctrl_cleanup;
    cln->data = ctx;

    return ctx;

fail:

    nxt_mp_destroy(mp);

    return NULL;
}


static void
ngx_http_ctrl_cleanup(void *data)
{
    ngx_http_ctrl_ctx_t *ctx = data;

    ngx_rbtree_node_t                *node;
    ngx_http_ctrl_shctx_t            *shctx;
    ngx_http_ctrl_main_conf_t        *cmcf;
    ngx_http_ctrl_limit_conn_node_t  *cn;

    if (ctx->http_conf) {
        ngx_http_conf_release(ctx->http_conf);
    }

    if (ctx->node) {
        node = ctx->node;
        cn = (ngx_http_ctrl_limit_conn_node_t *) &node->color;

        cmcf = ngx_http_get_module_main_conf(ctx->request, ngx_http_ctrl_module);
        shctx = cmcf->shm_zone->data;

        ngx_shmtx_lock(&shctx->shpool->mutex);

        cn->conn--;

        if (cn->conn == 0) {
            ngx_rbtree_delete(&shctx->sh->limit_conn_rbtree, node);
            ngx_slab_free_locked(shctx->shpool, node);
        }

        ngx_shmtx_unlock(&shctx->shpool->mutex);
    }

    nxt_mp_destroy(ctx->mem_pool);
}


static ngx_int_t
ngx_http_ctrl_preaccess_handler(ngx_http_request_t *r)
{
    ngx_int_t                   rc;
    ngx_http_ctrl_ctx_t        *ctx;
    ngx_http_ctrl_loc_conf_t   *clcf;

    ctx = ngx_http_ctrl_get_ctx(r);
    clcf = ngx_http_get_module_loc_conf(r, ngx_http_ctrl_module);

    if (ctx == NULL || ctx->action == NULL) {
        return NGX_DECLINED;
    }

    if (clcf->conf_enable) {

        if (ctx->action->limit_rate > 0) {
            r->limit_rate = ctx->action->limit_rate;
            r->limit_rate_set = 1;
        }

        if (ctx->action->limit_conn) {
            rc = ngx_http_ctrl_limit_conn(r, ctx->action->limit_conn);
            if (rc != NGX_DECLINED) {
                return rc;
            }
        }

        if (ctx->action->limit_req) {
            rc = ngx_http_ctrl_limit_req(r, ctx->action->limit_req);
            if (rc != NGX_DECLINED) {
                return rc;
            }
        }
    }

    return NGX_DECLINED;
}


static ngx_int_t
ngx_http_ctrl_access_handler(ngx_http_request_t *r)
{
    ngx_int_t                  rc;
    ngx_http_ctrl_ctx_t       *ctx;
    ngx_http_ctrl_loc_conf_t  *clcf;

    ctx = ngx_http_ctrl_get_ctx(r);
    clcf = ngx_http_get_module_loc_conf(r, ngx_http_ctrl_module);

    if (ctx == NULL || ctx->action == NULL) {
        return NGX_DECLINED;
    }

    if (clcf->conf_enable) {

        if (ctx->action->blacklist) {
            rc = ngx_http_ctrl_blacklist(r, ctx->action->blacklist);
            if (rc == NGX_OK) {
                return NGX_HTTP_FORBIDDEN;
            }
        }

        if (ctx->action->whitelist) {
            rc = ngx_http_ctrl_whitelist(r, ctx->action->whitelist);
            if (rc != NGX_OK) {
                return NGX_HTTP_FORBIDDEN;
            }
        }
    }

    return NGX_DECLINED;
}


static ngx_int_t
ngx_http_ctrl_precontent_handler(ngx_http_request_t *r)
{
    ngx_int_t                   rc;
    ngx_str_t                  *text;
    ngx_uint_t                 status;
    ngx_http_action_t         *action;
    ngx_http_ctrl_ctx_t       *ctx;
    ngx_http_complex_value_t   cv;
    ngx_http_ctrl_loc_conf_t  *clcf;

    ctx = ngx_http_ctrl_get_ctx(r);
    clcf = ngx_http_get_module_loc_conf(r, ngx_http_ctrl_module);

    if (ctx == NULL || ctx->action == NULL) {
        return NGX_DECLINED;
    }

    if (clcf->conf_enable) {
        action = ctx->action;

        status = action->return_status;

        if (status) {
            if (status < NGX_HTTP_BAD_REQUEST
                || action->return_text.length)
            {
                ngx_memzero(&cv, sizeof(ngx_http_complex_value_t));

                text = &cv.value;

                if (status == NGX_HTTP_MOVED_PERMANENTLY
                    || status == NGX_HTTP_MOVED_TEMPORARILY
                    || status == NGX_HTTP_SEE_OTHER
                    || status == NGX_HTTP_TEMPORARY_REDIRECT
                    || status == NGX_HTTP_PERMANENT_REDIRECT)
                {
                    text->len = action->return_location.length;
                    text->data = action->return_location.start;

                } else {
                    text->len = action->return_text.length;
                    text->data = action->return_text.start;
                }

                rc = ngx_http_send_response(r, action->return_status,
                                            NULL, &cv);

                ngx_http_finalize_request(r, rc);

                return NGX_DONE;

            } else {
                return action->return_status;
            }
        }
    }

    return NGX_DECLINED;
}


static ngx_int_t
ngx_http_ctrl_header_filter(ngx_http_request_t *r)
{
    ngx_http_ctrl_ctx_t           *ctx;
    ngx_http_ctrl_loc_conf_t      *clcf;

    ctx = ngx_http_ctrl_get_ctx(r);

    if (ctx == NULL) {
        return ngx_http_next_header_filter(r);
    }

    clcf = ngx_http_get_module_loc_conf(r, ngx_http_ctrl_module);

    if (clcf->stats_enable) {
        ngx_http_ctrl_stats_code(r);
    }

    return ngx_http_next_header_filter(r);
}


ngx_int_t
ngx_http_ctrl_response(ngx_http_request_t *r, nxt_uint_t status,
    nxt_str_t *body)
{
    ngx_int_t             rc;
    ngx_buf_t            *b;
    ngx_chain_t           out;

    b = ngx_pcalloc(r->pool, sizeof(ngx_buf_t));
    if (b == NULL) {
        return NGX_HTTP_INTERNAL_SERVER_ERROR;
    }

    b->pos = body->start;
    b->last = b->pos + body->length;
    b->memory = 1;
    b->last_buf = 1;

    r->headers_out.content_type.len = sizeof("text/plain") - 1;
    r->headers_out.content_type.data = (u_char *) "text/plain";

    r->headers_out.status = status;
    r->headers_out.content_length_n = b->last - b->pos;

    rc = ngx_http_send_header(r);

    if (rc == NGX_ERROR || rc > NGX_OK || r->header_only) {
        return rc;
    }

    out.buf = b;
    out.next = NULL;

    return ngx_http_output_filter(r, &out);
}


static ngx_int_t
ngx_http_ctrl_init_module(ngx_cycle_t *cycle)
{
    int                         value;
    size_t                      buffer_size;
    socklen_t                   olen;
    nxt_str_t                   error;
    ngx_str_t                   log;
    ngx_uint_t                  n, nfd;
    ngx_socket_t               *fd, fds[2];
    ngx_core_conf_t            *ccf;
    ngx_pool_cleanup_t         *cln;
    ngx_http_ctrl_main_conf_t  *cmcf;

    cmcf = ngx_http_cycle_get_module_main_conf(cycle, ngx_http_ctrl_module);

    if (cmcf == NULL) {
        return NGX_OK;
    }

    cmcf->file.name = cmcf->state.data;

    nxt_memzero(&error, sizeof(nxt_str_t));

    if (ngx_http_conf_start(cycle, &cmcf->file, &error) != NGX_OK) {
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0,
                      "http conf start failed.");
        return NGX_ERROR;
    }

    if (error.length > 0) {
        log.len = error.length;
        log.data = error.start;
        ngx_log_error(NGX_LOG_ALERT, ngx_cycle->log, 0,
                      "invalid http conf start: %V", &log);
    }

    ccf = (ngx_core_conf_t *) ngx_get_conf(cycle->conf_ctx, ngx_core_module);

    nfd = ccf->worker_processes;

    fd = ngx_palloc(cycle->pool, nfd * 2 * sizeof(ngx_socket_t));
    if (fd == NULL) {
        return NGX_ERROR;
    }

    cmcf->read_fd = fd;
    cmcf->write_fd = fd + nfd;

    cln = ngx_pool_cleanup_add(cycle->pool, 0);
    if (cln == NULL) {
        return NGX_ERROR;
    }

    cln->handler = ngx_http_ctrl_notify_cleanup;
    cln->data = cmcf;

    buffer_size = 0;

    for (n = 0; n < nfd; n++) {
        if (socketpair(AF_UNIX,
#if !defined(SOCK_SEQPACKET) || (NGX_DARWIN)
                       SOCK_DGRAM,
#else
                       SOCK_SEQPACKET,
#endif
                       0, fds))
        {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_socket_errno,
                          "socketpair() failed");
            return NGX_ERROR;
        }

        ngx_log_error(NGX_LOG_INFO, cycle->log, 0,
                       "http ctrl socketpair w:%d, %d<-%d",
                       n, (int) fds[0], (int) fds[1]);

        cmcf->read_fd[n] = fds[0];
        cmcf->write_fd[n] = fds[1];

        cmcf->nfd++;

        if (ngx_nonblocking(fds[0]) == -1
            || ngx_nonblocking(fds[1]) == -1)
        {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_socket_errno,
                          ngx_nonblocking_n " failed");
            return NGX_ERROR;
        }

        if (buffer_size == 0) {
            olen = sizeof(int);

            if (getsockopt(fds[1], SOL_SOCKET, SO_SNDBUF, &value, &olen) == -1) {
                ngx_log_error(NGX_LOG_CRIT, cycle->log, ngx_socket_errno,
                              "getsockopt(SO_SNDBUF) ctrl notify failed");
                return NGX_ERROR;
            }

            if (value <= 64) {
                ngx_log_error(NGX_LOG_CRIT, cycle->log, 0,
                              "too small ctrl notify buffer");
                return NGX_ERROR;
            }

            /*
             * On BSD/Macos buffer_size is the full socket queue length.
             * It should not exceed (net.local.dgram.recvspace - 16).
             */

            buffer_size = value;

            #if (NGX_LINUX)
            /*
             * On Linux buffer_size is max length of a single packet.
             * It is 32 bytes less than SO_SNDBUF returned by setsockopt().
             */

            buffer_size -= 32;
#endif

            if (buffer_size < sizeof(nxt_port_msg_t)) {
                ngx_log_error(NGX_LOG_CRIT, cycle->log, 0,
                              "too small ctrl notify buffer");
                return NGX_ERROR;
            }

            ngx_log_error(NGX_LOG_NOTICE, cycle->log, 0,
                          "ctrl notify buffer size:%uz", buffer_size);
        }
    }

    cmcf->buf = ngx_create_temp_buf(cycle->pool, buffer_size);
    if (cmcf->buf == NULL) {
        return NGX_ERROR;
    }

    return NGX_OK;
}


static void
ngx_http_ctrl_notify_cleanup(void *data)
{
    ngx_http_ctrl_main_conf_t *cmcf = data;

    ngx_uint_t  n;

    if (cmcf->read_fd && cmcf->write_fd) {
        for (n = 0; n < cmcf->nfd; n++) {
            (void) ngx_close_socket(cmcf->read_fd[n]);
            (void) ngx_close_socket(cmcf->write_fd[n]);
        }
    }
}


static ngx_int_t
ngx_http_ctrl_init_process(ngx_cycle_t *cycle)
{
    ngx_int_t                   event;
    ngx_uint_t                  n;
    ngx_event_t                *rev, *wev;
    ngx_socket_t                fd;
    ngx_connection_t           *c;
    ngx_http_ctrl_main_conf_t  *cmcf;

    if (ngx_process != NGX_PROCESS_WORKER) {
        return NGX_OK;
    }

    cmcf = ngx_http_cycle_get_module_main_conf(cycle, ngx_http_ctrl_module);

    if (cmcf == NULL || cmcf->nfd == 0) {
        return NGX_OK;
    }

    fd = NGX_INVALID_FILE;

    for (n = 0; n < cmcf->nfd; n++) {
        if (n == ngx_worker) {
            fd = cmcf->read_fd[n];
            continue;
        }

        if (ngx_close_socket(cmcf->read_fd[n]) == NGX_FILE_ERROR) {
            ngx_log_error(NGX_LOG_ALERT, cycle->log, ngx_socket_errno,
                          ngx_close_socket_n " ctrl_notify_socket[%ui] failed",
                          n);
        }
    }

    if (fd == NGX_INVALID_FILE) {
        ngx_log_error(NGX_LOG_ALERT, cycle->log, 0,
                      "worker has no ctrl notify socket");
        return NGX_ERROR;
    }

    event = (ngx_event_flags & NGX_USE_CLEAR_EVENT) ? NGX_CLEAR_EVENT
                                                    : NGX_LEVEL_EVENT;

    c = ngx_get_connection(fd, cycle->log);
    if (c == NULL) {
        return NGX_ERROR;
    }

    c->recv = ngx_udp_recv;
    c->data = cmcf;

    rev = c->read;
    rev->channel = 1;
    rev->log = cycle->log;
    rev->handler = ngx_http_ctrl_notify_read_handler;

    if (ngx_add_event(rev, NGX_READ_EVENT, event) != NGX_OK) {
        return NGX_ERROR;
    }

    cmcf->conn = ngx_pcalloc(ngx_cycle->pool,
                             cmcf->nfd * sizeof(ngx_connection_t *));
    if (cmcf->conn == NULL) {
        return NGX_ERROR;
    }

    for (n = 0; n < cmcf->nfd; n++) {
        if (n == ngx_worker) {
            continue;
        }

        c = ngx_get_connection(cmcf->write_fd[n], cycle->log);
        if (c == NULL) {
            return NGX_ERROR;
        }

        cmcf->conn[n] = c;

        c->send = ngx_send;
        c->data = NULL;

        rev = c->read;
        rev->channel = 1;

        wev = c->write;
        wev->log = cycle->log;
        wev->handler = ngx_http_ctrl_notify_write_handler;

        if (ngx_add_event(wev, NGX_WRITE_EVENT, event) != NGX_OK) {
            return NGX_ERROR;
        }
    }

    cmcf->read_fd = NULL;
    cmcf->write_fd = NULL;

    return NGX_OK;
}


static void *
ngx_http_ctrl_create_main_conf(ngx_conf_t *cf)
{
    ngx_http_ctrl_main_conf_t  *cmcf;

    cmcf = ngx_pcalloc(cf->pool, sizeof(ngx_http_ctrl_main_conf_t));
    if (cmcf == NULL) {
        return NULL;
    }

    /*
     * set by ngx_pcalloc()
     *
     *     cmcf->write_fd = NULL;
     *     cmcf->read_fd = NULL;
     *     cmcf->nfd = 0;
     *     cmcf->buf = NULL;
     *     cmcf->shm_zone = NULL;
     */

    return cmcf;
}


static char *
ngx_http_ctrl_init_main_conf(ngx_conf_t *cf, void *conf)
{
    ngx_http_ctrl_main_conf_t *cmcf = conf;

    if (cmcf->state.data == NULL) {
        ngx_str_set(&cmcf->state, "conf.json");
    }

    ngx_conf_full_name(cf->cycle, &cmcf->state, 1);

    return NGX_CONF_OK;
}


static void *
ngx_http_ctrl_create_loc_conf(ngx_conf_t *cf)
{
    ngx_http_ctrl_loc_conf_t  *conf;

    conf = ngx_pcalloc(cf->pool, sizeof(ngx_http_ctrl_loc_conf_t));
    if (conf == NULL) {
        return NULL;
    }

    conf->conf_enable = NGX_CONF_UNSET;
    conf->stats_enable = NGX_CONF_UNSET;

    return conf;
}


static char *
ngx_http_ctrl_merge_loc_conf(ngx_conf_t *cf, void *parent, void *child)
{
    ngx_http_ctrl_loc_conf_t *prev = parent;
    ngx_http_ctrl_loc_conf_t *conf = child;

    ngx_http_ctrl_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_ctrl_module);

    ngx_conf_merge_value(conf->conf_enable, prev->conf_enable, 0);
    ngx_conf_merge_value(conf->stats_enable, prev->stats_enable, 0);

    if ((conf->conf_enable || conf->stats_enable)
        && cmcf->shm_zone == NULL)
    {
        return "require \"ctrl_zone\"";
    }

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_ctrl_init_zone(ngx_shm_zone_t *shm_zone, void *data)
{
    ngx_http_ctrl_shctx_t  *octx = data;

    size_t                  len;
    ngx_rbtree_node_t      *sentinel;
    ngx_http_ctrl_shctx_t  *ctx;

    ctx = shm_zone->data;

    if (octx) {
        ctx->sh = octx->sh;
        ctx->shpool = octx->shpool;

        return NGX_OK;
    }

    ctx->shpool = (ngx_slab_pool_t *) shm_zone->shm.addr;

    if (shm_zone->shm.exists) {
        ctx->sh = ctx->shpool->data;

        return NGX_OK;
    }

    ctx->sh = ngx_slab_calloc(ctx->shpool, sizeof(ngx_http_ctrl_shdata_t));
    if (ctx->sh == NULL) {
        return NGX_ERROR;
    }

    ctx->shpool->data = ctx->sh;

    sentinel = ngx_slab_alloc(ctx->shpool, sizeof(ngx_rbtree_node_t));
    if (sentinel == NULL) {
        return NGX_ERROR;
    }

    ngx_rbtree_init(&ctx->sh->limit_conn_rbtree, sentinel,
                    ngx_http_ctrl_limit_conn_rbtree_insert_value);

    sentinel = ngx_slab_alloc(ctx->shpool, sizeof(ngx_rbtree_node_t));
    if (sentinel == NULL) {
        return NGX_ERROR;
    }

    ngx_rbtree_init(&ctx->sh->limit_req_rbtree, sentinel,
                    ngx_http_ctrl_limit_req_rbtree_insert_value);

    ngx_queue_init(&ctx->sh->limit_req_queue);

    len = sizeof(" in ctrl_zone \"\"") + shm_zone->shm.name.len;

    ctx->shpool->log_ctx = ngx_slab_alloc(ctx->shpool, len);
    if (ctx->shpool->log_ctx == NULL) {
        return NGX_ERROR;
    }

    ngx_sprintf(ctx->shpool->log_ctx, " in ctrl_zone \"%V\"%Z",
                &shm_zone->shm.name);

    return NGX_OK;
}


static char *
ngx_http_ctrl_zone(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_ctrl_main_conf_t  *cmcf = conf;

    u_char                   *p;
    ssize_t                   size;
    ngx_str_t                *value, name, s;
    ngx_uint_t                i;
    ngx_shm_zone_t           *shm_zone;
    ngx_http_ctrl_shctx_t    *ctx;

    ctx = ngx_pcalloc(cf->pool, sizeof(ngx_http_ctrl_shctx_t));
    if (ctx == NULL) {
        return NGX_CONF_ERROR;
    }

    value = cf->args->elts;

    size = 0;
    name.len = 0;

    for (i = 1; i < cf->args->nelts; i++) {

        if (ngx_strncmp(value[i].data, "zone=", 5) == 0) {

            name.data = value[i].data + 5;

            p = (u_char *) ngx_strchr(name.data, ':');

            if (p == NULL) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "invalid zone size \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }

            name.len = p - name.data;

            s.data = p + 1;
            s.len = value[i].data + value[i].len - s.data;

            size = ngx_parse_size(&s);

            if (size == NGX_ERROR) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "invalid zone size \"%V\"", &value[i]);
                return NGX_CONF_ERROR;
            }

            if (size < (ssize_t) (8 * ngx_pagesize)) {
                ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                                   "zone \"%V\" is too small", &value[i]);
                return NGX_CONF_ERROR;
            }

            continue;
        }

        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid parameter \"%V\"", &value[i]);
        return NGX_CONF_ERROR;
    }

    if (name.len == 0) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "\"%V\" must have \"zone\" parameter",
                           &cmd->name);
        return NGX_CONF_ERROR;
    }

    shm_zone = ngx_shared_memory_add(cf, &name, size, &ngx_http_ctrl_module);
    if (shm_zone == NULL) {
        return NGX_CONF_ERROR;
    }

    if (shm_zone->data) {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "duplicate zone \"%V\"", &name);
        return NGX_CONF_ERROR;
    }

    shm_zone->init = ngx_http_ctrl_init_zone;
    shm_zone->data = ctx;

    cmcf->shm_zone = shm_zone;

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_ctrl_variable(ngx_http_request_t *r, ngx_http_variable_value_t *v,
    uintptr_t data)
{
    ngx_str_t *var = (ngx_str_t *) data;

    v->len = var->len;
    v->valid = 1;
    v->no_cacheable = 0;
    v->not_found = 0;
    v->data = var->data;

    return NGX_OK;
}


static char *
ngx_http_ctrl_set(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_int_t                   index;
    ngx_str_t                  *value, *var;
    ngx_http_variable_t        *v;

    value = cf->args->elts;

    if (value[1].data[0] != '$') {
        ngx_conf_log_error(NGX_LOG_EMERG, cf, 0,
                           "invalid variable name \"%V\"", &value[1]);
        return NGX_CONF_ERROR;
    }

    value[1].len--;
    value[1].data++;

    v = ngx_http_add_variable(cf, &value[1], NGX_HTTP_VAR_CHANGEABLE);
    if (v == NULL) {
        return NGX_CONF_ERROR;
    }

    index = ngx_http_get_variable_index(cf, &value[1]);
    if (index == NGX_ERROR) {
        return NGX_CONF_ERROR;
    }

    var = ngx_palloc(cf->pool, sizeof(ngx_str_t));
    if (var == NULL) {
        return NGX_CONF_ERROR;
    }

    *var = value[2];

    v->get_handler = ngx_http_ctrl_variable;
    v->data = (uintptr_t) var;

    return NGX_CONF_OK;
}


static char *
ngx_http_ctrl_config(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t  *clcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);

    clcf->handler = ngx_http_ctrl_config_handler;

    return NGX_CONF_OK;
}


static char *
ngx_http_ctrl_stats_display(ngx_conf_t *cf, ngx_command_t *cmd, void *conf)
{
    ngx_http_core_loc_conf_t   *clcf;
	ngx_http_ctrl_main_conf_t  *cmcf;

    clcf = ngx_http_conf_get_module_loc_conf(cf, ngx_http_core_module);
    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_ctrl_module);

    if (cmcf->shm_zone == NULL) {
        return "require \"ctrl_zone\"";
    }

    clcf->handler = ngx_http_ctrl_stats_handler;

    return NGX_CONF_OK;
}


static ngx_int_t
ngx_http_ctrl_init(ngx_conf_t *cf)
{
    ngx_http_handler_pt        *h;
    ngx_http_core_main_conf_t  *cmcf;

    cmcf = ngx_http_conf_get_module_main_conf(cf, ngx_http_core_module);

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_REWRITE_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_ctrl_rewrite_handler;    

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_PREACCESS_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_ctrl_preaccess_handler;

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_ACCESS_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_ctrl_access_handler;

    h = ngx_array_push(&cmcf->phases[NGX_HTTP_PRECONTENT_PHASE].handlers);
    if (h == NULL) {
        return NGX_ERROR;
    }

    *h = ngx_http_ctrl_precontent_handler;

    ngx_http_next_header_filter = ngx_http_top_header_filter;
    ngx_http_top_header_filter = ngx_http_ctrl_header_filter;

    return NGX_OK;
}
