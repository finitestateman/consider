#!/bin/bash
if [ $# != "1" ]
then
    echo "Usage: ./utils/releasetools/02_upload_tarball.sh <version_tag>"
    exit 1
fi

echo "Uploading..."
scp /tmp/sider-${1}.tar.gz ubuntu@host.sider.io:/var/www/download/releases/
echo "Updating web site... "
echo "Please check the github action tests for the release."
echo "Press any key if it is a stable release, or Ctrl+C to abort"
read x
ssh ubuntu@host.sider.io "cd /var/www/download;
                          rm -rf sider-${1}.tar.gz;
                          wget http://download.sider.io/releases/sider-${1}.tar.gz;
                          tar xvzf sider-${1}.tar.gz;
                          rm -rf sider-stable;
                          mv sider-${1} sider-stable;
                          tar cvzf sider-stable.tar.gz sider-stable;
                          rm -rf sider-${1}.tar.gz;
                          shasum -a 256 sider-stable.tar.gz > sider-stable.tar.gz.SHA256SUM;
                          "
