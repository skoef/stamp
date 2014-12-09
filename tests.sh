#!/usr/bin/env bats

STAMP="./stamp"

setup() {
    export STAMP_PATH=$(mktemp -d /tmp/stamp.XXX)
}

@test "create note category" {
    run ${STAMP} -a foobar mekker
    [ -f ${STAMP_PATH}/foobar ]
}

@test "create note with custom date" {
    run ${STAMP} -a foobar mekker 1970-01-01
    sum=$(md5 -q ${STAMP_PATH}/foobar)
    [ "${sum}" = "8fe3b94dec1806a22d1308824be398fb" ]
}

teardown() {
    rm -r ${STAMP_PATH}
}
