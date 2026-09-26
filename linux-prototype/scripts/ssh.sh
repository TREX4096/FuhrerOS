#!/bin/bash
# SSH into the running VM as root (key generated at build time).
#   ./scripts/ssh.sh [command...]
. "$(dirname "$0")/lib.sh"
if [ -t 0 ] && [ $# -eq 0 ]; then
	exec ssh -i "$FU_SSH_KEY" -p "$SSH_PORT" -o StrictHostKeyChecking=no \
		-o UserKnownHostsFile=/dev/null -o LogLevel=ERROR root@127.0.0.1
fi
fu_ssh "$@"
