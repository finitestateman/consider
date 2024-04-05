#!/bin/bash
if [ $# != "1" ]
then
    echo "Usage: ./utils/releasetools/04_release_hash.sh <version_tag>"
    exit 1
fi

SHA=$(curl -s http://download.sider.io/releases/sider-${1}.tar.gz | shasum -a 256 | cut -f 1 -d' ')
ENTRY="hash sider-${1}.tar.gz sha256 $SHA http://download.sider.io/releases/sider-${1}.tar.gz"
echo $ENTRY >> ../sider-hashes/README
echo "Press any key to commit, Ctrl-C to abort)."
read yes
(cd ../sider-hashes; git commit -a -m "${1} hash."; git push)
