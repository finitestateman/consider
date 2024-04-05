proc sidercli_tls_config {testsdir} {
    set tlsdir [file join $testsdir tls]
    set cert [file join $tlsdir client.crt]
    set key [file join $tlsdir client.key]
    set cacert [file join $tlsdir ca.crt]

    if {$::tls} {
        return [list --tls --cert $cert --key $key --cacert $cacert]
    } else {
        return {}
    }
}

# Returns command line for executing sider-cli
proc sidercli {host port {opts {}}} {
    set cmd [list src/sider-cli -h $host -p $port]
    lappend cmd {*}[sidercli_tls_config "tests"]
    lappend cmd {*}$opts
    return $cmd
}

# Returns command line for executing sider-cli with a unix socket address
proc sidercli_unixsocket {unixsocket {opts {}}} {
    return [list src/sider-cli -s $unixsocket {*}$opts]
}

# Run sider-cli with specified args on the server of specified level.
# Returns output broken down into individual lines.
proc sidercli_exec {level args} {
    set cmd [sidercli_unixsocket [srv $level unixsocket] $args]
    set fd [open "|$cmd" "r"]
    set ret [lrange [split [read $fd] "\n"] 0 end-1]
    close $fd

    return $ret
}
