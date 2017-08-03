#!/bin/bash

count="$1"

#set -x

broker_address=http://eff-t-cnt7-02:8080/conn
#broker_address=http://localhost:8080/conn

mkdir testrun
for i in {1..3}
do
    
    mkdir testrun/load_test_pair_$i
    cp load_test_responder testrun/load_test_pair_$i
    cp load_test_requester testrun/load_test_pair_$i

    echo "Creating subscription path /downstream/responder${i}/rng"
    echo -n "/downstream/responder${i}/rng" > requester$i.requestpath

    ./testrun/load_test_pair_$i/load_test_requester --broker=${broker_address} --name=requester$i &> requester_$i.out &
    sleep 1
    ./testrun/load_test_pair_$i/load_test_responder --broker=${broker_address} --name=responder$i &> responder_$i.out &
done

read -p "Press key to stop the test... " -n1 -s

rm requester*.requestpath
rm -rf testrun

killall -9 load_test_responder
killall -9 load_test_requester