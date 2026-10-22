#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
/* Host relay: the pure encoders and parsers (recomp_net/host_relay.h), the
 * probe round trip on loopback, and the orchestration's two lobby ops. No
 * router or STUN server is asked (RNET_HOST_RELAY_ENDPOINT / _NO_ROUTER). */
#include "recomp_net/host_relay.h"
#include "platform/rnet_platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

static void expect_true(int condition, const char *message)
{
    if (!condition) {
        fprintf(stderr, "FAIL: %s\n", message);
        failures++;
    }
}

static void test_ssdp(void)
{
    char out[256];
    expect_true(rnet_hr_ssdp_location("HTTP/1.1 200 OK\r\nCACHE-CONTROL: max-age=120\r\n"
                                      "Location:  http://192.168.1.1:5000/rootDesc.xml \r\n"
                                      "ST: upnp:rootdevice\r\n\r\n", out, sizeof(out)) &&
                strcmp(out, "http://192.168.1.1:5000/rootDesc.xml") == 0,
                "ssdp LOCATION is found case-insensitively and trimmed");
    expect_true(!rnet_hr_ssdp_location("HTTP/1.1 200 OK\r\nST: x\r\n\r\n", out, sizeof(out)),
                "no LOCATION -> 0");
}

static void test_xml(void)
{
    char out[128];
    expect_true(rnet_hr_xml_text("<a><u:NewExternalIPAddress>1.2.3.4</u:NewExternalIPAddress></a>",
                                 "NewExternalIPAddress", out, sizeof(out)) &&
                strcmp(out, "1.2.3.4") == 0, "namespaced tag text");
    expect_true(rnet_hr_xml_text("<x><URLBase>http://h/</URLBase></x>", "URLBase", out, sizeof(out)) &&
                strcmp(out, "http://h/") == 0, "plain tag text");
    expect_true(!rnet_hr_xml_text("<x><serviceType>a</serviceType></x>", "controlURL", out, sizeof(out)),
                "absent tag -> 0");
}

static void test_urls(void)
{
    char out[256];
    rnet_hr_absolute_url("/ctl", "http://192.168.1.1:5000/desc/root.xml", out, sizeof(out));
    expect_true(strcmp(out, "http://192.168.1.1:5000/ctl") == 0, "root-relative ref");
    rnet_hr_absolute_url("ctl", "http://192.168.1.1:5000/desc/root.xml", out, sizeof(out));
    expect_true(strcmp(out, "http://192.168.1.1:5000/desc/ctl") == 0, "relative ref");
    rnet_hr_absolute_url("http://x/y", "http://z/", out, sizeof(out));
    expect_true(strcmp(out, "http://x/y") == 0, "absolute ref kept");
}

static void test_igd(void)
{
    static const char xml[] =
        "<root><URLBase>http://10.0.0.1:1900/</URLBase><device><serviceList>"
        "<service><serviceType>urn:schemas-upnp-org:service:Layer3Forwarding:1</serviceType>"
        "<controlURL>/l3f</controlURL></service>"
        "<service><serviceType>urn:schemas-upnp-org:service:WANIPConnection:1</serviceType>"
        "<controlURL>/ctl/IPConn</controlURL></service>"
        "</serviceList></device></root>";
    char ctl[256], type[128];
    expect_true(rnet_hr_igd_control_url(xml, "http://10.0.0.1:5000/root.xml", ctl, sizeof(ctl),
                                        type, sizeof(type)),
                "WANIPConnection found");
    expect_true(strcmp(ctl, "http://10.0.0.1:1900/ctl/IPConn") == 0, "control URL made absolute against URLBase");
    expect_true(strcmp(type, "urn:schemas-upnp-org:service:WANIPConnection:1") == 0, "service type");
    expect_true(!rnet_hr_igd_control_url("<root><device></device></root>", "http://h/", ctl,
                                         sizeof(ctl), type, sizeof(type)),
                "no WAN service -> 0");
}

static void test_soap(void)
{
    const char *kv[] = { "NewRemoteHost", "", "NewExternalPort", "7777" };
    char out[1024];
    expect_true(rnet_hr_soap_body("urn:s:WANIPConnection:1", "AddPortMapping", kv, 2, out, sizeof(out)),
                "soap body builds");
    expect_true(strstr(out, "<u:AddPortMapping xmlns:u=\"urn:s:WANIPConnection:1\">"
                            "<NewRemoteHost></NewRemoteHost><NewExternalPort>7777</NewExternalPort>"
                            "</u:AddPortMapping>") != NULL,
                "soap body carries the action and its arguments in order");
    expect_true(!rnet_hr_soap_body("t", "a", kv, 2, out, 40), "too small -> 0");
}

static void test_natpmp(void)
{
    rnet_u8 req[12];
    rnet_u8 map_ok[16] = { 0, 129, 0, 0, 0, 0, 0, 1, 0x1E, 0x61, 0x1E, 0x62, 0, 0, 0x1C, 0x20 };
    rnet_u8 map_err[16] = { 0, 129, 0, 2, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0 };
    rnet_u8 addr[12] = { 0, 128, 0, 0, 0, 0, 0, 1, 203, 0, 113, 9 };
    unsigned short ext = 0, rc = 0;
    char ip[32];
    rnet_hr_natpmp_mapping_request(req, 7777, 7777, 7200);
    expect_true(req[1] == 1 && req[4] == 0x1E && req[5] == 0x61 && req[6] == 0x1E && req[7] == 0x61 &&
                req[8] == 0 && req[9] == 0 && req[10] == 0x1C && req[11] == 0x20,
                "mapping request encodes UDP op, ports and lifetime big-endian");
    expect_true(rnet_hr_natpmp_parse_mapping(map_ok, sizeof(map_ok), &ext, &rc) && ext == 7778 && rc == 0,
                "mapping response external port");
    expect_true(!rnet_hr_natpmp_parse_mapping(map_err, sizeof(map_err), &ext, &rc) && rc == 2,
                "mapping error result code surfaces");
    expect_true(rnet_hr_natpmp_parse_address(addr, sizeof(addr), ip, sizeof(ip)) &&
                strcmp(ip, "203.0.113.9") == 0, "address response");
    expect_true(!rnet_hr_natpmp_parse_address(addr, 8, ip, sizeof(ip)), "short address response -> 0");
}

static void test_route(void)
{
    char out[32];
    expect_true(rnet_hr_gateway_from_route(
                    "Iface\tDestination\tGateway \tFlags\tRefCnt\tUse\tMetric\tMask\n"
                    "eth0\t00006FC0\t00000000\t0001\t0\t0\t0\t00FFFFFF\n"
                    "eth0\t00000000\t0101A8C0\t0003\t0\t0\t100\t00000000\n",
                    out, sizeof(out)) && strcmp(out, "192.168.1.1") == 0,
                "default gateway from /proc/net/route (little-endian hex)");
    expect_true(!rnet_hr_gateway_from_route("Iface\n", out, sizeof(out)), "no default route -> 0");
}

static void test_http_split(void)
{
    static const char raw[] = "HTTP/1.1 200 OK\r\nContent-Length: 3\r\n\r\nabc";
    int status = 0;
    const char *body = NULL;
    size_t body_len = 0;
    expect_true(rnet_hr_http_split(raw, sizeof(raw) - 1, &status, &body, &body_len) &&
                status == 200 && body_len == 3 && memcmp(body, "abc", 3) == 0,
                "status and body split");
    expect_true(!rnet_hr_http_split("nope", 4, &status, &body, &body_len), "not HTTP -> 0");
}

/* A loopback host port answers a loopback probe; an unanswered probe fails. */
static void test_probe_roundtrip(void)
{
    RNetHostPort *hp = NULL;
    RNetHostProbe *probe = NULL;
    RNetHostPortStatus st;
    char ep[64];
    int r = 0, i;
    rnet_os_startup();
#ifdef _WIN32
    _putenv("RNET_HOST_RELAY_ENDPOINT=127.0.0.1:1");
#else
    setenv("RNET_HOST_RELAY_ENDPOINT", "127.0.0.1:1", 1);
#endif
    expect_true(rnet_host_port_open(&hp, 0, NULL, 0) == 0 && hp, "host port binds a free port");
    rnet_host_port_status(hp, &st);
    expect_true(st.done && strcmp(st.how, "env") == 0 && st.local_port >= 7777,
                "env override finishes discovery at once");
    snprintf(ep, sizeof(ep), "127.0.0.1:%u", (unsigned)st.local_port);
    expect_true(rnet_host_probe_open(&probe, ep, 3) == 0, "probe opens");
    for (i = 0; i < 300 && r == 0; ++i) {
        rnet_host_port_pump(hp);
        r = rnet_host_probe_pump(probe);
        if (r == 0) rnet_os_sleep_micros(5000);
    }
    expect_true(r == 1, "probe is answered by the host port");
    rnet_host_port_status(hp, &st);
    expect_true(st.probes_answered >= 1, "host counts the answered probe");
    rnet_host_probe_close(&probe);
    rnet_host_port_release(&hp);
    expect_true(hp == NULL, "release clears the handle");

    /* Nothing listens here: every try goes unanswered. */
    expect_true(rnet_host_probe_open(&probe, ep, 2) == 0, "probe to a closed port opens");
    r = 0;
    for (i = 0; i < 600 && r == 0; ++i) {
        r = rnet_host_probe_pump(probe);
        if (r == 0) rnet_os_sleep_micros(5000);
    }
    expect_true(r == -1, "unanswered probe fails after its tries");
    rnet_host_probe_close(&probe);
}

static char g_sent[8][256];
static int g_sent_n;
static int capture_send(const char *json, void *ctx)
{
    (void)ctx;
    if (g_sent_n < 8) snprintf(g_sent[g_sent_n], sizeof(g_sent[0]), "%s", json);
    g_sent_n++;
    return 0;
}

/* The orchestration: a host advertises once; a guest reports direct. */
static void test_orchestration(void)
{
    RNetHostRelay *host = rnet_host_relay_create();
    RNetHostRelay *guest = rnet_host_relay_create();
    RNetHostRelayView hv, gv;
    RNetHostRelayStatus hs, gs;
    char ep[64];
    int i;
    memset(&hv, 0, sizeof(hv));
    hv.active = 1;
    hv.is_host = 1;
    hv.bind_port = 0; /* first free */
    hv.send_json = capture_send;
    g_sent_n = 0;
    for (i = 0; i < 20; ++i) rnet_host_relay_update(host, &hv);
    rnet_host_relay_status(host, &hs);
    expect_true(hs.role == 1 && hs.port.done, "host role, discovery done");
    expect_true(g_sent_n == 1 && strstr(g_sent[0], "\"op\":\"set_host_endpoint\"") &&
                strstr(g_sent[0], "127.0.0.1:1"),
                "host advertises its endpoint exactly once");
    expect_true(strcmp(hs.advertised, "127.0.0.1:1") == 0, "advertised recorded");

    /* The guest probes the port the host actually holds (the env override
     * only names what is advertised). */
    snprintf(ep, sizeof(ep), "127.0.0.1:%u", (unsigned)hs.port.local_port);
    memset(&gv, 0, sizeof(gv));
    gv.active = 1;
    gv.is_host = 0;
    gv.host_endpoint = ep;
    gv.send_json = capture_send;
    g_sent_n = 0;
    for (i = 0; i < 400 && g_sent_n == 0; ++i) {
        rnet_host_relay_update(host, &hv);
        rnet_host_relay_update(guest, &gv);
        rnet_os_sleep_micros(5000);
    }
    rnet_host_relay_status(guest, &gs);
    expect_true(g_sent_n == 1 && strstr(g_sent[0], "\"op\":\"path_report\"") &&
                strstr(g_sent[0], "\"direct\""),
                "guest reports path direct after the host answers");
    expect_true(gs.role == 2 && strcmp(gs.last_report, "direct") == 0 && !gs.probing,
                "guest status records the report");
    /* Steady state: no second report until the refresh interval. */
    for (i = 0; i < 20; ++i) rnet_host_relay_update(guest, &gv);
    expect_true(g_sent_n == 1, "no repeated report within the refresh window");

    /* The server dropped our report (a roster change): proven again at once,
     * not at the refresh. */
    rnet_host_relay_path_cleared(guest);
    rnet_host_relay_status(guest, &gs);
    expect_true(gs.last_report[0] == '\0', "a cleared report is forgotten");
    for (i = 0; i < 400 && g_sent_n == 1; ++i) {
        rnet_host_relay_update(host, &hv);
        rnet_host_relay_update(guest, &gv);
        rnet_os_sleep_micros(5000);
    }
    expect_true(g_sent_n == 2 && strstr(g_sent[1], "\"direct\""),
                "the guest reports direct again after the server cleared it");
    rnet_host_relay_path_cleared(host);
    rnet_host_relay_status(host, &hs);
    expect_true(hs.role == 1, "path_cleared is a no-op for the host");

    /* The host releases the port for the game: the mapping/advert stays known. */
    rnet_host_relay_release_port(host);
    rnet_host_relay_status(host, &hs);
    expect_true(strcmp(hs.advertised, "127.0.0.1:1") == 0, "release keeps the advert");
    /* Inactive view holds nothing. */
    hv.active = 0;
    rnet_host_relay_update(host, &hv);
    rnet_host_relay_status(host, &hs);
    expect_true(hs.role == 0, "inactive -> idle");
    rnet_host_relay_destroy(&host);
    rnet_host_relay_destroy(&guest);
    expect_true(host == NULL && guest == NULL, "destroy clears handles");
}

int main(void)
{
    test_ssdp();
    test_xml();
    test_urls();
    test_igd();
    test_soap();
    test_natpmp();
    test_route();
    test_http_split();
    test_probe_roundtrip();
    test_orchestration();
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("host_relay_test: ok\n");
    return 0;
}
