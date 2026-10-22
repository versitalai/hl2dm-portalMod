HL2DM Portal - dedicated server

Players need the mod installed (install.bat / install.sh). Servers can send
maps and models, never code, so the server can't install it for them.

Your existing HL2DM server keeps running as it is. These scripts add the mod to
the same server folder and set up a second server on port 27016.

Which OS is my server?
  Look in the server folder (the one with the hl2mp folder):
    srcds_run / srcds_linux        -> Linux:   server/install_server.sh
    srcds.exe / srcds_win64.exe    -> Windows: server\install_server.bat
  Rented from a host with a web panel? Upload the hl2dm_portal folder next to
  hl2mp, replace hl2dm_portal/gameinfo.txt with server/gameinfo_server.txt,
  and ask the host to run the server with "-game hl2dm_portal". Linux hosts
  also need the steps in install_server.sh (64-bit launcher and links).

Linux:
  server/install_server.sh /path/to/server [--port 27016] [--map dm_lockdown]
  /path/to/server/start_portal_server.sh

Windows (tested on Linux only so far):
  server\install_server.bat "C:\path\to\server" [port]
  start_portal_server.bat in the server folder

Then open UDP 27016 in the firewall / router, and put settings (hostname,
password, rcon_password) in hl2dm_portal/cfg/server.cfg.

Players join with the console: connect <address>:27016
Or: write the address (e.g. 203.0.113.5:27016) as the only line of server.txt
in the release folder before you share it. install.bat then also makes a
"Join HL2DM Portal server" shortcut.
