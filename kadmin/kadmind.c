/*
 * Copyright (c) 1997, 1998 Kungliga Tekniska H�gskolan
 * (Royal Institute of Technology, Stockholm, Sweden). 
 * All rights reserved. 
 *
 * Redistribution and use in source and binary forms, with or without 
 * modification, are permitted provided that the following conditions 
 * are met: 
 *
 * 1. Redistributions of source code must retain the above copyright 
 *    notice, this list of conditions and the following disclaimer. 
 *
 * 2. Redistributions in binary form must reproduce the above copyright 
 *    notice, this list of conditions and the following disclaimer in the 
 *    documentation and/or other materials provided with the distribution. 
 *
 * 3. All advertising materials mentioning features or use of this software 
 *    must display the following acknowledgement: 
 *      This product includes software developed by Kungliga Tekniska 
 *      H�gskolan and its contributors. 
 *
 * 4. Neither the name of the Institute nor the names of its contributors 
 *    may be used to endorse or promote products derived from this software 
 *    without specific prior written permission. 
 *
 * THIS SOFTWARE IS PROVIDED BY THE INSTITUTE AND CONTRIBUTORS ``AS IS'' AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE 
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE 
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE INSTITUTE OR CONTRIBUTORS BE LIABLE 
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL 
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS 
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) 
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT 
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY 
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF 
 * SUCH DAMAGE. 
 */

#include "kadmin_locl.h"

RCSID("$Id$");

static char *config_file;
static char *keyfile;
static char *keytab_str;
static int help_flag;
static int version_flag;
static int debug_flag;
static int debug_port;
static char *realm;

static struct getargs args[] = {
    { 
	"config-file",	'c',	arg_string,	&config_file, 
	"location of config file",	"file" 
    },
    {
	"key-file",	'k',	arg_string, &keyfile, 
	"location of master key file", "file"
    },
    {
	"keytab",	0,	arg_string, &keytab_str,
	"what keytab to use", "keytab"
    },
    {	"realm",	'r',	arg_string,   &realm, 
	"realm to use", "realm" 
    },
    {	"debug",	'd',	arg_flag,   &debug_flag, 
	"enable debugging" 
    },
    {	"debug-port",	'p',	arg_integer,&debug_port, 
	"port to use with debug", "port" },
    {	"help",		'h',	arg_flag,   &help_flag },
    {	"version",	'v',	arg_flag,   &version_flag }
};

static int num_args = sizeof(args) / sizeof(args[0]);

krb5_context context;
void *kadm_handle;

static void
usage(int ret)
{
    arg_printusage (args, num_args, "");
    exit (ret);
}

int
main(int argc, char **argv)
{
    krb5_error_code ret;
    krb5_config_section *cf;
    int optind = 0;
    int e;
    krb5_log_facility *logf;
    krb5_keytab keytab;

    set_progname(argv[0]);

    krb5_init_context(&context);

    ret = krb5_openlog(context, "kadmind", &logf);
    ret = krb5_set_warn_dest(context, logf);

    while((e = getarg(args, num_args, argc, argv, &optind)))
	warnx("error at argument `%s'", argv[optind]);

    if (help_flag)
	usage (0);

    if (version_flag)
	krb5_errx(context, 0, "%s", heimdal_version);

    argc -= optind;
    argv += optind;

    if (config_file == NULL)
	config_file = HDB_DB_DIR "/kdc.conf";

    if(krb5_config_parse_file(config_file, &cf) == 0) {
	const char *p = krb5_config_get_string (context, cf, 
						"kdc", "key-file", NULL);
	if (p)
	    keyfile = strdup(p);
    }

    if(keytab_str == NULL)
	keytab = NULL;
    else {
	ret = krb5_kt_resolve(context, keytab_str, &keytab);
	if(ret)
	    krb5_err(context, 1, ret, "krb5_kt_resolve");
    }

    {
	krb5_principal server;
	int fd = 0;
	krb5_auth_context ac = NULL;
	krb5_ticket *ticket;
	char *client;
	if(debug_flag){
	    if(debug_port == 0)
		debug_port = krb5_getportbyname (context, "kerberos-adm", 
						 "tcp", 749);
	    mini_inetd(debug_port);
	}
	if(realm)
	    krb5_set_default_realm(context, realm); /* XXX */
	krb5_parse_name(context, KADM5_ADMIN_SERVICE, &server);
	ret = krb5_recvauth(context, &ac, &fd, KADMIN_APPL_VERSION, 
			    server, 0, keytab, &ticket);
	krb5_free_principal(context, server);
	
	if(ret)
	    krb5_err(context, 1, ret, "krb5_recvauth");
	krb5_unparse_name(context, ticket->client, &client);
	
	ret = kadm5_init_with_password_ctx(context, 
					   client, 
					   "password", 
					   "service",
					   NULL, 0, 0, 
					   &kadm_handle);
	
	while(1){
	    unsigned char buf[1024];
	    krb5_storage *sp;

	    sp = krb5_storage_from_mem(buf, sizeof(buf));
	    ret = kadm5_server_recv_sp(context, ac, sp, fd);
	    if(ret)
		krb5_err(context, 1, ret, "kadm5_server_recv");
	    kadmind_dispatch(kadm_handle, sp);
	    ret = kadm5_server_send_sp(context, ac, sp, fd);
	    krb5_storage_free(sp);
	}
    }
}
