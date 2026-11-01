# SMB share for the console

The PS2 network stack (ps2sdk `smbman`) only speaks old SMB1 with NTLMv1, so the share needs deliberately weak
settings. **Use a dedicated user and a trusted LAN (or an isolated VM network only).** Do not expose this to
untrusted networks.

## Samba configuration

`docs/samba/smb.conf.example` (copy the sections into `/etc/samba/smb.conf`):

```ini
[global]
    workgroup = WORKGROUP
    server role = standalone server
    security = user
    server min protocol = NT1
    server max protocol = NT1
    server signing = disabled
    smb encrypt = disabled
    ntlm auth = ntlmv1-permitted
    log file = /var/log/samba/%m.log
    log level = 1

[ps2engine]
    comment = PS2GameEngine builds and logs
    path = /srv/ps2engine
    browseable = yes
    read only = no
    guest ok = no
    valid users = ps2deploy
```

## One-time setup

`tools/deploy/setup_share.sh` does the following (read it before running it, it uses sudo; it was written down from the working setup rather than run on a clean machine):

```sh
sudo groupadd -f ps2deploy
sudo useradd -M -s /usr/bin/nologin -g ps2deploy ps2deploy         # share user, no login
sudo usermod -aG ps2deploy "$USER"                                # your account must write to the share too
sudo mkdir -p /srv/ps2engine/logs
sudo chown -R "$USER":ps2deploy /srv/ps2engine
sudo chmod 2750 /srv/ps2engine                                    # setgid: new files keep the group
sudo chown ps2deploy:ps2deploy /srv/ps2engine/logs && sudo chmod 2775 /srv/ps2engine/logs   # the console uploads logs here
sudo smbpasswd -a ps2deploy                                       # choose a password; it goes into PS2Deployer's build
sudo systemctl enable --now smb nmb
```

Log in again so the new group applies to your shell. Test from another machine:
`smbclient -m NT1 //<ip>/ps2engine -U ps2deploy -c ls`.

## Network

- The VM must use a bridged adapter (a NAT adapter hides it from the console).
- PS2 side: the network adapter's IP settings come from the HDD network config; give the VM a fixed IP or a DHCP reservation, since the address is compiled into PS2Deployer.
- Firewall: allow TCP 139 and 445.

## Troubleshooting

- `NT_STATUS_...` on the console only: check `/var/log/samba/<client>.log`, make sure NT1 and ntlmv1 are on (newer Samba disables them by default).
- Logs not appearing: `logs/` must be writable by `ps2deploy` (setgid group dir as above).
- Share lists nothing on the console: `mkmanifest.py` writes `manifest.txt`; run the deploy script again.
