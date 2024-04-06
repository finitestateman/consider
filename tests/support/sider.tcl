# Tcl client library - used by the Sider test
# Copyright (C) 2009-2014 Salvatore Sanfilippo
# Released under the BSD license like Sider itself
#
# Example usage:
#
# set r [sider 127.0.0.1 6379]
# $r lpush mylist foo
# $r lpush mylist bar
# $r lrange mylist 0 -1
# $r close
#
# Non blocking usage example:
#
# proc handlePong {r type reply} {
#     puts "PONG $type '$reply'"
#     if {$reply ne "PONG"} {
#         $r ping [list handlePong]
#     }
# }
#
# set r [sider]
# $r blocking 0
# $r get fo [list handlePong]
#
# vwait forever

package require Tcl 8.5
package provide sider 0.1

source [file join [file dirname [info script]] "response_transformers.tcl"]

namespace eval sider {}
set ::sider::id 0
array set ::sider::fd {}
array set ::sider::addr {}
array set ::sider::blocking {}
array set ::sider::deferred {}
array set ::sider::readraw {}
array set ::sider::attributes {} ;# Holds the RESP3 attributes from the last call
array set ::sider::reconnect {}
array set ::sider::tls {}
array set ::sider::callback {}
array set ::sider::state {} ;# State in non-blocking reply reading
array set ::sider::statestack {} ;# Stack of states, for nested mbulks
array set ::sider::curr_argv {} ;# Remember the current argv, to be used in response_transformers.tcl
array set ::sider::testing_resp3 {} ;# Indicating if the current client is using RESP3 (only if the test is trying to test RESP3 specific behavior. It won't be on in case of force_resp3)

set ::force_resp3 0
set ::log_req_res 0

proc sider {{server 127.0.0.1} {port 6379} {defer 0} {tls 0} {tlsoptions {}} {readraw 0}} {
    if {$tls} {
        package require tls
        ::tls::init \
            -cafile "$::tlsdir/ca.crt" \
            -certfile "$::tlsdir/client.crt" \
            -keyfile "$::tlsdir/client.key" \
            {*}$tlsoptions
        set fd [::tls::socket $server $port]
    } else {
        set fd [socket $server $port]
    }
    fconfigure $fd -translation binary
    set id [incr ::sider::id]
    set ::sider::fd($id) $fd
    set ::sider::addr($id) [list $server $port]
    set ::sider::blocking($id) 1
    set ::sider::deferred($id) $defer
    set ::sider::readraw($id) $readraw
    set ::sider::reconnect($id) 0
    set ::sider::curr_argv($id) 0
    set ::sider::testing_resp3($id) 0
    set ::sider::tls($id) $tls
    ::sider::sider_reset_state $id
    interp alias {} ::sider::siderHandle$id {} ::sider::__dispatch__ $id
}

# On recent versions of tcl-tls/OpenSSL, reading from a dropped connection
# results with an error we need to catch and mimic the old behavior.
proc ::sider::sider_safe_read {fd len} {
    if {$len == -1} {
        set err [catch {set val [read $fd]} msg]
    } else {
        set err [catch {set val [read $fd $len]} msg]
    }
    if {!$err} {
        return $val
    }
    if {[string match "*connection abort*" $msg]} {
        return {}
    }
    error $msg
}

proc ::sider::sider_safe_gets {fd} {
    if {[catch {set val [gets $fd]} msg]} {
        if {[string match "*connection abort*" $msg]} {
            return {}
        }
        error $msg
    }
    return $val
}

# This is a wrapper to the actual dispatching procedure that handles
# reconnection if needed.
proc ::sider::__dispatch__ {id method args} {
    set errorcode [catch {::sider::__dispatch__raw__ $id $method $args} retval]
    if {$errorcode && $::sider::reconnect($id) && $::sider::fd($id) eq {}} {
        # Try again if the connection was lost.
        # FIXME: we don't re-select the previously selected DB, nor we check
        # if we are inside a transaction that needs to be re-issued from
        # scratch.
        set errorcode [catch {::sider::__dispatch__raw__ $id $method $args} retval]
    }
    return -code $errorcode $retval
}

proc ::sider::__dispatch__raw__ {id method argv} {
    set fd $::sider::fd($id)

    # Reconnect the link if needed.
    if {$fd eq {} && $method ne {close}} {
        lassign $::sider::addr($id) host port
        if {$::sider::tls($id)} {
            set ::sider::fd($id) [::tls::socket $host $port]
        } else {
            set ::sider::fd($id) [socket $host $port]
        }
        fconfigure $::sider::fd($id) -translation binary
        set fd $::sider::fd($id)
    }

    # Transform HELLO 2 to HELLO 3 if force_resp3
    # All set the connection var testing_resp3 in case of HELLO 3
    if {[llength $argv] > 0 && [string compare -nocase $method "HELLO"] == 0} {
        if {[lindex $argv 0] == 3} {
            set ::sider::testing_resp3($id) 1
        } else {
            set ::sider::testing_resp3($id) 0
            if {$::force_resp3} {
                # If we are in force_resp3 we run HELLO 3 instead of HELLO 2
                lset argv 0 3
            }
        }
    }

    set blocking $::sider::blocking($id)
    set deferred $::sider::deferred($id)
    if {$blocking == 0} {
        if {[llength $argv] == 0} {
            error "Please provide a callback in non-blocking mode"
        }
        set callback [lindex $argv end]
        set argv [lrange $argv 0 end-1]
    }
    if {[info command ::sider::__method__$method] eq {}} {
        catch {unset ::sider::attributes($id)}
        set cmd "*[expr {[llength $argv]+1}]\r\n"
        append cmd "$[string length $method]\r\n$method\r\n"
        foreach a $argv {
            append cmd "$[string length $a]\r\n$a\r\n"
        }
        ::sider::sider_write $fd $cmd
        if {[catch {flush $fd}]} {
            catch {close $fd}
            set ::sider::fd($id) {}
            return -code error "I/O error reading reply"
        }

        set ::sider::curr_argv($id) [concat $method $argv]
        if {!$deferred} {
            if {$blocking} {
                ::sider::sider_read_reply $id $fd
            } else {
                # Every well formed reply read will pop an element from this
                # list and use it as a callback. So pipelining is supported
                # in non blocking mode.
                lappend ::sider::callback($id) $callback
                fileevent $fd readable [list ::sider::sider_readable $fd $id]
            }
        }
    } else {
        uplevel 1 [list ::sider::__method__$method $id $fd] $argv
    }
}

proc ::sider::__method__blocking {id fd val} {
    set ::sider::blocking($id) $val
    fconfigure $fd -blocking $val
}

proc ::sider::__method__reconnect {id fd val} {
    set ::sider::reconnect($id) $val
}

proc ::sider::__method__read {id fd} {
    ::sider::sider_read_reply $id $fd
}

proc ::sider::__method__rawread {id fd {len -1}} {
    return [sider_safe_read $fd $len]
}

proc ::sider::__method__write {id fd buf} {
    ::sider::sider_write $fd $buf
}

proc ::sider::__method__flush {id fd} {
    flush $fd
}

proc ::sider::__method__close {id fd} {
    catch {close $fd}
    catch {unset ::sider::fd($id)}
    catch {unset ::sider::addr($id)}
    catch {unset ::sider::blocking($id)}
    catch {unset ::sider::deferred($id)}
    catch {unset ::sider::readraw($id)}
    catch {unset ::sider::attributes($id)}
    catch {unset ::sider::reconnect($id)}
    catch {unset ::sider::tls($id)}
    catch {unset ::sider::state($id)}
    catch {unset ::sider::statestack($id)}
    catch {unset ::sider::callback($id)}
    catch {unset ::sider::curr_argv($id)}
    catch {unset ::sider::testing_resp3($id)}
    catch {interp alias {} ::sider::siderHandle$id {}}
}

proc ::sider::__method__channel {id fd} {
    return $fd
}

proc ::sider::__method__deferred {id fd val} {
    set ::sider::deferred($id) $val
}

proc ::sider::__method__readraw {id fd val} {
    set ::sider::readraw($id) $val
}

proc ::sider::__method__readingraw {id fd} {
    return $::sider::readraw($id)
}

proc ::sider::__method__attributes {id fd} {
    set _ $::sider::attributes($id)
}

proc ::sider::sider_write {fd buf} {
    puts -nonewline $fd $buf
}

proc ::sider::sider_writenl {fd buf} {
    sider_write $fd $buf
    sider_write $fd "\r\n"
    flush $fd
}

proc ::sider::sider_readnl {fd len} {
    set buf [sider_safe_read $fd $len]
    sider_safe_read $fd 2 ; # discard CR LF
    return $buf
}

proc ::sider::sider_bulk_read {fd} {
    set count [sider_read_line $fd]
    if {$count == -1} return {}
    set buf [sider_readnl $fd $count]
    return $buf
}

proc ::sider::sider_multi_bulk_read {id fd} {
    set count [sider_read_line $fd]
    if {$count == -1} return {}
    set l {}
    set err {}
    for {set i 0} {$i < $count} {incr i} {
        if {[catch {
            lappend l [sider_read_reply_logic $id $fd]
        } e] && $err eq {}} {
            set err $e
        }
    }
    if {$err ne {}} {return -code error $err}
    return $l
}

proc ::sider::sider_read_map {id fd} {
    set count [sider_read_line $fd]
    if {$count == -1} return {}
    set d {}
    set err {}
    for {set i 0} {$i < $count} {incr i} {
        if {[catch {
            set k [sider_read_reply_logic $id $fd] ; # key
            set v [sider_read_reply_logic $id $fd] ; # value
            dict set d $k $v
        } e] && $err eq {}} {
            set err $e
        }
    }
    if {$err ne {}} {return -code error $err}
    return $d
}

proc ::sider::sider_read_line fd {
    string trim [sider_safe_gets $fd]
}

proc ::sider::sider_read_null fd {
    sider_safe_gets $fd
    return {}
}

proc ::sider::sider_read_bool fd {
    set v [sider_read_line $fd]
    if {$v == "t"} {return 1}
    if {$v == "f"} {return 0}
    return -code error "Bad protocol, '$v' as bool type"
}

proc ::sider::sider_read_double {id fd} {
    set v [sider_read_line $fd]
    # unlike many other DTs, there is a textual difference between double and a string with the same value,
    # so we need to transform to double if we are testing RESP3 (i.e. some tests check that a
    # double reply is "1.0" and not "1")
    if {[should_transform_to_resp2 $id]} {
        return $v
    } else {
        return [expr {double($v)}]
    }
}

proc ::sider::sider_read_verbatim_str fd {
    set v [sider_bulk_read $fd]
    # strip the first 4 chars ("txt:")
    return [string range $v 4 end]
}

proc ::sider::sider_read_reply_logic {id fd} {
    if {$::sider::readraw($id)} {
        return [sider_read_line $fd]
    }

    while {1} {
        set type [sider_safe_read $fd 1]
        switch -exact -- $type {
            _ {return [sider_read_null $fd]}
            : -
            ( -
            + {return [sider_read_line $fd]}
            , {return [sider_read_double $id $fd]}
            # {return [sider_read_bool $fd]}
            = {return [sider_read_verbatim_str $fd]}
            - {return -code error [sider_read_line $fd]}
            $ {return [sider_bulk_read $fd]}
            > -
            ~ -
            * {return [sider_multi_bulk_read $id $fd]}
            % {return [sider_read_map $id $fd]}
            | {
                set attrib [sider_read_map $id $fd]
                set ::sider::attributes($id) $attrib
                continue
            }
            default {
                if {$type eq {}} {
                    catch {close $fd}
                    set ::sider::fd($id) {}
                    return -code error "I/O error reading reply"
                }
                return -code error "Bad protocol, '$type' as reply type byte"
            }
        }
    }
}

proc ::sider::sider_read_reply {id fd} {
    set response [sider_read_reply_logic $id $fd]
    ::response_transformers::transform_response_if_needed $id $::sider::curr_argv($id) $response
}

proc ::sider::sider_reset_state id {
    set ::sider::state($id) [dict create buf {} mbulk -1 bulk -1 reply {}]
    set ::sider::statestack($id) {}
}

proc ::sider::sider_call_callback {id type reply} {
    set cb [lindex $::sider::callback($id) 0]
    set ::sider::callback($id) [lrange $::sider::callback($id) 1 end]
    uplevel #0 $cb [list ::sider::siderHandle$id $type $reply]
    ::sider::sider_reset_state $id
}

# Read a reply in non-blocking mode.
proc ::sider::sider_readable {fd id} {
    if {[eof $fd]} {
        sider_call_callback $id eof {}
        ::sider::__method__close $id $fd
        return
    }
    if {[dict get $::sider::state($id) bulk] == -1} {
        set line [gets $fd]
        if {$line eq {}} return ;# No complete line available, return
        switch -exact -- [string index $line 0] {
            : -
            + {sider_call_callback $id reply [string range $line 1 end-1]}
            - {sider_call_callback $id err [string range $line 1 end-1]}
            ( {sider_call_callback $id reply [string range $line 1 end-1]}
            $ {
                dict set ::sider::state($id) bulk \
                    [expr [string range $line 1 end-1]+2]
                if {[dict get $::sider::state($id) bulk] == 1} {
                    # We got a $-1, hack the state to play well with this.
                    dict set ::sider::state($id) bulk 2
                    dict set ::sider::state($id) buf "\r\n"
                    ::sider::sider_readable $fd $id
                }
            }
            * {
                dict set ::sider::state($id) mbulk [string range $line 1 end-1]
                # Handle *-1
                if {[dict get $::sider::state($id) mbulk] == -1} {
                    sider_call_callback $id reply {}
                }
            }
            default {
                sider_call_callback $id err \
                    "Bad protocol, $type as reply type byte"
            }
        }
    } else {
        set totlen [dict get $::sider::state($id) bulk]
        set buflen [string length [dict get $::sider::state($id) buf]]
        set toread [expr {$totlen-$buflen}]
        set data [read $fd $toread]
        set nread [string length $data]
        dict append ::sider::state($id) buf $data
        # Check if we read a complete bulk reply
        if {[string length [dict get $::sider::state($id) buf]] ==
            [dict get $::sider::state($id) bulk]} {
            if {[dict get $::sider::state($id) mbulk] == -1} {
                sider_call_callback $id reply \
                    [string range [dict get $::sider::state($id) buf] 0 end-2]
            } else {
                dict with ::sider::state($id) {
                    lappend reply [string range $buf 0 end-2]
                    incr mbulk -1
                    set bulk -1
                }
                if {[dict get $::sider::state($id) mbulk] == 0} {
                    sider_call_callback $id reply \
                        [dict get $::sider::state($id) reply]
                }
            }
        }
    }
}

# when forcing resp3 some tests that rely on resp2 can fail, so we have to translate the resp3 response to resp2
proc ::sider::should_transform_to_resp2 {id} {
    return [expr {$::force_resp3 && !$::sider::testing_resp3($id)}]
}
