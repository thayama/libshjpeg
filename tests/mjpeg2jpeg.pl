#!/usr/bin/perl
#
# Copyright 2009 IGEL Co.,Ltd.
# Copyright 2008,2009 Renesas Solutions Co.
# Copyright 2008 Denis Oliver Kropp
#
# Licensed under the Apache License, Version 2.0 (the "License");
#
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#       http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

use warnings;
use strict;
use Socket;
use FileHandle;
use Getopt::Long;

my ($arg, $ip, $sockaddr, $n, $nbyte, $nread, $data);

# init socket
my $host = '192.168.200.135';
my $port = 8080;
my $path = '/mjpeg';
my $prefix = 'jpegfile-';
my $opt_help = 0;
my $opt_verbose = 0;

GetOptions('host=s' => \$host, 'port=i' => \$port, 'prefix=s' => \$prefix,
    'help' => \$opt_help, 'verbose' => \$opt_verbose);

if ($opt_help) {
    print "Usage: $0 [<options>]\n";
    print "Connect to sighttpd and stores retrieved JPEG file.\n\n";
    print " --help            This message.\n";
    print " --verbose         Verbose output.\n";
    print " --host=<host>     Server IP address (Default: $host)\n";
    print " --port=<port>     Server port number (Default: $port)\n";
    print " --prefix=<prefix> Prefix for JPEG files (Default: '$prefix')\n\n";

    exit 0;
}

$ip = inet_aton($host) or die "can't connect to $host - $!\n";
$sockaddr = pack_sockaddr_in($port, $ip);
socket(S, PF_INET, SOCK_STREAM, 0) or die "can't open socket - $!\n";
connect(S, $sockaddr) or die "connection failed - $!\n";
my @_sockaddr = unpack_sockaddr_in(getsockname(S));
my $myip = inet_ntoa($_sockaddr[1]);
autoflush S (1);

print S "GET $path HTTP/1.1\n";
print S "Host: $myip\n";
print S "Connection: close\n\n";

$n = 0;
$| = 1;
while(<S>) {
	if (/^--\+\+\+\+\+\+/) {
		print "Found boundary.\n" if ($opt_verbose);
		$_ = <S>; 
		$_ = <S>;
		/^Content-length: (\d+)/;
		print "$1 bytes\n" if ($opt_verbose);
		$nbyte = $1;

		read S, $data, 2;
		$nread = read S, $data, $nbyte;

		print $nread . "bytes read\n" if ($opt_verbose);

		my $fn = $prefix . $n . ".jpg";

		open OUTPUT, ">", $fn or die "hoe: $!";
		binmode OUTPUT;
		syswrite OUTPUT, $data, $nbyte;
		close OUTPUT;

		print "Stored in $fn\n" if ($opt_verbose);

		$n++;
		print "+" if (!$opt_verbose);
	}
}
