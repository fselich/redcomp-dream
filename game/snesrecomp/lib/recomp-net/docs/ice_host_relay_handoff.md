# Host-as-relay over ICE — engine wiring brief and handoff

Source of truth: this repo, branch `feat/ice-hub` (pushed). Read docs/host_integration.md (engine call order) and include/recomp_net/{session.h,lobby_client.h,host_ice.h,ice.h}.
Server half: recomp-net-server branch `feat/ice-relay` (pushed) — contract only; no engine work needed there.

## Why
Online "host as relay" used a UPnP/STUN-advertised UDP endpoint + probe, which needs a forwarded port. The new path: the host runs one libjuice agent per guest (host = answerer, guests offer), signalled over the lobby `signal` op, proof = guest `path_report direct`. The match then runs over those SAME connected agents (no renegotiation): host = hub fanning out guest datagrams to the other guests; guest = ordinary 1:1 ICE to the host. No TURN. The server relays nothing; if a guest has no ICE link, the launch is refused with a reason.

## Wire
match_caps `{"relay":"host","relay_via":"ice"}` (lobby client publishes it when rnet_lobby_relay_via_ice() is on (default ON) and the build has ICE). launch: transport "host", relay_via "ice", host_endpoint "" . Legacy launches (no relay_via) keep the old endpoint path — do not remove it.
Signals: wire type 130 + RNetSignalType, addressed with to_player_id.

## Public API
session.h:
  typedef struct RNetIceAdoptSeat { int slot; RNetIceAgent *agent; } RNetIceAdoptSeat;  /* SESSION slot 1..RNET_MAX_SLOTS-1 */
  int rnet_session_start_ice_hub_adopt(RNetSession *s, const RNetIceAdoptSeat *seats, int n); /* host */
  int rnet_session_adopt_ice_agent(RNetSession *s, RNetIceAgent *agent);                       /* guest */
  (success: session owns agents; -1: caller still owns them; on_signal may be NULL; never push signals to adopted agents)
  Also: rnet_session_start_ice_hub / rnet_session_push_signal_from (not needed by engines using the lobby handover).
lobby_client.h:
  RNetLobbyMatchCaps.relay_via_ice;  RNetLobbyJoinInfo.transport_ice_hub (1 when launch transport=="host" && relay_via=="ice"; transport_host is ALSO 1 -> test transport_ice_hub first)
  rnet_lobby_set_relay_via_ice(int) / rnet_lobby_relay_via_ice(void)
  int rnet_lobby_host_ice_status(struct RNetHostIceStatus*)   /* waiting-room UI; 1 when running */
  typedef struct RNetLobbyIceSeat { int lobby_slot; char player_id[RNET_LOBBY_ID_LEN]; struct RNetIceAgent *agent; } RNetLobbyIceSeat;
  int rnet_lobby_ice_take_hub(RNetLobbyIceSeat *out, int max);   /* host: n>=1 ownership moves to caller, or -1 */
  struct RNetIceAgent *rnet_lobby_ice_take_guest_agent(void);    /* guest */
  const char *rnet_lobby_ice_launch_error(void);  void rnet_lobby_ice_discard(void);
  Take BEFORE rnet_lobby_clear_launch_pending(). Untaken bundle destroyed after 60 s / next launch / leave. The ENGINE maps lobby_slot -> its own session slot (host_spectates offset, moved seats).
  If any seated guest (or the guest's own link) isn't COMPLETED the client refuses the launch: launch_pending stays 0, join.last_error=="ice_not_connected".
  In ICE mode bind_hostport is placeholder "0.0.0.0:0", no peer/host endpoint; bind NO UDP socket, dial nothing.
host_ice.h: RNET_LOBBY_SIG_HOSTICE_BASE=130; rnet_host_ice_{create,destroy,update,push_signal,status,take_completed,peer_completed,destroy_agent,available}. rnet_host_ice_available()==0 without RNET_ENABLE_ICE.
Env test aids: RNET_HOST_ICE_BIND, RNET_HOST_ICE_NO_STUN=1.
Spectator seats are not on the ICE path: a launch with a spectator seat is refused (ice_not_connected).

## Engine call order (host): launch parsed -> if join.transport_ice_hub: n=rnet_lobby_ice_take_hub(...); map lobby_slot->session slot; rnet_session_start_ice_hub_adopt(session, seats, n); on -1 destroy agents with rnet_host_ice_destroy_agent and report error.
## (guest): agent=rnet_lobby_ice_take_guest_agent(); rnet_session_adopt_ice_agent(session, agent).
Engines that fork the lobby client (psx, nes, genesis, older snes) must port the equivalent behaviour OR switch to recomp-net's shared rnet_lobby_* client — prefer switching to the shared client where the fork is a thin copy; report which you chose and why.

## Rules
No stubs; never edit generated output; never fake results. Real internet/NAT behaviour and gameplay verdicts belong to the user — say so in reports. Follow the workspace rules (CLAUDE.md, recomp-ai-rules/PRINCIPLES.md).

## State at handoff (2026-10-03)
Pushed feature branches (nothing merged to main):
- recomp-net `feat/ice-hub` (748f053); recomp-net-server `feat/ice-relay`; recomp-ui, snesrecomp, psxrecomp, n64lle `feat/ice-hub-relay` (RetroPortingToolKit); segagenesisrecomp, nesrecomp `feat/ice-hub-relay` (TechnicallyComputers forks). Engine branches pin recomp-net 748f053.
Verified: recomp-net 30/30 ctest (ICE on); server 195/195 cargo test. Engines: loopback only.

## Open items
1. Real NAT/internet test of a match (user) after the server branch is deployed.
2. Merge order: recomp-net -> recomp-ui + server -> engines; bump pins to the merged recomp-net commit.
3. Default lobby URL differs: genesis moved to netplay.retcomm.net, nes kept netplay.technicallycomputers.ca — pick one.
4. psx: gpu_gp0_history_guard_test fails (ws_ui_group_assign signature mismatch; not checked whether it predates the change); waiting-room RTT is unavailable in ICE mode; build needs -DPSX_NETPLAY=ON -DRNET_ENABLE_ICE=ON.
5. snesrecomp: tests/run_c_tests.sh stops at config.ini round trip (strdup under -std=c11, util.c); apply_launch never sets slot_count from player_count for ordinary 3+ seat launches.
6. nes: launcher bridge (nes_launcher_netplay.c) behind newer recomp-ui cb_create; 3/4-seat real-server runs take ~47 s (cause unknown); seats capped at 2 unless a title opts in.
7. genesis: enforced 2-seat cap (no multitap in the machine).
8. n64lle: seam only — retro-hub must run the lobby client (relay_via_ice on), call rnet_lobby_ice_take_* before clear_launch_pending, map seats, then n64lle_netplay_ice_offer_hub/_offer_guest; export the C entries from whichever artifact it links.
9. libjuice logs "Remote candidate added after remote gathering done" during normal runs (harmless so far, uninvestigated).
10. Not tested anywhere: srflx/prflx traversal, Windows ICE hub build, long waiting-room NAT-binding survival beyond the 21 s idle test, rollback over ICE.
