# Stack-headroom guard: the programs run with SP = 0xD000 and their BSS
# (the explorer's 800-entry listing above all) ends at __tail. Fail the
# build when less than 1 KB would be left for the stack.
# Usage: cmake -DMAP=<file.map> -DLIMIT=0xCC00 -P check_tail.cmake
file(STRINGS "${MAP}" TAIL_LINE REGEX "^__tail[ \t]+=[ \t]+\\$[0-9A-Fa-f]+")
if(NOT TAIL_LINE)
    message(FATAL_ERROR "check_tail: no __tail symbol in ${MAP}")
endif()
string(REGEX REPLACE "^__tail[ \t]+=[ \t]+\\$([0-9A-Fa-f]+).*" "\\1" TAIL_HEX "${TAIL_LINE}")
math(EXPR TAIL "0x${TAIL_HEX}")
math(EXPR LIM "${LIMIT}")
if(TAIL GREATER LIM)
    message(FATAL_ERROR "check_tail: ${MAP}: __tail 0x${TAIL_HEX} exceeds ${LIMIT} - less than 1 KB of stack left below 0xD000")
endif()
message(STATUS "check_tail: ${MAP} __tail 0x${TAIL_HEX} (limit ${LIMIT})")
