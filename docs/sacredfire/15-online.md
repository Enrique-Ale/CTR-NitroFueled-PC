# Online play with a private server (work in progress, 2026-10-13)

CTR's online play is Activision's Demonware behind a PSN sign-in. The official servers are not
an option and never will be for this project: the port only talks to a **private server** the
player names, and it is **off by default**.

## Rules

- Off unless `online_server` (bbport.ini) or `BB_ONLINE_SERVER` names a server. Offline,
  `runtime_net_resolve` answers nothing and the offline contracts of `runtime_services.c` stay:
  cable unplugged, signed out.
- Every host name the game resolves answers the private server's address. The only real name
  lookup is the private server's own name, once at start.
- Sockets connect and send only to the private server, loopback and `online_allow` addresses.
  Anything else is refused (`ENETUNREACH` / `EHOSTUNREACH`) and logged.
- PSN is simulated for the private server only: no Sony account, no real credentials.
  Authorization codes and ID tokens are made up and mean nothing outside the private server.
- Own code, GPL-2.0. Prelude/Nextendo (PolyForm Shield) is not used; shadPS4's network
  libraries (GPL-2.0) are the reference for the PS4 contracts.

## Settings

| bbport.ini | Environment | Meaning |
|---|---|---|
| `online_server` | `BB_ONLINE_SERVER` | private server, name or IPv4 (empty: offline) |
| `online_http_port` | `BB_ONLINE_HTTP_PORT` | where HTTP(S) requests go (8080) |
| `online_id` | `BB_ONLINE_ID` | PSN online id (else `BB_USER_NAME`, else Player1); 3-16 of `A-Z a-z 0-9 - _` |
| | `BB_ONLINE_ACCOUNT_ID` | account id (else a hash of the online id) |
| `online_allow` | `BB_ONLINE_ALLOW` | more IPv4 addresses the game may reach (peers, later) |
| | `BB_ONLINE_TRACE=1` | hex dump of every packet, not only each socket's first |

## What runs (src/runtime_net.c)

- **libSceNet**: real sockets (Winsock / BSD), TCP and UDP (P2P types as plain UDP/TCP, the
  virtual port ignored), PS4 sockaddr layout, errors `0x80410100 | errno` in BSD numbering,
  socket options mapped (non-blocking `SO_NBIO`, timeouts in microseconds), `MSG_DONTWAIT` on
  blocking sockets, epoll (timeouts in microseconds, sockets and asynchronous resolvers),
  resolver (every name: the private server).
- **libSceNetCtl**: wired, address obtained (state 3); the local address that reaches the
  server; the private server as DNS; registered callbacks hear `IPOBTAINED` once.
- **libSceHttp**: requests go as plain HTTP/1.1 to the private server's HTTP port with the
  original `Host`, `X-SacredFire-Scheme: https|http` and `Connection: close`; the whole reply is
  read in `sceHttpSendRequest` (chunked decoded), so non-blocking users see each request ready
  once on their HTTP epoll.
- **NpManager / NpAuth**: user 1 signed in (`sceNpGetState` 2), online id and account id, PS
  Plus granted, no parental restrictions, state callbacks told "signed in" once; authorization
  codes `SACREDFIRE-<online id>-<account>` (issuer 256) and ID tokens
  `sacredfire.<online id>.<account>`, with the client id and scope the game asks for logged.
- **NpWebApi** (NpToolkit2 calls it once signed in: friends, profiles, sessions): a request to
  API group G and path P is `https://G.np.community.playstation.net/P`, sent like any other
  HTTPS request to the private server (Sony is never contacted); replies of 400 and above
  become `0x82f00000 | status` with the error body in the response information, as on the
  console.
- **POSIX sockets** (`socket`, `connect`, `select`, ...): the same sockets, -1 and errno.
- PSN services the private server does not offer yet (NpScore, NpTus, NpTss, NpWordFilter,
  NpMatching2, NpBandwidthTest, NpInGameMessage, NpSnsFacebook) fail like an unavailable
  service, cleanup calls succeed, one log line per service: the game goes on instead of
  stopping on an unknown import.

## First run (2026-10-13, discovery server on 127.0.0.1)

- UDP socket bound to port 3074; `stun.us.demonware.net` resolved (NAT discovery).
- SSDP to 239.255.255.250:1900 (UPnP, "DemonwarePortMapping"): refused, as everything outside
  the private server.
- NpToolkit2, once signed in, called `sceNpWebApiCreateContextA` (not implemented then: the
  game stopped; implemented since).
- PSN ID token asked for client `94b26225-0bc0-4d10-a8a8-0e4d94381dfc`, scope
  `openid,openid:duid`.
- **Demonware authentication** (bdHTTPPS4 thread): `POST
  https://lavender-ps4-auth3.prod.demonware.net:443/auth/`, `User-Agent: SCE-PS4`,
  `Content-Type: application/json`, `X-TransactionID`, body
  `{"auth_task":"46","iv_seed":"...","title_id":"5760","identity":"...","extra_data":"{\"version\":\"0\",\"token\":\"{\\\"scope\\\":\\\"openid,openid:duid\\\",\\\"id_token\\\":\\\"<PSN ID token>\\\"}\",\"extended_data\":true}"}`.
  Demonware's title id for CTR is 5760; the PSN ID token is what the official server checked
  with Sony, so a private server accepts whatever it likes.

The game's strings for Demonware are obfuscated in the eboot (a scan finds only the UPnP
template "DemonwarePortMapping"), so its hosts, ports and messages are learned at run time.

## Discovery (step 3)

```
python tools/online/discovery_server.py            # HTTP 8080, TCP 3074-3080 and 443, UDP 3074-3080
set BB_ONLINE_SERVER=127.0.0.1
```

The server only listens and logs (console and `online-discovery.log`); HTTP gets 404. The game
log shows the other side: `Net: resolve`, `Net: socket`, `Net: connect`, the first bytes of each
socket, `Net: HTTP`, `Net: PSN: authorization code for client ...`.

## Plan

1. Network layer (this file) — done, tests/test_net.c.
2. PSN simulated — done for NpManager/NpAuth; WebApi when the game needs it.
3. Discovery server — done; next: a run with the game.
4. Reverse engineering of the Demonware client (Ghidra): authentication, lobby, the signature
   and certificate checks; a patch active only with online on.
5. Matchmaking and race traffic: two instances on one PC, then LAN, then internet.
6. Launcher: an *Online (private server)* tab, off by default.
