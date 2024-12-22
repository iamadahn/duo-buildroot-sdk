#!/bin/sh

docker start duodocker
docker exec -it duodocker /bin/bash -c "cd /home/work && cat /etc/issue && ./build.sh ${1}"
