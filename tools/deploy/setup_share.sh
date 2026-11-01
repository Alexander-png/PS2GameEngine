#!/bin/sh
# One-time setup of the SMB share /srv/ps2engine for the PS2 (see docs/smb-share.md). Uses sudo; read before running.
# Does not touch /etc/samba/smb.conf: add the sections from docs/samba/smb.conf.example yourself.
set -e
SHARE=${1:-/srv/ps2engine}
sudo groupadd -f ps2deploy
id ps2deploy >/dev/null 2>&1 || sudo useradd -M -s /usr/bin/nologin -g ps2deploy ps2deploy
sudo usermod -aG ps2deploy "$USER"
sudo mkdir -p "$SHARE/logs"
sudo chown -R "$USER":ps2deploy "$SHARE"
sudo chmod 2750 "$SHARE"
sudo chown ps2deploy:ps2deploy "$SHARE/logs"
sudo chmod 2775 "$SHARE/logs"
echo "Now set the share password:  sudo smbpasswd -a ps2deploy"
echo "Then:                        sudo systemctl enable --now smb nmb   (and log in again for the group change)"
