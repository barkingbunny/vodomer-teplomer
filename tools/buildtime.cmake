# Spousti se pri kazdem buildu (cmake -P): zapise cas buildu (unix, UTC) do
# hlavicky v build adresari (mimo Google Drive). Pouziva main/timebase.cpp.
string(TIMESTAMP now "%s" UTC)
file(WRITE ${OUT} "#pragma once\n#define VT_BUILD_UNIX ${now}UL\n")
