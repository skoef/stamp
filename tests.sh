#!/usr/bin/env bats

STAMP="./stamp"
FIXTURE_PATH="tests/fixtures"

setup() {
    # create path for this test
    export STAMP_PATH=$(mktemp -d "/tmp/stamp.${BATS_TEST_NAME}.XXX")
    mkdir -p "${STAMP_PATH}"

    # possible fixtures for this test
    export FIXTURE_TXT="${FIXTURE_PATH}/${BATS_TEST_NAME}.txt"
    export FIXTURE_SUM="${FIXTURE_PATH}/${BATS_TEST_NAME}.sum"
}

@test "seting custom STAMP_PATH" {
    run ${STAMP} -p
    [ $status -eq 0 ]
    [ $lines = ${STAMP_PATH} ]
}

@test "create note" {
    run ${STAMP} -a foobar testing
    shouldbe=$(date "+1%t%Y-%m-%d%ttesting")
    run cat "${STAMP_PATH}/foobar"
    [ $shouldbe = $lines ]
}

@test "create note with custom date" {
    run ${STAMP} -a foobar testing 1970-01-01
    run cmp "${STAMP_PATH}/foobar" ${FIXTURE_TXT}
    [ $status -eq 0 ]
    run ${STAMP} -a foobar testing invaliddate
    [ $status -eq 1 ]
}

@test "delete specific note" {
    run ${STAMP} -a foobar testing1 2014-12-09
    run ${STAMP} -a foobar testing2
    run ${STAMP} -a foobar testing3 2014-12-09
    # delete existing note, should work
    run ${STAMP} -d foobar 2
    run cmp "${STAMP_PATH}/foobar" ${FIXTURE_TXT}
    [ $status -eq 0 ]
    # delete note again, should fail
    run ${STAMP} -d foobar 2
    [ $status -eq 2 ]
}

@test "delete all notes from category" {
    run ${STAMP} -a foobar testing1
    export STAMP_CONFIRM_DELETE=no
    run ${STAMP} -D foobar
    [ ! -f "${STAMP_PATH}/foobar" ]
}

@test "export notes to HTML" {
    run ${STAMP} -a foobar testing 2014-12-10
    run ${STAMP} -e foobar "${STAMP_PATH}/foobar.html"
    run cmp "${STAMP_PATH}/foobar.html" ${FIXTURE_TXT}
    [ $status -eq 0 ]
}

@test "find searching for string" {
    run ${STAMP} -a foobar testing
    run ${STAMP} -a foobar foo
    run ${STAMP} -a foobar bar
    run ${STAMP} -f foobar testing && [ $status -eq 0 ]
    run ${STAMP} -f foobar foo     && [ $status -eq 0 ]
    run ${STAMP} -f foobar tin     && [ $status -eq 0 ]
    run ${STAMP} -f foobar oba     && [ $status -eq 2 ]
}

@test "find searching for regex" {
    skip "can't find a way to actually use the regexpes correclty"
}

@test "use stdin for add note" {
    echo testing | ${STAMP} -i foobar
    shouldbe=$(date "+1%t%Y-%m-%d%ttesting")
    run cat "${STAMP_PATH}/foobar"
    [ $shouldbe = $lines ]
}

@test "show last n notes" {
    for i in {1..10}; do
        run ${STAMP} -a foobar "testing${i}"
    done
    for i in {1..10}; do
        k=$((11 - ${i}))
        shouldbe=$(date "+${k}%t%Y-%m-%d%ttesting${k}")
        run ${STAMP} -l foobar ${i}
        [ "${lines[0]}"  = ${shouldbe} ]
    done
}

@test "show categories" {
    run ${STAMP} -a foobar testing1
    run ${STAMP} -a foobar testing2
    run ${STAMP} -a barfoo testing
    run ${STAMP} -a testing testing
    run ${STAMP} -d testing 1
    run ${STAMP} -L
    [ $status -eq 0 ]
    skip "order of readdir is guaranteed"
    [ "${lines[0]}" = "barfoo (1 note)" ]
    [ "${lines[1]}" = "foobar (2 notes)" ]
    [ "${lines[2]}" = "testing (empty)" ]
}

@test "parameter checks" {
    # no arguments
    run ${STAMP} && [ $status -eq 255 ]
    # wrong option
    run ${STAMP} -X && [ $status -eq 1 ]
    # too few arguments -a
    run ${STAMP} -a        && [ $status -eq 1 ]
    run ${STAMP} -a foobar && [ $status -eq 1 ]
    # wrong date argument for -a
    run ${STAMP} -a foobar test test [ $status -eq 1 ]
    # too few arguments -d
    run ${STAMP} -d        && [ $status -eq 1 ]
    run ${STAMP} -d foobar && [ $status -eq 1 ]
    # too few arguments -D
    # run ${STAMP} -D [ $status -eq 1 ]
    # too few arguments -e
    run ${STAMP} -e        && [ $status -eq 1 ]
    run ${STAMP} -e foobar && [ $status -eq 1 ]
    # too few arguments -f
    run ${STAMP} -f        && [ $status -eq 1 ]
    run ${STAMP} -f foobar && [ $status -eq 1 ]
    # too few arguments -F
    run ${STAMP} -F        && [ $status -eq 1 ]
    run ${STAMP} -F foobar && [ $status -eq 1 ]
    # too few arguments -i
    run ${STAMP} -i && [ $status -eq 1 ]
    # too few arguments -l
    run ${STAMP} -l        && [ $status -eq 1 ]
    run ${STAMP} -l foobar && [ $status -eq 1 ]
    # too few arguments -o
    run ${STAMP} -o && [ $status -eq 1 ]
    # too few arguments -r
    run ${STAMP} -r          && [ $status -eq 1 ]
    run ${STAMP} -r foobar   && [ $status -eq 1 ]
    run ${STAMP} -r foobar 1 && [ $status -eq 1 ]
    # too few arguments -s
    run ${STAMP} -s && [ $status -eq 1 ]
}

teardown() {
    rm -r "${STAMP_PATH}"
}
