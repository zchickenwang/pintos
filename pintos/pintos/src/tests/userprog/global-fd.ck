# -*- perl -*-
use strict;
use warnings;
use tests::tests;
check_expected ([<<'EOF']);
(global-fd) begin
(child-fd1) run
child-fd1: exit(13)
(child-fd2) run
(child-fd2) -1
child-fd2: exit(13)
(global-fd) end
global-fd: exit(0)
EOF
pass;
