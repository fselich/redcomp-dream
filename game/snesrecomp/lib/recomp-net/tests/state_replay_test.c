/* Replay actual captured UDP BEGIN packets across bidirectional transfers. */
#include "recomp_net/recomp_net.h"
#include "protocol/rnet_protocol.h"
#include "transport/rnet_transport.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <process.h>
#define test_pid _getpid
#else
#include <unistd.h>
#define test_pid getpid
#endif

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"%s:%d: %s\n",__FILE__,__LINE__,#x); exit(1); } } while (0)
static RNetTransport relay[2];
static RNetSession *peer[2];
static rnet_u64 clock_ms=1000;
static rnet_u8 captured[2][RNET_MAX_PACKET];
static int captured_size[2];
static rnet_u64 now_ms(void *ctx) { (void)ctx; return clock_ms; }
static void sample(rnet_u32 tick,RNetInputSample *out,void *ctx)
{ (void)tick; (void)ctx; memset(out,0,sizeof(*out)); out->valid=1; }
static void publish(rnet_u32 tick,const RNetInputSample *row,int count,void *ctx)
{ (void)tick; (void)row; (void)count; (void)ctx; }

static void forward_packets(void)
{
    int seat;
    for (seat=0;seat<2;seat++) {
        rnet_u8 buf[RNET_MAX_PACKET];
        int n;
        while ((n=rnet_transport_recv(&relay[seat],buf,sizeof(buf)))>0) {
            RNetDecodedPacket packet;
            CHECK(rnet_proto_decode(buf,(size_t)n,0x524e4554u,&packet)==0);
            if (packet.type==RNET_PKT_STATE_BEGIN && !captured_size[seat]) {
                memcpy(captured[seat],buf,(size_t)n); captured_size[seat]=n;
            }
            CHECK(rnet_transport_send(&relay[1-seat],buf,(size_t)n)>=0);
        }
        CHECK(n==0);
    }
}
static void pump(void)
{
    clock_ms++;
    forward_packets();
    rnet_session_pump(peer[0]); rnet_session_pump(peer[1]);
    forward_packets();
}
static void replay_begin(int seat)
{
    int i;
    CHECK(captured_size[seat]>0);
    CHECK(rnet_transport_send(&relay[1-seat],captured[seat],(size_t)captured_size[seat])>=0);
    for (i=0;i<4;i++) pump();
}
static void exchange(int sender,int inject_old)
{
    rnet_u8 payload[48];
    int ready[2]={0,0},i,j;
    memset(payload,sender ? 0x91 : 0x38,sizeof(payload));
    CHECK(rnet_session_state_begin(peer[sender],sender ? RNET_STATE_OP_MEMCARD : RNET_STATE_OP_SAVE,
                                  (rnet_u8)sender,payload,sizeof(payload))==0);
    /* Put the old BEGIN behind the new one on the same receiving socket. */
    if (inject_old) { pump(); replay_begin(sender); }
    for (i=0;i<2000 && (!ready[0] || !ready[1]);i++) {
        pump();
        for (j=0;j<2;j++) if (!ready[j]) {
            rnet_u8 op,slot; const void *data; size_t size;
            if (rnet_session_state_take_ready(peer[j],&op,&slot,&data,&size)) {
                CHECK(op==(sender ? RNET_STATE_OP_MEMCARD : RNET_STATE_OP_SAVE));
                CHECK(slot==sender && size==sizeof(payload) && !memcmp(data,payload,size));
                ready[j]=1;
            }
        }
    }
    CHECK(ready[0] && ready[1]);
    rnet_session_state_finish(peer[0],0); rnet_session_state_finish(peer[1],0);
}
int main(void)
{
    RNetConfig cfg; RNetHostVTable host={0};
    char bind[2][64],proxy[2][64];
    unsigned base=32000u+((unsigned)test_pid()%6000u)*4u;
    int i;
    rnet_config_init_defaults(&cfg); cfg.session_id=0x52504c59u^(unsigned)test_pid();
    host.now_ms=now_ms; host.sample_local=sample; host.publish=publish;
    for (i=0;i<2;i++) {
        cfg.local_slot=(rnet_u8)i;
        peer[i]=rnet_session_create(&cfg,&host); CHECK(peer[i]);
        snprintf(bind[i],sizeof(bind[i]),"127.0.0.1:%u",base+i);
        snprintf(proxy[i],sizeof(proxy[i]),"127.0.0.1:%u",base+2+i);
        rnet_transport_init(&relay[i]);
        CHECK(rnet_transport_start_lan(&relay[i],proxy[i],bind[i])==0);
        CHECK(rnet_session_start_lan(peer[i],bind[i],proxy[i])==0);
    }
    for (i=0;i<2000 && (!rnet_session_is_running(peer[0]) || !rnet_session_is_running(peer[1]));i++) pump();
    CHECK(rnet_session_is_running(peer[0]) && rnet_session_is_running(peer[1]));
    exchange(0,0); // host proposal
    exchange(1,0); // guest receipt must not erase the received-proposal history
    replay_begin(0);
    exchange(1,0); // buggy receiver is stuck in the resurrected host transfer
    replay_begin(1);
    exchange(0,1); // stale first proposal must not displace the second proposal
    exchange(1,1);
    for (i=0;i<2;i++) {
        rnet_session_hard_resync(peer[i]);
        replay_begin(1-i); // history also survives an input epoch reset
    }
    exchange(0,1); exchange(1,1);
    for (i=0;i<2;i++) { rnet_transport_shutdown(&relay[i]); rnet_session_destroy(peer[i]); }
    puts("state_replay_test: bidirectional completed/stale BEGINs cannot reopen or replace transfers");
    return 0;
}
