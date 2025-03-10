# sider-sha1.rb - Copyright (C) 2009 Salvatore Sanfilippo
# BSD license, See the COPYING file for more information.
#
# Performs the SHA1 sum of the whole dataset.
# This is useful to spot bugs in persistence related code and to make sure
# Slaves and Masters are in SYNC.
#
# If you hack this code make sure to sort keys and set elements as this are
# unsorted elements. Otherwise the sum may differ with equal dataset.

require 'rubygems'
require 'sider'
require 'digest/sha1'

def siderSha1(opts={})
    sha1=""
    r = Sider.new(opts)
    r.keys('*').sort.each{|k|
        vtype = r.type?(k)
        if vtype == "string"
            len = 1
            sha1 = Digest::SHA1.hexdigest(sha1+k)
            sha1 = Digest::SHA1.hexdigest(sha1+r.get(k))
        elsif vtype == "list"
            len = r.llen(k)
            if len != 0
                sha1 = Digest::SHA1.hexdigest(sha1+k)
                sha1 = Digest::SHA1.hexdigest(sha1+r.list_range(k,0,-1).join("\x01"))
            end
        elsif vtype == "set"
            len = r.scard(k)
            if len != 0
                sha1 = Digest::SHA1.hexdigest(sha1+k)
                sha1 = Digest::SHA1.hexdigest(sha1+r.set_members(k).to_a.sort.join("\x02"))
            end
        elsif vtype == "zset"
            len = r.zcard(k)
            if len != 0
                sha1 = Digest::SHA1.hexdigest(sha1+k)
                sha1 = Digest::SHA1.hexdigest(sha1+r.zrange(k,0,-1).join("\x01"))
            end
        end
        # puts "#{k} => #{sha1}" if len != 0
    }
    sha1
end

host = ARGV[0] || "127.0.0.1"
port = ARGV[1] || "6379"
db = ARGV[2] || "0"
puts "Performing SHA1 of Sider server #{host} #{port} DB: #{db}"
p "Dataset SHA1: #{siderSha1(:host => host, :port => port.to_i, :db => db)}"
