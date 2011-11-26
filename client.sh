#!/usr/bin/zsh

unset GLOG_logtostderr

nclients=${1:-1}

if [ -e stop ] ; then
  rm stop
fi

for i in {1..${nclients}}
do
  nice ./client --redis-host ${GPS_REDIS_HOST:?GPS_REDIS_HOST not found} \
                --redis-port ${GPS_REDIS_PORT:?GPS_REDIS_PORT not found} \
                --redis-password ${GPS_REDIS_PASSWORD:?GPS_REDIS_PASSWORD not found} \
                -v 0 \
                --depth 1600 &
done
