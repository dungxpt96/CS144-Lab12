*** Setting ***
Library    ${CURDIR}${/}../lab12/py-lab12-test.py

*** Variables ***
${ret}    nothing
${true}=    Convert To Boolean    True

*** Test Cases ***
Client sends data
    ${ret}=    Test Case    num_testcase=1
    Should Be Equal    ${ret}    ${true}
Client receives data
    ${ret}=    Test Case    num_testcase=2
    Should Be Equal    ${ret}    ${true}
Correct checksum
    ${ret}=    Test Case    num_testcase=3
    Should Be Equal    ${ret}    ${true}
Correct header fields
    ${ret}=    Test Case    num_testcase=4
    Should Be Equal    ${ret}    ${true}
Bidirectionally transfer data
    ${ret}=    Test Case    num_testcase=5
    Should Be Equal    ${ret}    ${true}
Handles data larger than window size
    ${ret}=    Test Case    num_testcase=6
    Should Be Equal    ${ret}    ${true}
Handles segment corruption
    ${ret}=    Test Case    num_testcase=7
    Should Be Equal    ${ret}    ${true}
Handles segment drops
    ${ret}=    Test Case    num_testcase=8
    Should Be Equal    ${ret}    ${true}
Handles segment delay
    ${ret}=    Test Case    num_testcase=9
    Should Be Equal    ${ret}    ${true}
Handles duplicate segments
    ${ret}=    Test Case    num_testcase=10
    Should Be Equal    ${ret}    ${true}
Handles truncated segments
    ${ret}=    Test Case    num_testcase=11
    Should Be Equal    ${ret}    ${true}
Sends FIN when reading in EOF
    ${ret}=    Test Case    num_testcase=12
    Should Be Equal    ${ret}    ${true}
Tears down connection
    ${ret}=    Test Case    num_testcase=13
    Should Be Equal    ${ret}    ${true}
Handles sliding window
    ${ret}=    Test Case    num_testcase=14
    Should Be Equal    ${ret}    ${true}
