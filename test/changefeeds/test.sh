#!/bin/bash

set -e
set -o nounset

HOSTNAME=`hostname`
tag() {
    echo 1
}

QUIET=
>log.t
deeplog() {
    tag="$HOSTNAME@`date +%s.%N`"
    space=`printf "%-$1s" ""`
    shift
    { [[ -z "$@" ]] && stdbuf -o0 sed "s/^/$tag$space /" || echo "$tag$space $@"; } \
        | tee -a log.t \
        | { [[ -z $QUIET ]] && cat 1>&2 || cat 1>/dev/null; }
}
log() {
    deeplog 0 "$@"
}

abort() {
    echo "ABORTING ($@)"
    log "ABORTING ($@)"
    exit 1
}

# servers="arclight sinister"
servers="arclight sinister"
nodes_per=2
joinline=
for server in $servers; do
    for n in `seq $nodes_per`; do
        joinline+=" --join $server:$((29015+n))"
    done
done
echo "Nodes:$joinline"

id=~/.ssh/test_rsa
[[ ! -e $id ]] && abort "No file $id."
eval "`ssh-agent | head -2` 2>&1 | QUIET=1 log"
ssh-add $id 2>&1 | QUIET=1 log
run() {
    server=$1
    n=$2
    shift 2
    ssh -T -oBatchMode=yes test@$server <<EOF
mkdir -p /mnt/ssd/test/$n
cd /mnt/ssd/test/$n
`cat`
EOF
}

log "Setting up servers..."
for server in $servers; do
    for n in `seq $nodes_per`; do
        run $server $n <<EOF 2>&1 | deeplog 2 &
mkdir -p /mnt/ssd/test/$n
swapoff -a
EOF
        rsync -avz \
            ~/rethinkdb/build/debug/rethinkdb \
            ~/rethinkdb/build/debug/rethinkdb_web_assets \
            test@$server:/mnt/ssd/test/$n 2>&1 | QUIET=1 deeplog 2 &
    done
done
wait
log "Setup DONE"

log "Launching servers..."
for server in $servers; do
    for n in `seq $nodes_per`; do
        run $server $n <<EOF 2>&1 | HOSTNAME=$server.$n QUIET=1 deeplog 2 &
pwd

killall -9 rethinkdb
mv rethinkdb_data rethinkdb_data_old
rm -r rethinkdb_data_old &

sleep .5
echo ./rethinkdb -o $n --bind all $joinline
./rethinkdb -d rethinkdb_data -o $n --bind all $joinline
wait
EOF
    done
done

ruby ~/rethinkdb/test/changefeeds/setup.rb arclight 1 2>&1 | log

wait
log "DONE"