/* $7F track-table source. See ws_source.c. */
#ifndef WS_SOURCE_H
#define WS_SOURCE_H

#include <stdint.h>

int WsSourceValid(void);
int WsSourceAnchorCount(void);
int32_t WsSourceAnchorX(int i);
int32_t WsSourceAnchorY(int i);
int WsSourceCell(int32_t tx, int32_t ty, uint16_t *out);
int WsSourceAgree(int32_t wx, int32_t wy, uint16_t *out);
void WsSourceNoteServed(void);
void WsSourceFrame(void);
void WsSourceInvalidate(void);

#endif /* WS_SOURCE_H */
