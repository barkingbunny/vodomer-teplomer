# Doplni ID kanalu a Write API key ThingSpeaku do buildu (nahrada drivejsiho
# PlatformIO tools/secrets.py, chovani stejne).
#
# Hodnoty se ctou z %USERPROFILE%\.thingspeak\vodomer-teplomer.env (mimo
# repozitar, Google Drive se synchronizuje do cloudu) a zapisou se jako makra
# TS_CHANNEL_ID a TS_WRITE_KEY do hlavicky vt_secrets.h v build adresari (mimo
# Drive). NE jako -D na prikazove radce - ta se vypisuje pri kazde chybe
# prekladu a skoncila by v logu. Kdyz soubor nebo klic chybi, build skonci
# chybou - FW nikdy nezapise do ciziho kanalu.

function(vt_add_thingspeak_secrets target gen_dir)
    set(path "$ENV{USERPROFILE}/.thingspeak/vodomer-teplomer.env")
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "[secrets] chybi ${path} - viz zadani.md, Klice ThingSpeak")
    endif()

    file(STRINGS "${path}" lines)
    set(channel "")
    set(key "")
    foreach(line IN LISTS lines)
        string(STRIP "${line}" line)
        if(line MATCHES "^#" OR NOT line MATCHES "=")
            continue()
        endif()
        string(FIND "${line}" "=" eq)
        string(SUBSTRING "${line}" 0 ${eq} k)
        math(EXPR start "${eq} + 1")
        string(SUBSTRING "${line}" ${start} -1 v)
        string(STRIP "${k}" k)
        string(STRIP "${v}" v)
        string(REGEX REPLACE "^\"(.*)\"$" "\\1" v "${v}")
        if(k STREQUAL "THINGSPEAK_CHANNEL_ID")
            set(channel "${v}")
        elseif(k STREQUAL "THINGSPEAK_WRITE_API_KEY")
            set(key "${v}")
        endif()
    endforeach()

    if(NOT channel MATCHES "^[0-9]+$")
        message(FATAL_ERROR "[secrets] THINGSPEAK_CHANNEL_ID chybi nebo neni cislo")
    endif()
    if(NOT key MATCHES "^[A-Za-z0-9]+$")
        message(FATAL_ERROR "[secrets] THINGSPEAK_WRITE_API_KEY chybi nebo ma neplatne znaky")
    endif()

    # Zapsat jen pri zmene - jinak by se upload.cpp prekladal pri kazdem configure.
    set(content "#pragma once\n#define TS_CHANNEL_ID ${channel}\n#define TS_WRITE_KEY \"${key}\"\n")
    set(out "${gen_dir}/vt_secrets.h")
    if(EXISTS "${out}")
        file(READ "${out}" old)
    else()
        set(old "")
    endif()
    if(NOT old STREQUAL content)
        file(WRITE "${out}" "${content}")
    endif()
    target_include_directories(${target} PRIVATE "${gen_dir}")
    message(STATUS "[secrets] ThingSpeak kanal a Write key doplneny (vt_secrets.h)")
endfunction()
