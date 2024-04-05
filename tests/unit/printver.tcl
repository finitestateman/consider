start_server {} {
    set i [r info]
    regexp {sider_version:(.*?)\r\n} $i - version
    regexp {sider_git_sha1:(.*?)\r\n} $i - sha1
    puts "Testing Sider version $version ($sha1)"
}
