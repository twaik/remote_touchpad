#include <sys/socket.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <sys/stat.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <unistd.h>
#include <dirent.h>
#include <signal.h>

#ifdef EVENT__HAVE_SYS_UN_H
#include <sys/un.h>
#endif
#ifdef EVENT__HAVE_AFUNIX_H
#include <afunix.h>
#endif

#include <event2/event.h>
#include <event2/http.h>
#include <event2/listener.h>
#include <event2/buffer.h>
#include <event2/util.h>
#include <event2/keyvalq_struct.h>


#ifdef EVENT__HAVE_NETINET_IN_H
#include <netinet/in.h>
# ifdef _XOPEN_SOURCE_EXTENDED
#  include <arpa/inet.h>
# endif
#endif

#define INCBIN_STYLE INCBIN_STYLE_SNAKE
#define INCBIN_PREFIX 
#include "incbin.h"

INCBIN(index_html, "index.html");
void process_input(char* payload);

/* Callback used for the /dump URI, and for every non-GET request:
 * dumps all information to stdout and gives back a trivial 200 ok */
static void
dump_request_cb(struct evhttp_request *req, void *arg)
{
	const char *cmdtype;
	struct evkeyvalq *headers;
	struct evkeyval *header;
	struct evbuffer *buf;

	switch (evhttp_request_get_command(req)) {
	case EVHTTP_REQ_GET: cmdtype = "GET"; break;
	case EVHTTP_REQ_POST: cmdtype = "POST"; break;
	case EVHTTP_REQ_HEAD: cmdtype = "HEAD"; break;
	case EVHTTP_REQ_PUT: cmdtype = "PUT"; break;
	case EVHTTP_REQ_DELETE: cmdtype = "DELETE"; break;
	case EVHTTP_REQ_OPTIONS: cmdtype = "OPTIONS"; break;
	case EVHTTP_REQ_TRACE: cmdtype = "TRACE"; break;
	case EVHTTP_REQ_CONNECT: cmdtype = "CONNECT"; break;
	case EVHTTP_REQ_PATCH: cmdtype = "PATCH"; break;
	default: cmdtype = "unknown"; break;
	}

	printf("Received a %s request for %s\nHeaders:\n",
	    cmdtype, evhttp_request_get_uri(req));

	headers = evhttp_request_get_input_headers(req);
	for (header = headers->tqh_first; header;
	    header = header->next.tqe_next) {
		printf("  %s: %s\n", header->key, header->value);
	}

	buf = evhttp_request_get_input_buffer(req);
	puts("Input data: <<<");
	while (evbuffer_get_length(buf)) {
		int n;
		char cbuf[128];
		n = evbuffer_remove(buf, cbuf, sizeof(cbuf));
		if (n > 0)
			(void) fwrite(cbuf, 1, n, stdout);
	}
	puts(">>>");

	evhttp_send_reply(req, 200, "OK", NULL);
}

static void
input_cb(struct evhttp_request *req, void *arg)
{
	if (evhttp_request_get_command(req) != EVHTTP_REQ_POST)
		evhttp_send_reply(req, 500, "Internal Server Error", NULL);

	struct evbuffer *buf = evhttp_request_get_input_buffer(req);
	char payload[2048] = {0};
	int n = 0;
	n = evbuffer_remove(buf, payload, sizeof(payload));
	payload[n] = 0;
	//printf("Input data: >>>\n%s\n<<<", payload);
	evhttp_send_reply(req, 200, "OK", NULL);
	process_input(payload);
}

static void
manifest_json_cb(struct evhttp_request *req, void *arg)
{
	if (evhttp_request_get_command(req) != EVHTTP_REQ_GET)
		evhttp_send_reply(req, 500, "Internal Server Error", NULL);

	const char *data = 
		"{\n"
		"\t\"manifest_version\": 3, \n"
		"\t\"name\": \"Remote Touchpad\", \n"
		"\t\"version\": \"1.0\" \n"
		"}\n";
	printf("Manifest requested\n");
	struct evbuffer *evb = NULL;
	evb = evbuffer_new();
	evbuffer_add(evb, data, strlen(data));
	evhttp_send_reply(req, 200, "OK", evb);
}

static void
index_cb(struct evhttp_request *req, void *arg)
{
	if (evhttp_request_get_command(req) != EVHTTP_REQ_GET)
		evhttp_send_reply(req, 500, "Internal Server Error", NULL);

	printf("Index page requested\n");
	struct evbuffer *evb = NULL;
	evb = evbuffer_new();
	evbuffer_add(evb, index_html_data, index_html_size);
	evhttp_send_reply(req, 200, "OK", evb);
}

/* This callback gets invoked when we get any http request that doesn't match
 * any other callback.  Like any evhttp server callback, it has a simple job:
 * it must eventually call evhttp_send_error() or evhttp_send_reply().
 */
static void
send_document_cb(struct evhttp_request *req, void *arg)
{
	evhttp_send_error(req, HTTP_NOTFOUND, NULL);
}

static void
do_term(int sig, short events, void *arg)
{
	struct event_base *base = arg;
	event_base_loopbreak(base);
	fprintf(stderr, "Got %i, Terminating\n", sig);
}

int
main(int argc, char **argv)
{
	struct event_config *cfg = NULL;
	struct event_base *base = NULL;
	struct evhttp *http = NULL;
	struct event *term = NULL;
	int ret = 0;

	if (signal(SIGPIPE, SIG_IGN) == SIG_ERR) {
		ret = 1;
		goto err;
	}

	setbuf(stdout, NULL);
	setbuf(stderr, NULL);

	/** Read env like in regress */
	if (getenv("EVENT_DEBUG_LOGGING_ALL"))
		event_enable_debug_logging(EVENT_DBG_ALL);

	cfg = event_config_new();

	base = event_base_new_with_config(cfg);
	if (!base) {
		fprintf(stderr, "Couldn't create an event_base: exiting\n");
		ret = 1;
	}
	event_config_free(cfg);
	cfg = NULL;

	/* Create a new evhttp object to handle requests. */
	http = evhttp_new(base);
	if (!http) {
		fprintf(stderr, "couldn't create evhttp. Exiting.\n");
		ret = 1;
	}

	evhttp_set_cb(http, "/", index_cb, NULL);
	evhttp_set_cb(http, "/manifest.json", manifest_json_cb, NULL);
	evhttp_set_cb(http, "/input", input_cb, NULL);
	evhttp_set_cb(http, "/dump", dump_request_cb, NULL);
	//evhttp_set_gencb(http, send_document_cb, NULL);
	evhttp_set_gencb(http, dump_request_cb, NULL);

	if (!evhttp_bind_socket_with_handle(http, NULL, 8080)) {
		fprintf(stderr, "couldn't bind to %d. Exiting.\n", 8080);
		ret = 1;
		goto err;
	}

	term = evsignal_new(base, SIGINT, do_term, base);
	if (!term)
		goto err;
	if (event_add(term, NULL))
		goto err;

	printf("Started on port 8080\n");
	event_base_dispatch(base);

err:
	if (cfg)
		event_config_free(cfg);
	if (http)
		evhttp_free(http);
	if (term)
		event_free(term);
	if (base)
		event_base_free(base);

	return ret;
}
 
