proc siderbenchmark_tls_config {testsdir} {
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

proc siderbenchmark {host port {opts {}}} {
    set cmd [list src/sider-benchmark -h $host -p $port]
    lappend cmd {*}[siderbenchmark_tls_config "tests"]
    lappend cmd {*}$opts
    return $cmd
}

proc siderbenchmarkuri {host port {opts {}}} {
    set cmd [list src/sider-benchmark -u sider://$host:$port]
    lappend cmd {*}[siderbenchmark_tls_config "tests"]
    lappend cmd {*}$opts
    return $cmd
}

proc siderbenchmarkuriuserpass {host port user pass {opts {}}} {
    set cmd [list src/sider-benchmark -u sider://$user:$pass@$host:$port]
    lappend cmd {*}[siderbenchmark_tls_config "tests"]
    lappend cmd {*}$opts
    return $cmd
}
