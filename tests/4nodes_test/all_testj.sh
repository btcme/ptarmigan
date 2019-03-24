#!/bin/bash -ue

function check_live() {
	echo check proc count start
	PROC_COUNT=`ps -C ptarmd | grep ptarmd | wc -l`
	test $PROC_COUNT == 4
	echo check proc count end
}

function check_log() {
    echo check log
    BAD_LINE_COUNT=`../../tools/log_filter.sh | wc -l`
    if [ $BAD_LINE_COUNT -ne 0 ];
    then
        ../../tools/log_filter.sh
        exit 1
    fi
    echo check log end
}

START=`date +%s`

echo clean start
./clean.sh >/dev/null 2>&1 | :
echo clean end

echo st1 start
./example_st1.sh
echo st1 end

echo st2 start
./example_st2.sh
sleep 5 # wait conf file
echo st2 end

echo st3 start
./example_st3j.sh
sleep 5 # XXX: TODO
echo st3 end

check_live
check_log

echo st4c start
./example_st4c.sh
sleep 5 # XXX: TODO
echo st4c end

check_live
check_log

FEERATE_PER_KW=600
echo update_fee $FEERATE_PER_KW start
./example_st_update_fee_all.sh $FEERATE_PER_KW
sleep 5 # XXX: TODO
echo update_fee $FEERATE_PER_KW end

check_live
check_log

echo st4d start
./example_st4d.sh
sleep 5 # XXX: TODO
echo st4d end

check_live
check_log

echo st4e start
./example_st4e.sh
sleep 5 # XXX: TODO
echo st4e end

check_live
check_log

echo st4f start
./example_st4f.sh
sleep 5 # XXX: TODO
echo st4f end

check_live
check_log

echo disconnect start
./example_st_quit.sh
sleep 5
echo disconnect end

echo clear skip route
for i in 3333 4444 5555 6666
do
    ./routing -d ./node_$i -c
done

echo reconnect start
./example_st_conn.sh
sleep 5
echo reconnect end

check_live
check_log

echo st4c start
./example_st4c.sh
sleep 5 # XXX: TODO
echo st4c end

check_live
check_log

echo st4d start
./example_st4d.sh
sleep 5 # XXX: TODO
echo st4d end

check_live
check_log

FEERATE_PER_KW=700
echo update_fee $FEERATE_PER_KW start
./example_st_update_fee_all.sh $FEERATE_PER_KW
sleep 5 # XXX: TODO
echo update_fee $FEERATE_PER_KW end

check_live
check_log

echo st4e start
./example_st4e.sh
sleep 5 # XXX: TODO
echo st4e end

check_live
check_log

echo st4f start
./example_st4f.sh
sleep 5 # XXX: TODO
echo st4f end

check_live
check_log

echo st5 start
./example_st5.sh
sleep 5 # XXX: TODO
echo st5 end

check_live
check_log

echo clean start
./clean.sh
echo clean end

END=`date +%s`
ELAPSED=`expr $END - $START` 
echo "$ELAPSED seconds"