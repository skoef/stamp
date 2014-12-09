#!/usr/bin/env bats

STAMP="./stamp"
FIXTURE_PATH="tests/fixtures"

setup() {
    # create path for this test
    export STAMP_PATH=$(mktemp -d /tmp/stamp.${BATS_TEST_NAME}.XXX)
    mkdir -p ${STAMP_PATH}

    # possible fixtures for this test
    export FIXTURE_TXT=${FIXTURE_PATH}/${BATS_TEST_NAME}.txt
    export FIXTURE_SUM=${FIXTURE_PATH}/${BATS_TEST_NAME}.sum
}

@test "create note" {
    run ${STAMP} -a foobar testing
    shouldbe=$(date "+1%t%Y-%m-%d%ttesting")
    run cat ${STAMP_PATH}/foobar
    [ $shouldbe = $lines ]
}

@test "create note with custom date" {
    run ${STAMP} -a foobar testing 1970-01-01
    run cmp ${STAMP_PATH}/foobar ${FIXTURE_TXT}
    [ $status -eq 0 ]
}

@test "delete specific note" {
    run ${STAMP} -a foobar testing1 2014-12-09
    run ${STAMP} -a foobar testing2
    run ${STAMP} -a foobar testing3 2014-12-09
    run ${STAMP} -d foobar 2
    run cmp ${STAMP_PATH}/foobar ${FIXTURE_TXT}
    [ $status -eq 0 ]
}

@test "delete all notes from category" {
    skip "no way to confirm from stdin yet"
    run ${STAMP} -a foobar testing1
    run ${STAMP} -D foobar
    [ -f ${STAMP_PATH}/foobar ] && [ ! -s ${STAMP_PATH}/foobar ]
}

teardown() {
    return
    rm -r ${STAMP_PATH}
}
