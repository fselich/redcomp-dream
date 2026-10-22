#ifndef RNET_OPEN_URL_H
#define RNET_OPEN_URL_H

/* Launch an HTTP(S) URL in the host browser. Returns 0 if every available
 * opener fails; a command that remains active past the short startup window
 * counts as launched. This cannot verify that the page rendered. */
int rnet_open_url(const char *url);

#endif
