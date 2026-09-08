#!/bin/sh
# sync-to-lombard.sh -- push driver source + scripts from THIS dev Mac to the Lombard (OS X PPC target).
# Run on the DEV MAC (this repo's dir), e.g. via the `!` prefix in the Claude session:
#     !sh sync-to-lombard.sh
# It scp's the Path-B driver sources, the shared ABI headers, and the top-level *.sh helpers into
# ~/RageProLT (and the run/deploy scripts into ~) on the Lombard, using the legacy SSH algorithms the
# vintage 10.3.9 sshd needs.  (The Lombard's ~/RageProLT is a plain synced dir -- no git remote/mount.)
set -e
cd "$(dirname "$0")"

L="192.168.1.193"                       # ssh lombard
SSHOPTS="-o ConnectTimeout=10 \
 -o HostKeyAlgorithms=+ssh-rsa \
 -o PubkeyAcceptedAlgorithms=+ssh-rsa \
 -o KexAlgorithms=+diffie-hellman-group1-sha1,diffie-hellman-group14-sha1 \
 -o Ciphers=+aes128-cbc,3des-cbc \
 -o MACs=+hmac-md5,hmac-sha1"

echo "== driver sources -> lombard:~/RageProLT/gldriver =="
scp $SSHOPTS gldriver/*.c gldriver/*.h lombard:RageProLT/gldriver/

echo "== shared ABI headers -> lombard:~/RageProLT/common =="
scp $SSHOPTS common/*.h lombard:RageProLT/common/

echo "== helper scripts -> lombard:~/RageProLT and ~ =="
scp $SSHOPTS ./*.sh lombard:RageProLT/
scp $SSHOPTS ./*.sh lombard:

echo "== done.  verify a marker on the Lombard: =="
ssh $SSHOPTS lombard 'grep -c "FULLSCREEN SHRINK -> " ~/RageProLT/gldriver/gld_render.c'
